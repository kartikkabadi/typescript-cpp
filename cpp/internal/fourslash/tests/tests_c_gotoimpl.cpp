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

[[maybe_unused]] static std::pair<std::shared_ptr<fourslash::FourslashTest>, std::function<void()>> newContentMapperFourslash(gostd::testing::T* t, std::string content, std::string mapper, const std::vector<std::string>& extensions) {
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

// goToImplementationClassMethod_00_test.go

// goToImplementationClassMethod_00_test.go
static void TestGoToImplementationClassMethod_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Bar {
    [|{|"parts": ["(","method",")"," ","Bar",".","hello","(",")",":"," ","void"], "kind": "method"|}hello|]() {}
}

new Bar().hel/*reference*/lo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationClassMethod_00, TestGoToImplementationClassMethod_00);

// goToImplementationClassMethod_01_test.go

// goToImplementationClassMethod_01_test.go
static void TestGoToImplementationClassMethod_01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(abstract class AbstractBar {
    abstract he/*declaration*/llo(): void;
}

class Bar extends AbstractBar{
    [|hello|]() {}
}

function whatever(x: AbstractBar) {
    x.he/*reference*/llo();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference", "declaration"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationClassMethod_01, TestGoToImplementationClassMethod_01);

// goToImplementationEnum_00_test.go

// goToImplementationEnum_00_test.go
static void TestGoToImplementationEnum_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum Foo {
    [|Foo1|] = function initializer() { return 5 } (),
    Foo2 = 6
}

Foo.Fo/*reference*/o1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationEnum_00, TestGoToImplementationEnum_00);

// goToImplementationEnum_01_test.go

// goToImplementationEnum_01_test.go
static void TestGoToImplementationEnum_01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum [|Foo|] {
    Foo1 = function initializer() { return 5 } (),
    Foo2 = 6
}

Fo/*reference*/o;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationEnum_01, TestGoToImplementationEnum_01);

// goToImplementationInterfaceMethod_00_test.go

// goToImplementationInterfaceMethod_00_test.go
static void TestGoToImplementationInterfaceMethod_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    he/*declaration*/llo: () => void
}

var bar: Foo = { [|hello|]: helloImpl };
var baz: Foo = { "[|hello|]": helloImpl };

function helloImpl () {}

function whatever(x: Foo = { [|hello|]() {/**1*/} }) {
    x.he/*function_call*/llo()
}

class Bar {
    x: Foo = { [|hello|]() {/*2*/} }

    constructor(public f: Foo = { [|hello|]() {/**3*/} } ) {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call", "declaration"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_00, TestGoToImplementationInterfaceMethod_00);

// goToImplementationInterfaceMethod_01_test.go

// goToImplementationInterfaceMethod_01_test.go
static void TestGoToImplementationInterfaceMethod_01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    hel/*declaration*/lo(): void;
    okay?: number;
}

class Bar implements Foo {
    [|hello|]() {}
    public sure() {}
}

function whatever(a: Foo) {
    a.he/*function_call*/llo();
}

whatever(new Bar());)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call", "declaration"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_01, TestGoToImplementationInterfaceMethod_01);

// goToImplementationInterfaceMethod_02_test.go

// goToImplementationInterfaceMethod_02_test.go
static void TestGoToImplementationInterfaceMethod_02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    he/*declaration*/llo(): void
}

abstract class AbstractBar implements Foo {
    abstract hello(): void;
}

class Bar extends AbstractBar {
    [|hello|]() {}
}

function whatever(a: AbstractBar) {
    a.he/*function_call*/llo();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call", "declaration"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_02, TestGoToImplementationInterfaceMethod_02);

// goToImplementationInterfaceMethod_03_test.go

// goToImplementationInterfaceMethod_03_test.go
static void TestGoToImplementationInterfaceMethod_03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    hello (): void;
}

class Bar extends SuperBar {
    [|hello|]() {}
}

class SuperBar implements Foo {
    hello() {} // should not show up
}

class OtherBar implements Foo {
    hello() {} // should not show up
}

new Bar().hel/*function_call*/lo();
new Bar()["hello"]();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_03, TestGoToImplementationInterfaceMethod_03);

// goToImplementationInterfaceMethod_04_test.go

