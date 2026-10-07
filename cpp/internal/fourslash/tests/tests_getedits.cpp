// Ported fourslash tests -- batch B (getedits). One static void TestX(gostd::testing::T*)
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

// getEditsForFileRenameWithSolutionConfigFile_test.go
static void TestGetEditsForFileRenameWithSolutionConfigFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// The parent-directory solution tsconfig only references the composite child;
		// project, so when the child file is opened the solution is created as an;
		// ancestor project without ever building its program (it stays nil). Renaming;
		// a file in the child project must not crash when iterating that nil-program;
		// solution project.;
		const std::string content = R"TS(
// @Filename: /tsconfig.json
{
  "files": [],
  "references": [
    { "path": "./src/tsconfig.json" }
  ]
}

// @Filename: /src/tsconfig.json
{
  "compilerOptions": {
    "composite": true
  },
  "files": ["./a.ts", "./b.ts"]
}

// @Filename: /src/a.ts
import { b } from "./b";
b;

// @Filename: /src/b.ts
export const b = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/b.ts", "/src/c.ts", std::unordered_map<std::string, std::string>{{
		"/src/a.ts", R"TS(import { b } from "./c";
b;
)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRenameWithSolutionConfigFile, TestGetEditsForFileRenameWithSolutionConfigFile);

// getEditsForFileRename_ambientModule_test.go
static void TestGetEditsForFileRename_ambientModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /tsconfig.json
{}
// @Filename: /sub/types.d.ts
// @Symlink: /node_modules/sub/types.d.ts
declare module "sub" {
    declare export const abc: number
}
// @Filename: /sub/package.json
// @Symlink: /node_modules/sub/package.json
{ "types": "types.d.ts" }
// @Filename: /a.ts
import { abc } from "sub";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/a.ts", "/b.ts", std::unordered_map<std::string, std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_ambientModule, TestGetEditsForFileRename_ambientModule);

// getEditsForFileRename_amd_test.go
static void TestGetEditsForFileRename_amd(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @moduleResolution: classic
// @Filename: /src/user.ts
import { x } from "old";
// @Filename: /src/old.ts
export const x = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/old.ts", "/src/new.ts", std::unordered_map<std::string, std::string>{{
		"/src/user.ts", R"TS(import { x } from "./new";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_amd, TestGetEditsForFileRename_amd);

// getEditsForFileRename_caseInsensitive_test.go
static void TestGetEditsForFileRename_caseInsensitive(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @useCaseSensitiveFileNames: false
// @Filename: /a.ts
export const a = 0;
// @Filename: /b.ts
import { a } from "./A";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/a.ts", "/eh.ts", std::unordered_map<std::string, std::string>{{
		"/b.ts", R"TS(import { a } from "./eh";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_caseInsensitive, TestGetEditsForFileRename_caseInsensitive);

// getEditsForFileRename_casing_test.go
static void TestGetEditsForFileRename_casing(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
import { foo } from "./dir/fOo";
// @Filename: /dir/fOo.ts
export const foo = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/dir", "/newDir", std::unordered_map<std::string, std::string>{{
		"/a.ts", R"TS(import { foo } from "./newDir/fOo";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_casing, TestGetEditsForFileRename_casing);

// getEditsForFileRename_cssImport2_test.go
static void TestGetEditsForFileRename_cssImport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /tsconfig.json
{ "compilerOptions": { "allowArbitraryExtensions": true } }
// @Filename: /app.css
.cookie-banner {
  display: none;
}
// @Filename: /app.d.css.ts
declare const css: {
  cookieBanner: string;
};
export default css;
// @Filename: /a.ts
import styles from "./app.css";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/app.d.css.ts", "/app2.d.css.ts", std::unordered_map<std::string, std::string>{{
		"/a.ts", R"TS(import styles from "./app2.css";)TS"},{
		"/app2.css", R"TS(.cookie-banner {
  display: none;
})TS"},{
		"/app2.d.css.ts", R"TS(declare const css: {
  cookieBanner: string;
};
export default css;)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_cssImport2, TestGetEditsForFileRename_cssImport2);

