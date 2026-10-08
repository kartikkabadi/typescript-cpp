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


static std::pair<std::shared_ptr<fourslash::FourslashTest>, std::function<void()>> newContentMapperFourslash(gostd::testing::T* t, std::string content, std::string mapper, const std::vector<std::string>& extensions) {
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

// findAllReferencesDynamicImport1_test.go

// findAllReferencesDynamicImport1_test.go
static void TestFindAllReferencesDynamicImport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: foo.ts
export function foo() { return "foo"; }
/*1*/import("/*2*/./foo")
/*3*/var x = import("/*4*/./foo"))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesDynamicImport1, TestFindAllReferencesDynamicImport1);

// findAllReferencesDynamicImport2_test.go

// findAllReferencesDynamicImport2_test.go
static void TestFindAllReferencesDynamicImport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
[|export function /*1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}bar|]() { return "bar"; }|]
var x = import("./foo");
x.then(foo => {
    foo./*2*/[|bar|]();
}))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"bar"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesDynamicImport2, TestFindAllReferencesDynamicImport2);

// findAllReferencesDynamicImport3_test.go

// findAllReferencesDynamicImport3_test.go
static void TestFindAllReferencesDynamicImport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
[|export function /*0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}bar|]() { return "bar"; }|]
import('./foo').then(([|{ /*1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}bar|] }|]) => undefined);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3]});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesDynamicImport3, TestFindAllReferencesDynamicImport3);

// findAllReferencesFromLinkTagReference1_test.go

// findAllReferencesFromLinkTagReference1_test.go
static void TestFindAllReferencesFromLinkTagReference1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    /** {@link /**/A} */
    A
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesFromLinkTagReference1, TestFindAllReferencesFromLinkTagReference1);

// findAllReferencesFromLinkTagReference2_test.go

// findAllReferencesFromLinkTagReference2_test.go
static void TestFindAllReferencesFromLinkTagReference2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
enum E {
    /** {@link /**/Foo} */
    Foo
}
interface Foo {
    foo: E.Foo;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesFromLinkTagReference2, TestFindAllReferencesFromLinkTagReference2);

// findAllReferencesFromLinkTagReference3_test.go

// findAllReferencesFromLinkTagReference3_test.go
static void TestFindAllReferencesFromLinkTagReference3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: a.ts
interface Foo {
    foo: E.Foo;
}
// @Filename: b.ts
enum E {
    /** {@link /**/Foo} */
    Foo
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesFromLinkTagReference3, TestFindAllReferencesFromLinkTagReference3);

// findAllReferencesFromLinkTagReference4_test.go

// findAllReferencesFromLinkTagReference4_test.go
static void TestFindAllReferencesFromLinkTagReference4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    /** {@link /**/B} */
    A,
    B
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesFromLinkTagReference4, TestFindAllReferencesFromLinkTagReference4);

// findAllReferencesFromLinkTagReference5_test.go

// findAllReferencesFromLinkTagReference5_test.go
static void TestFindAllReferencesFromLinkTagReference5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    /** {@link E./**/A} */
    A
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesFromLinkTagReference5, TestFindAllReferencesFromLinkTagReference5);

// findAllReferencesJSDocFunctionNew_test.go

// findAllReferencesJSDocFunctionNew_test.go
static void TestFindAllReferencesJSDocFunctionNew(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/** @type {function (/*1*/new: string, string): string} */
var f;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesJSDocFunctionNew, TestFindAllReferencesJSDocFunctionNew);

// findAllReferencesJSDocFunctionThis_test.go

// findAllReferencesJSDocFunctionThis_test.go
static void TestFindAllReferencesJSDocFunctionThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/** @type {function (this: string, string): string} */
var f = function (s) { return /*0*/this + s; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesJSDocFunctionThis, TestFindAllReferencesJSDocFunctionThis);

// findAllReferencesJsDocTypeLiteral_test.go

// findAllReferencesJsDocTypeLiteral_test.go
static void TestFindAllReferencesJsDocTypeLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: foo.js
/**
 * @param {object} o - very important!
 * @param {string} o.x - a thing, its ok
 * @param {number} o.y - another thing
 * @param {Object} o.nested - very nested
 * @param {boolean} o.nested./*1*/great - much greatness
 * @param {number} o.nested.times - twice? probably!??
 */
 function f(o) { return o.nested./*2*/great; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesJsDocTypeLiteral, TestFindAllReferencesJsDocTypeLiteral);

// findAllReferencesJsOverloadedFunctionParameter_test.go

// findAllReferencesJsOverloadedFunctionParameter_test.go
static void TestFindAllReferencesJsOverloadedFunctionParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: foo.js
/**
 * @overload
 * @param {number} x
 * @returns {number}
 *
 * @overload
 * @param {string} x
 * @returns {string} 
 *
 * @param {unknown} x
 * @returns {unknown} 
 */
function foo(x/*1*/) {
  return x;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesJsOverloadedFunctionParameter, TestFindAllReferencesJsOverloadedFunctionParameter);

// findAllReferencesJsRequireDestructuring1_test.go

// findAllReferencesJsRequireDestructuring1_test.go
static void TestFindAllReferencesJsRequireDestructuring1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @noEmit: true
// @checkJs: true
// @Filename: /X.js
module.exports = { x: 1 };
// @Filename: /Y.js
const { /*1*/x: { y } } = require("./X");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesJsRequireDestructuring1, TestFindAllReferencesJsRequireDestructuring1);

// findAllReferencesJsRequireDestructuring_test.go

// findAllReferencesJsRequireDestructuring_test.go
static void TestFindAllReferencesJsRequireDestructuring(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @noEmit: true
// @checkJs: true
// @Filename: foo.js
module.exports = {
    foo: '1'
};
// @Filename: bar.js
const { /*1*/foo: bar } = require('./foo');)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesJsRequireDestructuring, TestFindAllReferencesJsRequireDestructuring);

// findAllReferencesLinkTag1_test.go

// findAllReferencesLinkTag1_test.go
static void TestFindAllReferencesLinkTag1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C/*7*/ {
    m/*1*/() { }
    n/*2*/ = 1
    static s/*3*/() { }
    /**
     * {@link m}
     * @see {m}
     * {@link C.m}
     * @see {C.m}
     * {@link C#m}
     * @see {C#m}
     * {@link C.prototype.m}
     * @see {C.prototype.m}
     */
    p() { }
    /**
     * {@link n}
     * @see {n}
     * {@link C.n}
     * @see {C.n}
     * {@link C#n}
     * @see {C#n}
     * {@link C.prototype.n}
     * @see {C.prototype.n}
     */
    q() { }
    /**
     * {@link s}
     * @see {s}
     * {@link C.s}
     * @see {C.s}
     */
    r() { }
}

interface I/*8*/ {
    a/*4*/()
    b/*5*/: 1
    /**
     * {@link a}
     * @see {a}
     * {@link I.a}
     * @see {I.a}
     * {@link I#a}
     * @see {I#a}
     */
    c()
    /**
     * {@link b}
     * @see {b}
     * {@link I.b}
     * @see {I.b}
     */
    d()
}

function nestor() {
    /** {@link r2} */
    function ref() { }
    /** @see {r2} */
    function d3() { }
    function r2/*6*/() { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesLinkTag1, TestFindAllReferencesLinkTag1);

// findAllReferencesLinkTag2_test.go

// findAllReferencesLinkTag2_test.go
static void TestFindAllReferencesLinkTag2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace NPR/*5*/ {
    export class Consider/*4*/ {
        This/*3*/ = class {
            show/*2*/() { }
        }
        m/*1*/() { }
    }
    /**
     * @see {Consider.prototype.m}
     * {@link Consider#m}
     * @see {Consider#This#show}
     * {@link Consider.This.show}
     * @see {NPR.Consider#This#show}
     * {@link NPR.Consider.This#show}
     * @see {NPR.Consider#This.show} # doesn't parse trailing .
     * @see {NPR.Consider.This.show}
     */
    export function ref() { }
}
/**
 * {@link NPR.Consider#This#show hello hello}
 * {@link NPR.Consider.This#show}
 * @see {NPR.Consider#This.show} # doesn't parse trailing .
 * @see {NPR.Consider.This.show}
 */
export function outerref() { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesLinkTag2, TestFindAllReferencesLinkTag2);

// findAllReferencesLinkTag3_test.go

// findAllReferencesLinkTag3_test.go
static void TestFindAllReferencesLinkTag3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace NPR/*5*/ {
    export class Consider/*4*/ {
        This/*3*/ = class {
            show/*2*/() { }
        }
        m/*1*/() { }
    }
    /**
     * {@linkcode Consider.prototype.m}
     * {@linkplain Consider#m}
     * {@linkcode Consider#This#show}
     * {@linkplain Consider.This.show}
     * {@linkcode NPR.Consider#This#show}
     * {@linkplain NPR.Consider.This#show}
     * {@linkcode NPR.Consider#This.show} # doesn't parse trailing .
     * {@linkcode NPR.Consider.This.show}
     */
    export function ref() { }
}
/**
 * {@linkplain NPR.Consider#This#show hello hello}
 * {@linkplain NPR.Consider.This#show}
 * {@linkcode NPR.Consider#This.show} # doesn't parse trailing .
 * {@linkcode NPR.Consider.This.show}
 */
export function outerref() { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesLinkTag3, TestFindAllReferencesLinkTag3);

// findAllReferencesNonExistentExportBinding_test.go

// findAllReferencesNonExistentExportBinding_test.go
static void TestFindAllReferencesNonExistentExportBinding(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
 { "compilerOptions": { "module": "commonjs" } }
// @filename: /bar.ts
import { Foo/**/ } from "./foo";
// @filename: /foo.ts
export { Foo })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesNonExistentExportBinding, TestFindAllReferencesNonExistentExportBinding);

// findAllReferencesOfConstructor_badOverload_test.go

// findAllReferencesOfConstructor_badOverload_test.go
static void TestFindAllReferencesOfConstructor_badOverload(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    /*1*/constructor(n: number);
    /*2*/constructor(){}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesOfConstructor_badOverload, TestFindAllReferencesOfConstructor_badOverload);

// findAllReferencesOfConstructor_test.go

// findAllReferencesOfConstructor_test.go
static void TestFindAllReferencesOfConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
export class C {
    /*0*/constructor(n: number);
    /*1*/constructor();
    /*2*/constructor(n?: number){}
    static f() {
        this.f();
        new this();
    }
}
new C();
const D = C;
new D();
// @Filename: b.ts
import { C } from "./a";
new C();
// @Filename: c.ts
import { C } from "./a";
class D extends C {
    constructor() {
        super();
        super.method();
    }
    method() { super(); }
}
class E implements C {
    constructor() { super(); }
}
// @Filename: d.ts
import * as a from "./a";
new a.C();
class d extends a.C { constructor() { super(); })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesOfConstructor, TestFindAllReferencesOfConstructor);

// findAllReferencesOfJsonModule_test.go

// findAllReferencesOfJsonModule_test.go
static void TestFindAllReferencesOfJsonModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @resolveJsonModule: true
// @module: commonjs
// @esModuleInterop: true
// @Filename: /foo.ts
/*1*/import /*2*/settings from "./settings.json";
/*3*/settings;
// @Filename: /settings.json
 {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesOfJsonModule, TestFindAllReferencesOfJsonModule);

// findAllReferencesTripleSlash_test.go

// findAllReferencesTripleSlash_test.go
static void TestFindAllReferencesTripleSlash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @Filename: /node_modules/@types/globals/index.d.ts
declare const someAmbientGlobal: unknown;
// @Filename: /a.ts
/// <reference path="b.ts/*1*/" />
/// <reference types="globals/*2*/" />
// @Filename: /b.ts
console.log("b.ts");
// @Filename: /c.js
require("./b");
require("globals");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesTripleSlash, TestFindAllReferencesTripleSlash);

// findAllReferencesUmdModuleAsGlobalConst_test.go

// findAllReferencesUmdModuleAsGlobalConst_test.go
static void TestFindAllReferencesUmdModuleAsGlobalConst(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/@types/three/three-core.d.ts
export class Vector3 {
    constructor(x?: number, y?: number, z?: number);
    x: number;
    y: number;
}
// @Filename: /node_modules/@types/three/index.d.ts
export * from "./three-core";
export as namespace /*0*/THREE;
// @Filename: /typings/global.d.ts
import * as _THREE from '/*1*/three';
declare global {
    const /*2*/THREE: typeof _THREE;
}
// @Filename: /src/index.ts
export const a = {};
let v = new /*3*/THREE.Vector2();
// @Filename: /tsconfig.json
{
    "compilerOptions": {
        "esModuleInterop": true,
        "outDir": "./build/js/",
        "noImplicitAny": true,
        "module": "es6",
        "target": "es6",
        "allowJs": true,
        "skipLibCheck": true,
        "lib": ["es2016", "dom"],
        "typeRoots": ["node_modules/@types/"],
        "types": ["three"]
 	},
    "files": ["/src/index.ts", "typings/global.d.ts"]
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesUmdModuleAsGlobalConst, TestFindAllReferencesUmdModuleAsGlobalConst);

// findAllRefsCatchClause_test.go

// findAllRefsCatchClause_test.go
static void TestFindAllRefsCatchClause(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(try { }
catch (/*1*/err) {
    /*2*/err;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsCatchClause, TestFindAllRefsCatchClause);

// findAllRefsClassExpression0_test.go

// findAllRefsClassExpression0_test.go
static void TestFindAllRefsClassExpression0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export = class /*0*/A {
    m() { /*1*/A; }
};
// @Filename: /b.ts
import /*2*/A = require("./a");
/*3*/A;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsClassExpression0, TestFindAllRefsClassExpression0);

// findAllRefsClassExpression1_test.go

// findAllRefsClassExpression1_test.go
static void TestFindAllRefsClassExpression1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
module.exports = class /*0*/A {};
// @Filename: /b.js
import /*1*/A = require("./a");
/*2*/A;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsClassExpression1, TestFindAllRefsClassExpression1);

// findAllRefsClassExpression2_test.go

// findAllRefsClassExpression2_test.go
static void TestFindAllRefsClassExpression2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
exports./*0*/A = class {};
// @Filename: /b.js
import { /*1*/A } from "./a";
/*2*/A;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsClassExpression2, TestFindAllRefsClassExpression2);

// findAllRefsClassStaticBlocks_test.go

// findAllRefsClassStaticBlocks_test.go
static void TestFindAllRefsClassStaticBlocks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class ClassStaticBocks {
    static x;
    [|[|/*classStaticBocks1*/static|] {}|]
    static y;
    [|[|/*classStaticBocks2*/static|] {}|]
    static y;
    [|[|/*classStaticBocks3*/static|] {}|]
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"classStaticBocks1", "classStaticBocks2", "classStaticBocks3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsClassStaticBlocks, TestFindAllRefsClassStaticBlocks);

// findAllRefsClassWithStaticThisAccess_test.go

// findAllRefsClassWithStaticThisAccess_test.go
static void TestFindAllRefsClassWithStaticThisAccess(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|class /*0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}C|] {
    static s() {
        /*1*/[|this|];
    }
    static get f() {
        return /*2*/[|this|];

        function inner() { this; }
        class Inner { x = this; }
    }
}|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1]});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsClassWithStaticThisAccess, TestFindAllRefsClassWithStaticThisAccess);

// findAllRefsCommonJsRequire2_test.go

// findAllRefsCommonJsRequire2_test.go
static void TestFindAllRefsCommonJsRequire2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
function f() { }
module.exports.f = f
// @Filename: /b.js
const { f } = require('./a')
/**/f)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsCommonJsRequire2, TestFindAllRefsCommonJsRequire2);

// findAllRefsCommonJsRequire3_test.go

// findAllRefsCommonJsRequire3_test.go
static void TestFindAllRefsCommonJsRequire3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
function f() { }
module.exports = { f }
// @Filename: /b.js
const { f } = require('./a')
/**/f)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsCommonJsRequire3, TestFindAllRefsCommonJsRequire3);

// findAllRefsCommonJsRequire_test.go

// findAllRefsCommonJsRequire_test.go
static void TestFindAllRefsCommonJsRequire(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
function f() { }
export { f }
// @Filename: /b.js
const { f } = require('./a')
/**/f)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsCommonJsRequire, TestFindAllRefsCommonJsRequire);

// findAllRefsConst_test.go

// findAllRefsConst_test.go
static void TestFindAllRefsConst(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
/**/const const
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsConst, TestFindAllRefsConst);

// findAllRefsConstructorFunctions_test.go

// findAllRefsConstructorFunctions_test.go
static void TestFindAllRefsConstructorFunctions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
function f() {
    /*1*/this./*2*/x = 0;
}
f.prototype.setX = function() {
    /*3*/this./*4*/x = 1;
}
f.prototype.useX = function() { this./*5*/x; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsConstructorFunctions, TestFindAllRefsConstructorFunctions);

// findAllRefsDeclareClass_test.go

// findAllRefsDeclareClass_test.go
static void TestFindAllRefsDeclareClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/declare class /*2*/C {
    static m(): void;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsDeclareClass, TestFindAllRefsDeclareClass);

// findAllRefsDefaultImport_test.go

// findAllRefsDefaultImport_test.go
static void TestFindAllRefsDefaultImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export default function /*0*/a() {}
// @Filename: /b.ts
import /*1*/a, * as ns from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsDefaultImport, TestFindAllRefsDefaultImport);

// findAllRefsDestructureGeneric_test.go

// findAllRefsDestructureGeneric_test.go
static void TestFindAllRefsDestructureGeneric(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I<T> {
    /*0*/x: boolean;
}
declare const i: I<number>;
const { /*1*/x } = i;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsDestructureGeneric, TestFindAllRefsDestructureGeneric);

// findAllRefsDestructureGetter_test.go

// findAllRefsDestructureGetter_test.go
static void TestFindAllRefsDestructureGetter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Test {
    get /*x0*/x() { return 0; }

    set /*y0*/y(a: number) {}
}
const { /*x1*/x, /*y1*/y } = new Test();
/*x2*/x; /*y2*/y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"x0", "x1", "x2", "y0", "y1", "y2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsDestructureGetter, TestFindAllRefsDestructureGetter);

// findAllRefsExportAsNamespace_test.go

// findAllRefsExportAsNamespace_test.go
static void TestFindAllRefsExportAsNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/a/index.d.ts
export function /*0*/f(): void;
export as namespace A;
// @Filename: /b.ts
import { /*1*/f } from "a";
// @Filename: /c.ts
A./*2*/f();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsExportAsNamespace, TestFindAllRefsExportAsNamespace);

// findAllRefsExportConstEqualToClass_test.go

// findAllRefsExportConstEqualToClass_test.go
static void TestFindAllRefsExportConstEqualToClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
class C {}
export const /*0*/D = C;
// @Filename: /b.ts
import { /*1*/D } from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsExportConstEqualToClass, TestFindAllRefsExportConstEqualToClass);

