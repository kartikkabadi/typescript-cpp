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

// importNameCodeFixConvertTypeOnly1_test.go

// importNameCodeFixConvertTypeOnly1_test.go
static void TestImportNameCodeFixConvertTypeOnly1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export class A {}
export class B {}
// @Filename: /b.ts
import type { A } from './a';
new B)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { B, type A } from './a';
new B)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixConvertTypeOnly1, TestImportNameCodeFixConvertTypeOnly1);

// importNameCodeFixDefaultExport1_test.go

// importNameCodeFixDefaultExport1_test.go
static void TestImportNameCodeFixDefaultExport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /foo-bar.ts
export default function fooBar();
// @Filename: /b.ts
[|import * as fb from "./foo-bar";
foo/**/Bar|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import fooBar, * as fb from "./foo-bar";
fooBar)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixDefaultExport1, TestImportNameCodeFixDefaultExport1);

// importNameCodeFixDefaultExport2_test.go

// importNameCodeFixDefaultExport2_test.go
static void TestImportNameCodeFixDefaultExport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /lib.ts
class Base { }
export default Base;
// @Filename: /test.ts
[|class Derived extends Base { }|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/test.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import Base from "./lib";

class Derived extends Base { })TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixDefaultExport2, TestImportNameCodeFixDefaultExport2);

// importNameCodeFixDefaultExport3_test.go

// importNameCodeFixDefaultExport3_test.go
static void TestImportNameCodeFixDefaultExport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /foo-bar/index.ts
export default 0;
// @Filename: /b.ts
[|foo/**/Bar|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import fooBar from "./foo-bar";

fooBar)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixDefaultExport3, TestImportNameCodeFixDefaultExport3);

// importNameCodeFixDefaultExport4_test.go

// importNameCodeFixDefaultExport4_test.go
static void TestImportNameCodeFixDefaultExport4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /foo.ts
const a = () => {};
export default a;
// @Filename: /test.ts
[|foo|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/test.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import foo from "./foo";

foo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixDefaultExport4, TestImportNameCodeFixDefaultExport4);

// importNameCodeFixDefaultExport5_test.go

// importNameCodeFixDefaultExport5_test.go
static void TestImportNameCodeFixDefaultExport5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /node_modules/hooks/useFoo.ts
declare const _default: () => void;
export default _default;
// @Filename: /test.ts
[|useFoo|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.AutoImportEntrypointDirectorySearch = Tristate::True});
		f->GoToFile(t, "/test.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import useFoo from "hooks/useFoo";

useFoo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixDefaultExport5, TestImportNameCodeFixDefaultExport5);

// importNameCodeFixDefaultExport6_test.go

// importNameCodeFixDefaultExport6_test.go
static void TestImportNameCodeFixDefaultExport6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export default Math.foo;
// @Filename: /index.ts
a/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "a", .Source = "./a", .Description = R"TS(Add import from "./a")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import a from "./a";

a)TS"), .UserPreferences = nullptr}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixDefaultExport6, TestImportNameCodeFixDefaultExport6);

// importNameCodeFixDefaultExport7_test.go

// importNameCodeFixDefaultExport7_test.go
static void TestImportNameCodeFixDefaultExport7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: dom
// @Filename: foo.ts
export default globalThis.localStorage;
// @Filename: index.ts
foo/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixDefaultExport7, TestImportNameCodeFixDefaultExport7);

// importNameCodeFixDefaultExport_test.go

// importNameCodeFixDefaultExport_test.go
static void TestImportNameCodeFixDefaultExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /foo-bar.ts
export default 0;
// @Filename: /b.ts
[|foo/**/Bar|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import fooBar from "./foo-bar";

fooBar)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixDefaultExport, TestImportNameCodeFixDefaultExport);

// importNameCodeFixExistingImport0_test.go

// importNameCodeFixExistingImport0_test.go
static void TestImportNameCodeFixExistingImport0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import [|{ v1 }|] from "./module";
f1/*0*/();
// @Filename: module.ts
export function f1() {}
export var v1 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS({ f1, v1 })TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport0, TestImportNameCodeFixExistingImport0);

// importNameCodeFixExistingImport10_test.go

// importNameCodeFixExistingImport10_test.go
static void TestImportNameCodeFixExistingImport10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import [|{
    v1,
    v2
}|] from "./module";
f1/*0*/();
// @Filename: module.ts
export function f1() {}
export var v1 = 5;
export var v2 = 5;
export var v3 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS({
    f1,
    v1,
    v2
})TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport10, TestImportNameCodeFixExistingImport10);

// importNameCodeFixExistingImport11_test.go

// importNameCodeFixExistingImport11_test.go
static void TestImportNameCodeFixExistingImport11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import [|{
    v1, v2,
    v3
}|] from "./module";
f1/*0*/();
// @Filename: module.ts
 export function f1() {}
 export var v1 = 5;
 export var v2 = 5;
 export var v3 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS({
    f1,
    v1, v2,
    v3
})TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport11, TestImportNameCodeFixExistingImport11);

// importNameCodeFixExistingImport12_test.go

// importNameCodeFixExistingImport12_test.go
static void TestImportNameCodeFixExistingImport12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import [|{}|] from "./module";
f1/*0*/();
// @Filename: module.ts
export function f1() {}
export var v1 = 5;
export var v2 = 5;
export var v3 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS({ f1 })TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport12, TestImportNameCodeFixExistingImport12);

// importNameCodeFixExistingImport1_test.go

// importNameCodeFixExistingImport1_test.go
static void TestImportNameCodeFixExistingImport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import d, [|{ v1 }|] from "./module";
f1/*0*/();
// @Filename: module.ts
export function f1() {}
export var v1 = 5;
export default var d1 = 6;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS({ f1, v1 })TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport1, TestImportNameCodeFixExistingImport1);

// importNameCodeFixExistingImport2_test.go

// importNameCodeFixExistingImport2_test.go
static void TestImportNameCodeFixExistingImport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import * as ns from "./module";
// Comment
f1/*0*/();
// @Filename: module.ts
 export function f1() {}
 export var v1 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import * as ns from "./module";
// Comment
ns.f1();)TS", R"TS(import * as ns from "./module";
import { f1 } from "./module";
// Comment
f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport2, TestImportNameCodeFixExistingImport2);

// importNameCodeFixExistingImport3_test.go

// importNameCodeFixExistingImport3_test.go
static void TestImportNameCodeFixExistingImport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|import d, * as ns from "./module"   ;
f1/*0*/();|]
// @Filename: module.ts
export function f1() {}
export var v1 = 5;
export default var d1 = 6;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import d, * as ns from "./module"   ;
ns.f1();)TS", R"TS(import d, * as ns from "./module"   ;
import { f1 } from "./module";
f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport3, TestImportNameCodeFixExistingImport3);

// importNameCodeFixExistingImport4_test.go

// importNameCodeFixExistingImport4_test.go
static void TestImportNameCodeFixExistingImport4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|import d from "./module";
f1/*0*/();|]
// @Filename: module.ts
export function f1() {}
export var v1 = 5;
export default var d1 = 6;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import d, { f1 } from "./module";
f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport4, TestImportNameCodeFixExistingImport4);

// importNameCodeFixExistingImport5_test.go

// importNameCodeFixExistingImport5_test.go
static void TestImportNameCodeFixExistingImport5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|import "./module";
f1/*0*/();|]
// @Filename: module.ts
export function f1() {}
export var v1 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import "./module";
import { f1 } from "./module";
f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport5, TestImportNameCodeFixExistingImport5);

// importNameCodeFixExistingImport6_test.go

// importNameCodeFixExistingImport6_test.go
static void TestImportNameCodeFixExistingImport6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import [|{ v1 }|] from "fake-module";
f1/*0*/();
// @Filename: ../package.json
{ "dependencies": { "fake-module": "latest" } }
// @Filename: ../node_modules/fake-module/index.ts
export var v1 = 5;
export function f1();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS({ f1, v1 })TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport6, TestImportNameCodeFixExistingImport6);

// importNameCodeFixExistingImport7_test.go

// importNameCodeFixExistingImport7_test.go
static void TestImportNameCodeFixExistingImport7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import [|{ v1 }|] from "../other_dir/module";
f1/*0*/();
// @Filename: ../other_dir/module.ts
export var v1 = 5;
export function f1();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS({ f1, v1 })TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport7, TestImportNameCodeFixExistingImport7);

// importNameCodeFixExistingImport8_test.go

// importNameCodeFixExistingImport8_test.go
static void TestImportNameCodeFixExistingImport8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import [|{v1, v2, v3,}|] from "./module";
v4/*0*/();
// @Filename: module.ts
export function v4() {}
export var v1 = 5;
export var v2 = 5;
export var v3 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS({v1, v2, v3, v4,})TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport8, TestImportNameCodeFixExistingImport8);

// importNameCodeFixExistingImport9_test.go

// importNameCodeFixExistingImport9_test.go
static void TestImportNameCodeFixExistingImport9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import [|{
    v1
}|] from "./module";
f1/*0*/();
// @Filename: module.ts
export function f1() {}
export var v1 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS({
    f1,
    v1
})TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImport9, TestImportNameCodeFixExistingImport9);

// importNameCodeFixExistingImportEquals0_test.go

// importNameCodeFixExistingImportEquals0_test.go
static void TestImportNameCodeFixExistingImportEquals0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|import ns = require("ambient-module");
var x = v1/*0*/ + 5;|]
// @Filename: ambientModule.ts
declare module "ambient-module" {
   export function f1();
   export var v1;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import ns = require("ambient-module");
var x = ns.v1 + 5;)TS", R"TS(import { v1 } from "ambient-module";
import ns = require("ambient-module");
var x = v1 + 5;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExistingImportEquals0, TestImportNameCodeFixExistingImportEquals0);

// importNameCodeFixExportAsDefaultExistingImport_test.go

// importNameCodeFixExportAsDefaultExistingImport_test.go
static void TestImportNameCodeFixExportAsDefaultExistingImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import [|{ v1, v2, v3 }|] from "./module";
v4/*0*/();
// @Filename: module.ts
const v4 = 5;
export { v4 as default };
export const v1 = 5;
export const v2 = 5;
export const v3 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(v4, { v1, v2, v3 })TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExportAsDefaultExistingImport, TestImportNameCodeFixExportAsDefaultExistingImport);

// importNameCodeFixExportAsDefault_test.go

// importNameCodeFixExportAsDefault_test.go
static void TestImportNameCodeFixExportAsDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /foo.ts
const foo = 'foo'
export { foo as default }
// @Filename: /index.ts
 foo/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "foo", .Source = "./foo", .Description = R"TS(Add import from "./foo")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import foo from "./foo";

foo)TS"), .UserPreferences = nullptr}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixExportAsDefault, TestImportNameCodeFixExportAsDefault);

// importNameCodeFixIndentedIdentifier_test.go

// importNameCodeFixIndentedIdentifier_test.go
static void TestImportNameCodeFixIndentedIdentifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
[|import * as b from "./b";
{
    x/**/
}|]
// @Filename: /b.ts
export const x = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import * as b from "./b";
{
    b.x
})TS", R"TS(import * as b from "./b";
import { x } from "./b";
{
    x
})TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixIndentedIdentifier, TestImportNameCodeFixIndentedIdentifier);

// importNameCodeFixInferEndingPreference_classic_test.go

// importNameCodeFixInferEndingPreference_classic_test.go
static void TestImportNameCodeFixInferEndingPreference_classic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @checkJs: true
// @allowJs: true
// @noEmit: true
// @Filename: /a.js
export const a = 0;
// @Filename: /b.js
export const b = 0;
// @Filename: /c.js
import { a } from "./a.js";

b/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./b.js"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixInferEndingPreference_classic, TestImportNameCodeFixInferEndingPreference_classic);

// importNameCodeFixInferEndingPreference_test.go

// importNameCodeFixInferEndingPreference_test.go
static void TestImportNameCodeFixInferEndingPreference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @moduleResolution: bundler
// @Filename: /a.mts
export {};
// @Filename: /b.ts
export {};
// @Filename: /c.ts
export const c = 0;
// @Filename: /main.ts
import {} from "./a.mjs";
import {} from "./b";

c/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./c"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixInferEndingPreference, TestImportNameCodeFixInferEndingPreference);

// importNameCodeFixJsEnding_test.go

// importNameCodeFixJsEnding_test.go
static void TestImportNameCodeFixJsEnding(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @module: commonjs
// @Filename: /node_modules/lit/package.json
{ "name": "lit", "version": "1.0.0" }
// @Filename: /node_modules/lit/index.d.ts
import "./decorators";
// @Filename: /node_modules/lit/decorators.d.ts
export declare function customElement(name: string): any;
// @Filename: /a.ts
customElement/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"lit/decorators.js"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierEnding = "js", .AutoImportEntrypointDirectorySearch = Tristate::True}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixJsEnding, TestImportNameCodeFixJsEnding);

// importNameCodeFixNewImportAllowSyntheticDefaultImports0_test.go

// importNameCodeFixNewImportAllowSyntheticDefaultImports0_test.go
static void TestImportNameCodeFixNewImportAllowSyntheticDefaultImports0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @AllowSyntheticDefaultImports: true
// @Filename: a/f1.ts
[|export var x = 0;
bar/*0*/();|]
// @Filename: a/foo.d.ts
declare function bar(): number;
export = bar;
export as namespace bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import bar from "./foo";

export var x = 0;
bar();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportAllowSyntheticDefaultImports0, TestImportNameCodeFixNewImportAllowSyntheticDefaultImports0);

// importNameCodeFixNewImportAllowSyntheticDefaultImports1_test.go

// importNameCodeFixNewImportAllowSyntheticDefaultImports1_test.go
static void TestImportNameCodeFixNewImportAllowSyntheticDefaultImports1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Module: system
// @Filename: a/f1.ts
[|export var x = 0;
bar/*0*/();|]
// @Filename: a/foo.d.ts
declare function bar(): number;
export = bar;
export as namespace bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import bar from "./foo";

