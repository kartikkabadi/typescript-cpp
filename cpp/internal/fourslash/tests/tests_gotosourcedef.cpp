// Ported fourslash tests -- batch B (gotosourcedef). One static void TestX(gostd::testing::T*)
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

// goToSourceDefinitionAliasedImportUsage_test.go
static void TestGoToSourceAliasedImportAtUsageSite(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When the cursor is on a usage of an aliased import (not on the import;
		// specifier itself), the module specifier is discovered via;
		// findImportForName, and the original export name is passed as;
		// additionalNames so that the .js file is searched for the correct;
		// declaration. Without additionalNames, only the alias name would be;
		// searched, which does not exist in the .js file.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function unrelated(): void;
export declare function original(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function unrelated() {}
export function /*target*/original() { return "ok"; }
// @Filename: /home/src/workspaces/project/index.ts
import { original as renamed } from "pkg";
renamed/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceAliasedImportAtUsageSite, TestGoToSourceAliasedImportAtUsageSite);

static void TestGoToSourceAliasedImportAtUsageSiteNamespaceImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When an aliased namespace import (import * as ns) is used at a property;
		// access site (ns.foo), the root identifier's import is discovered and the;
		// module specifier is used to resolve the property in the .js file.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function helper(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*target*/helper() { return "ok"; }
// @Filename: /home/src/workspaces/project/index.ts
import * as ns from "pkg";
ns./*usage*/helper();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceAliasedImportAtUsageSiteNamespaceImport, TestGoToSourceAliasedImportAtUsageSiteNamespaceImport);

// goToSourceDefinitionAliasedImportWithPrecedingExports_test.go
static void TestGoToSourceAliasedImportWithPrecedingExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When importing { original as alias }, the module specifier path resolves;
		// the alias text (not the original name). If the target function is NOT the;
		// first export in the .js file, the entry-declaration fallback will point to;
		// the wrong declaration. The fix should resolve to the original export name.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function unrelated(): void;
export declare function original(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function unrelated() {}
export function /*target*/original() { return "ok"; }
// @Filename: /home/src/workspaces/project/index.ts
import { original as /*aliasedImport*/renamed } from "pkg";
renamed();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"aliasedImport"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceAliasedImportWithPrecedingExports, TestGoToSourceAliasedImportWithPrecedingExports);

static void TestGoToSourceReExportAliasWithPrecedingExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Re-export with alias: export { original as alias } from "pkg";
		// should navigate to the original export, not the first statement.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function unrelated(): void;
export declare function original(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function unrelated() {}
export function /*target*/original() { return "ok"; }
// @Filename: /home/src/workspaces/project/reexport.ts
export { original as /*reExportAlias*/renamed } from "pkg";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"reExportAlias"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceReExportAliasWithPrecedingExports, TestGoToSourceReExportAliasWithPrecedingExports);

// goToSourceDefinitionDefaultExport_test.go
static void TestGoToSourceNamedAndDefaultExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// findDeclarationNodesByName correctly finds both named exports and;
		// default-exported classes/functions via the AST visitor.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export default class Widget {}
export declare function helper(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export default class /*targetWidget*/Widget {}
export function /*targetHelper*/helper() {}
// @Filename: /home/src/workspaces/project/index.ts
import /*importDefault*/Widget, { /*importHelper*/helper } from "pkg";
Widget;
helper();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importDefault", "importHelper"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceNamedAndDefaultExport, TestGoToSourceNamedAndDefaultExport);

static void TestGoToSourceDefaultImportNotFirstStatement(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Default import navigates to the actual export default declaration,;
		// not the first statement of the file, when the default export is not first.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare const version: string;
export default class Widget {}
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export const version = "1.0";
export default class /*targetWidget*/Widget {}
// @Filename: /home/src/workspaces/project/index.ts
import /*importDefault*/Widget from "pkg";
Widget;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importDefault"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefaultImportNotFirstStatement, TestGoToSourceDefaultImportNotFirstStatement);

static void TestGoToSourceUnnamedDefaultExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export default function(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export default /*targetDefault*/function() { return "ok"; }
// @Filename: /home/src/workspaces/project/index.ts
import /*importDefault*/myFunc from "pkg";
myFunc/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importDefault", "usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceUnnamedDefaultExport, TestGoToSourceUnnamedDefaultExport);

static void TestGoToSourceEmptyNamesEntryFallback(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// getCandidateSourceDeclarationNames returns empty names,;
		// so mapDeclarationToSourceDefinitions falls through to entry declarations.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
declare const _default: { run(): void };
export default _default;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export default { run() {} };
// @Filename: /home/src/workspaces/project/index.ts
import /*defaultImport*/pkg from "pkg";
pkg.run();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"defaultImport"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceEmptyNamesEntryFallback, TestGoToSourceEmptyNamesEntryFallback);

static void TestGoToSourceExportAssignmentDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// ExportAssignment/default path in findDeclarationNodesByName;
		// and getCandidateSourceDeclarationNames.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
declare const _default: { run(): void };
export default _default;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
/*target*/export default { run() {} };
// @Filename: /home/src/workspaces/project/index.ts
import pkg from "pkg";
pkg/*usage*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceExportAssignmentDefault, TestGoToSourceExportAssignmentDefault);

static void TestGoToSourceExportAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// findDeclarationNodesByName finds export assignment (export = ...);
		// when searching for "default".;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/legacy/package.json
{ "name": "legacy", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/legacy/index.d.ts
declare function legacyFn(): string;
export = legacyFn;
// @Filename: /home/src/workspaces/project/node_modules/legacy/index.js
function /*targetFn*/legacyFn() { return "ok"; }
module.exports = legacyFn;
// @Filename: /home/src/workspaces/project/index.ts
import /*importName*/legacyFn from "legacy";
legacyFn();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceExportAssignment, TestGoToSourceExportAssignment);

static void TestGoToSourceExportAssignmentExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export default function createThing(): { value: number };
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export default function createThing() { return { value: 42 }; }
// @Filename: /home/src/workspaces/project/index.ts
import /*defaultName*/createThing from "pkg";
createThing/*callDefault*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"defaultName", "callDefault"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceExportAssignmentExpression, TestGoToSourceExportAssignmentExpression);

// goToSourceDefinitionDefaultUsageSite_test.go
static void TestGoToSourceDefaultImportUsageSiteChecker(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When the cursor is on a usage of a default import (not on the import;
		// clause itself), the checker path is taken. getCandidateSourceDeclarationNames;
		// must include "default" from the resolved declaration's export-default;
		// modifier, since isDefaultImportName returns false at the usage site.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export default class Widget {
    render(): void;
}
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export default class /*targetWidget*/Widget {
    /*targetRender*/render() {}
}
// @Filename: /home/src/workspaces/project/index.ts
import Widget from "pkg";
const w = new Widget/*constructUsage*/("test");
w./*methodUsage*/render();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"constructUsage", "methodUsage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefaultImportUsageSiteChecker, TestGoToSourceDefaultImportUsageSiteChecker);

static void TestGoToSourceDefaultImportReExportUsage(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Default import re-exported and then used at a call site. The checker;
		// must resolve the alias chain, and the source definition should reach;
		// the original implementation file.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export default function greet(name: string): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export default function /*targetGreet*/greet(name) { return "Hello, " + name; }
// @Filename: /home/src/workspaces/project/index.ts
import greet from "pkg";
greet/*callUsage*/("world");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"callUsage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefaultImportReExportUsage, TestGoToSourceDefaultImportReExportUsage);

// goToSourceDefinitionEmptyAndMissing_test.go
static void TestGoToSourceDefinitionEmptyJsFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When the resolved .js file is empty (0 statements), source definition;
		// navigates to the SourceFile node itself.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function foo(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
// @Filename: /home/src/workspaces/project/index.ts
import { foo } from /*specifier*/"pkg";
foo();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"specifier"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefinitionEmptyJsFile, TestGoToSourceDefinitionEmptyJsFile);

static void TestGoToSourceDefaultImportNoDefaultInJs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When a default import resolves to a .js file that has no default export,;
		// source definition falls back to the first statement of the file.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export default function create(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
/*targetEntry*/function internalCreate() { return {}; }
module.exports = { create: internalCreate };
// @Filename: /home/src/workspaces/project/index.ts
import /*importDefault*/create from "pkg";
create();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importDefault"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefaultImportNoDefaultInJs, TestGoToSourceDefaultImportNoDefaultInJs);

// goToSourceDefinitionExtensionlessMappedSource_test.go
static void TestGoToSourceDefinitionExtensionlessMappedSource(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /lib/helper.d.ts
export declare function helper(): string;
//# sourceMappingURL=helper.d.ts.map
// @Filename: /lib/helper.d.ts.map
{"version":3,"file":"helper.d.ts","sourceRoot":"","sources":["helper"],"names":[],"mappings":"AAAA,wBAAgB,MAAM,IAAI,MAAM,CAAC"}
// @Filename: /lib/helper
export function helper(): string { return ""; }
// @Filename: /index.ts
import { /*usage*/helper } from "./lib/helper";
helper();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefinitionExtensionlessMappedSource, TestGoToSourceDefinitionExtensionlessMappedSource);

// goToSourceDefinitionImportVariants_test.go
static void TestGoToSourceRequireCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// findContainingModuleSpecifier handles require() calls.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @allowJs: true
// @checkJs: true
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function helper(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
exports./*target*/helper = function() { return "ok"; };
// @Filename: /home/src/workspaces/project/index.js
const { /*importName*/helper } = require("pkg");
helper/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName", "usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceRequireCall, TestGoToSourceRequireCall);

static void TestGoToSourceDynamicImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// findContainingModuleSpecifier handles dynamic import() calls.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @target: esnext
// @module: esnext
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function dynHelper(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*target*/dynHelper() { return "dynamic"; }
// @Filename: /home/src/workspaces/project/index.ts
async function main() {
    const mod = await import("pkg");
    mod./*usage*/dynHelper();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDynamicImport, TestGoToSourceDynamicImport);

// goToSourceDefinitionImports_test.go
static void TestGoToSourceAliasedImportExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare const foo: number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
exports./*target*/foo = 1;
// @Filename: /home/src/workspaces/project/index.ts
import { foo as /*importAlias*/bar } from "pkg";
bar;
// @Filename: /home/src/workspaces/project/reexport.ts
export { foo as /*reExportAlias*/bar } from "pkg";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importAlias", "reExportAlias"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceAliasedImportExport, TestGoToSourceAliasedImportExport);

static void TestGoToSourceAliasedImportSpecifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// import { original as alias } uses the propertyName branch.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function original(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*target*/original() { return "ok"; }
// @Filename: /home/src/workspaces/project/index.ts
import { original as /*aliasedImport*/renamed } from "pkg";
renamed();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"aliasedImport"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceAliasedImportSpecifier, TestGoToSourceAliasedImportSpecifier);

static void TestGoToSourceCallThroughImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When calling an imported function, the checker returns both the import specifier;
		// (in the current file) and the call signature target (from .d.ts → mapped to .js).;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare class Widget {
    constructor(name: string);
    render(): void;
}
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export class /*targetWidget*/Widget {
    constructor(name) { this.name = name; }
    /*targetRender*/render() {}
}
// @Filename: /home/src/workspaces/project/index.ts
import { Widget } from "pkg";
const w = new /*constructorCall*/Widget("test");
w./*methodCall*/render();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"constructorCall", "methodCall"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceCallThroughImport, TestGoToSourceCallThroughImport);

