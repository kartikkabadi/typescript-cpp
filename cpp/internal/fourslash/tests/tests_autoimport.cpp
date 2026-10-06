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

// autoImport_node12_node_modules1_test.go
static void TestAutoImport_node12_node_modules1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @lib: es5
// @module: node16
// @Filename: /node_modules/undici/index.d.ts
export function request(): any;
// @Filename: /index.mts
request/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"undici"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImport_node12_node_modules1, TestAutoImport_node12_node_modules1);

// autoImportTypedefMissingName_test.go
static void TestAutoImportTypedefMissingName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /utils.js
/** @typedef {{ x: number }} */

export function doSomething() {}
// @Filename: /index.ts
doSomething/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->BaselineAutoImportsCompletions(t, {std::vector<std::string>{""}});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportTypedefMissingName, TestAutoImportTypedefMissingName);

// autoImportPackageJsonImportsLength1_test.go
static void TestAutoImportPackageJsonImportsLength1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#*": "./src/*.ts"
  }
}
// @Filename: /src/a/b/c/something.ts
export function something(name: string): any;
// @Filename: /src/a/b/c/d.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./something"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsLength1, TestAutoImportPackageJsonImportsLength1);

// autoImportPackageJsonImportsPattern_test.go
static void TestAutoImportPackageJsonImportsPattern(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#*": "./src/*"
  }
}
// @Filename: /src/something.ts
export function something(name: string): any;
// @Filename: /a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#something.js"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsPattern, TestAutoImportPackageJsonImportsPattern);

// autoImportPackageJsonImports_capsInPath1_test.go
static void TestAutoImportPackageJsonImports_capsInPath1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: node18
// @Filename: /Dev/package.json
{
  "imports": {
    "#thing": "./src/something.js"
  }
}
// @Filename: /Dev/src/something.ts
export function something(name: string): any;
// @Filename: /Dev/a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#thing"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImports_capsInPath1, TestAutoImportPackageJsonImports_capsInPath1);

// completionsImport_fromAmbientModule_test.go
static void TestCompletionsImport_fromAmbientModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: esnext
// @Filename: /a.ts
declare module "m" {
    export const x: number;
}
// @Filename: /b.ts
/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "x", .Source = "m", .Description = "Add import from \"m\"", .NewFileContent = std::make_shared<std::string>(R"TS(import { x } from "m";

)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsImport_fromAmbientModule, TestCompletionsImport_fromAmbientModule);

// completionsImportYieldExpression_test.go
static void TestCompletionsImportYieldExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
export function a() {}
// @Filename: /b.ts
function *f() {
  yield a/**/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "a", .Source = "./a", .Description = "Add import from \"./a\"", .NewFileContent = std::make_shared<std::string>(R"TS(import { a } from "./a";

function *f() {
  yield a
})TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsImportYieldExpression, TestCompletionsImportYieldExpression);

// completionsImport_noSemicolons_test.go
static void TestCompletionsImport_noSemicolons(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"});
		const std::string content = R"TS(// @Filename: /a.ts
export function foo() {}
// @Filename: /b.ts
const x = 0
const y = 1
const z = fo/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "foo", .Source = "./a", .Description = "Add import from \"./a\"", .NewFileContent = std::make_shared<std::string>(R"TS(import { foo } from "./a"

const x = 0
const y = 1
const z = fo)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsImport_noSemicolons, TestCompletionsImport_noSemicolons);

// importFixWithMultipleModuleExportAssignment_test.go
static void TestImportFixWithMultipleModuleExportAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: esnext
// @allowJs: true
// @checkJs: true
// @Filename: /a.js
function f() {}
module.exports = f;
module.exports = 42;
// @Filename: /b.js
export const foo = 0;
// @Filename: /c.js
foo)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/c.js");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(const { foo } = require("./b");

foo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixWithMultipleModuleExportAssignment, TestImportFixWithMultipleModuleExportAssignment);

// importFixes_ambientCircularDefaultCrash_test.go
static void TestImportFixes_ambientCircularDefaultCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "preserve",
    "lib": ["es5"]
  }
}
// @Filename: /home/src/workspaces/project/types.d.ts
declare module "mymod" {
  import mymod from "mymod";
  export default mymod;
}
// @Filename: /home/src/workspaces/project/index.ts
my/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixes_ambientCircularDefaultCrash, TestImportFixes_ambientCircularDefaultCrash);

}  // namespace
