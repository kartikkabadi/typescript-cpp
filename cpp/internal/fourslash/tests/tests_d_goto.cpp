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


static void TestGoToDefinitionPreferSourceDefinition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/a.js
export const /*sourceTarget*/a = "a";
// @Filename: /home/src/workspaces/project/a.d.ts
export declare const /*dtsTarget*/a: string;
// @Filename: /home/src/workspaces/project/index.ts
import { a } from "./a";
a/*start*/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"start"});
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
		f->Configure(t, lsutil::UserPreferences{.PreferGoToSourceDefinition = true});
		f->VerifyBaselineGoToDefinition(t, false, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionPreferSourceDefinition, TestGoToDefinitionPreferSourceDefinition);

static void TestGoToDefinitionPreferSourceDefinitionFallback(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export interface Config {
    enabled: boolean;
}
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
exports.makeConfig = () => ({ enabled: true });
// @Filename: /home/src/workspaces/project/index.ts
import type { Config } from "pkg";
let value: /*start*/Config;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.PreferGoToSourceDefinition = true});
		f->VerifyBaselineGoToDefinition(t, false, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionPreferSourceDefinitionFallback, TestGoToDefinitionPreferSourceDefinitionFallback);


static void TestGoToDefinitionObjectBindingPattern(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
interface SomeType {
    targetProperty: number;
}

function foo(callback: (p: SomeType) => void) {}

foo(({ /*1*/targetProperty }) => {
    /*4*/targetProperty
});

let { /*2*/targetProperty }: SomeType = { /*3*/targetProperty: 42 };

let { /*5*/targetProperty: /*6*/alias_1 }: SomeType = { targetProperty: 42 };

let { x: { /*7*/targetProperty: /*8*/{} } }: { x: SomeType } = { x: { targetProperty: 42 } };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, f->MarkerNames());
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionObjectBindingPattern, TestGoToDefinitionObjectBindingPattern);

static void TestGoToDefinitionObjectBindingPatternRest(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
interface SomeType {
    targetProperty: number;
}

let { .../*1*/rest }: SomeType = { targetProperty: 42 };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, f->MarkerNames());
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionObjectBindingPatternRest, TestGoToDefinitionObjectBindingPatternRest);


static void TestGotoDefinitionThrowsTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class [|/*def*/E|] extends Error {}

/**
 * @throws {/*use*/[|E|]}
 */
function f() {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionThrowsTag, TestGotoDefinitionThrowsTag);


static void TestGotoDefinitionSatisfiesTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJS: true
// @checkJs: true
// @filename: /a.js
/**
 * @typedef {Object} [|/*def*/T|]
 * @property {number} a
 */

/** @satisfies {/*use*/[|T|]} comment */
const foo = { a: 1 };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionSatisfiesTag, TestGotoDefinitionSatisfiesTag);


static void TestGotoDefinitionPropertyAccessExpressionHeritageClause(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class B {}
function foo() {
    return {/*refB*/B: B};
}
class C extends (foo()).[|/*B*/B|] {}
class C1 extends foo().[|/*B1*/B|] {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"B", "B1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionPropertyAccessExpressionHeritageClause, TestGotoDefinitionPropertyAccessExpressionHeritageClause);


static void TestGotoDefinitionLinkTag6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    /** {@link E./*1*/[|A|]} */
    [|/*2*/A|]
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionLinkTag6, TestGotoDefinitionLinkTag6);


static void TestGotoDefinitionLinkTag5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    /** {@link /*1*/[|B|]} */
    A,
    [|/*2*/B|]
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionLinkTag5, TestGotoDefinitionLinkTag5);


static void TestGotoDefinitionLinkTag4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: a.ts
interface [|/*2*/Foo|] {
    foo: E.Foo;
}
// @Filename: b.ts
enum E {
    /** {@link /*1*/[|Foo|]} */
    Foo
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionLinkTag4, TestGotoDefinitionLinkTag4);


static void TestGotoDefinitionLinkTag3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
enum E {
    /** {@link /*1*/[|Foo|]} */
    Foo
}
interface [|/*2*/Foo|] {
    foo: E.Foo;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionLinkTag3, TestGotoDefinitionLinkTag3);


static void TestGotoDefinitionLinkTag2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    /** {@link /*1*/[|A|]} */
    [|/*2*/A|]
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionLinkTag2, TestGotoDefinitionLinkTag2);


static void TestGotoDefinitionLinkTag1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
interface [|/*def1*/Foo|] {
    foo: string
}
namespace NS {
    export interface [|/*def2*/Bar|] {
        baz: Foo
    }
}
/** {@link /*use1*/[|Foo|]} foooo*/
const a = ""
/** {@link NS./*use2*/[|Bar|]} ns.bar*/
const b = ""
/** {@link /*use3*/[|Foo|] f1}*/
const c = ""
/** {@link NS./*use4*/[|Bar|] ns.bar}*/
const [|/*def3*/d|] = ""
/** {@link /*use5*/[|d|] }dd*/
const e = ""
/** @param x {@link /*use6*/[|Foo|]} */
function foo(x) { }
// @Filename: bar.ts
/** {@link /*use7*/[|Foo|] }dd*/
const f = "")TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"use1", "use2", "use3", "use4", "use5", "use6", "use7"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionLinkTag1, TestGotoDefinitionLinkTag1);


static void TestGotoDefinitionInObjectBindingPattern2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var p0 = ({a/*1*/a}) => {console.log(aa)};
function f2({ [|a/*a1*/1|], [|b/*b1*/1|] }: { /*a1_dest*/a1: number, /*b1_dest*/b1: number } = { a1: 0, b1: 0 }) {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1", "a1", "b1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionInObjectBindingPattern2, TestGotoDefinitionInObjectBindingPattern2);


static void TestGotoDefinitionInObjectBindingPattern1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function bar<T>(onfulfilled: (value: T) => void) {
  return undefined;
}
interface Test {
  /*destination*/prop2: number
}
bar<Test>(({[|pr/*goto*/op2|]})=>{});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"goto"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionInObjectBindingPattern1, TestGotoDefinitionInObjectBindingPattern1);


static void TestGotoDefinitionConstructorFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @noEmit: true
// @filename: gotoDefinitionConstructorFunction.js
function /*end*/StringStreamm() {
}
StringStreamm.prototype = {
};

function runMode () {
new [|/*start*/StringStreamm|]()
};)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGotoDefinitionConstructorFunction, TestGotoDefinitionConstructorFunction);


static void TestGoToSource9_mapFromAtTypes2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/lodash/package.json
{ "name": "lodash", "version": "4.17.15", "main": "./lodash.js" }
// @Filename: /home/src/workspaces/project/node_modules/lodash/lodash.js
;(function() {
    /**
     * Adds two numbers.
     *
     * @static
     * @memberOf _
     * @since 3.4.0
     * @category Math
     * @param {number} augend The first number in an addition.
     * @param {number} addend The second number in an addition.
     * @returns {number} Returns the total.
     * @example
     *
     * _.add(6, 4);
     * // => 10
     */
    var [|/*variable*/add|] = createMathOperation(function(augend, addend) {
     return augend + addend;
    }, 0);

    function lodash(value) {}
    lodash.[|/*property*/add|] = add;

    /** Detect free variable `global` from Node.js. */
    var freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
    /** Detect free variable `self`. */
    var freeSelf = typeof self == 'object' && self && self.Object === Object && self;
    /** Used as a reference to the global object. */
    var root = freeGlobal || freeSelf || Function('return this')();
    /** Detect free variable `exports`. */
    var freeExports = typeof exports == 'object' && exports && !exports.nodeType && exports;////     
    /** Detect free variable `module`. */
    var freeModule = freeExports && typeof module == 'object' && module && !module.nodeType && module;
    if (freeModule) {
      // Export for Node.js.
      (freeModule.exports = _)._ = _;
      // Export for CommonJS support.
      freeExports._ = _;
    }
    else {
      // Export to the global object.
      root._ = _;
    }
}.call(this));
// @Filename: /home/src/workspaces/project/node_modules/@types/lodash/package.json
{ "name": "@types/lodash", "version": "4.14.97", "types": "index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/@types/lodash/index.d.ts
/// <reference path="./common/math.d.ts" />
export = _;
export as namespace _;
declare const _: _.LoDashStatic;
declare namespace _ {
    interface LoDashStatic {}
}
// @Filename: /home/src/workspaces/project/node_modules/@types/lodash/common/math.d.ts
import _ = require("../index");
declare module "../index" {
    interface LoDashStatic {
        add(augend: number, addend: number): number;
    }
}
// @Filename: /home/src/workspaces/project/index.ts
import [|/*defaultImport*/_|], { [|/*unresolvableNamedImport*/foo|] } from [|/*moduleSpecifier*/'lodash'|];
_.[|/*propertyAccess*/add|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"defaultImport", "unresolvableNamedImport", "moduleSpecifier"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource9_mapFromAtTypes2, TestGoToSource9_mapFromAtTypes2);


static void TestGoToSource8_mapFromAtTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/lodash/package.json
{ "name": "lodash", "version": "4.17.15", "main": "./lodash.js" }
// @Filename: /home/src/workspaces/project/node_modules/lodash/lodash.js
;(function() {
    /**
     * Adds two numbers.
     *
     * @static
     * @memberOf _
     * @since 3.4.0
     * @category Math
     * @param {number} augend The first number in an addition.
     * @param {number} addend The second number in an addition.
     * @returns {number} Returns the total.
     * @example
     *
     * _.add(6, 4);
     * // => 10
     */
    var [|/*variable*/add|] = createMathOperation(function(augend, addend) {
     return augend + addend;
    }, 0);

    function lodash(value) {}
    lodash.[|/*property*/add|] = add;

    /** Detect free variable `global` from Node.js. */
    var freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
    /** Detect free variable `self`. */
    var freeSelf = typeof self == 'object' && self && self.Object === Object && self;
    /** Used as a reference to the global object. */
    var root = freeGlobal || freeSelf || Function('return this')();
    /** Detect free variable `exports`. */
    var freeExports = typeof exports == 'object' && exports && !exports.nodeType && exports;////     
    /** Detect free variable `module`. */
    var freeModule = freeExports && typeof module == 'object' && module && !module.nodeType && module;
    if (freeModule) {
      // Export for Node.js.
      (freeModule.exports = _)._ = _;
      // Export for CommonJS support.
      freeExports._ = _;
    }
    else {
      // Export to the global object.
      root._ = _;
    }
}.call(this));
// @Filename: /home/src/workspaces/project/node_modules/@types/lodash/package.json
{ "name": "@types/lodash", "version": "4.14.97", "types": "index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/@types/lodash/index.d.ts
/// <reference path="./common/math.d.ts" />
export = _;
export as namespace _;
declare const _: _.LoDashStatic;
declare namespace _ {
    interface LoDashStatic {}
}
// @Filename: /home/src/workspaces/project/node_modules/@types/lodash/common/math.d.ts
import _ = require("../index");
declare module "../index" {
    interface LoDashStatic {
        add(augend: number, addend: number): number;
    }
}
// @Filename: /home/src/workspaces/project/index.ts
import { [|/*start*/add|] } from 'lodash';)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource8_mapFromAtTypes, TestGoToSource8_mapFromAtTypes);