export var x = 0;
bar();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportAllowSyntheticDefaultImports1, TestImportNameCodeFixNewImportAllowSyntheticDefaultImports1);

// importNameCodeFixNewImportAllowSyntheticDefaultImports2_test.go

// importNameCodeFixNewImportAllowSyntheticDefaultImports2_test.go
static void TestImportNameCodeFixNewImportAllowSyntheticDefaultImports2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @AllowSyntheticDefaultImports: false
// @Module: system
// @Filename: a/f1.ts
[|export var x = 0;
bar/*0*/();|]
// @Filename: a/foo.d.ts
declare function bar(): number;
export = bar;
export as namespace bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import * as bar from "./foo";

export var x = 0;
bar();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportAllowSyntheticDefaultImports2, TestImportNameCodeFixNewImportAllowSyntheticDefaultImports2);

// importNameCodeFixNewImportAllowSyntheticDefaultImports3_test.go

// importNameCodeFixNewImportAllowSyntheticDefaultImports3_test.go
static void TestImportNameCodeFixNewImportAllowSyntheticDefaultImports3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @AllowSyntheticDefaultImports: false
// @Module: commonjs
// @Filename: a/f1.ts
[|export var x = 0;
bar/*0*/();|]
// @Filename: a/foo.d.ts
declare function bar(): number;
export = bar;
export as namespace bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import bar = require("./foo");

export var x = 0;
bar();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportAllowSyntheticDefaultImports3, TestImportNameCodeFixNewImportAllowSyntheticDefaultImports3);

// importNameCodeFixNewImportAllowSyntheticDefaultImports5_test.go

// importNameCodeFixNewImportAllowSyntheticDefaultImports5_test.go
static void TestImportNameCodeFixNewImportAllowSyntheticDefaultImports5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @AllowSyntheticDefaultImports: false
// @Module: umd
// @Filename: a/f1.ts
[|export var x = 0;
bar/*0*/();|]
// @Filename: a/foo.d.ts
declare function bar(): number;
export = bar;
export as namespace bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import bar = require("./foo");

export var x = 0;
bar();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportAllowSyntheticDefaultImports5, TestImportNameCodeFixNewImportAllowSyntheticDefaultImports5);

// importNameCodeFixNewImportAmbient0_test.go

// importNameCodeFixNewImportAmbient0_test.go
static void TestImportNameCodeFixNewImportAmbient0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/();|]
// @Filename: ambientModule.ts
declare module "ambient-module" {
   export function f1();
   export var v1;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "ambient-module";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportAmbient0, TestImportNameCodeFixNewImportAmbient0);

// importNameCodeFixNewImportAmbient1_test.go

// importNameCodeFixNewImportAmbient1_test.go
static void TestImportNameCodeFixNewImportAmbient1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import d from "other-ambient-module";
import * as ns from "yet-another-ambient-module";
var x = v1/*0*/ + 5;
// @Filename: ambientModule.ts
declare module "ambient-module" {
   export function f1();
   export var v1;
}
// @Filename: otherAmbientModule.ts
declare module "other-ambient-module" {
   export default function f2();
}
// @Filename: yetAnotherAmbientModule.ts
declare module "yet-another-ambient-module" {
   export function f3();
   export var v3;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { v1 } from "ambient-module";
import d from "other-ambient-module";
import * as ns from "yet-another-ambient-module";
var x = v1 + 5;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportAmbient1, TestImportNameCodeFixNewImportAmbient1);

// importNameCodeFixNewImportAmbient2_test.go

// importNameCodeFixNewImportAmbient2_test.go
static void TestImportNameCodeFixNewImportAmbient2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|/*!
 * I'm a license or something
 */
f1/*0*/();|]
// @Filename: ambientModule.ts
 declare module "ambient-module" {
    export function f1();
    export var v1;
 })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(/*!
 * I'm a license or something
 */

import { f1 } from "ambient-module";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportAmbient2, TestImportNameCodeFixNewImportAmbient2);

// importNameCodeFixNewImportAmbient3_test.go

// importNameCodeFixNewImportAmbient3_test.go
static void TestImportNameCodeFixNewImportAmbient3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let a = "I am a non-trivial statement that appears before imports";
import d from "other-ambient-module"
import * as ns from "yet-another-ambient-module"
var x = v1/*0*/ + 5;
// @Filename: ambientModule.ts
declare module "ambient-module" {
   export function f1();
   export var v1;
}
// @Filename: otherAmbientModule.ts
declare module "other-ambient-module" {
   export default function f2();
}
// @Filename: yetAnotherAmbientModule.ts
declare module "yet-another-ambient-module" {
   export function f3();
   export var v3;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(let a = "I am a non-trivial statement that appears before imports";
import { v1 } from "ambient-module";
import d from "other-ambient-module"
import * as ns from "yet-another-ambient-module"
var x = v1 + 5;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportAmbient3, TestImportNameCodeFixNewImportAmbient3);

// importNameCodeFixNewImportBaseUrl0_test.go

// importNameCodeFixNewImportBaseUrl0_test.go
static void TestImportNameCodeFixNewImportBaseUrl0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/();|]
// @Filename: tsconfig.json
{
    "compilerOptions": {
        "baseUrl": "./a"
    }
}
// @Filename: a/b.ts
export function f1() { };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "b";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportBaseUrl0, TestImportNameCodeFixNewImportBaseUrl0);

// importNameCodeFixNewImportBaseUrl1_test.go

// importNameCodeFixNewImportBaseUrl1_test.go
static void TestImportNameCodeFixNewImportBaseUrl1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
    "compilerOptions": {
        "baseUrl": "./a"
    }
}
// @Filename: /a/b/x.ts
export function f1() { };
// @Filename: /a/b/y.ts
[|f1/*0*/();|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a/b/y.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "./x";

f1();)TS"}, nullptr);
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "b/x";

f1();)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "non-relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportBaseUrl1, TestImportNameCodeFixNewImportBaseUrl1);

// importNameCodeFixNewImportBaseUrl2_test.go

// importNameCodeFixNewImportBaseUrl2_test.go
static void TestImportNameCodeFixNewImportBaseUrl2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
    "compilerOptions": {
        "baseUrl": "./a"
    }
}
// @Filename: /a/b/x.ts
export function f1() { };
// @Filename: /a/c/y.ts
[|f1/*0*/();|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a/c/y.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "b/x";

f1();)TS"}, nullptr);
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "../b/x";

f1();)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportBaseUrl2, TestImportNameCodeFixNewImportBaseUrl2);

// importNameCodeFixNewImportDefault0_test.go

// importNameCodeFixNewImportDefault0_test.go
static void TestImportNameCodeFixNewImportDefault0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/();|]
// @Filename: module.ts
export default function f1() { };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import f1 from "./module";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportDefault0, TestImportNameCodeFixNewImportDefault0);

// importNameCodeFixNewImportExportEqualsCommonJSInteropOn_test.go

// importNameCodeFixNewImportExportEqualsCommonJSInteropOn_test.go
static void TestImportNameCodeFixNewImportExportEqualsCommonJSInteropOn(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Module: commonjs
// @EsModuleInterop: true
// @Filename: /foo.d.ts
declare module "bar" {
  const bar: number;
  export = bar;
}
declare module "foo" {
  const foo: number;
  export = foo;
}
declare module "es" {
  const es = 0;
  export default es;
}
// @Filename: /a.ts
import bar = require("bar");

foo
// @Filename: /b.ts
foo
// @Filename: /c.ts
import es from "es";
import bar = require("bar");

foo)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import bar = require("bar");
import foo = require("foo");

foo)TS"}, nullptr);
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import foo from "foo";

foo)TS"}, nullptr);
		f->GoToFile(t, "/c.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import es from "es";
import bar = require("bar");
import foo = require("foo");

foo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportExportEqualsCommonJSInteropOn, TestImportNameCodeFixNewImportExportEqualsCommonJSInteropOn);

// importNameCodeFixNewImportExportEqualsESNextInteropOff_test.go

// importNameCodeFixNewImportExportEqualsESNextInteropOff_test.go
static void TestImportNameCodeFixNewImportExportEqualsESNextInteropOff(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Module: esnext
// @Filename: /foo.d.ts
declare module "foo" {
  const foo: number;
  export = foo;
}
// @Filename: /index.ts
foo)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/index.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import foo from "foo";

foo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportExportEqualsESNextInteropOff, TestImportNameCodeFixNewImportExportEqualsESNextInteropOff);

// importNameCodeFixNewImportExportEqualsESNextInteropOn_test.go

// importNameCodeFixNewImportExportEqualsESNextInteropOn_test.go
static void TestImportNameCodeFixNewImportExportEqualsESNextInteropOn(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @EsModuleInterop: true
// @Module: es2015
// @Filename: /foo.d.ts
declare module "foo" {
  const foo: number;
  export = foo;
}
// @Filename: /index.ts
[|foo|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/index.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import foo from "foo";

foo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportExportEqualsESNextInteropOn, TestImportNameCodeFixNewImportExportEqualsESNextInteropOn);

// importNameCodeFixNewImportFile0_test.go

// importNameCodeFixNewImportFile0_test.go
static void TestImportNameCodeFixNewImportFile0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/();|]
// @Filename: jalapeño.ts
export function f1() {}
export var v1 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "./jalapeño";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFile0, TestImportNameCodeFixNewImportFile0);

// importNameCodeFixNewImportFile1_test.go

// importNameCodeFixNewImportFile1_test.go
static void TestImportNameCodeFixNewImportFile1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|/// <reference path="./tripleSlashReference.ts" />
f1/*0*/();|]
// @Filename: Module.ts
export function f1() {}
export var v1 = 5;
// @Filename: tripleSlashReference.ts
var x = 5;/*dummy*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(/// <reference path="./tripleSlashReference.ts" />

import { f1 } from "./Module";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFile1, TestImportNameCodeFixNewImportFile1);

// importNameCodeFixNewImportFile2_test.go

// importNameCodeFixNewImportFile2_test.go
static void TestImportNameCodeFixNewImportFile2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/();|]
// @Filename: ../../other_dir/module.ts
export var v1 = 5;
export function f1();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "../../other_dir/module";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFile2, TestImportNameCodeFixNewImportFile2);

// importNameCodeFixNewImportFile3_test.go

// importNameCodeFixNewImportFile3_test.go
static void TestImportNameCodeFixNewImportFile3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|let t: XXX/*0*/.I;|]
// @Filename: ./module.ts
export namespace XXX {
   export interface I {
   }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { XXX } from "./module";

let t: XXX.I;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFile3, TestImportNameCodeFixNewImportFile3);

// importNameCodeFixNewImportFile4_test.go

// importNameCodeFixNewImportFile4_test.go
static void TestImportNameCodeFixNewImportFile4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|let t: A/*0*/.B.I;|]
// @Filename: ./module.ts
export namespace A {
   export namespace B {
       export interface I { }
   }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { A } from "./module";

let t: A.B.I;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFile4, TestImportNameCodeFixNewImportFile4);

// importNameCodeFixNewImportFileAllComments_test.go

// importNameCodeFixNewImportFileAllComments_test.go
static void TestImportNameCodeFixNewImportFileAllComments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|/*!
 * This is a license or something
 */
/// <reference types="node" />
/// <reference path="./a.ts" />
/// <amd-dependency path="./b.ts" />
/**
 * This is a comment intended to be attached to this interface
 */
export interface SomeInterface {
}
f1/*0*/();|]
// @Filename: module.ts
export function f1() {}
export var v1 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(/*!
 * This is a license or something
 */
/// <reference types="node" />
/// <reference path="./a.ts" />
/// <amd-dependency path="./b.ts" />

import { f1 } from "./module";

/**
 * This is a comment intended to be attached to this interface
 */
export interface SomeInterface {
}
f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFileAllComments, TestImportNameCodeFixNewImportFileAllComments);

// importNameCodeFixNewImportFileDetachedComments_test.go

// importNameCodeFixNewImportFileDetachedComments_test.go
static void TestImportNameCodeFixNewImportFileDetachedComments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|/**
 * This is a comment intended to be attached to this interface
 */
export interface SomeInterface {
}
f1/*0*/();|]
// @Filename: module.ts
export function f1() {}
export var v1 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "./module";

/**
 * This is a comment intended to be attached to this interface
 */
export interface SomeInterface {
}
f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFileDetachedComments, TestImportNameCodeFixNewImportFileDetachedComments);

// importNameCodeFixNewImportFileQuoteStyle0_test.go

// importNameCodeFixNewImportFileQuoteStyle0_test.go
static void TestImportNameCodeFixNewImportFileQuoteStyle0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|import { v2 } from './module2';

f1/*0*/();|]
// @Filename: module1.ts
export function f1() {}
// @Filename: module2.ts
export var v2 = 6;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from './module1';
import { v2 } from './module2';

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFileQuoteStyle0, TestImportNameCodeFixNewImportFileQuoteStyle0);

// importNameCodeFixNewImportFileQuoteStyle1_test.go

// importNameCodeFixNewImportFileQuoteStyle1_test.go
static void TestImportNameCodeFixNewImportFileQuoteStyle1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|import { v2 } from "./module2";

f1/*0*/();|]
// @Filename: module1.ts
export function f1() {}
// @Filename: module2.ts
export var v2 = 6;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "./module1";
import { v2 } from "./module2";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFileQuoteStyle1, TestImportNameCodeFixNewImportFileQuoteStyle1);

// importNameCodeFixNewImportFileQuoteStyle2_test.go

// importNameCodeFixNewImportFileQuoteStyle2_test.go
static void TestImportNameCodeFixNewImportFileQuoteStyle2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|import m2 = require('./module2');

f1/*0*/();|]
// @Filename: module1.ts
export function f1() {}
// @Filename: module2.ts
export var v2 = 6;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from './module1';
import m2 = require('./module2');

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFileQuoteStyle2, TestImportNameCodeFixNewImportFileQuoteStyle2);

