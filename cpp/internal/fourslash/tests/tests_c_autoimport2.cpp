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

// newContentMapperFourslash — fourslash/tests/contentMapper_test.go.
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
static const std::string TestAutoImportSymlinkedMonorepoSourceUpdateScenario = R"TS(
// @Filename: /home/src/workspaces/project/tsconfig.base.json
{
  "compilerOptions": {
    "module": "nodenext",
    "moduleResolution": "nodenext",
	"composite": true
  }
}

// @Filename: /home/src/workspaces/project/packages/foo/package.json
{
  "name": "@packages/foo",
  "type": "module",
  "exports": {
    ".": {
	  "types": "./src/index.ts",
	  "default": "./dist/index.js"
	}
  }
}

// @Filename: /home/src/workspaces/project/packages/foo/tsconfig.json
{ "extends": "../../tsconfig.base.json" }

// @Filename: /home/src/workspaces/project/packages/foo/src/index.ts
/*fooEdit*/

// @Filename: /home/src/workspaces/project/packages/bar/package.json
{
  "name": "@packages/bar",
  "type": "module",
  "exports": {
    ".": {
	  "types": "./src/index.ts",
	  "default": "./dist/index.js"
	}
  },
  "dependencies": {
    "@packages/foo": "*"
  }
}

// @Filename: /home/src/workspaces/project/packages/bar/tsconfig.json
{ "extends": "../../tsconfig.base.json" }

// @Filename: /home/src/workspaces/project/packages/bar/src/index.ts
/*fooCompletion*/

// @Filename: /home/src/workspaces/project/package.json
{ "workspaces": ["packages/*"], "type": "module" }

// @link: /home/src/workspaces/project/packages/bar -> /home/src/workspaces/project/node_modules/@packages/bar
// @link: /home/src/workspaces/project/packages/foo -> /home/src/workspaces/project/node_modules/@packages/foo
)TS";
static const std::string TestAutoImportTransitiveLeakScenario = R"TS(
// @Filename: /home/src/workspaces/project/tsconfig.base.json
{
  "compilerOptions": {
    "module": "nodenext",
    "moduleResolution": "nodenext",
    "composite": true
  }
}

// @Filename: /home/src/workspaces/project/packages/foo/package.json
{
  "name": "@packages/foo",
  "type": "module",
  "exports": {
    ".": {
	  "types": "./src/index.ts",
	  "default": "./dist/index.js"
	}
  },
  "imports": {
    "#*": {
	  "types": "./src/*.ts",
      "default": "./dist/*.js"
    }
  }
}

// @Filename: /home/src/workspaces/project/packages/foo/tsconfig.json
{ "extends": "../../tsconfig.base.json" }

// @Filename: /home/src/workspaces/project/packages/foo/src/internal/index.ts
export function fooInternal() {
  console.log("foo");
}

// @Filename: /home/src/workspaces/project/packages/foo/src/index.ts
import { fooInternal } from "#internal/index"
export function foo() {
  fooInternal();
}

// @Filename: /home/src/workspaces/project/packages/bar/package.json
{
  "name": "@packages/bar",
  "type": "module",
  "exports": {
    ".": {
	  "types": "./src/index.ts",
	  "default": "./dist/index.js"
	}
  },
  "imports": {
    "#*": {
	  "types": "./src/*.ts",
      "default": "./dist/*.js"
    }
  },
  "dependencies": {
    "@packages/foo": "*"
  }
}

// @Filename: /home/src/workspaces/project/packages/bar/tsconfig.json
{ "extends": "../../tsconfig.base.json" }

// @Filename: /home/src/workspaces/project/packages/bar/src/index.ts
import { foo } from "@packages/foo"

fo/*fooCompletion*/

// @Filename: /home/src/workspaces/project/package.json
{ "workspaces": ["packages/*"], "type": "module" }

// @link: /home/src/workspaces/project/packages/bar -> /home/src/workspaces/project/node_modules/@packages/bar
// @link: /home/src/workspaces/project/packages/foo -> /home/src/workspaces/project/node_modules/@packages/foo
)TS";

// augmentedTypesModule1_test.go

// augmentedTypesModule1_test.go
static void TestAugmentedTypesModule1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m1c {
    export interface I { foo(): void; }
}
var m1c = 1; // Should be allowed
var x: m1c./*1*/;
var /*2*/r = m1c;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"I"}})}));
		f->VerifyQuickInfoAt(t, "2", "var r: number", "");
	});
}
struct anonStruct0 { std::string name; std::string content;};
REGISTER_FOURSLASH_TEST(TestAugmentedTypesModule1, TestAugmentedTypesModule1);

// augmentedTypesModule2_test.go

// augmentedTypesModule2_test.go
static void TestAugmentedTypesModule2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function /*11*/m2f(x: number) { };
namespace m2f { export interface I { foo(): void } }
var x: m2f./*1*/
var /*2*/r = m2f/*3*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "11", "function m2f(x: number): void", "");
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"I"}})}));
		f->Insert(t, "I.");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, nullptr);
		f->Backspace(t, 1);
		f->VerifyQuickInfoAt(t, "2", "var r: (x: number) => void", "");
		f->GoToMarker(t, "3");
		f->Insert(t, "(");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "m2f(x: number): void"});
	});
}
REGISTER_FOURSLASH_TEST(TestAugmentedTypesModule2, TestAugmentedTypesModule2);

// augmentedTypesModule3_test.go

// augmentedTypesModule3_test.go
static void TestAugmentedTypesModule3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function m2g() { };
namespace m2g { export class C { foo(x: number) { } } }
var x: m2g./*1*/;
var /*2*/r = m2g/*3*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"C"}})}));
		f->Insert(t, "C.");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, nullptr);
		f->Backspace(t, 1);
		f->VerifyQuickInfoAt(t, "2", "var r: typeof m2g", "");
		f->GoToMarker(t, "3");
		f->Insert(t, "(");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "m2g(): void"});
	});
}
REGISTER_FOURSLASH_TEST(TestAugmentedTypesModule3, TestAugmentedTypesModule3);

// augmentedTypesModule4_test.go

// augmentedTypesModule4_test.go
static void TestAugmentedTypesModule4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m3d { export var y = 2; }
declare class m3d { foo(): void }
var /*1*/r = new m3d();
r./*2*/
var /*4*/r2 = m3d./*3*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var r: m3d", "");
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"foo"}})}));
		f->Insert(t, "foo();");
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"y"}})}));
		f->Insert(t, "y;");
		f->VerifyQuickInfoAt(t, "4", "var r2: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestAugmentedTypesModule4, TestAugmentedTypesModule4);

// augmentedTypesModule5_test.go

// augmentedTypesModule5_test.go
static void TestAugmentedTypesModule5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare class m3e { foo(): void }
namespace m3e { export var y = 2; }
var /*1*/r = new m3e();
r./*2*/
var /*4*/r2 = m3e./*3*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var r: m3e", "");
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"foo"}})}));
		f->Insert(t, "foo();");
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"y"}})}));
		f->Insert(t, "y;");
		f->VerifyQuickInfoAt(t, "4", "var r2: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestAugmentedTypesModule5, TestAugmentedTypesModule5);

// augmentedTypesModule6_test.go

// augmentedTypesModule6_test.go
static void TestAugmentedTypesModule6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare class m3f { foo(x: number): void }
namespace m3f { export interface I { foo(): void } }
var x: m3f./*1*/
var /*4*/r = new /*2*/m3f(/*3*/);
r./*5*/
var r2: m3f.I = r;
r2./*6*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"I"}, .Excludes = std::vector<std::string>{"foo"}})}));
		f->Insert(t, "I;");
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"m3f"}})}));
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "m3f(): m3f"});
		f->VerifyQuickInfoAt(t, "4", "var r: m3f", "");
		f->VerifyCompletions(t, "5", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"foo"}})}));
		f->Insert(t, "foo(1)");
		f->VerifyCompletions(t, "6", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"foo"}})}));
		f->Insert(t, "foo(");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(): void"});
	});
}
REGISTER_FOURSLASH_TEST(TestAugmentedTypesModule6, TestAugmentedTypesModule6);

// autoImportAllowImportingTsExtensionsPackageJsonImports1_test.go

// autoImportAllowImportingTsExtensionsPackageJsonImports1_test.go
static void TestAutoImportAllowImportingTsExtensionsPackageJsonImports1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @module: node18
// @allowImportingTsExtensions: true
// @Filename: /node_modules/pkg/package.json
{
  "name": "pkg",
  "type": "module",
  "exports": {
    "./*": {
      "types": "./types/*",
      "default": "./dist/*"
    }
  }
}
// @Filename: /node_modules/pkg/types/external.d.ts
export declare function external(name: string): any;
// @Filename: /package.json
{
  "name": "self",
  "type": "module",
  "imports": {
    "#*": "./src/*"
  },
  "dependencies": {
    "pkg": "*"
  }
}
// @Filename: /src/add.ts
export function add(a: number, b: number) {}
// @Filename: /src/index.ts
add/*imports*/;
external/*exports*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "imports", std::vector<std::string>{"#add.ts"}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "exports", std::vector<std::string>{"pkg/external.js"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportAllowImportingTsExtensionsPackageJsonImports1, TestAutoImportAllowImportingTsExtensionsPackageJsonImports1);

// autoImportAllowImportingTsExtensionsPackageJsonImports2_test.go

// autoImportAllowImportingTsExtensionsPackageJsonImports2_test.go
static void TestAutoImportAllowImportingTsExtensionsPackageJsonImports2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "allowImportingTsExtensions": true,
    "rootDir": "src",
    "outDir": "dist",
    "declarationDir": "types",
    "declaration": true
  }
}
// @Filename: /package.json
{
  "name": "self",
  "type": "module",
  "imports": {
    "#*": {
      "types": "./types/*",
      "default": "./dist/*"
    }
  }
}
// @Filename: /src/add.ts
export function add(a: number, b: number) {}
// @Filename: /src/index.ts
add/*imports*/;
external/*exports*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "imports", std::vector<std::string>{"#add.js"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportAllowImportingTsExtensionsPackageJsonImports2, TestAutoImportAllowImportingTsExtensionsPackageJsonImports2);

// autoImportAutomaticJsxRuntimeCrash_test.go

// autoImportAutomaticJsxRuntimeCrash_test.go
static void TestAutoImportAutomaticJsxRuntimeCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /project/node_modules/pkg/package.json
{ "name": "pkg", "types": "index.tsx" }

// @Filename: /project/node_modules/pkg/index.tsx
/** @jsxRuntime automatic */
const container = { Widget: { value: <div /> } satisfies {} };
export default container.Widget;

// @Filename: /project/package.json
{ "dependencies": { "pkg": "*" } }

// @Filename: /project/tsconfig.json
{ "compilerOptions": { "jsx": "react-jsx" } }

// @Filename: /project/index.ts
Widg/**/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"Widget"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportAutomaticJsxRuntimeCrash, TestAutoImportAutomaticJsxRuntimeCrash);

// autoImportAutomaticJsxRuntimeCrash_test.go
static void TestAutoImportAutomaticJsxRuntimeProjectReferenceCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /packages/lib/package.json
{ "name": "lib", "types": "out/index.d.ts" }

// @Filename: /packages/lib/tsconfig.json
{ "compilerOptions": { "composite": true, "jsx": "react-jsx", "outDir": "out" } }

// @Filename: /packages/lib/index.tsx
/** @jsxRuntime automatic */
const container = { Widget: { value: <div /> } satisfies {} };
export default container.Widget;

// @Filename: /packages/app/package.json
{ "dependencies": { "lib": "*" } }

// @Filename: /packages/app/tsconfig.json
{ "references": [{ "path": "../lib" }] }

// @Filename: /packages/app/index.ts
Widg/**/

// @link: /packages/lib -> /packages/app/node_modules/lib
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"Widget"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportAutomaticJsxRuntimeProjectReferenceCrash, TestAutoImportAutomaticJsxRuntimeProjectReferenceCrash);

// autoImportBundlerBlockRelativeNodeModulesPaths_test.go

// autoImportBundlerBlockRelativeNodeModulesPaths_test.go
static void TestAutoImportBundlerBlockRelativeNodeModulesPaths(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @moduleResolution: bundler
// @Filename: /node_modules/dep/package.json
{
  "name": "dep",
  "version": "1.0.0",
  "exports": "./dist/index.js"
}
// @Filename: /node_modules/dep/dist/utils.d.ts
export const util: () => void;
// @Filename: /node_modules/dep/dist/index.d.ts
export * from "./utils";
// @Filename: /index.ts
util/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"dep"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportBundlerBlockRelativeNodeModulesPaths, TestAutoImportBundlerBlockRelativeNodeModulesPaths);

// autoImportBundlerExports_test.go

// autoImportBundlerExports_test.go
static void TestAutoImportBundlerExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @moduleResolution: bundler
// @Filename: /node_modules/dep/package.json
{
  "name": "dep",
  "version": "1.0.0",
  "exports": {
    ".": "./dist/index.js"
  }
}
// @Filename: /node_modules/dep/dist/index.d.ts
export const dep: number;
// @Filename: /index.ts
dep/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"dep"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportBundlerExports, TestAutoImportBundlerExports);

// autoImportCJSWithNodeModuleKind_test.go

// autoImportCJSWithNodeModuleKind_test.go
static void TestAutoImportCJSWithNodeModuleKind(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
  "compilerOptions": {
    "allowJs": true,
    "module": "node20",
    "checkJs": true,
    "noEmit": true
  }
}
// @Filename: /package.json
{ "type": "commonjs" }
// @Filename: /lib.js
module.exports = { LIB_VERSION: 1 };
// @Filename: /main.js
module.exports.foo = 0;
LIB_VERSION/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(const { LIB_VERSION } = require("./lib");

module.exports.foo = 0;
LIB_VERSION)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCJSWithNodeModuleKind, TestAutoImportCJSWithNodeModuleKind);

// autoImportCJSWithNodeModuleKind_test.go
static void TestAutoImportCJSWithNodeModuleKindEmptyFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
  "compilerOptions": {
    "allowJs": true,
    "module": "node20",
    "checkJs": true,
    "noEmit": true
  }
}
// @Filename: /package.json
{ "type": "commonjs" }
// @Filename: /lib.js
module.exports = { LIB_VERSION: 1 };
// @Filename: /main.js
LIB_VERSION/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(const { LIB_VERSION } = require("./lib");

LIB_VERSION)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCJSWithNodeModuleKindEmptyFile, TestAutoImportCJSWithNodeModuleKindEmptyFile);

// autoImportCJSWithNodeModuleKind_test.go
static void TestAutoImportCJSWithModuleDetectionForce(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
  "compilerOptions": {
    "allowJs": true,
    "module": "preserve",
    "moduleDetection": "force",
    "checkJs": true,
    "noEmit": true
  }
}
// @Filename: /lib.js
export const LIB_VERSION = 1;
// @Filename: /main.js
const path = require("path");
LIB_VERSION/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(const path = require("path");
const { LIB_VERSION } = require("./lib");
LIB_VERSION)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCJSWithModuleDetectionForce, TestAutoImportCJSWithModuleDetectionForce);

// autoImportCompletionAmbientMergedModule1_test.go

// autoImportCompletionAmbientMergedModule1_test.go
static void TestAutoImportCompletionAmbientMergedModule1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @module: commonjs
// @filename: /node_modules/@types/vscode/index.d.ts
declare module "vscode" {
  export class Position {
    readonly line: number;
    readonly character: number;
  }
}
// @filename: src/motion.ts
import { Position } from "vscode";

export abstract class MoveQuoteMatch {
  public override async execActionWithCount(
    position: Position,
  ): Promise<void> {}
}

declare module "vscode" {
  interface Position {
    toString(): string;
  }
}
// @filename: src/smartQuotes.ts
import { MoveQuoteMatch } from "./motion";

export class MoveInsideNextQuote extends MoveQuoteMatch {/*1*/
  keys = ["i", "n", "q"];
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "execActionWithCount", .FilterText = std::string("execActionWithCount"), .InsertText = std::string(R"TS(public execActionWithCount(position: Position): Promise<void> {
})TS"), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.Source = "ClassMemberSnippet/"})})}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>("1"), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "execActionWithCount", .Source = "ClassMemberSnippet/", .Description = "Includes imports of types referenced by 'execActionWithCount'", .NewFileContent = std::make_shared<std::string>(R"TS(import { Position } from "vscode";
import { MoveQuoteMatch } from "./motion";

export class MoveInsideNextQuote extends MoveQuoteMatch {
  keys = ["i", "n", "q"];
})TS"), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCompletionAmbientMergedModule1, TestAutoImportCompletionAmbientMergedModule1);

// autoImportCompletionExportEqualsWithDefault1_test.go

// autoImportCompletionExportEqualsWithDefault1_test.go
static void TestAutoImportCompletionExportEqualsWithDefault1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @module: commonjs
// @esModuleInterop: false
// @allowSyntheticDefaultImports: false
// @filename: node.ts
import Container from "./container.js";
import Document from "./document.js";

declare namespace Node {
  class Node extends Node_ {}

  export { Node as default };
}

declare abstract class Node_ {
  parent: Container | Document | undefined;
}

declare class Node extends Node_ {}

export = Node;
// @filename: document.ts
import Container from "./container.js";

declare namespace Document {
  export { Document_ as default };
}

declare class Document_ extends Container {}

declare class Document extends Document_ {}

export = Document;
// @filename: container.ts
import Node from "./node.js";

declare namespace Container {
  export { Container_ as default };
}

declare abstract class Container_ extends Node {
  p/*1*/
}

declare class Container extends Container_ {}

export = Container;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "parent", .FilterText = std::string("parent"), .InsertText = std::string("parent: Container_ | Document_ | undefined;"), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.Source = "ClassMemberSnippet/"})})}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>("1"), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "parent", .Source = "ClassMemberSnippet/", .Description = "Includes imports of types referenced by 'parent'", .NewFileContent = std::make_shared<std::string>(R"TS(import Document_ from "./document.js";
import Node from "./node.js";

declare namespace Container {
  export { Container_ as default };
}

declare abstract class Container_ extends Node {
  p
}

declare class Container extends Container_ {}

export = Container;)TS"), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCompletionExportEqualsWithDefault1, TestAutoImportCompletionExportEqualsWithDefault1);

// autoImportCompletionExportListAugmentation1_test.go

// autoImportCompletionExportListAugmentation1_test.go
static void TestAutoImportCompletionExportListAugmentation1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/@sapphire/pieces/index.d.ts
interface Container {
  stores: unknown;
}

declare class Piece {
  container: Container;
}