// findAllRefsExportDefaultClassConstructor_test.go

// findAllRefsExportDefaultClassConstructor_test.go
static void TestFindAllRefsExportDefaultClassConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export default class {
    /*1*/constructor() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsExportDefaultClassConstructor, TestFindAllRefsExportDefaultClassConstructor);

// findAllRefsExportEquals_test.go

// findAllRefsExportEquals_test.go
static void TestFindAllRefsExportEquals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
type /*0*/T = number;
/*1*/export = /*2*/T;
// @Filename: /b.ts
import /*3*/T = require("/*4*/./a");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsExportEquals, TestFindAllRefsExportEquals);

// findAllRefsExportNotAtTopLevel_test.go

// findAllRefsExportNotAtTopLevel_test.go
static void TestFindAllRefsExportNotAtTopLevel(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS({
    /*1*/export const /*2*/x = 0;
    /*3*/x;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsExportNotAtTopLevel, TestFindAllRefsExportNotAtTopLevel);

// findAllRefsExportStringRename_test.go

// findAllRefsExportStringRename_test.go
static void TestFindAllRefsExportStringRename(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const foo = 123;
export { foo as /**/"bar" };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsExportStringRename, TestFindAllRefsExportStringRename);

// findAllRefsForComputedProperties2_test.go

// findAllRefsForComputedProperties2_test.go
static void TestFindAllRefsForComputedProperties2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    [/*1*/42](): void;
}

class C implements I {
    [/*2*/42]: any;
}

var x: I = {
    ["/*3*/42"]: function () { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForComputedProperties2, TestFindAllRefsForComputedProperties2);

// findAllRefsForComputedProperties_test.go

// findAllRefsForComputedProperties_test.go
static void TestFindAllRefsForComputedProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    ["/*0*/prop1"]: () => void;
}

class C implements I {
    ["/*1*/prop1"]: any;
}

var x: I = {
    ["/*2*/prop1"]: function () { },
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForComputedProperties, TestFindAllRefsForComputedProperties);

// findAllRefsForDefaultExport01_test.go

// findAllRefsForDefaultExport01_test.go
static void TestFindAllRefsForDefaultExport01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/export default class /*2*/DefaultExportedClass {
}

var x: /*3*/DefaultExportedClass;

var y = new /*4*/DefaultExportedClass;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExport01, TestFindAllRefsForDefaultExport01);

// findAllRefsForDefaultExport02_test.go

// findAllRefsForDefaultExport02_test.go
static void TestFindAllRefsForDefaultExport02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/export default function /*2*/DefaultExportedFunction() {
    return /*3*/DefaultExportedFunction;
}

var x: typeof /*4*/DefaultExportedFunction;

var y = /*5*/DefaultExportedFunction();

/*6*/namespace /*7*/DefaultExportedFunction {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExport02, TestFindAllRefsForDefaultExport02);

// findAllRefsForDefaultExport03_test.go

// findAllRefsForDefaultExport03_test.go
static void TestFindAllRefsForDefaultExport03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/function /*2*/f() {
    return 100;
}

/*3*/export default /*4*/f;

var x: typeof /*5*/f;

var y = /*6*/f();

/*7*/namespace /*8*/f {
    var local = 100;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExport03, TestFindAllRefsForDefaultExport03);

// findAllRefsForDefaultExport04_test.go

// findAllRefsForDefaultExport04_test.go
static void TestFindAllRefsForDefaultExport04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
const /*0*/a = 0;
export /*1*/default /*2*/a;
// @Filename: /b.ts
import /*3*/a from "./a";
/*4*/a;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "2", "1", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExport04, TestFindAllRefsForDefaultExport04);

// findAllRefsForDefaultExport08_test.go

// findAllRefsForDefaultExport08_test.go
static void TestFindAllRefsForDefaultExport08(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export default class DefaultExportedClass {
}

var x: DefaultExportedClass;

var y = new DefaultExportedClass;

namespace /*1*/DefaultExportedClass {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExport08, TestFindAllRefsForDefaultExport08);

// findAllRefsForDefaultExport09_test.go

// findAllRefsForDefaultExport09_test.go
static void TestFindAllRefsForDefaultExport09(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /tsconfig.json
{
    "compilerOptions": {
        "target": "esnext",
        "strict": true,
        "outDir": "./out",
        "allowSyntheticDefaultImports": true
    }
}
// @filename: /a.js
module.exports = [];
// @filename: /b.js
module.exports = 1;
// @filename: /c.ts
export = [];
// @filename: /d.ts
export = 1;
// @filename: /foo.ts
import * as /*0*/a from "./a.js"
import /*1*/aDefault from "./a.js"
import * as /*2*/b from "./b.js"
import /*3*/bDefault from "./b.js"

import * as /*4*/c from "./c"
import /*5*/cDefault from "./c"
import * as /*6*/d from "./d"
import /*7*/dDefault from "./d")TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3", "4", "5", "6", "7"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExport09, TestFindAllRefsForDefaultExport09);

// findAllRefsForDefaultExportVS_test.go

// findAllRefsForDefaultExportVS_test.go
static void TestFindAllRefsForDefaultExportVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
export default function /*def*/f() {}
// @Filename: b.ts
import /*deg*/g from "./a";
[|/*ref*/g|]();
// @Filename: c.ts
import { f } from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineVSFindAllReferences(t, {"def", "deg"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExportVS, TestFindAllRefsForDefaultExportVS);

// findAllRefsForDefaultExport_anonymous_test.go

// findAllRefsForDefaultExport_anonymous_test.go
static void TestFindAllRefsForDefaultExport_anonymous(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export /*1*/default 1;
// @Filename: /b.ts
import a from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExport_anonymous, TestFindAllRefsForDefaultExport_anonymous);

// findAllRefsForDefaultExport_reExport_allowSyntheticDefaultImports_test.go

// findAllRefsForDefaultExport_reExport_allowSyntheticDefaultImports_test.go
static void TestFindAllRefsForDefaultExport_reExport_allowSyntheticDefaultImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowSyntheticDefaultImports: true
// @module: commonjs
// @Filename: /export.ts
const /*0*/foo = 1;
export = /*1*/foo;
// @Filename: /re-export.ts
export { /*2*/default } from "./export";
// @Filename: /re-export-dep.ts
import /*3*/fooDefault from "./re-export";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExport_reExport_allowSyntheticDefaultImports, TestFindAllRefsForDefaultExport_reExport_allowSyntheticDefaultImports);

// findAllRefsForDefaultExport_reExport_test.go

// findAllRefsForDefaultExport_reExport_test.go
static void TestFindAllRefsForDefaultExport_reExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /export.ts
const /*0*/foo = 1;
export default /*1*/foo;
// @Filename: /re-export.ts
export { /*2*/default } from "./export";
// @Filename: /re-export-dep.ts
import /*3*/fooDefault from "./re-export";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExport_reExport, TestFindAllRefsForDefaultExport_reExport);

// findAllRefsForDefaultExport_test.go

// findAllRefsForDefaultExport_test.go
static void TestFindAllRefsForDefaultExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
export default function /*def*/f() {}
// @Filename: b.ts
import /*deg*/g from "./a";
[|/*ref*/g|]();
// @Filename: c.ts
import { f } from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"def", "deg"});
		f->VerifyBaselineGoToDefinition(t, true, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultExport, TestFindAllRefsForDefaultExport);

// findAllRefsForDefaultKeyword_test.go

// findAllRefsForDefaultKeyword_test.go
static void TestFindAllRefsForDefaultKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
function f(value: string, /*1*/default: string) {}

const /*2*/default = 1;

function /*3*/default() {}

class /*4*/default {}

const foo = {
    /*5*/default: 1
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForDefaultKeyword, TestFindAllRefsForDefaultKeyword);

// findAllRefsForFunctionExpression01_test.go

// findAllRefsForFunctionExpression01_test.go
static void TestFindAllRefsForFunctionExpression01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
var foo = /*1*/function /*2*/foo(a = /*3*/foo(), b = () => /*4*/foo) {
    /*5*/foo(/*6*/foo, /*7*/foo);
}
// @Filename: file2.ts
/// <reference path="file1.ts" />
foo();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForFunctionExpression01, TestFindAllRefsForFunctionExpression01);

// findAllRefsForImportCallType_test.go

// findAllRefsForImportCallType_test.go
static void TestFindAllRefsForImportCallType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /app.ts
export function he/**/llo() {};
// @Filename: /re-export.ts
export type app = typeof import("./app")
// @Filename: /indirect-use.ts
import type { app } from "./re-export";
declare const app: app
app.hello();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForImportCallType, TestFindAllRefsForImportCallType);

// findAllRefsForImportCall_test.go

// findAllRefsForImportCall_test.go
static void TestFindAllRefsForImportCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /app.ts
export function he/**/llo() {};
// @Filename: /re-export.ts
export const services = { app: setup(() => import('./app')) }
function setup<T>(importee: () => Promise<T>): T { return {} as any }
// @Filename: /indirect-use.ts
import("./re-export").then(mod => mod.services.app.hello());
// @Filename: /direct-use.ts
async function main() {
    const mod = await import("./app")
    mod.hello();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForImportCall, TestFindAllRefsForImportCall);

// findAllRefsForMappedType_test.go

// findAllRefsForMappedType_test.go
static void TestFindAllRefsForMappedType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface T { /*1*/a: number };
type U = { [K in keyof T]: string };
type V = { [K in keyof U]: boolean };
const u: U = { a: "" }
const v: V = { a: true })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForMappedType, TestFindAllRefsForMappedType);

// findAllRefsForModuleGlobal_test.go

// findAllRefsForModuleGlobal_test.go
static void TestFindAllRefsForModuleGlobal(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/foo/index.d.ts
export const x = 0;
// @Filename: /b.ts
/// <reference types="foo" />
import { x } from "/*1*/foo";
declare module "foo" {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForModuleGlobal, TestFindAllRefsForModuleGlobal);

// findAllRefsForModule_test.go

// findAllRefsForModule_test.go
static void TestFindAllRefsForModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.ts
export const x = 0;
// @Filename: /b.ts
[|import { x } from "/*0*/[|{| "contextRangeIndex": 0 |}./a|]";|]
// @Filename: /c/sub.js
[|const a = require("/*1*/[|{| "contextRangeIndex": 2 |}../a|]");|]
// @Filename: /d.ts
 /// <reference path="/*2*/[|./a.ts|]" />)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2"});
		f->VerifyBaselineDocumentHighlightsWithOptions(t, nullptr, std::vector<std::string>{"/b.ts", "/c/sub.js", "/d.ts"}, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[4]});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForModule, TestFindAllRefsForModule);

// findAllRefsForObjectLiteralProperties_test.go

// findAllRefsForObjectLiteralProperties_test.go
static void TestFindAllRefsForObjectLiteralProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = {
    /*1*/property: {}
};

x./*2*/property;

/*3*/let {/*4*/property: pVar} = x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForObjectLiteralProperties, TestFindAllRefsForObjectLiteralProperties);

// findAllRefsForObjectSpread_test.go

// findAllRefsForObjectSpread_test.go
static void TestFindAllRefsForObjectSpread(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A1 { readonly /*0*/a: string };
interface A2 { /*1*/a?: number };
let a1: A1;
let a2: A2;
let a12 = { ...a1, ...a2 };
a12./*2*/a;
a1./*3*/a;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForObjectSpread, TestFindAllRefsForObjectSpread);

// findAllRefsForRest_test.go

// findAllRefsForRest_test.go
static void TestFindAllRefsForRest(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Gen {
    x: number
    /*1*/parent: Gen;
    millenial: string;
}
let t: Gen;
var { x, ...rest } = t;
rest./*2*/parent;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForRest, TestFindAllRefsForRest);

// findAllRefsForStaticInstanceMethodInheritance_test.go

// findAllRefsForStaticInstanceMethodInheritance_test.go
static void TestFindAllRefsForStaticInstanceMethodInheritance(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class X{
	/*0*/foo(): void{}
}

class Y extends X{
	static /*1*/foo(): void{}
}

class Z extends Y{
	static /*2*/foo(): void{}
	/*3*/foo(): void{}
}

const x = new X();
const y = new Y();
const z = new Z();
x.foo();
y.foo();
z.foo();
Y.foo();
Z.foo();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForStaticInstanceMethodInheritance, TestFindAllRefsForStaticInstanceMethodInheritance);

// findAllRefsForStaticInstancePropertyInheritance_test.go

// findAllRefsForStaticInstancePropertyInheritance_test.go
static void TestFindAllRefsForStaticInstancePropertyInheritance(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class X{
	/*0*/foo:any
}

class Y extends X{
	static /*1*/foo:any
}

class Z extends Y{
	static /*2*/foo:any
	/*3*/foo:any
}

const x = new X();
const y = new Y();
const z = new Z();
x./*4*/foo;
y./*5*/foo;
z./*6*/foo;
Y./*7*/foo;
Z./*8*/foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3", "4", "5", "6", "7", "8"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForStaticInstancePropertyInheritance, TestFindAllRefsForStaticInstancePropertyInheritance);

// findAllRefsForStringLiteral_test.go

// findAllRefsForStringLiteral_test.go
static void TestFindAllRefsForStringLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /a.ts
interface Foo {
    property: /**/"foo";
}
/**
 * @type {{ property: "foo"}}
 */
const obj: Foo = {
    property: "foo",
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForStringLiteral, TestFindAllRefsForStringLiteral);

// findAllRefsForUMDModuleAlias1_test.go

// findAllRefsForUMDModuleAlias1_test.go
static void TestFindAllRefsForUMDModuleAlias1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: 0.d.ts
export function doThing(): string;
export function doTheOtherThing(): void;
/*1*/export as namespace /*2*/myLib;
// @Filename: 1.ts
/// <reference path="0.d.ts" />
/*3*/myLib.doThing();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForUMDModuleAlias1, TestFindAllRefsForUMDModuleAlias1);

// findAllRefsForVariableInExtendsClause01_test.go

// findAllRefsForVariableInExtendsClause01_test.go
static void TestFindAllRefsForVariableInExtendsClause01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/var /*2*/Base = class { };
class C extends /*3*/Base { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForVariableInExtendsClause01, TestFindAllRefsForVariableInExtendsClause01);

// findAllRefsForVariableInExtendsClause02_test.go

// findAllRefsForVariableInExtendsClause02_test.go
static void TestFindAllRefsForVariableInExtendsClause02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/interface /*2*/Base { }
namespace n {
    var Base = class { };
    interface I extends /*3*/Base { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForVariableInExtendsClause02, TestFindAllRefsForVariableInExtendsClause02);

// findAllRefsForVariableInImplementsClause01_test.go

// findAllRefsForVariableInImplementsClause01_test.go
static void TestFindAllRefsForVariableInImplementsClause01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var Base = class { };
class C extends Base implements /**/Base { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForVariableInImplementsClause01, TestFindAllRefsForVariableInImplementsClause01);

// findAllRefsFromContextualUnionType1_test.go

// findAllRefsFromContextualUnionType1_test.go
static void TestFindAllRefsFromContextualUnionType1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
function test1(arg: { prop: "foo" }) {}
test1({ /*1*/prop: "bar" });

function test2(arg: { prop: "foo" } | undefined) {}
test2({ /*2*/prop: "bar" });)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsFromContextualUnionType1, TestFindAllRefsFromContextualUnionType1);

// findAllRefsGlobalModuleAugmentation_test.go

// findAllRefsGlobalModuleAugmentation_test.go
static void TestFindAllRefsGlobalModuleAugmentation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export {};
declare global {
    /*1*/function /*2*/f(): void;
}
// @Filename: /b.ts
/*3*/f();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsGlobalModuleAugmentation, TestFindAllRefsGlobalModuleAugmentation);

// findAllRefsGlobalThisKeywordInModule_test.go

// findAllRefsGlobalThisKeywordInModule_test.go
static void TestFindAllRefsGlobalThisKeywordInModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
/*1*/this;
export const c = 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsGlobalThisKeywordInModule, TestFindAllRefsGlobalThisKeywordInModule);

// findAllRefsImportDefault_test.go

// findAllRefsImportDefault_test.go
static void TestFindAllRefsImportDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: f.ts
export { foo as default };
function /*start*/foo(a: number, b: number) {
    return a + b;
}
// @Filename: b.ts
import bar from "./f";
bar(1, 2);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsImportDefault, TestFindAllRefsImportDefault);

// findAllRefsImportEqualsJsonFile_test.go

// findAllRefsImportEqualsJsonFile_test.go
static void TestFindAllRefsImportEqualsJsonFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @resolveJsonModule: true
// @module: commonjs
// @Filename: /a.ts
import /*0*/j = require("/*1*/./j.json");
/*2*/j;
// @Filename: /b.js
const /*3*/j = require("/*4*/./j.json");
/*5*/j;
// @Filename: /j.json
/*6*/{ "x": 0 })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"0", "2", "1", "4", "3", "5", "6"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsImportEqualsJsonFile, TestFindAllRefsImportEqualsJsonFile);

// findAllRefsImportNamed_test.go

// findAllRefsImportNamed_test.go
static void TestFindAllRefsImportNamed(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: f.ts
export { foo as foo }
function /*start*/foo(a: number, b: number) { }
// @Filename: b.ts
import x = require("./f");
x.foo(1, 2);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsImportNamed, TestFindAllRefsImportNamed);

// findAllRefsImportStarOfExportEquals_test.go

// findAllRefsImportStarOfExportEquals_test.go
static void TestFindAllRefsImportStarOfExportEquals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowSyntheticDefaultimports: true
// @Filename: /node_modules/a/index.d.ts
[|declare function /*a0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}a|](): void;|]
[|declare namespace /*a1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}a|] {
    export const x: number;
}|]
[|export = /*a2*/[|{| "contextRangeIndex": 4 |}a|];|]
// @Filename: /b.ts
[|import /*b0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 6 |}b|] from "a";|]
/*b1*/[|b|]();
[|b|].x;
// @Filename: /c.ts
[|import /*c0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 10 |}a|] from "a";|]
/*c1*/[|a|]();
/*c2*/[|a|].x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"a0", "a1", "a2", "b0", "b1", "c0", "c1", "c2"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[5]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[7], f->Ranges()[8], f->Ranges()[9]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[11], f->Ranges()[12], f->Ranges()[13]});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsImportStarOfExportEquals, TestFindAllRefsImportStarOfExportEquals);

// findAllRefsImportType_test.go

// findAllRefsImportType_test.go
static void TestFindAllRefsImportType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
module.exports = 0;
/*1*/export type /*2*/N = number;
// @Filename: /b.js
type T = import("./a")./*3*/N;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsImportType, TestFindAllRefsImportType);

// findAllRefsInClassExpression_test.go