// importNameCodeFixNewImportFileQuoteStyle3_test.go

// importNameCodeFixNewImportFileQuoteStyle3_test.go
static void TestImportNameCodeFixNewImportFileQuoteStyle3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|export { v2 } from './module2';

f1/*0*/();|]
// @Filename: module1.ts
export function f1() {}
// @Filename: module2.ts
export var v2 = 6;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from './module1';

export { v2 } from './module2';

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFileQuoteStyle3, TestImportNameCodeFixNewImportFileQuoteStyle3);

// importNameCodeFixNewImportFileQuoteStyleMixed0_test.go

// importNameCodeFixNewImportFileQuoteStyleMixed0_test.go
static void TestImportNameCodeFixNewImportFileQuoteStyleMixed0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|import { v2 } from "./module2";
import { v3 } from './module3';

f1/*0*/();|]
// @Filename: module1.ts
export function f1() {}
// @Filename: module2.ts
export var v2 = 6;
// @Filename: module3.ts
export var v3 = 6;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "./module1";
import { v2 } from "./module2";
import { v3 } from './module3';

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFileQuoteStyleMixed0, TestImportNameCodeFixNewImportFileQuoteStyleMixed0);

// importNameCodeFixNewImportFileQuoteStyleMixed1_test.go

// importNameCodeFixNewImportFileQuoteStyleMixed1_test.go
static void TestImportNameCodeFixNewImportFileQuoteStyleMixed1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|import { v2 } from './module2';
import { v3 } from "./module3";

f1/*0*/();|]
// @Filename: module1.ts
export function f1() {}
// @Filename: module2.ts
export var v2 = 6;
// @Filename: module3.ts
export var v3 = 6;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from './module1';
import { v2 } from './module2';
import { v3 } from "./module3";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFileQuoteStyleMixed1, TestImportNameCodeFixNewImportFileQuoteStyleMixed1);

// importNameCodeFixNewImportFromAtTypesScopedPackage_test.go

// importNameCodeFixNewImportFromAtTypesScopedPackage_test.go
static void TestImportNameCodeFixNewImportFromAtTypesScopedPackage(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/();|]
// @Filename: node_modules/@types/myLib__scoped/index.d.ts
export function f1() {}
export var v1 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "@myLib/scoped";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFromAtTypesScopedPackage, TestImportNameCodeFixNewImportFromAtTypesScopedPackage);

// importNameCodeFixNewImportFromAtTypes_test.go

// importNameCodeFixNewImportFromAtTypes_test.go
static void TestImportNameCodeFixNewImportFromAtTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/();|]
// @Filename: node_modules/@types/myLib/index.d.ts
export function f1() {}
export var v1 = 5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "myLib";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportFromAtTypes, TestImportNameCodeFixNewImportFromAtTypes);

// importNameCodeFixNewImportIndex_notForClassicResolution_test.go

// importNameCodeFixNewImportIndex_notForClassicResolution_test.go
static void TestImportNameCodeFixNewImportIndex_notForClassicResolution(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: classic
// @Filename: /a/index.ts
export const foo = 0;
// @Filename: /node_modules/x/index.d.ts
export const bar = 0;
// @Filename: /b.ts
[|foo;|]
// @Filename: /c.ts
[|bar;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a/index.ts");
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "./a/index";

foo;)TS"}, nullptr);
		f->GoToFile(t, "/c.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { bar } from "./node_modules/x/index";

bar;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportIndex_notForClassicResolution, TestImportNameCodeFixNewImportIndex_notForClassicResolution);

// importNameCodeFixNewImportIndex_test.go

// importNameCodeFixNewImportIndex_test.go
static void TestImportNameCodeFixNewImportIndex(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a/index.ts
export const foo = 0;
// @Filename: /b.ts
[|/**/foo;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a/index.ts");
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "./a";

foo;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportIndex, TestImportNameCodeFixNewImportIndex);

// importNameCodeFixNewImportNodeModules0_test.go

// importNameCodeFixNewImportNodeModules0_test.go
static void TestImportNameCodeFixNewImportNodeModules0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/();|]
// @Filename: ../package.json
{ "dependencies": { "fake-module": "latest" } }
// @Filename: ../node_modules/fake-module/index.ts
export var v1 = 5;
export function f1();
// @Filename: ../node_modules/fake-module/package.json
{})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "fake-module";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportNodeModules0, TestImportNameCodeFixNewImportNodeModules0);

// importNameCodeFixNewImportNodeModules1_test.go

// importNameCodeFixNewImportNodeModules1_test.go
static void TestImportNameCodeFixNewImportNodeModules1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/();|]
// @Filename: ../package.json
{ "dependencies": { "fake-module": "latest" } }
// @Filename: ../node_modules/fake-module/nested.ts
export var v1 = 5;
export function f1();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->Configure(t, lsutil::UserPreferences{.AutoImportEntrypointDirectorySearch = Tristate::True});
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "fake-module/nested";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportNodeModules1, TestImportNameCodeFixNewImportNodeModules1);

// importNameCodeFixNewImportNodeModules2_test.go

// importNameCodeFixNewImportNodeModules2_test.go
static void TestImportNameCodeFixNewImportNodeModules2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/();|]
// @Filename: ../package.json
{ "dependencies": { "fake-module": "latest" } }
// @Filename: ../node_modules/fake-module/notindex.d.ts
export var v1 = 5;
export function f1();
// @Filename: ../node_modules/fake-module/notindex.js
module.exports = {
   v1: 5,
   f1: function () {}
};
// @Filename: ../node_modules/fake-module/package.json
{ "main":"./notindex.js", "typings":"./notindex.d.ts" })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "fake-module";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportNodeModules2, TestImportNameCodeFixNewImportNodeModules2);

// importNameCodeFixNewImportNodeModules3_test.go

// importNameCodeFixNewImportNodeModules3_test.go
static void TestImportNameCodeFixNewImportNodeModules3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
[|f1/*0*/();|]
// @Filename: /node_modules/@types/random/index.d.ts
export var v1 = 5;
export function f1();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "random";

f1();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportNodeModules3, TestImportNameCodeFixNewImportNodeModules3);

// importNameCodeFixNewImportNodeModules4_test.go

// importNameCodeFixNewImportNodeModules4_test.go
static void TestImportNameCodeFixNewImportNodeModules4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/('');|]
// @Filename: package.json
{ "dependencies": { "package-name": "latest" } }
// @Filename: node_modules/package-name/bin/lib/libfile.d.ts
export function f1(text: string): string;
// @Filename: node_modules/package-name/bin/lib/libfile.js
function f1(text) { }
exports.f1 = f1;
// @Filename: node_modules/package-name/package.json
{
  "main": "bin/lib/libfile.js",
  "types": "bin/lib/libfile.d.ts"
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "package-name";

f1('');)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportNodeModules4, TestImportNameCodeFixNewImportNodeModules4);

// importNameCodeFixNewImportNodeModules6_test.go

// importNameCodeFixNewImportNodeModules6_test.go
static void TestImportNameCodeFixNewImportNodeModules6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/('');|]
// @Filename: package.json
{ "dependencies": { "package-name": "latest" } }
// @Filename: node_modules/package-name/bin/lib/index.d.ts
export function f1(text: string): string;
// @Filename: node_modules/package-name/bin/lib/index.js
function f1(text) { }
exports.f1 = f1;
// @Filename: node_modules/package-name/package.json
{
  "main": "bin/lib/index.js",
  "types": "bin/lib/index.d.ts"
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "package-name";

f1('');)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportNodeModules6, TestImportNameCodeFixNewImportNodeModules6);

// importNameCodeFixNewImportNodeModules7_test.go

// importNameCodeFixNewImportNodeModules7_test.go
static void TestImportNameCodeFixNewImportNodeModules7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/('');|]
// @Filename: package.json
{ "dependencies": { "package-name": "0.0.1" } }
// @Filename: node_modules/package-name/bin/lib/libfile.d.ts
export declare function f1(text: string): string;
// @Filename: node_modules/package-name/bin/lib/libfile.js
function f1(text) {}
exports.f1 = f1;
// @Filename: node_modules/package-name/package.json
{ "main": "bin/lib/libfile.js" })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "package-name";

f1('');)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportNodeModules7, TestImportNameCodeFixNewImportNodeModules7);

// importNameCodeFixNewImportNodeModules8_test.go

// importNameCodeFixNewImportNodeModules8_test.go
static void TestImportNameCodeFixNewImportNodeModules8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|f1/*0*/('');|]
// @Filename: package.json
{ "dependencies": { "@scope/package-name": "latest" } }
// @Filename: node_modules/@scope/package-name/bin/lib/index.d.ts
export function f1(text: string): string;
// @Filename: node_modules/@scope/package-name/bin/lib/index.js
function f1(text) { }
exports.f1 = f1;
// @Filename: node_modules/@scope/package-name/package.json
{
  "main": "bin/lib/index.js",
  "types": "bin/lib/index.d.ts"
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { f1 } from "@scope/package-name";

f1('');)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportNodeModules8, TestImportNameCodeFixNewImportNodeModules8);

// importNameCodeFixNewImportPaths0_test.go