// getEditsForFileRename_cssImport3_test.go
static void TestGetEditsForFileRename_cssImport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(
// @Filename: /tsconfig.json
{ "compilerOptions": { "allowArbitraryExtensions": true } }
// @Filename: /app.css
.cookie-banner {
  display: none;
}
// @Filename: /app.d.css.ts
declare const css: {
  cookieBanner: string;
};
export default css;
// @Filename: /a.ts
import styles from ".//*rename*/app.css";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRename(t, "rename", "app2.css", std::unordered_map<std::string, std::string>{{
		"/a.ts", R"TS(import styles from "./app2.css";)TS"},{
		"/app2.d.css.ts", R"TS(declare const css: {
  cookieBanner: string;
};
export default css;)TS"},{
		"/app2.css", R"TS(.cookie-banner {
  display: none;
})TS"}});
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_cssImport3, TestGetEditsForFileRename_cssImport3);

// getEditsForFileRename_directory_down_test.go
static void TestGetEditsForFileRename_directory_down(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
/// <reference path="./src/old/file.ts" />
import old from "./src/old";
import old2 from "./src/old/file";
export default 0;
// @Filename: /src/b.ts
/// <reference path="./old/file.ts" />
import old from "./old";
import old2 from "./old/file";
export default 0;
// @Filename: /src/foo/c.ts
/// <reference path="../old/file.ts" />
import old from "../old";
import old2 from "../old/file";
export default 0;
// @Filename: /src/old/index.ts
import a from "../../a";
import a2 from "../b";
import a3 from "../foo/c";
import f from "./file";
export default 0;
// @Filename: /src/old/file.ts
export default 0;
// @Filename: /tsconfig.json
{ "files": ["a.ts", "src/b.ts", "src/foo/c.ts", "src/old/index.ts", "src/old/file.ts"] })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/old", "/src/newDir/new", std::unordered_map<std::string, std::string>{{
		"/a.ts", R"TS(/// <reference path="./src/newDir/new/file.ts" />
import old from "./src/newDir/new";
import old2 from "./src/newDir/new/file";
export default 0;)TS"},{
		"/src/b.ts", R"TS(/// <reference path="./newDir/new/file.ts" />
import old from "./newDir/new";
import old2 from "./newDir/new/file";
export default 0;)TS"},{
		"/src/foo/c.ts", R"TS(/// <reference path="../newDir/new/file.ts" />
import old from "../newDir/new";
import old2 from "../newDir/new/file";
export default 0;)TS"},{
		"/src/newDir/new/index.ts", R"TS(import a from "../../../a";
import a2 from "../../b";
import a3 from "../../foo/c";
import f from "./file";
export default 0;)TS"},{
		"/tsconfig.json", R"TS({ "files": ["a.ts", "src/b.ts", "src/foo/c.ts", "src/newDir/new/index.ts", "src/newDir/new/file.ts"] })TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_directory_down, TestGetEditsForFileRename_directory_down);

// getEditsForFileRename_directory_noUpdateNodeModulesImport_test.go
static void TestGetEditsForFileRename_directory_noUpdateNodeModulesImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a/b/file1.ts
import { foo } from "foo";
// @Filename: /a/b/node_modules/foo/index.d.ts
export const foo = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/a/b", "/a/d", std::unordered_map<std::string, std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_directory_noUpdateNodeModulesImport, TestGetEditsForFileRename_directory_noUpdateNodeModulesImport);

// getEditsForFileRename_directory_test.go
static void TestGetEditsForFileRename_directory(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
/// <reference path="./src/old/file.ts" />
import old from "./src/old";
import old2 from "./src/old/file";
export default 0;
// @Filename: /src/b.ts
/// <reference path="./old/file.ts" />
import old from "./old";
import old2 from "./old/file";
export default 0;
// @Filename: /src/foo/c.ts
/// <reference path="../old/file.ts" />
import old from "../old";
import old2 from "../old/file";
export default 0;
// @Filename: /src/old/index.ts
import a from "../../a";
import a2 from "../b";
import a3 from "../foo/c";
import f from "./file";
export default 0;
// @Filename: /src/old/file.ts
export default 0;
// @Filename: /tsconfig.json
{ "files": ["a.ts", "src/b.ts", "src/foo/c.ts", "src/old/index.ts", "src/old/file.ts"] })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/old", "/src/new", std::unordered_map<std::string, std::string>{{
		"/a.ts", R"TS(/// <reference path="./src/new/file.ts" />