static void TestGoToSource7_conditionallyMinified(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/react/package.json
{ "name": "react", "version": "16.8.6", "main": "index.js" }
// @Filename: /home/src/workspaces/project/node_modules/react/index.js
'use strict';

if (process.env.NODE_ENV === 'production') {
  module.exports = require('./cjs/react.production.min.js');
} else {
  module.exports = require('./cjs/react.development.js');
}
// @Filename: /home/src/workspaces/project/node_modules/react/cjs/react.production.min.js
'use strict';exports./*production*/useState=function(a){};exports.version='16.8.6';
// @Filename: /home/src/workspaces/project/node_modules/react/cjs/react.development.js
'use strict';
if (process.env.NODE_ENV !== 'production') {
  (function() {
    function useState(initialState) {}
    exports./*development*/useState = useState;
    exports.version = '16.8.6';
  }());
}
// @Filename: /home/src/workspaces/project/index.ts
import { [|/*start*/useState|] } from 'react';)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource7_conditionallyMinified, TestGoToSource7_conditionallyMinified);


static void TestGoToSource6_sameAsGoToDef2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: /home/src/workspaces/project/node_modules/foo/package.json
{ "name": "foo", "version": "1.2.3", "typesVersions": { "*": { "*": ["./types/*"] } } }
// @Filename: /home/src/workspaces/project/node_modules/foo/src/a.ts
export const /*end*/a = 'a';
// @Filename: /home/src/workspaces/project/node_modules/foo/types/a.d.ts
export declare const a: string;
//# sourceMappingURL=a.d.ts.map
// @Filename: /home/src/workspaces/project/node_modules/foo/types/a.d.ts.map
{"version":3,"file":"a.d.ts","sourceRoot":"","sources":["../src/a.ts"],"names":[],"mappings":"AAAA,eAAO,MAAM,EAAE,OAAO,CAAC;;AACvB,wBAAsB"}
// @Filename: /home/src/workspaces/project/node_modules/foo/dist/a.js
export const a = 'a';
// @Filename: /home/src/workspaces/project/b.ts
import { a } from 'foo/a';
[|a/*start*/|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource6_sameAsGoToDef2, TestGoToSource6_sameAsGoToDef2);


static void TestGoToSource5_sameAsGoToDef1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: /home/src/workspaces/project/a.ts
export const /*end*/a = 'a';
// @Filename: /home/src/workspaces/project/a.d.ts
export declare const a: string;
// @Filename: /home/src/workspaces/project/a.js
export const a = 'a';
// @Filename: /home/src/workspaces/project/b.ts
import { a } from './a';
[|a/*start*/|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource5_sameAsGoToDef1, TestGoToSource5_sameAsGoToDef1);


static void TestGoToSource3_nodeModulesAtTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/foo/package.json
{ "name": "foo", "version": "1.0.0", "main": "./lib/main.js" }
// @Filename: /home/src/workspaces/project/node_modules/foo/lib/main.js
export const /*end*/a = "a";
// @Filename: /home/src/workspaces/project/node_modules/@types/foo/package.json
{ "name": "@types/foo", "version": "1.0.0", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/@types/foo/index.d.ts
export declare const a: string;
// @Filename: /home/src/workspaces/project/index.ts
import { a } from "foo";
[|a/*start*/|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource3_nodeModulesAtTypes, TestGoToSource3_nodeModulesAtTypes);


static void TestGoToSource2_nodeModulesWithTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/foo/package.json
{ "name": "foo", "version": "1.0.0", "main": "./lib/main.js", "types": "./types/main.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/foo/lib/main.js
export const /*end*/a = "a";
// @Filename: /home/src/workspaces/project/node_modules/foo/types/main.d.ts
export declare const a: string;
// @Filename: /home/src/workspaces/project/index.ts
import { a } from "foo";
[|a/*start*/|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource2_nodeModulesWithTypes, TestGoToSource2_nodeModulesWithTypes);


static void TestGoToSource1_localJsBesideDts(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: /home/src/workspaces/project/a.js
export const /*end*/a = "a";
// @Filename: /home/src/workspaces/project/a.d.ts
export declare const a: string;
// @Filename: /home/src/workspaces/project/index.ts
import { a } from [|"./a"/*moduleSpecifier*/|];
[|a/*identifier*/|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"identifier", "moduleSpecifier"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource1_localJsBesideDts, TestGoToSource1_localJsBesideDts);


static void TestGoToSource18_reusedFromDifferentFolder(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/package.json
{
    "name": "@types/yargs",
    "version": "1.0.0",
    "types": "./index.d.ts"
}
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/callback.d.ts
export declare class Yargs { positional(): Yargs; }
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/index.d.ts
import { Yargs } from "./callback";
export declare function command(command: string, cb: (yargs: Yargs) => void): void;
// @Filename: /home/src/workspaces/project/node_modules/yargs/package.json
{
    "name": "yargs",
    "version": "1.0.0",
    "main": "index.js"
}
// @Filename: /home/src/workspaces/project/node_modules/yargs/callback.js
export class Yargs { positional() { } }
// @Filename: /home/src/workspaces/project/node_modules/yargs/index.js
// Specifically didnt have ./callback import to ensure that resolving module sepcifier adds the file to project at later stage
export function command(cmd, cb) { cb(Yargs) }
// @Filename: /home/src/workspaces/project/folder/random.ts
import { Yargs } from "yargs/callback";
// @Filename: /home/src/workspaces/project/some/index.ts
import { random } from "../folder/random";
import { command } from "yargs";
command("foo", yargs => {
    yargs.[|/*start*/positional|]();
});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource18_reusedFromDifferentFolder, TestGoToSource18_reusedFromDifferentFolder);


static void TestGoToSource17_AddsFileToProject(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/package.json
{
    "name": "@types/yargs",
    "version": "1.0.0",
    "types": "./index.d.ts"
}
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/callback.d.ts
export declare class Yargs { positional(): Yargs; }
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/index.d.ts
import { Yargs } from "./callback";
export declare function command(command: string, cb: (yargs: Yargs) => void): void;
// @Filename: /home/src/workspaces/project/node_modules/yargs/package.json
{
    "name": "yargs",
    "version": "1.0.0",
    "main": "index.js"
}
// @Filename: /home/src/workspaces/project/node_modules/yargs/callback.js
export class Yargs { positional() { } }
// @Filename: /home/src/workspaces/project/node_modules/yargs/index.js
// Specifically didnt have ./callback import to ensure that resolving module sepcifier adds the file to project at later stage
export function command(cmd, cb) { cb(Yargs) }
// @Filename: /home/src/workspaces/project/index.ts
import { command } from "yargs";
command("foo", yargs => {
    yargs.[|/*start*/positional|]();
});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource17_AddsFileToProject, TestGoToSource17_AddsFileToProject);


static void TestGoToSource16_callbackParamDifferentFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/package.json
{
    "name": "@types/yargs",
    "version": "1.0.0",
    "types": "./index.d.ts"
}
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/callback.d.ts
export declare class Yargs { positional(): Yargs; }
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/index.d.ts
import { Yargs } from "./callback";
export declare function command(command: string, cb: (yargs: Yargs) => void): void;
// @Filename: /home/src/workspaces/project/node_modules/yargs/package.json
{
    "name": "yargs",
    "version": "1.0.0",
    "main": "index.js"
}
// @Filename: /home/src/workspaces/project/node_modules/yargs/callback.js
export class Yargs { positional() { } }
// @Filename: /home/src/workspaces/project/node_modules/yargs/index.js
import { Yargs } from "./callback";
export function command(cmd, cb) { cb(Yargs) }
// @Filename: /home/src/workspaces/project/index.ts
import { command } from "yargs";
command("foo", yargs => {
    yargs.[|/*start*/positional|]();
});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource16_callbackParamDifferentFile, TestGoToSource16_callbackParamDifferentFile);


static void TestGoToSource15_bundler(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "module": "esnext", "moduleResolution": "bundler", "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/node_modules/react/package.json
{ "name": "react", "version": "16.8.6", "main": "index.js" }
// @Filename: /home/src/workspaces/project/node_modules/react/index.js
'use strict';

if (process.env.NODE_ENV === 'production') {
  module.exports = require('./cjs/react.production.min.js');
} else {
  module.exports = require('./cjs/react.development.js');
}
// @Filename: /home/src/workspaces/project/node_modules/react/cjs/react.production.min.js
'use strict';exports./*production*/useState=function(a){};exports.version='16.8.6';
// @Filename: /home/src/workspaces/project/node_modules/react/cjs/react.development.js
'use strict';
if (process.env.NODE_ENV !== 'production') {
  (function() {
    function useState(initialState) {}
    exports./*development*/useState = useState;
    exports.version = '16.8.6';
  }());
}
// @Filename: /home/src/workspaces/project/index.ts
import { [|/*start*/useState|] } from 'react';)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource15_bundler, TestGoToSource15_bundler);


static void TestGoToSource14_unresolvedRequireDestructuring(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowJs: true
// @Filename: /home/src/workspaces/project/index.js
const { blah/**/ } = require("unresolved");)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource14_unresolvedRequireDestructuring, TestGoToSource14_unresolvedRequireDestructuring);


static void TestGoToSource13_nodenext(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/node_modules/left-pad/package.json
{
  "name": "left-pad",
  "version": "1.3.0",
  "description": "String left pad",
  "main": "index.js",
  "types": "index.d.ts"
}
// @Filename: /home/src/workspaces/project/node_modules/left-pad/index.d.ts
declare function leftPad(str: string|number, len: number, ch?: string|number): string;
declare namespace leftPad { }
export = leftPad;
// @Filename: /home/src/workspaces/project/node_modules/left-pad/index.js
module.exports = leftPad;
function /*end*/leftPad(str, len, ch) {}
// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
      "module": "node16",
      "lib": ["es5"],
      "strict": true,
      "outDir": "./out",

  }
}
// @Filename: /home/src/workspaces/project/index.mts
import leftPad = require("left-pad");
/*start*/leftPad("", 4);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource13_nodenext, TestGoToSource13_nodenext);


static void TestGoToSource12_callbackParam(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/package.json
{
    "name": "@types/yargs",
    "version": "1.0.0",
    "types": "./index.d.ts"
}
// @Filename: /home/src/workspaces/project/node_modules/@types/yargs/index.d.ts
export interface Yargs { positional(): Yargs; }
export declare function command(command: string, cb: (yargs: Yargs) => void): void;
// @Filename: /home/src/workspaces/project/node_modules/yargs/package.json
{
    "name": "yargs",
    "version": "1.0.0",
    "main": "index.js"
}
// @Filename: /home/src/workspaces/project/node_modules/yargs/index.js
export function command(cmd, cb) { cb({ /*end*/positional: "This is obviously not even close to realistic" }); }
// @Filename: /home/src/workspaces/project/index.ts
import { command } from "yargs";
command("foo", yargs => {
    yargs.[|/*start*/positional|]();
});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource12_callbackParam, TestGoToSource12_callbackParam);