// importNameCodeFixNewImportPaths0_test.go
static void TestImportNameCodeFixNewImportPaths0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|foo/*0*/();|]
// @Filename: folder_a/f2.ts
export function foo() {};
// @Filename: tsconfig.json
{
    "compilerOptions": {
        "baseUrl": ".",
        "paths": {
            "a": [ "folder_a/f2" ]
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "a";

foo();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportPaths0, TestImportNameCodeFixNewImportPaths0);

// importNameCodeFixNewImportPaths1_test.go

// importNameCodeFixNewImportPaths1_test.go
static void TestImportNameCodeFixNewImportPaths1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|foo/*0*/();|]
// @Filename: folder_b/f2.ts
export function foo() {};
// @Filename: tsconfig.json
{
    "compilerOptions": {
        "baseUrl": ".",
        "paths": {
            "b/*": [ "folder_b/*" ]
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "b/f2";

foo();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportPaths1, TestImportNameCodeFixNewImportPaths1);

// importNameCodeFixNewImportPaths2_test.go

// importNameCodeFixNewImportPaths2_test.go
static void TestImportNameCodeFixNewImportPaths2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|foo/*0*/();|]
// @Filename: folder_b/index.ts
export function foo() {};
// @Filename: tsconfig.path.json
{
    "compilerOptions": {
        "baseUrl": ".",
        "paths": {
            "b": [ "folder_b/index" ]
        }
    }
}
// @Filename: tsconfig.json
{
    "extends": "./tsconfig.path",
    "compilerOptions": { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "b";

foo();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportPaths2, TestImportNameCodeFixNewImportPaths2);

// importNameCodeFixNewImportPaths_withExtension_test.go

// importNameCodeFixNewImportPaths_withExtension_test.go
static void TestImportNameCodeFixNewImportPaths_withExtension(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/a.ts
[|foo|]
// @Filename: /src/thisHasPathMapping.ts
export function foo() {};
// @Filename: /tsconfig.json
{
    "compilerOptions": {
        "baseUrl": ".",
        "paths": {
            "foo": ["src/thisHasPathMapping.ts"]
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "foo";

foo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportPaths_withExtension, TestImportNameCodeFixNewImportPaths_withExtension);

// importNameCodeFixNewImportPaths_withLeadingDotSlash_test.go

// importNameCodeFixNewImportPaths_withLeadingDotSlash_test.go
static void TestImportNameCodeFixNewImportPaths_withLeadingDotSlash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
[|foo|]
// @Filename: /thisHasPathMapping.ts
export function foo() {};
// @Filename: /tsconfig.json
{
    "compilerOptions": {
        "baseUrl": ".",
        "paths": {
            "foo": ["././thisHasPathMapping"]
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "foo";

foo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportPaths_withLeadingDotSlash, TestImportNameCodeFixNewImportPaths_withLeadingDotSlash);

// importNameCodeFixNewImportPaths_withParentRelativePath_test.go

// importNameCodeFixNewImportPaths_withParentRelativePath_test.go
static void TestImportNameCodeFixNewImportPaths_withParentRelativePath(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/a.ts
[|foo|]
// @Filename: /thisHasPathMapping.ts
export function foo() {};
// @Filename: /tsconfig.json
{
    "compilerOptions": {
        "baseUrl": "src",
        "paths": {
            "foo": ["..\\thisHasPathMapping"]
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "foo";

foo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportPaths_withParentRelativePath, TestImportNameCodeFixNewImportPaths_withParentRelativePath);

// importNameCodeFixNewImportRootDirs0_test.go

// importNameCodeFixNewImportRootDirs0_test.go
static void TestImportNameCodeFixNewImportRootDirs0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a/f1.ts
[|foo/*0*/();|]
// @Filename: b/c/f2.ts
export function foo() {};
// @Filename: tsconfig.json
{
    "compilerOptions": {
        "rootDirs": [
            "a",
            "b/c"
        ]
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "./f2";

foo();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportRootDirs0, TestImportNameCodeFixNewImportRootDirs0);

// importNameCodeFixNewImportRootDirs1_test.go

// importNameCodeFixNewImportRootDirs1_test.go
static void TestImportNameCodeFixNewImportRootDirs1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a/f1.ts
[|foo/*0*/();|]
// @Filename: a/b/index.ts
export function foo() {};
// @Filename: tsconfig.json
{
    "compilerOptions": {
        "rootDirs": [
            "a"
        ]
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "./b";

foo();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportRootDirs1, TestImportNameCodeFixNewImportRootDirs1);

// importNameCodeFixNewImportTypeRoots0_test.go

// importNameCodeFixNewImportTypeRoots0_test.go
static void TestImportNameCodeFixNewImportTypeRoots0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a/f1.ts
[|foo/*0*/();|]
// @Filename: types/random/index.ts
export function foo() {};
// @Filename: tsconfig.json
{
    "compilerOptions": {
        "typeRoots": [
            "./types"
        ]
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "../types/random";

foo();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportTypeRoots0, TestImportNameCodeFixNewImportTypeRoots0);

// importNameCodeFixNewImportTypeRoots1_test.go

// importNameCodeFixNewImportTypeRoots1_test.go
static void TestImportNameCodeFixNewImportTypeRoots1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: a/f1.ts
[|foo/*0*/();|]
// @Filename: types/random/index.ts
export function foo() {};
// @Filename: tsconfig.json
{
    "compilerOptions": {
        "baseUrl": ".",
        "typeRoots": [
            "./types"
        ]
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "types/random";

foo();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixNewImportTypeRoots1, TestImportNameCodeFixNewImportTypeRoots1);

// importNameCodeFixOptionalImport0_test.go

// importNameCodeFixOptionalImport0_test.go
static void TestImportNameCodeFixOptionalImport0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a/f1.ts
[|import * as ns from "./foo";
foo/*0*/();|]
// @Filename: a/foo/bar.ts
export function foo() {};
// @Filename: a/foo.ts
export { foo } from "./foo/bar";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import * as ns from "./foo";
ns.foo();)TS", R"TS(import * as ns from "./foo";
import { foo } from "./foo";
foo();)TS", R"TS(import * as ns from "./foo";
import { foo } from "./foo/bar";
foo();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixOptionalImport0, TestImportNameCodeFixOptionalImport0);

// importNameCodeFixOptionalImport1_test.go

// importNameCodeFixOptionalImport1_test.go
static void TestImportNameCodeFixOptionalImport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a/f1.ts
[|foo/*0*/();|]
// @Filename: a/node_modules/bar/index.ts
export function foo() {};
// @Filename: a/foo.ts
export { foo } from "bar";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "bar";

foo();)TS", R"TS(import { foo } from "./foo";

foo();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixOptionalImport1, TestImportNameCodeFixOptionalImport1);

// importNameCodeFixReExport_test.go

// importNameCodeFixReExport_test.go
static void TestImportNameCodeFixReExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const x = 0";
// @Filename: /b.ts
[|export { x } from "./a";
x;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyRangeAfterCodeFix(t, R"TS(import { x } from "./a";

export { x } from "./a";
x;)TS", true, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixReExport, TestImportNameCodeFixReExport);

// importNameCodeFixShebang_test.go

// importNameCodeFixShebang_test.go
static void TestImportNameCodeFixShebang(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const foo = 0;
// @Filename: /b.ts
[|#!/usr/bin/env node
foo/**/|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.ts");
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(#!/usr/bin/env node

import { foo } from "./a";

foo)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixShebang, TestImportNameCodeFixShebang);

// importNameCodeFixUMDGlobal0_test.go

// importNameCodeFixUMDGlobal0_test.go
static void TestImportNameCodeFixUMDGlobal0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @AllowSyntheticDefaultImports: false
// @Module: es2015
// @Filename: a/f1.ts
[|export function test() { };
bar1/*0*/.bar;|]
// @Filename: a/foo.d.ts
export declare function bar(): number;
export as namespace bar1; )TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import * as bar1 from "./foo";

export function test() { };
bar1.bar;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixUMDGlobal0, TestImportNameCodeFixUMDGlobal0);

// importNameCodeFixUMDGlobal1_test.go

// importNameCodeFixUMDGlobal1_test.go
static void TestImportNameCodeFixUMDGlobal1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @AllowSyntheticDefaultImports: false
// @Module: esnext
// @Filename: a/f1.ts
[|import { bar } from "./foo";

export function test() { };
bar1/*0*/.bar();|]
// @Filename: a/foo.d.ts
export declare function bar(): number;
export as namespace bar1; )TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import * as bar1 from "./foo";
import { bar } from "./foo";

export function test() { };
bar1.bar();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixUMDGlobal1, TestImportNameCodeFixUMDGlobal1);

// importNameCodeFixUMDGlobalJavaScript_test.go

// importNameCodeFixUMDGlobalJavaScript_test.go
static void TestImportNameCodeFixUMDGlobalJavaScript(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @AllowSyntheticDefaultImports: false
// @Module: commonjs
// @CheckJs: true
// @AllowJs: true
// @Filename: a/f1.js
[|export function test() { };
bar1/*0*/.bar;|]
// @Filename: a/foo.d.ts
export declare function bar(): number;
export as namespace bar1; )TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import * as bar1 from "./foo";

export function test() { };
bar1.bar;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixUMDGlobalJavaScript, TestImportNameCodeFixUMDGlobalJavaScript);

// importNameCodeFixUMDGlobalReact0_test.go

// importNameCodeFixUMDGlobalReact0_test.go
static void TestImportNameCodeFixUMDGlobalReact0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @allowSyntheticDefaultImports: false
// @module: es2015
// @moduleResolution: bundler
// @Filename: /node_modules/@types/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    export class Component { render(): JSX.Element | null; }
}
declare global {
    namespace JSX {
        interface Element {}
    }
}
// @Filename: /a.tsx
[|import { Component } from "react";
export class MyMap extends Component { }
<MyMap/>;|]
// @Filename: /b.tsx
[|import { Component } from "react";
<></>;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import * as React from "react";
import { Component } from "react";
export class MyMap extends Component { }
<MyMap/>;)TS"}, nullptr);
		f->GoToFile(t, "/b.tsx");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import * as React from "react";
import { Component } from "react";
<></>;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixUMDGlobalReact0, TestImportNameCodeFixUMDGlobalReact0);

// importNameCodeFixUMDGlobalReact1_test.go

// importNameCodeFixUMDGlobalReact1_test.go
static void TestImportNameCodeFixUMDGlobalReact1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @allowSyntheticDefaultImports: false
// @module: es2015
// @moduleResolution: bundler
// @Filename: /node_modules/@types/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    export class Component { render(): JSX.Element | null; }
}
declare global {
    namespace JSX {
        interface Element {}
    }
}
// @Filename: /a.tsx
[|import { Component } from "react";
export class MyMap extends Component { }
<MyMap></MyMap>;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import * as React from "react";
import { Component } from "react";
export class MyMap extends Component { }
<MyMap></MyMap>;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixUMDGlobalReact1, TestImportNameCodeFixUMDGlobalReact1);

// importNameCodeFixUMDGlobalReact2_test.go

// importNameCodeFixUMDGlobalReact2_test.go
static void TestImportNameCodeFixUMDGlobalReact2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @jsxFactory: factory
// @Filename: /factory.ts
export function factory() { return {}; }
declare global {
    namespace JSX {
        interface Element {}
    }
}
// @Filename: /a.tsx
[|<div/>|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { factory } from "./factory";

<div/>)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFixUMDGlobalReact2, TestImportNameCodeFixUMDGlobalReact2);

// importNameCodeFix_HeaderComment1_test.go

// importNameCodeFix_HeaderComment1_test.go
static void TestImportNameCodeFix_HeaderComment1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const foo = 0;
// @Filename: /b.ts
export const bar = 0;
// @Filename: /c.ts
/*--------------------
 *  Copyright Header
 *--------------------*/

import { bar } from "./b";
foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/c.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(/*--------------------
 *  Copyright Header
 *--------------------*/

import { foo } from "./a";
import { bar } from "./b";
foo;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_HeaderComment1, TestImportNameCodeFix_HeaderComment1);

// importNameCodeFix_HeaderComment2_test.go

// importNameCodeFix_HeaderComment2_test.go
static void TestImportNameCodeFix_HeaderComment2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const foo = 0;
// @Filename: /b.ts
export const bar = 0;
// @Filename: /c.ts
/*--------------------
 *  Copyright Header
 *--------------------*/

const afterHeader = 1;

// non-header comment
import { bar } from "./b";
foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/c.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(/*--------------------
 *  Copyright Header
 *--------------------*/

const afterHeader = 1;

import { foo } from "./a";
// non-header comment
import { bar } from "./b";
foo;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_HeaderComment2, TestImportNameCodeFix_HeaderComment2);

// importNameCodeFix_add_all_missing_imports_test.go

// importNameCodeFix_add_all_missing_imports_test.go
static void TestImportNameCodeFix_add_all_missing_imports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const a: number;
// @Filename: /b.ts
export const b: number;
// @Filename: /c.ts
export const c: number;
// @Filename: /main.ts
a;
b;
c;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/main.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import { a } from "./a";
import { b } from "./b";
import { c } from "./c";

a;
b;
c;)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_add_all_missing_imports, TestImportNameCodeFix_add_all_missing_imports);

// importNameCodeFix_all2_test.go

// importNameCodeFix_all2_test.go
static void TestImportNameCodeFix_all2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /path.ts
export declare function join(): void;
// @Filename: /os.ts
export declare function homedir(): void;
// @Filename: /index.ts

join();
homedir();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/index.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import { homedir } from "./os";
import { join } from "./path";

join();
homedir();)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_all2, TestImportNameCodeFix_all2);

// importNameCodeFix_all_js_test.go

// importNameCodeFix_all_js_test.go
static void TestImportNameCodeFix_all_js(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		t->Skip({"Go *_js_test.go file: GOOS js-gated, never compiled on this platform"}); return;
		const std::string content = R"TS(// @module: esnext
// @allowJs: true
// @checkJs: true
// @Filename: /a.js
export class C {}
/** @typedef {number} T */
// @Filename: /b.js
C;
/** @type {T} */
const x = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.js");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import { C } from "./a";

C;
/** @type {import("./a").T} */
const x = 0;)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_all_js, TestImportNameCodeFix_all_js);

// importNameCodeFix_all_promoteType_test.go

// importNameCodeFix_all_promoteType_test.go
static void TestImportNameCodeFix_all_promoteType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export class A {}
export class B {}
export class C {}
export class D {}
export class E {}
export class F {}
export class G {}
// @Filename: /b.ts
import type { A, C, D, E, G } from './a';
type Z = B | A;
new F;
// @Filename: /c.ts
import type { A, C, D, E, G } from './a';
type Z = B | A;
type Y = F;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import { B, F, type A, type C, type D, type E, type G } from './a';
type Z = B | A;
new F;)TS"});
		f->GoToFile(t, "/c.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import type { A, B, C, D, E, F, G } from './a';
type Z = B | A;
type Y = F;)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_all_promoteType, TestImportNameCodeFix_all_promoteType);

// importNameCodeFix_all_test.go

// importNameCodeFix_all_test.go
static void TestImportNameCodeFix_all(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @esModuleInterop: false
// @allowSyntheticDefaultImports: false
// @Filename: /a.ts
export default function ad() {}
export const a0 = 0;
// @Filename: /b.ts
export default function bd() {}
export const b0 = 0;
// @Filename: /c.ts
export default function cd() {}
export const c0 = 0;
// @Filename: /d.ts
export default function dd() {}
export const d0 = 0;
export const d1 = 1;
// @Filename: /e.d.ts
declare function e(): void;
export = e;
// @Filename: /disposable.d.ts
export declare class Disposable { }
// @Filename: /disposable_global.d.ts
interface Disposable { }
// @Filename: /user.ts
import * as b from "./b";
import { } from "./c";
import dd from "./d";

ad; ad; a0; a0;
bd; bd; b0; b0;
cd; cd; c0; c0;
dd; dd; d0; d0; d1; d1;
e; e;
class X extends Disposable { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/user.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import ad, { a0 } from "./a";
import bd, * as b from "./b";
import cd, { c0 } from "./c";
import dd, { d0, d1 } from "./d";
import { Disposable } from "./disposable";
import e = require("./e");

ad; ad; a0; a0;
bd; bd; b.b0; b.b0;
cd; cd; c0; c0;
dd; dd; d0; d0; d1; d1;
e; e;
class X extends Disposable { })TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_all, TestImportNameCodeFix_all);

// importNameCodeFix_avoidRelativeNodeModules_test.go

// importNameCodeFix_avoidRelativeNodeModules_test.go
static void TestImportNameCodeFix_avoidRelativeNodeModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a/index.d.ts
// @Symlink: /b/node_modules/a/index.d.ts
// @Symlink: /c/node_modules/a/index.d.ts
export const a: number;
// @Filename: /b/index.ts
// @Symlink: /c/node_modules/b/index.d.ts
import { a } from 'a'
export const b: number;
// @Filename: /c/a_user.ts
import { a } from "a";
// @Filename: /c/foo.ts
[|import { b } from "b";
a;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/c/foo.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a } from "a";
import { b } from "b";
a;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_avoidRelativeNodeModules, TestImportNameCodeFix_avoidRelativeNodeModules);

// importNameCodeFix_barrelExport2_test.go

// importNameCodeFix_barrelExport2_test.go
static void TestImportNameCodeFix_barrelExport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @baseUrl: /
// @Filename: /proj/foo/a.ts
export const A = 0;
// @Filename: /proj/foo/b.ts
export {};
A/*sibling*/
// @Filename: /proj/foo/index.ts
export * from "./a";
export * from "./b";
// @Filename: /proj/index.ts
export * from "./foo";
export * from "./src";
// @Filename: /proj/src/a.ts
export {};
A/*parent*/
// @Filename: /proj/src/utils.ts
export function util() { return "util"; }
export { A } from "../foo/a";
// @Filename: /proj/src/index.ts
export * from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "sibling", std::vector<std::string>{"proj/foo/a", "proj/src/utils", "proj", "proj/foo"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "non-relative"}));
		f->VerifyImportFixModuleSpecifiers(t, "parent", std::vector<std::string>{"proj/foo", "proj/foo/a", "proj/src/utils", "proj"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "non-relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_barrelExport2, TestImportNameCodeFix_barrelExport2);

// importNameCodeFix_barrelExport3_test.go

// importNameCodeFix_barrelExport3_test.go
static void TestImportNameCodeFix_barrelExport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: /foo/a.ts
export const A = 0;
// @Filename: /foo/b.ts
export {};
A/*sibling*/
// @Filename: /foo/index.ts
export * from "./a";
export * from "./b";
// @Filename: /index.ts
export * from "./foo";
export * from "./src";
// @Filename: /src/a.ts
export {};
A/*parent*/
// @Filename: /src/index.ts
export * from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "sibling", std::vector<std::string>{"./a", "./index", "../index"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierEnding = "index"}));
		f->VerifyImportFixModuleSpecifiers(t, "parent", std::vector<std::string>{"../foo/a", "../foo/index", "../index"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierEnding = "index"}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_barrelExport3, TestImportNameCodeFix_barrelExport3);