export { Piece, type Container };
// @FileName: /augmentation.ts
declare module "@sapphire/pieces" {
  interface Container {
    client: unknown;
  }
  export { Container };
}
// @Filename: /index.ts
import { Piece } from "@sapphire/pieces";
class FullPiece extends Piece {
  /*1*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "container", .FilterText = std::string("container"), .InsertText = std::string("container: Container;"), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.Source = "ClassMemberSnippet/"})})}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>("1"), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "container", .Source = "ClassMemberSnippet/", .Description = "Includes imports of types referenced by 'container'", .NewFileContent = std::make_shared<std::string>(R"TS(import { Container, Piece } from "@sapphire/pieces";
class FullPiece extends Piece {
  
})TS"), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCompletionExportListAugmentation1, TestAutoImportCompletionExportListAugmentation1);

// autoImportCompletionExportListAugmentation2_test.go

// autoImportCompletionExportListAugmentation2_test.go
static void TestAutoImportCompletionExportListAugmentation2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/@sapphire/pieces/index.d.ts
interface Container {
  stores: unknown;
}

declare class Piece {
  get container(): Container;
}

declare class AliasPiece extends Piece {}

export { AliasPiece, type Container };
// @Filename: /node_modules/@sapphire/framework/index.d.ts
import { AliasPiece } from "@sapphire/pieces";

declare class Command extends AliasPiece {}

declare module "@sapphire/pieces" {
  interface Container {
    client: unknown;
  }
}

export { Command };
// @Filename: /index.ts
import "@sapphire/pieces";
import { Command } from "@sapphire/framework";
class PingCommand extends Command {
  /*1*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "container", .FilterText = std::string("container"), .InsertText = std::string(R"TS(get container(): Container {
})TS"), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.Source = "ClassMemberSnippet/"})})}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>("1"), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "container", .Source = "ClassMemberSnippet/", .Description = "Includes imports of types referenced by 'container'", .NewFileContent = std::make_shared<std::string>(R"TS(import "@sapphire/pieces";
import { Command } from "@sapphire/framework";
import { Container } from "@sapphire/pieces";
class PingCommand extends Command {
  
})TS"), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCompletionExportListAugmentation2, TestAutoImportCompletionExportListAugmentation2);

// autoImportCompletionExportListAugmentation3_test.go

// autoImportCompletionExportListAugmentation3_test.go
static void TestAutoImportCompletionExportListAugmentation3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/@sapphire/pieces/index.d.ts
export interface Container {
  stores: unknown;
}

declare class Piece {
  container: Container;
}

export { Piece };
// @FileName: /augmentation.ts
declare module "@sapphire/pieces" {
  interface Container {
    client: unknown;
  }
}
// @Filename: /index.ts
import { Piece } from "@sapphire/pieces";
class FullPiece extends Piece {
  /*1*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "container", .FilterText = std::string("container"), .InsertText = std::string("container: Container;"), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.Source = "ClassMemberSnippet/"})})}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>("1"), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "container", .Source = "ClassMemberSnippet/", .Description = "Includes imports of types referenced by 'container'", .NewFileContent = std::make_shared<std::string>(R"TS(import { Container, Piece } from "@sapphire/pieces";
class FullPiece extends Piece {
  
})TS"), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCompletionExportListAugmentation3, TestAutoImportCompletionExportListAugmentation3);

// autoImportCompletionExportListAugmentation4_test.go

// autoImportCompletionExportListAugmentation4_test.go
static void TestAutoImportCompletionExportListAugmentation4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/@sapphire/pieces/index.d.ts
interface Container {
  stores: unknown;
}

declare class Piece {
  get container(): Container;
}

export { Piece as Alias, type Container };
// @Filename: /node_modules/@sapphire/framework/index.d.ts
import { Alias } from "@sapphire/pieces";

declare class Command extends Alias {}

declare module "@sapphire/pieces" {
  interface Container {
    client: unknown;
  }
}

export { Command as CommandAlias };
// @Filename: /index.ts
import "@sapphire/pieces";
import { CommandAlias } from "@sapphire/framework";
class PingCommand extends CommandAlias {
  /*1*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "container", .FilterText = std::string("container"), .InsertText = std::string(R"TS(get container(): Container {
})TS"), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.Source = "ClassMemberSnippet/"})})}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>("1"), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "container", .Source = "ClassMemberSnippet/", .Description = "Includes imports of types referenced by 'container'", .NewFileContent = std::make_shared<std::string>(R"TS(import "@sapphire/pieces";
import { CommandAlias } from "@sapphire/framework";
import { Container } from "@sapphire/pieces";
class PingCommand extends CommandAlias {
  
})TS"), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsWithClassMemberSnippets = Tristate::True})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCompletionExportListAugmentation4, TestAutoImportCompletionExportListAugmentation4);

// autoImportCompletion_test.go

// autoImportCompletion_test.go
static void TestAutoImportCompletion1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
export const someVar = 10;

// @Filename: b.ts
export const anotherVar = 10;

// @Filename: c.ts
import {someVar} from "./a.ts";
someVar;
a/**/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"someVar", "anotherVar"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Excludes = std::vector<std::string>{"anotherVar"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::False, .IncludeCompletionsForImportStatements = Tristate::False})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCompletion1, TestAutoImportCompletion1);

// autoImportCompletion_test.go
static void TestAutoImportCompletion2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
export const someVar = 10;
export const anotherVar = 10;

// @Filename: c.ts
import {someVar} from "./a.ts";
someVar;
a/**/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"someVar", "anotherVar"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCompletion2, TestAutoImportCompletion2);

// autoImportCompletion_test.go
static void TestAutoImportCompletion3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
export const aa = "asdf";
export const someVar = 10;
export const bb = 10;

// @Filename: c.ts
import { aa, someVar } from "./a.ts";
someVar;
b/**/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"bb"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCompletion3, TestAutoImportCompletion3);

// autoImportCompletionsForArbitraryNonIdentifierExports_test.go

// autoImportCompletionsForArbitraryNonIdentifierExports_test.go
static void TestAutoImportCompletionsForArbitraryNonIdentifierExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @module: esnext
// @Filename: /a.ts
const foo = 0;
export { foo as "foo-bar" };
export const fooBar = 1;

// @Filename: /b.ts
foo/**/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"fooBar"}, .Excludes = std::vector<std::string>{"foo-bar"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCompletionsForArbitraryNonIdentifierExports, TestAutoImportCompletionsForArbitraryNonIdentifierExports);

// autoImportCrossPackage_pathsAndSymlink_test.go

// autoImportCrossPackage_pathsAndSymlink_test.go
static void TestAutoImportCrossPackage_pathsAndSymlink(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/packages/common/package.json
{
  "name": "@company/common",
  "version": "1.0.0",
  "main": "./lib/index.tsx"
}
// @Filename: /home/src/workspaces/project/packages/common/lib/index.tsx
export function Tooltip {};
// @Filename: /home/src/workspaces/project/packages/app/package.json
{
  "name": "@company/app",
  "version": "1.0.0",
  "dependencies": {
    "@company/common": "1.0.0"
  }
}
// @Filename: /home/src/workspaces/project/packages/app/tsconfig.json
{
  "compilerOptions": {
    "composite": true,
    "lib": ["es5"],
    "module": "esnext",
    "moduleResolution": "bundler",
    "paths": {
      "@/*": ["./*"]
    }
  }
}
// @Filename: /home/src/workspaces/project/packages/app/lib/index.ts
Tooltip/**/
// @link: /home/src/workspaces/project/packages/common -> /home/src/workspaces/project/node_modules/@company/common)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"@company/common"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossPackage_pathsAndSymlink, TestAutoImportCrossPackage_pathsAndSymlink);

// autoImportCrossProjectNodeModules_test.go

// autoImportCrossProjectNodeModules_test.go
static void TestAutoImportCrossProjectNodeModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/pkg-listed/package.json
{ "name": "pkg-listed", "version": "1.0.0" }
// @Filename: /node_modules/pkg-listed/index.d.ts
export declare const pkg_listed_value: number;
// @Filename: /node_modules/pkg-unlisted/package.json
{ "name": "pkg-unlisted", "version": "1.0.0" }
// @Filename: /node_modules/pkg-unlisted/index.d.ts
export declare const pkg_unlisted_value: string;
// @Filename: /project-a/tsconfig.json
{ "compilerOptions": { "module": "commonjs", "strict": true } }
// @Filename: /project-a/package.json
{ "name": "project-a", "dependencies": { "pkg-listed": "*" } }
// @Filename: /project-a/index.ts
import { pkg_unlisted_value } from "pkg-unlisted";
console.log(pkg_unlisted_value);
// @Filename: /project-b/tsconfig.json
{ "compilerOptions": { "module": "commonjs", "strict": true } }
// @Filename: /project-b/package.json
{ "name": "project-b", "dependencies": { "pkg-listed": "*" } }
// @Filename: /project-b/index.ts
pkg_/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/project-a/index.ts");
		f->GoToMarker(t, "");
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossProjectNodeModules, TestAutoImportCrossProjectNodeModules);

// autoImportCrossProject_baseUrl_toDist_test.go

// autoImportCrossProject_baseUrl_toDist_test.go
static void TestAutoImportCrossProject_baseUrl_toDist(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/common/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "commonjs",
    "outDir": "dist",
    "composite": true
  },
  "include": ["src"]
}
// @Filename: /home/src/workspaces/project/common/src/MyModule.ts
export function square(n: number) {
  return n * 2;
}
// @Filename: /home/src/workspaces/project/web/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "esnext",
    "moduleResolution": "node",
    "noEmit": true,
    "baseUrl": "."
  },
  "include": ["src"],
  "references": [{ "path": "../common" }]
}
// @Filename: /home/src/workspaces/project/web/src/MyApp.ts
import { square } from "../../common/dist/src/MyModule";
// @Filename: /home/src/workspaces/project/web/src/Helper.ts
export function saveMe() {
  square/**/(2);
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToFile(t, "/home/src/workspaces/project/web/src/Helper.ts");
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"../../common/src/MyModule"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "non-relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossProject_baseUrl_toDist, TestAutoImportCrossProject_baseUrl_toDist);

// autoImportCrossProject_paths_sharedOutDir_test.go

// autoImportCrossProject_paths_sharedOutDir_test.go
static void TestAutoImportCrossProject_paths_sharedOutDir(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.base.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "commonjs",
    "baseUrl": ".",
    "paths": {
      "packages/*": ["./packages/*"]
    }
  }
}
// @Filename: /home/src/workspaces/project/packages/app/tsconfig.json
{
  "extends": "../../tsconfig.base.json",
  "compilerOptions": { "outDir": "../../dist/packages/app" },
  "references": [{ "path": "../dep" }]
}
// @Filename: /home/src/workspaces/project/packages/app/index.ts
dep/**/
// @Filename: /home/src/workspaces/project/packages/app/utils.ts
import "packages/dep";
// @Filename: /home/src/workspaces/project/packages/dep/tsconfig.json
{
  "extends": "../../tsconfig.base.json",
  "compilerOptions": { "outDir": "../../dist/packages/dep" }
}
// @Filename: /home/src/workspaces/project/packages/dep/index.ts
import "./sub/folder";
// @Filename: /home/src/workspaces/project/packages/dep/sub/folder/index.ts
export const dep = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { dep } from "packages/dep/sub/folder";

dep)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossProject_paths_sharedOutDir, TestAutoImportCrossProject_paths_sharedOutDir);

// autoImportCrossProject_paths_stripSrc_test.go

// autoImportCrossProject_paths_stripSrc_test.go
static void TestAutoImportCrossProject_paths_stripSrc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/packages/app/package.json
{ "name": "app", "dependencies": { "dep": "*" } }
// @Filename: /home/src/workspaces/project/packages/app/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "commonjs",
    "outDir": "dist",
    "rootDir": "src",
    "baseUrl": ".",
    "paths": {
      "dep": ["../dep/src/main"],
      "dep/*": ["../dep/src/*"]
    }
  }
  "references": [{ "path": "../dep" }]
}
// @Filename: /home/src/workspaces/project/packages/app/src/index.ts
dep1/*1*/;
// @Filename: /home/src/workspaces/project/packages/app/src/utils.ts
dep2/*2*/;
// @Filename: /home/src/workspaces/project/packages/app/src/a.ts
import "dep";
// @Filename: /home/src/workspaces/project/packages/dep/package.json
{ "name": "dep", "main": "dist/main.js", "types": "dist/main.d.ts" }
// @Filename: /home/src/workspaces/project/packages/dep/tsconfig.json
{
  "compilerOptions": { "lib": ["es5"], "outDir": "dist", "rootDir": "src", "module": "commonjs" }
}
// @Filename: /home/src/workspaces/project/packages/dep/src/main.ts
import "./sub/folder";
export const dep1 = 0;
// @Filename: /home/src/workspaces/project/packages/dep/src/sub/folder/index.ts
export const dep2 = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "1");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { dep1 } from "dep";

dep1;)TS"}, nullptr);
		f->GoToMarker(t, "2");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { dep2 } from "dep/sub/folder";

dep2;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossProject_paths_stripSrc, TestAutoImportCrossProject_paths_stripSrc);

// autoImportCrossProject_paths_toDist2_test.go

// autoImportCrossProject_paths_toDist2_test.go
static void TestAutoImportCrossProject_paths_toDist2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/common/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "commonjs",
    "outDir": "dist",
    "composite": true
  },
  "include": ["src"]
}
// @Filename: /home/src/workspaces/project/common/src/MyModule.ts
export function square(n: number) {
  return n * 2;
}
// @Filename: /home/src/workspaces/project/web/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "esnext",
    "moduleResolution": "node",
    "noEmit": true,
    "paths": {
      "@common/*": ["../common/dist/src/*"]
    }
  },
  "include": ["src"],
  "references": [{ "path": "../common" }]
}
// @Filename: /home/src/workspaces/project/web/src/MyApp.ts
import { square } from "@common/MyModule";
// @Filename: /home/src/workspaces/project/web/src/Helper.ts
export function saveMe() {
  square/**/(2);
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToFile(t, "/home/src/workspaces/project/web/src/Helper.ts");
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"@common/MyModule"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "non-relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossProject_paths_toDist2, TestAutoImportCrossProject_paths_toDist2);

// autoImportCrossProject_paths_toDist_test.go

// autoImportCrossProject_paths_toDist_test.go
static void TestAutoImportCrossProject_paths_toDist(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/packages/app/package.json
{ "name": "app", "dependencies": { "dep": "*" } }
// @Filename: /home/src/workspaces/project/packages/app/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "commonjs",
    "outDir": "dist",
    "rootDir": "src",
    "baseUrl": ".",
    "paths": {
      "dep": ["../dep/src/main"],
      "dep/dist/*": ["../dep/src/*"]
    }
  }
  "references": [{ "path": "../dep" }]
}
// @Filename: /home/src/workspaces/project/packages/app/src/index.ts
dep1/*1*/;
// @Filename: /home/src/workspaces/project/packages/app/src/utils.ts
dep2/*2*/;
// @Filename: /home/src/workspaces/project/packages/app/src/a.ts
import "dep";
// @Filename: /home/src/workspaces/project/packages/dep/package.json
{ "name": "dep", "main": "dist/main.js", "types": "dist/main.d.ts" }
// @Filename: /home/src/workspaces/project/packages/dep/tsconfig.json
{
  "compilerOptions": { "lib": ["es5"], "outDir": "dist", "rootDir": "src", "module": "commonjs" }
}
// @Filename: /home/src/workspaces/project/packages/dep/src/main.ts
import "./sub/folder";
export const dep1 = 0;
// @Filename: /home/src/workspaces/project/packages/dep/src/sub/folder/index.ts
export const dep2 = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "1");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { dep1 } from "dep";

dep1;)TS"}, nullptr);
		f->GoToMarker(t, "2");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { dep2 } from "dep/dist/sub/folder";

dep2;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossProject_paths_toDist, TestAutoImportCrossProject_paths_toDist);

// autoImportCrossProject_paths_toSrc_test.go

// autoImportCrossProject_paths_toSrc_test.go
static void TestAutoImportCrossProject_paths_toSrc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/packages/app/package.json
{ "name": "app", "dependencies": { "dep": "*" } }
// @Filename: /home/src/workspaces/project/packages/app/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "commonjs",
    "outDir": "dist",
    "rootDir": "src",
    "baseUrl": ".",
    "paths": {
      "dep": ["../dep/src/main"],
      "dep/*": ["../dep/*"]
    }
  }
  "references": [{ "path": "../dep" }]
}
// @Filename: /home/src/workspaces/project/packages/app/src/index.ts
dep1/*1*/;
// @Filename: /home/src/workspaces/project/packages/app/src/utils.ts
dep2/*2*/;
// @Filename: /home/src/workspaces/project/packages/app/src/a.ts
import "dep";
// @Filename: /home/src/workspaces/project/packages/dep/package.json
{ "name": "dep", "main": "dist/main.js", "types": "dist/main.d.ts" }
// @Filename: /home/src/workspaces/project/packages/dep/tsconfig.json
{
  "compilerOptions": { "lib": ["es5"], "outDir": "dist", "rootDir": "src", "module": "commonjs" }
}
// @Filename: /home/src/workspaces/project/packages/dep/src/main.ts
import "./sub/folder";
export const dep1 = 0;
// @Filename: /home/src/workspaces/project/packages/dep/src/sub/folder/index.ts
export const dep2 = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "1");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { dep1 } from "dep";

dep1;)TS"}, nullptr);
		f->GoToMarker(t, "2");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { dep2 } from "dep/src/sub/folder";

dep2;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossProject_paths_toSrc, TestAutoImportCrossProject_paths_toSrc);

// autoImportCrossProject_symlinks_stripSrc_test.go

// autoImportCrossProject_symlinks_stripSrc_test.go
static void TestAutoImportCrossProject_symlinks_stripSrc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/packages/app/package.json
{ "name": "app", "dependencies": { "dep": "*" } }
// @Filename: /home/src/workspaces/project/packages/app/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "commonjs",
    "outDir": "dist",
    "rootDir": "src",
    "baseUrl": ".",
    "paths": {
      "dep/*": ["../dep/src/*"]  
    }
  }
  "references": [{ "path": "../dep" }]
}
// @Filename: /home/src/workspaces/project/packages/app/src/index.ts
dep/**/
// @Filename: /home/src/workspaces/project/packages/dep/package.json
{ "name": "dep", "main": "dist/index.js", "types": "dist/index.d.ts" }
// @Filename: /home/src/workspaces/project/packages/dep/tsconfig.json
{
  "compilerOptions": { "lib": ["es5"], "outDir": "dist", "rootDir": "src", "module": "commonjs" }
}
// @Filename: /home/src/workspaces/project/packages/dep/src/index.ts
import "./sub/folder";
// @Filename: /home/src/workspaces/project/packages/dep/src/sub/folder/index.ts
export const dep = 0;
// @link: /home/src/workspaces/project/packages/dep -> /home/src/workspaces/project/packages/app/node_modules/dep)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { dep } from "dep/sub/folder";

dep)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossProject_symlinks_stripSrc, TestAutoImportCrossProject_symlinks_stripSrc);

// autoImportCrossProject_symlinks_toDist_test.go

// autoImportCrossProject_symlinks_toDist_test.go
static void TestAutoImportCrossProject_symlinks_toDist(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/packages/app/package.json
{ "name": "app", "dependencies": { "dep": "*" } }
// @Filename: /home/src/workspaces/project/packages/app/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "commonjs",
    "outDir": "dist",
    "rootDir": "src",
    "baseUrl": ".",
    "paths": {
      "dep/dist/*": ["../dep/src/*"]  
    }
  }
  "references": [{ "path": "../dep" }]
}
// @Filename: /home/src/workspaces/project/packages/app/src/index.ts
dep/**/
// @Filename: /home/src/workspaces/project/packages/dep/package.json
{ "name": "dep", "main": "dist/index.js", "types": "dist/index.d.ts" }
// @Filename: /home/src/workspaces/project/packages/dep/tsconfig.json
{
  "compilerOptions": { "lib": ["es5"], "outDir": "dist", "rootDir": "src", "module": "commonjs" }
}
// @Filename: /home/src/workspaces/project/packages/dep/src/index.ts
import "./sub/folder";
// @Filename: /home/src/workspaces/project/packages/dep/src/sub/folder/index.ts
export const dep = 0;
// @link: /home/src/workspaces/project/packages/dep -> /home/src/workspaces/project/packages/app/node_modules/dep)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { dep } from "dep/dist/sub/folder";