// goToImplementationInterfaceMethod_04_test.go
static void TestGoToImplementationInterfaceMethod_04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    hello (): void;
}

class Bar extends SuperBar {
    [|hello|]() {}
}

class SuperBar implements Foo {
    [|hello|]() {}
}

class OtherBar implements Foo {
    hello() {} // should not show up
}

function (x: SuperBar) {
    x.he/*function_call*/llo()
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_04, TestGoToImplementationInterfaceMethod_04);

// goToImplementationInterfaceMethod_05_test.go

// goToImplementationInterfaceMethod_05_test.go
static void TestGoToImplementationInterfaceMethod_05(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    hello (): void;
}

class SuperBar implements Foo {
    [|hello|]() {}
}

class Bar extends SuperBar {
    hello2() {}
}

class OtherBar extends SuperBar {
    hello() {}
    hello2() {}
    hello3() {}
}

class NotRelatedToBar {
    hello() {}         // Equivalent to last case, but shares no common ancestors with Bar and so is not returned
    hello2() {}
    hello3() {}
}

class NotBar extends SuperBar {
    hello() {}         // Should not be returned because it is not structurally equivalent to Bar
}

function whatever(x: Bar) {
    x.he/*function_call*/llo()
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_05, TestGoToImplementationInterfaceMethod_05);

// goToImplementationInterfaceMethod_06_test.go

// goToImplementationInterfaceMethod_06_test.go
static void TestGoToImplementationInterfaceMethod_06(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface SuperFoo {
    hello (): void;
}

interface Foo extends SuperFoo {
    someOtherFunction(): void;
}

class Bar implements Foo {
     [|hello|]() {}
     someOtherFunction() {}
}

function createFoo(): Foo {
    return {
        [|hello|]() {},
        someOtherFunction() {}
    };
}

var y: Foo = {
    [|hello|]() {},
    someOtherFunction() {}
};

class FooLike implements SuperFoo {
     hello() {}
     someOtherFunction() {}
}

class NotRelatedToFoo {
     hello() {}                // This case is equivalent to the last case, but is not returned because it does not share a common ancestor with Foo
     someOtherFunction() {}
}

class NotFoo implements SuperFoo {
     hello() {}                // We only want implementations of Foo, even though the function is declared in SuperFoo
}

function (x: Foo) {
    x.he/*function_call*/llo()
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_06, TestGoToImplementationInterfaceMethod_06);

// goToImplementationInterfaceMethod_08_test.go

// goToImplementationInterfaceMethod_08_test.go
static void TestGoToImplementationInterfaceMethod_08(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    hello (): void;
}

class SuperBar implements Foo {
   [|hello|]() {}
}

class Bar extends SuperBar {
   whatever() { this.he/*function_call*/llo(); }
}

class SubBar extends Bar {
   [|hello|]() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_08, TestGoToImplementationInterfaceMethod_08);

// goToImplementationInterfaceMethod_09_test.go

// goToImplementationInterfaceMethod_09_test.go
static void TestGoToImplementationInterfaceMethod_09(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    hello (): void;
}

class SubBar extends Bar {
    hello() {}
}

class Bar extends SuperBar {
    hello() {}

    whatever() {
        super.he/*function_call*/llo();
        super["hel/*element_access*/lo"]();
    }
}

class SuperBar extends MegaBar {
    [|hello|]() {}
}

class MegaBar implements Foo {
    hello() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call", "element_access"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_09, TestGoToImplementationInterfaceMethod_09);

// goToImplementationInterfaceMethod_10_test.go

// goToImplementationInterfaceMethod_10_test.go
static void TestGoToImplementationInterfaceMethod_10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface BaseFoo {
	 hello(): void;
}

interface Foo extends BaseFoo {
	 aloha(): void;
}

interface Bar {
 	 hello(): void;
 	 goodbye(): void;
}

class FooImpl implements Foo {
 	 [|hello|]() {/**FooImpl*/}
 	 aloha() {}
}

class BaseFooImpl implements BaseFoo {
 	 hello() {/**BaseFooImpl*/}    // Should not show up
}

class BarImpl implements Bar {
	 [|hello|]() {/**BarImpl*/}
	 goodbye() {}
}

class FooAndBarImpl implements Foo, Bar {
	 [|hello|]() {/**FooAndBarImpl*/}
	 aloha() {}
	 goodbye() {}
}

function someFunction(x: Foo | Bar) {
	 x.he/*function_call0*/llo();
}

function anotherFunction(x: Foo & Bar) {
	 x.he/*function_call1*/llo();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call0", "function_call1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_10, TestGoToImplementationInterfaceMethod_10);

// goToImplementationInterfaceMethod_11_test.go

// goToImplementationInterfaceMethod_11_test.go
static void TestGoToImplementationInterfaceMethod_11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
   hel/*reference*/lo(): void;
}

var x = <Foo> { [|hello|]: () => {} };
var y = <Foo> (((({ [|hello|]: () => {} }))));)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceMethod_11, TestGoToImplementationInterfaceMethod_11);