// importNameCodeFix_barrelExport4_test.go

// importNameCodeFix_barrelExport4_test.go
static void TestImportNameCodeFix_barrelExport4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: preserve
// @moduleResolution: bundler
// @Filename: /foo/a.ts
export const A = 0;
// @Filename: /foo/b.ts
export {};
A/*sibling*/
// @Filename: /foo/index.ts
export * from "./a";
export * from "./b";
// @Filename: /index.ts
export * from "./foo";
export * from "./src";
// @Filename: /src/a.ts
export {};
A/*parent*/
// @Filename: /src/index.ts
export * from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "sibling", std::vector<std::string>{"./a", ".", ".."}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "parent", std::vector<std::string>{"../foo", "../foo/a", ".."}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_barrelExport4, TestImportNameCodeFix_barrelExport4);

// importNameCodeFix_barrelExport5_test.go

// importNameCodeFix_barrelExport5_test.go
static void TestImportNameCodeFix_barrelExport5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{ "type": "module" }
// @Filename: /foo/a.ts
export const A = 0;
// @Filename: /foo/b.ts
export {};
A/*sibling*/
// @Filename: /foo/index.ts
export * from "./a.js";
export * from "./b.js";
// @Filename: /index.ts
export * from "./foo/index.js";
export * from "./src/index.js";
// @Filename: /src/a.ts
export {};
A/*parent*/
// @Filename: /src/index.ts
export * from "./a.js";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "sibling", std::vector<std::string>{"./a.js", "./index.js", "../index.js"}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "parent", std::vector<std::string>{"../foo/a.js", "../foo/index.js", "../index.js"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_barrelExport5, TestImportNameCodeFix_barrelExport5);

// importNameCodeFix_barrelExport_test.go

// importNameCodeFix_barrelExport_test.go
static void TestImportNameCodeFix_barrelExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: /foo/a.ts
export const A = 0;
// @Filename: /foo/b.ts
export {};
A/*sibling*/
// @Filename: /foo/index.ts
export * from "./a";
export * from "./b";
// @Filename: /index.ts
export * from "./foo";
export * from "./src";
// @Filename: /src/a.ts
export {};
A/*parent*/
// @Filename: /src/index.ts
export * from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "sibling", std::vector<std::string>{"./a", ".", ".."}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "parent", std::vector<std::string>{"../foo", "../foo/a", ".."}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_barrelExport, TestImportNameCodeFix_barrelExport);

// importNameCodeFix_commonjs_allowSynthetic_test.go

// importNameCodeFix_commonjs_allowSynthetic_test.go
static void TestImportNameCodeFix_commonjs_allowSynthetic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @moduleResolution: bundler
// @allowJs: true
// @checkJs: true
// @allowSyntheticDefaultImports: true
// @Filename: /test_module.js
const MY_EXPORTS = {}
module.exports = MY_EXPORTS;
// @Filename: /index.js
const newVar = {
  any: MY_EXPORTS/**/,
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(const MY_EXPORTS = require("./test_module");

const newVar = {
  any: MY_EXPORTS,
})TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_commonjs_allowSynthetic, TestImportNameCodeFix_commonjs_allowSynthetic);

// importNameCodeFix_defaultExport_test.go

// importNameCodeFix_defaultExport_test.go
static void TestImportNameCodeFix_defaultExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @allowJs: true
// @checkJs: true
// @Filename: /a.js
class C {}
export default C;
// @Filename: /b.js
[|C;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.js");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import C from "./a";

C;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_defaultExport, TestImportNameCodeFix_defaultExport);

// importNameCodeFix_dollar_test.go

// importNameCodeFix_dollar_test.go
static void TestImportNameCodeFix_dollar(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @moduleResolution: bundler
// @Filename: /node_modules/qwik/index.d.ts
export declare const $: any;
// @Filename: /index.ts
import {} from "qwik";
$/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { $ } from "qwik";
$)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_dollar, TestImportNameCodeFix_dollar);

// importNameCodeFix_exportEquals_test.go

// importNameCodeFix_exportEquals_test.go
static void TestImportNameCodeFix_exportEquals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @esModuleInterop: false
// @allowSyntheticDefaultImports: false
// @Filename: /a.d.ts
declare function a(): void;
declare namespace a {
    export interface b {}
}
export = a;
// @Filename: /b.ts
a;
let x: b;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import { b } from "./a";
import a = require("./a");

a;
let x: b;)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_exportEquals, TestImportNameCodeFix_exportEquals);

// importNameCodeFix_externalNonRelateive2_test.go

// importNameCodeFix_externalNonRelateive2_test.go
static void TestImportNameCodeFix_externalNonRelateive2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/apps/app1/tsconfig.json
{
  "compilerOptions": {
    "module": "commonjs",
    "lib": ["es5"],
    "paths": {
      "shared/*": ["../../shared/*"]
    }
  },
  "include": ["src", "../../shared"]
}
// @Filename: /home/src/workspaces/project/apps/app1/src/index.ts
shared/*internal2external*/
// @Filename: /home/src/workspaces/project/apps/app1/src/app.ts
utils/*internal2internal*/
// @Filename: /home/src/workspaces/project/apps/app1/src/utils.ts
export const utils = 0;
// @Filename: /home/src/workspaces/project/shared/constants.ts
export const shared = 0;
// @Filename: /home/src/workspaces/project/shared/data.ts
shared/*external2external*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		auto opts839 = f->GetOptions();
		opts839.FormatCodeSettings.NewLineCharacter = R"TS(
)TS";
		f->Configure(t, opts839);
		f->GoToMarker(t, "internal2external");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { shared } from "shared/constants";

shared)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "project-relative"}));
		f->GoToMarker(t, "internal2internal");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { utils } from "./utils";

utils)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "project-relative"}));
		f->GoToMarker(t, "external2external");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { shared } from "./constants";

shared)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "project-relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_externalNonRelateive2, TestImportNameCodeFix_externalNonRelateive2);

// importNameCodeFix_externalNonRelative1_test.go

// importNameCodeFix_externalNonRelative1_test.go
static void TestImportNameCodeFix_externalNonRelative1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.base.json
{
  "compilerOptions": {
    "module": "commonjs",
    "lib": ["es5"],
    "paths": {
      "pkg-1/*": ["./packages/pkg-1/src/*"],
      "pkg-2/*": ["./packages/pkg-2/src/*"]
    }
  }
}
// @Filename: /home/src/workspaces/project/packages/pkg-1/package.json
{ "dependencies": { "pkg-2": "*" } }
// @Filename: /home/src/workspaces/project/packages/pkg-1/tsconfig.json
{
  "extends": "../../tsconfig.base.json",
  "references": [
    { "path": "../pkg-2" }
  ]
}
// @Filename: /home/src/workspaces/project/packages/pkg-1/src/index.ts
Pkg2/*external*/
// @Filename: /home/src/workspaces/project/packages/pkg-2/package.json
{ "types": "dist/index.d.ts" }
// @Filename: /home/src/workspaces/project/packages/pkg-2/tsconfig.json
{
  "extends": "../../tsconfig.base.json",
  "compilerOptions": { "outDir": "dist", "rootDir": "src", "composite": true, "lib": ["es5"] }
}
// @Filename: /home/src/workspaces/project/packages/pkg-2/src/index.ts
import "./utils";
// @Filename: /home/src/workspaces/project/packages/pkg-2/src/utils.ts
export const Pkg2 = {};
// @Filename: /home/src/workspaces/project/packages/pkg-2/src/blah/foo/data.ts
Pkg2/*internal*/
// @link: /home/src/workspaces/project/packages/pkg-2 -> /home/src/workspaces/project/packages/pkg-1/node_modules/pkg-2)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		auto opts1534 = f->GetOptions();
		opts1534.FormatCodeSettings.NewLineCharacter = R"TS(
)TS";
		f->Configure(t, opts1534);
		f->GoToMarker(t, "external");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { Pkg2 } from "pkg-2/utils";

Pkg2)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "project-relative"}));
		f->GoToMarker(t, "internal");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { Pkg2 } from "../../utils";

Pkg2)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierPreference = "project-relative"}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_externalNonRelative1, TestImportNameCodeFix_externalNonRelative1);

// importNameCodeFix_fileWithNoTrailingNewline_test.go

// importNameCodeFix_fileWithNoTrailingNewline_test.go
static void TestImportNameCodeFix_fileWithNoTrailingNewline(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const foo = 0;
// @Filename: /b.ts
export const bar = 0;
// @Filename: /c.ts
foo;
import { bar } from "./b";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/c.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(foo;
import { foo } from "./a";
import { bar } from "./b";)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_fileWithNoTrailingNewline, TestImportNameCodeFix_fileWithNoTrailingNewline);

// importNameCodeFix_fromPathMapping_test.go

// importNameCodeFix_fromPathMapping_test.go
static void TestImportNameCodeFix_fromPathMapping(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const foo = 0;
// @Filename: /x/y.ts
foo;
// @Filename: /tsconfig.json
{
    "compilerOptions": {
        "baseUrl": ".",
        "paths": {
            "@root/*": ["*"],
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/x/y.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "@root/a";

foo;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_fromPathMapping, TestImportNameCodeFix_fromPathMapping);

// importNameCodeFix_getCanonicalFileName_test.go

// importNameCodeFix_getCanonicalFileName_test.go
static void TestImportNameCodeFix_getCanonicalFileName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /howNow/node_modules/brownCow/index.d.ts
export const foo: number;
// @Filename: /howNow/a.ts
foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/howNow/a.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "brownCow";

foo;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_getCanonicalFileName, TestImportNameCodeFix_getCanonicalFileName);

// importNameCodeFix_importType1_test.go

// importNameCodeFix_importType1_test.go
static void TestImportNameCodeFix_importType1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @verbatimModuleSyntax: true
// @module: es2015
// @Filename: /exports.ts
export default someValue = 0;
export function Component() {}
export interface ComponentProps {}
// @Filename: /a.ts
import { Component } from "./exports.js";
interface MoreProps extends /*a*/ComponentProps {}
// @Filename: /b.ts
import someValue from "./exports.js";
interface MoreProps extends /*b*/ComponentProps {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "a");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { Component, type ComponentProps } from "./exports.js";
interface MoreProps extends ComponentProps {})TS"}, nullptr);
		f->GoToMarker(t, "b");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import someValue, { type ComponentProps } from "./exports.js";
interface MoreProps extends ComponentProps {})TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_importType1, TestImportNameCodeFix_importType1);

// importNameCodeFix_importType2_test.go

// importNameCodeFix_importType2_test.go
static void TestImportNameCodeFix_importType2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @verbatimModuleSyntax: true
// @module: es2015
// @Filename: /exports1.ts
export default interface SomeType {}
export interface OtherType {}
export interface OtherOtherType {}
export const someValue = 0;
// @Filename: /a.ts
import type SomeType from "./exports1.js";
someValue/*a*/
// @Filename: /b.ts
import { someValue } from "./exports1.js";
const b: SomeType/*b*/ = someValue;
// @Filename: /c.ts
import type SomeType from "./exports1.js";
const x: OtherType/*c*/
// @Filename: /d.ts
import type { OtherType } from "./exports1.js";
const x: OtherOtherType/*d*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "a");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import type SomeType from "./exports1.js";
import { someValue } from "./exports1.js";
someValue)TS"}, nullptr);
		f->GoToMarker(t, "b");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import type SomeType from "./exports1.js";
import { someValue } from "./exports1.js";
const b: SomeType = someValue;)TS"}, nullptr);
		f->GoToMarker(t, "c");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import type { OtherType } from "./exports1.js";
import type SomeType from "./exports1.js";
const x: OtherType)TS"}, nullptr);
		f->GoToMarker(t, "d");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import type { OtherOtherType, OtherType } from "./exports1.js";
const x: OtherOtherType)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_importType2, TestImportNameCodeFix_importType2);

// importNameCodeFix_importType3_test.go

// importNameCodeFix_importType3_test.go
static void TestImportNameCodeFix_importType3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @verbatimModuleSyntax: true
// @module: es2015
// @Filename: /exports.ts
class SomeClass {}
export type { SomeClass };
// @Filename: /a.ts
import {} from "./exports.js";
function takeSomeClass(c: SomeClass/**/))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { type SomeClass } from "./exports.js";
function takeSomeClass(c: SomeClass))TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_importType3, TestImportNameCodeFix_importType3);

// importNameCodeFix_importType4_test.go

// importNameCodeFix_importType4_test.go
static void TestImportNameCodeFix_importType4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @preserveValueImports: true
// @isolatedModules: true
// @module: es2015
// @Filename: /exports.ts
export interface SomeInterface {}
export class SomePig {}
// @Filename: /a.ts
import type { SomeInterface } from "./exports.js";
new SomePig/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { SomePig, type SomeInterface } from "./exports.js";
new SomePig)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_importType4, TestImportNameCodeFix_importType4);

// importNameCodeFix_importType5_test.go

// importNameCodeFix_importType5_test.go
static void TestImportNameCodeFix_importType5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: es2015
// @Filename: /exports.ts
export interface SomeInterface {}
export class SomePig {}
// @Filename: /a.ts
import type { SomeInterface, SomePig } from "./exports.js";
new SomePig/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { SomeInterface, SomePig } from "./exports.js";
new SomePig)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_importType5, TestImportNameCodeFix_importType5);