dep)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossProject_symlinks_toDist, TestAutoImportCrossProject_symlinks_toDist);

// autoImportCrossProject_symlinks_toSrc_test.go

// autoImportCrossProject_symlinks_toSrc_test.go
static void TestAutoImportCrossProject_symlinks_toSrc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/packages/app/package.json
{ "name": "app", "dependencies": { "dep": "*" } }
// @Filename: /home/src/workspaces/project/packages/app/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "commonjs",
    "outDir": "dist",
    "rootDir": "src",
    "baseUrl": "."
  }
  "references": [{ "path": "../dep" }]
}
// @Filename: /home/src/workspaces/project/packages/app/src/index.ts
dep/**/
// @Filename: /home/src/workspaces/project/packages/dep/package.json
{ "name": "dep", "main": "dist/index.js", "types": "dist/index.d.ts" }
// @Filename: /home/src/workspaces/project/packages/dep/tsconfig.json
{
  "compilerOptions": { "lib": ["es5"], "outDir": "dist", "rootDir": "src", "module": "commonjs" }
}
// @Filename: /home/src/workspaces/project/packages/dep/src/index.ts
import "./sub/folder";
// @Filename: /home/src/workspaces/project/packages/dep/src/sub/folder/index.ts
export const dep = 0;
// @link: /home/src/workspaces/project/packages/dep -> /home/src/workspaces/project/packages/app/node_modules/dep)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.AutoImportEntrypointDirectorySearch = Tristate::True});
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { dep } from "dep/src/sub/folder";

dep)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCrossProject_symlinks_toSrc, TestAutoImportCrossProject_symlinks_toSrc);

// autoImportCssModule_test.go

// autoImportCssModule_test.go
static void TestAutoImportCssModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /tsconfig.json
{ "compilerOptions": { "module": "nodenext", "moduleResolution": "nodenext" } }

// @Filename: /package.json
{ "type": "module" }

// @Filename: /augmentations.ts
export {};
declare module "./styles.css" {
    export const myClass: string;
}

// @Filename: /index.ts
myClass/**/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "myClass", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "./styles.css"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportCssModule, TestAutoImportCssModule);

// autoImportDefaultPascalCase_test.go

// autoImportDefaultPascalCase_test.go
static void TestAutoImportDefaultPascalCase(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @module: esnext
// @moduleResolution: bundler

// @Filename: /src/components/ChargerHeader.tsx
export default function ChargerHeader() {
  return null;
}

// @Filename: /src/screens/SomeScreen.tsx
export function SomeScreen() {
  return <ChargerHeader/*1*/
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"ChargerHeader"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportDefaultPascalCase, TestAutoImportDefaultPascalCase);

// autoImportDefaultPascalCase_test.go
static void TestAutoImportDefaultPascalCaseAnonymous(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @module: esnext
// @moduleResolution: bundler

// @Filename: /src/components/ChargerHeader.tsx
export default function() {
  return null;
}

// @Filename: /src/screens/SomeScreen.tsx
export function SomeScreen() {
  return <ChargerHeader/*1*/
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"ChargerHeader"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportDefaultPascalCaseAnonymous, TestAutoImportDefaultPascalCaseAnonymous);

// autoImportDefaultPascalCase_test.go
static void TestAutoImportDefaultPascalCaseCaseInsensitive(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @module: esnext
// @moduleResolution: bundler
// @useCaseSensitiveFileNames: false

// @Filename: /src/components/ChargerHeader.tsx
export default function ChargerHeader() {
  return null;
}

// @Filename: /src/screens/SomeScreen.tsx
export function SomeScreen() {
  return <ChargerHeader/*1*/
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"ChargerHeader"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportDefaultPascalCaseCaseInsensitive, TestAutoImportDefaultPascalCaseCaseInsensitive);

// autoImportDefaultPascalCase_test.go
static void TestAutoImportDefaultPascalCaseAnonymousCaseInsensitive(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @module: esnext
// @moduleResolution: bundler
// @useCaseSensitiveFileNames: false

// @Filename: /src/components/ChargerHeader.tsx
export default function() {
  return null;
}

// @Filename: /src/screens/SomeScreen.tsx
export function SomeScreen() {
  return <ChargerHeader/*1*/
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"ChargerHeader"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportDefaultPascalCaseAnonymousCaseInsensitive, TestAutoImportDefaultPascalCaseAnonymousCaseInsensitive);

// autoImportDefaultPascalCase_test.go
static void TestAutoImportDefaultPascalCaseReexportCaseInsensitive(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @module: esnext
// @moduleResolution: bundler
// @useCaseSensitiveFileNames: false

// @Filename: /src/components/ChargerHeader.tsx
export default function() {
  return null;
}

// @Filename: /src/components/index.ts
export { default } from "./ChargerHeader";

// @Filename: /src/screens/SomeScreen.tsx
export function SomeScreen() {
  return <ChargerHeader/*1*/
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"ChargerHeader"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportDefaultPascalCaseReexportCaseInsensitive, TestAutoImportDefaultPascalCaseReexportCaseInsensitive);

// autoImportDefaultPascalCase_test.go
static void TestAutoImportDefaultPascalCaseAliasCaseInsensitive(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @module: esnext
// @moduleResolution: bundler
// @useCaseSensitiveFileNames: false

// @Filename: /src/components/ChargerHeader.tsx
function ChargerHeader() {
  return null;
}
export default ChargerHeader;

// @Filename: /src/screens/SomeScreen.tsx
export function SomeScreen() {
  return <ChargerHeader/*1*/
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"ChargerHeader"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportDefaultPascalCaseAliasCaseInsensitive, TestAutoImportDefaultPascalCaseAliasCaseInsensitive);

// autoImportErrorMixedExportKinds_test.go

// autoImportErrorMixedExportKinds_test.go
static void TestAutoImportErrorMixedExportKinds(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
export function foo(): number {
	return 10
}

const bar = 20;
export { bar as foo };

// @Filename: b.ts
foo/**/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportErrorMixedExportKinds, TestAutoImportErrorMixedExportKinds);

// autoImportExportEqualsOfImportStar_test.go

// autoImportExportEqualsOfImportStar_test.go
static void TestAutoImportExportEqualsOfImportStar(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: /node_modules/mdx/package.json
{ "name": "mdx", "version": "1.0.0", "types": "index.d.ts" }
// @Filename: /node_modules/mdx/index.d.ts
import * as mdx from './lib/index.js'

export = mdx
// @Filename: /node_modules/mdx/lib/index.d.ts
export * from './core.js'
export * from './compile.js'
// @Filename: /node_modules/mdx/lib/core.d.ts
export declare function core(): void
// @Filename: /node_modules/mdx/lib/compile.d.ts
export declare function compile(): void
// @Filename: /package.json
{ "dependencies": { "mdx": "*" } }
// @Filename: /index.ts
mdx/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportExportEqualsOfImportStar, TestAutoImportExportEqualsOfImportStar);

// autoImportFileExcludePatterns10_test.go

// autoImportFileExcludePatterns10_test.go
static void TestAutoImportFileExcludePatterns10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/vs/test.ts
import { Parts } from './parts';
export class /**/Extended implements Parts {
}
// @Filename: /src/vs/parts.ts
import { Event } from '../event/event';

export interface Parts {
	readonly options: Event;
}
// @Filename: /src/event/event.ts
export interface Event {
	(): string;
}
// @Filename: /src/thing.ts
import { Event } from './event/event';
export { Event };
// @Filename: /src/a.ts
import './thing'
declare module './thing' {
	interface Event {
		c: string;
	}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = "Implement interface 'Parts'", .NewFileContent = R"TS(import { Event } from '../event/event';
import { Parts } from './parts';
export class Extended implements Parts {
    options: Event;
})TS", .Index = 0, .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"src/thing.ts"}})});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns10, TestAutoImportFileExcludePatterns10);

// autoImportFileExcludePatterns11_test.go

// autoImportFileExcludePatterns11_test.go
static void TestAutoImportFileExcludePatterns11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/vs/test.ts
import { Parts } from './parts';
export class /**/Extended implements Parts {
}
// @Filename: /src/vs/parts.ts
import { Event } from '../thing';
export interface Parts {
	readonly options: Event;
}
// @Filename: /src/event/event.ts
export interface Event {
	(): string;
}
// @Filename: /src/thing.ts
import { Event } from './event/event';
export { Event };
// @Filename: /src/a.ts
import './thing'
declare module './thing' {
	interface Event {
		c: string;
	}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = "Implement interface 'Parts'", .NewFileContent = R"TS(import { Event } from '../event/event';
import { Parts } from './parts';
export class Extended implements Parts {
    options: Event;
})TS", .Index = 0, .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"src/thing.ts"}})});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns11, TestAutoImportFileExcludePatterns11);

// autoImportFileExcludePatterns12_test.go

// autoImportFileExcludePatterns12_test.go
static void TestAutoImportFileExcludePatterns12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/vs/test.ts
import { Parts } from './parts';
export class /**/Extended implements Parts {
}
// @Filename: /src/vs/parts.ts
import { Event } from '../thing';
export interface Parts {
	readonly options: Event;
}
// @Filename: /src/event/event.ts
export interface Event {
	(): string;
}
// @Filename: /src/thing.ts
import { Event } from '../event/event';
export { Event };
// @Filename: /src/a.ts
import './thing'
declare module './thing' {
	interface Event {
		c: string;
	}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = "Implement interface 'Parts'", .NewFileContent = R"TS(import { Parts } from './parts';
export class Extended implements Parts {
    options: Event;
})TS", .Index = 0, .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"src/thing.ts"}})});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns12, TestAutoImportFileExcludePatterns12);

// autoImportFileExcludePatterns13_test.go

// autoImportFileExcludePatterns13_test.go
static void TestAutoImportFileExcludePatterns13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/vs/test.ts
import { Parts } from './parts';
export class /**/Extended implements Parts {
}
// @Filename: /src/vs/parts.ts
import { Event } from '../event/event';

export interface Parts {
	readonly options: Event;
}
// @Filename: /src/event/event.ts
export interface Event {
	(): string;
}
// @Filename: /src/thing.ts
import { Event } from '../event/event';
export { Event };
// @Filename: /src/a.ts
import './thing'
declare module './thing' {
	interface Event {
		c: string;
	}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = "Implement interface 'Parts'", .NewFileContent = R"TS(import { Event } from '../event/event';
import { Parts } from './parts';
export class Extended implements Parts {
    options: Event;
})TS", .Index = 0, .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"src/thing.ts"}})});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns13, TestAutoImportFileExcludePatterns13);

// autoImportFileExcludePatterns2_test.go

// autoImportFileExcludePatterns2_test.go
static void TestAutoImportFileExcludePatterns2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: /lib/components/button/Button.ts
export function Button() {}
// @Filename: /lib/components/button/index.ts
export * from "./Button";
// @Filename: /lib/components/index.ts
export * from "./button";
// @Filename: /lib/main.ts
export { Button } from "./components";
// @Filename: /lib/index.ts
export * from "./main";
// @Filename: /i-hate-index-files.ts
Button/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalsPlus(std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Button", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "./lib/main"})})})}, false)}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"/**/index.*"}})}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./lib/main", "./lib/components/button/Button"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"/**/index.*"}}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns2, TestAutoImportFileExcludePatterns2);

// autoImportFileExcludePatterns3_test.go

// autoImportFileExcludePatterns3_test.go
static void TestAutoImportFileExcludePatterns3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @module: commonjs
// @Filename: /ambient1.d.ts
declare module "foo" {
   export const x = 1;
}
// @Filename: /ambient2.d.ts
declare module "foo" {
   export const y = 2;
}
// @Filename: /index.ts
/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalsPlus(std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "foo"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "y", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "foo"})})})}, false)}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"/**/ambient1.d.ts"}})}));
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobals}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"/**/ambient*"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns3, TestAutoImportFileExcludePatterns3);

// autoImportFileExcludePatterns4_test.go

// autoImportFileExcludePatterns4_test.go
static void TestAutoImportFileExcludePatterns4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/vs/workbench/test.ts
import { Parts } from './parts';
export class /**/EditorParts implements Parts { }
// @Filename: /src/vs/event/event.ts
export interface Event {
	(): string;
}
// @Filename: /src/vs/workbench/parts.ts
import { Event } from '../event/event';
export interface Parts {
	readonly options: Event;
}
// @Filename: /src/vs/workbench/workbench.ts
import { Event } from '../event/event';
export { Event };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = "Implement interface 'Parts'", .NewFileContent = R"TS(import { Event } from '../event/event';
import { Parts } from './parts';
export class EditorParts implements Parts {
    options: Event;
})TS", .Index = 0, .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"src/vs/workbench/workbench.ts"}})});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns4, TestAutoImportFileExcludePatterns4);

// autoImportFileExcludePatterns5_test.go

// autoImportFileExcludePatterns5_test.go
static void TestAutoImportFileExcludePatterns5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/vs/workbench/test.ts
import { Parts } from './parts';
export class /**/EditorParts implements Parts { }
// @Filename: /src/vs/event/event.ts
export interface Event {
	(): string;
}
// @Filename: /src/vs/workbench/parts.ts
import { Event } from '../event/event';
export interface Parts {
	readonly options: Event;
}
// @Filename: /src/vs/workbench/workbench.ts
import { Event } from '../event/event';
export { Event };
// @Filename: /src/vs/workbench/canImport.ts
import { Event } from '../event/event';
export { Event };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = "Implement interface 'Parts'", .NewFileContent = R"TS(import { Event } from './canImport';
import { Parts } from './parts';
export class EditorParts implements Parts {
    options: Event;
})TS", .Index = 0, .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"src/vs/workbench/workbench.ts"}})});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns5, TestAutoImportFileExcludePatterns5);

// autoImportFileExcludePatterns6_test.go

// autoImportFileExcludePatterns6_test.go
static void TestAutoImportFileExcludePatterns6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/vs/workbench/test.ts
import { Parts } from './parts';
export class /**/EditorParts implements Parts { }
// @Filename: /src/vs/event/event.ts
export interface Event {
	(): string;
}
// @Filename: /src/vs/workbench/parts.ts
import { Event } from '../event/event';
export interface Parts {
	readonly options: Event;
}
// @Filename: /src/vs/workbench/workbench.ts
import { Event } from './canImport';
export { Event };
// @Filename: /src/vs/workbench/canImport.ts
import { Event } from '../event/event';
export { Event };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = "Implement interface 'Parts'", .NewFileContent = R"TS(import { Event } from './canImport';
import { Parts } from './parts';
export class EditorParts implements Parts {
    options: Event;
})TS", .Index = 0, .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"src/vs/workbench/workbench.ts"}})});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns6, TestAutoImportFileExcludePatterns6);

// autoImportFileExcludePatterns7_test.go

// autoImportFileExcludePatterns7_test.go
static void TestAutoImportFileExcludePatterns7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/vs/workbench/test.ts
import { Parts } from './parts';
export class /**/EditorParts implements Parts { }
// @Filename: /src/vs/event/event.ts
export interface Event {
	(): string;
}
// @Filename: /src/vs/workbench/parts.ts
import { Event } from '../event/event';
export interface Parts {
	readonly options: Event;
}
// @Filename: /src/vs/workbench/workbench.ts
import { Event } from '../event/event';
export { Event };
// @Filename: /src/vs/workbench/workbench2.ts
import { Event } from '../event/event';
export { Event };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = "Implement interface 'Parts'", .NewFileContent = R"TS(import { Event } from '../event/event';
import { Parts } from './parts';
export class EditorParts implements Parts {
    options: Event;
})TS", .Index = 0, .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"src/vs/workbench/workbench*"}})});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns7, TestAutoImportFileExcludePatterns7);

// autoImportFileExcludePatterns8_test.go

// autoImportFileExcludePatterns8_test.go
static void TestAutoImportFileExcludePatterns8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/vs/workbench/test.ts
import { Parts } from './parts';
export class /**/EditorParts implements Parts { }
// @Filename: /src/vs/event/event.ts
export interface Event {
	(): string;
}
// @Filename: /src/vs/workbench/parts.ts
import { Event } from '../event/event';
export interface Parts {
	readonly options: Event;
}
// @Filename: /src/vs/workbench/workbench.ts
import { Event } from './workbench2';
export { Event };
// @Filename: /src/vs/workbench/workbench2.ts
import { Event } from '../event/event';
export { Event };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = "Implement interface 'Parts'", .NewFileContent = R"TS(import { Event } from '../event/event';
import { Parts } from './parts';
export class EditorParts implements Parts {
    options: Event;
})TS", .Index = 0, .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"src/vs/workbench/workbench*"}})});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns8, TestAutoImportFileExcludePatterns8);

// autoImportFileExcludePatterns9_test.go

// autoImportFileExcludePatterns9_test.go
static void TestAutoImportFileExcludePatterns9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/vs/workbench/test.ts
import { Parts } from './parts';
export class /**/EditorParts implements Parts { }
// @Filename: /src/vs/event/event.ts
export interface Event {
	(): string;
}
// @Filename: /src/vs/workbench/parts.ts
import { Event } from '../event/event';
export interface Parts {
	readonly options: Event;
}
// @Filename: /src/vs/workbench/workbench.ts
import { Event } from '../event/event';
export { Event };
// @Filename: /src/vs/test.ts
import { Event } from './event/event';
export { Event };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = "Implement interface 'Parts'", .NewFileContent = R"TS(import { Event } from '../test';
import { Parts } from './parts';
export class EditorParts implements Parts {
    options: Event;
})TS", .Index = 0, .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportFileExcludePatterns = std::vector<std::string>{"src/vs/workbench/workbench*"}})});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns9, TestAutoImportFileExcludePatterns9);

// autoImportFileExcludePatterns_test.go

// autoImportFileExcludePatterns_test.go
static void TestAutoImportFileExcludePatterns(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export const mySymbol = 1;
// @Filename: ignoreme.ts
export const ignoredSymbol = 2;
// @Filename: bar.ts
mySym/*1*/
ignoredSym/*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True, .AutoImportFileExcludePatterns = std::vector<std::string>{"*ignoreme.ts"}});
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"mySymbol"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Excludes = std::vector<std::string>{"ignoredSymbol"}})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportFileExcludePatterns, TestAutoImportFileExcludePatterns);

// autoImportIndexReExportPrefix_test.go

// autoImportIndexReExportPrefix_test.go
static void TestAutoImportIndexReExportPrefix(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: nodenext
// @Filename: /package.json
{ "type": "module" }
// @Filename: /utils/sum/index.ts
export { sum } from "./sum.js";
// @Filename: /utils/sum/sum.ts
export const sum = 0;
// @Filename: /utils/sumAB.ts
sum/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./sum/index.js", "./sum/sum.js"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierEnding = "js"}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportIndexReExportPrefix, TestAutoImportIndexReExportPrefix);

// autoImportJsDocImport1_test.go

// autoImportJsDocImport1_test.go
static void TestAutoImportJsDocImport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @verbatimModuleSyntax: true
// @target: esnext
// @allowJs: true
// @checkJs: true
// @Filename: /foo.ts
 export const A = 1;
 export type B = { x: number };
 export type C = 1;
 export class D { y: string }
// @Filename: /test.js
/**
 * @import { A, D, C } from "./foo"
 */

/**
 * @param { typeof A } a
 * @param { B/**/ | C } b
 * @param { C } c
 * @param { D } d
 */
export function f(a, b, c, d) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(/**
 * @import { A, D, C, B } from "./foo"
 */

/**
 * @param { typeof A } a
 * @param { B | C } b
 * @param { C } c
 * @param { D } d
 */
export function f(a, b, c, d) { })TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportJsDocImport1, TestAutoImportJsDocImport1);

