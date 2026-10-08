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

// statecallhierarchy_test.go

// statecallhierarchy_test.go
static void TestCallHierarchyAcrossProject(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @stateBaseline: true
// @Filename: /projects/temp/temp.ts
/*temp*/let x = 10
// @Filename: /projects/temp/tsconfig.json
{}
// @Filename: /projects/container/lib/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	references: [],
	files: [
		"index.ts",
		"bar.ts",
		"baz.ts"
	],
}
// @Filename: /projects/container/lib/index.ts
export function /*call*/createModelReference() {}
// @Filename: /projects/container/lib/bar.ts
import { createModelReference } from "./index";
function openElementsAtEditor() {
  createModelReference();
}
// @Filename: /projects/container/lib/baz.ts
import { createModelReference } from "./index";
function registerDefaultLanguageCommand() {
  createModelReference();
}
// @Filename: /projects/container/exec/tsconfig.json
{
	"files": ["./index.ts"],
	"references": [
		{ "path": "../lib" },
	],
}
// @Filename: /projects/container/exec/index.ts
import { createModelReference } from "../lib";
function openElementsAtEditor1() {
  createModelReference();
}
// @Filename: /projects/container/compositeExec/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"files": ["./index.ts"],
	"references": [
		{ "path": "../lib" },
	],
}
// @Filename: /projects/container/compositeExec/index.ts
import { createModelReference } from "../lib";
function openElementsAtEditor2() {
  createModelReference();
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "call");
		f->GoToMarker(t, "temp");
		f->GoToMarker(t, "call");
		f->VerifyBaselineCallHierarchy(t);
		f->CloseFileOfMarker(t, "temp");
		f->GoToMarker(t, "temp");
		f->CloseFileOfMarker(t, "call");
		f->CloseFileOfMarker(t, "temp");
		f->GoToMarker(t, "temp");
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyAcrossProject, TestCallHierarchyAcrossProject);

// statecodelens_test.go

// statecodelens_test.go
static void TestCodeLensAcrossProjects(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true
// @Filename: /projects/temp/temp.ts
/*temp*/let x = 10
// @Filename: /projects/temp/tsconfig.json
{}
// @Filename: /projects/container/lib/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	references: [],
	files: [
		"index.ts",
		"bar.ts"
	],
}
// @Filename: /projects/container/lib/index.ts
/*impl*/
export interface Pointable {
  getX(): number;
  getY(): number;
}
export const val = 42;
// @Filename: /projects/container/lib/bar.ts
import { Pointable } from "./index";
class Point implements Pointable {
  getX(): number {
    return 0;
  }
  getY(): number {
    return 0;
  }
}
// @Filename: /projects/container/exec/tsconfig.json
{
	"files": ["./index.ts"],
	"references": [
		{ "path": "../lib" },
	],
}
// @Filename: /projects/container/exec/index.ts
import { Pointable } from "../lib";
class Point1 implements Pointable {
  getX(): number {
    return 0;
  }
  getY(): number {
    return 0;
  }
}
// @Filename: /projects/container/compositeExec/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"files": ["./index.ts"],
	"references": [
		{ "path": "../lib" },
	],
}
// @Filename: /projects/container/compositeExec/index.ts
import { Pointable } from "../lib";
class Point2 implements Pointable {
  getX(): number {
    return 0;
  }
  getY(): number {
    return 0;
  }
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "impl");
		f->GoToMarker(t, "temp");
		f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLensUserPreferences = lsutil::CodeLensUserPreferences{.ReferencesCodeLensEnabled = Tristate::True, .ImplementationsCodeLensEnabled = Tristate::True, .ReferencesCodeLensShowOnAllFunctions = Tristate::True, .ImplementationsCodeLensShowOnInterfaceMethods = Tristate::True, .ImplementationsCodeLensShowOnAllClassMethods = Tristate::True}}));
		f->CloseFileOfMarker(t, "temp");
		f->GoToMarker(t, "temp");
		f->CloseFileOfMarker(t, "impl");
		f->CloseFileOfMarker(t, "temp");
		f->GoToMarker(t, "temp");
	});
}
REGISTER_FOURSLASH_TEST(TestCodeLensAcrossProjects, TestCodeLensAcrossProjects);

// statecodelens_test.go
static void TestCodeLensOnFunctionAcrossProjects1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @filename: ./a/tsconfig.json
{
  "compilerOptions": {
	"composite": true,
	"declaration": true,
	"declarationMaps": true,
	"outDir": "./dist",
	"rootDir": "src"
  },
  "include": ["./src"]
}

// @filename: ./a/src/foo.ts
export function aaa() {}
aaa();

// @filename: ./b/tsconfig.json
{
  "compilerOptions": {
	"composite": true,
	"declaration": true,
	"declarationMaps": true,
	"outDir": "./dist",
	"rootDir": "src"
  },
  "references": [{ "path": "../a" }],
  "include": ["./src"]
}

// @filename: ./b/src/bar.ts
import * as foo from '../../a/dist/foo.js';
foo.aaa();
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLensUserPreferences = lsutil::CodeLensUserPreferences{.ReferencesCodeLensEnabled = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestCodeLensOnFunctionAcrossProjects1, TestCodeLensOnFunctionAcrossProjects1);

// statedeclarationmaps_test.go