// importNameCodeFix_importType6_test.go

// importNameCodeFix_importType6_test.go
static void TestImportNameCodeFix_importType6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: es2015
// @esModuleInterop: true
// @jsx: react
// @Filename: /types.d.ts
declare module "react" { var React: any; export = React; export as namespace React; }
// @Filename: /a.tsx
import type React from "react";
function Component() {}
(<Component/**/ />))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import React from "react";
function Component() {}
(<Component />))TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_importType6, TestImportNameCodeFix_importType6);

// importNameCodeFix_importType7_test.go

// importNameCodeFix_importType7_test.go
static void TestImportNameCodeFix_importType7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: es2015
// @Filename: /exports.ts
export interface SomeInterface {}
export class SomePig {}
// @Filename: /a.ts
import {
    type SomeInterface,
    type SomePig,
} from "./exports.js";
new SomePig/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import {
    SomePig,
    type SomeInterface,
} from "./exports.js";
new SomePig)TS"}, nullptr);
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import {
    SomePig,
    type SomeInterface,
} from "./exports.js";
new SomePig)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import {
    type SomeInterface,
    SomePig,
} from "./exports.js";
new SomePig)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import {
    type SomeInterface,
    SomePig,
} from "./exports.js";
new SomePig)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderFirst}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_importType7, TestImportNameCodeFix_importType7);

// importNameCodeFix_importType8_test.go

// importNameCodeFix_importType8_test.go
static void TestImportNameCodeFix_importType8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: es2015
// @verbatimModuleSyntax: true
// @Filename: /exports.ts
export interface SomeInterface {}
export class SomePig {}
// @Filename: /a.ts
import type { SomeInterface, SomePig } from "./exports.js";
new SomePig/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { SomePig, type SomeInterface } from "./exports.js";
new SomePig)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_importType8, TestImportNameCodeFix_importType8);

// importNameCodeFix_importType_test.go

// importNameCodeFix_importType_test.go
static void TestImportNameCodeFix_importType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
export {};
/** @typedef {number} T */
// @Filename: /b.js
/** @type {T} */
const x = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.js");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(/** @type {import("./a").T} */
const x = 0;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_importType, TestImportNameCodeFix_importType);

// importNameCodeFix_jsCJSvsESM1_test.go

// importNameCodeFix_jsCJSvsESM1_test.go
static void TestImportNameCodeFix_jsCJSvsESM1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: types/dep.d.ts
export declare class Dep {}
// @Filename: index.js
Dep/**/
// @Filename: util.js
import fs from 'fs';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { Dep } from "./types/dep";

Dep)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsCJSvsESM1, TestImportNameCodeFix_jsCJSvsESM1);

// importNameCodeFix_jsCJSvsESM2_test.go

// importNameCodeFix_jsCJSvsESM2_test.go
static void TestImportNameCodeFix_jsCJSvsESM2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: types/dep.d.ts
export declare class Dep {}
// @Filename: index.js
Dep/**/
// @Filename: util1.ts
import fs from 'fs';
// @Filename: util2.js
const fs = require('fs');)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(const { Dep } = require("./types/dep");

Dep)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsCJSvsESM2, TestImportNameCodeFix_jsCJSvsESM2);

// importNameCodeFix_jsCJSvsESM3_test.go

// importNameCodeFix_jsCJSvsESM3_test.go
static void TestImportNameCodeFix_jsCJSvsESM3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: types/dep.d.ts
export declare class Dep {}
// @Filename: index.js
import fs from 'fs';
const path = require('path');

Dep/**/
// @Filename: util2.js
export {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import fs from 'fs';
import { Dep } from './types/dep';
const path = require('path');

Dep)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsCJSvsESM3, TestImportNameCodeFix_jsCJSvsESM3);

// importNameCodeFix_jsExtension_test.go

// importNameCodeFix_jsExtension_test.go
static void TestImportNameCodeFix_jsExtension(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @noLib: true
// @jsx: preserve
// @Filename: /a.ts
export function a() {}
// @Filename: /b.ts
export function b() {}
// @Filename: /c.tsx
export function c() {}
// @Filename: /c.ts
import * as g from "global"; // Global imports skipped
import { a } from "./a.js";
import { a as a2 } from "./a"; // Ignored, only the first relative import is considered
b; c;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/c.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import * as g from "global"; // Global imports skipped
import { a } from "./a.js";
import { a as a2 } from "./a"; // Ignored, only the first relative import is considered
import { b } from "./b.js";
import { c } from "./c.jsx";
b; c;)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsExtension, TestImportNameCodeFix_jsExtension);

// importNameCodeFix_jsx1_test.go

// importNameCodeFix_jsx1_test.go
static void TestImportNameCodeFix_jsx1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @Filename: /node_modules/react/index.d.ts
export const React: any;
// @Filename: /a.tsx
[|<this>|]</this>
// @Filename: /Foo.tsx
export const Foo = 0;
// @Filename: /c.tsx
import { React } from "react";
<Foo />;
// @Filename: /d.tsx
import { Foo } from "./Foo";
<Foo />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{}, nullptr);
		f->GoToFile(t, "/c.tsx");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { React } from "react";
import { Foo } from "./Foo";
<Foo />;)TS"}, nullptr);
		f->GoToFile(t, "/d.tsx");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { React } from "react";
import { Foo } from "./Foo";
<Foo />;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsx1, TestImportNameCodeFix_jsx1);

// importNameCodeFix_jsx2_test.go

// importNameCodeFix_jsx2_test.go
static void TestImportNameCodeFix_jsx2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @jsx: react
// @module: esnext
// @esModuleInterop: true
// @moduleResolution: bundler
// @Filename: /node_modules/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    class Component {}
}
// @Filename: /node_modules/react-native/index.d.ts
import * as React from "react";
export class Text extends React.Component {};
// @Filename: /a.tsx
import React from "react";
<[|Text|]></Text>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Add import from "react-native")TS", .NewFileContent = R"TS(import React from "react";
import { Text } from "react-native";
<Text></Text>;)TS", .Index = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsx2, TestImportNameCodeFix_jsx2);

// importNameCodeFix_jsx3_test.go

// importNameCodeFix_jsx3_test.go
static void TestImportNameCodeFix_jsx3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @jsx: react
// @module: esnext
// @esModuleInterop: true
// @moduleResolution: bundler
// @Filename: /node_modules/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    class Component {}
}
// @Filename: /node_modules/react-native/index.d.ts
import * as React from "react";
export class Text extends React.Component {};
// @Filename: /a.tsx
import React from "react";
<Text></[|Text|]>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Add import from "react-native")TS", .NewFileContent = R"TS(import React from "react";
import { Text } from "react-native";
<Text></Text>;)TS", .Index = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsx3, TestImportNameCodeFix_jsx3);

// importNameCodeFix_jsx4_test.go

// importNameCodeFix_jsx4_test.go
static void TestImportNameCodeFix_jsx4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @module: esnext
// @esModuleInterop: true
// @moduleResolution: bundler
// @Filename: /node_modules/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    class Component {}
}
// @Filename: /node_modules/react-native/index.d.ts
import * as React from "react";
export class Text extends React.Component {};
// @Filename: /a.tsx
import { Text } from "react-native";
<Text></Text>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Import 'React' from "react")TS", .NewFileContent = R"TS(import React from "react";
import { Text } from "react-native";
<Text></Text>;)TS", .Index = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsx4, TestImportNameCodeFix_jsx4);

// importNameCodeFix_jsx5_test.go

// importNameCodeFix_jsx5_test.go
static void TestImportNameCodeFix_jsx5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @jsx: react
// @module: esnext
// @esModuleInterop: true
// @moduleResolution: bundler
// @Filename: /node_modules/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    class Component {}
}
// @Filename: /node_modules/react-native/index.d.ts
import * as React from "react";
export class Text extends React.Component {};
// @Filename: /a.tsx
import React from "react";
<[|Text|] />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Add import from "react-native")TS", .NewFileContent = R"TS(import React from "react";
import { Text } from "react-native";
<Text />;)TS", .Index = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsx5, TestImportNameCodeFix_jsx5);

// importNameCodeFix_jsx6_test.go

// importNameCodeFix_jsx6_test.go
static void TestImportNameCodeFix_jsx6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @jsx: react
// @module: esnext
// @esModuleInterop: true
// @moduleResolution: bundler
// @Filename: /node_modules/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    class Component {}
}
// @Filename: /node_modules/react-native/index.d.ts
import * as React from "react";
export class Text extends React.Component {};
// @Filename: /a.tsx
<[|Text|]></Text>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Add import from "react-native")TS", .NewFileContent = R"TS(import { Text } from "react-native";

<Text></Text>;)TS", .Index = 0});
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Import 'React' from "react")TS", .NewFileContent = R"TS(import React from "react";

<Text></Text>;)TS", .Index = 1});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsx6, TestImportNameCodeFix_jsx6);

// importNameCodeFix_jsx7_test.go

// importNameCodeFix_jsx7_test.go
static void TestImportNameCodeFix_jsx7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @module: esnext
// @esModuleInterop: true
// @moduleResolution: bundler
// @Filename: /node_modules/react/index.d.ts
// React was not defined
// @Filename: /a.tsx
<[|Text|]></Text>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifyCodeFixNotAvailable(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsx7, TestImportNameCodeFix_jsx7);

// importNameCodeFix_jsxOpeningTagImportDefault_test.go

// importNameCodeFix_jsxOpeningTagImportDefault_test.go
static void TestImportNameCodeFix_jsxOpeningTagImportDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @jsx: react-jsx
// @Filename: /component.tsx
export default function (props: any) {}
// @Filename: /index.tsx
export function Index() {
    return <Component/**/ />;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import Component from "./component";

export function Index() {
    return <Component />;
})TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsxOpeningTagImportDefault, TestImportNameCodeFix_jsxOpeningTagImportDefault);

// importNameCodeFix_jsxReact17_test.go

// importNameCodeFix_jsxReact17_test.go
static void TestImportNameCodeFix_jsxReact17(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: preserve
// @module: commonjs
// @Filename: /node_modules/@types/react/index.d.ts
declare namespace React {
  function createElement(): any;
}
export = React;
export as namespace React;

declare global {
  namespace JSX {
    interface IntrinsicElements {}
    interface IntrinsicAttributes {}
  }  
}
// @Filename: /component.tsx
import "react";
export declare function Component(): any;
// @Filename: /index.tsx
(<Component/**/ />);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { Component } from "./component";

(<Component />);)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_jsxReact17, TestImportNameCodeFix_jsxReact17);

// importNameCodeFix_noDestructureNonObjectLiteral_test.go

// importNameCodeFix_noDestructureNonObjectLiteral_test.go
static void TestImportNameCodeFix_noDestructureNonObjectLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @target: es2015
// @strict: true
// @esModuleInterop: true
// @Filename: /array.ts
declare const arr: number[];
export = arr;
// @Filename: /class-instance-member.ts
class C { filter() {} }
export = new C();
// @Filename: /object-literal.ts
declare function filter(): void;
export = { filter };
// @Filename: /jquery.d.ts
interface JQueryStatic {
  filter(): void;
}
declare const $: JQueryStatic;
export = $;
// @Filename: /jquery.js
module.exports = {};
// @Filename: /index.ts
filter/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"./object-literal", "./jquery"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_noDestructureNonObjectLiteral, TestImportNameCodeFix_noDestructureNonObjectLiteral);

// importNameCodeFix_order2_test.go

// importNameCodeFix_order2_test.go
static void TestImportNameCodeFix_order2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const _aB: number;
export const _Ab: number;
export const aB: number;
export const Ab: number;
// @Filename: /b.ts
[|import {
    _aB,
    _Ab,
    Ab,
} from "./a";
aB;|]
// @Filename: /c.ts
[|import {
    _aB,
    _Ab,
    Ab,
} from "./a";
aB;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import {
    _aB,
    _Ab,
    Ab,
    aB,
} from "./a";
aB;)TS"}, nullptr);
		f->GoToFile(t, "/c.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import {
    _aB,
    _Ab,
    aB,
    Ab,
} from "./a";
aB;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_order2, TestImportNameCodeFix_order2);

// importNameCodeFix_order_test.go

// importNameCodeFix_order_test.go
static void TestImportNameCodeFix_order(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const foo: number;
// @Filename: /b.ts
export const foo: number;
export const bar: number;
// @Filename: /c.ts
[|import { bar } from "./b";
foo;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/c.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { bar, foo } from "./b";
foo;)TS", R"TS(import { foo } from "./a";
import { bar } from "./b";
foo;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_order, TestImportNameCodeFix_order);

// importNameCodeFix_pathsWithExtension_test.go

// importNameCodeFix_pathsWithExtension_test.go
static void TestImportNameCodeFix_pathsWithExtension(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
  "compilerOptions": {
    "target": "ESNext",
    "module": "Node16",
    "moduleResolution": "Node16",
    "rootDir": "./src",
    "outDir": "./dist",
    "paths": {
      "#internals/*": ["./src/internals/*.ts"]
    }
  },
  "include": ["src"]
}
// @Filename: /src/internals/example.ts
export function helloWorld() {}
// @Filename: /src/index.ts
helloWorld/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"#internals/example"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierEnding = "js"}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_pathsWithExtension, TestImportNameCodeFix_pathsWithExtension);

// importNameCodeFix_pathsWithoutBaseUrl1_test.go

// importNameCodeFix_pathsWithoutBaseUrl1_test.go
static void TestImportNameCodeFix_pathsWithoutBaseUrl1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: tsconfig.json
{
  "compilerOptions": {
    "module": "commonjs",
    "paths": {
      "@app/*": ["./lib/*"]
    }
  }
}
// @Filename: index.ts
utils/**/
// @Filename: lib/utils.ts
export const utils = {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { utils } from "@app/utils";

utils)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_pathsWithoutBaseUrl1, TestImportNameCodeFix_pathsWithoutBaseUrl1);

