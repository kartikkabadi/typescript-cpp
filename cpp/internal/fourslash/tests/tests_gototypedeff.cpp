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

// goToTypeDefinition2_test.go

// goToTypeDefinition2_test.go
static void TestGoToTypeDefinition2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: goToTypeDefinition2_Definition.ts
interface /*definition*/I1 {
    p;
}
type propertyType = I1;
interface I2 {
    property: propertyType;
}
// @Filename: goToTypeDefinition2_Consumption.ts
var i2: I2;
i2.prop/*reference*/erty;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition2, TestGoToTypeDefinition2);

// goToTypeDefinition3_test.go

// goToTypeDefinition3_test.go
static void TestGoToTypeDefinition3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type /*definition*/T = string;
const x: /*reference*/T;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition3, TestGoToTypeDefinition3);

// goToTypeDefinition4_test.go

// goToTypeDefinition4_test.go
static void TestGoToTypeDefinition4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export type /*def0*/T = string;
export const /*def1*/T = "";
// @Filename: bar.ts
import { T } from "./foo";
let x: [|/*reference*/T|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference"});
		f->VerifyBaselineGoToDefinition(t, true, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition4, TestGoToTypeDefinition4);

// goToTypeDefinition5_test.go

// goToTypeDefinition5_test.go
static void TestGoToTypeDefinition5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
let Foo: /*definition*/unresolved;
type Foo = { x: string };
/*reference*/Foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition5, TestGoToTypeDefinition5);

// goToTypeDefinitionAliases_test.go

// goToTypeDefinitionAliases_test.go
static void TestGoToTypeDefinitionAliases(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: goToTypeDefinitioAliases_module1.ts
interface /*definition*/I {
    p;
}
export {I as I2};
// @Filename: goToTypeDefinitioAliases_module2.ts
import {I2 as I3} from "./goToTypeDefinitioAliases_module1";
var v1: I3;
export {v1 as v2};
// @Filename: goToTypeDefinitioAliases_module3.ts
import {/*reference1*/v2 as v3} from "./goToTypeDefinitioAliases_module2";
/*reference2*/v3;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference1", "reference2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinitionAliases, TestGoToTypeDefinitionAliases);

// goToTypeDefinitionEnumMembers_test.go

// goToTypeDefinitionEnumMembers_test.go
static void TestGoToTypeDefinitionEnumMembers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    value1,
    /*definition*/value2
}
var x = E.value2;

/*reference*/x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinitionEnumMembers, TestGoToTypeDefinitionEnumMembers);

// goToTypeDefinitionImportMeta_test.go

// goToTypeDefinitionImportMeta_test.go
static void TestGoToTypeDefinitionImportMeta(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @module: esnext
// @Filename: foo.ts
/// <reference path='./bar.d.ts' />
import.me/*reference*/ta;
//@Filename: bar.d.ts
interface /*definition*/ImportMeta {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinitionImportMeta, TestGoToTypeDefinitionImportMeta);

// goToTypeDefinitionModifiers_test.go

// goToTypeDefinitionModifiers_test.go
static void TestGoToTypeDefinitionModifiers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: /a.ts
/*export*/export class A/*A*/ {

    /*private*/private z/*z*/: string;

    /*private2*/private y/*y*/: A;

    /*readonly*/readonly x/*x*/: string;

    /*async*/async a/*a*/() {  }

    /*override*/override b/*b*/() {}

    /*public1*/public/*public2*/ as/*multipleModifiers*/ync c/*c*/() { }
}

exp/*exportFunction*/ort function foo/*foo*/() { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"export", "A", "private", "z", "private2", "y", "readonly", "x", "async", "a", "override", "b", "public1", "public2", "multipleModifiers", "c", "exportFunction", "foo"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinitionModifiers, TestGoToTypeDefinitionModifiers);

// goToTypeDefinitionModule_test.go

// goToTypeDefinitionModule_test.go
static void TestGoToTypeDefinitionModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: module1.ts
module /*definition*/M {
    export var p;
}
var m: typeof M;
// @Filename: module3.ts
/*reference1*/M;
/*reference2*/m;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference1", "reference2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinitionModule, TestGoToTypeDefinitionModule);

// goToTypeDefinitionPrimitives_test.go