// autoImportModuleAugmentation_test.go

// autoImportModuleAugmentation_test.go
static void TestAutoImportModuleAugmentation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export interface Foo {
    x: number;
}

// @Filename: /b.ts
export {};
declare module "./a" {
    export const Foo: any;
}

// @Filename: /c.ts
Foo/**/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportModuleAugmentation, TestAutoImportModuleAugmentation);

// autoImportModuleNone2_test.go

// autoImportModuleNone2_test.go
static void TestAutoImportModuleNone2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @module: none
// @moduleResolution: bundler
// @target: es2015
// @Filename: /node_modules/dep/index.d.ts
export const x: number;
// @Filename: /index.ts
 x/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dep"})})})}})}));
		f->ReplaceLine(t, 0, "import { x } from 'dep'; x;");
		f->VerifyNonSuggestionDiagnostics(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportModuleNone2, TestAutoImportModuleNone2);

// autoImportNewLine_test.go

// autoImportNewLine_test.go
static void TestAutoImportNewLine(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export function readFileSync() {}

// @Filename: /b.ts
import {} from "./other1";
import {} from "./other2";


readFileSync/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportNewLine, TestAutoImportNewLine);

// autoImportNewLine_test.go
static void TestAutoImportNewLineWithHeaderComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export function readFileSync() {}

// @Filename: /b.ts
/* file header comment */
import {} from "./other1";
import {} from "./other2";


readFileSync/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportNewLineWithHeaderComment, TestAutoImportNewLineWithHeaderComment);

// autoImportNoPackageJson_commonjs_test.go

// autoImportNoPackageJson_commonjs_test.go
static void TestAutoImportNoPackageJson_commonjs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @module: commonjs
// @Filename: /node_modules/lit/index.d.cts
export declare function customElement(name: string): any;
// @Filename: /a.ts
customElement/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.AutoImportEntrypointDirectorySearch = Tristate::True});
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"lit/index.cjs"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportNoPackageJson_commonjs, TestAutoImportNoPackageJson_commonjs);

// autoImportNoPackageJson_nodenext_test.go

// autoImportNoPackageJson_nodenext_test.go
static void TestAutoImportNoPackageJson_nodenext(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @module: node18
// @Filename: /node_modules/lit/index.d.cts
export declare function customElement(name: string): any;
// @Filename: /a.ts
customElement/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.AutoImportEntrypointDirectorySearch = Tristate::True});
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"lit/index.cjs"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportNoPackageJson_nodenext, TestAutoImportNoPackageJson_nodenext);

// autoImportNodeBuiltinNodenext_test.go

// autoImportNodeBuiltinNodenext_test.go
static void TestAutoImportNodeBuiltinNodenext(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{ "compilerOptions": { "module": "nodenext", "types": ["node"] } }
// @Filename: /package.json
{ "type": "module" }
// @Filename: /node_modules/@types/node/package.json
{ "name": "@types/node", "version": "22.0.0" }
// @Filename: /node_modules/@types/node/index.d.ts
declare module "fs" {
    export function existsSync(path: string): boolean;
    export function mkdirSync(path: string, options?: { recursive?: boolean }): void;
}
declare module "node:fs" { export * from "fs"; }
// @Filename: /index.ts
existsSync/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "existsSync", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "node:fs"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportNodeBuiltinNodenext, TestAutoImportNodeBuiltinNodenext);

// autoImportNodeModuleSymlinkRenamed_test.go

// autoImportNodeModuleSymlinkRenamed_test.go
static void TestAutoImportNodeModuleSymlinkRenamed(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/solution/package.json
{
    "name": "monorepo",
    "workspaces": ["packages/*"]
}
// @Filename: /home/src/workspaces/solution/packages/utils/package.json
{
    "name": "utils",
    "version": "1.0.0",
    "exports": "./dist/index.js"
}
// @Filename: /home/src/workspaces/solution/packages/utils/tsconfig.json
{
    "compilerOptions": {
        "lib": ["es5"],
        "composite": true,
        "module": "nodenext",
        "rootDir": "src",
        "outDir": "dist"
    },
    "include": ["src"]
}
// @Filename: /home/src/workspaces/solution/packages/utils/src/index.ts
export function gainUtility() { return 0; }
// @Filename: /home/src/workspaces/solution/packages/web/package.json
{
    "name": "web",
    "version": "1.0.0",
    "dependencies": {
        "@monorepo/utils": "file:../utils"
    }
}
// @Filename: /home/src/workspaces/solution/packages/web/tsconfig.json
{
    "compilerOptions": {
        "lib": ["es5"],
        "composite": true,
        "module": "esnext",
        "moduleResolution": "bundler",
        "rootDir": "src",
        "outDir": "dist",
        "emitDeclarationOnly": true
    },
    "include": ["src"],
    "references": [
        { "path": "../utils" }
    ]
}
// @Filename: /home/src/workspaces/solution/packages/web/src/index.ts
gainUtility/**/
// @link: /home/src/workspaces/solution/packages/utils -> /home/src/workspaces/solution/node_modules/utils
// @link: /home/src/workspaces/solution/packages/utils -> /home/src/workspaces/solution/node_modules/@monorepo/utils
// @link: /home/src/workspaces/solution/packages/web -> /home/src/workspaces/solution/node_modules/web)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"@monorepo/utils"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportNodeModuleSymlinkRenamed, TestAutoImportNodeModuleSymlinkRenamed);

// autoImportNodeNextJSRequire_test.go

// autoImportNodeNextJSRequire_test.go
static void TestAutoImportNodeNextJSRequire(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @allowJs: true
// @checkJs: true
// @noEmit: true
// @Filename: /matrix.js
exports.variants = [];
// @Filename: /main.js
exports.dedupeLines = data => {
  variants/**/
}
// @Filename: /totally-irrelevant-no-way-this-changes-things-right.js
export default 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/main.js");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(const { variants } = require("./matrix")

exports.dedupeLines = data => {
  variants
})TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportNodeNextJSRequire, TestAutoImportNodeNextJSRequire);

// autoImportPackageJsonExportsSpecifierEndsInTs_test.go

// autoImportPackageJsonExportsSpecifierEndsInTs_test.go
static void TestAutoImportPackageJsonExportsSpecifierEndsInTs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/pkg/package.json
{
    "name": "pkg",
    "version": "1.0.0",
    "exports": {
      "./something.ts": "./a.js"
    }
 }
// @Filename: /node_modules/pkg/a.d.ts
export function foo(): void;
// @Filename: /package.json
{
    "dependencies": {
       "pkg": "*"
    }
 }
// @Filename: /index.ts
foo/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"pkg/something.ts"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonExportsSpecifierEndsInTs, TestAutoImportPackageJsonExportsSpecifierEndsInTs);

// autoImportPackageJsonFilterExistingImport2_test.go

// autoImportPackageJsonFilterExistingImport2_test.go
static void TestAutoImportPackageJsonFilterExistingImport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @module: preserve
// @Filename: /home/src/workspaces/project/node_modules/@types/react/index.d.ts
export declare function useMemo(): void;
export declare function useState(): void;
// @Filename: /home/src/workspaces/project/package.json
{}
// @Filename: /home/src/workspaces/project/index.ts
useMemo/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{}, nullptr);
		f->GoToBOF(t);
		f->InsertLine(t, R"TS(import { useState } from "react";)TS");
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { useMemo, useState } from "react";
useMemo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonFilterExistingImport2, TestAutoImportPackageJsonFilterExistingImport2);

// autoImportPackageJsonFilterExistingImport3_test.go

// autoImportPackageJsonFilterExistingImport3_test.go
static void TestAutoImportPackageJsonFilterExistingImport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "lib": ["es5"], "module": "preserve", "types": ["*"] } }
// @Filename: /home/src/workspaces/project/node_modules/@types/node/index.d.ts
declare module "node:fs" {
    export function readFile(): void;
    export function writeFile(): void;
}
// @Filename: /home/src/workspaces/project/package.json
{}
// @Filename: /home/src/workspaces/project/index.ts
readFile/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{}, nullptr);
		f->GoToBOF(t);
		f->InsertLine(t, R"TS(import { writeFile } from "node:fs";)TS");
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { readFile, writeFile } from "node:fs";
readFile)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonFilterExistingImport3, TestAutoImportPackageJsonFilterExistingImport3);

// autoImportPackageJsonImportsCaseSensitivity_test.go

// autoImportPackageJsonImportsCaseSensitivity_test.go
static void TestAutoImportPackageJsonImportsCaseSensitivity(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @allowImportingTsExtensions: true
// @Filename: /package.json
{
  "type": "module",
  "imports": {
    "#src/*": "./SRC/*"
  }
}
// @Filename: /src/add.ts
export function add(a: number, b: number) {}
// @Filename: /src/index.ts
add/*imports*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "imports", std::vector<std::string>{"#src/add.ts"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "non-relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsCaseSensitivity, TestAutoImportPackageJsonImportsCaseSensitivity);

// autoImportPackageJsonImportsConditions_test.go

// autoImportPackageJsonImportsConditions_test.go
static void TestAutoImportPackageJsonImportsConditions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#thing": {
        "types": { "import": "./types-esm/thing.d.mts", "require": "./types/thing.d.ts" },
        "default": { "import": "./esm/thing.mjs", "require": "./dist/thing.js" }
     }
  }
}
// @Filename: /src/.ts
something/*a*/
// @Filename: /types/thing.d.ts
export function something(name: string): any;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "a", std::vector<std::string>{"#thing"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsConditions, TestAutoImportPackageJsonImportsConditions);

// autoImportPackageJsonImportsHashSlash_test.go

// autoImportPackageJsonImportsHashSlash_test.go
static void TestAutoImportPackageJsonImportsHashSlashNodenext(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "rootDir": "./",
    "outDir": "build"
  }
}
// @Filename: /package.json
{
  "imports": {
    "#/*": {
      "types": "./src/*",
      "default": "./src/*"
    }
  }
}
// @Filename: /src/domain/entities/entity.ts
export const entity = 1;
// @Filename: /src/features/deep/consumer.ts
entit/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsHashSlashNodenext, TestAutoImportPackageJsonImportsHashSlashNodenext);

// autoImportPackageJsonImportsHashSlash_test.go
static void TestAutoImportPackageJsonImportsHashSlashNode16(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
  "compilerOptions": {
    "module": "node16"
  }
}
// @Filename: /package.json
{
  "imports": {
    "#/*": "./src/*"
  }
}
// @Filename: /src/domain/entities/entity.ts
export const entity = 1;
// @Filename: /src/consumer.ts
entit/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsHashSlashNode16, TestAutoImportPackageJsonImportsHashSlashNode16);

// autoImportPackageJsonImportsLength2_test.go

// autoImportPackageJsonImportsLength2_test.go
static void TestAutoImportPackageJsonImportsLength2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#*": "./src/*.ts"
  }
}
// @Filename: /src/a/b/c/something.ts
export function something(name: string): any;
// @Filename: /a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#a/b/c/something"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsLength2, TestAutoImportPackageJsonImportsLength2);

// autoImportPackageJsonImportsPattern_js_test.go

// autoImportPackageJsonImportsPattern_js_test.go
static void TestAutoImportPackageJsonImportsPattern_js(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		t->Skip({"Go *_js_test.go file: GOOS js-gated, never compiled on this platform"}); return;
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#*": "./src/*.js"
  }
}
// @Filename: /src/something.ts
export function something(name: string): any;
// @Filename: /a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#something"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsPattern_js, TestAutoImportPackageJsonImportsPattern_js);

// autoImportPackageJsonImportsPattern_js_ts_test.go

// autoImportPackageJsonImportsPattern_js_ts_test.go
static void TestAutoImportPackageJsonImportsPattern_js_ts(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#*.js": "./src/*.ts"
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
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsPattern_js_ts, TestAutoImportPackageJsonImportsPattern_js_ts);

// autoImportPackageJsonImportsPattern_ts_js_test.go

// autoImportPackageJsonImportsPattern_ts_js_test.go
static void TestAutoImportPackageJsonImportsPattern_ts_js(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		t->Skip({"Go *_js_test.go file: GOOS js-gated, never compiled on this platform"}); return;
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#*.ts": "./src/*.js"
  }
}
// @Filename: /src/something.ts
export function something(name: string): any;
// @Filename: /a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#something.ts"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsPattern_ts_js, TestAutoImportPackageJsonImportsPattern_ts_js);

// autoImportPackageJsonImportsPattern_ts_test.go

// autoImportPackageJsonImportsPattern_ts_test.go
static void TestAutoImportPackageJsonImportsPattern_ts(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#*": "./src/*.ts"
  }
}
// @Filename: /src/something.ts
export function something(name: string): any;
// @Filename: /a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#something"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsPattern_ts, TestAutoImportPackageJsonImportsPattern_ts);

// autoImportPackageJsonImportsPattern_ts_ts_test.go

// autoImportPackageJsonImportsPattern_ts_ts_test.go
static void TestAutoImportPackageJsonImportsPattern_ts_ts(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#*.ts": "./src/*.ts"
  }
}
// @Filename: /src/something.ts
export function something(name: string): any;
// @Filename: /a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#something.ts"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsPattern_ts_ts, TestAutoImportPackageJsonImportsPattern_ts_ts);

// autoImportPackageJsonImportsPreference1_test.go

// autoImportPackageJsonImportsPreference1_test.go
static void TestAutoImportPackageJsonImportsPreference1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#*": "./src/*.ts"
  }
}
// @Filename: /src/a/b/c/something.ts
export function something(name: string): any;
// @Filename: /a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./src/a/b/c/something"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsPreference1, TestAutoImportPackageJsonImportsPreference1);

// autoImportPackageJsonImportsPreference2_test.go

// autoImportPackageJsonImportsPreference2_test.go
static void TestAutoImportPackageJsonImportsPreference2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#*": "./src/*.ts"
  }
}
// @Filename: /src/a/b/c/something.ts
export function something(name: string): any;
// @Filename: /a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./src/a/b/c/something"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "project-relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsPreference2, TestAutoImportPackageJsonImportsPreference2);

// autoImportPackageJsonImportsPreference3_test.go

// autoImportPackageJsonImportsPreference3_test.go
static void TestAutoImportPackageJsonImportsPreference3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
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
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#a/b/c/something"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "non-relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImportsPreference3, TestAutoImportPackageJsonImportsPreference3);

// autoImportPackageJsonImports_capsInPath2_test.go

// autoImportPackageJsonImports_capsInPath2_test.go
static void TestAutoImportPackageJsonImports_capsInPath2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /Dev/package.json
{
  "imports": {
    "#thing/*": "./src/*.js"
  }
}
// @Filename: /Dev/src/something.ts
export function something(name: string): any;
// @Filename: /Dev/a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#thing/something"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImports_capsInPath2, TestAutoImportPackageJsonImports_capsInPath2);

// autoImportPackageJsonImports_js_test.go

// autoImportPackageJsonImports_js_test.go
static void TestAutoImportPackageJsonImports_js(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		t->Skip({"Go *_js_test.go file: GOOS js-gated, never compiled on this platform"}); return;
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#thing": "./src/something.js"
  }
}
// @Filename: /src/something.ts
export function something(name: string): any;
// @Filename: /a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#thing"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImports_js, TestAutoImportPackageJsonImports_js);

// autoImportPackageJsonImports_ts_test.go

// autoImportPackageJsonImports_ts_test.go
static void TestAutoImportPackageJsonImports_ts(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#thing": "./src/something.ts"
  }
}
// @Filename: /src/something.ts
export function something(name: string): any;
// @Filename: /a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#thing"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageJsonImports_ts, TestAutoImportPackageJsonImports_ts);

// autoImportPackageRootPathExtension_test.go

// autoImportPackageRootPathExtension_test.go
static void TestAutoImportPackageRootPathExtension(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /node_modules/pkg/package.json
{
    "name": "pkg",
    "version": "1.0.0",
    "main": "lib"
 }
// @Filename: /node_modules/pkg/lib/index.d.mts
export declare function foo(): any;
// @Filename: /package.json
{
    "dependencies": {
       "pkg": "*"
    }
 }
// @Filename: /index.ts
foo/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.AutoImportEntrypointDirectorySearch = Tristate::True});
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"pkg/lib/index.mjs"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageRootPathExtension, TestAutoImportPackageRootPathExtension);

// autoImportPackageRootPathTypeModule_test.go

// autoImportPackageRootPathTypeModule_test.go
static void TestAutoImportPackageRootPathTypeModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /node_modules/pkg/package.json
{
    "name": "pkg",
    "version": "1.0.0",
    "main": "lib",
    "type": "module"
 }
// @Filename: /node_modules/pkg/lib/index.js
export function foo() {};
// @Filename: /package.json
{
    "dependencies": {
       "pkg": "*"
    }
 }
// @Filename: /index.ts
foo/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"pkg"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageRootPathTypeModule, TestAutoImportPackageRootPathTypeModule);

// autoImportPackageRootPath_test.go

// autoImportPackageRootPath_test.go
static void TestAutoImportPackageRootPath(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /node_modules/pkg/package.json
{
    "name": "pkg",
    "version": "1.0.0",
    "main": "lib",
    "module": "lib"
 }
// @Filename: /node_modules/pkg/lib/index.js
export function foo() {};
// @Filename: /package.json
{
    "dependencies": {
       "pkg": "*"
    }
 }
// @Filename: /index.ts
foo/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"pkg"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPackageRootPath, TestAutoImportPackageRootPath);

// autoImportPathsAliasesAndBarrels_test.go

// autoImportPathsAliasesAndBarrels_test.go
static void TestAutoImportPathsAliasesAndBarrels(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
 {
  "compilerOptions": {
    "module": "commonjs",
    "paths": {
      "~/*": ["src/*"]  
    }
  }
}
// @Filename: /src/dirA/index.ts
 export * from "./thing1A";
 export * from "./thing2A";
// @Filename: /src/dirA/thing1A.ts
 export class Thing1A {}
 Thing/**/
// @Filename: /src/dirA/thing2A.ts
 export class Thing2A {}
// @Filename: /src/dirB/index.ts
 export * from "./thing1B";
 export * from "./thing2B";
// @Filename: /src/dirB/thing1B.ts
 export class Thing1B {}
// @Filename: /src/dirB/thing2B.ts
 export class Thing2B {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Thing2A", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "./thing2A"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Thing1B", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "~/dirB"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Thing2B", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "~/dirB"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPathsAliasesAndBarrels, TestAutoImportPathsAliasesAndBarrels);

// autoImportPathsConfigDir_test.go

// autoImportPathsConfigDir_test.go
static void TestAutoImportPathsConfigDir(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: tsconfig.json
{
    "compilerOptions": {
        "paths": {
            "@root/*": ["${configDir}/src/*"]
        }
    }
}
// @Filename: src/one.ts
export const one = 1;
// @Filename: src/foo/two.ts
one/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"@root/one"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPathsConfigDir, TestAutoImportPathsConfigDir);

// autoImportPathsNodeModules_test.go

// autoImportPathsNodeModules_test.go
static void TestAutoImportPathsNodeModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: tsconfig.json
{
    "compilerOptions": {
        "module": "commonjs",
        "moduleResolution": "node",
        "rootDir": "ts",
        "baseUrl": ".",
        "paths": {
            "*": ["node_modules/@woltlab/wcf/ts/*"]
        }
    },
    "include": [
        "ts",
        "node_modules/@woltlab/wcf/ts",
     ]
}
// @Filename: node_modules/@woltlab/wcf/ts/WoltLabSuite/Core/Component/Dialog.ts
export class Dialog {}
// @Filename: ts/main.ts
Dialog/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"WoltLabSuite/Core/Component/Dialog"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPathsNodeModules, TestAutoImportPathsNodeModules);

// autoImportPaths_test.go

// autoImportPaths_test.go
static void TestAutoImportPaths(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /package1/jsconfig.json
{
  "compilerOptions": {
    checkJs: true,
    "paths": {
      "package1/*": ["./*"],
      "package2/*": ["../package2/*"]
    },
    "baseUrl": "."
  },
  "include": [
    ".",
    "../package2"
  ]
}
// @Filename: /package1/file1.js
bar/**/
// @Filename: /package2/file1.js
export const bar = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"package2/file1"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "shortest"}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPaths, TestAutoImportPaths);

