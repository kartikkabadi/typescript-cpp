// Ported fourslash tests -- batch B (importfix). One static void TestX(gostd::testing::T*)
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
#include "internal/ls/ls.h"
#include "internal/testutil/testutil.h"

namespace {
using namespace tsc;
namespace tsu = tsc::fourslash::tests::util;

// importFixFromAtTypesWithRealPackage_test.go
static void TestImportFixFromAtTypesWithRealPackage(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Simulate a project where both `myLib` (JS-only package) and `@types/myLib` (type declarations) are installed.;
		// Another file already imports from `myLib` (resolving to @types/myLib).;
		// The import fix should suggest importing from "myLib", not "@types/myLib".;
		const std::string content = R"TS(// @Filename: /node_modules/myLib/package.json
{"name":"myLib","version":"1.0.0","main":"index.js"}
// @Filename: /node_modules/myLib/index.js
module.exports = {};
// @Filename: /node_modules/@types/myLib/package.json
{"name":"@types/myLib","version":"1.0.0","types":"index.d.ts"}
// @Filename: /node_modules/@types/myLib/index.d.ts
export function f1(): void;
export function f2(): void;
// @Filename: /package.json
{"dependencies":{"myLib":"*"}}
// @Filename: /other.ts
import { f1 } from "myLib";
f1();
// @Filename: /index.ts
[|f2/*0*/();|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "0", std::vector<std::string>{"myLib"}, nullptr );
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixFromAtTypesWithRealPackage, TestImportFixFromAtTypesWithRealPackage);

static void TestImportFixFromAtTypesWithRealPackageExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Like the above test, but the real package has an exports field pointing to JS files.;
		// This is the React 19 scenario: react has exports but no .d.ts, @types/react provides types.;
		const std::string content = R"TS(// @Filename: /node_modules/myLib/package.json
{"name":"myLib","version":"1.0.0","exports":{".":{"default":"./index.js"}}}
// @Filename: /node_modules/myLib/index.js
module.exports = {};
// @Filename: /node_modules/@types/myLib/package.json
{"name":"@types/myLib","version":"1.0.0","types":"index.d.ts"}
// @Filename: /node_modules/@types/myLib/index.d.ts
export function f1(): void;
export function f2(): void;
// @Filename: /package.json
{"dependencies":{"myLib":"*"}}
// @Filename: /other.ts
import { f1 } from "myLib";
f1();
// @Filename: /index.ts
[|f2/*0*/();|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "0", std::vector<std::string>{"myLib"}, nullptr );
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixFromAtTypesWithRealPackageExports, TestImportFixFromAtTypesWithRealPackageExports);

// importFixIndentedStatements_test.go
static void TestImportFixBeforeIndentedImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		auto __fsp = fourslash::NewFourslashWithOptions(t, R"TS(// @Filename: /aaa.ts
export const helper = 1;

// @Filename: /dep.ts
export const existing = 2;

// @Filename: /main.ts
// header
  import { existing } from "./dep";
  const value = help/**/;
)TS", tsu::ptr(fourslash::FourslashOptions{})); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{
		.Name =        "helper",
		.Source =      "./aaa",
		.Description = R"TS(Add import from "./aaa")TS",
		.NewFileContent =std::make_shared<std::string>(R"TS(// header
  import { helper } from "./aaa";
  import { existing } from "./dep";
  const value = help;
)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixBeforeIndentedImport, TestImportFixBeforeIndentedImport);

static void TestImportFixAfterIndentedImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		auto __fsp = fourslash::NewFourslashWithOptions(t, R"TS(// @Filename: /aaa.ts
export const existing = 2;

// @Filename: /zzz.ts
export const helper = 1;

// @Filename: /main.ts
// header
  import { existing } from "./aaa";
  const value = help/**/;
)TS", tsu::ptr(fourslash::FourslashOptions{})); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{
		.Name =        "helper",
		.Source =      "./zzz",
		.Description = R"TS(Add import from "./zzz")TS",
		.NewFileContent =std::make_shared<std::string>(R"TS(// header
  import { existing } from "./aaa";
  import { helper } from "./zzz";
  const value = help;
)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixAfterIndentedImport, TestImportFixAfterIndentedImport);