static void TestGoToSource11_propertyOfAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/a.js
export const a = { /*end*/a: 'a' };
// @Filename: /home/src/workspaces/project/a.d.ts
export declare const a: { a: string };
// @Filename: /home/src/workspaces/project/b.ts
import { a } from './a';
a.[|a/*start*/|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource11_propertyOfAlias, TestGoToSource11_propertyOfAlias);


static void TestGoToSource10_mapFromAtTypes3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/lodash/package.json
{ "name": "lodash", "version": "4.17.15", "main": "./lodash.js" }
// @Filename: /home/src/workspaces/project/node_modules/lodash/lodash.js
;(function() {
    /**
     * Adds two numbers.
     *
     * @static
     * @memberOf _
     * @since 3.4.0
     * @category Math
     * @param {number} augend The first number in an addition.
     * @param {number} addend The second number in an addition.
     * @returns {number} Returns the total.
     * @example
     *
     * _.add(6, 4);
     * // => 10
     */
    var [|/*variable*/add|] = createMathOperation(function(augend, addend) {
     return augend + addend;
    }, 0);

    function lodash(value) {}
    lodash.[|/*property*/add|] = add;

    /** Detect free variable `global` from Node.js. */
    var freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
    /** Detect free variable `self`. */
    var freeSelf = typeof self == 'object' && self && self.Object === Object && self;
    /** Used as a reference to the global object. */
    var root = freeGlobal || freeSelf || Function('return this')();
    /** Detect free variable `exports`. */
    var freeExports = typeof exports == 'object' && exports && !exports.nodeType && exports;////     
    /** Detect free variable `module`. */
    var freeModule = freeExports && typeof module == 'object' && module && !module.nodeType && module;
    if (freeModule) {
      // Export for Node.js.
      (freeModule.exports = _)._ = _;
      // Export for CommonJS support.
      freeExports._ = _;
    }
    else {
      // Export to the global object.
      root._ = _;
    }
}.call(this));
// @Filename: /home/src/workspaces/project/node_modules/@types/lodash/package.json
{ "name": "@types/lodash", "version": "4.14.97", "types": "index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/@types/lodash/index.d.ts
export = _;
export as namespace _;
declare const _: _.LoDashStatic;
declare namespace _ {
    interface LoDashStatic {}
}
// @Filename: /home/src/workspaces/project/index.ts
import { [|/*start*/add|] } from 'lodash';)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToSourceDefinition(t, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSource10_mapFromAtTypes3, TestGoToSource10_mapFromAtTypes3);


static void TestGoToModuleAliasDefinition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
export class /*2*/Foo {}
// @Filename: b.ts
 import /*3*/n = require('a');
 var x = new [|/*1*/n|].Foo();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToModuleAliasDefinition, TestGoToModuleAliasDefinition);


static void TestGoToDefinition_untypedModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/foo/index.js
not read
// @Filename: /a.ts
import { /*def*/f } from "foo";
[|/*use*/f|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinition_untypedModule, TestGoToDefinition_untypedModule);


static void TestGoToDefinition_super(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    /*ctr*/constructor() {}
    x() {}
}
class /*B*/B extends A {}
class C extends B {
    constructor() {
        [|/*super*/super|]();
    }
    method() {
        [|/*superExpression*/super|].x();
    }
}
class D {
    constructor() {
        /*superBroken*/super();
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"super", "superExpression", "superBroken"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinition_super, TestGoToDefinition_super);


static void TestGoToDefinition_mappedType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I { /*def*/m(): void; };
declare const i: { [K in "m"]: I[K] };
i.[|/*ref*/m|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinition_mappedType, TestGoToDefinition_mappedType);


static void TestGoToDefinition_filteringMappedType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const obj = { /*def*/a: 1, b: 2 };
const filtered: { [P in keyof typeof obj as P extends 'b' ? never : P]: 0; } = { a: 0 };
filtered.[|/*ref*/a|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinition_filteringMappedType, TestGoToDefinition_filteringMappedType);


static void TestGoToDefinition_filteringGenericMappedType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const obj = {
  get /*def*/id() {
    return 1;
  },
  name: "test",
};

type Omit2<T, DroppedKeys extends PropertyKey> = {
  [K in keyof T as Exclude<K, DroppedKeys>]: T[K];
};

declare function omit2<O, Mask extends { [K in keyof O]?: true }>(
  obj: O,
  mask: Mask
): Omit2<O, keyof Mask>;

const obj2 = omit2(obj, {
  name: true,
});

obj2.[|/*ref*/id|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinition_filteringGenericMappedType, TestGoToDefinition_filteringGenericMappedType);


static void TestGoToDefinitionYield4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function* gen() {
    class C { [/*start*/yield 10]() {} }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionYield4, TestGoToDefinitionYield4);


static void TestGoToDefinitionYield3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    notAGenerator() {
      [|/*start1*/yield|] 0;
    }

    foo*/*end2*/() {
      [|/*start2*/yield|] 0;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start1", "start2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionYield3, TestGoToDefinitionYield3);


static void TestGoToDefinitionYield2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function* outerGen() {
    function* /*end*/gen() {
        [|/*start*/yield|] 0;
    }
    return gen
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionYield2, TestGoToDefinitionYield2);


static void TestGoToDefinitionYield1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function* /*end1*/gen() {
    [|/*start1*/yield|] 0;
}

const /*end2*/genFunction = function*() {
    [|/*start2*/yield|] 0;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start1", "start2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionYield1, TestGoToDefinitionYield1);


static void TestGoToDefinitionVariableAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @filename: foo.js
const Bar;
const Foo = /*def*/Bar = function () {}
Foo.prototype.bar = function() {}
new [|Foo/*ref*/|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "foo.js");
		f->VerifyBaselineGoToDefinition(t, true, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionVariableAssignment, TestGoToDefinitionVariableAssignment);


static void TestGoToDefinitionVariableAssignment3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: foo.ts
const Foo = module./*def*/exports = function () {}
Foo.prototype.bar = function() {}
new [|Foo/*ref*/|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "foo.ts");
		f->VerifyBaselineGoToDefinition(t, true, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionVariableAssignment3, TestGoToDefinitionVariableAssignment3);


static void TestGoToDefinitionVariableAssignment2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: foo.ts
const Bar;
const Foo = /*def*/Bar = function () {}
Foo.prototype.bar = function() {}
new [|Foo/*ref*/|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "foo.ts");
		f->VerifyBaselineGoToDefinition(t, true, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionVariableAssignment2, TestGoToDefinitionVariableAssignment2);


static void TestGoToDefinitionVariableAssignment1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @filename: foo.js
const Foo = module./*def*/exports = function () {}
Foo.prototype.bar = function() {}
new [|Foo/*ref*/|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "foo.js");
		f->VerifyBaselineGoToDefinition(t, true, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionVariableAssignment1, TestGoToDefinitionVariableAssignment1);


static void TestGoToDefinitionUnionTypeProperty_discriminated(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type U = A | B;

interface A {
  /*aKind*/kind: "a";
  /*aProp*/prop: number;
};

interface B {
  /*bKind*/kind: "b";
  /*bProp*/prop: string;
}

const u: U = {
  [|/*kind*/kind|]: "a",
  [|/*prop*/prop|]: 0,
};
const u2: U = {
  [|/*kindBogus*/kind|]: "bogus",
  [|/*propBogus*/prop|]: 0,
};)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"kind", "prop", "kindBogus", "propBogus"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionUnionTypeProperty_discriminated, TestGoToDefinitionUnionTypeProperty_discriminated);


static void TestGoToDefinitionUnionTypeProperty4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface SnapCrackle {
    /*def1*/pop(): string;
}

interface Magnitude {
    /*def2*/pop(): number;
}

interface Art {
    /*def3*/pop(): boolean;
}

var art: Art;
var magnitude: Magnitude;
var snapcrackle: SnapCrackle;

var x = (snapcrackle || magnitude || art).[|/*usage*/pop|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionUnionTypeProperty4, TestGoToDefinitionUnionTypeProperty4);


static void TestGoToDefinitionUnionTypeProperty3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Array<T> {
    /*definition*/specialPop(): T
}

var strings: string[];
var numbers: number[];

var x = (strings || numbers).[|/*usage*/specialPop|]())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionUnionTypeProperty3, TestGoToDefinitionUnionTypeProperty3);


static void TestGoToDefinitionUnionTypeProperty2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface HasAOrB {
    /*propertyDefinition1*/a: string;
    b: string;
}

interface One {
    common: { /*propertyDefinition2*/a : number; };
}

interface Two {
    common: HasAOrB;
}

var x : One | Two;