// findAllRefsInClassExpression_test.go
static void TestFindAllRefsInClassExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I { /*0*/boom(): void; }
new class C implements I {
   /*1*/boom(){}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsInClassExpression, TestFindAllRefsInClassExpression);

// findAllRefsIndexedAccessTypes_test.go

// findAllRefsIndexedAccessTypes_test.go
static void TestFindAllRefsIndexedAccessTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    /*1*/0: number;
    /*2*/s: string;
}
interface J {
    a: I[/*3*/0],
    b: I["/*4*/s"],
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsIndexedAccessTypes, TestFindAllRefsIndexedAccessTypes);

// findAllRefsInheritedProperties1VS_test.go

// findAllRefsInheritedProperties1VS_test.go
static void TestFindAllRefsInheritedProperties1VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class class1 extends class1 {
   /*1*/doStuff() { }
   /*2*/propName: string;
}

var v: class1;
v./*3*/doStuff();
v./*4*/propName;)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineVSFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsInheritedProperties1VS, TestFindAllRefsInheritedProperties1VS);

// findAllRefsInheritedProperties1_test.go

// findAllRefsInheritedProperties1_test.go
static void TestFindAllRefsInheritedProperties1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class class1 extends class1 {
   /*1*/doStuff() { }
   /*2*/propName: string;
}

var v: class1;
v./*3*/doStuff();
v./*4*/propName;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsInheritedProperties1, TestFindAllRefsInheritedProperties1);

// findAllRefsInheritedProperties2_test.go

// findAllRefsInheritedProperties2_test.go
static void TestFindAllRefsInheritedProperties2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface interface1 extends interface1 {
   /*1*/doStuff(): void;   // r0
   /*2*/propName: string;  // r1
}

var v: interface1;
v./*3*/doStuff();  // r2
v./*4*/propName;   // r3)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsInheritedProperties2, TestFindAllRefsInheritedProperties2);

// findAllRefsInheritedProperties3_test.go

// findAllRefsInheritedProperties3_test.go
static void TestFindAllRefsInheritedProperties3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class class1 extends class1 {
    [|/*0*/doStuff() { }|]
    [|/*1*/propName: string;|]
}
interface interface1 extends interface1 {
    [|/*2*/doStuff(): void;|]
    [|/*3*/propName: string;|]
}
class class2 extends class1 implements interface1 {
    [|/*4*/doStuff() { }|]
    [|/*5*/propName: string;|]
}

var v: class2;
v./*6*/doStuff();
v./*7*/propName;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3", "4", "6", "5", "7"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsInheritedProperties3, TestFindAllRefsInheritedProperties3);

// findAllRefsInheritedProperties4_test.go

// findAllRefsInheritedProperties4_test.go
static void TestFindAllRefsInheritedProperties4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface C extends D {
    /*0*/prop0: string;
    /*1*/prop1: number;
}

interface D extends C {
    /*2*/prop0: string;
}

var d: D;
d./*3*/prop0;
d./*4*/prop1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "2", "3", "1", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsInheritedProperties4, TestFindAllRefsInheritedProperties4);

// findAllRefsInheritedProperties5_test.go

// findAllRefsInheritedProperties5_test.go
static void TestFindAllRefsInheritedProperties5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C extends D {
    /*0*/prop0: string;
    /*1*/prop1: number;
}

class D extends C {
    /*2*/prop0: string;
}

var d: D;
d./*3*/prop0;
d./*4*/prop1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsInheritedProperties5, TestFindAllRefsInheritedProperties5);

// findAllRefsInsideTemplates1_test.go

// findAllRefsInsideTemplates1_test.go
static void TestFindAllRefsInsideTemplates1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(/*1*/var /*2*/x = 10;
var y = )TS") + "`") + std::string(R"TS(${ /*3*/x } ${ /*4*/x })TS")) + std::string("`")) + std::string(R"TS()TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsInsideTemplates1, TestFindAllRefsInsideTemplates1);

// findAllRefsInsideTemplates2_test.go

// findAllRefsInsideTemplates2_test.go
static void TestFindAllRefsInsideTemplates2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(/*1*/function /*2*/f(...rest: any[]) { }
/*3*/f )TS") + "`") + std::string(R"TS(${ /*4*/f } ${ /*5*/f })TS")) + std::string("`")) + std::string(R"TS()TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsInsideTemplates2, TestFindAllRefsInsideTemplates2);

// findAllRefsInsideWithBlock_test.go

// findAllRefsInsideWithBlock_test.go
static void TestFindAllRefsInsideWithBlock(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/var /*2*/x = 0;

with ({}) {
    var y = x;  // Reference of x here should not be picked
    y++;        // also reference for y should be ignored
}

/*3*/x = /*4*/x + 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsInsideWithBlock, TestFindAllRefsInsideWithBlock);

// findAllRefsIsDefinition_test.go

// findAllRefsIsDefinition_test.go
static void TestFindAllRefsIsDefinition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function foo(a: number): number;
declare function foo(a: string): string;
declare function foo/*1*/(a: string | number): string | number;

function foon(a: number): number;
function foon(a: string): string;
function foon/*2*/(a: string | number): string | number {
    return a
}

foo; foon;

export const bar/*3*/ = 123;
console.log({ bar });

interface IFoo {
    foo/*4*/(): void;
}
class Foo implements IFoo {
    constructor(n: number)
    constructor()
    /*5*/constructor(n: number?) { }
    foo/*6*/(): void { }
    static init() { return new this() }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsIsDefinition, TestFindAllRefsIsDefinition);

// findAllRefsJSDocNamespacedTypedef_test.go

// findAllRefsJSDocNamespacedTypedef_test.go
static void TestFindAllRefsJSDocNamespacedTypedef(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @allowJs: true
// @checkJs: true
// @Filename: /index.js
// Namespaced typedef
/** @typedef {string} [|NS|].[|T|] */

// Namespaced typedef aliased to qualified namespaced typedef.
/** @typedef {NS.T} NS.[|U|] */

// Namespaced typedef aliased to implicitly-resolved typedef.
/** @typedef {U} NS.[|V|] */
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJSDocNamespacedTypedef, TestFindAllRefsJSDocNamespacedTypedef);

// findAllRefsJsDocImportTag2_test.go

// findAllRefsJsDocImportTag2_test.go
static void TestFindAllRefsJsDocImportTag2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @Filename: /component.js
export default class Component {
  constructor() {
    this.id_ = Math.random();
  }
  id() {
    return this.id_;
  }
}
// @Filename: /spatial-navigation.js
/** @import Component from './component.js' */

export class SpatialNavigation {
  /**
   * @param {Component} component
   */
  add(component) {}
}
// @Filename: /player.js
import Component from './component.js';

/**
 * @extends Component/*1*/
 */
export class Player extends Component {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocImportTag2, TestFindAllRefsJsDocImportTag2);

// findAllRefsJsDocImportTag3_test.go

// findAllRefsJsDocImportTag3_test.go
static void TestFindAllRefsJsDocImportTag3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @Filename: /component.js
export class Component {
  constructor() {
    this.id_ = Math.random();
  }
  id() {
    return this.id_;
  }
}
// @Filename: /spatial-navigation.js
/** @import { Component } from './component.js' */

export class SpatialNavigation {
  /**
   * @param {Component} component
   */
  add(component) {}
}
// @Filename: /player.js
import { Component } from './component.js';

/**
 * @extends Component/*1*/
 */
export class Player extends Component {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocImportTag3, TestFindAllRefsJsDocImportTag3);

// findAllRefsJsDocImportTag4_test.go

// findAllRefsJsDocImportTag4_test.go
static void TestFindAllRefsJsDocImportTag4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @Filename: /component.js
export class Component {
  constructor() {
    this.id_ = Math.random();
  }
  id() {
    return this.id_;
  }
}
// @Filename: /spatial-navigation.js
/** @import * as C from './component.js' */

export class SpatialNavigation {
  /**
   * @param {C.Component} component
   */
  add(component) {}
}
// @Filename: /player.js
import * as C from './component.js';

/**
 * @extends C/*1*/.Component
 */
export class Player extends Component {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocImportTag4, TestFindAllRefsJsDocImportTag4);

// findAllRefsJsDocImportTag5_test.go

// findAllRefsJsDocImportTag5_test.go
static void TestFindAllRefsJsDocImportTag5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @Filename: /a.js
export default function /*0*/a() {}
// @Filename: /b.js
/** @import /*1*/a, * as ns from "./a" */)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocImportTag5, TestFindAllRefsJsDocImportTag5);

// findAllRefsJsDocImportTag_test.go

// findAllRefsJsDocImportTag_test.go
static void TestFindAllRefsJsDocImportTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @Filename: /b.ts
export interface A { }
// @Filename: /a.js
/**
 * @import { A } from "./b";
 */

/**
 * @param { [|A/**/|] } a
 */
function f(a) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocImportTag, TestFindAllRefsJsDocImportTag);

// findAllRefsJsDocTemplateTag_class_js_test.go

// findAllRefsJsDocTemplateTag_class_js_test.go
static void TestFindAllRefsJsDocTemplateTag_class_js(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		t->Skip({"Go *_js_test.go file: GOOS js-gated, never compiled on this platform"}); return;
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/** @template /*1*/T */
class C {
    constructor() {
        /** @type {/*2*/T} */
        this.x = null;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocTemplateTag_class_js, TestFindAllRefsJsDocTemplateTag_class_js);

// findAllRefsJsDocTemplateTag_class_test.go

// findAllRefsJsDocTemplateTag_class_test.go
static void TestFindAllRefsJsDocTemplateTag_class(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @template /*1*/T */
class C</*2*/T> {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocTemplateTag_class, TestFindAllRefsJsDocTemplateTag_class);

// findAllRefsJsDocTemplateTag_function_js_test.go

// findAllRefsJsDocTemplateTag_function_js_test.go
static void TestFindAllRefsJsDocTemplateTag_function_js(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		t->Skip({"Go *_js_test.go file: GOOS js-gated, never compiled on this platform"}); return;
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/**
 * @template /*1*/T
 * @return {/*2*/T}
 */
function f() {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocTemplateTag_function_js, TestFindAllRefsJsDocTemplateTag_function_js);

// findAllRefsJsDocTemplateTag_function_test.go

// findAllRefsJsDocTemplateTag_function_test.go
static void TestFindAllRefsJsDocTemplateTag_function(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @template /*1*/T */
function f</*2*/T>() {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocTemplateTag_function, TestFindAllRefsJsDocTemplateTag_function);

// findAllRefsJsDocTypeDef_js_test.go

// findAllRefsJsDocTypeDef_js_test.go
static void TestFindAllRefsJsDocTypeDef_js(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		t->Skip({"Go *_js_test.go file: GOOS js-gated, never compiled on this platform"}); return;
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/** /*1*/@typedef {number} /*2*/T */

/**
 * @return {/*3*/T}
 */
function f(obj) { return 0; }

/**
 * @return {/*4*/T}
 */
function f2(obj) { return 0; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocTypeDef_js, TestFindAllRefsJsDocTypeDef_js);

// findAllRefsJsDocTypeDef_test.go

// findAllRefsJsDocTypeDef_test.go
static void TestFindAllRefsJsDocTypeDef(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @typedef {Object} /*0*/T */
function foo() {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsDocTypeDef, TestFindAllRefsJsDocTypeDef);

// findAllRefsJsThisPropertyAssignment2_test.go

// findAllRefsJsThisPropertyAssignment2_test.go
static void TestFindAllRefsJsThisPropertyAssignment2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @noImplicitThis: true
// @Filename: infer.d.ts
export declare function infer(o: { m: Record<string, Function> } & ThisType<{ x: number }>): void;
// @Filename: a.js
import { infer } from "./infer";
infer({
    m: {
        initData() {
            this.x = 1;
            this./*1*/x;
        },
    }
});
// @Filename: b.ts
import { infer } from "./infer";
infer({
    m: {
        initData() {
            this.x = 1;
            this./*2*/x;
        },
    }
});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsThisPropertyAssignment2, TestFindAllRefsJsThisPropertyAssignment2);

// findAllRefsJsThisPropertyAssignment_test.go

// findAllRefsJsThisPropertyAssignment_test.go
static void TestFindAllRefsJsThisPropertyAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @noImplicitThis: true
// @Filename: infer.d.ts
export declare function infer(o: { m(): void } & ThisType<{ x: number }>): void;
// @Filename: a.js
import { infer } from "./infer";
infer({
    m() {
        this.x = 1;
        this./*1*/x;
    },
});
// @Filename: b.js
/**
 * @template T
 * @param {{m(): void} & ThisType<{x: number}>} o
 */
function infer(o) {}
infer({
    m() {
        this.x = 2;
        this./*2*/x;
    },
});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsJsThisPropertyAssignment, TestFindAllRefsJsThisPropertyAssignment);

// findAllRefsMappedType_nonHomomorphic_test.go

// findAllRefsMappedType_nonHomomorphic_test.go
static void TestFindAllRefsMappedType_nonHomomorphic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
function f(x: { [K in "m"]: number; }) {
    x./*1*/m;
    x./*2*/m
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsMappedType_nonHomomorphic, TestFindAllRefsMappedType_nonHomomorphic);

// findAllRefsMappedType_test.go

// findAllRefsMappedType_test.go
static void TestFindAllRefsMappedType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface T { /*1*/a: number; }
type U = { readonly [K in keyof T]?: string };
declare const t: T;
t./*2*/a;
declare const u: U;
u./*3*/a;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsMappedType, TestFindAllRefsMappedType);

// findAllRefsMissingModulesOverlappingSpecifiers_test.go

// findAllRefsMissingModulesOverlappingSpecifiers_test.go
static void TestFindAllRefsMissingModulesOverlappingSpecifiers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// https://github.com/microsoft/TypeScript/issues/5551
import { resolve/*0*/ as resolveUrl } from "idontcare";
import { resolve/*1*/ } from "whatever";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsMissingModulesOverlappingSpecifiers, TestFindAllRefsMissingModulesOverlappingSpecifiers);

// findAllRefsModuleAugmentation_test.go

// findAllRefsModuleAugmentation_test.go
static void TestFindAllRefsModuleAugmentation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/foo/index.d.ts
/*1*/export type /*2*/T = number;
// @Filename: /a.ts
import * as foo from "foo";
declare module "foo" {
    export const x: /*3*/T;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsModuleAugmentation, TestFindAllRefsModuleAugmentation);

// findAllRefsModuleDotExports_test.go

// findAllRefsModuleDotExports_test.go
static void TestFindAllRefsModuleDotExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/*1*/const b = require("/*2*/./b");
// @Filename: /b.js
/*3*/module.exports = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsModuleDotExports, TestFindAllRefsModuleDotExports);

// findAllRefsNoImportClause_test.go

// findAllRefsNoImportClause_test.go
static void TestFindAllRefsNoImportClause(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
/*1*/export const /*2*/x = 0;
// @Filename: /b.ts
import "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsNoImportClause, TestFindAllRefsNoImportClause);

// findAllRefsNonModule_test.go

// findAllRefsNonModule_test.go
static void TestFindAllRefsNonModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @Filename: /script.ts
console.log("I'm a script!");
// @Filename: /import.ts
import "./script/*1*/";
// @Filename: /require.js
require("./script/*2*/");
console.log("./script/*3*/");
// @Filename: /tripleSlash.ts
/// <reference path="script.ts" />
// @Filename: /stringLiteral.ts
console.log("./script");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsNonModule, TestFindAllRefsNonModule);

// findAllRefsNonexistentPropertyNoCrash1_test.go

// findAllRefsNonexistentPropertyNoCrash1_test.go
static void TestFindAllRefsNonexistentPropertyNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @allowJs: true
// @checkJs: true
// @filename: ./src/parser-input.js
export default () => {
  let input;

  const parserInput = {};

  parserInput.currentChar = () => input.charAt(parserInput.i);

  parserInput.end = () => {
    const isFinished = parserInput.i >= input.length;

    return {
      isFinished,
      furthest: parserInput.i,
    };
  };

  return parserInput;
};
// @filename: ./src/parser.js
import getParserInput from "./parser-input";

const Parser = function Parser(context, imports, fileInfo, currentIndex) {
  currentIndex = currentIndex || 0;
  let parsers;
  const parserInput = getParserInput();

  return {
    parserInput,
    parsers: (parsers = {
      variable: function () {
        let name;

        if (parserInput.currentChar() === "/*1*/@") {
          return name[1];
        }
      },
    }),
  };
};

export default Parser;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsNonexistentPropertyNoCrash1, TestFindAllRefsNonexistentPropertyNoCrash1);

// findAllRefsObjectBindingElementPropertyName01_test.go

// findAllRefsObjectBindingElementPropertyName01_test.go
static void TestFindAllRefsObjectBindingElementPropertyName01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    /*1*/property1: number;
    property2: string;
}

var foo: I;
/*2*/var { /*3*/property1: prop1 } = foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsObjectBindingElementPropertyName01, TestFindAllRefsObjectBindingElementPropertyName01);

// findAllRefsObjectBindingElementPropertyName02_test.go

// findAllRefsObjectBindingElementPropertyName02_test.go
static void TestFindAllRefsObjectBindingElementPropertyName02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    /*1*/property1: number;
    property2: string;
}

var foo: I;
/*2*/var { /*3*/property1: {} } = foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsObjectBindingElementPropertyName02, TestFindAllRefsObjectBindingElementPropertyName02);

// findAllRefsObjectBindingElementPropertyName03_test.go

// findAllRefsObjectBindingElementPropertyName03_test.go
static void TestFindAllRefsObjectBindingElementPropertyName03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    /*1*/property1: number;
    property2: string;
}

var foo: I;
var [ { property1: prop1 }, { /*2*/property1, property2 } ] = [foo, foo];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsObjectBindingElementPropertyName03, TestFindAllRefsObjectBindingElementPropertyName03);

// findAllRefsObjectBindingElementPropertyName04_test.go

// findAllRefsObjectBindingElementPropertyName04_test.go
static void TestFindAllRefsObjectBindingElementPropertyName04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    /*0*/property1: number;
    property2: string;
}

function f({ /*1*/property1: p1 }: I,
           { /*2*/property1 }: I,
           { property1: p2 }) {

    return /*3*/property1 + 1;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsObjectBindingElementPropertyName04, TestFindAllRefsObjectBindingElementPropertyName04);

// findAllRefsObjectBindingElementPropertyName05_test.go

// findAllRefsObjectBindingElementPropertyName05_test.go
static void TestFindAllRefsObjectBindingElementPropertyName05(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    property1: number;
    property2: string;
}

function f({ /**/property1: p }, { property1 }) {
    let x = property1;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsObjectBindingElementPropertyName05, TestFindAllRefsObjectBindingElementPropertyName05);

// findAllRefsObjectBindingElementPropertyName06_test.go

// findAllRefsObjectBindingElementPropertyName06_test.go
static void TestFindAllRefsObjectBindingElementPropertyName06(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    /*0*/property1: number;
    property2: string;
}