static void TestImportFixBeforeIndentedImportWithCarriageReturns(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		auto __fsp = fourslash::NewFourslashWithOptions(t, R"TS(// @Filename: /aaa.ts
export const helper = 1;

// @Filename: /dep.ts
export const existing = 2;

// @Filename: /main.ts
)TS"+std::string("// header\r  import { existing } from \"./dep\";\r  const value = help/**/;\r"), tsu::ptr(fourslash::FourslashOptions{})); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		// The inserted line is separated with the tracker's newline rather than the file's, which is a;
		// pre-existing behavior unrelated to indentation; what matters here is that both imports stay indented.;
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{
		.Name =           "helper",
		.Source =         "./aaa",
		.Description =    R"TS(Add import from "./aaa")TS",
		.NewFileContent =std::make_shared<std::string>("// header\r  import { helper } from \"./aaa\";\n  import { existing } from \"./dep\";\r  const value = help;\r")}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixBeforeIndentedImportWithCarriageReturns, TestImportFixBeforeIndentedImportWithCarriageReturns);

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
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/c.js");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{
		R"TS(const { foo } = require("./b");

foo)TS"}, nullptr );
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixWithMultipleModuleExportAssignment, TestImportFixWithMultipleModuleExportAssignment);

// importFixesGlobalTypingsCache_test.go
static void TestImportFixesGlobalTypingsCache(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"});
		return;
		const std::string content = R"TS(// @Filename: /project/tsconfig.json
 { "compilerOptions": { "allowJs": true, "checkJs": true, "module": "commonjs" } }
// @Filename: /home/src/Library/Caches/typescript/node_modules/@types/react-router-dom/package.json
 { "name": "@types/react-router-dom", "version": "16.8.4", "types": "index.d.ts" }
// @Filename: /home/src/Library/Caches/typescript/node_modules/@types/react-router-dom/index.d.ts
export class BrowserRouter {}
// @Filename: /project/node_modules/react-router-dom/package.json
 { "name": "react-router-dom", "version": "16.8.4", "main": "index.js" }
// @Filename: /project/node_modules/react-router-dom/index.js
 export const BrowserRouter = () => null;
// @Filename: /project/index.js
BrowserRouter/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/project/index.js");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{
		R"TS(const { BrowserRouter } = require("react-router-dom");

BrowserRouter)TS"}, nullptr );
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixesGlobalTypingsCache, TestImportFixesGlobalTypingsCache);

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
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{}, nullptr );
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixes_ambientCircularDefaultCrash, TestImportFixes_ambientCircularDefaultCrash);

// importFixes_quotePreferenceDouble_importHelpers_test.go
static void TestImportFixes_quotePreferenceDouble_importHelpers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @importHelpers: true
// @filename: /a.ts
export default () => {};
// @filename: /b.ts
export default () => {};
// @filename: /test.ts
import a from "./a";
[|b|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/test.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{
		R"TS(import b from "./b";
b)TS"}, nullptr );
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixes_quotePreferenceDouble_importHelpers, TestImportFixes_quotePreferenceDouble_importHelpers);

// importFixes_quotePreferenceSingle_importHelpers_test.go
static void TestImportFixes_quotePreferenceSingle_importHelpers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @importHelpers: true
// @filename: /a.ts
export default () => {};
// @filename: /b.ts
export default () => {};
// @filename: /test.ts
import a from './a';
[|b|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/test.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{
		R"TS(import b from './b';
b)TS"}, nullptr );
	});
}
REGISTER_FOURSLASH_TEST(TestImportFixes_quotePreferenceSingle_importHelpers, TestImportFixes_quotePreferenceSingle_importHelpers);


}  // namespace