import old from "./src/new";
import old2 from "./src/new/file";
export default 0;)TS"},{
		"/src/b.ts", R"TS(/// <reference path="./new/file.ts" />
import old from "./new";
import old2 from "./new/file";
export default 0;)TS"},{
		"/src/foo/c.ts", R"TS(/// <reference path="../new/file.ts" />
import old from "../new";
import old2 from "../new/file";
export default 0;)TS"},{
		"/tsconfig.json", R"TS({ "files": ["a.ts", "src/b.ts", "src/foo/c.ts", "src/new/index.ts", "src/new/file.ts"] })TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_directory, TestGetEditsForFileRename_directory);

// getEditsForFileRename_directory_up_test.go
static void TestGetEditsForFileRename_directory_up(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
/// <reference path="./src/old/file.ts" />
import old from "./src/old";
import old2 from "./src/old/file";
export default 0;
// @Filename: /src/b.ts
/// <reference path="./old/file.ts" />
import old from "./old";
import old2 from "./old/file";
export default 0;
// @Filename: /src/foo/c.ts
/// <reference path="../old/file.ts" />
import old from "../old";
import old2 from "../old/file";
export default 0;
// @Filename: /src/old/index.ts
import a from "../../a";
import a2 from "../b";
import a3 from "../foo/c";
import f from "./file";
export default 0;
// @Filename: /src/old/file.ts
export default 0;
// @Filename: /tsconfig.json
{ "files": ["a.ts", "src/b.ts", "src/foo/c.ts", "src/old/index.ts", "src/old/file.ts"] })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/old", "/newDir/new", std::unordered_map<std::string, std::string>{{
		"/a.ts", R"TS(/// <reference path="./newDir/new/file.ts" />
import old from "./newDir/new";
import old2 from "./newDir/new/file";
export default 0;)TS"},{
		"/src/b.ts", R"TS(/// <reference path="../newDir/new/file.ts" />
import old from "../newDir/new";
import old2 from "../newDir/new/file";
export default 0;)TS"},{
		"/src/foo/c.ts", R"TS(/// <reference path="../../newDir/new/file.ts" />
import old from "../../newDir/new";
import old2 from "../../newDir/new/file";
export default 0;)TS"},{
		"/newDir/new/index.ts", R"TS(import a from "../../a";
import a2 from "../../src/b";
import a3 from "../../src/foo/c";
import f from "./file";
export default 0;)TS"},{
		"/tsconfig.json", R"TS({ "files": ["a.ts", "src/b.ts", "src/foo/c.ts", "newDir/new/index.ts", "newDir/new/file.ts"] })TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_directory_up, TestGetEditsForFileRename_directory_up);