var elems: I[];
for (let { /*1*/property1: p } of elems) {
}
for (let { /*2*/property1 } of elems) {
}
for (var { /*3*/property1: p1 } of elems) {
}
var p2;
for ({ /*4*/property1 : p2 } of elems) {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "3", "4", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsObjectBindingElementPropertyName06, TestFindAllRefsObjectBindingElementPropertyName06);

// findAllRefsObjectBindingElementPropertyName07_test.go

// findAllRefsObjectBindingElementPropertyName07_test.go
static void TestFindAllRefsObjectBindingElementPropertyName07(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let p, b;

p, [{ /*1*/a: p, b }] = [{ a: 10, b: true }];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsObjectBindingElementPropertyName07, TestFindAllRefsObjectBindingElementPropertyName07);

// findAllRefsObjectBindingElementPropertyName10_test.go

// findAllRefsObjectBindingElementPropertyName10_test.go
static void TestFindAllRefsObjectBindingElementPropertyName10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Recursive {
    /*1*/next?: Recursive;
    value: any;
}

function f (/*2*/{ /*3*/next: { /*4*/next: x} }: Recursive) {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsObjectBindingElementPropertyName10, TestFindAllRefsObjectBindingElementPropertyName10);

// findAllRefsOfConstructor2_test.go

// findAllRefsOfConstructor2_test.go
static void TestFindAllRefsOfConstructor2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    /*a*/constructor(s: string) {}
}
class B extends A {
    /*b*/constructor() { super(""); }
}
class C extends B {
    /*c*/constructor() {
        super();
    }
}
class D extends B { }
const a = new A("a");
const b = new B();
const c = new C();
const d = new D();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"a", "b", "c"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOfConstructor2, TestFindAllRefsOfConstructor2);

// findAllRefsOfConstructor_multipleFiles_test.go

// findAllRefsOfConstructor_multipleFiles_test.go
static void TestFindAllRefsOfConstructor_multipleFiles(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: f.ts
class A {
    /*aCtr*/constructor(s: string) {}
}
class B extends A { }
export { A, B };
// @Filename: a.ts
import { A as A1 } from "./f";
const a1 = new A1("a1");
export default class extends A1 { }
export { B as B1 } from "./f";
// @Filename: b.ts
import B, { B1 } from "./a";
const d = new B("b");
const d1 = new B1("b1");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"aCtr"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOfConstructor_multipleFiles, TestFindAllRefsOfConstructor_multipleFiles);

// findAllRefsOfConstructor_test.go

// findAllRefsOfConstructor_test.go
static void TestFindAllRefsOfConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    /*aCtr*/constructor(s: string) {}
}
class B extends A { }
class C extends B {
    /*cCtr*/constructor() {
        super("");
    }
}
class D extends B { }
class E implements A { }
const a = new A("a");
const b = new B("b");
const c = new C();
const d = new D("d");
const e = new E();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"aCtr", "cCtr"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOfConstructor, TestFindAllRefsOfConstructor);

// findAllRefsOfConstructor_withModifier_test.go

// findAllRefsOfConstructor_withModifier_test.go
static void TestFindAllRefsOfConstructor_withModifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class X {
    public /*0*/constructor() {}
}
var x = new X();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOfConstructor_withModifier, TestFindAllRefsOfConstructor_withModifier);

// findAllRefsOnDecorators_test.go

// findAllRefsOnDecorators_test.go
static void TestFindAllRefsOnDecorators(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
/*1*/function /*2*/decorator(target) {
    return target;
}
/*3*/decorator();
// @Filename: b.ts
@/*4*/decorator @/*5*/decorator("again")
class C {
    @/*6*/decorator
    method() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOnDecorators, TestFindAllRefsOnDecorators);

// findAllRefsOnDefinition2_test.go

// findAllRefsOnDefinition2_test.go
static void TestFindAllRefsOnDefinition2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: findAllRefsOnDefinition2-import.ts
export module Test{

    /*1*/export interface /*2*/start { }

    export interface stop { }
}
//@Filename: findAllRefsOnDefinition2.ts
import Second = require("./findAllRefsOnDefinition2-import");

var start: Second.Test./*3*/start;
var stop: Second.Test.stop;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOnDefinition2, TestFindAllRefsOnDefinition2);

// findAllRefsOnDefinition_test.go

// findAllRefsOnDefinition_test.go
static void TestFindAllRefsOnDefinition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: findAllRefsOnDefinition-import.ts
export class Test{

    constructor(){

    }

    /*1*/public /*2*/start(){
        return this;
    }

    public stop(){
        return this;
    }
}
//@Filename: findAllRefsOnDefinition.ts
import Second = require("./findAllRefsOnDefinition-import");

var second = new Second.Test()
second./*3*/start();
second.stop();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOnDefinition, TestFindAllRefsOnDefinition);

// findAllRefsOnImportAliases2_test.go

// findAllRefsOnImportAliases2_test.go
static void TestFindAllRefsOnImportAliases2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: a.ts
[|export class /*class0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}Class|] {}|]
//@Filename: b.ts
[|import { /*class1*/[|{| "contextRangeIndex": 2 |}Class|] as /*c2_0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}C2|] } from "./a";|]
var c = new /*c2_1*/[|C2|]();
//@Filename: c.ts
[|export { /*class2*/[|{| "contextRangeIndex": 6 |}Class|] as /*c3*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 6 |}C3|] } from "./a";|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"class0", "class1", "class2", "c2_0", "c2_1", "c3"});
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"Class", "C2", "C3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOnImportAliases2, TestFindAllRefsOnImportAliases2);

// findAllRefsOnImportAliases_test.go

// findAllRefsOnImportAliases_test.go
static void TestFindAllRefsOnImportAliases(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: a.ts
export class /*0*/Class {
}
//@Filename: b.ts
import { /*1*/Class } from "./a";

var c = new /*2*/Class();
//@Filename: c.ts
export { /*3*/Class } from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOnImportAliases, TestFindAllRefsOnImportAliases);

// findAllRefsOnPrivateParameterProperty1_test.go

// findAllRefsOnPrivateParameterProperty1_test.go
static void TestFindAllRefsOnPrivateParameterProperty1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class ABCD {
    constructor(private x: number, public y: number, /*1*/private /*2*/z: number) {
    }

    func() {
        return this./*3*/z;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOnPrivateParameterProperty1, TestFindAllRefsOnPrivateParameterProperty1);

// findAllRefsParameterPropertyDeclaration1_test.go

// findAllRefsParameterPropertyDeclaration1_test.go
static void TestFindAllRefsParameterPropertyDeclaration1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    constructor(private /*1*/privateParam: number) {
        let localPrivate = privateParam;
        this.privateParam += 10;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsParameterPropertyDeclaration1, TestFindAllRefsParameterPropertyDeclaration1);

// findAllRefsParameterPropertyDeclaration2_test.go

// findAllRefsParameterPropertyDeclaration2_test.go
static void TestFindAllRefsParameterPropertyDeclaration2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    constructor(public /*0*/publicParam: number) {
        let localPublic = /*1*/publicParam;
        this./*2*/publicParam += 10;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsParameterPropertyDeclaration2, TestFindAllRefsParameterPropertyDeclaration2);

// findAllRefsParameterPropertyDeclaration3_test.go

// findAllRefsParameterPropertyDeclaration3_test.go
static void TestFindAllRefsParameterPropertyDeclaration3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    constructor(protected /*0*/protectedParam: number) {
        let localProtected = /*1*/protectedParam;
        this./*2*/protectedParam += 10;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsParameterPropertyDeclaration3, TestFindAllRefsParameterPropertyDeclaration3);

// findAllRefsParameterPropertyDeclaration_inheritance_test.go

// findAllRefsParameterPropertyDeclaration_inheritance_test.go
static void TestFindAllRefsParameterPropertyDeclaration_inheritance(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
	constructor(public /*0*/x: string) {
		/*1*/x;
	}
}
class D extends C {
	constructor(public /*2*/x: string) {
		super(/*3*/x);
	}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsParameterPropertyDeclaration_inheritance, TestFindAllRefsParameterPropertyDeclaration_inheritance);

// findAllRefsParameterPropertyWithConflictingMember_test.go

// findAllRefsParameterPropertyWithConflictingMember_test.go
static void TestFindAllRefsParameterPropertyWithConflictingMember(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @filename: c1.ts
class C1 {
  [|x|]() {}
  constructor(public [|x|]: number) {
    [|x|]++;
  }
}
new C1(1).[|x|];

// @filename: c2.ts
interface C2 {
  get [|x|](): void
}
class C2 {
  constructor(public [|x|]: number) {
    [|x|]++;
  }
}
new C2(1).[|x|];
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsParameterPropertyWithConflictingMember, TestFindAllRefsParameterPropertyWithConflictingMember);

// findAllRefsPrefixSuffixPreference_test.go

// findAllRefsPrefixSuffixPreference_test.go
static void TestFindAllRefsPrefixSuffixPreference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /file1.ts
declare function log(s: string | number): void;
[|const /*q0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}q|] = 1;|]
[|export { /*q1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}q|] };|]
const x = {
    [|/*z0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 4 |}z|]: 'value'|]
}
[|const { /*z1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 6 |}z|] } = x;|]
log(/*z2*/[|z|]);
// @Filename: /file2.ts
declare function log(s: string | number): void;
[|import { /*q2*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 9 |}q|] } from "./file1";|]
log(/*q3*/[|q|] + 1);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"q0", "q1", "q2", "q3", "z0", "z1", "z2"});
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}), {f->Ranges()[1], f->Ranges()[3], f->Ranges()[10], f->Ranges()[11]});
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}), {f->Ranges()[1], f->Ranges()[3], f->Ranges()[10], f->Ranges()[11]});
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}), {f->Ranges()[5], f->Ranges()[7], f->Ranges()[8]});
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}), {f->Ranges()[5], f->Ranges()[7], f->Ranges()[8]});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsPrefixSuffixPreference, TestFindAllRefsPrefixSuffixPreference);

// findAllRefsPrimitiveJsDoc_test.go

// findAllRefsPrimitiveJsDoc_test.go
static void TestFindAllRefsPrimitiveJsDoc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
/**
 * @param {/*1*/number} n
 * @returns {/*2*/number}
 */
function f(n: /*3*/number): /*4*/number {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsPrimitiveJsDoc, TestFindAllRefsPrimitiveJsDoc);

// findAllRefsPrivateNameAccessors_test.go

// findAllRefsPrivateNameAccessors_test.go
static void TestFindAllRefsPrivateNameAccessors(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    /*1*/get /*2*/#foo(){ return 1; }
    /*3*/set /*4*/#foo(value: number){  }
    constructor() {
        this./*5*/#foo();
    }
}
class D extends C {
    constructor() {
        super()
        this.#foo = 20;
    }
}
class E {
    /*6*/get /*7*/#foo(){ return 1; }
    /*8*/set /*9*/#foo(value: number){  }
    constructor() {
        this./*10*/#foo();
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsPrivateNameAccessors, TestFindAllRefsPrivateNameAccessors);

// findAllRefsPrivateNameMethods_test.go

// findAllRefsPrivateNameMethods_test.go
static void TestFindAllRefsPrivateNameMethods(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    /*1*/#foo(){ }
    constructor() {
        this./*2*/#foo();
    }
}
class D extends C {
    constructor() {
        super()
        this.#foo = 20;
    }
}
class E {
    /*3*/#foo(){ }
    constructor() {
        this./*4*/#foo();
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsPrivateNameMethods, TestFindAllRefsPrivateNameMethods);

// findAllRefsPrivateNameProperties_test.go

// findAllRefsPrivateNameProperties_test.go
static void TestFindAllRefsPrivateNameProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    /*1*/#foo = 10;
    constructor() {
        this./*2*/#foo = 20;
        /*3*/#foo in this;
    }
}
class D extends C {
    constructor() {
        super()
        this.#foo = 20;
    }
}
class E {
    /*4*/#foo: number;
    constructor() {
        this./*5*/#foo = 20;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsPrivateNameProperties, TestFindAllRefsPrivateNameProperties);

// findAllRefsPropertyContextuallyTypedByTypeParam01_test.go

// findAllRefsPropertyContextuallyTypedByTypeParam01_test.go
static void TestFindAllRefsPropertyContextuallyTypedByTypeParam01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface IFoo {
    /*1*/a: string;
}
class C<T extends IFoo> {
    method() {
        var x: T = {
            a: ""
        };
        x.a;
    }
}


var x: IFoo = {
    a: "ss"
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsPropertyContextuallyTypedByTypeParam01, TestFindAllRefsPropertyContextuallyTypedByTypeParam01);

// findAllRefsReExportLocal_test.go

// findAllRefsReExportLocal_test.go
static void TestFindAllRefsReExportLocal(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
// @strict: false
// @Filename: /a.ts
[|var /*ax0*/[|{| "isDefinition": true, "contextRangeIndex": 0 |}x|];|]
[|export { /*ax1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}x|] };|]
[|export { /*ax2*/[|{| "contextRangeIndex": 4 |}x|] as /*ay*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 4 |}y|] };|]
// @Filename: /b.ts
[|import { /*bx0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 7 |}x|], /*by0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 7 |}y|] } from "./a";|]
/*bx1*/[|x|]; /*by1*/[|y|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"ax0", "ax1", "ax2", "bx0", "bx1", "ay", "by0", "by1"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[5]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[3]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[8], f->Ranges()[10]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[6]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[9], f->Ranges()[11]});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsReExportLocal, TestFindAllRefsReExportLocal);

// findAllRefsReExportRightNameWrongSymbol_test.go

// findAllRefsReExportRightNameWrongSymbol_test.go
static void TestFindAllRefsReExportRightNameWrongSymbol(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
[|export const /*a*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}x|] = 0;|]
// @Filename: /b.ts
[|export const /*b*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}x|] = 0;|]
//@Filename: /c.ts
[|export { /*cFromB*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 4 |}x|] } from "./b";|]
[|import { /*cFromA*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 6 |}x|] } from "./a";|]
/*cUse*/[|x|];
// @Filename: /d.ts
[|import { /*d*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 9 |}x|] } from "./c";|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"a", "b", "cFromB", "cFromA", "cUse", "d"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[7], f->Ranges()[8]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[3]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[5]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[10]});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsReExportRightNameWrongSymbol, TestFindAllRefsReExportRightNameWrongSymbol);

// findAllRefsReExportStarAs_test.go

// findAllRefsReExportStarAs_test.go
static void TestFindAllRefsReExportStarAs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /leafModule.ts
export const /*helloDef*/hello = () => 'Hello';
// @Filename: /exporting.ts
export * as /*leafDef*/Leaf from './leafModule';
// @Filename: /importing.ts
 import { /*leafImportDef*/Leaf } from './exporting';
 /*leafUse*/[|Leaf|]./*helloUse*/[|hello|]())TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"helloDef", "helloUse", "leafDef", "leafImportDef", "leafUse"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsReExportStarAs, TestFindAllRefsReExportStarAs);

// findAllRefsReExportStar_test.go

// findAllRefsReExportStar_test.go
static void TestFindAllRefsReExportStar(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export function /*0*/foo(): void {}
// @Filename: /b.ts
export * from "./a";
// @Filename: /c.ts
import { /*1*/foo } from "./b";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"0", "1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsReExportStar, TestFindAllRefsReExportStar);

// findAllRefsReExport_broken2_test.go

// findAllRefsReExport_broken2_test.go
static void TestFindAllRefsReExport_broken2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
/*1*/export { /*2*/x } from "nonsense";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsReExport_broken2, TestFindAllRefsReExport_broken2);

// findAllRefsReExport_broken_test.go

// findAllRefsReExport_broken_test.go
static void TestFindAllRefsReExport_broken(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
/*1*/export { /*2*/x };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsReExport_broken, TestFindAllRefsReExport_broken);

// findAllRefsReExports2_test.go

// findAllRefsReExports2_test.go
static void TestFindAllRefsReExports2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export function /*1*/foo(): void {}
// @Filename: /b.ts
import { foo as oof } from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsReExports2, TestFindAllRefsReExports2);

// findAllRefsReExportsUseInImportType_test.go

// findAllRefsReExportsUseInImportType_test.go
static void TestFindAllRefsReExportsUseInImportType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /foo/types/types.ts
[|export type /*full0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}Full|] = { prop: string; };|]
// @Filename: /foo/types/index.ts
[|import * as /*foo0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}foo|] from './types';|]
[|export { /*foo1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 4 |}foo|] };|]
// @Filename: /app.ts
[|import { /*foo2*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 6 |}foo|] } from './foo/types';|]
export type fullType = /*foo3*/[|foo|]./*full1*/[|Full|];
type namespaceImport = typeof import('./foo/types');
type fullType2 = import('./foo/types')./*foo4*/[|foo|]./*full2*/[|Full|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"full0", "full1", "full2", "foo0", "foo1", "foo2", "foo3", "foo4"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[9], f->Ranges()[11]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[3]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[5], f->Ranges()[10]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[7], f->Ranges()[8]});
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}), {f->Ranges()[7], f->Ranges()[8], f->Ranges()[10], f->Ranges()[3], f->Ranges()[5]});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsReExportsUseInImportType, TestFindAllRefsReExportsUseInImportType);

// findAllRefsRedeclaredPropertyInDerivedInterface_test.go

// findAllRefsRedeclaredPropertyInDerivedInterface_test.go
static void TestFindAllRefsRedeclaredPropertyInDerivedInterface(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
interface A {
    readonly /*0*/x: number | string;
}
interface B extends A {
    readonly /*1*/x: number;
}
const a: A = { /*2*/x: 0 };
const b: B = { /*3*/x: 0 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsRedeclaredPropertyInDerivedInterface, TestFindAllRefsRedeclaredPropertyInDerivedInterface);

// findAllRefsRenameImportWithSameName_test.go

// findAllRefsRenameImportWithSameName_test.go
static void TestFindAllRefsRenameImportWithSameName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
[|export const /*0*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}x|] = 0;|]
//@Filename: /b.ts
[|import { /*1*/[|{| "contextRangeIndex": 2 |}x|] as /*2*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}x|] } from "./a";|]
/*3*/[|x|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[4], f->Ranges()[5]});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsRenameImportWithSameName, TestFindAllRefsRenameImportWithSameName);

// findAllRefsRootSymbols_test.go

// findAllRefsRootSymbols_test.go
static void TestFindAllRefsRootSymbols(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I { /*0*/x: {}; }
interface J { /*1*/x: {}; }
declare const o: (I | J) & { /*2*/x: string };
o./*3*/x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsRootSymbols, TestFindAllRefsRootSymbols);

// findAllRefsThisKeywordMultipleFiles_test.go

// findAllRefsThisKeywordMultipleFiles_test.go
static void TestFindAllRefsThisKeywordMultipleFiles(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
/*1*/this; /*2*/this;
// @Filename: file2.ts
/*3*/this;
/*4*/this;
// @Filename: file3.ts
 ((x = /*5*/this, y) => /*6*/this)(/*7*/this, /*8*/this);
 // different 'this'
 function f(this) { return this; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsThisKeywordMultipleFiles, TestFindAllRefsThisKeywordMultipleFiles);

// findAllRefsThisKeyword_test.go

// findAllRefsThisKeyword_test.go
static void TestFindAllRefsThisKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
/*1*/this;
function f(/*2*/this) {
    return /*3*/this;
    function g(/*4*/this) { return /*5*/this; }
}
class C {
    static x() {
        /*6*/this;
    }
    static y() {
        () => /*7*/this;
    }
    constructor() {
        /*8*/this;
    }
    method() {
        () => /*9*/this;
    }
}
// These are *not* real uses of the 'this' keyword, they are identifiers.
const x = { /*10*/this: 0 }
x./*11*/this;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsThisKeyword, TestFindAllRefsThisKeyword);

// findAllRefsTripleSlashRef1_test.go

// findAllRefsTripleSlashRef1_test.go
static void TestFindAllRefsTripleSlashRef1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/@types/react/index.d.ts
export type JSX = {};

// @Filename: /node_modules/excalidraw/index.d.ts
/// <reference types="react" />

// @Filename: /index.ts
import type {JSX} from '/*m*/react';
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"m"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsTripleSlashRef1, TestFindAllRefsTripleSlashRef1);