// autoImportPatternAmbientModule_test.go

// autoImportPatternAmbientModule_test.go
static void TestAutoImportMergedPatternAmbientModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{ "compilerOptions": { "module": "preserve", "moduleResolution": "bundler" } }

// @Filename: /first.d.ts
declare module "*.asset" with { type: "css" } {
    export const styles: string;
}

// @Filename: /second.d.ts
declare module "*.asset" with { type: "css" } {
    export const styleTokens: string;
}

// @Filename: /index.ts
sty/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Excludes = std::vector<std::string>{"styles", "styleTokens"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportMergedPatternAmbientModule, TestAutoImportMergedPatternAmbientModule);

// autoImportPnpm_test.go

// autoImportPnpm_test.go
static void TestAutoImportPnpm(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{ "compilerOptions": { "module": "commonjs" } }
// @Filename: /node_modules/.pnpm/mobx@6.0.4/node_modules/mobx/package.json
{ "types": "dist/mobx.d.ts" }
// @Filename: /node_modules/.pnpm/mobx@6.0.4/node_modules/mobx/dist/mobx.d.ts
export declare function autorun(): void;
// @Filename: /index.ts
autorun/**/
// @Filename: /utils.ts
import "mobx";
// @link: /node_modules/.pnpm/mobx@6.0.4/node_modules/mobx -> /node_modules/mobx
// @link: /node_modules/.pnpm/mobx@6.0.4/node_modules/mobx -> /node_modules/.pnpm/cool-mobx-dependent@1.2.3/node_modules/mobx)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { autorun } from "mobx";

autorun)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportPnpm, TestAutoImportPnpm);

// autoImportProvider1_test.go

// autoImportProvider1_test.go
static void TestAutoImportProvider1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/node_modules/@angular/forms/package.json
{ "name": "@angular/forms", "typings": "./forms.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/@angular/forms/forms.d.ts
export class PatternValidator {}
// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/package.json
{ "dependencies": { "@angular/forms": "*" } }
// @Filename: /home/src/workspaces/project/index.ts
PatternValidator/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		auto opts654 = f->GetOptions();
		opts654.FormatCodeSettings.NewLineCharacter = R"TS(
)TS";
		f->Configure(t, opts654);
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { PatternValidator } from "@angular/forms";

PatternValidator)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider1, TestAutoImportProvider1);

// autoImportProvider2_test.go

// autoImportProvider2_test.go
static void TestAutoImportProvider2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/node_modules/direct-dependency/package.json
{ "name": "direct-dependency", "dependencies": { "indirect-dependency": "*" } }
// @Filename: /home/src/workspaces/project/node_modules/direct-dependency/index.d.ts
import "indirect-dependency";
export declare class DirectDependency {}
// @Filename: /home/src/workspaces/project/node_modules/indirect-dependency/package.json
{ "name": "indirect-dependency" }
// @Filename: /home/src/workspaces/project/node_modules/indirect-dependency/index.d.ts
export declare class IndirectDependency
// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/package.json
{ "dependencies": { "direct-dependency": "*" } }
// @Filename: /home/src/workspaces/project/index.ts
IndirectDependency/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		auto opts1155 = f->GetOptions();
		opts1155.FormatCodeSettings.NewLineCharacter = R"TS(
)TS";
		f->Configure(t, opts1155);
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider2, TestAutoImportProvider2);

// autoImportProvider4_test.go

// autoImportProvider4_test.go
static void TestAutoImportProvider4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/a/package.json
{ "dependencies": { "b": "*" } }
// @Filename: /home/src/workspaces/project/a/tsconfig.json
{ "compilerOptions": { "lib": ["es5"], "module": "commonjs", "target": "esnext" }, "references": [{ "path": "../b" }] }
// @Filename: /home/src/workspaces/project/a/index.ts
new Shape/**/
// @Filename: /home/src/workspaces/project/b/package.json
{ "types": "out/index.d.ts" }
// @Filename: /home/src/workspaces/project/b/tsconfig.json
{ "compilerOptions": { "lib": ["es5"], "outDir": "out", "composite": true } }
// @Filename: /home/src/workspaces/project/b/index.ts
export class Shape {}
// @link: /home/src/workspaces/project/b -> /home/src/workspaces/project/a/node_modules/b)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { Shape } from "b";

new Shape)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider4, TestAutoImportProvider4);

// autoImportProvider5_test.go

// autoImportProvider5_test.go
static void TestAutoImportProvider5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: /home/src/workspaces/project/package.json
{ "dependencies": { "react-hook-form": "*" } }
// @Filename: /home/src/workspaces/project/node_modules/react-hook-form/package.json
{ "types": "dist/index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/react-hook-form/dist/index.d.ts
export * from "./useForm";
// @Filename: /home/src/workspaces/project/node_modules/react-hook-form/dist/useForm.d.ts
export declare function useForm(): void;
// @Filename: /home/src/workspaces/project/index.ts
useForm/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.AutoImportEntrypointDirectorySearch = Tristate::True});
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { useForm } from "react-hook-form";

useForm)TS", R"TS(import { useForm } from "react-hook-form/dist/useForm";

useForm)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider5, TestAutoImportProvider5);

// autoImportProvider6_test.go

// autoImportProvider6_test.go
static void TestAutoImportProvider6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "module": "commonjs", "lib": ["es2019"], "types": ["*"] } }
// @Filename: /home/src/workspaces/project/package.json
{ "dependencies": { "antd": "*", "react": "*" } }
// @Filename: /home/src/workspaces/project/node_modules/@types/react/index.d.ts
export declare function Component(): void;
// @Filename: /home/src/workspaces/project/node_modules/antd/index.d.ts
import "react";
// @Filename: /home/src/workspaces/project/index.ts
Component/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Component", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "react"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider6, TestAutoImportProvider6);

// autoImportProvider7_test.go

// autoImportProvider7_test.go
static void TestAutoImportProvider7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "lib": ["es5"], "module": "commonjs" } }
// @Filename: /home/src/workspaces/project/package.json
{ "dependencies": { "mylib": "file:packages/mylib" } }
// @Filename: /home/src/workspaces/project/packages/mylib/package.json
{ "name": "mylib", "version": "1.0.0", "main": "index.js", "types": "index" }
// @Filename: /home/src/workspaces/project/packages/mylib/index.ts
export * from "./mySubDir";
// @Filename: /home/src/workspaces/project/packages/mylib/mySubDir/index.ts
export * from "./myClass";
export * from "./myClass2";
// @Filename: /home/src/workspaces/project/packages/mylib/mySubDir/myClass.ts
export class MyClass {}
// @Filename: /home/src/workspaces/project/packages/mylib/mySubDir/myClass2.ts
export class MyClass2 {}
// @link: /home/src/workspaces/project/packages/mylib -> /home/src/workspaces/project/node_modules/mylib
// @Filename: /home/src/workspaces/project/src/index.ts

const a = new MyClass/*1*/();
const b = new MyClass2/*2*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "1");
		auto opts1196 = f->GetOptions();
		opts1196.FormatCodeSettings.NewLineCharacter = R"TS(
)TS";
		f->Configure(t, opts1196);
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "MyClass", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "mylib"})})})}})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>("1"), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "MyClass", .Source = "mylib", .AutoImportFix = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{}), .Description = R"TS(Add import from "mylib")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import { MyClass } from "mylib";

const a = new MyClass();
const b = new MyClass2();)TS"), .UserPreferences = nullptr}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider7, TestAutoImportProvider7);

// autoImportProvider8_test.go

// autoImportProvider8_test.go
static void TestAutoImportProvider8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "lib": ["es5"], "module": "commonjs" } }
// @Filename: /home/src/workspaces/project/package.json
{ "dependencies": { "mylib": "file:packages/mylib" } }
// @Filename: /home/src/workspaces/project/packages/mylib/package.json
{ "name": "mylib", "version": "1.0.0" }
// @Filename: /home/src/workspaces/project/packages/mylib/index.ts
export * from "./mySubDir";
// @Filename: /home/src/workspaces/project/packages/mylib/mySubDir/index.ts
export * from "./myClass";
export * from "./myClass2";
// @Filename: /home/src/workspaces/project/packages/mylib/mySubDir/myClass.ts
export class MyClass {}
// @Filename: /home/src/workspaces/project/packages/mylib/mySubDir/myClass2.ts
export class MyClass2 {}
// @link: /home/src/workspaces/project/packages/mylib -> /home/src/workspaces/project/node_modules/mylib
// @Filename: /home/src/workspaces/project/src/index.ts

const a = new MyClass/*1*/();
const b = new MyClass2/*2*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "1");
		auto opts1158 = f->GetOptions();
		opts1158.FormatCodeSettings.NewLineCharacter = R"TS(
)TS";
		f->Configure(t, opts1158);
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "MyClass", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "mylib"})})})}})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>("1"), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "MyClass", .Source = "mylib", .AutoImportFix = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{}), .Description = R"TS(Add import from "mylib")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import { MyClass } from "mylib";

const a = new MyClass();
const b = new MyClass2();)TS"), .UserPreferences = nullptr}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider8, TestAutoImportProvider8);

// autoImportProvider9_test.go

// autoImportProvider9_test.go
static void TestAutoImportProvider9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @module: preserve
// @Filename: /home/src/workspaces/project/index.ts
Lib1/**/
// @Filename: /home/src/workspaces/project/package.json
{
  "dependencies": {
    "lib1": "*",
    "lib2": "*",
    "lib3": "*",
    "lib4": "*",
    "lib5": "*",
    "lib6": "*",
    "lib7": "*",
    "lib8": "*",
    "lib9": "*",
    "lib10": "*",
    "lib11": "*"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/lib1/package.json
{ "name": "lib1", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib1/index.d.ts
export class Lib1 {}
// @Filename: /home/src/workspaces/project/node_modules/lib2/package.json
{ "name": "lib2", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib2/index.d.ts
export class Lib2 {}
// @Filename: /home/src/workspaces/project/node_modules/lib3/package.json
{ "name": "lib3", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib3/index.d.ts
export class Lib3 {}
// @Filename: /home/src/workspaces/project/node_modules/lib4/package.json
{ "name": "lib4", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib4/index.d.ts
export class Lib4 {}
// @Filename: /home/src/workspaces/project/node_modules/lib5/package.json
{ "name": "lib5", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib5/index.d.ts
export class Lib5 {}
// @Filename: /home/src/workspaces/project/node_modules/lib6/package.json
{ "name": "lib6", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib6/index.d.ts
export class Lib6 {}
// @Filename: /home/src/workspaces/project/node_modules/lib7/package.json
{ "name": "lib7", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib7/index.d.ts
export class Lib7 {}
// @Filename: /home/src/workspaces/project/node_modules/lib8/package.json
{ "name": "lib8", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib8/index.d.ts
export class Lib8 {}
// @Filename: /home/src/workspaces/project/node_modules/lib9/package.json
{ "name": "lib9", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib9/index.d.ts
export class Lib9 {}
// @Filename: /home/src/workspaces/project/node_modules/lib10/package.json
{ "name": "lib10", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib10/index.d.ts
export class Lib10 {}
// @Filename: /home/src/workspaces/project/node_modules/lib11/package.json
{ "name": "lib11", "types": "./index.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/lib11/index.d.ts
export class Lib11 {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{}, nullptr);
		f->InsertLine(t, "import {} from 'lib2';");
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"lib1"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider9, TestAutoImportProvider9);

// autoImportProvider_exportMap1_test.go

// autoImportProvider_exportMap1_test.go
static void TestAutoImportProvider_exportMap1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "nodenext"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "dependencies": {
    "dependency": "^1.0.0"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/package.json
{
  "type": "module",
  "name": "dependency",
  "version": "1.0.0",
  "exports": {
    ".": {
      "types": "./lib/index.d.ts"
    },
    "./lol": {
      "types": "./lib/lol.d.ts"
    }
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/index.d.ts
export function fooFromIndex(): void;
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/lol.d.ts
export function fooFromLol(): void;
// @Filename: /home/src/workspaces/project/src/foo.ts
fooFrom/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromIndex", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromLol", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency/lol"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_exportMap1, TestAutoImportProvider_exportMap1);

// autoImportProvider_exportMap2_test.go

// autoImportProvider_exportMap2_test.go
static void TestAutoImportProvider_exportMap2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "lib": ["es5"],
    "module": "commonjs",
    "moduleResolution": "node10"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "dependencies": {
    "dependency": "^1.0.0"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/package.json
{
  "type": "module",
  "name": "dependency",
  "version": "1.0.0",
  "types": "./lib/index.d.ts",
  "exports": {
    ".": {
      "types": "./lib/index.d.ts"
    },
    "./lol": {
      "types": "./lib/lol.d.ts"
    }
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/index.d.ts
export function fooFromIndex(): void;
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/lol.d.ts
export function fooFromLol(): void;
// @Filename: /home/src/workspaces/project/src/foo.ts
fooFrom/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalsPlus(std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromIndex", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency"})})})}, false)})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_exportMap2, TestAutoImportProvider_exportMap2);

// autoImportProvider_exportMap3_test.go

// autoImportProvider_exportMap3_test.go
static void TestAutoImportProvider_exportMap3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"]
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "dependencies": {
    "dependency": "^1.0.0"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/package.json
{
  "name": "dependency",
  "version": "1.0.0",
  "main": "./lib/index.js",
  "exports": "./lib/lol.d.ts"
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/index.d.ts
export function fooFromIndex(): void;
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/lol.d.ts
export function fooFromLol(): void;
// @Filename: /home/src/workspaces/project/src/foo.ts
fooFrom/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromLol", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency"})})})}, .Excludes = std::vector<std::string>{"fooFromIndex"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_exportMap3, TestAutoImportProvider_exportMap3);

// autoImportProvider_exportMap4_test.go

// autoImportProvider_exportMap4_test.go
static void TestAutoImportProvider_exportMap4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"]
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "dependencies": {
    "dependency": "^1.0.0"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/package.json
{
  "type": "module",
  "name": "dependency",
  "version": "1.0.0",
  "exports": {
    "types": "./lib/index.d.ts",
    "require": "./lib/lol.js"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/index.d.ts
export function fooFromIndex(): void;
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/lol.d.ts
export function fooFromLol(): void;
// @Filename: /home/src/workspaces/project/src/foo.ts
fooFrom/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromIndex", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency"})})})}, .Excludes = std::vector<std::string>{"fooFromLol"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_exportMap4, TestAutoImportProvider_exportMap4);

// autoImportProvider_exportMap5_test.go