// goToImplementationInterfaceObjectLiteral_test.go

// goToImplementationInterfaceObjectLiteral_test.go
static void TestGoToImplementationInterfaceObjectLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /file1.ts
export interface MyInterface { P: number; }

// @Filename: /file2.ts
import { MyInterface } from "./file1";

const x: /*impl*/MyInterface = { P: 2 };
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"impl"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceObjectLiteral, TestGoToImplementationInterfaceObjectLiteral);

// goToImplementationInterfaceProperty_00_test.go

// goToImplementationInterfaceProperty_00_test.go
static void TestGoToImplementationInterfaceProperty_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    hello: number
}

var bar: Foo = { [|hello|]: 5 };

function whatever(x: Foo = { [|hello|]: 5 * 9 }) {
    x.he/*reference*/llo
}

class Bar {
    x: Foo = { [|hello|]: 6 }

    constructor(public f: Foo = { [|hello|]: 7 } ) {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceProperty_00, TestGoToImplementationInterfaceProperty_00);

// goToImplementationInterfaceProperty_01_test.go

// goToImplementationInterfaceProperty_01_test.go
static void TestGoToImplementationInterfaceProperty_01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo { hello: number }

class Bar implements Foo {
    [|hello|] = 5 * 9;
}

function whatever(foo: Foo) {
    foo.he/*reference*/llo;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterfaceProperty_01, TestGoToImplementationInterfaceProperty_01);

// goToImplementationInterface_00_test.go

// goToImplementationInterface_00_test.go
static void TestGoToImplementationInterface_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Fo/*interface_definition*/o {
    hello: () => void
}

interface Baz extends Foo {}

var bar: Foo = [|{|"parts": ["(","object literal",")"], "kind": "interface"|}{ hello: helloImpl /**0*/ }|];
var baz: Foo[] = [|[{ hello: helloImpl /**4*/ }]|];

function helloImpl () {}

function whatever(x: Foo = [|{|"parts": ["(","object literal",")"], "kind": "interface"|}{ hello() {/**1*/} }|] ) {
}

class Bar {
    x: Foo = [|{ hello() {/*2*/} }|]