// importNameCodeFix_pathsWithoutBaseUrl2_test.go

// importNameCodeFix_pathsWithoutBaseUrl2_test.go
static void TestImportNameCodeFix_pathsWithoutBaseUrl2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /packages/test-package-1/tsconfig.json
{
  "compilerOptions": {
    "module": "commonjs",
    "paths": {
      "test-package-2/*": ["../test-package-2/src/*"]
    }
  }
}
// @Filename: /packages/test-package-1/src/common/logging.ts
export class Logger {};
// @Filename: /packages/test-package-1/src/something/index.ts
Logger/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { Logger } from "../common/logging";

Logger)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_pathsWithoutBaseUrl2, TestImportNameCodeFix_pathsWithoutBaseUrl2);

// importNameCodeFix_pnpm1_test.go

// importNameCodeFix_pnpm1_test.go
static void TestImportNameCodeFix_pnpm1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "module": "commonjs", "types": ["*"], "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/node_modules/.pnpm/@types+react@17.0.7/node_modules/@types/react/index.d.ts
export declare function Component(): void;
// @Filename: /home/src/workspaces/project/index.ts
Component/**/
// @link: /home/src/workspaces/project/node_modules/.pnpm/@types+react@17.0.7/node_modules/@types/react -> /home/src/workspaces/project/node_modules/@types/react)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { Component } from "react";

Component)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_pnpm1, TestImportNameCodeFix_pnpm1);

// importNameCodeFix_preferBaseUrl_test.go

// importNameCodeFix_preferBaseUrl_test.go
static void TestImportNameCodeFix_preferBaseUrl(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{ "compilerOptions": { "baseUrl": "./src" } }
// @Filename: /src/d0/d1/d2/file.ts
foo/**/;
// @Filename: /src/d0/a.ts
export const foo = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/src/d0/d1/d2/file.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "d0/a";

foo;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_preferBaseUrl, TestImportNameCodeFix_preferBaseUrl);

// importNameCodeFix_quoteStyle_test.go

// importNameCodeFix_quoteStyle_test.go
static void TestImportNameCodeFix_quoteStyle(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const foo: number;
// @Filename: /b.ts
[|foo;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from './a';

foo;)TS"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.QuotePreference = lsutil::QuotePreference("single")}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_quoteStyle, TestImportNameCodeFix_quoteStyle);

// importNameCodeFix_reExportDefault_test.go

// importNameCodeFix_reExportDefault_test.go
static void TestImportNameCodeFix_reExportDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /user.ts
foo;
// @Filename: /user2.ts
unnamed;
// @Filename: /user3.ts
reExportUnnamed;
// @Filename: /reExportNamed.ts
export { default } from "./named";
// @Filename: /reExportUnnamed.ts
export { default } from "./unnamed";
// @Filename: /named.ts
function foo() {}
export default foo;
// @Filename: /unnamed.ts
export default 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/user.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import foo from "./named";

foo;)TS", R"TS(import foo from "./reExportNamed";

foo;)TS"}, nullptr);
		f->GoToFile(t, "/user2.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import unnamed from "./unnamed";

unnamed;)TS", R"TS(import unnamed from "./reExportUnnamed";

unnamed;)TS"}, nullptr);
		f->GoToFile(t, "/user3.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import reExportUnnamed from "./reExportUnnamed";

reExportUnnamed;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_reExportDefault, TestImportNameCodeFix_reExportDefault);

// importNameCodeFix_reExport_test.go

// importNameCodeFix_reExport_test.go
static void TestImportNameCodeFix_reExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export default function foo(): void {}
// @Filename: /b.ts
export { default } from "./a";
// @Filename: /user.ts
[|foo;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/user.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import foo from "./a";

foo;)TS", R"TS(import foo from "./b";

foo;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_reExport, TestImportNameCodeFix_reExport);

// importNameCodeFix_require_UMD_test.go

// importNameCodeFix_require_UMD_test.go
static void TestImportNameCodeFix_require_UMD(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @module: commonjs
// @esModuleInterop: false
// @allowSyntheticDefaultImports: false
// @Filename: umd.d.ts
namespace Foo { function f() {} }
export = Foo;
export as namespace Foo;
// @Filename: index.js
Foo;
module.exports = {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "index.js");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Add import from "./umd")TS", .NewFileContent = R"TS(const Foo = require("./umd");

Foo;
module.exports = {};)TS", .Index = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_require_UMD, TestImportNameCodeFix_require_UMD);

// importNameCodeFix_require_addToExisting_test.go

// importNameCodeFix_require_addToExisting_test.go
static void TestImportNameCodeFix_require_addToExisting(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: blah.js
export default class Blah {}
export const Named1 = 0;
export const Named2 = 1;
// @Filename: index.js
var path = require('path')
  , { promisify } = require('util')
  , { Named1 } = require('./blah')

new Blah)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "index.js");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Update import from "./blah")TS", .NewFileContent = R"TS(var path = require('path')
  , { promisify } = require('util')
  , { Named1, default: Blah } = require('./blah')

new Blah)TS", .Index = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_require_addToExisting, TestImportNameCodeFix_require_addToExisting);

// importNameCodeFix_require_importVsRequire_addToExistingWins_test.go

// importNameCodeFix_require_importVsRequire_addToExistingWins_test.go
static void TestImportNameCodeFix_require_importVsRequire_addToExistingWins(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: blah.js
export default class Blah {}
export const Named1 = 0;
export const Named2 = 1;
// @Filename: index.js
var path = require('path')
  , { promisify } = require('util')
  , { Named1 } = require('./blah')

import fs from 'fs'

new Blah)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "index.js");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Update import from "./blah")TS", .NewFileContent = R"TS(var path = require('path')
  , { promisify } = require('util')
  , { Named1, default: Blah } = require('./blah')

import fs from 'fs'

new Blah)TS", .Index = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_require_importVsRequire_addToExistingWins, TestImportNameCodeFix_require_importVsRequire_addToExistingWins);

// importNameCodeFix_require_importVsRequire_importWins_test.go

// importNameCodeFix_require_importVsRequire_importWins_test.go
static void TestImportNameCodeFix_require_importVsRequire_importWins(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: blah.js
export default class Blah {}
export const Named1 = 0;
export const Named2 = 1;
// @Filename: addToExisting.js
const { Named2 } = require('./blah')
import { Named1 } from './blah'

new Blah
// @Filename: newImport.js
import fs from 'fs';
const path = require('path');

new Blah)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "addToExisting.js");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Update import from "./blah")TS", .NewFileContent = R"TS(const { Named2 } = require('./blah')
import Blah, { Named1 } from './blah'

new Blah)TS", .Index = 0});
		f->GoToFile(t, "newImport.js");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Add import from "./blah")TS", .NewFileContent = R"TS(import fs from 'fs';
import Blah from './blah';
const path = require('path');

new Blah)TS", .Index = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_require_importVsRequire_importWins, TestImportNameCodeFix_require_importVsRequire_importWins);

// importNameCodeFix_require_importVsRequire_moduleTarget_test.go

// importNameCodeFix_require_importVsRequire_moduleTarget_test.go
static void TestImportNameCodeFix_require_importVsRequire_moduleTarget(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @module: es2015
// @Filename: a.js
export const x = 0;
// @Filename: index.js
x)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "index.js");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Add import from "./a")TS", .NewFileContent = R"TS(import { x } from "./a";

x)TS", .Index = 0});
		f->GoToPosition(t, 0);
		f->InsertLine(t, R"TS(const fs = require('fs');
)TS");
		f->VerifyCodeFix(t, fourslash::VerifyCodeFixOptions{.Description = R"TS(Add import from "./a")TS", .NewFileContent = R"TS(const fs = require('fs');
const { x } = require('./a');

x)TS", .Index = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_require_importVsRequire_moduleTarget, TestImportNameCodeFix_require_importVsRequire_moduleTarget);

// importNameCodeFix_require_namedAndDefault_test.go

// importNameCodeFix_require_namedAndDefault_test.go
static void TestImportNameCodeFix_require_namedAndDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: blah.ts
export default class Blah {}
export const Named1 = 0;
export const Named2 = 1;
// @Filename: index.js
Named1 + Named2;
new Blah;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "index.js");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(const { default: Blah, Named1, Named2 } = require("./blah");

Named1 + Named2;
new Blah;)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_require_namedAndDefault, TestImportNameCodeFix_require_namedAndDefault);

// importNameCodeFix_require_test.go

// importNameCodeFix_require_test.go
static void TestImportNameCodeFix_require(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: foo.js
module.exports = function foo() {}
// @Filename: utils.js
function util1() {}
function util2() {}
module.exports = { util1, util2 };
// @Filename: blah.js
export default class Blah {}
// @Filename: index.js
foo();
util1();
util2();
new Blah;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "index.js");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(const { default: Blah } = require("./blah");
const foo = require("./foo");
const { util1, util2 } = require("./utils");

foo();
util1();
util2();
new Blah;)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_require, TestImportNameCodeFix_require);

// importNameCodeFix_shorthandPropertyAssignment1_test.go

// importNameCodeFix_shorthandPropertyAssignment1_test.go
static void TestImportNameCodeFix_shorthandPropertyAssignment1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const a = 1;
// @Filename: /b.ts
const b = { /**/a };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a } from "./a";

const b = { a };)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_shorthandPropertyAssignment1, TestImportNameCodeFix_shorthandPropertyAssignment1);

// importNameCodeFix_shorthandPropertyAssignment2_test.go

// importNameCodeFix_shorthandPropertyAssignment2_test.go
static void TestImportNameCodeFix_shorthandPropertyAssignment2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
const a = 1;
export default a;
// @Filename: /b.ts
const b = { /**/a };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import a from "./a";

const b = { a };)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_shorthandPropertyAssignment2, TestImportNameCodeFix_shorthandPropertyAssignment2);

// importNameCodeFix_sortByDistance_test.go

// importNameCodeFix_sortByDistance_test.go
static void TestImportNameCodeFix_sortByDistance(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: /src/admin/utils/db/db.ts
export const db = {};
// @Filename: /src/admin/utils/db/index.ts
export * from "./db";
// @Filename: /src/client/helpers/db.ts
export const db = {};
// @Filename: /src/client/db.ts
export const db = {};
// @Filename: /src/client/foo.ts
db/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { db } from "./db";

db)TS", R"TS(import { db } from "./helpers/db";

db)TS", R"TS(import { db } from "../admin/utils/db";

db)TS", R"TS(import { db } from "../admin/utils/db/db";

db)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_sortByDistance, TestImportNameCodeFix_sortByDistance);

// importNameCodeFix_symlink_own_package_2_test.go

// importNameCodeFix_symlink_own_package_2_test.go
static void TestImportNameCodeFix_symlink_own_package_2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /packages/a/test.ts
// @Symlink: /node_modules/a/test.ts
x;
// @Filename: /packages/a/utils.ts
// @Symlink: /node_modules/a/utils.ts
import {} from "a/utils";
export const x = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/packages/a/test.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { x } from "./utils";

x;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_symlink_own_package_2, TestImportNameCodeFix_symlink_own_package_2);

// importNameCodeFix_symlink_own_package_test.go

// importNameCodeFix_symlink_own_package_test.go
static void TestImportNameCodeFix_symlink_own_package(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /packages/b/b0.ts
// @Symlink: /node_modules/b/b0.ts
x;
// @Filename: /packages/b/b1.ts
// @Symlink: /node_modules/b/b1.ts
import { a } from "a";
export const x = 0;
// @Filename: /packages/a/index.d.ts
// @Symlink: /node_modules/a/index.d.ts
export const a: number;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/packages/b/b0.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { x } from "./b1";

x;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_symlink_own_package, TestImportNameCodeFix_symlink_own_package);

// importNameCodeFix_symlink_test.go

// importNameCodeFix_symlink_test.go
static void TestImportNameCodeFix_symlink(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @noLib: true
// @Filename: /node_modules/real/index.d.ts
// @Symlink: /node_modules/link/index.d.ts
export const foo: number;
// @Filename: /a.ts
import { foo } from "link";
// @Filename: /b.ts
[|foo;|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { foo } from "link";

foo;)TS", R"TS(import { foo } from "real";

foo;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_symlink, TestImportNameCodeFix_symlink);

// importNameCodeFix_trailingComma_test.go

// importNameCodeFix_trailingComma_test.go
static void TestImportNameCodeFix_trailingComma(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: index.ts
import {
  T2,
  T1,
} from "./types";

const x: T3/**/
// @Filename: types.ts
export type T1 = 0;
export type T2 = 0;
export type T3 = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import {
  T2,
  T1,
  T3,
} from "./types";

const x: T3)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_trailingComma, TestImportNameCodeFix_trailingComma);

// importNameCodeFix_tripleSlashOrdering_test.go

// importNameCodeFix_tripleSlashOrdering_test.go
static void TestImportNameCodeFix_tripleSlashOrdering(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
    "compilerOptions": {
        "skipDefaultLibCheck": false
    }
}
// @Filename: /a.ts
export const x = 0;
// @Filename: /b.ts
// some comment

/// <reference lib="es2017.string" />

const y = x + 1;
// @Filename: /c.ts
// some comment

/// <reference path="jquery-1.8.3.js" />

const y = x + 1;
// @Filename: /d.ts
// some comment

/// <reference types="node" />

const y = x + 1;
// @Filename: /f.ts
// some comment

/// <amd-module name="NamedModule" />

const y = x + 1;
// @Filename: /g.ts
// some comment

/// <amd-dependency path="legacy/moduleA" name="moduleA" />