// autoImportProvider_exportMap5_test.go
static void TestAutoImportProvider_exportMap5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @types package lookup
// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"]
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "dependencies": {
    "dependency": "^1.0.0"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/package.json
{
  "type": "module",
  "name": "dependency",
  "version": "1.0.0",
  "exports": {
    ".": "./lib/index.js",
    "./lol": "./lib/lol.js"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/index.js
export function fooFromIndex() {}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/lol.js
export function fooFromLol() {}
// @Filename: /home/src/workspaces/project/node_modules/@types/dependency/package.json
{
  "type": "module",
  "name": "@types/dependency",
  "version": "1.0.0",
  "exports": {
    ".": "./lib/index.d.ts",
    "./lol": "./lib/lol.d.ts"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/@types/dependency/lib/index.d.ts
export declare function fooFromIndex(): void;
// @Filename: /home/src/workspaces/project/node_modules/@types/dependency/lib/lol.d.ts
export declare function fooFromLol(): void;
// @Filename: /home/src/workspaces/project/src/foo.ts
fooFrom/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromIndex", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromLol", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency/lol"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_exportMap5, TestAutoImportProvider_exportMap5);

// autoImportProvider_exportMap6_test.go

// autoImportProvider_exportMap6_test.go
static void TestAutoImportProvider_exportMap6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @types package should be ignored because implementation package has types
// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"]
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "dependencies": {
    "dependency": "^1.0.0"
  },
  "devDependencies": {
    "@types/dependency": "^1.0.0"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/package.json
{
  "type": "module",
  "name": "dependency",
  "version": "1.0.0",
  "exports": {
    ".": "./lib/index.js",
    "./lol": "./lib/lol.js"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/index.js
export function fooFromIndex() {}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/index.d.ts
export declare function fooFromIndex(): void
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/lol.js
export function fooFromLol() {}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/lol.d.ts
export declare function fooFromLol(): void
// @Filename: /home/src/workspaces/project/node_modules/@types/dependency/package.json
{
  "type": "module",
  "name": "@types/dependency",
  "version": "1.0.0",
  "exports": {
    ".": "./lib/index.d.ts",
    "./lol": "./lib/lol.d.ts"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/@types/dependency/lib/index.d.ts
export declare function fooFromAtTypesIndex(): void;
// @Filename: /home/src/workspaces/project/node_modules/@types/dependency/lib/lol.d.ts
export declare function fooFromAtTypesLol(): void;
// @Filename: /home/src/workspaces/project/src/foo.ts
fooFrom/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromIndex", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromLol", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency/lol"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_exportMap6, TestAutoImportProvider_exportMap6);

// autoImportProvider_exportMap7_test.go

// autoImportProvider_exportMap7_test.go
static void TestAutoImportProvider_exportMap7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"]
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "dependencies": {
    "dependency": "^1.0.0"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/package.json
{
  "type": "module",
  "name": "dependency",
  "version": "1.0.0",
  "exports": {
    ".": {
      "types": "./lib/index.d.ts"
    },
    "./lol": {
      "types": "./lib/lol.d.ts"
    }
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/index.d.ts
export function fooFromIndex(): void;
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/lol.d.ts
export function fooFromLol(): void;
// @Filename: /home/src/workspaces/project/src/bar.ts
import { fooFromIndex } from "dependency";
// @Filename: /home/src/workspaces/project/src/foo.ts
fooFrom/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromIndex", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromLol", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency/lol"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_exportMap7, TestAutoImportProvider_exportMap7);

// autoImportProvider_exportMap8_test.go

// autoImportProvider_exportMap8_test.go
static void TestAutoImportProvider_exportMap8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"]
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "dependencies": {
    "dependency": "^1.0.0"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/package.json
{
  "type": "module",
  "name": "dependency",
  "version": "1.0.0",
  "exports": {
    "./lol": {
      "import": "./lib/index.js",
      "require": "./lib/lol.js"
    }
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/index.d.ts
export function fooFromIndex(): void;
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/lol.d.ts
export function fooFromLol(): void;
// @Filename: /home/src/workspaces/project/src/bar.ts
import { fooFromIndex } from "dependency";
// @Filename: /home/src/workspaces/project/src/foo.cts
fooFrom/*cts*/
// @Filename: /home/src/workspaces/project/src/foo.mts
fooFrom/*mts*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "cts");
		f->VerifyCompletions(t, "cts", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromLol", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency/lol"})})})}, .Excludes = std::vector<std::string>{"fooFromIndex"}})}));
		f->GoToMarker(t, "mts");
		f->VerifyCompletions(t, "mts", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromIndex", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency/lol"})})})}, .Excludes = std::vector<std::string>{"fooFromLol"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_exportMap8, TestAutoImportProvider_exportMap8);

// autoImportProvider_exportMap9_test.go

// autoImportProvider_exportMap9_test.go
static void TestAutoImportProvider_exportMap9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"]
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "dependencies": {
    "dependency": "^1.0.0"
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/package.json
{
  "type": "module",
  "name": "dependency",
  "version": "1.0.0",
  "exports": {
    "./lol": ["./lib/index.js", "./lib/lol.js"]
  }
}
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/index.d.ts
export function fooFromIndex(): void;
// @Filename: /home/src/workspaces/project/node_modules/dependency/lib/lol.d.ts
export function fooFromLol(): void;
// @Filename: /home/src/workspaces/project/src/bar.ts
import { fooFromIndex } from "dependency";
// @Filename: /home/src/workspaces/project/src/foo.ts
fooFrom/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooFromIndex", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "dependency/lol"})})})}, .Excludes = std::vector<std::string>{"fooFromLol"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_exportMap9, TestAutoImportProvider_exportMap9);

// autoImportProvider_globalTypingsCache_test.go

// autoImportProvider_globalTypingsCache_test.go
static void TestAutoImportProvider_globalTypingsCache(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/Library/Caches/typescript/node_modules/@types/react-router-dom/package.json
 { "name": "@types/react-router-dom", "version": "16.8.4", "types": "index.d.ts" }
// @Filename: /home/src/Library/Caches/typescript/node_modules/@types/react-router-dom/index.d.ts
 export class BrowserRouterFromDts {}
// @Filename: /home/src/workspaces/project/package.json
 { "dependencies": { "react-router-dom": "*" } }
// @Filename: /home/src/workspaces/project/tsconfig.json
 { "compilerOptions": { "module": "commonjs", "lib": ["es5"], "allowJs": true, "checkJs": true, "maxNodeModuleJsDepth": 2 }, "typeAcquisition": { "enable": true } }
// @Filename: /home/src/workspaces/project/node_modules/react-router-dom/package.json
 { "name": "react-router-dom", "version": "16.8.4", "main": "index.js" }
// @Filename: /home/src/workspaces/project/node_modules/react-router-dom/index.js
 import "./BrowserRouter";
 export {};
// @Filename: /home/src/workspaces/project/node_modules/react-router-dom/BrowserRouter.js
 export const BrowserRouterFromJs = () => null;
// @Filename: /home/src/workspaces/project/index.js
BrowserRouter/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalsInJSPlus(std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "BrowserRouterFromDts", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "react-router-dom"})})})}, false)})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_globalTypingsCache, TestAutoImportProvider_globalTypingsCache);

// autoImportProvider_importsMap1_test.go

// autoImportProvider_importsMap1_test.go
static void TestAutoImportProvider_importsMap1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"],
    "rootDir": "src",
    "outDir": "dist"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "imports": {
    "#is-browser": {
      "browser": "./dist/env/browser.js",
      "default": "./dist/env/node.js"
    }
  }
}
// @Filename: /home/src/workspaces/project/src/env/browser.ts
export const isBrowser = true;
// @Filename: /home/src/workspaces/project/src/env/node.ts
export const isBrowser = false;
// @Filename: /home/src/workspaces/project/src/a.ts
isBrowser/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#is-browser", "./env/browser.js"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_importsMap1, TestAutoImportProvider_importsMap1);

// autoImportProvider_importsMap2_test.go

// autoImportProvider_importsMap2_test.go
static void TestAutoImportProvider_importsMap2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"],
    "rootDir": "src",
    "outDir": "dist"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "imports": {
    "#internal/*": "./dist/internal/*"
  }
}
// @Filename: /home/src/workspaces/project/src/internal/foo.ts
export function something(name: string) {}
// @Filename: /home/src/workspaces/project/src/a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#internal/foo.js"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_importsMap2, TestAutoImportProvider_importsMap2);

// autoImportProvider_importsMap3_test.go

// autoImportProvider_importsMap3_test.go
static void TestAutoImportProvider_importsMap3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"],
    "rootDir": "src",
    "outDir": "dist"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "imports": {
    "#internal/": "./dist/internal/"
  }
}
// @Filename: /home/src/workspaces/project/src/internal/foo.ts
export function something(name: string) {}
// @Filename: /home/src/workspaces/project/src/a.ts
something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#internal/foo.js"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_importsMap3, TestAutoImportProvider_importsMap3);

// autoImportProvider_importsMap4_test.go

// autoImportProvider_importsMap4_test.go
static void TestAutoImportProvider_importsMap4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"],
    "rootDir": "src",
    "outDir": "dist"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "imports": {
    "#is-browser": {
      "types": "./dist/env/browser.d.ts",
      "default": "./dist/env/browser.js"
    }
  }
}
// @Filename: /home/src/workspaces/project/src/env/browser.ts
export const isBrowser = true;
// @Filename: /home/src/workspaces/project/src/a.ts
isBrowser/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#is-browser"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_importsMap4, TestAutoImportProvider_importsMap4);

// autoImportProvider_importsMap5_test.go

// autoImportProvider_importsMap5_test.go
static void TestAutoImportProvider_importsMap5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"],
    "rootDir": "src",
    "outDir": "dist",
    "declarationDir": "types",
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "type": "module",
  "imports": {
    "#is-browser": {
      "types": "./types/env/browser.d.ts",
      "default": "./not-dist-on-purpose/env/browser.js"
    }
  }
}
// @Filename: /home/src/workspaces/project/src/env/browser.ts
export const isBrowser = true;
// @Filename: /home/src/workspaces/project/src/a.ts
isBrowser/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#is-browser"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_importsMap5, TestAutoImportProvider_importsMap5);

// autoImportProvider_namespaceSameNameAsIntrinsic_test.go

// autoImportProvider_namespaceSameNameAsIntrinsic_test.go
static void TestAutoImportProvider_namespaceSameNameAsIntrinsic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/node_modules/fp-ts/package.json
{ "name": "fp-ts", "version": "0.10.4" }
// @Filename: /home/src/workspaces/project/node_modules/fp-ts/index.d.ts
export * as string from "./lib/string";
// @Filename: /home/src/workspaces/project/node_modules/fp-ts/lib/string.d.ts
export declare const fromString: (s: string) => string;
export type SafeString = string;
// @Filename: /home/src/workspaces/project/package.json
{ "dependencies": { "fp-ts": "^0.10.4" } }
// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "module": "commonjs", "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/index.ts
type A = { name: string/**/ })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "string", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "string", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "fp-ts"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_namespaceSameNameAsIntrinsic, TestAutoImportProvider_namespaceSameNameAsIntrinsic);

// autoImportProvider_pnpm_test.go

// autoImportProvider_pnpm_test.go
static void TestAutoImportProvider_pnpm(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "module": "commonjs", "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/package.json
{ "dependencies": { "mobx": "*" } }
// @Filename: /home/src/workspaces/project/node_modules/.pnpm/mobx@6.0.4/node_modules/mobx/package.json
{ "types": "dist/mobx.d.ts" }
// @Filename: /home/src/workspaces/project/node_modules/.pnpm/mobx@6.0.4/node_modules/mobx/dist/mobx.d.ts
export declare function autorun(): void;
// @Filename: /home/src/workspaces/project/index.ts
autorun/**/
// @link: /home/src/workspaces/project/node_modules/.pnpm/mobx@6.0.4/node_modules/mobx -> /home/src/workspaces/project/node_modules/mobx)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { autorun } from "mobx";

autorun)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_pnpm, TestAutoImportProvider_pnpm);

// autoImportProvider_referencesCrash_test.go

// autoImportProvider_referencesCrash_test.go
static void TestAutoImportProvider_referencesCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/a/package.json
{}
// @Filename: /home/src/workspaces/project/a/tsconfig.json
{ "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/a/index.ts
class A {}
// @Filename: /home/src/workspaces/project/a/index.d.ts
declare class A {
}
//# sourceMappingURL=index.d.ts.map
// @Filename: /home/src/workspaces/project/a/index.d.ts.map
{"version":3,"file":"index.d.ts","sourceRoot":"","sources":["index.ts"],"names":[],"mappings":"AAAA,OAAO,OAAO,CAAC;CAAG"}
// @Filename: /home/src/workspaces/project/b/tsconfig.json
{
  "compilerOptions": { "disableSourceOfProjectReferenceRedirect": true, "lib": ["es5"] },
  "references": [{ "path": "../a" }]
}
// @Filename: /home/src/workspaces/project/b/b.ts
/// <reference path="../a/index.d.ts" />
new A/**/();
// @Filename: /home/src/workspaces/project/c/package.json
{ "dependencies": { "a": "*" } }
// @Filename: /home/src/workspaces/project/c/tsconfig.json
{ "compilerOptions": { "lib": ["es5"] }, "references" [{ "path": "../a" }] }
// @Filename: /home/src/workspaces/project/c/index.ts
export {};
// @link: /home/src/workspaces/project/a -> /home/src/workspaces/project/c/node_modules/a)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToFile(t, "/home/src/workspaces/project/c/index.ts");
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_referencesCrash, TestAutoImportProvider_referencesCrash);

// autoImportProvider_wildcardExports1_test.go

// autoImportProvider_wildcardExports1_test.go
static void TestAutoImportProvider_wildcardExports1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{
    "name": "pkg",
    "version": "1.0.0",
    "exports": {
        "./*": "./a/*.js",
        "./b/*.js": "./b/*.js",
        "./c/*": "./c/*",
        "./d/*": {
            "import": "./d/*.mjs"
        }
    }
}
// @Filename: /home/src/workspaces/project/node_modules/pkg/a/a1.d.ts
export const a1: number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/b/b1.d.ts
export const b1: number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/b/b2.d.mts
export const NOT_REACHABLE: number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/c/c1.d.ts
export const c1: number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/c/subfolder/c2.d.mts
export const c2: number;
// @Filename: /home/src/workspaces/project/node_modules/pkg/d/d1.d.mts
export const d1: number;
// @Filename: /home/src/workspaces/project/package.json
{
    "type": "module",
    "dependencies": {
        "pkg": "1.0.0"
    }
}
// @Filename: /home/src/workspaces/project/tsconfig.json
{
    "compilerOptions": {
        "module": "nodenext",
        "lib": ["es5"]
    }
}
// @Filename: /home/src/workspaces/project/main.ts
/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "a1", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "pkg/a1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b1", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "pkg/b/b1.js"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c1", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "pkg/c/c1.js"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "pkg/c/subfolder/c2.mjs"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "d1", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "pkg/d/d1"})})})}, .Excludes = std::vector<std::string>{"NOT_REACHABLE"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_wildcardExports1, TestAutoImportProvider_wildcardExports1);

// autoImportProvider_wildcardExports2_test.go

// autoImportProvider_wildcardExports2_test.go
static void TestAutoImportProvider_wildcardExports2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{
    "name": "pkg",
    "version": "1.0.0",
    "exports": {
        "./core/*": {
            "types": "./lib/core/*.d.ts",
            "default": "./lib/core/*.js"
        }
    }
}
// @Filename: /home/src/workspaces/project/node_modules/pkg/lib/core/test.d.ts
export function test(): void;
// @Filename: /home/src/workspaces/project/package.json
{
    "type": "module",
    "dependencies": {
        "pkg": "1.0.0"
    }
}
// @Filename: /home/src/workspaces/project/tsconfig.json
{
    "compilerOptions": {
        "module": "nodenext",
        "lib": ["es5"]
    }
}
// @Filename: /home/src/workspaces/project/main.ts
/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "test", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "pkg/core/test"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_wildcardExports2, TestAutoImportProvider_wildcardExports2);

// autoImportProvider_wildcardExports3_test.go

// autoImportProvider_wildcardExports3_test.go
static void TestAutoImportProvider_wildcardExports3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/packages/ui/package.json
{
  "name": "@repo/ui",
  "version": "1.0.0",
  "exports": {
    "./*": "./src/*.tsx"
  }
}
// @Filename: /home/src/workspaces/project/packages/ui/src/Card.tsx
export const Card = () => null;
// @Filename: /home/src/workspaces/project/apps/web/package.json
{
  "name": "web",
  "version": "1.0.0",
  "dependencies": {
    "@repo/ui": "workspace:*"
  }
}
// @Filename: /home/src/workspaces/project/apps/web/tsconfig.json
{
  "compilerOptions": {
    "module": "esnext",
    "moduleResolution": "bundler",
    "noEmit": true,
    "jsx": "preserve",
    "lib": ["es5"]
  },
 "include": ["app"]
}
// @Filename: /home/src/workspaces/project/apps/web/app/index.tsx
(<Card/**/ />);
// @link: /home/src/workspaces/project/packages/ui -> /home/src/workspaces/project/apps/web/node_modules/@repo/ui)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Card", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "@repo/ui/Card"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportProvider_wildcardExports3, TestAutoImportProvider_wildcardExports3);

// autoImportQuoteDetection_test.go

// autoImportQuoteDetection_test.go
static void TestAutoImportQuoteDetection(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @Filename: /a.ts
export const foo = 0;
// @Filename: /b.ts
import {} from 'node:path';

fo/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "foo", .Source = "./a", .Description = R"TS(Add import from "./a")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import {} from 'node:path';
import { foo } from './a';

fo)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportQuoteDetection, TestAutoImportQuoteDetection);

// autoImportReExportFromAmbientModule_test.go

// autoImportReExportFromAmbientModule_test.go
static void TestAutoImportReExportFromAmbientModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "commonjs",
    "types": ["*"],
    "lib": ["es5"]
  }
}
// @Filename: /home/src/workspaces/project/node_modules/@types/node/index.d.ts
declare module "fs" {
  export function accessSync(path: string): void;
}
// @Filename: /home/src/workspaces/project/node_modules/@types/fs-extra/index.d.ts
export * from "fs";
// @Filename: /home/src/workspaces/project/index.ts
access/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "accessSync", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "fs"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "accessSync", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "fs-extra"})})})}})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "accessSync", .Source = "fs-extra", .AutoImportFix = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "fs-extra"}), .Description = R"TS(Add import from "fs-extra")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import { accessSync } from "fs-extra";

access)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportReExportFromAmbientModule, TestAutoImportReExportFromAmbientModule);

// autoImportReexportOfCrossPackageAugmentation_test.go

// autoImportReexportOfCrossPackageAugmentation_test.go
static void TestAutoImportReexportOfCrossPackageAugmentation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/vitest/package.json
{ "name": "vitest", "version": "1.0.0", "types": "index.d.ts" }
// @Filename: /node_modules/vitest/index.d.ts
export { AugmentedInterface, uniqueFunction } from "@vitest/expect";
// @Filename: /node_modules/vitest/augmentation.d.ts
export {};
declare module "@vitest/expect" {
    interface AugmentedInterface {
        bar: string;
    }
		function uniqueFunction(): void;
}
// @Filename: /node_modules/@vitest/expect/package.json
{ "name": "@vitest/expect", "version": "1.0.0", "types": "index.d.ts" }
// @Filename: /node_modules/@vitest/expect/index.d.ts
export interface AugmentedInterface {
    baz: number;
}
// @Filename: /tsconfig.json
{ "compilerOptions": { "module": "commonjs", "strict": true } }
// @Filename: /package.json
{ "name": "test", "dependencies": { "vitest": "*" } }
// @Filename: /index.ts
uniqueFunction/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.AutoImportEntrypointDirectorySearch = Tristate::True;
		f->Configure(t, prefs);
		f->GoToMarker(t, "");
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportReexportOfCrossPackageAugmentation, TestAutoImportReexportOfCrossPackageAugmentation);

// autoImportRelativePathToMonorepoPackage_test.go

// autoImportRelativePathToMonorepoPackage_test.go
static void TestAutoImportRelativePathToMonorepoPackage(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "lib": ["es5"]
  }
}
// @Filename: /home/src/workspaces/project/packages/app/dist/index.d.ts
import {} from "utils";
export const app: number;
// @Filename: /home/src/workspaces/project/packages/utils/package.json
{ "name": "utils", "version": "1.0.0", "main": "dist/index.js" }
// @Filename: /home/src/workspaces/project/packages/utils/dist/index.d.ts
export const x: number;
// @link: /home/src/workspaces/project/packages/utils -> /home/src/workspaces/project/packages/app/node_modules/utils
// @Filename: /home/src/workspaces/project/script.ts
import {} from "./packages/app/dist/index.js";
x/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./packages/utils/dist/index.js"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportRelativePathToMonorepoPackage, TestAutoImportRelativePathToMonorepoPackage);

// autoImportRootDirs_test.go

// autoImportRootDirs_test.go
static void TestAutoImportRootDirs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
    "compilerOptions": {
        "module": "commonjs",
        "rootDirs": [".", "./some/other/root"]
    }
}
// @Filename: /some/other/root/types.ts
export type Something = {};
// @Filename: /index.ts
const s: Something/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./types"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportRootDirs, TestAutoImportRootDirs);

// autoImportSameNameDefaultExported_test.go

// autoImportSameNameDefaultExported_test.go
static void TestAutoImportSameNameDefaultExported(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @module: commonjs
// @Filename: /node_modules/antd/index.d.ts
declare function Table(): void;
export default Table;
// @Filename: /node_modules/rc-table/index.d.ts
declare function Table(): void;
export default Table;
// @Filename: /index.ts
Table/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalsPlus(std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Table", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "antd"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Table", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "rc-table"})})})}, false)})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSameNameDefaultExported, TestAutoImportSameNameDefaultExported);

// autoImportSortCaseSensitivity1_test.go

// autoImportSortCaseSensitivity1_test.go
static void TestAutoImportSortCaseSensitivity1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /exports1.ts
export const a = 0;
export const A = 1;
export const b = 2;
export const B = 3;
export const c = 4;
export const C = 5;
// @Filename: /exports2.ts
export const d = 0;
export const D = 1;
export const e = 2;
export const E = 3;
// @Filename: /index0.ts
import { A, B, C } from "./exports1";
a/*0*/
// @Filename: /index1.ts
import { A, a, B, b } from "./exports1";
import { E } from "./exports2";
d/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "0");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a, A, B, C } from "./exports1";
a)TS"}, nullptr);
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a, A, B, C } from "./exports1";
a)TS"}, nullptr);
		f->GoToMarker(t, "1");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, a, B, b } from "./exports1";
import { d, E } from "./exports2";
d)TS"}, nullptr);
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, a, B, b } from "./exports1";
import { E, d } from "./exports2";
d)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSortCaseSensitivity1, TestAutoImportSortCaseSensitivity1);

// autoImportSortCaseSensitivity2_test.go

// autoImportSortCaseSensitivity2_test.go
static void TestAutoImportSortCaseSensitivity2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export interface HasBar { bar: number }
export function hasBar(x: unknown): x is HasBar { return x && typeof x.bar === "number" }
export function foo() {}
export type __String = string;
// @Filename: /b.ts
import { __String, HasBar, hasBar } from "./a";
f/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .Detail = std::string("function foo(): void"), .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "./a"})})})}})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "foo", .Source = "./a", .Description = R"TS(Update import from "./a")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import { __String, foo, HasBar, hasBar } from "./a";
f;)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSortCaseSensitivity2, TestAutoImportSortCaseSensitivity2);