// statedeclarationmaps_test.go
static void TestDeclarationMapsOpeningOriginalLocationProject(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		{
			int _ = 0;
			for (auto&& disableSourceOfProjectReferenceRedirect : std::vector<bool>{false, true}) {
				t->Run((std::string("TestDeclarationMapsOpeningOriginalLocationProject") + IfElse(disableSourceOfProjectReferenceRedirect, "DisableSourceOfProjectReferenceRedirect", "")), [&](gostd::testing::T* t) {
	t->Parallel();
	auto content = gostd::sprintf(R"TS(
// @stateBaseline: true
// @Filename: a/a.ts
export class A { }
// @Filename: a/tsconfig.json
{}
// @Filename: a/a.d.ts
export declare class A {
}
//# sourceMappingURL=a.d.ts.map
// @Filename: a/a.d.ts.map
{
	"version": 3,
	"file": "a.d.ts",
	"sourceRoot": "",
	"sources": ["./a.ts"],
	"names": [],
	"mappings": "AAAA,qBAAa,CAAC;CAAI"
}
// @Filename: b/b.ts
import {A} from "../a/a";
new /*1*/A();
// @Filename: b/tsconfig.json
{
	"compilerOptions": {
		"disableSourceOfProjectReferenceRedirect": %t
	},
	"references": [
		{ "path": "../a" }
	]
})TS", {bool(disableSourceOfProjectReferenceRedirect)});
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->VerifyBaselineFindAllReferences(t, {"1"});
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestDeclarationMapsOpeningOriginalLocationProject, TestDeclarationMapsOpeningOriginalLocationProject);

// statedeclarationmaps_test.go
static void TestDeclarationMapTestCasesForMaps(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		struct testCase {
			std::string name{};
			std::string goToMarker{};
			std::string opMarker{};
		};
		std::vector<testCase> tests = std::vector<testCase>{testCase{"FindAllRefs", "userFnA", "userFnA"}, testCase{"FindAllRefsStartingAtDefinition", "userFnA", "fnADef"}, testCase{"FindAllRefsTargetDoesNotExist", "userFnB", "userFnB"}, testCase{"Rename", "userFnA", "userFnA"}, testCase{"RenameStartingAtDefinition", "userFnA", "fnADef"}, testCase{"RenameTargetDoesNotExist", "userFnB", "userFnB"}};
		{
			int _ = 0;
			for (auto&& tc : tests) {
				t->Run((std::string("TestDeclarationMaps") + tc.name), [&](gostd::testing::T* t) {
	t->Parallel();
	auto content = R"TS(
// @stateBaseline: true
// @Filename: a/a.ts
export function /*fnADef*/fnA() {}
export interface IfaceA {}
export const instanceA: IfaceA = {};
// @Filename: a/tsconfig.json
{
	"compilerOptions": {
		"outDir": "bin",
		"declarationMap": true,
		"composite": true
	}
}
// @Filename: a/bin/a.d.ts.map
{
	"version": 3,
	"file": "a.d.ts",
	"sourceRoot": "",
	"sources": ["../a.ts"],
	"names": [],
	"mappings": "AAAA,wBAAgB,GAAG,SAAK;AACxB,MAAM,WAAW,MAAM;CAAG;AAC1B,eAAO,MAAM,SAAS,EAAE,MAAW,CAAC"
}
// @Filename: a/bin/a.d.ts
export declare function fnA(): void;
export interface IfaceA {
}
export declare const instanceA: IfaceA;
//# sourceMappingURL=a.d.ts.map
// @Filename: b/tsconfig.json
{
	"compilerOptions": {
		"outDir": "bin",
		"declarationMap": true,
		"composite": true
	}
}
// @Filename: b/bin/b.d.ts.map
{
	"version": 3,
	"file": "b.d.ts",
	"sourceRoot": "",
	"sources": ["../b.ts"],
	"names": [],
	"mappings": "AAAA,wBAAgB,GAAG,SAAK"
}
// @Filename: b/bin/b.d.ts
export declare function fnB(): void;
//# sourceMappingURL=b.d.ts.map
// @Filename: user/user.ts
import * as a from "../a/bin/a";
import * as b from "../b/bin/b";
export function fnUser() { a./*userFnA*/fnA(); b./*userFnB*/fnB(); a.instanceA; }
// @Filename: dummy/dummy.ts
/*dummy*/export const a = 10;
// @Filename: dummy/tsconfig.json
{})TS";
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->GoToMarker(t, tc.goToMarker);
	if ((tc.name.find("Rename") == 0)) {
		f->VerifyBaselineRename(t, nullptr, {tc.opMarker});
	} else {
		f->VerifyBaselineFindAllReferences(t, {tc.opMarker});
	}
	f->CloseFileOfMarker(t, tc.goToMarker);
	f->GoToMarker(t, "dummy");
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestDeclarationMapTestCasesForMaps, TestDeclarationMapTestCasesForMaps);

// statedeclarationmaps_test.go
static void TestDeclarationMapsWorkspaceSymbols(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(// @stateBaseline: true
// @Filename: a/a.ts
export function fnA() {}
export interface IfaceA {}
export const instanceA: IfaceA = {};
// @Filename: a/tsconfig.json
{
	"compilerOptions": {
		"outDir": "bin",
		"declarationMap": true,
		"composite": true
	}
}
// @Filename: a/bin/a.d.ts.map
{
	"version": 3,
	"file": "a.d.ts",
	"sourceRoot": "",
	"sources": ["../a.ts"],
	"names": [],
	"mappings": "AAAA,wBAAgB,GAAG,SAAK;AACxB,MAAM,WAAW,MAAM;CAAG;AAC1B,eAAO,MAAM,SAAS,EAAE,MAAW,CAAC"
}
// @Filename: a/bin/a.d.ts
export declare function fnA(): void;
export interface IfaceA {
}
export declare const instanceA: IfaceA;
//# sourceMappingURL=a.d.ts.map
// @Filename: b/b.ts
export function fnB() {}
// @Filename: b/c.ts
export function fnC() {}
// @Filename: b/tsconfig.json
{
	"compilerOptions": {
		"outDir": "bin",
		"declarationMap": true,
		"composite": true
	}
}
// @Filename: b/bin/b.d.ts.map
{
	"version": 3,
	"file": "b.d.ts",
	"sourceRoot": "",
	"sources": ["../b.ts"],
	"names": [],
	"mappings": "AAAA,wBAAgB,GAAG,SAAK"
}
// @Filename: b/bin/b.d.ts
export declare function fnB(): void;
//# sourceMappingURL=b.d.ts.map
// @Filename: user/user.ts
/*user*/import * as a from "../a/a";
import * as b from "../b/b";
export function fnUser() {
	a.fnA();
	b.fnB();
	a.instanceA;
}
// @Filename: user/tsconfig.json
{
	"references": [
		{ "path": "../a" },
		{ "path": "../b" }
	]
}
// @Filename: dummy/dummy.ts
/*dummy*/export const a = 10;
// @Filename: dummy/tsconfig.json
{})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "user");
		f->VerifyBaselineWorkspaceSymbol(t, "fn");
		f->CloseFileOfMarker(t, "user");
		f->GoToMarker(t, "dummy");
	});
}
REGISTER_FOURSLASH_TEST(TestDeclarationMapsWorkspaceSymbols, TestDeclarationMapsWorkspaceSymbols);

// statedeclarationmaps_test.go
static void TestDeclarationMapsFindAllRefsDefinitionInMappedFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true 
//@Filename: a/a.ts
export function f() {}
// @Filename: a/tsconfig.json
{
	"compilerOptions": {
		"outDir": "../bin",
		"declarationMap": true,
		"composite": true
	}
}
//@Filename: b/b.ts
import { f } from "../bin/a";
/*1*/f();
// @Filename: b/tsconfig.json
{
	"references": [
		{ "path": "../a" }
	]
}
// @Filename: bin/a.d.ts
export declare function f(): void;
//# sourceMappingURL=a.d.ts.map
// @Filename: bin/a.d.ts.map
{
	"version":3,
	"file":"a.d.ts",
	"sourceRoot":"",
	"sources":["a.ts"],
	"names":[],
	"mappings":"AAAA,wBAAgB,CAAC,SAAK"
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestDeclarationMapsFindAllRefsDefinitionInMappedFile, TestDeclarationMapsFindAllRefsDefinitionInMappedFile);

// statedeclarationmaps_test.go
static void TestDeclarationMapsRename(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		struct testCase {
			std::string name{};
			bool dontBuild{};
			bool mainWithNoRef{};
			bool disableSourceOfProjectReferenceRedirect{};
			bool tsconfigNotSolution{};
		};
		{
			int _ = 0;
			for (auto&& tc : std::vector<testCase>{testCase{.name = "ProjectReferences", .dontBuild = true}, testCase{.name = "DisableSourceOfProjectReferenceRedirect", .disableSourceOfProjectReferenceRedirect = true}, testCase{.name = "SourceMaps", .mainWithNoRef = true}, testCase{.name = "SourceMapsNotSolution", .mainWithNoRef = true, .tsconfigNotSolution = true}}) {
				auto buildStr = IfElse(!tc.dontBuild, "// @tsc: --build /myproject/dependency,--build /myproject/main", "");
				auto mainRefsStr = IfElse(!tc.mainWithNoRef, R"TS("references": [{ "path": "../dependency" }])TS", "");
				auto filesStr = IfElse(!tc.tsconfigNotSolution, R"TS("files": [],)TS", "");
				auto content = gostd::sprintf(R"TS(
// @stateBaseline: true 
%s
//@Filename: myproject/dependency/FnS.ts
/*firstLine*/export function fn1() { }
export function fn2() { }
export function /*rename*/fn3() { }
export function fn4() { }
export function fn5() { }
/*lastLine*/
// @Filename: myproject/dependency/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
		"declarationMap": true,
		"declarationDir": "../decls"
	}
}
//@Filename: myproject/main/main.ts
import {
	fn1,
	fn2,
	fn3,
	fn4,
	fn5
} from "../decls/FnS";

fn1();
fn2();
fn3();
fn4();
fn5();
// @Filename: myproject/main/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
		"declarationMap": true,
		"disableSourceOfProjectReferenceRedirect": %t
	},
	%s
}
// @Filename: myproject/tsconfig.json
{
	"compilerOptions": {
		"disableSourceOfProjectReferenceRedirect": %t
	},
	%s
	"references": [
		{ "path": "dependency" },
		{ "path": "main" }
	]
}
// @Filename: random/random.ts
/*dummy*/export const a = 10;
// @Filename: random/tsconfig.json
{})TS", {buildStr, tc.disableSourceOfProjectReferenceRedirect, mainRefsStr, tc.disableSourceOfProjectReferenceRedirect, filesStr});
				t->Run((std::string("TestDeclarationMapsRenameWith") + tc.name), [&](gostd::testing::T* t) {
	t->Parallel();
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->GoToMarker(t, "dummy");
	f->VerifyBaselineRename(t, nullptr, {"rename"});
	f->CloseFileOfMarker(t, "dummy");
	f->GoToMarker(t, "dummy");
	f->CloseFileOfMarker(t, "rename");
	f->CloseFileOfMarker(t, "dummy");
	f->GoToMarker(t, "dummy");
	});
				t->Run(((std::string("TestDeclarationMapsRenameWith") + tc.name) + std::string("Edit")), [&](gostd::testing::T* t) {
	t->Parallel();
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->VerifyBaselineRename(t, nullptr, {"rename"});
	f->GoToMarker(t, "firstLine");
	f->Insert(t, R"TS(function fooBar() { }
)TS");
	f->VerifyBaselineRename(t, nullptr, {"rename"});
	});
				t->Run(((std::string("TestDeclarationMapsRenameWith") + tc.name) + std::string("EditEnd")), [&](gostd::testing::T* t) {
	t->Parallel();
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->VerifyBaselineRename(t, nullptr, {"rename"});
	f->GoToMarker(t, "lastLine");
	f->Insert(t, "const x = 10;");
	f->VerifyBaselineRename(t, nullptr, {"rename"});
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestDeclarationMapsRename, TestDeclarationMapsRename);

// statedeclarationmaps_test.go
static void TestDeclarationMapsNonMonotonicMappings(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /src/index.ts
export function a() {}
export function b() {}
// @Filename: /src/indexdef.d.ts.map
{
	"version": 3,
	"file": "indexdef.d.ts",
	"sourceRoot": "",
	"sources": ["index.ts"],
	"names": [],
	"mappings": "AACA,wBAAgB,CADhB;AAAA,wBAAgB"
}
// @Filename: /src/indexdef.d.ts
export declare function b(): void;
export declare function a(): void;
//# sourceMappingURL=indexdef.d.ts.map
// @Filename: /src/user.ts
import { a, b } from "./indexdef";
/*1*/a();
/*2*/b();
// @Filename: /src/tsconfig.json
{})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestDeclarationMapsNonMonotonicMappings, TestDeclarationMapsNonMonotonicMappings);

// statefindallrefs_test.go

// statefindallrefs_test.go
static void TestFindAllRefsSolutionReferencingDefaultProjectDirectly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true 
// @tsc: --build /myproject/tsconfig.json
// @Filename: dummy/dummy.ts
/*dummy*/const x = 1;
// @Filename: dummy/tsconfig.json
{ }
// @Filename: myproject/tsconfig.json
{
	"files": [],
	"references": [{ "path": "./tsconfig-src.json" }]
}
// @Filename: myproject/tsconfig-src.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target",
		"declarationMap": true,
	},
	"include": ["./src/\**/*"]
}
// @Filename: myproject/src/main.ts
import { foo } from './helpers/functions';
export { /*mainFoo*/foo };
// @Filename: myproject/src/helpers/functions.ts
export function foo() { return 1; }
// @Filename: myproject/indirect3/tsconfig.json
{ }
// @Filename: myproject/indirect3/main.ts
import { /*fooIndirect3Import*/foo } from '../target/src/main';
foo()
export function bar() {}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->CloseFileOfMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->VerifyBaselineFindAllReferences(t, {"mainFoo"});
		f->CloseFileOfMarker(t, "mainFoo");
		f->VerifyBaselineFindAllReferences(t, {"fooIndirect3Import"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsSolutionReferencingDefaultProjectDirectly, TestFindAllRefsSolutionReferencingDefaultProjectDirectly);

// statefindallrefs_test.go
static void TestFindAllRefsSolutionReferencingDefaultProjectIndirectly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true 
// @tsc: --build /myproject/tsconfig.json
// @Filename: dummy/dummy.ts
/*dummy*/const x = 1;
// @Filename: dummy/tsconfig.json
{ }
// @Filename: myproject/tsconfig.json
{
	"files": [],
	"references":  [
		{ "path": "./tsconfig-indirect1.json" },
		{ "path": "./tsconfig-indirect2.json" },
	]
}
// @Filename: myproject/tsconfig-src.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target",
		"declarationMap": true,
	},
	"include": ["./src/\**/*"]
}
// @Filename: myproject/src/main.ts
import { foo } from './helpers/functions';
export { /*mainFoo*/foo };
// @Filename: myproject/src/helpers/functions.ts
export function foo() { return 1; }
// @Filename: myproject/indirect3/tsconfig.json
{ }
// @Filename: myproject/indirect3/main.ts
import { /*fooIndirect3Import*/foo } from '../target/src/main';
foo()
export function bar() {}
// @FileName: myproject/indirect1/main.ts
export const indirect = 1;
// @Filename: myproject/tsconfig-indirect1.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target/",
	},
	"files": [
		"./indirect1/main.ts"
	],
	"references": [
		{
			"path": "./tsconfig-src.json"
		}
	]
}
// @FileName: myproject/indirect2/main.ts
export const indirect = 1;
// @Filename: myproject/tsconfig-indirect2.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target/",
	},
	"files": [
		"./indirect2/main.ts"
	],
	"references": [
		{
			"path": "./tsconfig-src.json"
		}
	]
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->CloseFileOfMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->VerifyBaselineFindAllReferences(t, {"mainFoo"});
		f->CloseFileOfMarker(t, "mainFoo");
		f->VerifyBaselineFindAllReferences(t, {"fooIndirect3Import"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsSolutionReferencingDefaultProjectIndirectly, TestFindAllRefsSolutionReferencingDefaultProjectIndirectly);

// statefindallrefs_test.go
static void TestFindAllRefsSolutionWithDisableReferencedProjectLoadReferencingDefaultProjectDirectly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true 
// @tsc: --build /myproject/tsconfig.json
// @Filename: dummy/dummy.ts
/*dummy*/const x = 1;
// @Filename: dummy/tsconfig.json
{ }
// @Filename: myproject/tsconfig.json
{
	"compilerOptions": {
		"disableReferencedProjectLoad": true
	},
	"files": [],
	"references": [{ "path": "./tsconfig-src.json" }]
}
// @Filename: myproject/tsconfig-src.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target",
		"declarationMap": true,
	},
	"include": ["./src/\**/*"]
}
// @Filename: myproject/src/main.ts
import { foo } from './helpers/functions';
export { /*mainFoo*/foo };
// @Filename: myproject/src/helpers/functions.ts
export function foo() { return 1; }
// @Filename: myproject/indirect3/tsconfig.json
{ }
// @Filename: myproject/indirect3/main.ts
import { /*fooIndirect3Import*/foo } from '../target/src/main';
foo()
export function bar() {}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->CloseFileOfMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->VerifyBaselineFindAllReferences(t, {"mainFoo"});
		f->CloseFileOfMarker(t, "mainFoo");
		f->VerifyBaselineFindAllReferences(t, {"fooIndirect3Import"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsSolutionWithDisableReferencedProjectLoadReferencingDefaultProjectDirectly, TestFindAllRefsSolutionWithDisableReferencedProjectLoadReferencingDefaultProjectDirectly);

// statefindallrefs_test.go
static void TestFindAllRefsSolutionReferencingDefaultProjectIndirectlyThroughDisableReferencedProjectLoad(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true 
// @tsc: --build /myproject/tsconfig.json
// @Filename: dummy/dummy.ts
/*dummy*/const x = 1;
// @Filename: dummy/tsconfig.json
{ }
// @Filename: myproject/tsconfig.json
{
	"files": [],
	"references":  [
		{ "path": "./tsconfig-indirect1.json" },
		{ "path": "./tsconfig-indirect2.json" },
	]
}
// @Filename: myproject/tsconfig-src.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target",
		"declarationMap": true,
	},
	"include": ["./src/\**/*"]
}
// @Filename: myproject/src/main.ts
import { foo } from './helpers/functions';
export { /*mainFoo*/foo };
// @Filename: myproject/src/helpers/functions.ts
export function foo() { return 1; }
// @Filename: myproject/indirect3/tsconfig.json
{ }
// @Filename: myproject/indirect3/main.ts
import { /*fooIndirect3Import*/foo } from '../target/src/main';
foo()
export function bar() {}
// @FileName: myproject/indirect1/main.ts
export const indirect = 1;
// @Filename: myproject/tsconfig-indirect1.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target/",
		"disableReferencedProjectLoad": true,
	},
	"files": [
		"./indirect1/main.ts"
	],
	"references": [
		{
			"path": "./tsconfig-src.json"
		}
	]
}
// @FileName: myproject/indirect2/main.ts
export const indirect = 1;
// @Filename: myproject/tsconfig-indirect2.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target/",
		"disableReferencedProjectLoad": true,
	},
	"files": [
		"./indirect2/main.ts"
	],
	"references": [
		{
			"path": "./tsconfig-src.json"
		}
	]
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->CloseFileOfMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->VerifyBaselineFindAllReferences(t, {"mainFoo"});
		f->CloseFileOfMarker(t, "mainFoo");
		f->VerifyBaselineFindAllReferences(t, {"fooIndirect3Import"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsSolutionReferencingDefaultProjectIndirectlyThroughDisableReferencedProjectLoad, TestFindAllRefsSolutionReferencingDefaultProjectIndirectlyThroughDisableReferencedProjectLoad);

// statefindallrefs_test.go
static void TestFindAllRefsSolutionReferencingDefaultProjectIndirectlyThroughDisableReferencedProjectLoadInOneButWithoutItInAnother(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true 
// @tsc: --build /myproject/tsconfig.json
// @Filename: dummy/dummy.ts
/*dummy*/const x = 1;
// @Filename: dummy/tsconfig.json
{ }
// @Filename: myproject/tsconfig.json
{
	"files": [],
	"references":  [
		{ "path": "./tsconfig-indirect1.json" },
		{ "path": "./tsconfig-indirect2.json" },
	]
}
// @Filename: myproject/tsconfig-src.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target",
		"declarationMap": true,
	},
	"include": ["./src/\**/*"]
}
// @Filename: myproject/src/main.ts
import { foo } from './helpers/functions';
export { /*mainFoo*/foo };
// @Filename: myproject/src/helpers/functions.ts
export function foo() { return 1; }
// @Filename: myproject/indirect3/tsconfig.json
{ }
// @Filename: myproject/indirect3/main.ts
import { /*fooIndirect3Import*/foo } from '../target/src/main';
foo()
export function bar() {}
// @FileName: myproject/indirect1/main.ts
export const indirect = 1;
// @Filename: myproject/tsconfig-indirect1.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target/",
		"disableReferencedProjectLoad": true,
	},
	"files": [
		"./indirect1/main.ts"
	],
	"references": [
		{
			"path": "./tsconfig-src.json"
		}
	]
}
// @FileName: myproject/indirect2/main.ts
export const indirect = 1;
// @Filename: myproject/tsconfig-indirect2.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target/",
	},
	"files": [
		"./indirect2/main.ts"
	],
	"references": [
		{
			"path": "./tsconfig-src.json"
		}
	]
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->CloseFileOfMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->VerifyBaselineFindAllReferences(t, {"mainFoo"});
		f->CloseFileOfMarker(t, "mainFoo");
		f->VerifyBaselineFindAllReferences(t, {"fooIndirect3Import"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsSolutionReferencingDefaultProjectIndirectlyThroughDisableReferencedProjectLoadInOneButWithoutItInAnother, TestFindAllRefsSolutionReferencingDefaultProjectIndirectlyThroughDisableReferencedProjectLoadInOneButWithoutItInAnother);

// statefindallrefs_test.go
static void TestFindAllRefsProjectWithOwnFilesReferencingFileFromReferencedProject(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true 
// @tsc: --build /myproject/tsconfig.json
// @Filename: dummy/dummy.ts
/*dummy*/const x = 1;
// @Filename: dummy/tsconfig.json
{ }
// @Filename: myproject/tsconfig.json
{
	"files": ["./own/main.ts"],
	"references": [{ "path": "./tsconfig-src.json" }]
}
// @Filename: myproject/own/main.ts
import { foo } from '../target/src/main';
foo();
export function bar() {}
// @Filename: myproject/tsconfig-src.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "./target",
		"declarationMap": true,
	},
	"include": ["./src/\**/*"]
}
// @Filename: myproject/src/main.ts
import { foo } from './helpers/functions';
export { /*mainFoo*/foo };
// @Filename: myproject/src/helpers/functions.ts
export function foo() { return 1; }
// @Filename: myproject/indirect3/tsconfig.json
{ }
// @Filename: myproject/indirect3/main.ts
import { /*fooIndirect3Import*/foo } from '../target/src/main';
foo()
export function bar() {}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->CloseFileOfMarker(t, "mainFoo");
		f->GoToMarker(t, "dummy");
		f->CloseFileOfMarker(t, "dummy");
		f->VerifyBaselineFindAllReferences(t, {"mainFoo"});
		f->CloseFileOfMarker(t, "mainFoo");
		f->VerifyBaselineFindAllReferences(t, {"fooIndirect3Import"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsProjectWithOwnFilesReferencingFileFromReferencedProject, TestFindAllRefsProjectWithOwnFilesReferencingFileFromReferencedProject);

// statefindallrefs_test.go
static void TestFindAllRefsRootOfReferencedProject(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		{
			int _ = 0;
			for (auto&& disableSourceOfProjectReferenceRedirect : std::vector<bool>{false, true}) {
				t->Run((std::string("TestFindAllRefsRootOfReferencedProject") + IfElse(disableSourceOfProjectReferenceRedirect, "DeclarationMaps", "")), [&](gostd::testing::T* t) {
	t->Parallel();
	auto content = gostd::sprintf(R"TS(
// @stateBaseline: true
%s
// @Filename: src/common/input/keyboard.ts
function bar() { return "just a random function so .d.ts location doesnt match"; }
export function /*keyboard*/evaluateKeyboardEvent() { }
// @Filename: src/common/input/keyboard.test.ts
import { evaluateKeyboardEvent } from 'common/input/keyboard';
function testEvaluateKeyboardEvent() {
	return evaluateKeyboardEvent();
}
// @Filename: src/terminal.ts
/*terminal*/import { evaluateKeyboardEvent } from 'common/input/keyboard';
function foo() {
	return evaluateKeyboardEvent();
}
// @Filename: /src/common/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
		"declarationMap": true,
		"outDir": "../../out",
		"disableSourceOfProjectReferenceRedirect": %v,
		"paths": {
			"*": ["../*"],
		},
	},
	"include": ["./\**/*"]
}
// @Filename: src/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
		"declarationMap": true,
		"outDir": "../out",
		"disableSourceOfProjectReferenceRedirect": %v,
		"paths": {
			"common/*": ["./common/*"],
		},
		"tsBuildInfoFile": "../out/src.tsconfig.tsbuildinfo"
	},
	"include": ["./\**/*"],
	"references": [
		{ "path": "./common" },
	],
})TS", {IfElse(disableSourceOfProjectReferenceRedirect, "// @tsc: --build /src/tsconfig.json", ""), bool(disableSourceOfProjectReferenceRedirect), bool(disableSourceOfProjectReferenceRedirect)});
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->GoToMarker(t, "keyboard");
	f->GoToMarker(t, "terminal");
	f->VerifyBaselineFindAllReferences(t, {"keyboard"});
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsRootOfReferencedProject, TestFindAllRefsRootOfReferencedProject);

// statefindallrefs_test.go
static void TestFindAllRefsAncestorSiblingProjectsLoading(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		{
			int _ = 0;
			for (auto&& disableSolutionSearching : std::vector<bool>{false, true}) {
				t->Run((std::string("TestFindAllRefsAncestorSiblingProjectsLoading") + IfElse(disableSolutionSearching, "DisableSolutionSearching", "")), [&](gostd::testing::T* t) {
	t->Parallel();
	auto content = gostd::sprintf(R"TS(
// @stateBaseline: true
// @Filename: solution/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./compiler" },
		{ "path": "./services" },
	],
}
// @Filename: solution/compiler/tsconfig.json
{
	"compilerOptions": { 
		"composite": true,
		"disableSolutionSearching": %t,
	},
	"files": ["./types.ts", "./program.ts"]
}
// @Filename: solution/compiler/types.ts
namespace ts {
	export interface Program {
		getSourceFiles(): string[];
	}
}
// @Filename: solution/compiler/program.ts
namespace ts {
	export const program: Program = {
		/*notLocal*/getSourceFiles: () => [/*local*/getSourceFile()]
	};
	function getSourceFile() { return "something"; }
}
// @Filename: solution/services/tsconfig.json
{
	"compilerOptions": {
		"composite": true
	},
	"files": ["./services.ts"],
	"references": [
		{ "path": "../compiler" },
	],
}
// @Filename: solution/services/services.ts
/// <reference path="../compiler/types.ts" />
/// <reference path="../compiler/program.ts" />
namespace ts {
	const result = program.getSourceFiles();
})TS", {bool(disableSolutionSearching)});
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->VerifyBaselineFindAllReferences(t, {"local"});
	f->VerifyBaselineFindAllReferences(t, {"notLocal"});
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsAncestorSiblingProjectsLoading, TestFindAllRefsAncestorSiblingProjectsLoading);

// statefindallrefs_test.go
static void TestFindAllRefsOverlappingProjects(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true 
// @Filename: solution/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./a" },
		{ "path": "./b" },
		{ "path": "./c" },
		{ "path": "./d" },
	],
}
// @Filename: solution/a/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"files": ["./index.ts"]
}
// @Filename: solution/a/index.ts
export interface I {
	M(): void;
}
// @Filename: solution/b/tsconfig.json
{
	"compilerOptions": {
		"composite": true
	},
	"files": ["./index.ts"],
	"references": [
		{ "path": "../a" },
	],
}
// @Filename: solution/b/index.ts
import { I } from "../a";
export class B implements /**/I {
	M() {}
}
// @Filename: solution/c/tsconfig.json
{
	"compilerOptions": {
		"composite": true
	},
	"files": ["./index.ts"],
	"references": [
		{ "path": "../b" },
	],
}
// @Filename: solution/c/index.ts
import { I } from "../a";
import { B } from "../b";
export const C: I = new B();
// @Filename: solution/d/tsconfig.json
{
	"compilerOptions": {
		"composite": true
	},
	"files": ["./index.ts"],
	"references": [
		{ "path": "../c" },
	],
}
// @Filename: solution/d/index.ts
import { I } from "../a";
import { C } from "../c";
export const D: I = C;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOverlappingProjects, TestFindAllRefsOverlappingProjects);