// findAllRefsTypeParameterInMergedInterface_test.go

// findAllRefsTypeParameterInMergedInterface_test.go
static void TestFindAllRefsTypeParameterInMergedInterface(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I</*1*/T> { a: /*2*/T }
interface I</*3*/T> { b: /*4*/T })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsTypeParameterInMergedInterface, TestFindAllRefsTypeParameterInMergedInterface);

// findAllRefsTypedef_importType_test.go

// findAllRefsTypedef_importType_test.go
static void TestFindAllRefsTypedef_importType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
module.exports = 0;
/** /*1*/@typedef {number} /*2*/Foo */
const dummy = 0;
// @Filename: /b.js
/** @type {import('./a')./*3*/Foo} */
const x = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsTypedef_importType, TestFindAllRefsTypedef_importType);

// findAllRefsTypedef_test.go

// findAllRefsTypedef_test.go
static void TestFindAllRefsTypedef(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/**
 * @typedef I {Object}
 * /*1*/@prop /*2*/p {number}
 */

/** @type {I} */
let x;
x./*3*/p;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsTypedef, TestFindAllRefsTypedef);

// findAllRefsTypeofImport_test.go

// findAllRefsTypeofImport_test.go
static void TestFindAllRefsTypeofImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
/*1*/export const /*2*/x = 0;
declare const a: typeof import("./a");
a./*3*/x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsTypeofImport, TestFindAllRefsTypeofImport);

// findAllRefsUnionProperty_test.go

// findAllRefsUnionProperty_test.go
static void TestFindAllRefsUnionProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type T =
    | { /*t0*/type: "a", /*p0*/prop: number }
    | { /*t1*/type: "b", /*p1*/prop: string };
const tt: T = {
    /*t2*/type: "a",
    /*p2*/prop: 0,
};
declare const t: T;
if (t./*t3*/type === "a") {
    t./*t4*/type;
} else {
    t./*t5*/type;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"t0", "t1", "t3", "t4", "t5", "t2", "p0", "p1", "p2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsUnionProperty, TestFindAllRefsUnionProperty);

// findAllRefsUnresolvedSymbols1_test.go

// findAllRefsUnresolvedSymbols1_test.go
static void TestFindAllRefsUnresolvedSymbols1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let a: /*a0*/Bar;
let b: /*a1*/Bar<string>;
let c: /*a2*/Bar<string, number>;
let d: /*b0*/Bar./*c0*/X;
let e: /*b1*/Bar./*c1*/X<string>;
let f: /*b2*/Bar./*d0*/X./*e0*/Y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"a0", "a1", "a2", "b0", "b1", "b2", "c0", "c1", "d0", "e0"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsUnresolvedSymbols1, TestFindAllRefsUnresolvedSymbols1);

// findAllRefsUnresolvedSymbols2_test.go

// findAllRefsUnresolvedSymbols2_test.go
static void TestFindAllRefsUnresolvedSymbols2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import { /*a0*/Bar } from "does-not-exist";

let a: /*a1*/Bar;
let b: /*a2*/Bar<string>;
let c: /*a3*/Bar<string, number>;
let d: /*a4*/Bar./*b0*/X;
let e: /*a5*/Bar./*b1*/X<string>;
let f: /*a6*/Bar./*c0*/X./*d0*/Y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "b0", "b1", "c0", "d0"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsUnresolvedSymbols2, TestFindAllRefsUnresolvedSymbols2);

// findAllRefsUnresolvedSymbols3_test.go

// findAllRefsUnresolvedSymbols3_test.go
static void TestFindAllRefsUnresolvedSymbols3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import * as /*a0*/Bar from "does-not-exist";

let a: /*a1*/Bar;
let b: /*a2*/Bar<string>;
let c: /*a3*/Bar<string, number>;
let d: /*a4*/Bar./*b0*/X;
let e: /*a5*/Bar./*b1*/X<string>;
let f: /*a6*/Bar./*c0*/X./*d0*/Y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "b0", "b1", "c0", "d0"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsUnresolvedSymbols3, TestFindAllRefsUnresolvedSymbols3);

// findAllRefsWithLeadingUnderscoreNames1_test.go

// findAllRefsWithLeadingUnderscoreNames1_test.go
static void TestFindAllRefsWithLeadingUnderscoreNames1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    /*1*/public /*2*/_bar() { return 0; }
}

var x: Foo;
x./*3*/_bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithLeadingUnderscoreNames1, TestFindAllRefsWithLeadingUnderscoreNames1);

// findAllRefsWithLeadingUnderscoreNames2_test.go

// findAllRefsWithLeadingUnderscoreNames2_test.go
static void TestFindAllRefsWithLeadingUnderscoreNames2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    /*1*/public /*2*/__bar() { return 0; }
}

var x: Foo;
x./*3*/__bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithLeadingUnderscoreNames2, TestFindAllRefsWithLeadingUnderscoreNames2);

// findAllRefsWithLeadingUnderscoreNames3_test.go

// findAllRefsWithLeadingUnderscoreNames3_test.go
static void TestFindAllRefsWithLeadingUnderscoreNames3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    /*1*/public /*2*/___bar() { return 0; }
}

var x: Foo;
x./*3*/___bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithLeadingUnderscoreNames3, TestFindAllRefsWithLeadingUnderscoreNames3);

// findAllRefsWithLeadingUnderscoreNames4_test.go

// findAllRefsWithLeadingUnderscoreNames4_test.go
static void TestFindAllRefsWithLeadingUnderscoreNames4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    /*1*/public /*2*/____bar() { return 0; }
}

var x: Foo;
x./*3*/____bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithLeadingUnderscoreNames4, TestFindAllRefsWithLeadingUnderscoreNames4);

// findAllRefsWithLeadingUnderscoreNames5_test.go

// findAllRefsWithLeadingUnderscoreNames5_test.go
static void TestFindAllRefsWithLeadingUnderscoreNames5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    public _bar;
    public __bar;
    /*1*/public /*2*/___bar;
    public ____bar;
}

var x: Foo;
x._bar;
x.__bar;
x./*3*/___bar;
x.____bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithLeadingUnderscoreNames5, TestFindAllRefsWithLeadingUnderscoreNames5);

// findAllRefsWithLeadingUnderscoreNames6_test.go

// findAllRefsWithLeadingUnderscoreNames6_test.go
static void TestFindAllRefsWithLeadingUnderscoreNames6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    public _bar;
    /*1*/public /*2*/__bar;
    public ___bar;
    public ____bar;
}

var x: Foo;
x._bar;
x./*3*/__bar;
x.___bar;
x.____bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithLeadingUnderscoreNames6, TestFindAllRefsWithLeadingUnderscoreNames6);

// findAllRefsWithLeadingUnderscoreNames7_test.go

// findAllRefsWithLeadingUnderscoreNames7_test.go
static void TestFindAllRefsWithLeadingUnderscoreNames7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/function /*2*/__foo() {
    /*3*/__foo();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithLeadingUnderscoreNames7, TestFindAllRefsWithLeadingUnderscoreNames7);

// findAllRefsWithLeadingUnderscoreNames8_test.go

// findAllRefsWithLeadingUnderscoreNames8_test.go
static void TestFindAllRefsWithLeadingUnderscoreNames8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS((/*1*/function /*2*/__foo() {
    /*3*/__foo();
}))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithLeadingUnderscoreNames8, TestFindAllRefsWithLeadingUnderscoreNames8);

// findAllRefsWithLeadingUnderscoreNames9_test.go

// findAllRefsWithLeadingUnderscoreNames9_test.go
static void TestFindAllRefsWithLeadingUnderscoreNames9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS((/*1*/function /*2*/___foo() {
    /*3*/___foo();
}))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithLeadingUnderscoreNames9, TestFindAllRefsWithLeadingUnderscoreNames9);

// findAllRefsWithShorthandPropertyAssignment2_test.go

// findAllRefsWithShorthandPropertyAssignment2_test.go
static void TestFindAllRefsWithShorthandPropertyAssignment2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var /*0*/dx = "Foo";

namespace M { export var /*1*/dx; }
namespace M {
   var z = 100;
   export var y = { /*2*/dx, z };
}
M.y./*3*/dx;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithShorthandPropertyAssignment2, TestFindAllRefsWithShorthandPropertyAssignment2);

// findAllRefsWithShorthandPropertyAssignment_test.go

// findAllRefsWithShorthandPropertyAssignment_test.go
static void TestFindAllRefsWithShorthandPropertyAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
var /*0*/name = "Foo";

var obj = { /*1*/name };
var obj1 = { /*2*/name: /*3*/name };
obj./*4*/name;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "3", "1", "2", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWithShorthandPropertyAssignment, TestFindAllRefsWithShorthandPropertyAssignment);

// findAllRefsWriteAccess_test.go

// findAllRefsWriteAccess_test.go
static void TestFindAllRefsWriteAccess(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((((((((((std::string(R"TS(interface Obj {
    [)TS") + "`") + std::string(R"TS(/*1*/num)TS")) + std::string("`")) + std::string(R"TS(]: number;
}

let o: Obj = {
    [)TS")) + std::string("`")) + std::string(R"TS(num)TS")) + std::string("`")) + std::string(R"TS(]: 0
};

o = {
    ['num']: 1
};

o['num'] = 2;
o[)TS")) + std::string("`")) + std::string(R"TS(num)TS")) + std::string("`")) + std::string(R"TS(] = 3;

o['num'];
o[)TS")) + std::string("`")) + std::string(R"TS(num)TS")) + std::string("`")) + std::string(R"TS(];)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsWriteAccess, TestFindAllRefsWriteAccess);

// findAllRefs_importType_js1_test.go

// findAllRefs_importType_js1_test.go
static void TestFindAllRefs_importType_js1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
module.exports = class /**/C {};
module.exports.D = class D {};
// @Filename: /b.js
/** @type {import("./a")} */
const x = 0;
/** @type {import("./a").D} */
const y = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefs_importType_js1, TestFindAllRefs_importType_js1);

// findAllRefs_importType_js2_test.go

// findAllRefs_importType_js2_test.go
static void TestFindAllRefs_importType_js2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
module.exports = class C {};
module.exports./**/D = class D {};
// @Filename: /b.js
/** @type {import("./a")} */
const x = 0;
/** @type {import("./a").D} */
const y = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefs_importType_js2, TestFindAllRefs_importType_js2);

// findAllRefs_importType_js3_test.go

// findAllRefs_importType_js3_test.go
static void TestFindAllRefs_importType_js3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
module.exports = class C {};
module.exports.D = class /**/D {};
// @Filename: /b.js
/** @type {import("./a")} */
const x = 0;
/** @type {import("./a").D} */
const y = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefs_importType_js3, TestFindAllRefs_importType_js3);

// findAllRefs_importType_js4_test.go

// findAllRefs_importType_js4_test.go
static void TestFindAllRefs_importType_js4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @allowJs: true
// @checkJs: true
// @Filename: /a.js
/**
 * @callback /**/A
 * @param {unknown} response
 */

module.exports = {};
// @Filename: /b.js
/** @typedef {import("./a").A} A */)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefs_importType_js4, TestFindAllRefs_importType_js4);

// findAllRefs_importType_js_test.go

// findAllRefs_importType_js_test.go
static void TestFindAllRefs_importType_js(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		t->Skip({"Go *_js_test.go file: GOOS js-gated, never compiled on this platform"}); return;
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
/**/module.exports = class C {};
module.exports.D = class D {};
// @Filename: /b.js
/** @type {import("./a")} */
const x = 0;
/** @type {import("./a").D} */
const y = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefs_importType_js, TestFindAllRefs_importType_js);

// findAllRefs_importType_meaningAtLocation_test.go

// findAllRefs_importType_meaningAtLocation_test.go
static void TestFindAllRefs_importType_meaningAtLocation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
/*1*/export type /*2*/T = 0;
/*3*/export const /*4*/T = 0;
// @Filename: /b.ts
const x: import("./a")./*5*/T = 0;
const x: typeof import("./a")./*6*/T = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefs_importType_meaningAtLocation, TestFindAllRefs_importType_meaningAtLocation);

// findAllRefs_importType_named_test.go

// findAllRefs_importType_named_test.go
static void TestFindAllRefs_importType_named(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
/*1*/export type /*2*/T = number;
/*3*/export type /*4*/U = string;
// @Filename: /b.ts
const x: import("./a")./*5*/T = 0;
const x: import("./a")./*6*/U = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefs_importType_named, TestFindAllRefs_importType_named);

// findAllRefs_importType_typeofImport_test.go

// findAllRefs_importType_typeofImport_test.go
static void TestFindAllRefs_importType_typeofImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const x = 0;
// @Filename: /b.ts
/*1*/const x: typeof import("/*2*/./a") = { x: 0 };
/*3*/const y: typeof import("/*4*/./a") = { x: 0 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefs_importType_typeofImport, TestFindAllRefs_importType_typeofImport);

// findAllRefs_jsEnum_test.go

// findAllRefs_jsEnum_test.go
static void TestFindAllRefs_jsEnum(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/** @enum {string} */
/*1*/const /*2*/E = { A: "" };
/*3*/E["A"];
/** @type {/*4*/E} */
const e = /*5*/E.A;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefs_jsEnum, TestFindAllRefs_jsEnum);

// findReferencesAcrossMultipleProjectsVS_test.go

// findReferencesAcrossMultipleProjectsVS_test.go
static void TestFindReferencesAcrossMultipleProjectsVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: a.ts
/*1*/var /*2*/x: number;
//@Filename: b.ts
/// <reference path="a.ts" />
/*3*/x++;
//@Filename: c.ts
/// <reference path="a.ts" />
/*4*/x++;)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineVSFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindReferencesAcrossMultipleProjectsVS, TestFindReferencesAcrossMultipleProjectsVS);

// findReferencesAcrossMultipleProjects_test.go

// findReferencesAcrossMultipleProjects_test.go
static void TestFindReferencesAcrossMultipleProjects(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: a.ts
/*1*/var /*2*/x: number;
//@Filename: b.ts
/// <reference path="a.ts" />
/*3*/x++;
//@Filename: c.ts
/// <reference path="a.ts" />
/*4*/x++;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindReferencesAcrossMultipleProjects, TestFindReferencesAcrossMultipleProjects);

// findReferencesAfterEdit_test.go

// findReferencesAfterEdit_test.go
static void TestFindReferencesAfterEdit(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
interface A {
    /*1*/foo: string;
}
// @Filename: b.ts
///<reference path='a.ts'/>
/**/
function foo(x: A) {
    x./*2*/foo
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
		f->GoToMarker(t, "");
		f->Insert(t, R"TS(
)TS");
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindReferencesAfterEdit, TestFindReferencesAfterEdit);

// findReferencesBindingPatternInJsdocNoCrash1_test.go

// findReferencesBindingPatternInJsdocNoCrash1_test.go
static void TestFindReferencesBindingPatternInJsdocNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: node_modules/use-query/package.json
{
  "name": "use-query",
  "types": "index.d.ts"
}
// @Filename: node_modules/use-query/index.d.ts
declare function useQuery(): {
  data: string[];
};
// @Filename: node_modules/other/package.json
{
  "name": "other",
  "types": "index.d.ts"
}
// @Filename: node_modules/other/index.d.ts
interface BottomSheetModalProps {
  /**
   * A scrollable node or normal view.
   * @type {({ data: any }?) => any}
   */
  children: ({ data: any }?) => any;
}
// @Filename: src/index.ts
import { useQuery } from "use-query";
const { /*1*/data } = useQuery();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindReferencesBindingPatternInJsdocNoCrash1, TestFindReferencesBindingPatternInJsdocNoCrash1);

// findReferencesBindingPatternInJsdocNoCrash2_test.go

// findReferencesBindingPatternInJsdocNoCrash2_test.go
static void TestFindReferencesBindingPatternInJsdocNoCrash2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: node_modules/use-query/package.json
{
  "name": "use-query",
  "types": "index.d.ts"
}
// @Filename: node_modules/use-query/index.d.ts
declare function useQuery(): {
  data: string[];
};
// @Filename: node_modules/use-query/package.json
{
  "name": "other",
  "types": "index.d.ts"
}
// @Filename: node_modules/other/index.d.ts
interface BottomSheetModalProps {
  /**
   * A scrollable node or normal view.
   * @type null | (({ data: any }?) => any)
   */
  children: null | (({ data: any }?) => any);
}
// @Filename: src/index.ts
import { useQuery } from "use-query";
const { /*1*/data } = useQuery();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindReferencesBindingPatternInJsdocNoCrash2, TestFindReferencesBindingPatternInJsdocNoCrash2);

// findReferencesDefinitionDisplayParts_test.go

// findReferencesDefinitionDisplayParts_test.go
static void TestFindReferencesDefinitionDisplayParts(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Gre/*1*/eter {
    someFunction() { th/*2*/is;  }
}

type Options = "opt/*3*/ion 1" | "option 2";
let myOption: Options = "option 1";

some/*4*/Label:
break someLabel;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindReferencesDefinitionDisplayParts, TestFindReferencesDefinitionDisplayParts);

// findReferencesJSXTagName2_test.go

// findReferencesJSXTagName2_test.go
static void TestFindReferencesJSXTagName2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: index.tsx
/*1*/const /*2*/obj = {Component: () => <div/>};
const element = </*3*/obj.Component/>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindReferencesJSXTagName2, TestFindReferencesJSXTagName2);

// findReferencesJSXTagName3_test.go

// findReferencesJSXTagName3_test.go
static void TestFindReferencesJSXTagName3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: preserve
// @Filename: /a.tsx
namespace JSX {
    export interface Element { }
    export interface IntrinsicElements {
        [|[|/*1*/div|]: any;|]
    }
}