    constructor(public f: Foo = [|{ hello() {/**3*/} }|] ) {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"interface_definition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_00, TestGoToImplementationInterface_00);

// goToImplementationInterface_01_test.go

// goToImplementationInterface_01_test.go
static void TestGoToImplementationInterface_01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Fo/*interface_definition*/o { hello(): void }

class [|SuperBar|] implements Foo {
    hello () {}
}

abstract class [|AbstractBar|] implements Foo {
    abstract hello (): void;
}

class [|Bar|] extends SuperBar {
}

class [|NotAbstractBar|] extends AbstractBar {
    hello () {}
}

var x = new SuperBar();
var y: SuperBar = new SuperBar();
var z: AbstractBar = new NotAbstractBar();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"interface_definition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_01, TestGoToImplementationInterface_01);

// goToImplementationInterface_02_test.go

// goToImplementationInterface_02_test.go
static void TestGoToImplementationInterface_02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Fo/*interface_definition*/o { hello: () => void }

let x: number = 9;

function createFoo(): Foo {
    if (x === 2) {
        return [|{
            hello() {}
        }|];
    }
    return [|{
        hello() {}
    }|];
}

let createFoo2 = (): Foo => [|({hello() {}})|];

function createFooLike() {
    return {
        hello() {}
    };
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"interface_definition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_02, TestGoToImplementationInterface_02);

// goToImplementationInterface_03_test.go

// goToImplementationInterface_03_test.go
static void TestGoToImplementationInterface_03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Fo/*interface_definition*/o { hello: () => void }

var x = <Foo> [|{ hello: () => {} }|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"interface_definition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_03, TestGoToImplementationInterface_03);

// goToImplementationInterface_04_test.go

// goToImplementationInterface_04_test.go
static void TestGoToImplementationInterface_04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Fo/*interface_definition*/o {
    (a: number): void
}

var bar: Foo = [|(a) => {/**0*/}|];

function whatever(x: Foo = [|(a) => {/**1*/}|] ) {
}

class Bar {
    x: Foo = [|(a) => {/**2*/}|]

    constructor(public f: Foo = [|function(a) {}|] ) {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"interface_definition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_04, TestGoToImplementationInterface_04);

// goToImplementationInterface_05_test.go

// goToImplementationInterface_05_test.go
static void TestGoToImplementationInterface_05(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Fo/*interface_definition*/o {
    (a: number): void
}

let bar2 = <Foo> [|function(a) {}|];
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"interface_definition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_05, TestGoToImplementationInterface_05);

// goToImplementationInterface_06_test.go

// goToImplementationInterface_06_test.go
static void TestGoToImplementationInterface_06(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Fo/*interface_definition*/o {
    new (a: number): SomeOtherType;
}

interface SomeOtherType {}

let x: Foo = [|class { constructor (a: number) {} }|];
let y = <Foo> [|class { constructor (a: number) {} }|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"interface_definition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_06, TestGoToImplementationInterface_06);

// goToImplementationInterface_07_test.go

// goToImplementationInterface_07_test.go
static void TestGoToImplementationInterface_07(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Fo/*interface_definition*/o {
    hello (): void;
}

interface Bar {
    hello (): void;
}

let x1: Foo            = [|{ hello ()          { /**typeReference*/ } }|];
let x2: () => Foo      = [|(() => { hello ()   { /**functionType*/} })|];
let x3: Foo | Bar      = [|{ hello ()          { /**unionType*/} }|];
let x4: Foo & (Foo & Bar)      = [|{ hello ()          { /**intersectionType*/} }|];
let x5: [Foo]          = [|[{ hello ()         { /**tupleType*/} }]|];
let x6: (Foo)          = [|{ hello ()          { /**parenthesizedType*/} }|];
let x7: (new() => Foo) = [|class { hello ()    { /**constructorType*/} }|];
let x8: Foo[]          = [|[{ hello ()         { /**arrayType*/} }]|];
let x9: { y: Foo }     = [|{ y: { hello ()     { /**typeLiteral*/} } }|];
let x10 = [|{|"parts": ["(","anonymous local class",")"], "kind": "local class"|}class implements Foo { hello() {} }|]
let x11 = class [|{|"parts": ["(","local class",")"," ","C"], "kind": "local class"|}C|] implements Foo { hello() {} }

// Should not do anything for type predicates
function isFoo(a: any): a is Foo {
    return true;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"interface_definition"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_07, TestGoToImplementationInterface_07);

// goToImplementationInterface_08_test.go

// goToImplementationInterface_08_test.go
static void TestGoToImplementationInterface_08(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Base {
    hello (): void;
}

interface A extends Base {}
interface B extends C, A {}
interface C extends B, A {}

class X implements B {
    [|hello|]() {}
}

function someFunction(d : A) {
    d.he/*function_call*/llo();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_08, TestGoToImplementationInterface_08);

// goToImplementationInterface_09_test.go

// goToImplementationInterface_09_test.go
static void TestGoToImplementationInterface_09(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: def.d.ts
export interface Interface { P: number }
// @Filename: ref.ts
import { Interface } from "./def";
const c: I/*ref*/nterface = [|{ P: 2 }|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_09, TestGoToImplementationInterface_09);

// goToImplementationInterface_10_test.go

// goToImplementationInterface_10_test.go
static void TestGoToImplementationInterface_10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
interface /*def*/A {
	foo: boolean;
}
interface [|B|] extends A {
	bar: boolean;
}
export class [|C|] implements B {
	foo = true;
	bar = true;
}
export class [|D|] extends C { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"def"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInterface_10, TestGoToImplementationInterface_10);

// goToImplementationInvalid_test.go

// goToImplementationInvalid_test.go
static void TestGoToImplementationInvalid(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x1 = 50/*0*/0;
var x2 = "hel/*1*/lo";
/*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"0", "1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationInvalid, TestGoToImplementationInvalid);

// goToImplementationLocal_00_test.go

// goToImplementationLocal_00_test.go
static void TestGoToImplementationLocal_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(he/*function_call*/llo();
function [|hello|]() {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationLocal_00, TestGoToImplementationLocal_00);

// goToImplementationLocal_01_test.go

// goToImplementationLocal_01_test.go
static void TestGoToImplementationLocal_01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const [|hello|] = function() {};
he/*function_call*/llo();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationLocal_01, TestGoToImplementationLocal_01);

// goToImplementationLocal_02_test.go

// goToImplementationLocal_02_test.go
static void TestGoToImplementationLocal_02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const x = { [|hello|]: () => {} };

x.he/*function_call*/llo();
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationLocal_02, TestGoToImplementationLocal_02);

// goToImplementationLocal_03_test.go

// goToImplementationLocal_03_test.go
static void TestGoToImplementationLocal_03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let [|he/*local_var*/llo|] = {};

x.hello();

hello = {};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"local_var"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationLocal_03, TestGoToImplementationLocal_03);

// goToImplementationLocal_04_test.go

// goToImplementationLocal_04_test.go
static void TestGoToImplementationLocal_04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function [|he/*local_var*/llo|]() {}

hello();
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"local_var"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationLocal_04, TestGoToImplementationLocal_04);

// goToImplementationLocal_05_test.go

// goToImplementationLocal_05_test.go
static void TestGoToImplementationLocal_05(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Bar {
    public hello() {}
}

var [|someVar|] = new Bar();
someVa/*reference*/r.hello();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationLocal_05, TestGoToImplementationLocal_05);

// goToImplementationLocal_06_test.go

// goToImplementationLocal_06_test.go
static void TestGoToImplementationLocal_06(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare var [|someVar|]: string;
someVa/*reference*/r)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationLocal_06, TestGoToImplementationLocal_06);

// goToImplementationLocal_07_test.go

// goToImplementationLocal_07_test.go
static void TestGoToImplementationLocal_07(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function [|someFunction|](): () => void;
someFun/*reference*/ction();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationLocal_07, TestGoToImplementationLocal_07);

// goToImplementationLocal_08_test.go

// goToImplementationLocal_08_test.go
static void TestGoToImplementationLocal_08(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function [|someFunction|](): () => void;
someFun/*reference*/ction();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationLocal_08, TestGoToImplementationLocal_08);

// goToImplementationNamespace_00_test.go

// goToImplementationNamespace_00_test.go
static void TestGoToImplementationNamespace_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace /*implementation0*/Foo {
    export function hello() {}
}

module /*implementation1*/Bar {
    export function sure() {}
}

let x = Fo/*reference0*/o;
let y = Ba/*reference1*/r;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference0", "reference1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNamespace_00, TestGoToImplementationNamespace_00);

// goToImplementationNamespace_01_test.go

// goToImplementationNamespace_01_test.go
static void TestGoToImplementationNamespace_01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Foo {
    export function [|hello|]() {}
}

Foo.hell/*reference*/o();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNamespace_01, TestGoToImplementationNamespace_01);

// goToImplementationNamespace_02_test.go

// goToImplementationNamespace_02_test.go
static void TestGoToImplementationNamespace_02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Foo {
    export function [|hello|]() {}
}

Foo.hell/*reference*/o();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNamespace_02, TestGoToImplementationNamespace_02);

// goToImplementationNamespace_03_test.go

// goToImplementationNamespace_03_test.go
static void TestGoToImplementationNamespace_03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Foo {
    export interface Bar {
        hello(): void;
    }

    class [|BarImpl|] implements Bar {
        hello() {}
    }
}

class [|Baz|] implements Foo.Bar {
    hello() {}
}

var someVar1 : Foo.Bar = [|{ hello: () => {/**1*/} }|];

var someVar2 = <Foo.Bar> [|{ hello: () => {/**2*/} }|];

function whatever(x: Foo.Ba/*reference*/r) {

})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNamespace_03, TestGoToImplementationNamespace_03);

// goToImplementationNamespace_04_test.go

// goToImplementationNamespace_04_test.go
static void TestGoToImplementationNamespace_04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Foo {
    export interface Bar {
        hello(): void;
    }

    class [|BarImpl|] implements Bar {
        hello() {}
    }
}

class [|Baz|] implements Foo.Bar {
    hello() {}
}

var someVar1 : Foo.Bar = [|{ hello: () => {/**1*/} }|];

var someVar2 = <Foo.Bar> [|{ hello: () => {/**2*/} }|];

function whatever(x: Foo.Ba/*reference*/r) {

})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNamespace_04, TestGoToImplementationNamespace_04);

// goToImplementationNamespace_05_test.go

// goToImplementationNamespace_05_test.go
static void TestGoToImplementationNamespace_05(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace /*implementation0*/Foo./*implementation2*/Baz {
    export function hello() {}
}

module /*implementation1*/Bar./*implementation3*/Baz {
    export function sure() {}
}

let x = Fo/*reference0*/o;
let y = Ba/*reference1*/r;
let x1 = Foo.B/*reference2*/az;
let y1 = Bar.B/*reference3*/az;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference0", "reference1", "reference2", "reference3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNamespace_05, TestGoToImplementationNamespace_05);

// goToImplementationNamespace_06_test.go

// goToImplementationNamespace_06_test.go
static void TestGoToImplementationNamespace_06(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace [|F/*declaration*/oo|] {
    declare function hello(): void;
}

let x: typeof Foo = [|{ hello() {} }|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"declaration"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNamespace_06, TestGoToImplementationNamespace_06);

// goToImplementationNoCrashMultiSourceDts_test.go

// goToImplementationNoCrashMultiSourceDts_test.go
static void TestGoToImplementationNoCrashMultiSourceDts(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /a.ts
export {};
// @Filename: /b.ts
export {};
// @Filename: /combined.d.ts
export declare class Bar {
    method(): void;
}
//# sourceMappingURL=combined.d.ts.map
// @Filename: /combined.d.ts.map
{"version":3,"file":"combined.d.ts","sourceRoot":"","sources":["a.ts","b.ts"],"names":[],"mappings":";IAAA,OCAA;AAAA"}
// @Filename: /user.ts
import { Bar } from './combined';
declare const bar: Bar;
bar./*impl*/method();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"impl"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNoCrashMultiSourceDts, TestGoToImplementationNoCrashMultiSourceDts);

// goToImplementationNoCrashTripleSlashRef2_test.go

// goToImplementationNoCrashTripleSlashRef2_test.go
static void TestGoToImplementationNoCrashTripleSlashRef2(gostd::testing::T* t) {
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
		f->VerifyBaselineGoToImplementation(t, {"m"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNoCrashTripleSlashRef2, TestGoToImplementationNoCrashTripleSlashRef2);

// goToImplementationNoCrashTripleSlashRef_test.go

// goToImplementationNoCrashTripleSlashRef_test.go
static void TestGoToImplementationNoCrashTripleSlashRef(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/@types/mymod/index.d.ts
export declare function foo(): void;
// @Filename: /main.d.ts
/// <reference types="/*m*/mymod" />)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"m"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNoCrashTripleSlashRef, TestGoToImplementationNoCrashTripleSlashRef);

// goToImplementationNoCrashUMDWithDynamicImport_test.go

// goToImplementationNoCrashUMDWithDynamicImport_test.go
static void TestGoToImplementationNoCrashUMDWithDynamicImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /lib.d.ts
export as namespace Lib;
export interface /*1*/IFoo {}
// @Filename: /user.ts
const p = import('./lib');)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationNoCrashUMDWithDynamicImport, TestGoToImplementationNoCrashUMDWithDynamicImport);

// goToImplementationReachingNonExistentExport1_test.go

// goToImplementationReachingNonExistentExport1_test.go
static void TestGoToImplementationReachingNonExistentExport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /github.ts
export { transformRecordedData };

// @Filename: /gitGateway.ts
import { transformRecordedData as transformGitHub } from './github';

const methods = { github: {
    transformData: /*impl*/transformGitHub,
}};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"impl"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationReachingNonExistentExport1, TestGoToImplementationReachingNonExistentExport1);

// goToImplementationReachingNonExistentExport2_test.go

// goToImplementationReachingNonExistentExport2_test.go
static void TestGoToImplementationReachingNonExistentExport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @allowJs: true
// @checkJs: true

// @Filename: /github.js
module.exports = { transformRecordedData };

// @Filename: /gitGateway.js
const { transformRecordedData: transformGitHub } = require('./github');

const methods = { github: {
    transformData: /*impl*/transformGitHub,
}};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"impl"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationReachingNonExistentExport2, TestGoToImplementationReachingNonExistentExport2);

// goToImplementationReachingNonExistentExport3_test.go

// goToImplementationReachingNonExistentExport3_test.go
static void TestGoToImplementationReachingNonExistentExport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @allowJs: true
// @checkJs: true

// @Filename: /github.js
export { transformRecordedData };

// @Filename: /gitGateway.js
import { transformRecordedData as transformGitHub } from './github';

const methods = { github: {
    transformData: /*impl*/transformGitHub,
}};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"impl"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationReachingNonExistentExport3, TestGoToImplementationReachingNonExistentExport3);

// goToImplementationReexportedTypeOnlyNamespace1_test.go

// goToImplementationReexportedTypeOnlyNamespace1_test.go
static void TestGoToImplementationReexportedTypeOnlyNamespace1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /node_modules/@typescript-eslint/types/index.d.ts
export * as TSESTree from './generated/ast-spec';

// @Filename: /node_modules/@typescript-eslint/types/generated/ast-spec.d.ts
export interface BaseNode {}

// @Filename: /node_modules/@typescript-eslint/utils/index.d.ts
export { TSESTree } from '@typescript-eslint/types';

// @Filename: /src/check-license.ts
import type {TSE/*impl*/STree} from '@typescript-eslint/utils';

let node: TSESTree.Node | undefined;
export default node;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"impl"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationReexportedTypeOnlyNamespace1, TestGoToImplementationReexportedTypeOnlyNamespace1);