x.common.[|/*propertyReference*/a|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"propertyReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionUnionTypeProperty2, TestGoToDefinitionUnionTypeProperty2);


static void TestGoToDefinitionUnionTypeProperty1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface One {
    /*propertyDefinition1*/commonProperty: number;
    commonFunction(): number;
}

interface Two {
    /*propertyDefinition2*/commonProperty: string
    commonFunction(): number;
}

var x : One | Two;

x.[|/*propertyReference*/commonProperty|];
x./*3*/commonFunction;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"propertyReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionUnionTypeProperty1, TestGoToDefinitionUnionTypeProperty1);


static void TestGoToDefinitionUndefinedSymbols(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(some/*undefinedValue*/Variable;
var a: some/*undefinedType*/Type;
var x = {}; x.some/*undefinedProperty*/Property;
var a: any; a.some/*unkownProperty*/Property;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, f->MarkerNames());
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionUndefinedSymbols, TestGoToDefinitionUndefinedSymbols);


static void TestGoToDefinitionTypeofThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(/*fnDecl*/this: number) {
    type X = typeof [|/*fnUse*/this|];
}
class /*cls*/C {
    constructor() { type X = typeof [|/*clsUse*/this|]; }
    get self(/*getterDecl*/this: number) { type X = typeof [|/*getterUse*/this|]; }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"fnUse", "clsUse", "getterUse"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionTypeofThis, TestGoToDefinitionTypeofThis);


static void TestGoToDefinitionTypeReferenceDirective(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @typeRoots: src/types
// @Filename: src/types/lib/index.d.ts
/*0*/declare let $: {x: number};
// @Filename: src/app.ts
 /// <reference types="[|lib/*1*/|]"/>
 $.x;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionTypeReferenceDirective, TestGoToDefinitionTypeReferenceDirective);


static void TestGoToDefinitionTypePredicate(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class /*classDeclaration*/A {}
function f(/*parameterDeclaration*/parameter: any): [|/*parameterName*/parameter|] is [|/*typeReference*/A|] {
    return typeof parameter === "string";
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"parameterName", "typeReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionTypePredicate, TestGoToDefinitionTypePredicate);


static void TestGoToDefinitionTypeOnlyImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
enum /*1*/SyntaxKind { SourceFile }
export type { SyntaxKind }
// @Filename: /b.ts
 export type { SyntaxKind } from './a';
// @Filename: /c.ts
import type { SyntaxKind } from './b';
let kind: [|/*2*/SyntaxKind|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionTypeOnlyImport, TestGoToDefinitionTypeOnlyImport);


static void TestGoToDefinitionThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(/*fnDecl*/this: number) {
    return [|/*fnUse*/this|];
}
class /*cls*/C {
    constructor() { return [|/*clsUse*/this|]; }
    get self(/*getterDecl*/this: number) { return [|/*getterUse*/this|]; }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"fnUse", "clsUse", "getterUse"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionThis, TestGoToDefinitionThis);


static void TestGoToDefinitionTaggedTemplateOverloads(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function /*defFNumber*/f(strs: TemplateStringsArray, x: number): void;
function /*defFBool*/f(strs: TemplateStringsArray, x: boolean): void;
function f(strs: TemplateStringsArray, x: number | boolean) {}

[|/*useFNumber*/f|]`${0}`;
[|/*useFBool*/f|]`${false}`;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"useFNumber", "useFBool"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionTaggedTemplateOverloads, TestGoToDefinitionTaggedTemplateOverloads);


static void TestGoToDefinitionSwitchCase7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(switch (null) {
  case null:
    export [|/*start*/default|] 123;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSwitchCase7, TestGoToDefinitionSwitchCase7);


static void TestGoToDefinitionSwitchCase6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export default { [|/*a*/case|] };
[|/*b*/default|];
[|/*c*/case|] 42;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"a", "b", "c"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSwitchCase6, TestGoToDefinitionSwitchCase6);


static void TestGoToDefinitionSwitchCase5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = "export [|/*start*/default|] {}";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSwitchCase5, TestGoToDefinitionSwitchCase5);


static void TestGoToDefinitionSwitchCase4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(     switch (null) {
         case null: break;
     }

     switch (null) {
        [|/*start*/case|] null: break;
     })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSwitchCase4, TestGoToDefinitionSwitchCase4);


static void TestGoToDefinitionSwitchCase3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(switch (null) {
  [|/*start1*/default|]: {
    switch (null) {
      [|/*start2*/default|]: break;
    }
  };
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start1", "start2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSwitchCase3, TestGoToDefinitionSwitchCase3);


static void TestGoToDefinitionSwitchCase2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(switch (null) {
  [|/*start*/default|]: break;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSwitchCase2, TestGoToDefinitionSwitchCase2);


static void TestGoToDefinitionSwitchCase1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(switch (null ) {
  [|/*start*/case|] null: break;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSwitchCase1, TestGoToDefinitionSwitchCase1);


static void TestGoToDefinitionSourceUnit(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
 //MyFile Comments
 //more comments
 /// <reference path="so/*unknownFile*/mePath.ts" />
 /// <reference path="[|b/*knownFile*/.ts|]" />

 class clsInOverload {
     static fnOverload();
     static fnOverload(foo: string);
     static fnOverload(foo: any) { }
 }

// @Filename: b.ts
/*fileB*/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"unknownFile", "knownFile"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSourceUnit, TestGoToDefinitionSourceUnit);


static void TestGoToDefinitionSimple(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: Definition.ts
class /*2*/c { }
// @Filename: Consumption.ts
 var n = new [|/*1*/c|]();
 var n = new [|c/*3*/|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSimple, TestGoToDefinitionSimple);


static void TestGoToDefinitionSignatureAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: preserve
// @Filename: /a.tsx
function /*f*/f() {}
const /*g*/g = f;
const /*h*/h = g;
[|/*useF*/f|]();
[|/*useG*/g|]();
[|/*useH*/h|]();
const /*i*/i = () => 0;
const /*iFn*/iFn = function () { return 0; };
const /*j*/j = i;
[|/*useI*/i|]();
[|/*useIFn*/iFn|]();
[|/*useJ*/j|]();
const o = { /*m*/m: () => 0 };
o.[|/*useM*/m|]();
const oFn = { /*mFn*/mFn: function () { return 0; } };
oFn.[|/*useMFn*/mFn|]();
class Component { /*componentCtr*/constructor(props: {}) {} }
type ComponentClass = /*ComponentClass*/new () => Component;
interface ComponentClass2 { /*ComponentClass2*/new(): Component; }

class /*MyComponent*/MyComponent extends Component {}
<[|/*jsxMyComponent*/MyComponent|] />;
new [|/*newMyComponent*/MyComponent|]({});

declare const /*MyComponent2*/MyComponent2: ComponentClass;
<[|/*jsxMyComponent2*/MyComponent2|] />;
new [|/*newMyComponent2*/MyComponent2|]();

declare const /*MyComponent3*/MyComponent3: ComponentClass2;
<[|/*jsxMyComponent3*/MyComponent3|] />;
new [|/*newMyComponent3*/MyComponent3|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineGoToDefinition(t, true, {"useF", "useG", "useH", "useI", "useIFn", "useJ", "useM", "useMFn", "jsxMyComponent", "newMyComponent", "jsxMyComponent2", "newMyComponent2", "jsxMyComponent3", "newMyComponent3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSignatureAlias, TestGoToDefinitionSignatureAlias);


static void TestGoToDefinitionSignatureAlias_require(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
module.exports = function /*f*/f() {}
// @Filename: /b.js
const f = require("./a");
[|/*use*/f|]();
// @Filename: /bar.ts
import f = require("./a");
[|/*useTs*/f|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"use", "useTs"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSignatureAlias_require, TestGoToDefinitionSignatureAlias_require);


static void TestGoToDefinitionShorthandProperty06(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    /*2*/foo(): void
}
const foo = 1;
let x: Foo = {
    [|f/*1*/oo|]()
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionShorthandProperty06, TestGoToDefinitionShorthandProperty06);


static void TestGoToDefinitionShorthandProperty05(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    /*3*/foo(): void
}
const /*2*/foo = 1;
let x: Foo = {
    [|f/*1*/oo|]
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionShorthandProperty05, TestGoToDefinitionShorthandProperty05);


static void TestGoToDefinitionShorthandProperty04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    /*2*/foo(): void
}

let x: Foo = {
    [|f/*1*/oo|]
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionShorthandProperty04, TestGoToDefinitionShorthandProperty04);


static void TestGoToDefinitionShorthandProperty03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var /*varDef*/x = {
    [|/*varProp*/x|]
}
let /*letDef*/y = {
    [|/*letProp*/y|]
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"varProp", "letProp"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionShorthandProperty03, TestGoToDefinitionShorthandProperty03);


static void TestGoToDefinitionShorthandProperty02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let x = {
    [|f/*1*/oo|]
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionShorthandProperty02, TestGoToDefinitionShorthandProperty02);


static void TestGoToDefinitionShorthandProperty01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
var /*valueDeclaration1*/name = "hello";
var /*valueDeclaration2*/id = 100000;
declare var /*valueDeclaration3*/id;
var obj = {[|/*valueDefinition1*/name|], [|/*valueDefinition2*/id|]};
obj.[|/*valueReference1*/name|];
obj.[|/*valueReference2*/id|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"valueDefinition1", "valueDefinition2", "valueReference1", "valueReference2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionShorthandProperty01, TestGoToDefinitionShorthandProperty01);


static void TestGoToDefinitionShorthandObjectLiteralWithInterface(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Something {
    [|foo|]: string;
}

function makeSomething([|foo|]: string): Something {
    return { [|f/*1*/oo|] };
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionShorthandObjectLiteralWithInterface, TestGoToDefinitionShorthandObjectLiteralWithInterface);


static void TestGoToDefinitionShadowVariable(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var shadowVariable = "foo";
function shadowVariableTestModule() {
    var /*shadowVariableDefinition*/shadowVariable;
    /*shadowVariableReference*/shadowVariable = 1;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"shadowVariableReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionShadowVariable, TestGoToDefinitionShadowVariable);


static void TestGoToDefinitionShadowVariableInsideModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace shdModule {
    var /*shadowVariableDefinition*/shdVar;
    /*shadowVariableReference*/shdVar = 1;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"shadowVariableReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionShadowVariableInsideModule, TestGoToDefinitionShadowVariableInsideModule);


static void TestGoToDefinitionScriptImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: scriptThing.ts
/*1d*/console.log("woooo side effects")
// @filename: stylez.css
/*2d*/div {
  color: magenta;
}
// @filename: moduleThing.ts
import [|/*1*/"./scriptThing"|];
import [|/*2*/"./stylez.css"|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionScriptImport, TestGoToDefinitionScriptImport);


static void TestGoToDefinitionScriptImportServer(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: /home/src/workspaces/project/scriptThing.ts
/*1d*/console.log("woooo side effects")
// @Filename: /home/src/workspaces/project/stylez.css
/*2d*/div {
  color: magenta;
}
// @Filename: /home/src/workspaces/project/moduleThing.ts
import [|/*1*/"./scriptThing"|];
import [|/*2*/"./stylez.css"|];
import [|/*3*/"./foo.txt"|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToDefinition(t, true, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionScriptImportServer, TestGoToDefinitionScriptImportServer);


static void TestGoToDefinitionSatisfiesExpression1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const STRINGS = {
    [|/*definition*/title|]: 'A Title',
} satisfies Record<string,string>;

//somewhere in app
STRINGS.[|/*usage*/title|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"definition", "usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSatisfiesExpression1, TestGoToDefinitionSatisfiesExpression1);


static void TestGoToDefinitionSameFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var /*localVariableDefinition*/localVariable;
function /*localFunctionDefinition*/localFunction() { }
class /*localClassDefinition*/localClass { }
interface /*localInterfaceDefinition*/localInterface{ }
module /*localModuleDefinition*/localModule{ export var foo = 1;}


/*localVariableReference*/localVariable = 1;
/*localFunctionReference*/localFunction();
var foo = new /*localClassReference*/localClass();
class fooCls implements /*localInterfaceReference*/localInterface { }
var fooVar = /*localModuleReference*/localModule.foo;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"localVariableReference", "localFunctionReference", "localClassReference", "localInterfaceReference", "localModuleReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionSameFile, TestGoToDefinitionSameFile);


static void TestGoToDefinitionReturn7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(a: string, b: string): string;
function foo(a: number, b: number): number;
function /*end*/foo(a: any, b: any): any {
    [|/*start*/return|] a + b;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionReturn7, TestGoToDefinitionReturn7);


static void TestGoToDefinitionReturn6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo() {
    return /*end*/function () {
        [|/*start*/return|] 10;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionReturn6, TestGoToDefinitionReturn6);


static void TestGoToDefinitionReturn5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo() {
    class Foo {
        static { [|/*start*/return|]; }
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionReturn5, TestGoToDefinitionReturn5);


static void TestGoToDefinitionReturn4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = "[|/*start*/return|];";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionReturn4, TestGoToDefinitionReturn4);


static void TestGoToDefinitionReturn3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    /*end*/m() {
        [|/*start*/return|] 1;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionReturn3, TestGoToDefinitionReturn3);


static void TestGoToDefinitionReturn2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo() {
    return /*end*/() => {
        [|/*start*/return|] 10;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionReturn2, TestGoToDefinitionReturn2);


static void TestGoToDefinitionReturn1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function /*end*/foo() {
    [|/*start*/return|] 10;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionReturn1, TestGoToDefinitionReturn1);


static void TestGoToDefinitionRest(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Gen {
    x: number;
    /*1*/parent: Gen;
    millenial: string;
}
let t: Gen;
var { x, ...rest } = t;
rest.[|/*2*/parent|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionRest, TestGoToDefinitionRest);


static void TestGoToDefinitionPropertyAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export const /*FunctionResult*/Component = () => { return "OK"}
Component./*PropertyResult*/displayName = 'Component'

[|/*FunctionClick*/Component|]

Component.[|/*PropertyClick*/displayName|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"FunctionClick", "PropertyClick"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionPropertyAssignment, TestGoToDefinitionPropertyAssignment);


static void TestGoToDefinitionPrivateName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    [|/*pnMethodDecl*/#method|]() { }
    [|/*pnFieldDecl*/#foo|] = 3;
    get [|/*pnPropGetDecl*/#prop|]() { return ""; }
    set [|/*pnPropSetDecl*/#prop|](value: string) {  }
    constructor() {
        this.[|/*pnFieldUse*/#foo|]
        this.[|/*pnMethodUse*/#method|]
        this.[|/*pnPropUse*/#prop|]
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"pnFieldUse", "pnMethodUse", "pnPropUse"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionPrivateName, TestGoToDefinitionPrivateName);


static void TestGoToDefinitionPrimitives(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = "var x: st/*primitive*/ring;";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"primitive"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionPrimitives, TestGoToDefinitionPrimitives);


static void TestGoToDefinitionPartialImplementation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: goToDefinitionPartialImplementation_1.ts
namespace A {
    export interface /*Part1Definition*/IA {
        y: string;
    }
}
// @Filename: goToDefinitionPartialImplementation_2.ts
namespace A {
    export interface /*Part2Definition*/IA {
        x: number;
    }

    var x: [|/*Part2Use*/IA|];
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"Part2Use"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionPartialImplementation, TestGoToDefinitionPartialImplementation);


static void TestGoToDefinitionOverriddenMember9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
interface I {
    m(): void;
}
class A {
    /*2*/m() {};
}
class B extends A implements I {
   [|/*1*/override|] m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember9, TestGoToDefinitionOverriddenMember9);


static void TestGoToDefinitionOverriddenMember8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
// @Filename: ./a.ts
export class A {
    /*2*/m() {}
}
// @Filename: ./b.ts
import { A } from "./a";
class B extends A {
    [|/*1*/override|] m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember8, TestGoToDefinitionOverriddenMember8);


static void TestGoToDefinitionOverriddenMember7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
class Foo {
    [|/*1*/override|] m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember7, TestGoToDefinitionOverriddenMember7);


static void TestGoToDefinitionOverriddenMember6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
class Foo {
    m() {}
}
class Bar extends Foo {
    [|/*1*/override|] m1() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember6, TestGoToDefinitionOverriddenMember6);


static void TestGoToDefinitionOverriddenMember5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
class Foo extends (class {
    /*2*/m() {}
}) {
    [|/*1*/override|] m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember5, TestGoToDefinitionOverriddenMember5);


static void TestGoToDefinitionOverriddenMember4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
class Foo {
    /*2*/m() {}
}
function f () {
    return class extends Foo {
        [|/*1*/override|] m() {}
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember4, TestGoToDefinitionOverriddenMember4);


static void TestGoToDefinitionOverriddenMember3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
abstract class Foo {
	abstract /*2*/m() {}
}

export class Bar extends Foo {
	[|/*1*/override|] m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember3, TestGoToDefinitionOverriddenMember3);


static void TestGoToDefinitionOverriddenMember2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
class Foo {
	/*2*/m() {}
}

class Bar extends Foo {
	[|/*1*/override|] m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember2, TestGoToDefinitionOverriddenMember2);


static void TestGoToDefinitionOverriddenMember26(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @target: esnext
// @lib: esnext
const prop: symbol = Symbol();

abstract class A {}

export class B extends A {
  [|/*1*/override|] [prop]() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember26, TestGoToDefinitionOverriddenMember26);


static void TestGoToDefinitionOverriddenMember25(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @target: esnext
// @lib: esnext
const prop: symbol = Symbol();

abstract class A {}

export class B extends A {
  static [|/*1*/override|] [prop]() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember25, TestGoToDefinitionOverriddenMember25);


static void TestGoToDefinitionOverriddenMember24(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @target: esnext
// @lib: esnext
const prop: symbol = Symbol();

abstract class A {
  [prop]() {}
}

export class B extends A {
  [|/*1*/override|] [prop]() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember24, TestGoToDefinitionOverriddenMember24);


static void TestGoToDefinitionOverriddenMember23(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @target: esnext
// @lib: esnext
const prop: symbol = Symbol();

abstract class A {
  static [prop]() {}
}

export class B extends A {
  static [|/*1*/override|] [prop]() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember23, TestGoToDefinitionOverriddenMember23);


static void TestGoToDefinitionOverriddenMember22(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @target: esnext
// @lib: esnext
const prop = "foo" as const;

abstract class A {}

export class B extends A {
  [|/*1*/override|] readonly [prop] = "B";
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember22, TestGoToDefinitionOverriddenMember22);


static void TestGoToDefinitionOverriddenMember21(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @target: esnext
// @lib: esnext
const prop = "foo" as const;

abstract class A {}

export class B extends A {
  static [|/*1*/override|] readonly [prop] = "B";
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember21, TestGoToDefinitionOverriddenMember21);


static void TestGoToDefinitionOverriddenMember20(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @target: esnext
// @lib: esnext
const prop = "foo" as const;

abstract class A {
  readonly /*2*/[prop] = "A";
}

export class B extends A {
  [|/*1*/override|] readonly [prop] = "B";
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember20, TestGoToDefinitionOverriddenMember20);


static void TestGoToDefinitionOverriddenMember1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
class Foo {
	/*2*/p = '';
}
class Bar extends Foo {
	[|/*1*/override|] p = '';
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember1, TestGoToDefinitionOverriddenMember1);


static void TestGoToDefinitionOverriddenMember19(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @target: esnext
// @lib: esnext
const prop = "foo" as const;

abstract class A {
  static readonly /*2*/[prop] = "A";
}

export class B extends A {
  static [|/*1*/override|] readonly [prop] = "B";
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember19, TestGoToDefinitionOverriddenMember19);


static void TestGoToDefinitionOverriddenMember18(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @target: esnext
// @lib: esnext
const entityKind = Symbol.for("drizzle:entityKind");

abstract class MySqlColumn {
  readonly /*2*/[entityKind]: string = "MySqlColumn";
}

export class MySqlVarBinary extends MySqlColumn {
  [|/*1*/override|] readonly [entityKind]: string = "MySqlVarBinary";
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember18, TestGoToDefinitionOverriddenMember18);


static void TestGoToDefinitionOverriddenMember17(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @target: esnext
// @lib: esnext
const entityKind = Symbol.for("drizzle:entityKind");

abstract class MySqlColumn {
  static readonly /*2*/[entityKind]: string = "MySqlColumn";
}

export class MySqlVarBinary extends MySqlColumn {
  static [|/*1*/override|] readonly [entityKind]: string = "MySqlVarBinary";
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember17, TestGoToDefinitionOverriddenMember17);


static void TestGoToDefinitionOverriddenMember16(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: goToDefinitionOverrideJsdoc.ts
// @allowJs: true
// @checkJs: true
export class C extends CompletelyUndefined {
    /**
     * @override/*1*/
     * @returns {{}}
     */
    static foo() {
        return {}
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember16, TestGoToDefinitionOverriddenMember16);


static void TestGoToDefinitionOverriddenMember15(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
class A {
    static /*2*/m() {}
}
class B extends A {}
class C extends B {
    static [|/*1*/override|] m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember15, TestGoToDefinitionOverriddenMember15);


static void TestGoToDefinitionOverriddenMember14(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
class A {
    /*2*/m() {}
}
class B extends A {}
class C extends B {
    [|/*1*/override|] m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember14, TestGoToDefinitionOverriddenMember14);


static void TestGoToDefinitionOverriddenMember13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
class Foo {
	static /*2*/m() {}
}
class Bar extends Foo {
	static [|/*1*/override|] m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember13, TestGoToDefinitionOverriddenMember13);


static void TestGoToDefinitionOverriddenMember12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitOverride: true
class Foo {
	static /*2*/p = '';
}
class Bar extends Foo {
	static [|/*1*/override|] p = '';
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember12, TestGoToDefinitionOverriddenMember12);


static void TestGoToDefinitionOverriddenMember11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @noEmit: true
// @noImplicitOverride: true
// @filename: a.js
class Foo {
    /*Foo_m*/m() {}
}
class Bar extends Foo {
    /** @[|over{|"name": "1"|}ride|][| se{|"name": "2"|}e {@li{|"name": "3"|}nk https://test.c{|"name": "4"|}om} {|"name": "5"|}description |]*/
    m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember11, TestGoToDefinitionOverriddenMember11);


static void TestGoToDefinitionOverriddenMember10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @noEmit: true
// @noImplicitOverride: true
// @filename: a.js
class Foo {}
class Bar extends Foo {
    /** [|@override{|"name": "1"|} |]*/
    m() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverriddenMember10, TestGoToDefinitionOverriddenMember10);


static void TestGoToDefinitionOverloadsInMultiplePropertyAccesses(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace A {
    export namespace B {
        export function f(value: number): void;
        export function /*1*/f(value: string): void;
        export function f(value: number | string) {}
    }
}
A.B.[|/*2*/f|]("");)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOverloadsInMultiplePropertyAccesses, TestGoToDefinitionOverloadsInMultiplePropertyAccesses);


static void TestGoToDefinitionOnInvalidParameterDecorator(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = "function f(@/*1*/f) {}";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionOnInvalidParameterDecorator, TestGoToDefinitionOnInvalidParameterDecorator);


static void TestGoToDefinitionObjectSpread(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A1 { /*1*/a: number };
interface A2 { /*2*/a?: number };
let a1: A1;
let a2: A2;
let a12 = { ...a1, ...a2 };
a12.[|a/*3*/|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionObjectSpread, TestGoToDefinitionObjectSpread);


static void TestGoToDefinitionObjectLiteralProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var o = {
    /*valueDefinition*/value: 0,
    get /*getterDefinition*/getter() {return 0 },
    set /*setterDefinition*/setter(v: number) { },
    /*methodDefinition*/method: () => { },
    /*es6StyleMethodDefinition*/es6StyleMethod() { }
};

o./*valueReference*/value;
o./*getterReference*/getter;
o./*setterReference*/setter;
o./*methodReference*/method;
o./*es6StyleMethodReference*/es6StyleMethod;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"valueReference", "getterReference", "setterReference", "methodReference", "es6StyleMethodReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionObjectLiteralProperties, TestGoToDefinitionObjectLiteralProperties);


static void TestGoToDefinitionObjectLiteralProperties3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type A = {
  foo: unknown;
};

type B = {
  foo?: unknown;
  bar: unknown;
};

function test1(arg: A | B) {}

test1({
  foo/*1*/: 1,
});

function test2<T extends A>(arg: T | B) {}

test2({
  foo/*2*/: 2,
});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionObjectLiteralProperties3, TestGoToDefinitionObjectLiteralProperties3);


static void TestGoToDefinitionObjectLiteralProperties2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type C = {
  foo: string;
  bar: number;
};

declare function fn<T extends C>(arg: T): T;

fn({
  foo/*1*/: "",
  bar/*2*/: true,
});

const result = fn({
  foo/*3*/: "",
  bar/*4*/: 1,
});

// this one shouldn't go to the constraint type
result.foo/*5*/;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionObjectLiteralProperties2, TestGoToDefinitionObjectLiteralProperties2);


static void TestGoToDefinitionObjectLiteralProperties1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface PropsBag {
   /*first*/propx: number
}
function foo(arg: PropsBag) {}
foo({
   [|pr/*p1*/opx|]: 10
})
function bar(firstarg: boolean, secondarg: PropsBag) {}
bar(true, {
   [|pr/*p2*/opx|]: 10
}))TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"p1", "p2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionObjectLiteralProperties1, TestGoToDefinitionObjectLiteralProperties1);


static void TestGoToDefinitionObjectBindingElementPropertyName01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    /*def*/property1: number;
    property2: string;
}

var foo: I;
var { [|/*use*/property1|]: prop1 } = foo;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionObjectBindingElementPropertyName01, TestGoToDefinitionObjectBindingElementPropertyName01);


static void TestGoToDefinitionNewExpressionTargetNotClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C2 {
}
let /*I*/I: {
    /*constructSignature*/new(): C2;
};
new [|/*invokeExpression1*/I|]();
let /*symbolDeclaration*/I2: {
};
new [|/*invokeExpression2*/I2|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"invokeExpression1", "invokeExpression2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionNewExpressionTargetNotClass, TestGoToDefinitionNewExpressionTargetNotClass);


static void TestGoToDefinitionMultipleDefinitions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
interface /*interfaceDefinition1*/IFoo {
    instance1: number;
}
// @Filename: b.ts
interface /*interfaceDefinition2*/IFoo {
    instance2: number;
}

interface /*interfaceDefinition3*/IFoo {
    instance3: number;
}

var ifoo: [|IFo/*interfaceReference*/o|];
// @Filename: c.ts
module /*moduleDefinition1*/Module {
    export class c1 { }
}
// @Filename: d.ts
module /*moduleDefinition2*/Module {
    export class c2 { }
}
// @Filename: e.ts
[|Modul/*moduleReference*/e|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"interfaceReference", "moduleReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionMultipleDefinitions, TestGoToDefinitionMultipleDefinitions);


static void TestGoToDefinitionModifiers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
/*export*/export class A/*A*/ {

    /*private*/private z/*z*/: string;

    /*readonly*/readonly x/*x*/: string;

    /*async*/async a/*a*/() {  }

    /*override*/override b/*b*/() {}

    /*public1*/public/*public2*/ as/*multipleModifiers*/ync c/*c*/() { }
}

exp/*exportFunction*/ort function foo/*foo*/() { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"export", "A", "private", "z", "readonly", "x", "async", "a", "override", "b", "public1", "public2", "multipleModifiers", "c", "exportFunction", "foo"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionModifiers, TestGoToDefinitionModifiers);


static void TestGoToDefinitionMethodOverloads(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class MethodOverload {
    static [|/*staticMethodOverload1*/method|]();
    static /*staticMethodOverload2*/method(foo: string);
    static /*staticMethodDefinition*/method(foo?: any) { }
    public [|/*instanceMethodOverload1*/method|](): any;
    public /*instanceMethodOverload2*/method(foo: string);
    public /*instanceMethodDefinition*/method(foo?: any) { return "foo" }
}
// static method
MethodOverload.[|/*staticMethodReference1*/method|]();
MethodOverload.[|/*staticMethodReference2*/method|]("123");
// instance method
var methodOverload = new MethodOverload();
methodOverload.[|/*instanceMethodReference1*/method|]();
methodOverload.[|/*instanceMethodReference2*/method|]("456");)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"staticMethodReference1", "staticMethodReference2", "instanceMethodReference1", "instanceMethodReference2", "staticMethodOverload1", "instanceMethodOverload1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionMethodOverloads, TestGoToDefinitionMethodOverloads);


static void TestGoToDefinitionMetaProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
im/*1*/port.met/*2*/a;
function /*functionDefinition*/f() { n/*3*/ew.[|t/*4*/arget|]; }
// @Filename: /b.ts
im/*5*/port.m;
class /*classDefinition*/c { constructor() { n/*6*/ew.[|t/*7*/arget|]; } })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1", "2", "3", "4", "5", "6", "7"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionMetaProperty, TestGoToDefinitionMetaProperty);


static void TestGoToDefinitionMember(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
class A {
    private z/*z*/: string;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"z"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionMember, TestGoToDefinitionMember);


static void TestGoToDefinitionLabels(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*label1Definition*/label1: while (true) {
    /*label2Definition*/label2: while (true) {
        break [|/*1*/label1|];
        continue [|/*2*/label2|];
        () => { break [|/*3*/label1|]; }
        continue /*4*/unknownLabel;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionLabels, TestGoToDefinitionLabels);


static void TestGoToDefinitionJsxTagNameRightEdge(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @jsx: react-jsx
// @filename: /a.jsx
export function Component() {
    return null;
}

export function App() {
    return <Component/*use*/ />
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsxTagNameRightEdge, TestGoToDefinitionJsxTagNameRightEdge);


static void TestGoToDefinitionJsxNotSet(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /foo.jsx
const /*def*/Foo = () => (
    <div>foo</div>
);
export default Foo;
// @Filename: /bar.jsx
import Foo from './foo';
const a = <[|/*use*/Foo|] />)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsxNotSet, TestGoToDefinitionJsxNotSet);


static void TestGoToDefinitionJsxCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: ./test.tsx
interface FC<P = {}> {
    (props: P, context?: any): string;
}

const Thing: FC = (props) => <div></div>;
const HelloWorld = () => <[|/**/Thing|] />;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsxCall, TestGoToDefinitionJsxCall);


static void TestGoToDefinitionJsModuleName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: foo.js
/*2*/module.exports = {};
// @Filename: bar.js
var x = require([|/*1*/"./foo"|]);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsModuleName, TestGoToDefinitionJsModuleName);


static void TestGoToDefinitionJsModuleNameAtImportName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /foo.js
 /*moduleDef*/function notExported() { }
 class Blah {
    abc = 123;
 }
 module.exports.Blah = Blah;
// @Filename: /bar.js
const [|/*importDef*/BlahModule|] = require("./foo.js");
new [|/*importUsage*/BlahModule|].Blah()
// @Filename: /barTs.ts
import [|/*importDefTs*/BlahModule|] = require("./foo.js");
new [|/*importUsageTs*/BlahModule|].Blah())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"importDef", "importUsage", "importDefTs", "importUsageTs"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsModuleNameAtImportName, TestGoToDefinitionJsModuleNameAtImportName);


static void TestGoToDefinitionJsModuleExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: foo.js
x./*def*/test = () => { }
x.[|/*ref*/test|]();
x./*defFn*/test3 = function () { }
x.[|/*refFn*/test3|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"ref", "refFn"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsModuleExports, TestGoToDefinitionJsModuleExports);


static void TestGoToDefinitionJsDocImportTag5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @Filename: /b.ts
export interface /*2*/A { }
// @Filename: /a.js
/**
 * @import { A } from "./b";
 */

/**
 * @param { [|A/*1*/|] } a
 */
function f(a) {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsDocImportTag5, TestGoToDefinitionJsDocImportTag5);


static void TestGoToDefinitionJsDocImportTag4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @Filename: /b.ts
export interface /*2*/A { }
// @Filename: /a.js
/**
 * @import { [|A/*1*/|] } from "./b";
 */)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsDocImportTag4, TestGoToDefinitionJsDocImportTag4);


static void TestGoToDefinitionJsDocImportTag3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @Filename: /b.ts
/*2*/export interface A { }
// @Filename: /a.js
/**
 * @import { A } [|from     /*1*/|] "./b";
 */)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsDocImportTag3, TestGoToDefinitionJsDocImportTag3);


static void TestGoToDefinitionJsDocImportTag2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @Filename: /b.ts
/*2*/export interface A { }
// @Filename: /a.js
/**
 * @import { A } [|from/*1*/|]       "./b"
 */)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsDocImportTag2, TestGoToDefinitionJsDocImportTag2);


static void TestGoToDefinitionJsDocImportTag1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @Filename: /b.ts
/*2*/export interface A { }
// @Filename: /a.js
/**
 * @import { A } from      [|"./b/*1*/"|]
 */)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionJsDocImportTag1, TestGoToDefinitionJsDocImportTag1);


static void TestGoToDefinitionInterfaceAfterImplement(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface /*interfaceDefinition*/sInt {
    sVar: number;
    sFn: () => void;
}

class iClass implements /*interfaceReference*/sInt {
    public sVar = 1;
    public sFn() {
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"interfaceReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionInterfaceAfterImplement, TestGoToDefinitionInterfaceAfterImplement);


static void TestGoToDefinitionInstanceof2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: esnext
// @filename: /main.ts
class C {
  static /*end*/[Symbol.hasInstance](value: unknown): boolean { return true; }
}
declare var obj: any;
obj [|/*start*/instanceof|] C;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionInstanceof2, TestGoToDefinitionInstanceof2);


static void TestGoToDefinitionInstanceof1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
class /*end*/ C {
}
declare var obj: any;
obj [|/*start*/instanceof|] C;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionInstanceof1, TestGoToDefinitionInstanceof1);


static void TestGoToDefinitionIndexSignature(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    /*defI*/[x: string]: boolean;
}
interface J {
    /*defJ*/[x: string]: number;
}
interface K {
    /*defa*/[x: `a${string}`]: string;
    /*defb*/[x: `${string}b`]: string;
}
declare const i: I;
i.[|/*useI*/foo|];
declare const ij: I | J;
ij.[|/*useIJ*/foo|];
declare const k: K;
k.[|/*usea*/a|];
k.[|/*useb*/b|];
k.[|/*useab*/ab|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"useI", "useIJ", "usea", "useb", "useab"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionIndexSignature, TestGoToDefinitionIndexSignature);


static void TestGoToDefinitionIndexSignature2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
const o = {};
o.[|/*use*/foo|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionIndexSignature2, TestGoToDefinitionIndexSignature2);


static void TestGoToDefinitionInTypeArgument(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class /*fooDefinition*/Foo<T> { }

class /*barDefinition*/Bar { }

var x = new Fo/*fooReference*/o<Ba/*barReference*/r>();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"barReference", "fooReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionInTypeArgument, TestGoToDefinitionInTypeArgument);


static void TestGoToDefinitionInMemberDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface /*interfaceDefinition*/IFoo { method1(): number; }

class /*classDefinition*/Foo implements IFoo {
    public method1(): number { return 0; }
}

enum /*enumDefinition*/Enum { value1, value2 };

class /*selfDefinition*/Bar {
    public _interface: [|IFo/*interfaceReference*/o|] = new [|Fo/*classReferenceInInitializer*/o|]();
    public _class: [|Fo/*classReference*/o|] = new Foo();
    public _list: [|IF/*interfaceReferenceInList*/oo|][]=[];
    public _enum: [|E/*enumReference*/num|] = [|En/*enumReferenceInInitializer*/um|].value1;
    public _self: [|Ba/*selfReference*/r|];

    constructor(public _inConstructor: [|IFo/*interfaceReferenceInConstructor*/o|]) {
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"interfaceReference", "interfaceReferenceInList", "interfaceReferenceInConstructor", "classReference", "classReferenceInInitializer", "enumReference", "enumReferenceInInitializer", "selfReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionInMemberDeclaration, TestGoToDefinitionInMemberDeclaration);


static void TestGoToDefinitionImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export default function /*fDef*/f() {}
export const /*xDef*/x = 0;
// @Filename: /b.ts
/*bDef*/declare const b: number;
export = b;
// @Filename: /b.ts
import f, { x } from "./a";
import * as /*aDef*/a from "./a";
import b = require("./b");
[|/*fUse*/f|];
[|/*xUse*/x|];
[|/*aUse*/a|];
[|/*bUse*/b|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"aUse", "fUse", "xUse", "bUse"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImports, TestGoToDefinitionImports);


static void TestGoToDefinitionImportedNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
export {[|/*classAliasDefinition*/Class|]} from "./a";
// @Filename: a.ts
export namespace Module {
}
export class /*classDefinition*/Class {
    private f;
}
export interface Interface {
    x;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames, TestGoToDefinitionImportedNames);


static void TestGoToDefinitionImportedNames9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowjs: true
// @Filename: a.js
class /*classDefinition*/Class {
    f;
}
 export { Class };
// @Filename: b.js
const { Class } = require("./a");
 [|/*classAliasDefinition*/Class|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames9, TestGoToDefinitionImportedNames9);


static void TestGoToDefinitionImportedNames8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowjs: true
// @Filename: b.js
import { [|/*classAliasDefinition*/Class|] } from "./a";
// @Filename: a.js
class /*classDefinition*/Class {
    private f;
}
 export { Class };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames8, TestGoToDefinitionImportedNames8);


static void TestGoToDefinitionImportedNames7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
import [|/*classAliasDefinition*/defaultExport|] from "./a";
// @Filename: a.ts
class /*classDefinition*/Class {
    private f;
}
export default Class;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames7, TestGoToDefinitionImportedNames7);


static void TestGoToDefinitionImportedNames6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
import [|/*moduleAliasDefinition*/alias|] = require("./a");
// @Filename: a.ts
/*moduleDefinition*/export namespace Module {
}
export class Class {
    private f;
}
export interface Interface {
    x;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"moduleAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames6, TestGoToDefinitionImportedNames6);


static void TestGoToDefinitionImportedNames5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
export {Class as [|/*classAliasDefinition*/ClassAlias|]} from "./a";
// @Filename: a.ts
export namespace Module {
}
export class /*classDefinition*/Class {
    private f;
}
export interface Interface {
    x;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames5, TestGoToDefinitionImportedNames5);


static void TestGoToDefinitionImportedNames4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
import {Class as [|/*classAliasDefinition*/ClassAlias|]} from "./a";
// @Filename: a.ts
export namespace Module {
}
export class /*classDefinition*/Class {
    private f;
}
export interface Interface {
    x;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames4, TestGoToDefinitionImportedNames4);


static void TestGoToDefinitionImportedNames3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: e.ts
 import {M, [|/*classAliasDefinition*/C|], I} from "./d";
 var c = new [|/*classReference*/C|]();
// @Filename: d.ts
export * from "./c";
// @Filename: c.ts
export {Module as M, Class as C, Interface as I} from "./b";
// @Filename: b.ts
export * from "./a";
// @Filename: a.ts
export namespace Module {
}
export class /*classDefinition*/Class {
    private f;
}
export interface Interface {
    x;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classReference", "classAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames3, TestGoToDefinitionImportedNames3);


static void TestGoToDefinitionImportedNames2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
import {[|/*classAliasDefinition*/Class|]} from "./a";
// @Filename: a.ts
export namespace Module {
}
export class /*classDefinition*/Class {
    private f;
}
export interface Interface {
    x;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames2, TestGoToDefinitionImportedNames2);


static void TestGoToDefinitionImportedNames11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowjs: true
// @Filename: a.js
 class /*classDefinition*/Class {
     f;
 }
 module.exports = { Class };
// @Filename: b.js
const { Class } = require("./a");
 [|/*classAliasDefinition*/Class|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames11, TestGoToDefinitionImportedNames11);


static void TestGoToDefinitionImportedNames10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowjs: true
// @Filename: a.js
 class /*classDefinition*/Class {
   f;
 }
 module.exports.Class = Class;
// @Filename: b.js
const { Class } = require("./a");
 [|/*classAliasDefinition*/Class|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classAliasDefinition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportedNames10, TestGoToDefinitionImportedNames10);


static void TestGoToDefinitionImportMeta(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @Filename: foo.ts
/// <reference path='./bar.d.ts' />
import.me/*reference*/ta;
//@Filename: bar.d.ts
interface ImportMeta {
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"reference"});
		f->VerifyNoErrors(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImportMeta, TestGoToDefinitionImportMeta);


static void TestGoToDefinitionImport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /b.ts
/*2*/export const foo = 1;
// @Filename: /a.ts
import { foo } [|from     /*1*/|] "./b";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImport3, TestGoToDefinitionImport3);


static void TestGoToDefinitionImport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /b.ts
/*2*/export const foo = 1;
// @Filename: /a.ts
import { foo } [|from/*1*/|]       "./b";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImport2, TestGoToDefinitionImport2);


static void TestGoToDefinitionImport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /b.ts
/*2*/export const foo = 1;
// @Filename: /a.ts
import { foo } from      [|"./b/*1*/"|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImport1, TestGoToDefinitionImport1);


static void TestGoToDefinitionImplicitConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class /*constructorDefinition*/ImplicitConstructor {
}
var implicitConstructor = new /*constructorReference*/ImplicitConstructor();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"constructorReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionImplicitConstructor, TestGoToDefinitionImplicitConstructor);


static void TestGoToDefinitionGetterReturningCallableInterface(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/type.d.ts
export interface DidChangeContentEvent {
    (): void;
}

export declare class TextDocuments {
    get onDidChangeContent(): DidChangeContentEvent;
}

// @Filename: /home/src/workspaces/project/index.ts
import { TextDocuments } from "./type";

declare const documents: TextDocuments | undefined;

documents!./*usage*/onDidChangeContent())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionGetterReturningCallableInterface, TestGoToDefinitionGetterReturningCallableInterface);


static void TestGoToDefinitionFunctionType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const /*constDefinition*/c: () => void;
/*constReference*/c();
function test(/*cbDefinition*/cb: () => void) {
    /*cbReference*/cb();
}
class C {
    /*propDefinition*/prop: () => void;
    m() {
        this./*propReference*/prop();
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"constReference", "cbReference", "propReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionFunctionType, TestGoToDefinitionFunctionType);


static void TestGoToDefinitionFunctionOverloads(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function [|/*functionOverload1*/functionOverload|](value: number);
function /*functionOverload2*/functionOverload(value: string);
function /*functionOverloadDefinition*/functionOverload() {}

[|/*functionOverloadReference1*/functionOverload|](123);
[|/*functionOverloadReference2*/functionOverload|]("123");
[|/*brokenOverload*/functionOverload|]({});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"functionOverloadReference1", "functionOverloadReference2", "brokenOverload", "functionOverload1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionFunctionOverloads, TestGoToDefinitionFunctionOverloads);


static void TestGoToDefinitionFunctionOverloadsInClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class clsInOverload {
    static fnOverload();
    static [|/*staticFunctionOverload*/fnOverload|](foo: string);
    static /*staticFunctionOverloadDefinition*/fnOverload(foo: any) { }
    public [|/*functionOverload*/fnOverload|](): any;
    public fnOverload(foo: string);
    public /*functionOverloadDefinition*/fnOverload(foo: any) { return "foo" }

    constructor() { }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"staticFunctionOverload", "functionOverload"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionFunctionOverloadsInClass, TestGoToDefinitionFunctionOverloadsInClass);


static void TestGoToDefinitionExternalModuleName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
import n = require([|'./a/*1*/'|]);
var x = new n.Foo();
// @Filename: a.ts
 /*2*/export class Foo {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExternalModuleName, TestGoToDefinitionExternalModuleName);


static void TestGoToDefinitionExternalModuleName9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
export * from [|'e/*1*/'|];
// @Filename: a.ts
declare module /*2*/"e" {
    class Foo { }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExternalModuleName9, TestGoToDefinitionExternalModuleName9);


static void TestGoToDefinitionExternalModuleName8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
export {Foo, Bar} from [|'e/*1*/'|];
// @Filename: a.ts
declare module /*2*/"e" {
    class Foo { }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExternalModuleName8, TestGoToDefinitionExternalModuleName8);


static void TestGoToDefinitionExternalModuleName7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
import {Foo, Bar} from [|'e/*1*/'|];
// @Filename: a.ts
declare module /*2*/"e" {
    class Foo { }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExternalModuleName7, TestGoToDefinitionExternalModuleName7);


static void TestGoToDefinitionExternalModuleName6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
import * from [|'e/*1*/'|];
// @Filename: a.ts
declare module /*2*/"e" {
    class Foo { }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExternalModuleName6, TestGoToDefinitionExternalModuleName6);


static void TestGoToDefinitionExternalModuleName5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
declare module /*2*/[|"external/*1*/"|] {
    class Foo { }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExternalModuleName5, TestGoToDefinitionExternalModuleName5);


static void TestGoToDefinitionExternalModuleName4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
import n = require('unknown/*1*/');)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExternalModuleName4, TestGoToDefinitionExternalModuleName4);


static void TestGoToDefinitionExternalModuleName3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
import n = require([|'e/*1*/'|]);
var x = new n.Foo();
// @Filename: a.ts
declare module /*2*/"e" {
    class Foo { }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExternalModuleName3, TestGoToDefinitionExternalModuleName3);


static void TestGoToDefinitionExpandoElementAccess(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f() {}
f[/*0*/"x"] = 0;
f[[|/*1*/"x"|]] = 1;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExpandoElementAccess, TestGoToDefinitionExpandoElementAccess);


static void TestGoToDefinitionExpandoClass2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @allowJs: true
// @checkJs: true
// @filename: index.js
const Core = {}

Core.Test = class {
  constructor() { }
}

Core.Test.prototype.foo = 10

new Core.Tes/*1*/t())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExpandoClass2, TestGoToDefinitionExpandoClass2);


static void TestGoToDefinitionExpandoClass1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @allowJs: true
// @checkJs: true
// @filename: index.js
const Core = {}

Core.Test = class { }

Core.Test.prototype.foo = 10

new Core.Tes/*1*/t())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExpandoClass1, TestGoToDefinitionExpandoClass1);


static void TestGoToDefinitionDynamicImport4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export function /*Destination*/bar() { return "bar"; }
import('./foo').then(({ [|ba/*1*/r|] }) => undefined);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionDynamicImport4, TestGoToDefinitionDynamicImport4);


static void TestGoToDefinitionDynamicImport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export function /*Destination*/bar() { return "bar"; }
import('./foo').then(({ [|ba/*1*/r|] }) => undefined);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionDynamicImport3, TestGoToDefinitionDynamicImport3);


static void TestGoToDefinitionDynamicImport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export function /*Destination*/bar() { return "bar"; }
var x = import("./foo");
x.then(foo => {
    foo.[|b/*1*/ar|](); 
}))TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionDynamicImport2, TestGoToDefinitionDynamicImport2);


static void TestGoToDefinitionDynamicImport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
/*Destination*/export function foo() { return "foo"; }
import([|"./f/*1*/oo"|])
var x = import([|"./fo/*2*/o"|]))TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionDynamicImport1, TestGoToDefinitionDynamicImport1);


static void TestGoToDefinitionDifferentFileIndirectly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: Remote2.ts
var /*remoteVariableDefinition*/rem2Var;
function /*remoteFunctionDefinition*/rem2Fn() { }
class /*remoteClassDefinition*/rem2Cls { }
interface /*remoteInterfaceDefinition*/rem2Int{}
module /*remoteModuleDefinition*/rem2Mod { export var foo; }
// @Filename: Remote1.ts
var remVar;
function remFn() { }
class remCls { }
interface remInt{}
namespace remMod { export var foo; }
// @Filename: Definition.ts
/*remoteVariableReference*/rem2Var = 1;
/*remoteFunctionReference*/rem2Fn();
var rem2foo = new /*remoteClassReference*/rem2Cls();
class rem2fooCls implements /*remoteInterfaceReference*/rem2Int { }
var rem2fooVar = /*remoteModuleReference*/rem2Mod.foo;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"remoteVariableReference", "remoteFunctionReference", "remoteClassReference", "remoteInterfaceReference", "remoteModuleReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionDifferentFileIndirectly, TestGoToDefinitionDifferentFileIndirectly);


static void TestGoToDefinitionDestructuredRequire2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: util.js
class /*2*/Util {}
module.exports = { Util };
// @Filename: reexport.js
const { Util } = require('./util');
module.exports = { Util };
// @Filename: index.js
const { Util } = require('./reexport');
new [|Util/*1*/|]())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionDestructuredRequire2, TestGoToDefinitionDestructuredRequire2);


static void TestGoToDefinitionDestructuredRequire1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: util.js
class /*2*/Util {}
module.exports = { Util };
// @Filename: index.js
const { Util } = require('./util');
new [|Util/*1*/|]())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionDestructuredRequire1, TestGoToDefinitionDestructuredRequire1);


static void TestGoToDefinitionDecorator(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
@[|/*decoratorUse*/decorator|]
class C {
    @[|decora/*decoratorFactoryUse*/torFactory|](a, "22", true)
    method() {}
}
// @Filename: a.ts
function /*decoratorDefinition*/decorator(target) {
    return target;
}
function /*decoratorFactoryDefinition*/decoratorFactory(...args) {
    return target => target;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"decoratorUse", "decoratorFactoryUse"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionDecorator, TestGoToDefinitionDecorator);


static void TestGoToDefinitionDecoratorOverloads(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Target: ES6
// @experimentaldecorators: true
async function f() {}

function /*defDecString*/dec(target: any, propertyKey: string): void;
function /*defDecSymbol*/dec(target: any, propertyKey: symbol): void;
function dec(target: any, propertyKey: string | symbol) {}

declare const s: symbol;
class C {
    @[|/*useDecString*/dec|] f() {}
    @[|/*useDecSymbol*/dec|] [s]() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"useDecString", "useDecSymbol"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionDecoratorOverloads, TestGoToDefinitionDecoratorOverloads);


static void TestGoToDefinitionConstructorOverloads(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class ConstructorOverload {
    [|/*constructorOverload1*/constructor|]();
    /*constructorOverload2*/constructor(foo: string);
    /*constructorDefinition*/constructor(foo: any)  { }
}

var constructorOverload = new [|/*constructorOverloadReference1*/ConstructorOverload|]();
var constructorOverload = new [|/*constructorOverloadReference2*/ConstructorOverload|]("foo");

class Extended extends ConstructorOverload {
    readonly name = "extended";
}
var extended1 = new [|/*extendedRef1*/Extended|]();
var extended2 = new [|/*extendedRef2*/Extended|]("foo");)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"constructorOverloadReference1", "constructorOverloadReference2", "constructorOverload1", "extendedRef1", "extendedRef2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionConstructorOverloads, TestGoToDefinitionConstructorOverloads);


static void TestGoToDefinitionConstructorOfClassWhenClassIsPrecededByNamespace01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Foo {
    export var x;
}

class Foo {
    /*definition*/constructor() {
    }
}

var x = new [|/*usage*/Foo|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionConstructorOfClassWhenClassIsPrecededByNamespace01, TestGoToDefinitionConstructorOfClassWhenClassIsPrecededByNamespace01);


static void TestGoToDefinitionConstructorOfClassExpression01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = class C {
    /*definition*/constructor() {
        var other = new [|/*xusage*/C|];
    }
}

var y = class C extends x {
    constructor() {
        super();
        var other = new [|/*yusage*/C|];
    }
}
var z = class C extends x {
    m() {
        return new [|/*zusage*/C|];
    }
}

var x1 = new [|/*cref*/C|]();
var x2 = new [|/*xref*/x|]();
var y1 = new [|/*yref*/y|]();
var z1 = new [|/*zref*/z|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"xusage", "yusage", "zusage", "cref", "xref", "yref", "zref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionConstructorOfClassExpression01, TestGoToDefinitionConstructorOfClassExpression01);


static void TestGoToDefinitionClassStaticBlocks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class ClassStaticBocks {
    static x;
    [|/*classStaticBocks1*/static|] {}
    static y;
    [|/*classStaticBocks2*/static|] {}
    static y;
    [|/*classStaticBocks3*/static|] {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"classStaticBocks1", "classStaticBocks2", "classStaticBocks3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionClassStaticBlocks, TestGoToDefinitionClassStaticBlocks);


static void TestGoToDefinitionClassConstructors(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: definitions.ts
export class Base {
    constructor(protected readonly cArg: string) {}
}

export class Derived extends Base {
    readonly email = this.cArg.getByLabel('Email')
    readonly password =  this.cArg.getByLabel('Password')
}
// @filename: main.ts
import { Derived } from './definitions'
const derived = new [|/*Derived*/Derived|](cArg)
// @filename: defInSameFile.ts
import { Base } from './definitions'
class SameFile extends Base {
    readonly name: string = 'SameFile'
}
const SameFile = new [|/*SameFile*/SameFile|](cArg)
const wrapper = new [|/*Base*/Base|](cArg)
// @filename: hasConstructor.ts
import { Base } from './definitions'
class HasConstructor extends Base {
    constructor() {}
    readonly name: string = '';
}
const hasConstructor = new [|/*HasConstructor*/HasConstructor|](cArg))TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"Derived", "SameFile", "HasConstructor", "Base"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionClassConstructors, TestGoToDefinitionClassConstructors);


static void TestGoToDefinitionCSSPatternAmbientModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @esModuleInterop: true
// @Filename: index.css
/*2a*/html { font-size: 16px; }
// @Filename: types.ts
declare module /*2b*/"*.css" {
  const styles: any;
  export = styles;
}
// @Filename: index.ts
import styles from [|/*1*/"./index.css"|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionCSSPatternAmbientModule, TestGoToDefinitionCSSPatternAmbientModule);


static void TestGoToDefinitionBuiltInValues(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var u = /*undefined*/undefined;
var n = /*null*/null;
var a = function() { return /*arguments*/arguments; };
var t = /*true*/true;
var f = /*false*/false;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, f->MarkerNames());
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionBuiltInValues, TestGoToDefinitionBuiltInValues);


static void TestGoToDefinitionAwait4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(async function outerAsyncFun() {
    let /*end*/af = async () => {
      [|/*start*/await|] Promise.resolve(0);
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionAwait4, TestGoToDefinitionAwait4);


static void TestGoToDefinitionAwait3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    notAsync() {
      [|/*start1*/await|] Promise.resolve(0);
    }

    async /*end2*/foo() {
      [|/*start2*/await|] Promise.resolve(0);
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start1", "start2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionAwait3, TestGoToDefinitionAwait3);


static void TestGoToDefinitionAwait2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = "[|/*start*/await|] Promise.resolve(0);";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionAwait2, TestGoToDefinitionAwait2);


static void TestGoToDefinitionAwait1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(async function /*end1*/foo() {
    [|/*start1*/await|] Promise.resolve(0);
}
function notAsync() {
    [|/*start2*/await|] Promise.resolve(0);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"start1", "start2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionAwait1, TestGoToDefinitionAwait1);


static void TestGoToDefinitionApparentTypeProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Number {
    /*definition*/myObjectMethod(): number;
}

var o = 0;
o.[|/*reference1*/myObjectMethod|]();
o[[|"/*reference2*/myObjectMethod"|]]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"reference1", "reference2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionApparentTypeProperties, TestGoToDefinitionApparentTypeProperties);


static void TestGoToDefinitionAcrossMultipleProjects(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: a.ts
var /*def1*/x: number;
//@Filename: b.ts
var /*def2*/x: number;
//@Filename: c.ts
var /*def3*/x: number;
//@Filename: d.ts
var /*def4*/x: number;
//@Filename: e.ts
/// <reference path="a.ts" />
/// <reference path="b.ts" />
/// <reference path="c.ts" />
/// <reference path="d.ts" />
[|/*use*/x|]++;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionAcrossMultipleProjects, TestGoToDefinitionAcrossMultipleProjects);
} // namespace