// goToTypeDefinitionPrimitives_test.go
static void TestGoToTypeDefinitionPrimitives(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: module1.ts
var w: {a: number};
var x = "string";
var y: number | string;
var z; // any
// @Filename: module2.ts
w./*reference1*/a;
/*reference2*/x;
/*reference3*/y;
/*reference4*/y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference1", "reference2", "reference3", "reference4"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinitionPrimitives, TestGoToTypeDefinitionPrimitives);

// goToTypeDefinitionUnionType_test.go

// goToTypeDefinitionUnionType_test.go
static void TestGoToTypeDefinitionUnionType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class /*definition0*/C {
    p;
}

interface /*definition1*/I {
    x;
}

namespace M {
    export interface /*definition2*/I {
        y;
    }
}

var x: C | I | M.I;

/*reference*/x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinitionUnionType, TestGoToTypeDefinitionUnionType);

// goToTypeDefinition_Pick_test.go

// goToTypeDefinition_Pick_test.go
static void TestGoToTypeDefinition_Pick(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
type User = { id: number; name: string; };
declare const user: Pick<User, "name">
/*reference*/user

type PickedUser = Pick<User, "name">
declare const user2: PickedUser
/*reference2*/user2)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference", "reference2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition_Pick, TestGoToTypeDefinition_Pick);

// goToTypeDefinition_arrayType_test.go

// goToTypeDefinition_arrayType_test.go
static void TestGoToTypeDefinition_arrayType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
type User = { name: string };
declare const users: User[]
/*reference*/users

type UsersArr = Array<User>
declare const users2: UsersArr
/*reference2*/users2

class CustomArray<T> extends Array<T> { immutableReverse() { return [...this].reverse() } }
declare const users3: CustomArray<User>
/*reference3*/users3)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference", "reference2", "reference3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition_arrayType, TestGoToTypeDefinition_arrayType);

// goToTypeDefinition_promiseType_test.go

// goToTypeDefinition_promiseType_test.go
static void TestGoToTypeDefinition_promiseType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5,es2015.promise
type User = { name: string };
async function /*reference*/getUser() { return { name: "Bob" } satisfies User as User }

const /*reference2*/promisedBob = getUser() 

export {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference", "reference2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition_promiseType, TestGoToTypeDefinition_promiseType);

// goToTypeDefinition_returnType_test.go

// goToTypeDefinition_returnType_test.go
static void TestGoToTypeDefinition_returnType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface /*I*/I { x: number; }
interface /*J*/J { y: number; }

function f0(): I { return { x: 0 }; }

type T = /*T*/(i: I) => I;
const f1: T = i => ({ x: i.x + 1 });

const f2 = (i: I): I => ({ x: i.x + 1 });

const f3 = (i: I) => (/*f3Def*/{ x: i.x + 1 });

const f4 = (i: I) => i;

const f5 = /*f5Def*/(i: I): I | J => ({ x: i.x + 1 });

const f6 = (i: I, j: J, b: boolean) => b ? i : j;

const /*f7Def*/f7 = (i: I) => {};

function f8(i: I): I;
function f8(j: J): J;
function /*f8Def*/f8(ij: any): any { return ij; }

/*f0*/f0();
/*f1*/f1();
/*f2*/f2();
/*f3*/f3();
/*f4*/f4();
/*f5*/f5();
/*f6*/f6();
/*f7*/f7();
/*f8*/f8();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition_returnType, TestGoToTypeDefinition_returnType);

// goToTypeDefinition_test.go

// goToTypeDefinition_test.go
static void TestGoToTypeDefinition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: goToTypeDefinition_Definition.ts
class /*definition*/C {
    p;
}
var c: C;
// @Filename: goToTypeDefinition_Consumption.ts
/*reference*/c = undefined;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition, TestGoToTypeDefinition);

// goToTypeDefinition_typeReference_test.go

// goToTypeDefinition_typeReference_test.go
static void TestGoToTypeDefinition_typeReference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type User = { name: string };
type Box<T> = { value: T };
declare const boxedUser: Box<User>
/*reference*/boxedUser)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition_typeReference, TestGoToTypeDefinition_typeReference);

// goToTypeDefinition_typedef_test.go

// goToTypeDefinition_typedef_test.go
static void TestGoToTypeDefinition_typedef(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/**
 * /*def*/@typedef {object} I
 * @property {number} x
 */

/** @type {I} */
const /*ref*/i = { x: 0 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeDefinition_typedef, TestGoToTypeDefinition_typedef);

// goToTypeWithTupleTypes_test.go

// goToTypeWithTupleTypes_test.go
static void TestGoToTypeWithTupleTypes1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
export let x/*1*/: [number, number] = [1, 2];

type DoubleTupleTrouble<T> = [T, T];

export let y/*2*/: DoubleTupleTrouble<number> = [1, 2];
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {f->MarkerNames()});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToTypeWithTupleTypes1, TestGoToTypeWithTupleTypes1);

} // namespace