static void TestGoToSourceCallbackParam(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
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
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceCallbackParam, TestGoToSourceCallbackParam);

static void TestGoToSourceReExportNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function foo(): string;
export declare function bar(): number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*targetFoo*/foo() { return "ok"; }
export function /*targetBar*/bar() { return 42; }
// @Filename: /home/src/workspaces/project/reexport.ts
export { /*reExportFoo*/foo, /*reExportBar*/bar } from "pkg";
// @Filename: /home/src/workspaces/project/index.ts
import { foo, bar } from [|"pkg"/*moduleSpecifier*/|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"reExportFoo", "reExportBar", "moduleSpecifier"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceReExportNames, TestGoToSourceReExportNames);

static void TestGoToSourceReExportModuleSpecifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function alpha(): string;
export declare function beta(): number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*targetAlpha*/alpha() { return "a"; }
export function /*targetBeta*/beta() { return 2; }
// @Filename: /home/src/workspaces/project/reexport.ts
export { alpha, beta } from [|"pkg"/*reExportSpecifier*/|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"reExportSpecifier"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceReExportModuleSpecifier, TestGoToSourceReExportModuleSpecifier);

static void TestGoToSourceReExportedImplementation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts", "type": "module" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export { foo } from "./foo";
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export { foo } from "./foo.js";
// @Filename: /home/src/workspaces/project/node_modules/pkg/foo.d.ts
export declare function foo(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/foo.js
export function /*target*/foo() { return "ok"; }
// @Filename: /home/src/workspaces/project/index.ts
import { /*importName*/foo } from "pkg";
foo/*start*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName", "start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceReExportedImplementation, TestGoToSourceReExportedImplementation);

static void TestGoToSourceImportFilteredByExternalDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function helper(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*target*/helper() {}
// @Filename: /home/src/workspaces/project/index.ts
import { helper } from "pkg";
helper/*usage*/();
export { helper as /*reExport*/myHelper } from "pkg";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage", "reExport"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceImportFilteredByExternalDeclaration, TestGoToSourceImportFilteredByExternalDeclaration);

static void TestGoToSourceDtsReExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// The .d.ts declaration itself re-exports from another module,;
		// so findContainingModuleSpecifier(declaration) finds that specifier.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/impl.d.ts
export declare function helper(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/impl.js
export function /*target*/helper() {}
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export { helper } from "./impl";
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export { helper } from "./impl.js";
// @Filename: /home/src/workspaces/project/index.ts
import { helper } from "pkg";
helper/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDtsReExport, TestGoToSourceDtsReExport);

static void TestGoToSourceBarrelReExportChain(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// index.js re-exports from impl.js, causing getForwardedImplementationFiles;
		// to follow the chain.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/impl.js
export function /*target*/doWork() { return 42; }
// @Filename: /home/src/workspaces/project/node_modules/pkg/impl.d.ts
export declare function doWork(): number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export { doWork } from "./impl";
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export { doWork } from "./impl.js";
// @Filename: /home/src/workspaces/project/index.ts
import { /*importName*/doWork } from "pkg";
doWork/*callSite*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName", "callSite"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceBarrelReExportChain, TestGoToSourceBarrelReExportChain);

static void TestGoToSourceCJSReExportViaDefineProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function greet(name: string): string;
export declare enum TargetPopulation {
    Team = "team",
    Public = "public",
}
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.TargetPopulation = exports.greet = void 0;
var impl_1 = require("./impl");
Object.defineProperty(exports, "greet", { enumerable: true, get: function () { return impl_1.greet; } });
var types_1 = require("./types");
Object.defineProperty(exports, "TargetPopulation", { enumerable: true, get: function () { return types_1.TargetPopulation; } });
// @Filename: /home/src/workspaces/project/node_modules/pkg/impl.js
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.greet = void 0;
function /*greetImpl*/greet(name) { return "Hello, " + name; }
exports.greet = greet;
// @Filename: /home/src/workspaces/project/node_modules/pkg/types.js
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.TargetPopulation = void 0;
var /*targetPopulationImpl*/TargetPopulation;
(function (TargetPopulation) {
    TargetPopulation["Team"] = "team";
    TargetPopulation["Public"] = "public";
})(TargetPopulation || (exports.TargetPopulation = TargetPopulation = {}));
// @Filename: /home/src/workspaces/project/index.ts
import { /*namedImport*/greet, /*enumImport*/TargetPopulation } from "pkg";
greet/*call*/("world");
TargetPopulation/*enumAccess*/.Team;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"namedImport", "enumImport", "call", "enumAccess"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceCJSReExportViaDefineProperty, TestGoToSourceCJSReExportViaDefineProperty);

// goToSourceDefinitionMergedDeclarations_test.go
static void TestGoToSourceMergedDeclarationDedup(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When a symbol has merged declarations (class + namespace), source;
		// definition deduplicates them and navigates to the single source class.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare class /*dtsClass*/Util {
    run(): void;
}
export declare namespace Util {
    export const version: string;
}
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export class /*targetUtil*/Util {
    run() {}
}
Util.version = "1.0";
// @Filename: /home/src/workspaces/project/index.ts
import { /*importUtil*/Util } from "pkg";
const u: /*typeRef*/Util = new Util();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importUtil", "typeRef"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceMergedDeclarationDedup, TestGoToSourceMergedDeclarationDedup);

// goToSourceDefinitionNestedNodeModules_test.go
static void TestGoToSourceNestedNodeModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When a .d.ts file is inside nested node_modules (more than one /node_modules/;
		// segment), the findImplementationFileFromDtsFileName should bail out rather;
		// than trying to resolve, since the package name extraction may be incorrect.;
		// The module specifier path should still work though.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/outer/package.json
{ "name": "outer", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/outer/index.d.ts
export { inner } from "./node_modules/inner/index";
// @Filename: /home/src/workspaces/project/node_modules/outer/index.js
export { inner } from "./node_modules/inner/index.js";
// @Filename: /home/src/workspaces/project/node_modules/outer/node_modules/inner/package.json
{ "name": "inner", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/outer/node_modules/inner/index.d.ts
export declare function inner(): string;
// @Filename: /home/src/workspaces/project/node_modules/outer/node_modules/inner/index.js
export function /*target*/inner() { return "ok"; }
// @Filename: /home/src/workspaces/project/index.ts
import { /*importName*/inner } from "outer";
inner/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName", "usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceNestedNodeModules, TestGoToSourceNestedNodeModules);

// goToSourceDefinitionNestedScope_test.go
static void TestGoToSourceNestedScopeShadowing(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// findDeclarationNodesByName should only match top-level/exported declarations,;
		// not nested locals that happen to share the same name. Here "helper" is;
		// exported at the top level, but there's also a local "helper" variable inside;
		// a function body. We should navigate to the exported function, not the local.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function helper(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*targetHelper*/helper() { return "ok"; }
function unrelated() {
    const helper = "shadow";
    return helper;
}
// @Filename: /home/src/workspaces/project/index.ts
import { /*importHelper*/helper } from "pkg";
helper/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importHelper", "usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceNestedScopeShadowing, TestGoToSourceNestedScopeShadowing);

static void TestGoToSourceNestedClassShadowing(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// A class "Widget" is exported at the top level, and there's also a local;
		// class "Widget" inside a function. We should only navigate to the exported one.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare class Widget {}
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export class /*targetWidget*/Widget {}
function factory() {
    class Widget { constructor() { this.local = true; } }
    return new Widget();
}
// @Filename: /home/src/workspaces/project/index.ts
import { /*importWidget*/Widget } from "pkg";
new Widget();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importWidget"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceNestedClassShadowing, TestGoToSourceNestedClassShadowing);

// goToSourceDefinitionNonNodeModulesDts_test.go
static void TestGoToSourceFindImplementationNonNodeModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When a .d.ts file is not in node_modules and has no sibling .js,;
		// source definition falls back to the standard definition provider;
		// and navigates to the .d.ts declaration.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @declaration: true
// @Filename: /home/src/workspaces/project/lib/helper.d.ts
export declare function helper(): string;
// @Filename: /home/src/workspaces/project/index.ts
import { /*usage*/helper } from "./lib/helper";
helper();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceFindImplementationNonNodeModules, TestGoToSourceFindImplementationNonNodeModules);

// goToSourceDefinitionPackageResolution_test.go
static void TestGoToSourceAtTypesPackage(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// NoDts resolver can't resolve "foo" to any .js (only @types/foo has .d.ts),;
		// so findImplementationFileFromDtsFileName maps @types/foo → foo and finds the .js.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/@types/foo/package.json
{ "name": "@types/foo", "version": "1.0.0" }
// @Filename: /home/src/workspaces/project/node_modules/@types/foo/index.d.ts
export declare function bar(): string;
// @Filename: /home/src/workspaces/project/node_modules/foo/package.json
{ "name": "foo", "version": "1.0.0", "main": "./index.js" }
// @Filename: /home/src/workspaces/project/node_modules/foo/index.js
export function /*target*/bar() { return "hello"; }
// @Filename: /home/src/workspaces/project/index.ts
import { bar } from "foo";
bar/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceAtTypesPackage, TestGoToSourceAtTypesPackage);

static void TestGoToSourcePackageIndexDts(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When the .d.ts is index.d.ts, tryPackageRootFirst is true,;
		// so package root resolution is tried before subpath.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./lib/index.js", "types": "./lib/index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/lib/index.d.ts
export declare function greet(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/lib/index.js
export function /*target*/greet() { return "hi"; }
// @Filename: /home/src/workspaces/project/index.ts
import { greet } from "pkg";
greet/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourcePackageIndexDts, TestGoToSourcePackageIndexDts);

static void TestGoToSourcePackageRootThenSubpath(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// tryPackageRootFirst is true (index.d.ts), root resolution fails because;
		// there's no main entry, but subpath resolution ("pkg/index") succeeds.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function work(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*target*/work() {}
// @Filename: /home/src/workspaces/project/index.ts
import { work } from "pkg";
work/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourcePackageRootThenSubpath, TestGoToSourcePackageRootThenSubpath);

static void TestGoToSourcePackageRootFallsBackToSubpath(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// tryPackageRootFirst is true (index.d.ts), root resolution fails,;
		// falls back to subpath.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function work(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*target*/work() {}
// @Filename: /home/src/workspaces/project/index.ts
import { work } from "pkg";
work/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourcePackageRootFallsBackToSubpath, TestGoToSourcePackageRootFallsBackToSubpath);

static void TestGoToSourceSubpathNotIndex(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Subpath resolution succeeds when the d.ts is NOT index.d.ts.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "types": "./lib/utils.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/lib/utils.d.ts
export declare function util(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/lib/utils.js
export function /*target*/util() {}
// @Filename: /home/src/workspaces/project/index.ts
import { util } from "pkg";
util/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceSubpathNotIndex, TestGoToSourceSubpathNotIndex);

// goToSourceDefinitionProperties_test.go
static void TestGoToSourceAccessExpressionProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare const obj: { greet(name: string): string; count: number; };
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export const /*targetObj*/obj = { /*targetGreet*/greet(name) { return name; }, /*targetCount*/count: 42 };
// @Filename: /home/src/workspaces/project/index.ts
import { obj } from "pkg";
obj./*propAccess*/greet("world");
obj./*propAccess2*/count;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"propAccess", "propAccess2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceAccessExpressionProperty, TestGoToSourceAccessExpressionProperty);

static void TestGoToSourcePropertyOfAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/a.js
export const a = { /*end*/a: 'a' };
// @Filename: /home/src/workspaces/project/a.d.ts
export declare const a: { a: string };
// @Filename: /home/src/workspaces/project/b.ts
import { a } from './a';
a.[|a/*start*/|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourcePropertyOfAlias, TestGoToSourcePropertyOfAlias);

static void TestGoToSourceIndexSignatureProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When accessing a property defined via index signature, getDeclarationsFromLocation;
		// returns empty, so the GetPropertyOfType fallback is used.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare const config: { readonly [key: string]: string; name: string };
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export const config = { /*targetName*/name: "test" };
// @Filename: /home/src/workspaces/project/index.ts
import { config } from "pkg";
config./*propAccess*/name;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"propAccess"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceIndexSignatureProperty, TestGoToSourceIndexSignatureProperty);

static void TestGoToSourceMappedTypeProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// getDeclarationsFromLocation returns empty for a property that exists only;
		// via a mapped type (no explicit declaration), so GetPropertyOfType fallback is used.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
type Keys = "a" | "b";
export declare const obj: { [K in Keys]: number };
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export const obj = { a: 1, /*target*/b: 2 };
// @Filename: /home/src/workspaces/project/index.ts
import { obj } from "pkg";
obj./*propAccess*/b;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"propAccess"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceMappedTypeProperty, TestGoToSourceMappedTypeProperty);

static void TestGoToSourceCommonJSAliasPrefersDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare enum TargetPopulation {
    Team = "team",
    Internal = "internal",
    Insiders = "insider",
    Public = "public",
}
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.TargetPopulation = void 0;
var TargetPopulation;
(function (TargetPopulation) {
    TargetPopulation["Team"] = "team";
    TargetPopulation["Internal"] = "internal";
    TargetPopulation["Insiders"] = "insider";
    TargetPopulation["Public"] = "public";
})(TargetPopulation || (exports.TargetPopulation = TargetPopulation = {}));
// @Filename: /home/src/workspaces/project/index.ts
import * as tas from "pkg";
tas./*start*/TargetPopulation.Public;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceCommonJSAliasPrefersDeclaration, TestGoToSourceCommonJSAliasPrefersDeclaration);

// goToSourceDefinitionPropertyAccessNoDecl_test.go
static void TestGoToSourcePropertyAccessNoDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When a property exists only via a mapped type in the .d.ts, the checker;
		// returns no declarations. The source definition resolver should still;
		// navigate to the property in the .js file by finding the module specifier;
		// from the parent expression's import declaration.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
type Keys = "alpha" | "beta";
export declare const config: { [K in Keys]: string };
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export const config = { /*targetAlpha*/alpha: "a", /*targetBeta*/beta: "b" };
// @Filename: /home/src/workspaces/project/index.ts
import { config } from "pkg";
config./*accessAlpha*/alpha;
config./*accessBeta*/beta;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"accessAlpha", "accessBeta"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourcePropertyAccessNoDeclaration, TestGoToSourcePropertyAccessNoDeclaration);

static void TestGoToSourcePropertyAccessDeepChain(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Deep property access chain: import * as ns; ns.obj.prop;
		// where the intermediate object has no declaration but the root;
		// identifier can be traced back to its import.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare const nested: { inner: { value: number } };
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export const nested = { inner: { /*targetValue*/value: 42 } };
// @Filename: /home/src/workspaces/project/index.ts
import { nested } from "pkg";
nested.inner./*accessValue*/value;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"accessValue"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourcePropertyAccessDeepChain, TestGoToSourcePropertyAccessDeepChain);

static void TestGoToSourcePropertyAccessNamespaceImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// import * as ns from "pkg"; ns.thing — where "thing" has no declarations;
		// from the checker (e.g. module augmentation or dynamic).;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
type Keys = "x" | "y";
export declare const coords: { [K in Keys]: number };
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export const coords = { /*targetX*/x: 10, /*targetY*/y: 20 };
// @Filename: /home/src/workspaces/project/index.ts
import { coords } from "pkg";
coords./*accessX*/x;
coords./*accessY*/y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"accessX", "accessY"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourcePropertyAccessNamespaceImport, TestGoToSourcePropertyAccessNamespaceImport);