// getEditsForFileRename_jsExtension_test.go
static void TestGetEditsForFileRename_jsExtension(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @Filename: /src/a.js
export const a = 0;
// @Filename: /b.js
import { a } from "./src/a.js";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/b.js", "/src/b.js", std::unordered_map<std::string, std::string>{{
		"/src/b.js", R"TS(import { a } from "./a.js";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_jsExtension, TestGetEditsForFileRename_jsExtension);

// getEditsForFileRename_jsRename_test.go
static void TestGetEditsForFileRename_jsRename(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(
// @Filename: /tsconfig.json
{ "compilerOptions": { "module": "nodenext" } }
// @Filename: /a.ts
export const a = 1;
// @Filename: /b.ts
import { a } from ".//*rename*/a.js";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRename(t, "rename", "c.js", std::unordered_map<std::string, std::string>{{
		"/c.ts", R"TS(export const a = 1;)TS"},{
		"/b.ts", R"TS(import { a } from "./c.js";)TS"}});
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_jsRename, TestGetEditsForFileRename_jsRename);

// getEditsForFileRename_js_simple_test.go
static void TestGetEditsForFileRename_js_simple(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
import b from "./b.js";
// @Filename: /b.js
module.exports = 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/b.js", "/c.js", std::unordered_map<std::string, std::string>{{
		"/a.js", R"TS(import b from "./c.js";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_js_simple, TestGetEditsForFileRename_js_simple);

// getEditsForFileRename_keepFileExtensions_test.go
static void TestGetEditsForFileRename_keepFileExtensions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
  "compilerOptions": {
    "module": "Node16",
    "rootDirs": ["src"]
  }
}
// @Filename: /src/person.ts
export const name = 0;
// @Filename: /src/index.ts
import {name} from "./person.js";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/person.ts", "/src/vip.ts", std::unordered_map<std::string, std::string>{{
		"/src/index.ts", R"TS(import {name} from "./vip.js";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_keepFileExtensions, TestGetEditsForFileRename_keepFileExtensions);

// getEditsForFileRename_nodeModuleDirectoryCase_test.go
static void TestGetEditsForFileRename_nodeModuleDirectoryCase(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a/b/file1.ts
import { foo } from "foo";
// @Filename: /a/node_modules/foo/index.d.ts
export const foo = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/a/b", "/a/B", std::unordered_map<std::string, std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_nodeModuleDirectoryCase, TestGetEditsForFileRename_nodeModuleDirectoryCase);

// getEditsForFileRename_notAffectedByJsFile_test.go
static void TestGetEditsForFileRename_notAffectedByJsFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
export const x = 0;
// @Filename: /a.js
exports.x = 0;
// @Filename: /b.ts
import { x } from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/a.ts", "/a2.ts", std::unordered_map<std::string, std::string>{{
		"/b.ts", R"TS(import { x } from "./a2";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_notAffectedByJsFile, TestGetEditsForFileRename_notAffectedByJsFile);

// getEditsForFileRename_preferences_test.go
static void TestGetEditsForFileRename_preferences(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /dir/a.ts
export const a = 0;
// @Filename: /dir/b.ts
import {} from "dir/a";
import {} from 'dir/a';
// @Filename: /tsconfig.json
{"compilerOptions":{"paths":{"*":["*"]}}})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/dir/a.ts", "/dir/a1.ts", std::unordered_map<std::string, std::string>{{
		"/dir/b.ts", R"TS(import {} from "dir/a1";
import {} from 'dir/a1';)TS"}}, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
	.QuotePreference = lsutil::QuotePreference("single"),
	.ImportModuleSpecifierPreference = "non-relative",}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_preferences, TestGetEditsForFileRename_preferences);

// getEditsForFileRename_preservePathEnding_test.go
static void TestGetEditsForFileRename_preservePathEnding(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @strict: true
// @jsx: preserve
// @resolveJsonModule: true
// @Filename: /index.js
export const x = 0;
// @Filename: /jsx.jsx
export const y = 0;
// @Filename: /j.jonah.json
{ "j": 0 }
// @Filename: /a.js
import { x as x0 } from ".";
import { x as x1 } from "./index";
import { x as x2 } from "./index.js";
import { y } from "./jsx.jsx";
import { j } from "./j.jonah.json";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyWillRenameFilesEdits(t, "/a.js", "/b.js", std::unordered_map<std::string, std::string>{}, nullptr);
		f->VerifyWillRenameFilesEdits(t, "/b.js", "/src/b.js", std::unordered_map<std::string, std::string>{{
		"/src/b.js", R"TS(import { x as x0 } from "..";
import { x as x1 } from "../index";
import { x as x2 } from "../index.js";
import { y } from "../jsx.jsx";
import { j } from "../j.jonah.json";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_preservePathEnding, TestGetEditsForFileRename_preservePathEnding);

// getEditsForFileRename_renameFromIndex_test.go
static void TestGetEditsForFileRename_renameFromIndex(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
/// <reference path="./src/index.ts" />
import old from "./src";
import old2 from "./src/index";
// @Filename: /src/a.ts
/// <reference path="./index.ts" />
import old from ".";
import old2 from "./index";
// @Filename: /src/foo/a.ts
/// <reference path="../index.ts" />
import old from "..";
import old2 from "../index";
// @Filename: /src/index.ts

// @Filename: /tsconfig.json
{ "files": ["a.ts", "src/a.ts", "src/foo/a.ts", "src/index.ts"] })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/index.ts", "/src/new.ts", std::unordered_map<std::string, std::string>{{
		"/a.ts", R"TS(/// <reference path="./src/new.ts" />
import old from "./src/new";
import old2 from "./src/new";)TS"},{
		"/src/a.ts", R"TS(/// <reference path="./new.ts" />
import old from "./new";
import old2 from "./new";)TS"},{
		"/src/foo/a.ts", R"TS(/// <reference path="../new.ts" />
import old from "../new";
import old2 from "../new";)TS"},{
		"/tsconfig.json", R"TS({ "files": ["a.ts", "src/a.ts", "src/foo/a.ts", "src/new.ts"] })TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_renameFromIndex, TestGetEditsForFileRename_renameFromIndex);