const y = x + 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(// some comment

/// <reference lib="es2017.string" />

import { x } from "./a";

const y = x + 1;)TS"}, nullptr);
		f->GoToFile(t, "/c.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(// some comment

/// <reference path="jquery-1.8.3.js" />

import { x } from "./a";

const y = x + 1;)TS"}, nullptr);
		f->GoToFile(t, "/d.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(// some comment

/// <reference types="node" />

import { x } from "./a";

const y = x + 1;)TS"}, nullptr);
		f->GoToFile(t, "/f.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(// some comment

/// <amd-module name="NamedModule" />

import { x } from "./a";

const y = x + 1;)TS"}, nullptr);
		f->GoToFile(t, "/g.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(// some comment

/// <amd-dependency path="legacy/moduleA" name="moduleA" />

import { x } from "./a";

const y = x + 1;)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_tripleSlashOrdering, TestImportNameCodeFix_tripleSlashOrdering);

// importNameCodeFix_typeOnly2_test.go

// importNameCodeFix_typeOnly2_test.go
static void TestImportNameCodeFix_typeOnly2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @importsNotUsedAsValues: error
// @Filename: types.ts
export class A {}
// @Filename: index.ts
const a: A = new A();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "index.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import { A } from "./types";

const a: A = new A();)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_typeOnly2, TestImportNameCodeFix_typeOnly2);

// importNameCodeFix_typeOnly_test.go

// importNameCodeFix_typeOnly_test.go
static void TestImportNameCodeFix_typeOnly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @verbatimModuleSyntax: true
// @Filename: types.ts
export class A {}
// @Filename: index.ts
const a: /**/A)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import type { A } from "./types";

const a: A)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_typeOnly, TestImportNameCodeFix_typeOnly);

// importNameCodeFix_typeUsedAsValue_test.go

// importNameCodeFix_typeUsedAsValue_test.go
static void TestImportNameCodeFix_typeUsedAsValue(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export class ReadonlyArray<T> {}
// @Filename: /b.ts
[|new ReadonlyArray<string>();|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { ReadonlyArray } from "./a";

new ReadonlyArray<string>();)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_typeUsedAsValue, TestImportNameCodeFix_typeUsedAsValue);

// importNameCodeFix_typesVersions_test.go

// importNameCodeFix_typesVersions_test.go
static void TestImportNameCodeFix_typesVersions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @checkJs: true
// @Filename: /node_modules/unified/package.json
{
  "name": "unified",
  "types": "types/ts3.444/index.d.ts",
  "typesVersions": {
    ">=4.0": {
      "types/ts3.444/*": [
        "types/ts4.0/*"
      ]
    }
  }
}
// @Filename: /node_modules/unified/types/ts3.444/index.d.ts
export declare const x: number;
// @Filename: /node_modules/unified/types/ts4.0/index.d.ts
export declare const x: number;
// @Filename: /foo.js
import {} from "unified";
// @Filename: /index.js
x/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"unified", "unified/types/ts3.444/index.js"}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierEnding = "js", .AutoImportEntrypointDirectorySearch = Tristate::True}));
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_typesVersions, TestImportNameCodeFix_typesVersions);

// importNameCodeFix_types_classic_test.go

// importNameCodeFix_types_classic_test.go
static void TestImportNameCodeFix_types_classic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: classic
// @Filename: /node_modules/@types/foo/index.d.ts
export const xyz: number;
// @Filename: /node_modules/bar/index.d.ts
export const qrs: number;
// @Filename: /a.ts
xyz;
qrs;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import { xyz } from "foo";
import { qrs } from "./node_modules/bar/index";

xyz;
qrs;)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_types_classic, TestImportNameCodeFix_types_classic);

// importNameCodeFix_uriStyleNodeCoreModules1_test.go

// importNameCodeFix_uriStyleNodeCoreModules1_test.go
static void TestImportNameCodeFix_uriStyleNodeCoreModules1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: /node_modules/@types/node/index.d.ts
declare module "fs" { function writeFile(): void }
declare module "fs/promises" { function writeFile(): Promise<void> }
declare module "node:fs" { export * from "fs"; }
declare module "node:fs/promises" { export * from "fs/promises"; }
// @Filename: /index.ts
writeFile/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"fs", "node:fs", "fs/promises", "node:fs/promises"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_uriStyleNodeCoreModules1, TestImportNameCodeFix_uriStyleNodeCoreModules1);

// importNameCodeFix_uriStyleNodeCoreModules2_test.go

// importNameCodeFix_uriStyleNodeCoreModules2_test.go
static void TestImportNameCodeFix_uriStyleNodeCoreModules2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: /node_modules/@types/node/index.d.ts
declare module "fs" { function writeFile(): void }
declare module "fs/promises" { function writeFile(): Promise<void> }
declare module "node:fs" { export * from "fs"; }
declare module "node:fs/promises" { export * from "fs/promises"; }
// @Filename: /other.ts
import "node:fs/promises";
// @Filename: /index.ts
writeFile/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"node:fs", "node:fs/promises"}, nullptr);
		f->GoToFile(t, "/other.ts");
		f->ReplaceLine(t, 0, R"TS(
)TS");
		f->GoToFile(t, "/index.ts");
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"fs", "fs/promises", "node:fs", "node:fs/promises"}, nullptr);
		f->GoToFile(t, "/other.ts");
		f->ReplaceLine(t, 0, R"TS(import "node:fs/promises";
)TS");
		f->GoToFile(t, "/index.ts");
		f->VerifyImportFixModuleSpecifiers(t, "", std::vector<std::string>{"node:fs", "node:fs/promises"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_uriStyleNodeCoreModules2, TestImportNameCodeFix_uriStyleNodeCoreModules2);

// importNameCodeFix_uriStyleNodeCoreModules3_test.go

// importNameCodeFix_uriStyleNodeCoreModules3_test.go
static void TestImportNameCodeFix_uriStyleNodeCoreModules3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: /node_modules/@types/node/index.d.ts
declare module "path" { function join(...segments: readonly string[]): string; }
declare module "node:path" { export * from "path"; }
declare module "fs" { function writeFile(): void }
declare module "fs/promises" { function writeFile(): Promise<void> }
declare module "node:fs" { export * from "fs"; }
declare module "node:fs/promises" { export * from "fs/promises"; }
// @Filename: /other.ts
import "node:fs/promises";
// @Filename: /noPrefix.ts
import "path";
writeFile/*noPrefix*/
// @Filename: /prefix.ts
import "node:path";
writeFile/*prefix*/
// @Filename: /mixed1.ts
import "path";
import "node:path";
writeFile/*mixed1*/
// @Filename: /mixed2.ts
import "node:path";
import "path";
writeFile/*mixed2*/
// @Filename: /test1.ts
import "node:test";
import "path";
writeFile/*test1*/
// @Filename: /test2.ts
import "node:test";
writeFile/*test2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyImportFixModuleSpecifiers(t, "noPrefix", std::vector<std::string>{"fs", "fs/promises"}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "prefix", std::vector<std::string>{"node:fs", "node:fs/promises"}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "mixed1", std::vector<std::string>{"node:fs", "node:fs/promises"}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "mixed2", std::vector<std::string>{"node:fs", "node:fs/promises"}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "test1", std::vector<std::string>{"fs", "fs/promises"}, nullptr);
		f->VerifyImportFixModuleSpecifiers(t, "test2", std::vector<std::string>{"node:fs", "node:fs/promises"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_uriStyleNodeCoreModules3, TestImportNameCodeFix_uriStyleNodeCoreModules3);

// importNameCodeFix_withJson_test.go

// importNameCodeFix_withJson_test.go
static void TestImportNameCodeFix_withJson(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const a = 'a';
// @Filename: /b.ts
import "./anything.json";

a/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import { a } from "./a";
import "./anything.json";

a)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestImportNameCodeFix_withJson, TestImportNameCodeFix_withJson);

// missingMethodAfterEditAfterImport_test.go

// missingMethodAfterEditAfterImport_test.go
static void TestMissingMethodAfterEditAfterImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace foo {
    export namespace bar { namespace baz { export class boo { } } }
}

import f = /*foo*/foo;

/*delete*/var x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "foo", "namespace foo", "");
		f->GoToMarker(t, "delete");
		f->DeleteAtCaret(t, 6);
		f->VerifyQuickInfoAt(t, "foo", "namespace foo", "");
	});
}
REGISTER_FOURSLASH_TEST(TestMissingMethodAfterEditAfterImport, TestMissingMethodAfterEditAfterImport);

// sourceFixAllImports_test.go

// sourceFixAllImports_test.go
static void TestSourceFixAllImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const a: number = 1;
// @Filename: /b.ts
export const b: number = 2;
// @Filename: /main.ts
a;
b;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/main.ts");
		f->VerifyCodeFixAll(t, fourslash::VerifyCodeFixAllOptions{.FixID = "fixMissingImport", .NewFileContent = R"TS(import { a } from "./a";
import { b } from "./b";

a;
b;)TS"});
	});
}
REGISTER_FOURSLASH_TEST(TestSourceFixAllImports, TestSourceFixAllImports);

// sourceFixAllImports_test.go
static void TestSourceFixAllCodeAction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const a: number = 1;
// @Filename: /b.ts
export const b: number = 2;
// @Filename: /main.ts
a;
b;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/main.ts");
		f->VerifySourceFixAll(t, R"TS(import { a } from "./a";
import { b } from "./b";

a;
b;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSourceFixAllCodeAction, TestSourceFixAllCodeAction);

// unusedImports10FS_test.go

// unusedImports10FS_test.go
static void TestUnusedImports10FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
namespace A {
   export class Calculator {
        public handelChar() {
        }
    }
}
namespace B {
    [|import a = A;|]
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS()TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports10FS, TestUnusedImports10FS);

// unusedImports11FS_test.go

// unusedImports11FS_test.go
static void TestUnusedImports11FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[| import f1, * as s from "./file1"; |]
s.f2('hello');
// @Filename: file1.ts
export var v1;
export function f1(n: number){}
export function f2(s: string){};
export default f1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS(import * as s from "./file1";)TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports11FS, TestUnusedImports11FS);

// unusedImports12FS_test.go

// unusedImports12FS_test.go
static void TestUnusedImports12FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[| import f1, * as s from "./file1"; |]
f1(42);
// @Filename: file1.ts
export function f1(n: number){}
export function f2(s: string){};
export default f1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS(import f1 from "./file1";)TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports12FS, TestUnusedImports12FS);

// unusedImports13FS_test.go

// unusedImports13FS_test.go
static void TestUnusedImports13FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[| import A, { x } from './a'; |]
console.log(A);
// @Filename: file1.ts
export default 10;
export var x = 10;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS(import A from './a';)TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports13FS, TestUnusedImports13FS);

// unusedImports14FS_test.go

// unusedImports14FS_test.go
static void TestUnusedImports14FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[| import /* 1 */ A /* 2 */, /* 3 */ { /* 4 */ x /* 5 */ } /* 6 */ from './a'; |]
console.log(A);
// @Filename: file1.ts
export default 10;
export var x = 10;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS(import /* 1 */ A /* 2 */ /* 6 */ from './a';)TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports14FS, TestUnusedImports14FS);

// unusedImports1FS_test.go

// unusedImports1FS_test.go
static void TestUnusedImports1FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
  [|import { Calculator } from "./file1" |]
// @Filename: file1.ts
   export class Calculator {

   })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS()TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports1FS, TestUnusedImports1FS);

// unusedImports3FS_test.go

// unusedImports3FS_test.go
static void TestUnusedImports3FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[| import {Calculator, /*some comments*/ test, test2} from "./file1" |]
 test();
 test2();
// @Filename: file1.ts
 export class Calculator {
     handleChar() {}
 }
 export function test() {

 }
 export function test2() {

 })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS(import {/*some comments*/ test, test2} from "./file1")TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports3FS, TestUnusedImports3FS);

// unusedImports4FS_test.go

// unusedImports4FS_test.go
static void TestUnusedImports4FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[| import {Calculator, test, test2} from "./file1" |]

var x = new Calculator();
x.handleChar();
test2();
// @Filename: file1.ts
export class Calculator {
    handleChar() {}
}

export function test() {

}

export function test2() {

})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS(import {Calculator, test2} from "./file1")TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports4FS, TestUnusedImports4FS);

// unusedImports5FS_test.go

// unusedImports5FS_test.go
static void TestUnusedImports5FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[| import {Calculator, test, test2} from "./file1" |]

var x = new Calculator();
x.handleChar();
test();
// @Filename: file1.ts
export class Calculator {
    handleChar() {}
}

export function test() {

}

export function test2() {

})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS(import {Calculator, test} from "./file1")TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports5FS, TestUnusedImports5FS);

// unusedImports6FS_test.go

// unusedImports6FS_test.go
static void TestUnusedImports6FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[| import d from "./file1" |]
// @Filename: file1.ts
export class Calculator {
    handleChar() { }
}
export function test() {

}
export default function test2() {

})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS()TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports6FS, TestUnusedImports6FS);

// unusedImports7FS_test.go

// unusedImports7FS_test.go
static void TestUnusedImports7FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[| import * as n from "./file1" |]
// @Filename: file1.ts
export class Calculator {
    handleChar() { }
}
export function test() {
}
export default function test2() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS()TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports7FS, TestUnusedImports7FS);

// unusedImports8FS_test.go

// unusedImports8FS_test.go
static void TestUnusedImports8FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[|import {Calculator as calc, test as t1, test2 as t2} from "./file1"|]

var x = new calc();
x.handleChar();
t1();
// @Filename: file1.ts
export class Calculator {
    handleChar() { }
}
export function test() {

}
export function test2() {

})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS(import {Calculator as calc, test as t1} from "./file1")TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports8FS, TestUnusedImports8FS);

// unusedImports9FS_test.go

// unusedImports9FS_test.go
static void TestUnusedImports9FS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @noUnusedLocals: true
// @Filename: file2.ts
[|import c = require('./file1')|]
// @Filename: file1.ts
export class Calculator {
    handleChar() { }
}

export function test() {

}

export function test2() {

})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRangeAfterCodeFix(t, R"TS()TS", false, 0, 0);
	});
}
REGISTER_FOURSLASH_TEST(TestUnusedImports9FS, TestUnusedImports9FS);

} // namespace