// goToImplementationReexportedTypeOnlyNamespace2_test.go

// goToImplementationReexportedTypeOnlyNamespace2_test.go
static void TestGoToImplementationReexportedTypeOnlyNamespace2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /node_modules/@typescript-eslint/types/index.d.ts
export type * as TSESTree from './generated/ast-spec';

// @Filename: /node_modules/@typescript-eslint/types/generated/ast-spec.d.ts
export interface BaseNode {}

// @Filename: /node_modules/@typescript-eslint/utils/index.d.ts
export { TSESTree } from '@typescript-eslint/types';

// @Filename: /src/check-license.ts
import type {TSE/*impl*/STree} from '@typescript-eslint/utils';

let node: TSESTree.Node | undefined;
export default node;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"impl"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationReexportedTypeOnlyNamespace2, TestGoToImplementationReexportedTypeOnlyNamespace2);

// goToImplementationReexportedTypeOnlyNamespace3_test.go

// goToImplementationReexportedTypeOnlyNamespace3_test.go
static void TestGoToImplementationReexportedTypeOnlyNamespace3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /node_modules/@typescript-eslint/types/index.d.ts
export * as TSESTree from './generated/ast-spec';
export type * as TSESTree from './generated/ast-spec';

// @Filename: /node_modules/@typescript-eslint/types/generated/ast-spec.d.ts
export interface BaseNode {}