[|const [|/*6*/Comp|] = () =>
    [|<[|/*2*/div|]>
        Some content
        [|<[|/*3*/div|]>More content</[|/*4*/div|]>|]
    </[|/*5*/div|]>|];|]

const x = [|<[|/*7*/Comp|]>
    Content
</[|/*8*/Comp|]>|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8"});
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[1], f->Ranges()[5], f->Ranges()[7], f->Ranges()[8], f->Ranges()[9]});
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[3], f->Ranges()[11], f->Ranges()[12]});
	});
}
REGISTER_FOURSLASH_TEST(TestFindReferencesJSXTagName3, TestFindReferencesJSXTagName3);

// findReferencesJSXTagName_test.go

// findReferencesJSXTagName_test.go
static void TestFindReferencesJSXTagName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: index.tsx
import { /*1*/SubmissionComp } from "./RedditSubmission"
function displaySubreddit(subreddit: string) {
    let components = submissions
        .map((value, index) => <SubmissionComp key={ index } elementPosition= { index } {...value.data} />);
}
// @Filename: RedditSubmission.ts
export const /*2*/SubmissionComp = (submission: SubmissionProps) =>
    <div style={{ fontFamily: "sans-serif" }}></div>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindReferencesJSXTagName, TestFindReferencesJSXTagName);

// findReferencesSeeTagInTs_test.go

// findReferencesSeeTagInTs_test.go
static void TestFindReferencesSeeTagInTs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function doStuffWithStuff/*1*/(stuff: { quantity: number }) {}

declare const stuff: { quantity: number };
/** @see {doStuffWithStuff} */
if (stuff.quantity) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindReferencesSeeTagInTs, TestFindReferencesSeeTagInTs);

// references01_test.go

// references01_test.go
static void TestReferences01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: /home/src/workspaces/project/referencesForGlobals_1.ts
class /*0*/globalClass {
    public f() { }
}
// @Filename: /home/src/workspaces/project/referencesForGlobals_2.ts
///<reference path="referencesForGlobals_1.ts" />
var c = /*1*/globalClass();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferences01, TestReferences01);

// referencesBloomFilters2_test.go

// referencesBloomFilters2_test.go
static void TestReferencesBloomFilters2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: declaration.ts
var container = { /*1*/42: 1 };
// @Filename: expression.ts
function blah() { return (container[42]) === 2;  };
// @Filename: stringIndexer.ts
function blah2() { container["42"] };
// @Filename: redeclaration.ts
container = { "42" : 18 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesBloomFilters2, TestReferencesBloomFilters2);

// referencesBloomFilters3_test.go

// referencesBloomFilters3_test.go
static void TestReferencesBloomFilters3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: declaration.ts
enum Test { /*1*/"/*2*/42" = 1 };
// @Filename: expression.ts
(Test[/*3*/42]);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesBloomFilters3, TestReferencesBloomFilters3);

// referencesBloomFilters_test.go

// referencesBloomFilters_test.go
static void TestReferencesBloomFilters(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: declaration.ts
var container = { /*1*/searchProp : 1 };
// @Filename: expression.ts
function blah() { return (1 + 2 + container.searchProp()) === 2;  };
// @Filename: stringIndexer.ts
function blah2() { container["searchProp"] };
// @Filename: redeclaration.ts
container = { "searchProp" : 18 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesBloomFilters, TestReferencesBloomFilters);

// referencesForAmbients2_test.go

// referencesForAmbients2_test.go
static void TestReferencesForAmbients2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /defA.ts
declare module "a" {
    /*1*/export type /*2*/T = number;
}
// @Filename: /defB.ts
declare module "b" {
    export import a = require("a");
    export const x: a./*3*/T;
}
// @Filename: /defC.ts
declare module "c" {
    import b = require("b");
    const x: b.a./*4*/T;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForAmbients2, TestReferencesForAmbients2);

// referencesForAmbients_test.go

// referencesForAmbients_test.go
static void TestReferencesForAmbients(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/declare module "/*2*/foo" {
    /*3*/var /*4*/f: number;
}

/*5*/declare module "/*6*/bar" {
    /*7*/export import /*8*/foo = require("/*9*/foo");
    var f2: typeof /*10*/foo./*11*/f;
}

declare module "baz" {
    /*12*/import bar = require("/*13*/bar");
    var f2: typeof bar./*14*/foo;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForAmbients, TestReferencesForAmbients);

// referencesForClassLocal_test.go

// referencesForClassLocal_test.go
static void TestReferencesForClassLocal(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var n = 14;

class foo {
    /*1*/private /*2*/n = 0;

    public bar() {
        this./*3*/n = 9;
    }

    constructor() {
        this./*4*/n = 4;
    }

    public bar2() {
        var n = 12;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForClassLocal, TestReferencesForClassLocal);

// referencesForClassMembersExtendingAbstractClass_test.go

// referencesForClassMembersExtendingAbstractClass_test.go
static void TestReferencesForClassMembersExtendingAbstractClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(abstract class Base {
    abstract /*a1*/a: number;
    abstract /*method1*/method(): void;
}
class MyClass extends Base {
    /*a2*/a;
    /*method2*/method() { }
}

var c: MyClass;
c./*a3*/a;
c./*method3*/method();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"a1", "a2", "a3", "method1", "method2", "method3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForClassMembersExtendingAbstractClass, TestReferencesForClassMembersExtendingAbstractClass);

// referencesForClassMembersExtendingGenericClass_test.go

// referencesForClassMembersExtendingGenericClass_test.go
static void TestReferencesForClassMembersExtendingGenericClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Base<T> {
    /*a1*/a: this;
    /*method1*/method<U>(a?:T, b?:U): this { }
}
class MyClass extends Base<number> {
    /*a2*/a;
    /*method2*/method() { }
}

var c: MyClass;
c./*a3*/a;
c./*method3*/method();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"a1", "a2", "a3", "method1", "method2", "method3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForClassMembersExtendingGenericClass, TestReferencesForClassMembersExtendingGenericClass);

// referencesForClassMembers_test.go

// referencesForClassMembers_test.go
static void TestReferencesForClassMembers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Base {
    /*a1*/a: number;
    /*method1*/method(): void { }
}
class MyClass extends Base {
    /*a2*/a;
    /*method2*/method() { }
}

var c: MyClass;
c./*a3*/a;
c./*method3*/method();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"a1", "a2", "a3", "method1", "method2", "method3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForClassMembers, TestReferencesForClassMembers);

// referencesForClassParameter_test.go

// referencesForClassParameter_test.go
static void TestReferencesForClassParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var p = 2;

class p { }

class foo {
    constructor (/*1*/public /*2*/p: any) {
    }

    public f(p) {
        this./*3*/p = p;
    }

}

var n = new foo(undefined);
n./*4*/p = null;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForClassParameter, TestReferencesForClassParameter);

// referencesForContextuallyTypedObjectLiteralProperties_test.go

// referencesForContextuallyTypedObjectLiteralProperties_test.go
static void TestReferencesForContextuallyTypedObjectLiteralProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface IFoo { /*xy*/xy: number; }

// Assignment
var a1: IFoo = { xy: 0 };
var a2: IFoo = { xy: 0 };

// Function call
function consumer(f: IFoo) { }
consumer({ xy: 1 });

// Type cast
var c = <IFoo>{ xy: 0 };

// Array literal
var ar: IFoo[] = [{ xy: 1 }, { xy: 2 }];

// Nested object literal
var ob: { ifoo: IFoo } = { ifoo: { xy: 0 } };

// Widened type
var w: IFoo = { xy: undefined };

// Untped -- should not be included
var u = { xy: 0 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"xy"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForContextuallyTypedObjectLiteralProperties, TestReferencesForContextuallyTypedObjectLiteralProperties);

// referencesForContextuallyTypedUnionProperties2_test.go

// referencesForContextuallyTypedUnionProperties2_test.go
static void TestReferencesForContextuallyTypedUnionProperties2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A {
    a: number;
    common: string;
}

interface B {
    /*1*/b: number;
    common: number;
}

// Assignment
var v1: A | B = { a: 0, common: "" };
var v2: A | B = { b: 0, common: 3 };

// Function call
function consumer(f:  A | B) { }
consumer({ a: 0, b: 0, common: 1 });

// Type cast
var c = <A | B> { common: 0, b: 0 };

// Array literal
var ar: Array<A|B> = [{ a: 0, common: "" }, { b: 0, common: 0 }];

// Nested object literal
var ob: { aorb: A|B } = { aorb: { b: 0, common: 0 } };

// Widened type
var w: A|B = { b:undefined, common: undefined };

// Untped -- should not be included
var u1 = { a: 0, b: 0, common: "" };
var u2 = { b: 0, common: 0 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForContextuallyTypedUnionProperties2, TestReferencesForContextuallyTypedUnionProperties2);

// referencesForContextuallyTypedUnionProperties_test.go

// referencesForContextuallyTypedUnionProperties_test.go
static void TestReferencesForContextuallyTypedUnionProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A {
    a: number;
    /*1*/common: string;
}

interface B {
    b: number;
    /*2*/common: number;
}

// Assignment
var v1: A | B = { a: 0, /*3*/common: "" };
var v2: A | B = { b: 0, /*4*/common: 3 };

// Function call
function consumer(f:  A | B) { }
consumer({ a: 0, b: 0, /*5*/common: 1 });

// Type cast
var c = <A | B> { /*6*/common: 0, b: 0 };

// Array literal
var ar: Array<A|B> = [{ a: 0, /*7*/common: "" }, { b: 0, /*8*/common: 0 }];

// Nested object literal
var ob: { aorb: A|B } = { aorb: { b: 0, /*9*/common: 0 } };

// Widened type
var w: A|B = { a:0, /*10*/common: undefined };

// Untped -- should not be included
var u1 = { a: 0, b: 0, common: "" };
var u2 = { b: 0, common: 0 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForContextuallyTypedUnionProperties, TestReferencesForContextuallyTypedUnionProperties);

// referencesForDeclarationKeywords_test.go

// referencesForDeclarationKeywords_test.go
static void TestReferencesForDeclarationKeywords(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Base {}
interface Implemented1 {}
/*classDecl1_classKeyword*/class C1 /*classDecl1_extendsKeyword*/extends Base /*classDecl1_implementsKeyword*/implements Implemented1 {
    /*getDecl_getKeyword*/get e() { return 1; }
    /*setDecl_setKeyword*/set e(v) {}
}
/*interfaceDecl1_interfaceKeyword*/interface I1 /*interfaceDecl1_extendsKeyword*/extends Base { }
/*typeDecl_typeKeyword*/type T = { }
/*enumDecl_enumKeyword*/enum E { }
/*namespaceDecl_namespaceKeyword*/namespace N { }
/*moduleDecl_moduleKeyword*/namespace M { }
/*functionDecl_functionKeyword*/function fn() {}
/*varDecl_varKeyword*/var x;
/*letDecl_letKeyword*/let y;
/*constDecl_constKeyword*/const z = 1;
interface Implemented2 {}
interface Implemented3 {}
class C2 /*classDecl2_implementsKeyword*/implements Implemented2, Implemented3 {}
interface I2 /*interfaceDecl2_extendsKeyword*/extends Implemented2, Implemented3 {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"classDecl1_classKeyword", "classDecl1_extendsKeyword", "classDecl1_implementsKeyword", "classDecl2_implementsKeyword", "getDecl_getKeyword", "setDecl_setKeyword", "interfaceDecl1_interfaceKeyword", "interfaceDecl1_extendsKeyword", "interfaceDecl2_extendsKeyword", "typeDecl_typeKeyword", "enumDecl_enumKeyword", "namespaceDecl_namespaceKeyword", "moduleDecl_moduleKeyword", "functionDecl_functionKeyword", "varDecl_varKeyword", "letDecl_letKeyword", "constDecl_constKeyword"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForDeclarationKeywords, TestReferencesForDeclarationKeywords);

// referencesForEnums_test.go

// referencesForEnums_test.go
static void TestReferencesForEnums(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    /*1*/value1 = 1,
    /*2*/"/*3*/value2" = /*4*/value1,
    /*5*/111 = 11
}

E./*6*/value1;
E["/*7*/value2"];
E./*8*/value2;
E[/*9*/111];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForEnums, TestReferencesForEnums);

// referencesForExportedValues_test.go

// referencesForExportedValues_test.go
static void TestReferencesForExportedValues(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace M {
    /*1*/export var /*2*/variable = 0;

    // local use
    var x = /*3*/variable;
}

// external use
M./*4*/variable)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForExportedValues, TestReferencesForExportedValues);

// referencesForExpressionKeywords_test.go

// referencesForExpressionKeywords_test.go
static void TestReferencesForExpressionKeywords(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    static x = 1;
}
/*new*/new C();
/*void*/void C;
/*typeof*/typeof C;
/*delete*/delete C.x;
/*async*/async function* f() {
    /*yield*/yield C;
    /*await*/await C;
}
"x" /*in*/in C;
undefined /*instanceof*/instanceof C;
undefined /*as*/as C;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"new", "void", "typeof", "yield", "await", "in", "instanceof", "as", "delete"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForExpressionKeywords, TestReferencesForExpressionKeywords);

// referencesForExternalModuleNames_test.go

// referencesForExternalModuleNames_test.go
static void TestReferencesForExternalModuleNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: referencesForGlobals_1.ts
/*1*/declare module "/*2*/foo" {
    var f: number;
}
// @Filename: referencesForGlobals_2.ts
/*3*/import f = require("/*4*/foo");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForExternalModuleNames, TestReferencesForExternalModuleNames);

// referencesForFunctionOverloads_test.go

// referencesForFunctionOverloads_test.go
static void TestReferencesForFunctionOverloads(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/function /*2*/foo(x: string);
/*3*/function /*4*/foo(x: string, y: number) {
    /*5*/foo('', 43);
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForFunctionOverloads, TestReferencesForFunctionOverloads);

// referencesForFunctionParameter_test.go

// referencesForFunctionParameter_test.go
static void TestReferencesForFunctionParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x;
var n;

function n(x: number, /*1*/n: number) {
    /*2*/n = 32;
    x = /*3*/n;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForFunctionParameter, TestReferencesForFunctionParameter);

// referencesForGlobals2_test.go

// referencesForGlobals2_test.go
static void TestReferencesForGlobals2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: referencesForGlobals_1.ts
/*1*/class /*2*/globalClass {
    public f() { }
}
// @Filename: referencesForGlobals_2.ts
var c = /*3*/globalClass();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForGlobals2, TestReferencesForGlobals2);

// referencesForGlobals3_test.go

// referencesForGlobals3_test.go
static void TestReferencesForGlobals3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: referencesForGlobals_1.ts
/*1*/interface /*2*/globalInterface {
     f();
}
// @Filename: referencesForGlobals_2.ts
var i: /*3*/globalInterface;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForGlobals3, TestReferencesForGlobals3);

// referencesForGlobals4_test.go

// referencesForGlobals4_test.go
static void TestReferencesForGlobals4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: referencesForGlobals_1.ts
/*1*/module /*2*/globalModule {
     export f() { };
}
// @Filename: referencesForGlobals_2.ts
var m = /*3*/globalModule;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForGlobals4, TestReferencesForGlobals4);

// referencesForGlobals5_test.go

// referencesForGlobals5_test.go
static void TestReferencesForGlobals5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: referencesForGlobals_1.ts
namespace globalModule {
    export var x;
}

/*1*/import /*2*/globalAlias = globalModule;
// @Filename: referencesForGlobals_2.ts
var m = /*3*/globalAlias;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForGlobals5, TestReferencesForGlobals5);

// referencesForGlobalsInExternalModule_test.go

// referencesForGlobalsInExternalModule_test.go
static void TestReferencesForGlobalsInExternalModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/var /*2*/topLevelVar = 2;
var topLevelVar2 = /*3*/topLevelVar;

/*4*/class /*5*/topLevelClass { }
var c = new /*6*/topLevelClass();

/*7*/interface /*8*/topLevelInterface { }
var i: /*9*/topLevelInterface;

/*10*/module /*11*/topLevelModule {
    export var x;
}
var x = /*12*/topLevelModule.x;

export = x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForGlobalsInExternalModule, TestReferencesForGlobalsInExternalModule);

// referencesForGlobals_test.go

// referencesForGlobals_test.go
static void TestReferencesForGlobals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: referencesForGlobals_1.ts
/*1*/var /*2*/global = 2;

class foo {
    constructor (public global) { }
    public f(global) { }
    public f2(global) { }
}

class bar {
    constructor () {
        var n = /*3*/global;

        var f = new foo('');
        f.global = '';
    }
}

var k = /*4*/global;
// @Filename: referencesForGlobals_2.ts
var m = /*5*/global;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForGlobals, TestReferencesForGlobals);

// referencesForIllegalAssignment_test.go

// referencesForIllegalAssignment_test.go
static void TestReferencesForIllegalAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(f/*1*/oo = fo/*2*/o;
var /*bar*/bar = function () { };
bar = bar + 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "bar"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForIllegalAssignment, TestReferencesForIllegalAssignment);

// referencesForImports_test.go

// referencesForImports_test.go
static void TestReferencesForImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare module "jquery" {
    function $(s: string): any;
    export = $;
}
/*1*/import /*2*/$ = require("jquery");
/*3*/$("a");
/*4*/import /*5*/$ = require("jquery");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForImports, TestReferencesForImports);

// referencesForIndexProperty2_test.go

// referencesForIndexProperty2_test.go
static void TestReferencesForIndexProperty2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var a;
a["/*1*/blah"];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForIndexProperty2, TestReferencesForIndexProperty2);

// referencesForIndexProperty3_test.go

// referencesForIndexProperty3_test.go
static void TestReferencesForIndexProperty3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Object {
    /*1*/toMyString();
}

var y: Object;
y./*2*/toMyString();

var x = {};
x["/*3*/toMyString"]();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForIndexProperty3, TestReferencesForIndexProperty3);

// referencesForIndexProperty_test.go

// referencesForIndexProperty_test.go
static void TestReferencesForIndexProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    /*1*/property: number;
    /*2*/method(): void { }
}

var f: Foo;
f["/*3*/property"];
f["/*4*/method"];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForIndexProperty, TestReferencesForIndexProperty);

// referencesForInheritedProperties10_test.go

// referencesForInheritedProperties10_test.go
static void TestReferencesForInheritedProperties10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface IFeedbackHandler {
  /*1*/handleAccept?(): void;
  handleReject?(): void;
}

abstract class AbstractFeedbackHandler implements IFeedbackHandler {}

class FeedbackHandler extends AbstractFeedbackHandler {
  /*2*/handleAccept(): void {
    console.log("Feedback accepted");
  }

