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

// goToDefinitionAmbiants_test.go
static void TestGoToDefinitionAmbiants(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(declare var /*ambientVariableDefinition*/ambientVar;
declare function /*ambientFunctionDefinition*/ambientFunction();
declare class ambientClass {
    /*constructorDefinition*/constructor();
    static /*staticMethodDefinition*/method();
    public /*instanceMethodDefinition*/method();
}

/*ambientVariableReference*/ambientVar = 1;
/*ambientFunctionReference*/ambientFunction();
var ambientClassVariable = new /*constructorReference*/ambientClass();
ambientClass./*staticMethodReference*/method();
ambientClassVariable./*instanceMethodReference*/method();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"ambientVariableReference", "ambientFunctionReference", "constructorReference", "staticMethodReference", "instanceMethodReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionAmbiants, TestGoToDefinitionAmbiants);

// goToDefinitionAlias_test.go
static void TestGoToDefinitionAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: b.ts
import /*alias1Definition*/alias1 = require("fileb");
namespace Module {
    export import /*alias2Definition*/alias2 = alias1;
}

// Type position
var t1: [|/*alias1Type*/alias1|].IFoo;
var t2: Module.[|/*alias2Type*/alias2|].IFoo;

// Value posistion
var v1 = new [|/*alias1Value*/alias1|].Foo();
var v2 = new Module.[|/*alias2Value*/alias2|].Foo();
// @Filename: a.ts
export class Foo {
    private f;
}
export interface IFoo {
    x;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"alias1Type", "alias1Value", "alias2Type", "alias2Value"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionAlias, TestGoToDefinitionAlias);

// goToDefinitionExternalModuleName2_test.go
static void TestGoToDefinitionExternalModuleName2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: b.ts
import n = require([|'./a/*1*/'|]);
var x = new n.Foo();
// @Filename: a.ts
/*2*/class Foo {}
export var x = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionExternalModuleName2, TestGoToDefinitionExternalModuleName2);

// goToDefinitionDifferentFile_test.go
static void TestGoToDefinitionDifferentFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: goToDefinitionDifferentFile_Definition.ts
var /*remoteVariableDefinition*/remoteVariable;
function /*remoteFunctionDefinition*/remoteFunction() { }
class /*remoteClassDefinition*/remoteClass { }
interface /*remoteInterfaceDefinition*/remoteInterface{ }
module /*remoteModuleDefinition*/remoteModule{ export var foo = 1;}
// @Filename: goToDefinitionDifferentFile_Consumption.ts
/*remoteVariableReference*/remoteVariable = 1;
/*remoteFunctionReference*/remoteFunction();
var foo = new /*remoteClassReference*/remoteClass();
class fooCls implements /*remoteInterfaceReference*/remoteInterface { }
var fooVar = /*remoteModuleReference*/remoteModule.foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"remoteVariableReference", "remoteFunctionReference", "remoteClassReference", "remoteInterfaceReference", "remoteModuleReference"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionDifferentFile, TestGoToDefinitionDifferentFile);

// goToDefinitionBuiltInTypes_test.go
static void TestGoToDefinitionBuiltInTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(var n: /*number*/number;
var s: /*string*/string;
var b: /*boolean*/boolean;
var v: /*void*/void;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto markerNames = f->MarkerNames();
		f->VerifyBaselineGoToDefinition(t, true, std::vector<std::string>(markerNames.begin(), markerNames.end()));
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinitionBuiltInTypes, TestGoToDefinitionBuiltInTypes);

// definition01_test.go
static void TestDefinition01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @lib: es5
// @Filename: b.ts
import n = require([|'./a/*1*/'|]);
var x = new n.Foo();
// @Filename: a.ts
 /*2*/export class Foo {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToDefinition(t, true, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestDefinition01, TestDefinition01);

// definitionNameOnEnumMember_test.go
static void TestDefinitionNameOnEnumMember(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(enum e {
    firstMember,
    secondMember,
    thirdMember
}
var enumMember = e.[|/*1*/thirdMember|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestDefinitionNameOnEnumMember, TestDefinitionNameOnEnumMember);

}  // namespace