// statefindallrefs_test.go
static void TestFindAllRefsTwoProjectsOpenAndOneProjectReferences(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true
// @Filename: /myproject/main/src/file1.ts
/*main*/export const mainConst = 10;
// @Filename: /myproject/main/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"references": [
		{ "path": "../core" },
		{ "path": "../indirect" },
		{ "path": "../noCoreRef1" },
		{ "path": "../indirectDisabledChildLoad1" },
		{ "path": "../indirectDisabledChildLoad2" },
		{ "path": "../refToCoreRef3" },
		{ "path": "../indirectNoCoreRef" }
	]
}
// @Filename: /myproject/core/src/file1.ts
export const /*find*/coreConst = 10;
// @Filename: /myproject/core/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
}
// @Filename: /myproject/noCoreRef1/src/file1.ts
export const noCoreRef1Const = 10;
// @Filename: /myproject/noCoreRef1/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
}
// @Filename: /myproject/indirect/src/file1.ts
export const indirectConst = 10;
// @Filename: /myproject/indirect/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"references": [
		{ "path": "../coreRef1" },
	]
}
// @Filename: /myproject/coreRef1/src/file1.ts
export const coreRef1Const = 10;
// @Filename: /myproject/coreRef1/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"references": [
		{ "path": "../core" },
	]
}
// @Filename: /myproject/indirectDisabledChildLoad1/src/file1.ts
export const indirectDisabledChildLoad1Const = 10;
// @Filename: /myproject/indirectDisabledChildLoad1/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
		"disableReferencedProjectLoad": true,
	},
	"references": [
		{ "path": "../coreRef2" },
	]
}
// @Filename: /myproject/coreRef2/src/file1.ts
export const coreRef2Const = 10;
// @Filename: /myproject/coreRef2/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"references": [
		{ "path": "../core" },
	]
}
// @Filename: /myproject/indirectDisabledChildLoad2/src/file1.ts
export const indirectDisabledChildLoad2Const = 10;
// @Filename: /myproject/indirectDisabledChildLoad2/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
		"disableReferencedProjectLoad": true,
	},
	"references": [
		{ "path": "../coreRef3" },
	]
}
// @Filename: /myproject/coreRef3/src/file1.ts
export const coreRef3Const = 10;
// @Filename: /myproject/coreRef3/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"references": [
		{ "path": "../core" },
	]
}
// @Filename: /myproject/refToCoreRef3/src/file1.ts
export const refToCoreRef3Const = 10;
// @Filename: /myproject/refToCoreRef3/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"references": [
		{ "path": "../coreRef3" },
	]
}
// @Filename: /myproject/indirectNoCoreRef/src/file1.ts
export const indirectNoCoreRefConst = 10;
// @Filename: /myproject/indirectNoCoreRef/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"references": [
		{ "path": "../noCoreRef2" },
	]
}
// @Filename: /myproject/noCoreRef2/src/file1.ts
export const noCoreRef2Const = 10;
// @Filename: /myproject/noCoreRef2/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "main");
		f->VerifyBaselineFindAllReferences(t, {"find"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsTwoProjectsOpenAndOneProjectReferences, TestFindAllRefsTwoProjectsOpenAndOneProjectReferences);

// statefindallrefs_test.go
static void TestFindAllRefsDoesNotTryToSearchProjectAfterItsUpdateDoesNotIncludeTheFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true 
// @Filename: /packages/babel-loader/tsconfig.json
{
	"compilerOptions": {
		"target": "ES2018",
		"module": "commonjs",
		"strict": true,
		"esModuleInterop": true,
		"composite": true,
		"rootDir": "src",
		"outDir": "dist"
	},
	"include": ["src"],
	"references": [{"path": "../core"}]
}
// @Filename: /packages/babel-loader/src/index.ts
/*change*/import type { Foo } from "../../core/src/index.js";
// @Filename: /packages/core/tsconfig.json
{
	"compilerOptions": {
		"target": "ES2018",
		"module": "commonjs",
		"strict": true,
		"esModuleInterop": true,
		"composite": true,
		"rootDir": "./src",
		"outDir": "./dist",
	},
	"include": ["./src"]
}
// @Filename: /packages/core/src/index.ts
import { Bar } from "./loading-indicator.js";
export type Foo = {};
const bar: Bar = {
	/*prop*/prop: 0
}
// @Filename: /packages/core/src/loading-indicator.ts
export interface Bar {
	prop: number;
}
const bar: Bar = {
	prop: 1
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "change");
		f->GoToMarker(t, "prop");
		f->GoToMarker(t, "change");
		f->Insert(t, "// comment");
		f->VerifyBaselineFindAllReferences(t, {"prop"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsDoesNotTryToSearchProjectAfterItsUpdateDoesNotIncludeTheFile, TestFindAllRefsDoesNotTryToSearchProjectAfterItsUpdateDoesNotIncludeTheFile);

// statefindallrefs_test.go
static void TestFindAllRefsOpenFileInConfiguredProjectThatWillBeRemoved(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true
// @Filename: /myproject/playground/tsconfig.json
{}
// @Filename: /myproject/playground/tests.ts
/*tests*/export function foo() {}
// @Filename: /myproject/playground/tsconfig-json/tsconfig.json
{
	"include": ["./src"]
}
// @Filename: /myproject/playground/tsconfig-json/src/src.ts
export function foobar() {}
// @Filename: /myproject/playground/tsconfig-json/tests/spec.ts
export function /*find*/bar() { }
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "tests");
		f->CloseFileOfMarker(t, "tests");
		f->VerifyBaselineFindAllReferences(t, {"find"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsOpenFileInConfiguredProjectThatWillBeRemoved, TestFindAllRefsOpenFileInConfiguredProjectThatWillBeRemoved);

// statefindallrefs_test.go
static void TestFindAllRefsSpecialHandlingOfLocalness(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		struct testCase {
			std::string name{};
			std::string definition{};
			std::string usage{};
			std::string referenceTerm{};
		};
		{
			int _ = 0;
			for (auto&& tc : std::vector<testCase>{testCase{"ArrowFunctionAssignment", R"TS(export const dog = () => { };)TS", R"TS(shared.dog();)TS", "dog"}, testCase{"ArrowFunctionAsObjectLiteralPropertyTypes", R"TS(export const foo = { bar: () => { } };)TS", R"TS(shared.foo.bar();)TS", "bar"}, testCase{"ObjectLiteralProperty", R"TS(export const foo = {  baz: "BAZ" };)TS", R"TS(shared.foo.baz;)TS", "baz"}, testCase{"MethodOfClassExpression", R"TS(export const foo = class { fly() {} };)TS", testutil::stringtestutil::Dedent(R"TS(
					const instance = new shared.foo();
					instance.fly();)TS"), "fly"}, testCase{"ArrowFunctionAsObjectLiteralProperty", testutil::stringtestutil::Dedent(R"TS(
					const local = { bar: () => { } };
					export const foo = local;)TS"), R"TS(shared.foo.bar();)TS", "bar"}}) {
				t->Run((std::string("TestFindAllRefsSpecialHandlingOfLocalness") + tc.name), [&](gostd::testing::T* t) {
	t->Parallel();
	// Go: tc.usage[:idx] + "/*ref*/" + tc.usage[idx:] — the transpiler
	// dropped the leading slice, producing usage+"/*ref*/"+usage[idx:]
	// (marker after the statement instead of inside the reference).
	auto usageWithMarker = ((tc.usage.substr(0, static_cast<int>((tc.usage).find(tc.referenceTerm))) + std::string("/*ref*/")) + tc.usage.substr(static_cast<int>((tc.usage).find(tc.referenceTerm))));
	auto content = (((((std::string(R"TS(
// @stateBaseline: true
// @Filename: /solution/tsconfig.json
{
	"files": [],
	"references": [
		{ "path": "./api" },
		{ "path": "./app" },
	],
}
// @Filename: /solution/api/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "dist",
		"rootDir": "src"
	},
	"include": ["src"],
	"references": [{ "path": "../shared" }],
}
// @Filename: /solution/api/src/server.ts
import * as shared from "../../shared/dist"
)TS") + usageWithMarker) + std::string(R"TS(
// @Filename: /solution/app/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "dist",
		"rootDir": "src"
	},
	"include": ["src"],
	"references": [{ "path": "../shared" }],
}
// @Filename: /solution/app/src/app.ts
import * as shared from "../../shared/dist"
)TS")) + tc.usage) + std::string(R"TS(
// @Filename: /solution/app/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
		"outDir": "dist",
		"rootDir": "src"
	},
	"include": ["src"],
	"references": [{ "path": "../shared" }],
}
// @Filename: /solution/shared/tsconfig.json
{
    "compilerOptions": {
        "composite": true,
        "outDir": "dist",
        "rootDir": "src"
    },
    "include": ["src"],
}
// @Filename: /solution/shared/src/index.ts
)TS")) + tc.definition);
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->VerifyBaselineFindAllReferences(t, {"ref"});
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsSpecialHandlingOfLocalness, TestFindAllRefsSpecialHandlingOfLocalness);

// statefindallrefs_test.go
static void TestFindAllRefsReExportInMultiProjectSolution(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true
// @Filename: /tsconfig.base.json
{
	"compilerOptions": {
		"rootDir": ".",
		"outDir": "target",
		"module": "ESNext",
		"moduleResolution": "bundler",
		"composite": true,
		"declaration": true,
		"strict": true
	},
	"include": []
}
// @Filename: /tsconfig.json
{
	"extends": "./tsconfig.base.json",
	"references": [
		{ "path": "project-a" },
		{ "path": "project-b" },
		{ "path": "project-c" },
	]
}
// @Filename: /project-a/tsconfig.json
{
	"extends": "../tsconfig.base.json",
	"include": ["*"]
}
// @Filename: /project-a/private.ts
export const /*symbolA*/symbolA = 'some-symbol';
console.log(symbolA);
// @Filename: /project-a/public.ts
export { symbolA } from './private';
// @Filename: /project-b/tsconfig.json
{
	"extends": "../tsconfig.base.json",
	"include": ["*"]
}
// @Filename: /project-b/public.ts
export const /*symbolB*/symbolB = 'symbol-b';
// @Filename: /project-c/tsconfig.json
{
	"extends": "../tsconfig.base.json",
	"include": ["*"],
	"references": [
		{ "path": "../project-a" },
		{ "path": "../project-b" },
	]
}
// @Filename: /project-c/index.ts
import { symbolB } from '../project-b/public';
import { /*symbolAUsage*/symbolA } from '../project-a/public';
console.log(symbolB);
console.log(symbolA);
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"symbolA"});
		f->VerifyBaselineFindAllReferences(t, {"symbolB"});
		f->VerifyBaselineFindAllReferences(t, {"symbolAUsage"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsReExportInMultiProjectSolution, TestFindAllRefsReExportInMultiProjectSolution);

// statefindallrefs_test.go
static void TestFindAllRefsDeclarationInOtherProject(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		struct testCase {
			bool projectAlreadyLoaded{};
			bool disableReferencedProjectLoad{};
			bool disableSourceOfProjectReferenceRedirect{};
			bool dtsMapPresent{};
		};
		{
			int _ = 0;
			for (auto&& tc : std::vector<testCase>{testCase{true, true, true, true}, testCase{true, true, true, false}, testCase{true, true, false, true}, testCase{true, true, false, false}, testCase{true, false, true, true}, testCase{true, false, true, false}, testCase{true, false, false, true}, testCase{true, false, false, false}, testCase{false, true, true, true}, testCase{false, true, true, false}, testCase{false, true, false, true}, testCase{false, true, false, false}, testCase{false, false, true, true}, testCase{false, false, true, false}, testCase{false, false, false, true}, testCase{false, false, false, false}}) {
				auto subScenario = ((((((gostd::sprintf(R"TS(Proj%sLoaded)TS", {IfElse(tc.projectAlreadyLoaded, "Is", "IsNot")}) + std::string(R"TS(RefdProjLoadingIs)TS")) + IfElse(tc.disableReferencedProjectLoad, "Disabled", "Enabled")) + std::string(R"TS(ProjRefRedirectsAre)TS")) + IfElse(tc.disableSourceOfProjectReferenceRedirect, "Disabled", "Enabled")) + std::string(R"TS(DeclMapIs)TS")) + IfElse(tc.dtsMapPresent, "Present", "Missing"));
				t->Run((std::string("TestFindAllRefsDeclarationInOtherProject") + subScenario), [&](gostd::testing::T* t) {
	t->Parallel();
	auto content = gostd::sprintf(R"TS(
// @stateBaseline: true
// @Filename: /myproject/a/tsconfig.json
{
	"disableReferencedProjectLoad": %t,
	"disableSourceOfProjectReferenceRedirect": %t,
	"composite": true
}
// @Filename: /myproject/a/index.ts
import { B } from "../b/lib";
const b: /*ref*/B = new B();
// @Filename: /myproject/b/tsconfig.json
{
	"declarationMap": true,
	"outDir": "lib",
	"composite": true,
}
// @Filename: /myproject/b/index.ts
export class B {
	M() {}
}
// @Filename: /myproject/b/helper.ts
/*bHelper*/import { B } from ".";
const b: B = new B();
// @Filename: /myproject/b/lib/index.d.ts
export declare class B {
	M(): void;
}
//# sourceMappingURL=index.d.ts.map)TS", {tc.disableReferencedProjectLoad, tc.disableSourceOfProjectReferenceRedirect});
	if (tc.dtsMapPresent) {
		content += R"TS(
// @Filename: /myproject/b/lib/index.d.ts.map
{
	"version": 3,
	"file": "index.d.ts",
	"sourceRoot": "",
	"sources": ["../index.ts"],
	"names": [],
	"mappings": "AAAA,qBAAa,CAAC;IACV,CAAC;CACJ"
})TS";
	}
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	if (tc.projectAlreadyLoaded) {
		f->GoToMarker(t, "ref");
		f->GoToMarker(t, "bHelper");
	}
	f->VerifyBaselineFindAllReferences(t, {"ref"});
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsDeclarationInOtherProject, TestFindAllRefsDeclarationInOtherProject);

// stateimplementations_test.go

// stateimplementations_test.go
static void TestImplementationsAcrossProjects(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true
// @Filename: /projects/temp/temp.ts
/*temp*/let x = 10
// @Filename: /projects/temp/tsconfig.json
{}
// @Filename: /projects/container/lib/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	references: [],
	files: [
		"index.ts",
		"bar.ts"
	],
}
// @Filename: /projects/container/lib/index.ts
export interface /*impl*/Foo {
    func();
}
export const val = 42;
// @Filename: /projects/container/lib/bar.ts
import {Foo} from './index'
class A implements Foo {
    func() {}
}
class B implements Foo {
    func() {}
}
// @Filename: /projects/container/exec/tsconfig.json
{
	"files": ["./index.ts"],
	"references": [
		{ "path": "../lib" },
	],
}
// @Filename: /projects/container/exec/index.ts
import { Foo } from "../lib";
class A1 implements Foo {
    func() {}
}
class B1 implements Foo {
    func() {}
}
// @Filename: /projects/container/compositeExec/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"files": ["./index.ts"],
	"references": [
		{ "path": "../lib" },
	],
}
// @Filename: /projects/container/compositeExec/index.ts
import { Foo } from "../lib";
class A2 implements Foo {
    func() {}
}
class B2 implements Foo {
    func() {}
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "impl");
		f->GoToMarker(t, "temp");
		f->VerifyBaselineGoToImplementation(t, {"impl"});
		f->CloseFileOfMarker(t, "temp");
		f->GoToMarker(t, "temp");
		f->CloseFileOfMarker(t, "impl");
		f->CloseFileOfMarker(t, "temp");
		f->GoToMarker(t, "temp");
	});
}
REGISTER_FOURSLASH_TEST(TestImplementationsAcrossProjects, TestImplementationsAcrossProjects);

// staterename_test.go

// staterename_test.go
static void TestRenameAncestorProjectRefMangement(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true
// @Filename: /projects/temp/temp.ts
/*temp*/let x = 10
// @Filename: /projects/temp/tsconfig.json
{}
// @Filename: /projects/container/lib/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	references: [],
	files: [
		"index.ts",
	],
}
// @Filename: /projects/container/lib/index.ts
export const myConst = 30;
// @Filename: /projects/container/exec/tsconfig.json
{
	"files": ["./index.ts"],
	"references": [
		{ "path": "../lib" },
	],
}
// @Filename: /projects/container/exec/index.ts
import { myConst } from "../lib";
export function getMyConst() {
	return myConst;
}
// @Filename: /projects/container/compositeExec/tsconfig.json
{
	"compilerOptions": {
		"composite": true,
	},
	"files": ["./index.ts"],
	"references": [
		{ "path": "../lib" },
	],
}
// @Filename: /projects/container/compositeExec/index.ts
import { /*find*/myConst } from "../lib";
export function getMyConst() {
	return myConst;
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
// @Filename: /projects/container/tsconfig.json
{
	"files": [],
	"include": [],
	"references": [
		{ "path": "./exec" },
		{ "path": "./compositeExec" },
	],
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "find");
		f->GoToMarker(t, "temp");
		f->VerifyBaselineRename(t, nullptr, {"find"});
		f->CloseFileOfMarker(t, "temp");
		f->GoToMarker(t, "temp");
		f->CloseFileOfMarker(t, "find");
		f->CloseFileOfMarker(t, "temp");
		f->GoToMarker(t, "temp");
	});
}
REGISTER_FOURSLASH_TEST(TestRenameAncestorProjectRefMangement, TestRenameAncestorProjectRefMangement);

// staterename_test.go
static void TestRenameInCommonFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = R"TS(
// @stateBaseline: true
// @Filename: /projects/a/a.ts
/*aTs*/import {C} from "./c/fc";
console.log(C)
// @Filename: /projects/a/tsconfig.json
{}
// @link:  /projects/c -> /projects/a/c
// @Filename: /projects/b/b.ts
/*bTs*/import {C} from "../c/fc";
console.log(C)
// @Filename: /projects/b/tsconfig.json
{}
// @link:  /projects/c -> /projects/b/c
// @Filename: /projects/c/fc.ts
export const /*find*/C = 42;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "aTs");
		f->GoToMarker(t, "bTs");
		auto findMarker = f->MarkerByName(t, "find");
		auto aFcMarker = findMarker->MakerWithSymlink("/projects/a/c/fc.ts");
		f->GoToMarkerOrRange(t, (aFcMarker).get());
		f->GoToMarkerOrRange(t, (findMarker->MakerWithSymlink("/projects/b/c/fc.ts")).get());
		f->VerifyBaselineRename(t, nullptr, {aFcMarker});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameInCommonFile, TestRenameInCommonFile);

} // namespace