// goToSourceDefinitionPropertyAccess_test.go
static void TestGoToSourceMappedTypePropertyWithMatch(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When accessing a property that only exists via a mapped type, the checker;
		// returns no declarations. The property access fallback (GetPropertyOfType);
		// should find the property if it's in the .js implementation file.;
		// This test differs from the existing goToSourceMappedTypeProperty by having;
		// a named explicit property in the .d.ts alongside the mapped type.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare const obj: { a: number; b: number };
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export const obj = { /*targetA*/a: 1, /*targetB*/b: 2 };
// @Filename: /home/src/workspaces/project/index.ts
import { obj } from "pkg";
obj./*propA*/a;
obj./*propB*/b;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"propA", "propB"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceMappedTypePropertyWithMatch, TestGoToSourceMappedTypePropertyWithMatch);

static void TestGoToSourceNamespaceImportProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// import * as ns from "pkg"; ns.prop — should navigate to the property;
		// in the .js file.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function helper(): void;
export declare const value: number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*targetHelper*/helper() {}
export const /*targetValue*/value = 42;
// @Filename: /home/src/workspaces/project/index.ts
import * as pkg from "pkg";
pkg./*helperAccess*/helper();
pkg./*valueAccess*/value;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"helperAccess", "valueAccess"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceNamespaceImportProperty, TestGoToSourceNamespaceImportProperty);