// @Filename: /node_modules/@typescript-eslint/utils/index.d.ts
export { TSESTree } from '@typescript-eslint/types';

// @Filename: /src/check-license.ts
import type {TSE/*impl*/STree} from '@typescript-eslint/utils';

let node: TSESTree.Node | undefined;
export default node;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"impl"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationReexportedTypeOnlyNamespace3, TestGoToImplementationReexportedTypeOnlyNamespace3);

// goToImplementationShorthandPropertyAssignment_00_test.go

// goToImplementationShorthandPropertyAssignment_00_test.go
static void TestGoToImplementationShorthandPropertyAssignment_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    someFunction(): void;
}

interface FooConstructor {
    new (): Foo
}

interface Bar {
    Foo: FooConstructor;
}

var x = class /*classExpression*/Foo {
    createBarInClassExpression(): Bar {
        return {
            Fo/*classExpressionRef*/o
        };
    }

    someFunction() {}
}

class /*declaredClass*/Foo {

}

function createBarUsingClassDeclaration(): Bar {
    return {
        Fo/*declaredClassRef*/o
    };
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"classExpressionRef", "declaredClassRef"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationShorthandPropertyAssignment_00, TestGoToImplementationShorthandPropertyAssignment_00);

// goToImplementationShorthandPropertyAssignment_01_test.go