  handleReject(): void {
    console.log("Feedback rejected");
  }
}

function foo(handler: IFeedbackHandler) {
  handler./*3*/handleAccept?.();
  handler.handleReject?.();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForInheritedProperties10, TestReferencesForInheritedProperties10);

// referencesForInheritedProperties2_test.go

// referencesForInheritedProperties2_test.go
static void TestReferencesForInheritedProperties2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface interface1 {
    /*1*/doStuff(): void;
}

interface interface2 {
    doStuff(): void;
}

interface interface2 extends interface1 {
}

class class1 implements interface2 {
    doStuff() {

    }
}

class class2 extends class1 {

}

var v: class2;
v.doStuff();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForInheritedProperties2, TestReferencesForInheritedProperties2);

// referencesForInheritedProperties3_test.go

// referencesForInheritedProperties3_test.go
static void TestReferencesForInheritedProperties3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface interface1 extends interface1 {
   /*1*/doStuff(): void;
   /*2*/propName: string;
}

var v: interface1;
v./*3*/propName;
v./*4*/doStuff();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForInheritedProperties3, TestReferencesForInheritedProperties3);

// referencesForInheritedProperties4_test.go

// referencesForInheritedProperties4_test.go
static void TestReferencesForInheritedProperties4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class class1 extends class1 {
   /*1*/doStuff() { }
   /*2*/propName: string;
}

var c: class1;
c./*3*/doStuff();
c./*4*/propName;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForInheritedProperties4, TestReferencesForInheritedProperties4);

// referencesForInheritedProperties5_test.go

// referencesForInheritedProperties5_test.go
static void TestReferencesForInheritedProperties5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface interface1 extends interface1 {
   /*1*/doStuff(): void;
   /*2*/propName: string;
}
interface interface2 extends interface1 {
   doStuff(): void;
   propName: string;
}

var v: interface1;
v.propName;
v.doStuff();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForInheritedProperties5, TestReferencesForInheritedProperties5);

// referencesForInheritedProperties6_test.go

// referencesForInheritedProperties6_test.go
static void TestReferencesForInheritedProperties6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class class1 extends class1 {
    /*1*/doStuff() { }
}
class class2 extends class1 {
    doStuff() { }
}

var v: class2;
v.doStuff();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForInheritedProperties6, TestReferencesForInheritedProperties6);

// referencesForInheritedProperties7_test.go

// referencesForInheritedProperties7_test.go
static void TestReferencesForInheritedProperties7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class class1 extends class1 {
   /*0*/doStuff() { }
   /*1*/propName: string;
}
interface interface1 extends interface1 {
   /*2*/doStuff(): void;
   /*3*/propName: string;
}
class class2 extends class1 implements interface1 {
   /*4*/doStuff() { }
   /*5*/propName: string;
}

var v: class2;
v.doStuff();
v.propName;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForInheritedProperties7, TestReferencesForInheritedProperties7);

// referencesForInheritedProperties8_test.go

// referencesForInheritedProperties8_test.go
static void TestReferencesForInheritedProperties8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface C extends D {
    /*d*/propD: number;
}
interface D extends C {
    propD: string;
    /*c*/propC: number;
}
var d: D;
d.propD;
d.propC;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"d", "c"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForInheritedProperties8, TestReferencesForInheritedProperties8);

// referencesForInheritedProperties9_test.go

// referencesForInheritedProperties9_test.go
static void TestReferencesForInheritedProperties9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class D extends C {
    /*1*/prop1: string;
}

class C extends D {
    /*2*/prop1: string;
}

var c: C;
c./*3*/prop1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForInheritedProperties9, TestReferencesForInheritedProperties9);

// referencesForInheritedProperties_test.go

// referencesForInheritedProperties_test.go
static void TestReferencesForInheritedProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface interface1 {
    /*1*/doStuff(): void;
}

interface interface2  extends interface1{
    /*2*/doStuff(): void;
}

class class1 implements interface2 {
    /*3*/doStuff() {

    }
}

class class2 extends class1 {

}

var v: class2;
v./*4*/doStuff();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForInheritedProperties, TestReferencesForInheritedProperties);

// referencesForLabel2_test.go

// referencesForLabel2_test.go
static void TestReferencesForLabel2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var label = "label";
while (true) {
    if (false) break /**/label;
    if (true) continue label;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForLabel2, TestReferencesForLabel2);

// referencesForLabel3_test.go

// referencesForLabel3_test.go
static void TestReferencesForLabel3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/label: while (true) {
    var label = "label";
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForLabel3, TestReferencesForLabel3);

// referencesForLabel4_test.go

// referencesForLabel4_test.go
static void TestReferencesForLabel4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/label: function foo(label) {
    while (true) {
        /*2*/break /*3*/label;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForLabel4, TestReferencesForLabel4);

// referencesForLabel5_test.go

// referencesForLabel5_test.go
static void TestReferencesForLabel5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/label:  while (true) {
            if (false) /*2*/break /*3*/label;
            function blah() {
/*4*/label:          while (true) {
                    if (false) /*5*/break /*6*/label;
                }
            }
            if (false) /*7*/break /*8*/label;
        })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForLabel5, TestReferencesForLabel5);

// referencesForLabel6_test.go

// referencesForLabel6_test.go
static void TestReferencesForLabel6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/labela: while (true) {
/*2*/labelb:     while (false) { /*3*/break /*4*/labelb; }
            break labelc;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForLabel6, TestReferencesForLabel6);

// referencesForLabel_test.go

// referencesForLabel_test.go
static void TestReferencesForLabel(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/label: while (true) {
    if (false) /*2*/break /*3*/label;
    if (true) /*4*/continue /*5*/label;
}

/*6*/label: while (false) { }
var label = "label";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForLabel, TestReferencesForLabel);

// referencesForMergedDeclarations2_test.go

// referencesForMergedDeclarations2_test.go
static void TestReferencesForMergedDeclarations2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace ATest {
    export interface Bar { }
}

function ATest() { }

/*1*/import /*2*/alias = ATest; // definition

var a: /*3*/alias.Bar; // namespace
/*4*/alias.call(this); // value)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForMergedDeclarations2, TestReferencesForMergedDeclarations2);

// referencesForMergedDeclarations3_test.go

// referencesForMergedDeclarations3_test.go
static void TestReferencesForMergedDeclarations3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|class /*class*/[|testClass|] {
    static staticMethod() { }
    method() { }
}|]

[|module /*module*/[|testClass|] {
    export interface Bar {

    }
}|]

var c1: [|testClass|];
var c2: [|testClass|].Bar;
[|testClass|].staticMethod();
[|testClass|].prototype.method();
[|testClass|].bind(this);
new [|testClass|]();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"module", "class"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForMergedDeclarations3, TestReferencesForMergedDeclarations3);

// referencesForMergedDeclarations4_test.go

// referencesForMergedDeclarations4_test.go
static void TestReferencesForMergedDeclarations4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/class /*2*/testClass {
    static staticMethod() { }
    method() { }
}

/*3*/module /*4*/testClass {
    export interface Bar {

    }
    export var s = 0;
}

var c1: /*5*/testClass;
var c2: /*6*/testClass.Bar;
/*7*/testClass.staticMethod();
/*8*/testClass.prototype.method();
/*9*/testClass.bind(this);
/*10*/testClass.s;
new /*11*/testClass();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForMergedDeclarations4, TestReferencesForMergedDeclarations4);

// referencesForMergedDeclarations5_test.go

// referencesForMergedDeclarations5_test.go
static void TestReferencesForMergedDeclarations5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface /*1*/Foo { }
module /*2*/Foo { export interface Bar { } }
function /*3*/Foo() { }

export = /*4*/Foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForMergedDeclarations5, TestReferencesForMergedDeclarations5);

// referencesForMergedDeclarations6_test.go

// referencesForMergedDeclarations6_test.go
static void TestReferencesForMergedDeclarations6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo { }
/*1*/module /*2*/Foo {
    export interface Bar { }
    export namespace Bar { export interface Baz { } }
    export function Bar() { }
}

// module
import a1 = /*3*/Foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForMergedDeclarations6, TestReferencesForMergedDeclarations6);

// referencesForMergedDeclarations7_test.go

// referencesForMergedDeclarations7_test.go
static void TestReferencesForMergedDeclarations7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo { }
namespace Foo {
    export interface /*1*/Bar { }
    export module /*2*/Bar { export interface Baz { } }
    export function /*3*/Bar() { }
}

// module, value and type
import a2 = Foo./*4*/Bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForMergedDeclarations7, TestReferencesForMergedDeclarations7);

// referencesForMergedDeclarations8_test.go

// referencesForMergedDeclarations8_test.go
static void TestReferencesForMergedDeclarations8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo { }
namespace Foo {
    export interface Bar { }
    /*1*/export module /*2*/Bar { export interface Baz { } }
    export function Bar() { }
}

// module
import a3 = Foo./*3*/Bar.Baz;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForMergedDeclarations8, TestReferencesForMergedDeclarations8);

// referencesForMergedDeclarations_test.go

// referencesForMergedDeclarations_test.go
static void TestReferencesForMergedDeclarations(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/interface /*2*/Foo {
}

/*3*/module /*4*/Foo {
    export interface Bar { }
}

/*5*/function /*6*/Foo(): void {
}

var f1: /*7*/Foo.Bar;
var f2: /*8*/Foo;
/*9*/Foo.bind(this);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForMergedDeclarations, TestReferencesForMergedDeclarations);

// referencesForModifiers_test.go

// referencesForModifiers_test.go
static void TestReferencesForModifiers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
[|/*declareModifier*/declare /*abstractModifier*/abstract class C1 {
    [|/*staticModifier*/static a;|]
    [|/*readonlyModifier*/readonly b;|]
    [|/*publicModifier*/public c;|]
    [|/*protectedModifier*/protected d;|]
    [|/*privateModifier*/private e;|]
}|]
[|/*constModifier*/const enum E {
}|]
[|/*asyncModifier*/async function fn() {}|]
[|/*exportModifier*/export /*defaultModifier*/default class C2 {}|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"declareModifier", "abstractModifier", "staticModifier", "readonlyModifier", "publicModifier", "protectedModifier", "privateModifier", "constModifier", "asyncModifier", "exportModifier", "defaultModifier"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForModifiers, TestReferencesForModifiers);

// referencesForNoContext_test.go

// referencesForNoContext_test.go
static void TestReferencesForNoContext(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace modTest {
    //Declare
    export var modVar:number;
    /*1*/

    //Increments
    modVar++;

    class testCls{
        /*2*/
    }

    function testFn(){
        //Increments
        modVar++;
    }  /*3*/
/*4*/
    namespace testMod {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForNoContext, TestReferencesForNoContext);

// referencesForNumericLiteralPropertyNames_test.go

// referencesForNumericLiteralPropertyNames_test.go
static void TestReferencesForNumericLiteralPropertyNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    public /*1*/12: any;
}

var x: Foo;
x[12];
x = { "12": 0 };
x = { 12: 0 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForNumericLiteralPropertyNames, TestReferencesForNumericLiteralPropertyNames);

// referencesForObjectLiteralProperties_test.go

// referencesForObjectLiteralProperties_test.go
static void TestReferencesForObjectLiteralProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = { /*1*/add: 0, b: "string" };
x["/*2*/add"];
x./*3*/add;
var y = x;
y./*4*/add;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForObjectLiteralProperties, TestReferencesForObjectLiteralProperties);

// referencesForOverrides_test.go

// referencesForOverrides_test.go
static void TestReferencesForOverrides(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace FindRef3 {
	namespace SimpleClassTest {
		export class Foo {
			public /*foo*/foo(): void {
			}
		}
		export class Bar extends Foo {
			public foo(): void {
			}
		}
	}

	namespace SimpleInterfaceTest {
		export interface IFoo {
			/*ifoo*/ifoo(): void;
		}
		export interface IBar extends IFoo {
			ifoo(): void;
		}
	}

	namespace SimpleClassInterfaceTest {
		export interface IFoo {
			/*icfoo*/icfoo(): void;
		}
		export class Bar implements IFoo {
			public icfoo(): void {
			}
		}
	}

	namespace Test {
		export interface IBase {
			/*field*/field: string;
			/*method*/method(): void;
		}

		export interface IBlah extends IBase {
			field: string;
		}

		export interface IBlah2 extends IBlah {
			field: string;
		}

		export interface IDerived extends IBlah2 {
			method(): void;
		}

		export class Bar implements IDerived {
			public field: string;
			public method(): void { }
		}

		export class BarBlah extends Bar {
			public field: string;
		}
	}

	function test() {
		var x = new SimpleClassTest.Bar();
		x.foo();

		var y: SimpleInterfaceTest.IBar = null;
		y.ifoo();

        var w: SimpleClassInterfaceTest.Bar = null;
        w.icfoo();

		var z = new Test.BarBlah();
		z.field = "";
        z.method();
	}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"foo", "ifoo", "icfoo", "field", "method"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForOverrides, TestReferencesForOverrides);

// referencesForPropertiesOfGenericType_test.go

// referencesForPropertiesOfGenericType_test.go
static void TestReferencesForPropertiesOfGenericType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface IFoo<T> {
    /*1*/doSomething(v: T): T;
}

var x: IFoo<string>;
x./*2*/doSomething("ss");

var y: IFoo<number>;
y./*3*/doSomething(12);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForPropertiesOfGenericType, TestReferencesForPropertiesOfGenericType);

// referencesForStatementKeywords_test.go

// referencesForStatementKeywords_test.go
static void TestReferencesForStatementKeywords(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /main.ts
// import ... = ...
[|{| "id": "importEqualsDecl1" |}/*importEqualsDecl1_importKeyword*/[|import|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "importEqualsDecl1" |}A|] = /*importEqualsDecl1_requireKeyword*/[|require|]("[|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "importEqualsDecl1" |}./a|]");|]
[|{| "id": "namespaceDecl1" |}namespace [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "namespaceDecl1" |}N|] { }|]
[|{| "id": "importEqualsDecl2" |}/*importEqualsDecl2_importKeyword*/[|import|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "importEqualsDecl2" |}N2|] = [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "importEqualsDecl2" |}N|];|]

// import ... from ...
[|{| "id": "importDecl1" |}/*importDecl1_importKeyword*/[|import|] /*importDecl1_typeKeyword*/[|type|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "importDecl1" |}B|] /*importDecl1_fromKeyword*/[|from|] "[|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "importDecl1" |}./b|]";|]
[|{| "id": "importDecl2" |}/*importDecl2_importKeyword*/[|import|] /*importDecl2_typeKeyword*/[|type|] * /*importDecl2_asKeyword*/[|as|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "importDecl2" |}C|] /*importDecl2_fromKeyword*/[|from|] "[|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "importDecl2" |}./c|]";|]
[|{| "id": "importDecl3" |}/*importDecl3_importKeyword*/[|import|] /*importDecl3_typeKeyword*/[|type|] { [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "importDecl3" |}D|] } /*importDecl3_fromKeyword*/[|from|] "[|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "importDecl3" |}./d|]";|]
[|{| "id": "importDecl4" |}/*importDecl4_importKeyword*/[|import|] /*importDecl4_typeKeyword*/[|type|] { e1, e2 /*importDecl4_asKeyword*/[|as|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "importDecl4" |}e3|] } /*importDecl4_fromKeyword*/[|from|] "[|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "importDecl4" |}./e|]";|]

// import "module"
[|{| "id": "importDecl5" |}/*importDecl5_importKeyword*/[|import|] "[|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "importDecl5" |}./f|]";|]

// export ... from ...
[|{| "id": "exportDecl1" |}/*exportDecl1_exportKeyword*/[|export|] /*exportDecl1_typeKeyword*/[|type|] * /*exportDecl1_fromKeyword*/[|from|] "[|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "exportDecl1" |}./g|]";|]
[|{| "id": "exportDecl2" |}/*exportDecl2_exportKeyword*/[|export|] /*exportDecl2_typeKeyword*/[|type|] [|{| "id": "exportDecl2_namespaceExport" |}* /*exportDecl2_asKeyword*/[|as|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "exportDecl2" |}H|]|] /*exportDecl2_fromKeyword*/[|from|] "[|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "exportDecl2" |}./h|]";|]
[|{| "id": "exportDecl3" |}/*exportDecl3_exportKeyword*/[|export|] /*exportDecl3_typeKeyword*/[|type|] { [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "exportDecl3" |}I|] } /*exportDecl3_fromKeyword*/[|from|] "[|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "exportDecl3" |}./i|]";|]
[|{| "id": "exportDecl4" |}/*exportDecl4_exportKeyword*/[|export|] /*exportDecl4_typeKeyword*/[|type|] { j1, j2 /*exportDecl4_asKeyword*/[|as|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "exportDecl4" |}j3|] } /*exportDecl4_fromKeyword*/[|from|] "[|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "exportDecl4" |}./j|]";|]
[|{| "id": "typeDecl1" |}type [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "typeDecl1" |}Z1|] = 1;|]
[|{| "id": "exportDecl5" |}/*exportDecl5_exportKeyword*/[|export|] /*exportDecl5_typeKeyword*/[|type|] { [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "exportDecl5" |}Z1|] };|]
type Z2 = 2;
type Z3 = 3;
[|{| "id": "exportDecl6" |}/*exportDecl6_exportKeyword*/[|export|] /*exportDecl6_typeKeyword*/[|type|] { z2, z3 /*exportDecl6_asKeyword*/[|as|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "exportDecl6" |}z4|] };|]
// @filename: /main2.ts
[|{| "id": "varDecl1" |}const [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "varDecl1" |}x|] = {};|]
[|{| "id": "exportAssignment1" |}/*exportAssignment1_exportKeyword*/[|export|] = [|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "exportAssignment1"|}x|];|]
// @filename: /main3.ts
[|{| "id": "varDecl3" |}const [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "varDecl3" |}y|] = {};|]
[|{| "id": "exportAssignment2" |}/*exportAssignment2_exportKeyword*/[|export|] [|default|] [|{| "isWriteAccess": false, "isDefinition": false, "contextRangeId": "exportAssignment2"|}y|];|]
// @filename: /a.ts
export const a = 1;
// @filename: /b.ts
[|{| "id": "classDecl1" |}export default class [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "classDecl1" |}B|] {}|]
// @filename: /c.ts
export const c = 1;
// @filename: /d.ts
[|{| "id": "classDecl2" |}export class [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "classDecl2" |}D|] {}|]
// @filename: /e.ts
export const e1 = 1;
export const e2 = 2;
// @filename: /f.ts
export const f = 1;
// @filename: /g.ts
export const g = 1;
// @filename: /h.ts
export const h = 1;
// @filename: /i.ts
[|{| "id": "classDecl3" |}export class [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "classDecl3" |}I|] {}|]
// @filename: /j.ts
export const j1 = 1;
export const j2 = 2;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"importEqualsDecl1_importKeyword", "importEqualsDecl1_requireKeyword", "importEqualsDecl2_importKeyword", "importDecl1_importKeyword", "importDecl1_typeKeyword", "importDecl1_fromKeyword", "importDecl2_importKeyword", "importDecl2_typeKeyword", "importDecl2_asKeyword", "importDecl2_fromKeyword", "importDecl3_importKeyword", "importDecl3_typeKeyword", "importDecl3_fromKeyword", "importDecl4_importKeyword", "importDecl4_typeKeyword", "importDecl4_fromKeyword", "importDecl4_asKeyword", "importDecl5_importKeyword", "exportDecl1_exportKeyword", "exportDecl1_typeKeyword", "exportDecl1_fromKeyword", "exportDecl2_exportKeyword", "exportDecl2_typeKeyword", "exportDecl2_asKeyword", "exportDecl2_fromKeyword", "exportDecl3_exportKeyword", "exportDecl3_typeKeyword", "exportDecl3_fromKeyword", "exportDecl4_exportKeyword", "exportDecl4_typeKeyword", "exportDecl4_fromKeyword", "exportDecl4_asKeyword", "exportDecl5_exportKeyword", "exportDecl5_typeKeyword", "exportDecl6_exportKeyword", "exportDecl6_typeKeyword", "exportDecl6_asKeyword", "exportAssignment1_exportKeyword", "exportAssignment2_exportKeyword"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForStatementKeywords, TestReferencesForStatementKeywords);