// getEditsForFileRename_renameToIndex_test.go
static void TestGetEditsForFileRename_renameToIndex(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
/// <reference path="./src/old.ts" />
import old from "./src/old";
// @Filename: /src/a.ts
/// <reference path="./old.ts" />
import old from "./old";
// @Filename: /src/foo/a.ts
/// <reference path="../old.ts" />
import old from "../old";
// @Filename: /src/old.ts

// @Filename: /tsconfig.json
{ "files": ["a.ts", "src/a.ts", "src/foo/a.ts", "src/old.ts"] })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/old.ts", "/src/index.ts", std::unordered_map<std::string, std::string>{{
		"/a.ts", R"TS(/// <reference path="./src/index.ts" />
import old from "./src";)TS"},{
		"/src/a.ts", R"TS(/// <reference path="./index.ts" />
import old from ".";)TS"},{
		"/src/foo/a.ts", R"TS(/// <reference path="../index.ts" />
import old from "..";)TS"},{
		"/tsconfig.json", R"TS({ "files": ["a.ts", "src/a.ts", "src/foo/a.ts", "src/index.ts"] })TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_renameToIndex, TestGetEditsForFileRename_renameToIndex);

// getEditsForFileRename_resolveJsonModule_test.go
static void TestGetEditsForFileRename_resolveJsonModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @resolveJsonModule: true
// @Filename: /a.ts
import text from "./message.json";
// @Filename: /message.json
{})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/a.ts", "/src/a.ts", std::unordered_map<std::string, std::string>{{
		"/src/a.ts", R"TS(import text from "../message.json";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_resolveJsonModule, TestGetEditsForFileRename_resolveJsonModule);

// getEditsForFileRename_shortenRelativePaths_test.go
static void TestGetEditsForFileRename_shortenRelativePaths(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /src/foo/x.ts

// @Filename: /src/old.ts
import { x } from "./foo/x";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/old.ts", "/src/foo/new.ts", std::unordered_map<std::string, std::string>{{
		"/src/foo/new.ts", R"TS(import { x } from "./x";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_shortenRelativePaths, TestGetEditsForFileRename_shortenRelativePaths);

// getEditsForFileRename_subDir_test.go
static void TestGetEditsForFileRename_subDir(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /src/foo/a.ts

// @Filename: /src/old.ts
import a from "./foo/a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/old.ts", "/src/dir/new.ts", std::unordered_map<std::string, std::string>{{
		"/src/dir/new.ts", R"TS(import a from "../foo/a";)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_subDir, TestGetEditsForFileRename_subDir);

// getEditsForFileRename_symlink_test.go
static void TestGetEditsForFileRename_symlink(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /foo.ts
// @Symlink: /node_modules/foo/index.ts
export const x = 0;
// @Filename: /user.ts
import { x } from 'foo';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyWillRenameFilesEdits(t, "/user.ts", "/luser.ts", std::unordered_map<std::string, std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_symlink, TestGetEditsForFileRename_symlink);

// getEditsForFileRename_test.go
static void TestGetEditsForFileRename(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
/// <reference path="./src/old.ts" />
import old from "./src/old";
// @Filename: /src/a.ts
/// <reference path="./old.ts" />
import old from "./old";
// @Filename: /src/foo/a.ts
/// <reference path="../old.ts" />
import old from "../old";
// @Filename: /unrelated.ts
import { x } from "././src/./foo/./a";
// @Filename: /src/old.ts
export default 0;
// @Filename: /tsconfig.json
{ "files": ["a.ts", "src/a.ts", "src/foo/a.ts", "src/old.ts"] })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/old.ts", "/src/new.ts", std::unordered_map<std::string, std::string>{{
		"/a.ts", R"TS(/// <reference path="./src/new.ts" />
import old from "./src/new";)TS"},{
		"/src/a.ts", R"TS(/// <reference path="./new.ts" />
import old from "./new";)TS"},{
		"/src/foo/a.ts", R"TS(/// <reference path="../new.ts" />
import old from "../new";)TS"},{
		"/tsconfig.json", R"TS({ "files": ["a.ts", "src/a.ts", "src/foo/a.ts", "src/new.ts"] })TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename, TestGetEditsForFileRename);

// getEditsForFileRename_tsconfig_empty_include_test.go
static void TestGetEditsForFileRename_tsconfig_empty_include(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a/foo.ts
const x = 1
// @Filename: /a/tsconfig.json
{ "include": [] })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/a/foo.ts", "/a/bar.ts", std::unordered_map<std::string, std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_tsconfig_empty_include, TestGetEditsForFileRename_tsconfig_empty_include);

// getEditsForFileRename_tsconfig_include_add_test.go
static void TestGetEditsForFileRename_tsconfig_include_add(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /src/tsconfig.json
{
    "include": ["dir"],
}
// @Filename: /src/dir/a.ts
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/dir/a.ts", "/src/newDir/b.ts", std::unordered_map<std::string, std::string>{{
		"/src/tsconfig.json", R"TS({
    "include": ["dir", "newDir/b.ts"],
})TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_tsconfig_include_add, TestGetEditsForFileRename_tsconfig_include_add);