// goToImplementationShorthandPropertyAssignment_01_test.go
static void TestGoToImplementationShorthandPropertyAssignment_01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    someFunction(): void;
}

interface FooConstructor {
    new (): Foo
}

interface Bar {
    Foo: FooConstructor;
}

// Class expression that gets used in a bar implementation
var x = class [|Foo|] {
    createBarInClassExpression(): Bar {
        return {
            Foo
        };
    }

    someFunction() {}
};

// Class declaration that gets used in a bar implementation. This class has multiple definitions
// (the class declaration and the interface above), but we only want the class returned
class [|Foo|] {

}

function createBarUsingClassDeclaration(): Bar {
    return {
        Foo
    };
}

// Class expression that does not get used in a bar implementation
var y = class Foo {
    someFunction() {}
};

createBarUsingClassDeclaration().Fo/*reference*/o;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"reference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationShorthandPropertyAssignment_01, TestGoToImplementationShorthandPropertyAssignment_01);

// goToImplementationShorthandPropertyAssignment_02_test.go

// goToImplementationShorthandPropertyAssignment_02_test.go
static void TestGoToImplementationShorthandPropertyAssignment_02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
	 hello(): void;
}

function createFoo(): Foo {
    return {
         hello
    };

    function [|hello|]() {}
}