// goToSourceDefinitionReExportChain_test.go
static void TestGoToSourceForwardedReExportChain(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When an implementation file re-exports from another file, source;
		// definition follows the re-export chain to the actual implementation.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function helper(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export { helper } from './impl.js';
// @Filename: /home/src/workspaces/project/node_modules/pkg/impl.js
export function /*targetHelper*/helper() { return "ok"; }
// @Filename: /home/src/workspaces/project/index.ts
import { /*importHelper*/helper } from "pkg";
helper();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importHelper"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceForwardedReExportChain, TestGoToSourceForwardedReExportChain);

// goToSourceDefinitionScopedPackage_test.go
static void TestGoToSourceScopedPackage(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Scoped packages (@scope/pkg) exercise UnmangleScopedPackageName;
		// in findImplementationFileFromDtsFileName.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/@myscope/mylib/package.json
{ "name": "@myscope/mylib", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/@myscope/mylib/index.d.ts
export declare function scopedHelper(): string;
// @Filename: /home/src/workspaces/project/node_modules/@myscope/mylib/index.js
export function /*target*/scopedHelper() { return "scoped"; }
// @Filename: /home/src/workspaces/project/index.ts
import { /*importName*/scopedHelper } from "@myscope/mylib";
scopedHelper/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName", "usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceScopedPackage, TestGoToSourceScopedPackage);

static void TestGoToSourceScopedAtTypesPackage(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// @types/@scope/pkg should map to @scope/pkg implementation.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/@types/myns__mylib/package.json
{ "name": "@types/myns__mylib", "version": "1.0.0" }
// @Filename: /home/src/workspaces/project/node_modules/@types/myns__mylib/index.d.ts
export declare function nsHelper(): number;
// @Filename: /home/src/workspaces/project/node_modules/@myns/mylib/package.json
{ "name": "@myns/mylib", "version": "1.0.0", "main": "./index.js" }
// @Filename: /home/src/workspaces/project/node_modules/@myns/mylib/index.js
export function /*target*/nsHelper() { return 42; }
// @Filename: /home/src/workspaces/project/index.ts
import { nsHelper } from "@myns/mylib";
nsHelper/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceScopedAtTypesPackage, TestGoToSourceScopedAtTypesPackage);

// goToSourceDefinitionTripleSlashUnresolved_test.go
static void TestGoToSourceDefinitionUnresolvedTripleSlash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When the cursor is on a triple-slash reference directive that doesn't;
		// resolve to a file, source definition returns empty results.;
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/index.ts
/// <reference /*marker*/path="nonexistent.ts" />
export {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"marker"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefinitionUnresolvedTripleSlash, TestGoToSourceDefinitionUnresolvedTripleSlash);

// goToSourceDefinitionTripleSlash_test.go
static void TestGoToSourceReferenceTypesToJS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// /// <reference types="foo"/> resolves to @types/foo/index.d.ts.;
		// Source definition should find the corresponding foo/index.js.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/@types/foo/package.json
{ "name": "@types/foo", "version": "1.0.0" }
// @Filename: /home/src/workspaces/project/node_modules/@types/foo/index.d.ts
export declare function bar(): string;
// @Filename: /home/src/workspaces/project/node_modules/foo/package.json
{ "name": "foo", "version": "1.0.0", "main": "./index.js" }
// @Filename: /home/src/workspaces/project/node_modules/foo/index.js
export function /*target*/bar() { return "hello"; }
// @Filename: /home/src/workspaces/project/index.ts
/// <reference types="[|foo/*refTypes*/|]" />
import { bar } from "foo";
bar();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"refTypes"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceReferenceTypesToJS, TestGoToSourceReferenceTypesToJS);

static void TestGoToSourceReferencePathToDts(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// /// <reference path="./lib.d.ts"/> where a sibling .js file exists.;
		// Source definition should navigate to the .js implementation.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
/// <reference path="./lib.d.ts" />
export declare function main(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/lib.d.ts
export declare function helper(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/lib.js
export function /*target*/helper() { return "ok"; }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function main() {}
// @Filename: /home/src/workspaces/project/index.ts
/// <reference path="./node_modules/pkg/[|lib.d.ts/*refPath*/|]" />
declare function helper(): string;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"refPath"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceReferencePathToDts, TestGoToSourceReferencePathToDts);

// goToSourceDefinitionTypeOnlySymbol_test.go
static void TestGoToSourceDefinitionTypeOnlyImportFallsBackToDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When source definition is invoked on a type-only symbol (e.g. an;
		// interface) imported via a non-type-only import, the .js file has no;
		// corresponding declaration. Source definition should fall back to the;
		// .d.ts declaration rather than jumping to the first line of the .js file.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export interface /*targetDecl*/Config {
    name: string;
    value: number;
}
export declare function create(config: Config): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function create(config) { return config; }
// @Filename: /home/src/workspaces/project/index.ts
import { /*importConfig*/Config, create } from "pkg";
const c: Config = { name: "test", value: 1 };
create(c);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importConfig"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefinitionTypeOnlyImportFallsBackToDeclaration, TestGoToSourceDefinitionTypeOnlyImportFallsBackToDeclaration);

static void TestGoToSourceDefinitionTypeOnlyUsageFallsBackToDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When source definition is invoked at a usage site of a type-only symbol,;
		// the checker path finds the .d.ts declarations but mapDeclarationToSource;
		// finds nothing in the .js file. The result should fall back to regular;
		// definition (the .d.ts declaration).;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export interface /*targetDecl*/Config {
    name: string;
}
export declare function create(config: Config): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function create(config) { return config; }
// @Filename: /home/src/workspaces/project/index.ts
import { Config, create } from "pkg";
const c: /*usageSite*/Config = { name: "test" };
create(c);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usageSite"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefinitionTypeOnlyUsageFallsBackToDeclaration, TestGoToSourceDefinitionTypeOnlyUsageFallsBackToDeclaration);

static void TestGoToSourceDefinitionValueImportStillWorks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Value imports (functions, classes, variables) should still navigate;
		// to the .js implementation, not regress to .d.ts.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function /*dtsCreate*/create(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*targetCreate*/create() {}
// @Filename: /home/src/workspaces/project/index.ts
import { /*importCreate*/create } from "pkg";
create();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importCreate"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefinitionValueImportStillWorks, TestGoToSourceDefinitionValueImportStillWorks);

// goToSourceDefinitionTypes_test.go
static void TestGoToSourceFallbacksToDefinitionForInterface(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export interface /*target*/Config {
    enabled: boolean;
}
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
exports.makeConfig = () => ({ enabled: true });
// @Filename: /home/src/workspaces/project/index.ts
import type { /*importName*/Config } from "pkg";
let value: /*typeRef*/Config;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName", "typeRef"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceFallbacksToDefinitionForInterface, TestGoToSourceFallbacksToDefinitionForInterface);

static void TestGoToSourceTypeOnlySymbolFallback(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When a type-only symbol (type alias) is imported with a regular import and used;
		// in a value position, source definition should fall back to regular definition;
		// (the .d.ts declaration) since there's no concrete JS implementation.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/types.d.ts
export interface Config { enabled: boolean; }
// @Filename: /home/src/workspaces/project/node_modules/pkg/types.js
// no runtime content for Config interface
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export { Config } from "./types";
export declare function makeConfig(): Config;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export { Config } from "./types.js";
export function makeConfig() { return { enabled: true }; }
// @Filename: /home/src/workspaces/project/index.ts
import { Config, makeConfig } from "pkg";
let c: /*typeRef*/Config;
makeConfig/*callRef*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"typeRef", "callRef"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceTypeOnlySymbolFallback, TestGoToSourceTypeOnlySymbolFallback);

static void TestGoToSourceForwardedNonConcreteMerge(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Forwarded declarations are non-concrete, so they merge with the initial;
		// non-concrete declarations. The barrel index.js re-exports from types.js;
		// which only has type re-exports.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @allowJs: true
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export { Config } from "./types";
// @Filename: /home/src/workspaces/project/node_modules/pkg/types.d.ts
export interface Config { enabled: boolean; }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export { Config } from "./types.js";
// @Filename: /home/src/workspaces/project/node_modules/pkg/types.js
// Config is a type, no runtime value
// @Filename: /home/src/workspaces/project/index.ts
import { /*importName*/Config } from "pkg";
let c: Config;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceForwardedNonConcreteMerge, TestGoToSourceForwardedNonConcreteMerge);

// goToSourceDefinition_test.go
static void TestGoToSourceNodeModulesWithTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/foo/package.json
{ "name": "foo", "version": "1.0.0", "main": "./lib/main.js", "types": "./types/main.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/foo/lib/main.js
export const /*end*/a = "a";
// @Filename: /home/src/workspaces/project/node_modules/foo/types/main.d.ts
export declare const a: string;
// @Filename: /home/src/workspaces/project/index.ts
import { a } from "foo";
[|a/*start*/|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"start"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceNodeModulesWithTypes, TestGoToSourceNodeModulesWithTypes);

static void TestGoToSourceLocalJsBesideDts(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/a.js
export const /*end*/a = "a";
// @Filename: /home/src/workspaces/project/a.d.ts
export declare const a: string;
// @Filename: /home/src/workspaces/project/index.ts
import { a } from [|"./a"/*moduleSpecifier*/|];
[|a/*identifier*/|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"identifier", "moduleSpecifier"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceLocalJsBesideDts, TestGoToSourceLocalJsBesideDts);

static void TestGoToSourceNonDeclarationFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Declaration is in a .ts file (not .d.ts),;
		// so mapDeclarationToSourceDefinitions returns it as-is.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/utils.ts
export function /*target*/helper() { return 1; }
// @Filename: /home/src/workspaces/project/index.ts
import { helper } from "./utils";
helper/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceNonDeclarationFile, TestGoToSourceNonDeclarationFile);

static void TestGoToSourceNoImplementationFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// No implementation file can be resolved (types-only package with no .js).;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function typesOnly(): void;
// @Filename: /home/src/workspaces/project/index.ts
import { /*importName*/typesOnly } from "pkg";
typesOnly/*callSite*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName", "callSite"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceNoImplementationFile, TestGoToSourceNoImplementationFile);

static void TestGoToSourceDeclarationMapSourceMap(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// .d.ts has a sourcemap pointing back to the original .ts source.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./dist/index.js", "types": "./dist/index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/src/index.ts
export function /*target*/greet() { return "hi"; }
// @Filename: /home/src/workspaces/project/node_modules/pkg/dist/index.d.ts
export declare function greet(): string;
//# sourceMappingURL=index.d.ts.map
// @Filename: /home/src/workspaces/project/node_modules/pkg/dist/index.d.ts.map
{"version":3,"file":"index.d.ts","sourceRoot":"","sources":["../src/index.ts"],"names":[],"mappings":"AAAA,wBAAgB,KAAK,WAAY"}
// @Filename: /home/src/workspaces/project/node_modules/pkg/dist/index.js
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.greet = greet;
function greet() { return "hi"; }
// @Filename: /home/src/workspaces/project/index.ts
import { greet } from "pkg";
greet/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDeclarationMapSourceMap, TestGoToSourceDeclarationMapSourceMap);

static void TestGoToSourceDeclarationMapFallback(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// findClosestDeclarationNode walks up parents and finds no declaration,;
		// returns entry node. This happens when source map points to a position;
		// that's not inside any declaration.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./dist/index.js", "types": "./dist/index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/src/index.ts
/*target*/console.log("side effect");
export function greet() { return "hi"; }
// @Filename: /home/src/workspaces/project/node_modules/pkg/dist/index.d.ts
export declare function greet(): string;
//# sourceMappingURL=index.d.ts.map
// @Filename: /home/src/workspaces/project/node_modules/pkg/dist/index.d.ts.map
{"version":3,"file":"index.d.ts","sourceRoot":"","sources":["../src/index.ts"],"names":[],"mappings":"AAC6B"}
// @Filename: /home/src/workspaces/project/node_modules/pkg/dist/index.js
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.greet = greet;
console.log("side effect");
function greet() { return "hi"; }
// @Filename: /home/src/workspaces/project/index.ts
import { greet } from "pkg";
greet/*usage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"usage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDeclarationMapFallback, TestGoToSourceDeclarationMapFallback);

static void TestGoToSourceNamedExportsSpecifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function foo(): string;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
export function /*target*/foo() { return "ok"; }
// @Filename: /home/src/workspaces/project/index.ts
import { foo } from "pkg";
const result = foo/*valueUsage*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"valueUsage"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceNamedExportsSpecifier, TestGoToSourceNamedExportsSpecifier);

static void TestGoToSourceTripleSlashReference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Cursor on a /// <reference path="..."/> directive pointing to a .js file.;
		const std::string content = R"TS(// @allowJs: true
// @Filename: /home/src/workspaces/project/helper.js
/*target*/function helper() { return 1; }
// @Filename: /home/src/workspaces/project/index.ts
/// <reference path="./[|helper.js/*refPath*/|]" />
declare function helper(): number;
helper();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"refPath"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceTripleSlashReference, TestGoToSourceTripleSlashReference);

static void TestGoToSourceFallbackToModuleSpecifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// When the specific name can't be found in the .js implementation file;
		// (because the JS uses a different export pattern), the fallback returns;
		// the entry declaration of the .js file.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @allowJs: true
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./index.js", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.d.ts
export declare function internalHelper(): void;
// @Filename: /home/src/workspaces/project/node_modules/pkg/index.js
/*entryPoint*/Object.defineProperty(exports, "internalHelper", { value: function() {} });
// @Filename: /home/src/workspaces/project/index.ts
import { /*importName*/internalHelper } from "pkg";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceFallbackToModuleSpecifier, TestGoToSourceFallbackToModuleSpecifier);

static void TestGoToSourceFilterPreferredFallbackAll(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// filterPreferredSourceDeclarations returns all declarations when none are;
		// property-like and none are concrete. This happens with re-export specifiers;
		// matching the name in the .js file.;
		const std::string content = R"TS(// @moduleResolution: bundler
// @allowJs: true
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "main": "./barrel.js", "types": "./barrel.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/barrel.d.ts
export { value } from "./impl";
// @Filename: /home/src/workspaces/project/node_modules/pkg/impl.d.ts
export declare const value: number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/barrel.js
export { value } from "./impl.js";
// @Filename: /home/src/workspaces/project/node_modules/pkg/impl.js
export const /*target*/value = 42;
// @Filename: /home/src/workspaces/project/index.ts
import { /*importName*/value } from "pkg";
console.log(value);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToSourceDefinition(t, std::vector<std::string>{"importName"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceFilterPreferredFallbackAll, TestGoToSourceFilterPreferredFallbackAll);


}  // namespace