// getEditsForFileRename_tsconfig_include_noChange_test.go
static void TestGetEditsForFileRename_tsconfig_include_noChange(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /src/tsconfig.json
{
    "include": ["dir"],
}
// @Filename: /src/dir/a.ts
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/dir/a.ts", "/src/dir/b.ts", std::unordered_map<std::string, std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_tsconfig_include_noChange, TestGetEditsForFileRename_tsconfig_include_noChange);

// getEditsForFileRename_tsconfig_test.go
static void TestGetEditsForFileRename_tsconfig(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /src/tsconfig.json
{
    "compilerOptions": {
        "baseUrl": "./old",
        "paths": {
            "foo": ["old"],
        },
        "rootDir": "old",
        "rootDirs": ["old"],
        "typeRoots": ["old"],
    },
    "files": ["old/a.ts"],
    "include": ["old/*.ts"],
    "exclude": ["old"],
}
// @Filename: /src/old/someFile.ts
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/src/old", "/src/new", std::unordered_map<std::string, std::string>{{
		"/src/tsconfig.json", R"TS({
    "compilerOptions": {
        "baseUrl": "new",
        "paths": {
            "foo": ["new"],
        },
        "rootDir": "new",
        "rootDirs": ["new"],
        "typeRoots": ["new"],
    },
    "files": ["new/a.ts"],
    "include": ["new/*.ts"],
    "exclude": ["new"],
})TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_tsconfig, TestGetEditsForFileRename_tsconfig);

// getEditsForFileRename_unaffectedNonRelativePath_test.go
static void TestGetEditsForFileRename_unaffectedNonRelativePath(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /sub/a.ts
export const a = 1;
// @Filename: /sub/b.ts
import { a } from "sub/a";
// @Filename: /tsconfig.json
{"compilerOptions":{"paths":{"*":["*"]}}})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/sub/b.ts", "/sub/c/d.ts", std::unordered_map<std::string, std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_unaffectedNonRelativePath, TestGetEditsForFileRename_unaffectedNonRelativePath);

// getEditsForFileRename_unresolvableImport_test.go
static void TestGetEditsForFileRename_unresolvableImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /tsconfig.json
{
  "compilerOptions": {
    "allowJs": true,
    "paths": {
      "*": ["./next/src/*"],
      "@app": ["./modules/@app/*"],
      "@app/*": ["./modules/@app/*"],
      "@local": ["./modules/@local/*"],
      "@local/*": ["./modules/@local/*"]
    }
  }
}
// @Filename: /modules/@app/something/index.js
import "@local/some-other-import";
// @Filename: /modules/@local/index.js
import "@local/some-other-import";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/modules/@app/something", "/modules/@app/something-2", std::unordered_map<std::string, std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_unresolvableImport, TestGetEditsForFileRename_unresolvableImport);

// getEditsForFileRename_unresolvableNodeModule_test.go
static void TestGetEditsForFileRename_unresolvableNodeModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /modules/@app/something/index.js
import "doesnt-exist";
// @Filename: /modules/@local/foo.js
import "doesnt-exist"; )TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/modules/@app/something", "/modules/@app/something-2", std::unordered_map<std::string, std::string>{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_unresolvableNodeModule, TestGetEditsForFileRename_unresolvableNodeModule);


}  // namespace