function whatever(x: Foo) {
     x.h/*function_call*/ello();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"function_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationShorthandPropertyAssignment_02, TestGoToImplementationShorthandPropertyAssignment_02);

// goToImplementationSuper_00_test.go

// goToImplementationSuper_00_test.go
static void TestGoToImplementationSuper_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class [|Foo|] {
    constructor() {}
}

class Bar extends Foo {
    constructor() {
        su/*super_call*/per();
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"super_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationSuper_00, TestGoToImplementationSuper_00);

// goToImplementationSuper_01_test.go

// goToImplementationSuper_01_test.go
static void TestGoToImplementationSuper_01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class [|Foo|] {
    hello() {}
}

class Bar extends Foo {
    hello() {
        sup/*super_call*/er.hello();
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"super_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationSuper_01, TestGoToImplementationSuper_01);

// goToImplementationThis_00_test.go

// goToImplementationThis_00_test.go
static void TestGoToImplementationThis_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class [|Bar|] extends Foo {
    hello() {
        thi/*this_call*/s.whatever();
    }

    whatever() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"this_call"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationThis_00, TestGoToImplementationThis_00);

// goToImplementationThis_01_test.go

// goToImplementationThis_01_test.go
static void TestGoToImplementationThis_01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class [|Bar|] extends Foo {
    hello(): th/*this_type*/is {
        return this;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"this_type"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationThis_01, TestGoToImplementationThis_01);

// goToImplementationTypeAlias_00_test.go

// goToImplementationTypeAlias_00_test.go
static void TestGoToImplementationTypeAlias_00(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: def.d.ts
export type TypeAlias = { P: number }
// @Filename: ref.ts
import { TypeAlias } from "./def";
const c: T/*ref*/ypeAlias = [|{ P: 2 }|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"ref"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementationTypeAlias_00, TestGoToImplementationTypeAlias_00);

// goToImplementation_inDifferentFiles_test.go

// goToImplementation_inDifferentFiles_test.go
static void TestGoToImplementation_inDifferentFiles(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: /home/src/workspaces/project/bar.ts
import {Foo} from './foo'

class [|A|] implements Foo {
    func() {}
}

class [|B|] implements Foo {
    func() {}
}
// @Filename: /home/src/workspaces/project/foo.ts
export interface /**/Foo {
    func();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToImplementation(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementation_inDifferentFiles, TestGoToImplementation_inDifferentFiles);

// goToImplementation_satisfies_test.go

// goToImplementation_satisfies_test.go
static void TestGoToImplementation_satisfies(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /a.ts
interface /*def*/I {
	foo: string;
}

function f() {
    const foo = { foo: '' } satisfies [|I|];
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"def"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToImplementation_satisfies, TestGoToImplementation_satisfies);

} // namespace