// referencesForStatic_test.go

// referencesForStatic_test.go
static void TestReferencesForStatic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: referencesOnStatic_1.ts
var n = 43;

class foo {
    /*1*/static /*2*/n = '';

    public bar() {
        foo./*3*/n = "'";
        if(foo./*4*/n) {
            var x = foo./*5*/n;
        }
    }
}

class foo2 {
    private x = foo./*6*/n;
    constructor() {
        foo./*7*/n = x;
    }

    function b(n) {
        n = foo./*8*/n;
    }
}
// @Filename: referencesOnStatic_2.ts
var q = foo./*9*/n;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForStatic, TestReferencesForStatic);

// referencesForStaticsAndMembersWithSameNames_test.go

// referencesForStaticsAndMembersWithSameNames_test.go
static void TestReferencesForStaticsAndMembersWithSameNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace FindRef4 {
	namespace MixedStaticsClassTest {
		export class Foo {
			/*1*/bar: Foo;
			/*2*/static /*3*/bar: Foo;

			/*4*/public /*5*/foo(): void {
			}
			/*6*/public static /*7*/foo(): void {
			}
		}
	}

	function test() {
		// instance function
		var x = new MixedStaticsClassTest.Foo();
		x./*8*/foo();
		x./*9*/bar;

		// static function
		MixedStaticsClassTest.Foo./*10*/foo();
		MixedStaticsClassTest.Foo./*11*/bar;
	}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForStaticsAndMembersWithSameNames, TestReferencesForStaticsAndMembersWithSameNames);

// referencesForStringLiteralPropertyNames2_test.go

// referencesForStringLiteralPropertyNames2_test.go
static void TestReferencesForStringLiteralPropertyNames2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    /*1*/"/*2*/blah"() { return 0; }
}

var x: Foo;
x./*3*/blah;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForStringLiteralPropertyNames2, TestReferencesForStringLiteralPropertyNames2);

// referencesForStringLiteralPropertyNames3_test.go

// referencesForStringLiteralPropertyNames3_test.go
static void TestReferencesForStringLiteralPropertyNames3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo2 {
    /*1*/get "/*2*/42"() { return 0; }
    /*3*/set /*4*/42(n) { }
}

var y: Foo2;
y[/*5*/42];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForStringLiteralPropertyNames3, TestReferencesForStringLiteralPropertyNames3);

// referencesForStringLiteralPropertyNames4_test.go

// referencesForStringLiteralPropertyNames4_test.go
static void TestReferencesForStringLiteralPropertyNames4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = { "/*1*/someProperty": 0 }
x[/*2*/"someProperty"] = 3;
x.someProperty = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForStringLiteralPropertyNames4, TestReferencesForStringLiteralPropertyNames4);

// referencesForStringLiteralPropertyNames5_test.go

// referencesForStringLiteralPropertyNames5_test.go
static void TestReferencesForStringLiteralPropertyNames5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = { "/*1*/someProperty": 0 }
x["/*2*/someProperty"] = 3;
x.someProperty = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForStringLiteralPropertyNames5, TestReferencesForStringLiteralPropertyNames5);

// referencesForStringLiteralPropertyNames6_test.go

// referencesForStringLiteralPropertyNames6_test.go
static void TestReferencesForStringLiteralPropertyNames6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const x = function () { return 111111; }
x./*1*/someProperty = 5;
x["/*2*/someProperty"] = 3;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForStringLiteralPropertyNames6, TestReferencesForStringLiteralPropertyNames6);

// referencesForStringLiteralPropertyNames7_test.go

// referencesForStringLiteralPropertyNames7_test.go
static void TestReferencesForStringLiteralPropertyNames7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.js
// @noEmit: true
// @allowJs: true
// @checkJs: true
var x = { "/*1*/someProperty": 0 }
x["/*2*/someProperty"] = 3;
x.someProperty = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForStringLiteralPropertyNames7, TestReferencesForStringLiteralPropertyNames7);

// referencesForStringLiteralPropertyNames_test.go

// referencesForStringLiteralPropertyNames_test.go
static void TestReferencesForStringLiteralPropertyNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    public "/*1*/ss": any;
}

var x: Foo;
x.ss;
x["ss"];
x = { "ss": 0 };
x = { ss: 0 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForStringLiteralPropertyNames, TestReferencesForStringLiteralPropertyNames);

// referencesForTypeKeywords_test.go

// referencesForTypeKeywords_test.go
static void TestReferencesForTypeKeywords(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {}
function f<T /*typeParam_extendsKeyword*/extends I>() {}
type A1<T, U> = T /*conditionalType_extendsKeyword*/extends U ? 1 : 0;
type A2<T> = T extends /*inferType_inferKeyword*/infer U ? 1 : 0;
type A3<T> = { [P /*mappedType_inOperator*/in keyof T]: 1 };
type A4<T> = /*keyofOperator_keyofKeyword*/keyof T;
type A5<T> = /*readonlyOperator_readonlyKeyword*/readonly T[];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"typeParam_extendsKeyword", "conditionalType_extendsKeyword", "inferType_inferKeyword", "mappedType_inOperator", "keyofOperator_keyofKeyword", "readonlyOperator_readonlyKeyword"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForTypeKeywords, TestReferencesForTypeKeywords);

// referencesForUnionProperties_test.go

// referencesForUnionProperties_test.go
static void TestReferencesForUnionProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface One {
    common: { /*one*/a: number; };
}

interface Base {
    /*base*/a: string;
    b: string;
}

interface HasAOrB extends Base {
    a: string;
    b: string;
}

interface Two {
    common: HasAOrB;
}

var x : One | Two;

x.common./*x*/a;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"one", "base", "x"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesForUnionProperties, TestReferencesForUnionProperties);

// referencesInComment_test.go

// referencesInComment_test.go
static void TestReferencesInComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// References to /*1*/foo or b/*2*/ar
/* in comments should not find fo/*3*/o or bar/*4*/ */
class foo { }
var bar = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesInComment, TestReferencesInComment);

// referencesInConfiguredProject_test.go

// referencesInConfiguredProject_test.go
static void TestReferencesInConfiguredProject(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/referencesForGlobals_1.ts
class /*0*/globalClass {
    public f() { }
}
// @Filename: /home/src/workspaces/project/referencesForGlobals_2.ts
var c = /*1*/globalClass();
// @Filename: /home/src/workspaces/project/tsconfig.json
{ "files": ["referencesForGlobals_1.ts", "referencesForGlobals_2.ts"], "compilerOptions": { "lib": ["es5"] } })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesInConfiguredProject, TestReferencesInConfiguredProject);

// referencesInEmptyFileWithMultipleProjects_test.go

// referencesInEmptyFileWithMultipleProjects_test.go
static void TestReferencesInEmptyFileWithMultipleProjects(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/a/tsconfig.json
{ "files": ["a.ts"], "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/a/a.ts
/// <reference path="../b/b.ts" />
/*1*/;
// @Filename: /home/src/workspaces/project/b/tsconfig.json
{ "files": ["b.ts"], "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/b/b.ts
/*2*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesInEmptyFileWithMultipleProjects, TestReferencesInEmptyFileWithMultipleProjects);

// referencesInEmptyFile_test.go

// referencesInEmptyFile_test.go
static void TestReferencesInEmptyFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesInEmptyFile, TestReferencesInEmptyFile);

// referencesInStringLiteralValueWithMultipleProjects_test.go

// referencesInStringLiteralValueWithMultipleProjects_test.go
static void TestReferencesInStringLiteralValueWithMultipleProjects(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/a/tsconfig.json
{ "files": ["a.ts"], "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/a/a.ts
/// <reference path="../b/b.ts" />
const str: string = "hello/*1*/";
// @Filename: /home/src/workspaces/project/b/tsconfig.json
{ "files": ["b.ts"], "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/b/b.ts
const str2: string = "hello/*2*/";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesInStringLiteralValueWithMultipleProjects, TestReferencesInStringLiteralValueWithMultipleProjects);

// referencesIsAvailableThroughGlobalNoCrash_test.go

// referencesIsAvailableThroughGlobalNoCrash_test.go
static void TestReferencesIsAvailableThroughGlobalNoCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /packages/playwright-core/bundles/utils/node_modules/@types/debug/index.d.ts
declare var debug: debug.Debug & { debug: debug.Debug; default: debug.Debug };
export = debug;
export as namespace debug;
declare namespace debug {
    interface Debug {
       coerce: (val: any) => any;
    }
}
// @Filename: /packages/playwright-core/bundles/utils/node_modules/@types/debug/package.json
{ "types": "index.d.ts" }
// @Filename: /packages/playwright-core/src/index.ts
export const debug: typeof import('../bundles/utils/node_modules//*1*/@types/debug') = require('./utilsBundleImpl').debug;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesIsAvailableThroughGlobalNoCrash, TestReferencesIsAvailableThroughGlobalNoCrash);

// referencesToNonPropertyNameStringLiteral_test.go

// referencesToNonPropertyNameStringLiteral_test.go
static void TestReferencesToNonPropertyNameStringLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
const str: string = "hello/*1*/";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesToNonPropertyNameStringLiteral, TestReferencesToNonPropertyNameStringLiteral);

// referencesToStringLiteralValue_test.go

// referencesToStringLiteralValue_test.go
static void TestReferencesToStringLiteralValue(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
const s: string = "some /*1*/ string";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesToStringLiteralValue, TestReferencesToStringLiteralValue);

// renameForDefaultExport01_test.go

// renameForDefaultExport01_test.go
static void TestRenameForDefaultExport01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|export default class [|{| "contextRangeIndex": 0 |}DefaultExportedClass|] {
}|]
/*
 *  Commenting [|{| "inComment": true |}DefaultExportedClass|]
 */

var x: [|DefaultExportedClass|];

var y = new [|DefaultExportedClass|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto ranges = f->GetRangesByText()->Get("DefaultExportedClass");
		std::vector<fourslash::MarkerOrRangeOrName> markerOrRanges{};
		{
			int _ = 0;
			for (auto&& r : ranges) {
				if (!((((r->Marker != nullptr) && (r->Marker->Data != nullptr)) && (json::objGet(*r->Marker->Data, "inComment") != nullptr && json::objGet(*r->Marker->Data, "inComment")->boolVal == true)))) {
					markerOrRanges = ((markerOrRanges).push_back(r), markerOrRanges);
				}
				_++;
			}
		}
		f->VerifyBaselineRename(t, nullptr, markerOrRanges);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForDefaultExport01, TestRenameForDefaultExport01);

// renameForDefaultExport02_test.go

// renameForDefaultExport02_test.go
static void TestRenameForDefaultExport02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|export default function /*1*/[|{| "contextRangeIndex": 0 |}DefaultExportedFunction|]() {
    return /*2*/[|DefaultExportedFunction|]
}|]
/**
 *  Commenting [|{| "inComment": true |}DefaultExportedFunction|]
 */

var x: typeof /*3*/[|DefaultExportedFunction|];

var y = /*4*/[|DefaultExportedFunction|]();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, asMonVec(tsu::ToAny(gostr::coreFilter(f->GetRangesByText()->Get("DefaultExportedFunction"), [&](std::shared_ptr<fourslash::RangeMarker> r) {
	return ((r->Marker == nullptr) || (json::objGet(*r->Marker->Data, "inComment") == nullptr));
	}))));
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForDefaultExport02, TestRenameForDefaultExport02);

// renameForDefaultExport03_test.go

// renameForDefaultExport03_test.go
static void TestRenameForDefaultExport03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|function /*1*/[|{| "contextRangeIndex": 0 |}f|]() {
    return 100;
}|]

[|export default /*2*/[|{| "contextRangeIndex": 2 |}f|];|]

var x: typeof /*3*/[|f|];

var y = /*4*/[|f|]();

/**
 *  Commenting [|{| "inComment": true |}f|]
 */
[|namespace /*5*/[|{| "contextRangeIndex": 7 |}f|] {
    var local = 100;
}|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, asMonVec(tsu::ToAny(gostr::coreFilter(f->GetRangesByText()->Get("f"), [&](std::shared_ptr<fourslash::RangeMarker> r) {
	return ((r->Marker == nullptr) || (json::objGet(*r->Marker->Data, "inComment") == nullptr));
	}))));
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForDefaultExport03, TestRenameForDefaultExport03);

// renameForDefaultExport04_test.go

// renameForDefaultExport04_test.go
static void TestRenameForDefaultExport04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export default class /**/[|DefaultExportedClass|] {
}
/*
 *  Commenting DefaultExportedClass
 */

var x: DefaultExportedClass;

var y = new DefaultExportedClass;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForDefaultExport04, TestRenameForDefaultExport04);

// renameForDefaultExport05_test.go

// renameForDefaultExport05_test.go
static void TestRenameForDefaultExport05(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export default class DefaultExportedClass {
}
/*
 *  Commenting DefaultExportedClass
 */

var x: /**/[|DefaultExportedClass|];

var y = new DefaultExportedClass;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForDefaultExport05, TestRenameForDefaultExport05);

// renameForDefaultExport06_test.go

// renameForDefaultExport06_test.go
static void TestRenameForDefaultExport06(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export default class DefaultExportedClass {
}
/*
 *  Commenting DefaultExportedClass
 */

var x: DefaultExportedClass;

var y = new /**/[|DefaultExportedClass|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForDefaultExport06, TestRenameForDefaultExport06);

// renameForDefaultExport07_test.go

// renameForDefaultExport07_test.go
static void TestRenameForDefaultExport07(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export default function /**/[|DefaultExportedFunction|]() {
    return DefaultExportedFunction
}
/**
 *  Commenting DefaultExportedFunction
 */

var x: typeof DefaultExportedFunction;

var y = DefaultExportedFunction();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForDefaultExport07, TestRenameForDefaultExport07);

// renameForDefaultExport08_test.go

// renameForDefaultExport08_test.go
static void TestRenameForDefaultExport08(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export default function DefaultExportedFunction() {
    return /**/[|DefaultExportedFunction|]
}
/**
 *  Commenting DefaultExportedFunction
 */

var x: typeof DefaultExportedFunction;

var y = DefaultExportedFunction();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForDefaultExport08, TestRenameForDefaultExport08);

// renameForDefaultExport09_test.go

// renameForDefaultExport09_test.go
static void TestRenameForDefaultExport09(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
function /**/[|f|]() {
    return 100;
}

export default f;

var x: typeof f;

var y = f();

/**
 *  Commenting f
 */
namespace f {
    var local = 100;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForDefaultExport09, TestRenameForDefaultExport09);

// renameInheritedProperties1_test.go

// renameInheritedProperties1_test.go
static void TestRenameInheritedProperties1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class class1 extends class1 {
   [|[|{| "contextRangeIndex": 0 |}propName|]: string;|]
}

var v: class1;
v.[|propName|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"propName"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameInheritedProperties1, TestRenameInheritedProperties1);

// renameInheritedProperties2_test.go

// renameInheritedProperties2_test.go
static void TestRenameInheritedProperties2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class class1 extends class1 {
   [|[|{| "contextRangeIndex": 0 |}doStuff|]() { }|]
}

var v: class1;
v.[|doStuff|]();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"doStuff"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameInheritedProperties2, TestRenameInheritedProperties2);

// renameInheritedProperties3_test.go

// renameInheritedProperties3_test.go
static void TestRenameInheritedProperties3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface interface1 extends interface1 {
   [|[|{| "contextRangeIndex": 0 |}propName|]: string;|]
}

var v: interface1;
v.[|propName|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"propName"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameInheritedProperties3, TestRenameInheritedProperties3);

// renameInheritedProperties4_test.go

// renameInheritedProperties4_test.go
static void TestRenameInheritedProperties4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface interface1 extends interface1 {
   [|[|{| "contextRangeIndex": 0 |}doStuff|](): string;|]
}

var v: interface1;
v.[|doStuff|]();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"doStuff"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameInheritedProperties4, TestRenameInheritedProperties4);

// renameInheritedProperties5_test.go

// renameInheritedProperties5_test.go
static void TestRenameInheritedProperties5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface C extends D {
    propC: number;
}
interface D extends C {
    [|[|{| "contextRangeIndex": 0 |}propD|]: string;|]
}
var d: D;
d.[|propD|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"propD"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameInheritedProperties5, TestRenameInheritedProperties5);

// renameInheritedProperties6_test.go

// renameInheritedProperties6_test.go
static void TestRenameInheritedProperties6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface C extends D {
    propD: number;
}
interface D extends C {
    [|[|{| "contextRangeIndex": 0 |}propC|]: number;|]
}
var d: D;
d.[|propC|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"propC"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameInheritedProperties6, TestRenameInheritedProperties6);

// renameInheritedProperties7_test.go

// renameInheritedProperties7_test.go
static void TestRenameInheritedProperties7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C extends D {
    [|[|{| "contextRangeIndex": 0 |}prop1|]: string;|]
}

class D extends C {
    prop1: string;
}

var c: C;
c.[|prop1|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"prop1"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameInheritedProperties7, TestRenameInheritedProperties7);

// renameInheritedProperties8_test.go

// renameInheritedProperties8_test.go
static void TestRenameInheritedProperties8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C implements D {
    [|[|{| "contextRangeIndex": 0 |}prop1|]: string;|]
}

interface D extends C {
    [|[|{| "contextRangeIndex": 2 |}prop1|]: string;|]
}

var c: C;
c.[|prop1|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"prop1"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameInheritedProperties8, TestRenameInheritedProperties8);

} // namespace