// autoImportSpecifierExcludeRegexes1_test.go

// autoImportSpecifierExcludeRegexes1_test.go
static void TestAutoImportSpecifierExcludeRegexes1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: preserve
// @Filename: /node_modules/lib/index.d.ts
declare module "ambient" {
    export const x: number;
}
declare module "ambient/utils" {
   export const x: number;
}
// @Filename: /index.ts
x/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"ambient", "ambient/utils"}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"ambient"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"utils"}}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"ambient", "ambient/utils"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"/UTILS/"}}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"ambient"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"/UTILS/i"}}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"ambient", "ambient/utils"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"/ambient/utils/"}}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"ambient"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{R"TS(/ambient\/utils/)TS"}}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"ambient"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"/.*?$"}}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"ambient"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"^ambient/"}}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"ambient/utils"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"ambient$"}}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"ambient", "ambient/utils"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"oops("}}));
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "ambient"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "ambient/utils"})})})}})}));
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Excludes = std::vector<std::string>{"ambient/utils"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"utils"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSpecifierExcludeRegexes1, TestAutoImportSpecifierExcludeRegexes1);

// autoImportSpecifierExcludeRegexes2_test.go

// autoImportSpecifierExcludeRegexes2_test.go
static void TestAutoImportSpecifierExcludeRegexes2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
    "compilerOptions": {
        "module": "preserve",
        "paths": {
            "@app/*": ["./src/*"]
        }
    }
}
// @Filename: /src/utils.ts
export function add(a: number, b: number) {}
// @Filename: /src/index.ts
add/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./utils"}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"@app/utils"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{R"TS(^\./)TS"}}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"@app/utils"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "non-relative"}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./utils"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "non-relative", .AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"^@app/"}}));
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"utils"}}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSpecifierExcludeRegexes2, TestAutoImportSpecifierExcludeRegexes2);

// autoImportSpecifierExcludeRegexes3_test.go

// autoImportSpecifierExcludeRegexes3_test.go
static void TestAutoImportSpecifierExcludeRegexes3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: preserve
// @Filename: /node_modules/pkg/package.json
{
    "name": "pkg",
    "version": "1.0.0",
    "exports": {
        ".": "./index.js",
        "./utils": "./utils.js"
    }
}
// @Filename: /node_modules/pkg/utils.d.ts
export function add(a: number, b: number) {}
// @Filename: /node_modules/pkg/index.d.ts
export * from "./utils";
// @Filename: /src/index.ts
add/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"pkg", "pkg/utils"}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"pkg/utils"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.AutoImportSpecifierExcludeRegexes = std::vector<std::string>{"^pkg$"}}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSpecifierExcludeRegexes3, TestAutoImportSpecifierExcludeRegexes3);

// autoImportSpecifierExcludeRegexes_test.go

// autoImportSpecifierExcludeRegexes_test.go
static void TestAutoImportSpecifierExcludeRegexes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
export const mySymbol = 1;
// @Filename: ignoreme.ts
export const ignoredSymbol = 2;
// @Filename: bar.ts
mySym/*1*/
ignoredSym/*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True, .AutoImportSpecifierExcludeRegexes = std::vector<std::string>{".*ignoreme.*"}});
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"mySymbol"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Excludes = std::vector<std::string>{"ignoredSymbol"}})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSpecifierExcludeRegexes, TestAutoImportSpecifierExcludeRegexes);

// autoImportSymlinkCaseSensitive_test.go

// autoImportSymlinkCaseSensitive_test.go
static void TestAutoImportSymlinkCaseSensitive(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{ "compilerOptions": { "module": "commonjs" } }
// @Filename: /node_modules/.pnpm/mobx@6.0.4/node_modules/MobX/Foo.d.ts
export declare function autorun(): void;
// @Filename: /index.ts
autorun/**/
// @Filename: /utils.ts
import "MobX/Foo";
// @link: /node_modules/.pnpm/mobx@6.0.4/node_modules/MobX -> /node_modules/MobX
// @link: /node_modules/.pnpm/mobx@6.0.4/node_modules/MobX -> /node_modules/.pnpm/cool-mobx-dependent@1.2.3/node_modules/MobX)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.AutoImportEntrypointDirectorySearch = Tristate::True});
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { autorun } from "MobX/Foo";

autorun)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSymlinkCaseSensitive, TestAutoImportSymlinkCaseSensitive);

// autoImportSymlinkedMonorepoGranularUpdate_test.go

// autoImportSymlinkedMonorepoGranularUpdate_test.go
static void TestAutoImportSymlinkedMonorepoGranularUpdate(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /packages/project-b/tsconfig.json
{
  "compilerOptions": {
    "composite": true,
    "outDir": "./dist",
    "rootDir": "./src",
    "declaration": true,
    "module": "commonjs",
    "strict": true
  },
  "include": ["src"]
}
// @Filename: /packages/project-b/package.json
{
  "name": "project-b",
  "version": "1.0.0",
  "exports": {
    ".": {
      "types": "./dist/index.d.ts",
      "default": "./dist/index.js"
    }
  }
}
// @Filename: /packages/project-b/src/index.ts
export const projectBValue: number = 42;
/*projectBEdit*/
// @Filename: /packages/project-b/dist/index.d.ts
export declare const projectBValue: number;
// @Filename: /packages/project-a/tsconfig.json
{
  "compilerOptions": {
    "module": "commonjs",
    "strict": true,
    "outDir": "./dist",
    "rootDir": "./src"
  },
  "include": ["src"],
  "references": [{ "path": "../project-b" }]
}
// @Filename: /packages/project-a/package.json
{ "name": "project-a", "dependencies": { "project-b": "*" } }
// @Filename: /packages/project-a/src/index.ts
import { projectBValue } from "project-b";
console.log(projectBValue);
newlyAdded/*projectACompletion*/
// @link: /packages/project-b -> /packages/project-a/node_modules/project-b)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "projectACompletion");
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"projectACompletion"});
		f->GoToMarker(t, "projectBEdit");
		f->Insert(t, R"TS(
export function newlyAddedFunction(): void {})TS");
		f->GoToMarker(t, "projectACompletion");
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"projectACompletion"});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSymlinkedMonorepoGranularUpdate, TestAutoImportSymlinkedMonorepoGranularUpdate);

// autoImportSymlinkedMonorepoProjectReferencesNoPkgExports_test.go

// autoImportSymlinkedMonorepoProjectReferencesNoPkgExports_test.go
static void TestAutoImportSymlinkedMonorepoProjectReferencesNoPkgExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /packages/project-b/tsconfig.json
{
  "compilerOptions": {
    "composite": true,
    "outDir": "./dist",
    "rootDir": "./src",
    "declaration": true,
    "module": "commonjs",
    "strict": true
  },
  "include": ["src"]
}
// @Filename: /packages/project-b/package.json
{
  "name": "project-b",
  "version": "1.0.0",
  "main": "dist/index.js",
  "types": "dist/index.d.ts"
}
// @Filename: /packages/project-b/src/index.ts
export const projectBValue: number = 42;
export function projectBFunction(): string { return "hello"; }
// @Filename: /packages/project-b/dist/index.d.ts
export declare const projectBValue: number;
export declare function projectBFunction(): string;
// @Filename: /packages/project-a/tsconfig.json
{
  "compilerOptions": {
    "module": "commonjs",
    "strict": true,
    "outDir": "./dist",
    "rootDir": "./src"
  },
  "include": ["src"],
  "references": [{ "path": "../project-b" }]
}
// @Filename: /packages/project-a/package.json
{ "name": "project-a", "dependencies": { "project-b": "*" } }
// @Filename: /packages/project-a/src/index.ts
import { projectBValue } from "project-b";
console.log(projectBValue);
projectBFunc/**/
// @link: /packages/project-b -> /packages/project-a/node_modules/project-b)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSymlinkedMonorepoProjectReferencesNoPkgExports, TestAutoImportSymlinkedMonorepoProjectReferencesNoPkgExports);

// autoImportSymlinkedMonorepoProjectReferences_test.go

// autoImportSymlinkedMonorepoProjectReferences_test.go
static void TestAutoImportSymlinkedMonorepoProjectReferences(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /packages/project-b/tsconfig.json
{
  "compilerOptions": {
    "composite": true,
    "outDir": "./dist",
    "rootDir": "./src",
    "declaration": true,
    "module": "commonjs",
    "strict": true
  },
  "include": ["src"]
}
// @Filename: /packages/project-b/package.json
{
  "name": "project-b",
  "version": "1.0.0",
  "exports": {
    ".": {
      "types": "./dist/index.d.ts",
      "default": "./dist/index.js"
    }
  }
}
// @Filename: /packages/project-b/src/index.ts
export const projectBValue: number = 42;
export function projectBFunction(): string { return "hello"; }
// @Filename: /packages/project-b/dist/index.d.ts
export declare const projectBValue: number;
export declare function projectBFunction(): string;
// @Filename: /packages/project-a/tsconfig.json
{
  "compilerOptions": {
    "module": "commonjs",
    "strict": true,
    "outDir": "./dist",
    "rootDir": "./src"
  },
  "include": ["src"],
  "references": [{ "path": "../project-b" }]
}
// @Filename: /packages/project-a/package.json
{ "name": "project-a", "dependencies": { "project-b": "*" } }
// @Filename: /packages/project-a/src/index.ts
import { projectBValue } from "project-b";
console.log(projectBValue);
projectBFunc/**/
// @link: /packages/project-b -> /packages/project-a/node_modules/project-b)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSymlinkedMonorepoProjectReferences, TestAutoImportSymlinkedMonorepoProjectReferences);

// autoImportSymlinkedMonorepoReexport_test.go

// autoImportSymlinkedMonorepoReexport_test.go
static void TestAutoImportSymlinkedMonorepoReexport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /packages/project-b/tsconfig.json
{
  "compilerOptions": {
    "composite": true,
    "outDir": "./dist",
    "rootDir": "./src",
    "declaration": true,
    "module": "commonjs",
    "strict": true
  },
  "include": ["src"]
}
// @Filename: /packages/project-b/package.json
{
  "name": "project-b",
  "version": "1.0.0",
  "main": "dist/index.js",
  "types": "dist/index.d.ts"
}
// @Filename: /packages/project-b/src/utils/foo.ts
export function projectBFunction(): string { return "hello"; }
// @Filename: /packages/project-b/src/index.ts
export * from './utils/foo';
export const projectBValue: number = 42;
// @Filename: /packages/project-b/dist/utils/foo.d.ts
export declare function projectBFunction(): string;
// @Filename: /packages/project-b/dist/index.d.ts
export * from './utils/foo';
export declare const projectBValue: number;
// @Filename: /packages/project-a/tsconfig.json
{
  "compilerOptions": {
    "module": "commonjs",
    "strict": true,
    "outDir": "./dist",
    "rootDir": "./src"
  },
  "include": ["src"],
  "references": [{ "path": "../project-b" }]
}
// @Filename: /packages/project-a/package.json
{ "name": "project-a", "dependencies": { "project-b": "*" } }
// @Filename: /packages/project-a/src/index.ts
import { projectBValue } from "project-b";
console.log(projectBValue);
projectBFunction/**/
// @link: /packages/project-b -> /packages/project-a/node_modules/project-b)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.AutoImportEntrypointDirectorySearch = Tristate::True;
		f->Configure(t, prefs);
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { projectBFunction, projectBValue } from "project-b";
console.log(projectBValue);
projectBFunction)TS", R"TS(import { projectBValue } from "project-b";
import { projectBFunction } from "project-b/src/utils/foo";
console.log(projectBValue);
projectBFunction)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSymlinkedMonorepoReexport, TestAutoImportSymlinkedMonorepoReexport);

// autoImportSymlinkedMonorepoSourceUpdate_test.go

// autoImportSymlinkedMonorepoSourceUpdate_test.go
static void TestAutoImportSymlinkedMonorepoSourceUpdate(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = fourslash::NewFourslash(t, nullptr, TestAutoImportSymlinkedMonorepoSourceUpdateScenario); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "fooCompletion");
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"fooCompletion"});
		f->GoToMarker(t, "fooEdit");
		f->Insert(t, R"TS(
export function foo() {})TS");
		f->GoToMarker(t, "fooCompletion");
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{"fooCompletion"});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSymlinkedMonorepoSourceUpdate, TestAutoImportSymlinkedMonorepoSourceUpdate);

// autoImportSymlinkedMonorepo_test.go

// autoImportSymlinkedMonorepo_test.go
static void TestAutoImportSymlinkedMonorepo(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /packages/project-b/package.json
{ "name": "project-b", "version": "1.0.0", "main": "index.js", "types": "index.d.ts" }
// @Filename: /packages/project-b/index.d.ts
export declare const projectBValue: number;
export declare function projectBFunction(): string;
// @Filename: /packages/project-a/tsconfig.json
{ "compilerOptions": { "module": "commonjs", "strict": true } }
// @Filename: /packages/project-a/package.json
{ "name": "project-a", "dependencies": { "project-b": "*" } }
// @Filename: /packages/project-a/index.ts
import { projectBValue } from "project-b";
console.log(projectBValue);
projectBFunc/**/
// @link: /packages/project-b -> /packages/project-a/node_modules/project-b)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportSymlinkedMonorepo, TestAutoImportSymlinkedMonorepo);

// autoImportTransitiveLeak_test.go

// autoImportTransitiveLeak_test.go
static void TestAutoImportTransitiveLeak(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = fourslash::NewFourslash(t, nullptr, TestAutoImportTransitiveLeakScenario); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "fooCompletion", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .SortText = std::string(std::string(ls::SortTextLocationPriority))})}, .Excludes = std::vector<std::string>{"fooInternal"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportTransitiveLeak, TestAutoImportTransitiveLeak);

// autoImportTypeImport1_test.go

// autoImportTypeImport1_test.go
static void TestAutoImportTypeImport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @verbatimModuleSyntax: true
// @target: esnext
// @Filename: /foo.ts
export const A = 1;
export type B = { x: number };
export type C = 1;
export class D = { y: string };
// @Filename: /test.ts
import { A, D, type C } from './foo';
const b: B/**/ | C;
console.log(A, D);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, D, type C, type B } from './foo';
const b: B | C;
console.log(A, D);)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, D, type B, type C } from './foo';
const b: B | C;
console.log(A, D);)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, D, type C, type B } from './foo';
const b: B | C;
console.log(A, D);)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderFirst}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportTypeImport1, TestAutoImportTypeImport1);

// autoImportTypeImport2_test.go

// autoImportTypeImport2_test.go
static void TestAutoImportTypeImport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @verbatimModuleSyntax: true
// @target: esnext
// @Filename: /foo.ts
export const A = 1;
export type B = { x: number };
export type C = 1;
export class D = { y: string };
// @Filename: /test.ts
import { A, type C, D } from './foo';
const b: B/**/ | C;
console.log(A, D);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, type B, type C, D } from './foo';
const b: B | C;
console.log(A, D);)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, type C, D, type B } from './foo';
const b: B | C;
console.log(A, D);)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, type C, D, type B } from './foo';
const b: B | C;
console.log(A, D);)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderFirst}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportTypeImport2, TestAutoImportTypeImport2);

// autoImportTypeImport3_test.go

// autoImportTypeImport3_test.go
static void TestAutoImportTypeImport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @verbatimModuleSyntax: true
// @target: esnext
// @Filename: /foo.ts
export const A = 1;
export type B = { x: number };
export type C = 1;
export class D = { y: string };
// @Filename: /test.ts
import { A, type B, type C } from './foo';
const b: B | C;
console.log(A, D/**/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, D, type B, type C } from './foo';
const b: B | C;
console.log(A, D);)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, type B, type C, D } from './foo';
const b: B | C;
console.log(A, D);)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, type B, type C, D } from './foo';
const b: B | C;
console.log(A, D);)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderFirst}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportTypeImport3, TestAutoImportTypeImport3);

// autoImportTypeImport4_test.go

// autoImportTypeImport4_test.go
static void TestAutoImportTypeImport4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @verbatimModuleSyntax: true
// @target: esnext
// @Filename: /exports1.ts
export const a = 0;
export const A = 1;
export const b = 2;
export const B = 3;
export const c = 4;
export const C = 5;
export type x = 6;
export const X = 7;
export const Y = 8;
export const Z = 9;
// @Filename: /exports2.ts
export const d = 0;
export const D = 1;
export const e = 2;
export const E = 3;
// @Filename: /index0.ts
import { A, B, C } from "./exports1";
a/*0*//*0a*/;
b;
// @Filename: /index1.ts
import { A, B, C, type Y, type Z } from "./exports1";
a/*1*//*1a*//*1b*//*1c*/;
b;
// @Filename: /index2.ts
import { A, a, B, b, type Y, type Z } from "./exports1";
import { E } from "./exports2";
d/*2*//*2a*//*2b*//*2c*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "0");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a, A, B, C } from "./exports1";
a;
b;)TS", R"TS(import { A, b, B, C } from "./exports1";
a;
b;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->GoToMarker(t, "0a");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a, A, B, C } from "./exports1";
a;
b;)TS", R"TS(import { A, b, B, C } from "./exports1";
a;
b;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->GoToMarker(t, "1");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a, A, B, C, type Y, type Z } from "./exports1";
a;
b;)TS", R"TS(import { A, b, B, C, type Y, type Z } from "./exports1";
a;
b;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->GoToMarker(t, "1a");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a, A, B, C, type Y, type Z } from "./exports1";
a;
b;)TS", R"TS(import { A, b, B, C, type Y, type Z } from "./exports1";
a;
b;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->GoToMarker(t, "1b");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a, A, B, C, type Y, type Z } from "./exports1";
a;
b;)TS", R"TS(import { A, b, B, C, type Y, type Z } from "./exports1";
a;
b;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->GoToMarker(t, "1c");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a, A, B, C, type Y, type Z } from "./exports1";
a;
b;)TS", R"TS(import { A, b, B, C, type Y, type Z } from "./exports1";
a;
b;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->GoToMarker(t, "2");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, a, B, b, type Y, type Z } from "./exports1";
import { d, E } from "./exports2";
d)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->GoToMarker(t, "2a");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, a, B, b, type Y, type Z } from "./exports1";
import { E, d } from "./exports2";
d)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->GoToMarker(t, "2b");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, a, B, b, type Y, type Z } from "./exports1";
import { d, E } from "./exports2";
d)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->GoToMarker(t, "2c");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, a, B, b, type Y, type Z } from "./exports1";
import { E, d } from "./exports2";
d)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportTypeImport4, TestAutoImportTypeImport4);

// autoImportTypeImport5_test.go

// autoImportTypeImport5_test.go
static void TestAutoImportTypeImport5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @verbatimModuleSyntax: true
// @target: esnext
// @Filename: /exports1.ts
export const a = 0;
export const A = 1;
export const b = 2;
export const B = 3;
export const c = 4;
export const C = 5;
export type x = 6;
export const X = 7;
export type y = 8
export const Y = 9;
export const Z = 10;
// @Filename: /exports2.ts
export const d = 0;
export const D = 1;
export const e = 2;
export const E = 3;
// @Filename: /index0.ts
import { type X, type Y, type Z } from "./exports1";
const foo: x/*0*/;
const bar: y;
// @Filename: /index1.ts
import { A, B, type X, type Y, type Z } from "./exports1";
const foo: x/*1*/;
const bar: y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "0");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { type x, type X, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS", R"TS(import { type X, type y, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { type x, type X, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS", R"TS(import { type X, type y, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { type x, type X, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS", R"TS(import { type X, type y, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { type x, type X, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS", R"TS(import { type X, type y, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { type x, type X, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS", R"TS(import { type X, type y, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderFirst}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { type x, type X, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS", R"TS(import { type X, type y, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderFirst}));
		f->GoToMarker(t, "1");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, B, type x, type X, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS", R"TS(import { A, B, type X, type y, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, B, type x, type X, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS", R"TS(import { A, B, type X, type y, type Y, type Z } from "./exports1";
const foo: x;
const bar: y;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportTypeImport5, TestAutoImportTypeImport5);

// autoImportTypeOnlyPreferred1_test.go

// autoImportTypeOnlyPreferred1_test.go
static void TestAutoImportTypeOnlyPreferred1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @verbatimModuleSyntax: true
// @module: esnext
// @moduleResolution: bundler
// @Filename: /ts.d.ts
declare namespace ts {
  interface SourceFile {
      text: string;
  }
  function createSourceFile(): SourceFile;
}
export = ts;
// @Filename: /types.ts
export interface VFS {
  getSourceFile(path: string): ts/**/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "ts", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "./ts"})})})}})})).AndApplyCodeAction(t, tsu::ptr(fourslash::CompletionsExpectedCodeAction{.Name = "ts", .Source = "./ts", .Description = R"TS(Add import from "./ts")TS", .NewFileContent = R"TS(import type ts from "./ts";

export interface VFS {
  getSourceFile(path: string): ts
})TS"}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportTypeOnlyPreferred1, TestAutoImportTypeOnlyPreferred1);

// autoImportTypeOnlyPreferred2_test.go

// autoImportTypeOnlyPreferred2_test.go
static void TestAutoImportTypeOnlyPreferred2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/react/index.d.ts
export interface ComponentType {}
export interface ComponentProps {}
export declare function useState<T>(initialState: T): [T, (newState: T) => void];
export declare function useEffect(callback: () => void, deps: any[]): void;
// @Filename: /main.ts
import type { ComponentType } from "react";
import { useState } from "react";

export function Component({ prop } : { prop: ComponentType }) {
    const codeIsUnimportant = useState(1);
    useEffect/*1*/(() => {}, []);
}
// @Filename: /main2.ts
import { useState } from "react";
import type { ComponentType } from "react";

type _ = ComponentProps/*2*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import type { ComponentType } from "react";
import { useEffect, useState } from "react";

export function Component({ prop } : { prop: ComponentType }) {
    const codeIsUnimportant = useState(1);
    useEffect(() => {}, []);
})TS"}, nullptr);
		f->GoToMarker(t, "2");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { useState } from "react";
import type { ComponentProps, ComponentType } from "react";

type _ = ComponentProps;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportTypeOnlyPreferred2, TestAutoImportTypeOnlyPreferred2);

// autoImportTypeOnlyPreferred3_test.go

// autoImportTypeOnlyPreferred3_test.go
static void TestAutoImportTypeOnlyPreferred3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @moduleResolution: bundler
// @Filename: /a.ts
export class A {}
export class B {}
// @Filename: /b.ts
let x: A/*b*/;
// @Filename: /c.ts
import { A } from "./a";
new A();
let x: B/*c*/;
// @Filename: /d.ts
new A();
let x: B;
// @Filename: /ns.ts
export * as default from "./a";
// @Filename: /e.ts
let x: /*e*/ns.A;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "b");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import type { A } from "./a";

let x: A;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.PreferTypeOnlyAutoImports = Tristate::True}));
		f->GoToMarker(t, "c");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A, type B } from "./a";
new A();
let x: B;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.PreferTypeOnlyAutoImports = Tristate::True}));
		f->GoToFile(t, "/d.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import { A, type B } from "./a";

new A();
let x: B;)TS"});
		f->GoToMarker(t, "e");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import type ns from "./ns";

let x: ns.A;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.PreferTypeOnlyAutoImports = Tristate::True}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportTypeOnlyPreferred3, TestAutoImportTypeOnlyPreferred3);

// autoImportVerbatimTypeOnly1_test.go

// autoImportVerbatimTypeOnly1_test.go
static void TestAutoImportVerbatimTypeOnly1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @verbatimModuleSyntax: true
// @Filename: /mod.ts
export const value = 0;
export class C { constructor(v: any) {} }
export interface I {}
// @Filename: /a.mts
const x: /**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "I", .Source = "./mod", .AutoImportFix = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "./mod.js"}), .Description = R"TS(Add import from "./mod.js")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import type { I } from "./mod.js";

const x: )TS")}));
		f->Insert(t, "I = new C");
		f->VerifyApplyCodeActionFromCompletion(t, nullptr, tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "C", .Source = "./mod", .AutoImportFix = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "./mod.js"}), .Description = R"TS(Update import from "./mod.js")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import { C, type I } from "./mod.js";

const x: I = new C)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportVerbatimTypeOnly1, TestAutoImportVerbatimTypeOnly1);

// autoImportsCustomConditions_test.go

// autoImportsCustomConditions_test.go
static void TestAutoImportsCustomConditions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @moduleResolution: bundler
// @customConditions: custom
// @Filename: /node_modules/dep/package.json
{
  "name": "dep",
  "version": "1.0.0",
  "exports": {
    ".": {
      "custom": "./dist/index.js"
    }
  }
}
// @Filename: /node_modules/dep/dist/index.d.ts
export const dep: number;
// @Filename: /index.ts
dep/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"dep"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportsCustomConditions, TestAutoImportsCustomConditions);

// autoImportsNodeNext1_test.go

// autoImportsNodeNext1_test.go
static void TestAutoImportsNodeNext1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/pack/package.json
{
    "name": "pack",
    "version": "1.0.0",
    "exports": {
        ".": "./main.mjs"
    }
}
// @Filename: /node_modules/pack/main.d.mts
import {} from "./unreachable.mjs";
export const fromMain = 0;
// @Filename: /node_modules/pack/unreachable.d.mts
export const fromUnreachable = 0;
// @Filename: /index.mts
import { fromMain } from "pack";
fromUnreachable/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{}, nullptr);
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Excludes = std::vector<std::string>{"fromUnreachable"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportsNodeNext1, TestAutoImportsNodeNext1);

// autoImportsWithRootDirsAndRootedPath01_test.go

// autoImportsWithRootDirsAndRootedPath01_test.go
static void TestAutoImportsWithRootDirsAndRootedPath01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /dir/foo.ts
 export function foo() {}
// @Filename: /dir/bar.ts
 /*$*/
// @Filename: /dir/tsconfig.json
{
    "compilerOptions": {
        "module": "commonjs",
        "moduleResolution": "classic",
        "rootDirs": ["D:/"]
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "$");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{})}));
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportsWithRootDirsAndRootedPath01, TestAutoImportsWithRootDirsAndRootedPath01);

// javaScriptModules12_test.go

// javaScriptModules12_test.go
static void TestJavaScriptModules12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: mod1.js
var x = require('fs');
/*1*/
// @Filename: mod2.js
var y;
if(true) {
    y = require('fs');
}
/*2*/
// @Filename: glob1.js
var a = require;
/*3*/
// @Filename: glob2.js
var b = '';
/*4*/
// @Filename: consumer.js
/*5*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"x", std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "a", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))})}, .Excludes = std::vector<std::string>{"y"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"y", std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "a", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))})}, .Excludes = std::vector<std::string>{"x"}})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"a", std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))})}, .Excludes = std::vector<std::string>{"x", "y"}})}));
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "a", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))}), "b"}, .Excludes = std::vector<std::string>{"x", "y"}})}));
		f->VerifyCompletions(t, std::vector<std::string>{"5"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "a", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))})}, .Excludes = std::vector<std::string>{"x", "y"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJavaScriptModules12, TestJavaScriptModules12);

// javaScriptModules13_test.go

// javaScriptModules13_test.go
static void TestJavaScriptModules13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: myMod.js
if (true) {
    module.exports = { a: 10 };
}
var invisible = true;
// @Filename: isGlobal.js
var y = 10;
// @Filename: consumer.js
var x = require('./myMod');
/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "consumer.js");
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "y", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))})}, .Excludes = std::vector<std::string>{"invisible"}})}));
		f->Insert(t, "x.");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "a", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField)})}})}));
		f->Insert(t, "a.");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toFixed", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJavaScriptModules13, TestJavaScriptModules13);

// javaScriptModules14_test.go

// javaScriptModules14_test.go
static void TestJavaScriptModules14(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: myMod.js
if (true) {
    exports.b = true;
} else {
    exports.n = 3;
}
function fn() {
    exports.s = 'foo';
}
var invisible = true;
// @Filename: isGlobal.js
var y = 10;
// @Filename: consumer.js
var x = require('myMod');
/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "y", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))})}, .Excludes = std::vector<std::string>{"invisible"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJavaScriptModules14, TestJavaScriptModules14);

// javaScriptModules18_test.go

// javaScriptModules18_test.go
static void TestJavaScriptModules18(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: myMod.js
var x = require('fs');
// @Filename: other.js
/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Excludes = std::vector<std::string>{"x"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJavaScriptModules18, TestJavaScriptModules18);

// javaScriptModules19_test.go

// javaScriptModules19_test.go
static void TestJavaScriptModules19(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: myMod.js
var x = { a: 10 };
module.exports = x;
// @Filename: isGlobal.js
var y = 10;
// @Filename: consumer.js
var x = require('./myMod');
/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "consumer.js");
		f->GoToMarker(t, "");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "y", .SortText = std::string(std::string(ls::SortTextGlobalsOrKeywords))})}, .Excludes = std::vector<std::string>{"invisible"}})}));
		f->Insert(t, "x.");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "a", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField)})}})}));
		f->Insert(t, "a.");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toFixed", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJavaScriptModules19, TestJavaScriptModules19);

// javaScriptModulesError1_test.go

// javaScriptModulesError1_test.go
static void TestJavaScriptModulesError1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
define('mod1', ['a'], /**/function(a, b) {
	
});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
	});
}
REGISTER_FOURSLASH_TEST(TestJavaScriptModulesError1, TestJavaScriptModulesError1);

// javaScriptModulesWithBackticks_test.go

// javaScriptModulesWithBackticks_test.go
static void TestJavaScriptModulesWithBackticks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(// @allowJs: true
// @Filename: a.js
exports.x = 0;
// @Filename: consumer.js
var a = require()TS") + "`") + std::string(R"TS(./a)TS")) + std::string("`")) + std::string(R"TS();
a./**/;)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"x"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJavaScriptModulesWithBackticks, TestJavaScriptModulesWithBackticks);

// organizeImports_exportLeadingComment_test.go

// organizeImports_exportLeadingComment_test.go
static void TestOrganizeImports_exportLeadingComment_notDuplicated(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		std::vector<anonStruct0> cases = std::vector<anonStruct0>{anonStruct0{.name = "singleComment", .content = R"TS(// a
export { a } from "a";
console.log(a);)TS"}, anonStruct0{.name = "multipleComments", .content = R"TS(// a
// a
export { a } from "a";
console.log(a);)TS"}, anonStruct0{.name = "secondExport", .content = R"TS(export { a } from "a";
// b
export { b } from "b";
console.log(a, b);)TS"}, anonStruct0{.name = "secondExportWithBlankLine", .content = R"TS(export { a } from "a";

// b
export { b } from "b";
console.log(a, b);)TS"}, anonStruct0{.name = "commentWithBlankLine", .content = R"TS(// a

export { a } from "a";
console.log(a);)TS"}};
		{
			int _ = 0;
			for (auto&& tc : cases) {
				t->Run(tc.name, [&](gostd::testing::T* t) {
	t->Parallel();
	auto __fsp = fourslash::NewFourslash(t, nullptr, tc.content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->VerifyOrganizeImports(t, tc.content, lsproto::CodeActionKindSourceSortImportsTs, nullptr);
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_exportLeadingComment_notDuplicated, TestOrganizeImports_exportLeadingComment_notDuplicated);

// organizeImports_removeUnused_preservesMultiline_test.go

// organizeImports_removeUnused_preservesMultiline_test.go
static void TestOrganizeImports_removeUnusedUsesLanguageServiceFormatOptions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import {
    a,
    b,
    c,
} from "module";

export { a, c };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto preferences = lsutil::ParseUserPreferences(lsutil::JsonObject{{"editor", lsutil::JsonObject{{"tabSize", 2}, {"insertSpaces", false}}}});
		f->VerifyOrganizeImports(t, R"TS(import {
	a,
	c
} from "module";

export { a, c };)TS", lsproto::CodeActionKindSourceRemoveUnusedImportsTs, std::make_shared<lsutil::UserPreferences>(preferences));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_removeUnusedUsesLanguageServiceFormatOptions, TestOrganizeImports_removeUnusedUsesLanguageServiceFormatOptions);

// tripleSlashRefPathCompletionAbsolutePaths_test.go

// tripleSlashRefPathCompletionAbsolutePaths_test.go
static void TestTripleSlashRefPathCompletionAbsolutePaths(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tests/cases/fourslash/tests/test0.ts
/// <reference path="/tests/cases/f/*0*/
// @Filename: /tests/cases/fourslash/tests/test1.ts
/// <reference path="/tests/cases/fourslash/*1*/
// @Filename: /tests/cases/fourslash/tests/test2.ts
/// <reference path="/tests/cases/fourslash//*2*/
// @Filename: /tests/cases/fourslash/f1.ts
/*f1*/
// @Filename: /tests/cases/fourslash/f2.tsx
/*f2*/
// @Filename: /tests/cases/fourslash/folder/f1.ts
/*subf1*/
// @Filename: /tests/cases/fourslash/f3.js
/*f3*/
// @Filename: /tests/cases/fourslash/f4.jsx
/*f4*/
// @Filename: /tests/cases/fourslash/e1.ts
/*e1*/
// @Filename: /tests/cases/fourslash/e2.js
/*e2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"0", "1"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"fourslash"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"e1.ts", "f1.ts", "f2.tsx", "folder", "tests"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTripleSlashRefPathCompletionAbsolutePaths, TestTripleSlashRefPathCompletionAbsolutePaths);

// tripleSlashRefPathCompletionContext_test.go

// tripleSlashRefPathCompletionContext_test.go
static void TestTripleSlashRefPathCompletionContext(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: f.ts
/*f*/
// @Filename: test.ts
/// <reference path/*0*/=/*1*/"/*8*/
/// <reference path/*2*/=/*3*/"/*9*/"/*4*/ /*5*///*6*/>/*7*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"0", "1", "2", "3", "4", "5", "6", "7"}, nullptr);
		f->VerifyCompletions(t, std::vector<std::string>{"8", "9"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"f.ts"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTripleSlashRefPathCompletionContext, TestTripleSlashRefPathCompletionContext);

// tripleSlashRefPathCompletionExtensionsAllowJSFalse_test.go

// tripleSlashRefPathCompletionExtensionsAllowJSFalse_test.go
static void TestTripleSlashRefPathCompletionExtensionsAllowJSFalse(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: test0.ts
/// <reference path="/*0*/
/// <reference path=".//*1*/
/// <reference path="./f/*2*/
// @Filename: f1.ts

// @Filename: f1.js

// @Filename: f1.d.ts

// @Filename: f1.tsx

// @Filename: f1.js

// @Filename: f1.jsx

// @Filename: f1.cs
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, f->Markers(), tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"f1.d.ts", "f1.ts", "f1.tsx"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTripleSlashRefPathCompletionExtensionsAllowJSFalse, TestTripleSlashRefPathCompletionExtensionsAllowJSFalse);

// tripleSlashRefPathCompletionExtensionsAllowJSTrue_test.go

// tripleSlashRefPathCompletionExtensionsAllowJSTrue_test.go
static void TestTripleSlashRefPathCompletionExtensionsAllowJSTrue(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: test0.ts
/// <reference path="/*0*/
/// <reference path=".//*1*/
/// <reference path="./f/*2*/
// @Filename: f1.ts

// @Filename: f1.js

// @Filename: f1.d.ts

// @Filename: f1.tsx

// @Filename: f1.js

// @Filename: f1.jsx

// @Filename: f1.cs
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, f->Markers(), tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"f1.d.ts", "f1.js", "f1.jsx", "f1.ts", "f1.tsx"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTripleSlashRefPathCompletionExtensionsAllowJSTrue, TestTripleSlashRefPathCompletionExtensionsAllowJSTrue);

// tripleSlashRefPathCompletionHiddenFile_test.go

// tripleSlashRefPathCompletionHiddenFile_test.go
static void TestTripleSlashRefPathCompletionHiddenFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: f.ts
/*f*/
// @Filename: .hidden.ts
/*hidden*/
// @Filename: test.ts
/// <reference path="/*0*/
/// <reference path="[|./*1*/|]
/// <reference path=".//*2*/
/// <reference path=".\/*3*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"0", "2", "3"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f.ts", .Detail = std::string("f.ts")})}})}));
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f.ts", .Detail = std::string("f.ts"), .TextEdit = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(lsproto::TextEditOrInsertReplaceEdit{.TextEdit = std::make_shared<lsproto::TextEdit>(lsproto::TextEdit{.Range = f->Ranges()[0]->LSRange, .NewText = "f.ts"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTripleSlashRefPathCompletionHiddenFile, TestTripleSlashRefPathCompletionHiddenFile);

// tripleSlashRefPathCompletionRootdirs_test.go

// tripleSlashRefPathCompletionRootdirs_test.go
static void TestTripleSlashRefPathCompletionRootdirs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @rootDirs: sub/src1,src2
// @Filename: src2/test0.ts
/// <reference path="./mo/*0*/
// @Filename: src2/module0.ts
export var w = 0;
// @Filename: sub/src1/module1.ts
export var x = 0;
// @Filename: sub/src1/module2.ts
export var y = 0;
// @Filename: sub/src1/more/module3.ts
export var z = 0;
// @Filename: f1.ts
/*f1*/
// @Filename: f2.tsx
/*f2*/
// @Filename: folder/f1.ts
/*subf1*/
// @Filename: f3.js
/*f3*/
// @Filename: f4.jsx
/*f4*/
// @Filename: e1.ts
/*e1*/
// @Filename: e2.js
/*e2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "0", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"module0.ts"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTripleSlashRefPathCompletionRootdirs, TestTripleSlashRefPathCompletionRootdirs);

// tripleSlashReferenceResolutionMode_test.go

// tripleSlashReferenceResolutionMode_test.go
static void TestTripleSlashReferenceResolutionMode(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
 { "compilerOptions": { "lib": ["es5"], "module": "nodenext", "declaration": true, "strict": true, "outDir": "out" }, "files": ["./index.ts"] }
// @Filename: /home/src/workspaces/project/package.json
 { "private": true, "type": "commonjs" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/package.json
{ "name": "pkg", "version": "0.0.1", "exports": { "require": "./require.cjs", "default": "./import.js" }, "type": "module" }
// @Filename: /home/src/workspaces/project/node_modules/pkg/require.d.cts
export {};
export interface PkgRequireInterface { member: any; }
declare global { const pkgRequireGlobal: PkgRequireInterface; }
// @Filename: /home/src/workspaces/project/node_modules/pkg/import.d.ts
export {};
export interface PkgImportInterface { field: any; }
declare global { const pkgImportGlobal: PkgImportInterface; }
// @Filename: /home/src/workspaces/project/index.ts
/// <reference types="pkg" resolution-mode="import" />
pkgImportGlobal;
export {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToFile(t, "/home/src/workspaces/project/index.ts");
		f->VerifyNumberOfErrorsInCurrentFile(t, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestTripleSlashReferenceResolutionMode, TestTripleSlashReferenceResolutionMode);

} // namespace
