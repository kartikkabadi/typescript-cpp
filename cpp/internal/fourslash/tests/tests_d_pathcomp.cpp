// PORTED FROM Go fourslash tests (batch D, fsgen)
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


static void TestPathCompletionsPartialPathRelativeImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/main.ts
import { } from "./foo//*$*/";
// @Filename: /src/foo/async.ts
export const asyncApi = "async";
// @Filename: /src/foo/fs.ts
export const fsApi = "fs";
// @Filename: /src/foo/sync.ts
export const syncApi = "sync";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "$", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"async", "fs", "sync"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPartialPathRelativeImport, TestPathCompletionsPartialPathRelativeImport);

static void TestPathCompletionsPartialPathPackageNoExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /node_modules/@typescript/typescript/package.json
{ "name": "@typescript/typescript", "version": "0.0.0" }
// @Filename: /node_modules/@typescript/typescript/unstable/async.ts
export const asyncApi = "async";
// @Filename: /node_modules/@typescript/typescript/unstable/fs.ts
export const fsApi = "fs";
// @Filename: /node_modules/@typescript/typescript/unstable/sync.ts
export const syncApi = "sync";
// @Filename: /package.json
{ "dependencies": { "@typescript/typescript": "0.0.0" } }
// @Filename: /src/main.ts
import { } from "@typescript/typescript/unstable//*$*/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "$", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"async", "fs", "sync"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPartialPathPackageNoExports, TestPathCompletionsPartialPathPackageNoExports);

static void TestPathCompletionsPartialPathPackageExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /node_modules/@typescript/typescript/package.json
{
	"name": "@typescript/typescript",
	"version": "0.0.0",
	"exports": {
		"./unstable/sync": "./dist/api/sync/api.js",
		"./unstable/async": "./dist/api/async/api.js",
		"./unstable/fs": "./dist/api/fs.js"
	}
}
// @Filename: /node_modules/@typescript/typescript/index.d.ts
export {};
// @Filename: /node_modules/@typescript/typescript/dist/api/async/api.js
export const asyncApi = "async";
// @Filename: /node_modules/@typescript/typescript/dist/api/fs.js
export const fsApi = "fs";
// @Filename: /node_modules/@typescript/typescript/dist/api/sync/api.js
export const syncApi = "sync";
// @Filename: /package.json
{ "dependencies": { "@typescript/typescript": "0.0.0" } }
// @Filename: /src/main.ts
import { } from "@typescript/typescript/unstable//*$*/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "$", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"async", "fs", "sync"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPartialPathPackageExports, TestPathCompletionsPartialPathPackageExports);

static void TestPathCompletionsPartialPathPackageExportsEndingStar(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /node_modules/@typescript/typescript/package.json
{
	"name": "@typescript/typescript",
	"version": "0.0.0",
	"exports": {
		"./unstable/*": "./dist/unstable/*.d.ts"
	}
}
// @Filename: /node_modules/@typescript/typescript/dist/unstable/async.d.ts
export declare const asyncApi: string;
// @Filename: /node_modules/@typescript/typescript/dist/unstable/fs.d.ts
export declare const fsApi: string;
// @Filename: /node_modules/@typescript/typescript/dist/unstable/sync.d.ts
export declare const syncApi: string;
// @Filename: /package.json
{ "dependencies": { "@typescript/typescript": "0.0.0" } }
// @Filename: /src/main.ts
import { } from "@typescript/typescript/unstable//*$*/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "$", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"async", "fs", "sync"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPartialPathPackageExportsEndingStar, TestPathCompletionsPartialPathPackageExportsEndingStar);

static void TestPathCompletionsPartialPathPackageExportsMiddleStar(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /node_modules/@typescript/typescript/package.json
{
	"name": "@typescript/typescript",
	"version": "0.0.0",
	"exports": {
		"./unstable/_*/api": "./dist/api/*.d.ts"
	}
}
// @Filename: /node_modules/@typescript/typescript/dist/api/async.d.ts
export declare const asyncApi: string;
// @Filename: /node_modules/@typescript/typescript/dist/api/fs.d.ts
export declare const fsApi: string;
// @Filename: /node_modules/@typescript/typescript/dist/api/sync.d.ts
export declare const syncApi: string;
// @Filename: /package.json
{ "dependencies": { "@typescript/typescript": "0.0.0" } }
// @Filename: /src/main.ts
import { } from "@typescript/typescript/unstable//*$*/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "$", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"_async/api", "_fs/api", "_sync/api"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPartialPathPackageExportsMiddleStar, TestPathCompletionsPartialPathPackageExportsMiddleStar);


static void TestPathCompletionsTypesVersionsWildcard6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: /node_modules/foo/package.json
{
  "types": "index.d.ts",
  "typesVersions": {
    "*": {
      "bar/*": ["dist/*"],
      "exact-match": ["dist/index.d.ts"],
      "foo/*": ["dist/*"],
      "*": ["dist/*"]
    }
  }
}
// @Filename: /node_modules/foo/nope.d.ts
export const nope = 0;
// @Filename: /node_modules/foo/dist/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/dist/blah.d.ts
export const blah = 0;
// @Filename: /node_modules/foo/dist/foo/onlyInFooFolder.d.ts
export const foo = 0;
// @Filename: /node_modules/foo/dist/subfolder/one.d.ts
export const one = 0;
// @Filename: /a.ts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"bar", "exact-match", "foo", "blah", "index", "subfolder"}})}));
		f->Insert(t, "foo/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"blah", "index", "foo", "subfolder"}})}));
		f->Insert(t, "foo/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"onlyInFooFolder"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsTypesVersionsWildcard6, TestPathCompletionsTypesVersionsWildcard6);


static void TestPathCompletionsTypesVersionsWildcard5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: /node_modules/foo/package.json
{
  "types": "index.d.ts",
  "typesVersions": {
    "*": {
      "*": ["dist/*"],
      "foo/*": ["dist/*"],
      "bar/*": ["dist/*"],
      "exact-match": ["dist/index.d.ts"]
    }
  }
}
// @Filename: /node_modules/foo/nope.d.ts
export const nope = 0;
// @Filename: /node_modules/foo/dist/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/dist/blah.d.ts
export const blah = 0;
// @Filename: /node_modules/foo/dist/foo/onlyInFooFolder.d.ts
export const foo = 0;
// @Filename: /node_modules/foo/dist/subfolder/one.d.ts
export const one = 0;
// @Filename: /a.ts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"blah", "index", "foo", "subfolder", "bar", "exact-match"}})}));
		f->Insert(t, "foo/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"blah", "index", "foo", "subfolder"}})}));
		f->Insert(t, "foo/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"onlyInFooFolder"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsTypesVersionsWildcard5, TestPathCompletionsTypesVersionsWildcard5);


static void TestPathCompletionsTypesVersionsWildcard4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @resolveJsonModule: false
// @Filename: /node_modules/foo/package.json
{
  "types": "index.d.ts",
  "typesVersions": {
    ">=4.3.5": {
      "component-*": ["cjs/components/*"]
    }
  }
}
// @Filename: /node_modules/foo/nope.d.ts
export const nope = 0;
// @Filename: /node_modules/foo/cjs/components/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/cjs/components/blah.d.ts
export const blah = 0;
// @Filename: /node_modules/foo/cjs/components/subfolder/one.d.ts
export const one = 0;
// @Filename: /a.ts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"component-blah", "component-index", "component-subfolder", "nope", "cjs"}})}));
		f->Insert(t, "component-subfolder/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"one"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsTypesVersionsWildcard4, TestPathCompletionsTypesVersionsWildcard4);


static void TestPathCompletionsTypesVersionsWildcard3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @resolveJsonModule: false
// @Filename: /node_modules/foo/package.json
{
  "types": "index.d.ts",
  "typesVersions": {
    ">=4.3.5": {
      "browser/*": ["dist/*"]
    }
  }
}
// @Filename: /node_modules/foo/nope.d.ts
export const nope = 0;
// @Filename: /node_modules/foo/dist/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/dist/blah.d.ts
export const blah = 0;
// @Filename: /node_modules/foo/dist/subfolder/one.d.ts
export const one = 0;
// @Filename: /a.ts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"browser", "nope", "dist"}})}));
		f->Insert(t, "browser/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"blah", "index", "subfolder"}})}));
		f->Insert(t, "subfolder/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {"one"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsTypesVersionsWildcard3, TestPathCompletionsTypesVersionsWildcard3);


static void TestPathCompletionsTypesVersionsWildcard2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @resolveJsonModule: false
// @Filename: /node_modules/foo/package.json
{
  "types": "index.d.ts",
  "typesVersions": {
    "<=3.4.1": {
      "*": ["ts-old/*"]
    }
  }
}
// @Filename: /node_modules/foo/nope.d.ts
export const nope = 0;
// @Filename: /node_modules/foo/ts-old/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/ts-old/blah.d.ts
export const blah = 0;
// @Filename: /node_modules/foo/ts-old/subfolder/one.d.ts
export const one = 0;
// @Filename: /a.ts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"nope", "ts-old"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsTypesVersionsWildcard2, TestPathCompletionsTypesVersionsWildcard2);


static void TestPathCompletionsTypesVersionsWildcard1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: commonjs
// @Filename: /node_modules/foo/package.json
{
  "types": "index.d.ts",
  "typesVersions": {
    "*": {
      "*": ["dist/*"]
    }
  }
}
// @Filename: /node_modules/foo/nope.d.ts
export const nope = 0;
// @Filename: /node_modules/foo/dist/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/dist/blah.d.ts
export const blah = 0;
// @Filename: /node_modules/foo/dist/subfolder/one.d.ts
export const one = 0;
// @Filename: /a.ts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"blah", "index", "subfolder"}})}));
		f->Insert(t, "subfolder/");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"one"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsTypesVersionsWildcard1, TestPathCompletionsTypesVersionsWildcard1);


static void TestPathCompletionsTypesVersionsLocal(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /package.json
{
  "typesVersions": {
    "*": {
      "*": ["./src/*"]
    }
  }
}
// @Filename: /src/add.ts
export function add(a: number, b: number) { return a + b; }
// @Filename: /src/index.ts
import { add } from ".//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"add"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsTypesVersionsLocal, TestPathCompletionsTypesVersionsLocal);


static void TestPathCompletionsPackageJsonImportsWildcard9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @allowJs: true
// @Filename: /package.json
{
  "name": "foo",
  "imports": {
    "#*": "./dist/*.js"
  }
}
// @Filename: /dist/blah.js
export const blah = 0;
// @Filename: /index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard9, TestPathCompletionsPackageJsonImportsWildcard9);


static void TestPathCompletionsPackageJsonImportsWildcard8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "name": "foo",
  "imports": {
    "#*": "./dist/*.js"
  }
}
// @Filename: /dist/blah.js
export const blah = 0;
// @Filename: /dist/blah.d.ts
export declare const blah: 0;
// @Filename: /index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard8, TestPathCompletionsPackageJsonImportsWildcard8);


static void TestPathCompletionsPackageJsonImportsWildcard7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "name": "foo",
  "imports": {
    "#*": "./dist/*.js"
  }
}
// @Filename: /dist/blah.d.ts
export const blah = 0;
// @Filename: /index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard7, TestPathCompletionsPackageJsonImportsWildcard7);


static void TestPathCompletionsPackageJsonImportsWildcard6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "name": "foo",
  "main": "dist/index.js",
  "module": "dist/index.mjs",
  "types": "dist/index.d.ts",
  "imports": {
    "#*": "./dist/*?.d.ts"
  }
}
// @Filename: /dist/index.d.ts
export const index = 0;
// @Filename: /dist/blah?.d.ts
export const blah = 0;
// @Filename: /index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard6, TestPathCompletionsPackageJsonImportsWildcard6);


static void TestPathCompletionsPackageJsonImportsWildcard5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "name": "foo",
  "main": "dist/index.js",
  "module": "dist/index.mjs",
  "types": "dist/index.d.ts",
  "imports": {
    "#*": {
      "import": {
        "types": "./dist/types/*.d.mts",
        "default": "./dist/esm/*.mjs"
      },
      "default": {
        "types": "./dist/types/*.d.ts",
        "default": "./dist/cjs/*.js"
      }
    },
    "#only-in-cjs": {
      "require": {
        "types": "./dist/types/only-in-cjs/index.d.ts",
        "default": "./dist/cjs/only-in-cjs/index.js"
      }
    }
  }
}
// @Filename: /dist/types/index.d.mts
export const index = 0;
// @Filename: /dist/types/index.d.ts
export const index = 0;
// @Filename: /dist/types/blah.d.mts
export const blah = 0;
// @Filename: /dist/types/blah.d.ts
export const blah = 0;
// @Filename: /dist/types/only-in-cjs/index.d.ts
export const onlyInCjs = 0;
// @Filename: /index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.d.mts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#index", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#index.d.mts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard5, TestPathCompletionsPackageJsonImportsWildcard5);


static void TestPathCompletionsPackageJsonImportsWildcard4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "types": "index.d.ts",
  "imports": {
    "#*": "dist/*",
    "#foo/*": "dist/*",
    "#bar/*": "dist/*",
    "#exact-match": "dist/index.d.ts"
  }
}
// @Filename: /nope.d.ts
export const nope = 0;
// @Filename: /dist/index.d.ts
export const index = 0;
// @Filename: /dist/blah.d.ts
export const blah = 0;
// @Filename: /dist/foo/onlyInFooFolder.d.ts
export const foo = 0;
// @Filename: /dist/subfolder/one.d.ts
export const one = 0;
// @Filename: /a.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#index.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#index.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#foo", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "#foo"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#subfolder", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "#subfolder"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#bar", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "#bar"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#exact-match", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#exact-match.d.ts"})}})}));
		f->Insert(t, "#foo/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "blah.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "blah.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "index.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "index.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "foo"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "subfolder", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "subfolder"})}})}));
		f->Insert(t, "foo/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "onlyInFooFolder.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "onlyInFooFolder.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard4, TestPathCompletionsPackageJsonImportsWildcard4);


static void TestPathCompletionsPackageJsonImportsWildcard3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "types": "index.d.ts",
  "imports": {
    "#component-*": {
      "types@>=4.3.5": "types/components/*.d.ts"
    }
  }
}
// @Filename: /nope.d.ts
export const nope = 0;
// @Filename: /types/components/index.d.ts
export const index = 0;
// @Filename: /types/components/blah.d.ts
export const blah = 0;
// @Filename: /types/components/subfolder/one.d.ts
export const one = 0;
// @Filename: /a.ts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#component-blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#component-blah.d.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#component-index", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#component-index.d.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#component-subfolder", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "#component-subfolder"})}})}));
		f->Insert(t, "#component-subfolder/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "one", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "one.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard3, TestPathCompletionsPackageJsonImportsWildcard3);


static void TestPathCompletionsPackageJsonImportsWildcard2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "name": "salesforce-pageobjects",
  "version": "1.0.0",
  "imports": {
    "#*": {
      "types": "./dist/*.d.ts",
      "import": "./dist/*.mjs",
      "default": "./dist/*.js"
    }
  }
}
// @Filename: /dist/action/pageObjects/actionRenderer.d.ts
export const actionRenderer = 0;
// @Filename: /index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#action", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "#action"})}})}));
		f->Insert(t, "#action/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "pageObjects", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "pageObjects"})}})}));
		f->Insert(t, "pageObjects/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "actionRenderer", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "actionRenderer.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard2, TestPathCompletionsPackageJsonImportsWildcard2);


static void TestPathCompletionsPackageJsonImportsWildcard1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "name": "foo",
  "main": "dist/index.js",
  "module": "dist/index.mjs",
  "types": "dist/index.d.ts",
  "imports": {
    "#*": {
      "types": "./dist/*.d.ts",
      "import": "./dist/*.mjs",
      "default": "./dist/*.js"
    },
    "#arguments": {
      "types": "./dist/arguments/index.d.ts",
      "import": "./dist/arguments/index.mjs",
      "default": "./dist/arguments/index.js"
    }
  }
}
// @Filename: /dist/index.d.ts
export const index = 0;
// @Filename: /dist/blah.d.ts
export const blah = 0;
// @Filename: /dist/arguments/index.d.ts
export const arguments = 0;
// @Filename: /index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.d.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#index", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#index.d.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#arguments", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#arguments.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard1, TestPathCompletionsPackageJsonImportsWildcard1);


static void TestPathCompletionsPackageJsonImportsWildcard12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
 {
   "name": "repo",
   "imports": {
     "#foo/_*/suffix": "./src/*.ts"
   }
 }
// @Filename: /src/b.ts
export const x = 0;
// @Filename: /src/dir/x.ts
/export const x = 0;
// @Filename: /src/a.ts
import {} from "#foo//*0*/";
import {} from "#foo/dir//*1*/"; // invalid
import {} from "#foo/[|_|]/*2*/";
import {} from "#foo/_dir//*3*/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "0", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "_a/suffix", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "_a/suffix.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "_b/suffix", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "_b/suffix.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "_dir", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "_dir"})}})}));
		f->VerifyCompletions(t, "1", nullptr);
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "_a/suffix", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "_a/suffix.ts", .TextEdit = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(lsproto::TextEditOrInsertReplaceEdit{.TextEdit = std::make_shared<lsproto::TextEdit>(lsproto::TextEdit{.Range = f->Ranges()[0]->LSRange, .NewText = "_a/suffix"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "_b/suffix", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "_b/suffix.ts", .TextEdit = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(lsproto::TextEditOrInsertReplaceEdit{.TextEdit = std::make_shared<lsproto::TextEdit>(lsproto::TextEdit{.Range = f->Ranges()[0]->LSRange, .NewText = "_b/suffix"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "_dir", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "_dir", .TextEdit = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(lsproto::TextEditOrInsertReplaceEdit{.TextEdit = std::make_shared<lsproto::TextEdit>(lsproto::TextEdit{.Range = f->Ranges()[0]->LSRange, .NewText = "_dir"})})})}})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x/suffix", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "x/suffix.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard12, TestPathCompletionsPackageJsonImportsWildcard12);


static void TestPathCompletionsPackageJsonImportsWildcard11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: preserve
// @moduleResolution: bundler
// @jsx: react
// @Filename: /package.json
{
  "name": "repo",
  "imports": {
    "#*": "./src/*"
  }
}
// @Filename: /src/card.tsx
export {};
// @Filename: /main.ts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#card.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#card.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard11, TestPathCompletionsPackageJsonImportsWildcard11);


static void TestPathCompletionsPackageJsonImportsWildcard10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: preserve
// @moduleResolution: bundler
// @allowImportingTsExtensions: true
// @jsx: react
// @Filename: /package.json
{
  "name": "repo",
  "imports": {
    "#*": "./src/*"
  }
}
// @Filename: /src/card.tsx
export {};
// @Filename: /main.ts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#card.tsx", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#card.tsx"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsWildcard10, TestPathCompletionsPackageJsonImportsWildcard10);


static void TestPathCompletionsPackageJsonImportsSrcNoDistWildcard9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "rootDir": "src",
    "outDir": "dist",
    "allowJs": true
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "name": "foo",
  "imports": {
    "#*": "./dist/*.js"
  }
}
// @Filename: /home/src/workspaces/project/src/blah.js
export const blah = 0;
// @Filename: /home/src/workspaces/project/src/index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsSrcNoDistWildcard9, TestPathCompletionsPackageJsonImportsSrcNoDistWildcard9);


static void TestPathCompletionsPackageJsonImportsSrcNoDistWildcard8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "rootDir": "src",
    "outDir": "dist"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "name": "foo",
  "imports": {
    "#*": "./dist/*.js"
  }
}
// @Filename: /home/src/workspaces/project/src/blah.ts
export const blah = 0;
// @Filename: /home/src/workspaces/project/src/index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsSrcNoDistWildcard8, TestPathCompletionsPackageJsonImportsSrcNoDistWildcard8);


static void TestPathCompletionsPackageJsonImportsSrcNoDistWildcard7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "rootDir": "src",
    "outDir": "dist"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "name": "foo",
  "imports": {
    "#*": "./dist/*.js"
  }
}
// @Filename: /home/src/workspaces/project/src/blah.ts
export const blah = 0;
// @Filename: /home/src/workspaces/project/src/index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsSrcNoDistWildcard7, TestPathCompletionsPackageJsonImportsSrcNoDistWildcard7);


static void TestPathCompletionsPackageJsonImportsSrcNoDistWildcard6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "rootDir": "src",
    "outDir": "dist"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "name": "foo",
  "main": "dist/index.js",
  "module": "dist/index.mjs",
  "types": "dist/index.d.ts",
  "imports": {
    "#*": "./dist/*?.d.ts"
  }
}
// @Filename: /home/src/workspaces/project/src/index.ts
export const index = 0;
// @Filename: /home/src/workspaces/project/src/blah?.ts
export const blah = 0;
// @Filename: /home/src/workspaces/project/src/m.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsSrcNoDistWildcard6, TestPathCompletionsPackageJsonImportsSrcNoDistWildcard6);


static void TestPathCompletionsPackageJsonImportsSrcNoDistWildcard5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "rootDir": "src",
    "outDir": "dist/esm",
    "declarationDir": "dist/types"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "name": "foo",
  "main": "dist/index.js",
  "module": "dist/index.mjs",
  "types": "dist/index.d.ts",
  "imports": {
    "#*": {
      "import": {
        "types": "./dist/types/*.d.mts",
        "default": "./dist/esm/*.mjs"
      },
      "default": {
        "types": "./dist/types/*.d.ts",
        "default": "./dist/cjs/*.js"
      }
    },
    "#only-in-cjs": {
      "require": {
        "types": "./dist/types/only-in-cjs/index.d.ts",
        "default": "./dist/cjs/only-in-cjs/index.js"
      }
    }
  }
}
// @Filename: /home/src/workspaces/project/src/index.mts
export const index = 0;
// @Filename: /home/src/workspaces/project/src/index.ts
export const index = 0;
// @Filename: /home/src/workspaces/project/src/blah.mts
export const blah = 0;
// @Filename: /home/src/workspaces/project/src/blah.ts
export const blah = 0;
// @Filename: /home/src/workspaces/project/src/only-in-cjs/index.ts
export const onlyInCjs = 0;
// @Filename: /home/src/workspaces/project/src/index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.mts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#index", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#index.mts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsSrcNoDistWildcard5, TestPathCompletionsPackageJsonImportsSrcNoDistWildcard5);


static void TestPathCompletionsPackageJsonImportsSrcNoDistWildcard4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "rootDir": "src",
    "outDir": "dist"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "types": "index.d.ts",
  "imports": {
    "#*": "dist/*",
    "#foo/*": "dist/*",
    "#bar/*": "dist/*",
    "#exact-match": "dist/index.d.ts"
  }
}
// @Filename: /home/src/workspaces/project/nope.ts
export const nope = 0;
// @Filename: /home/src/workspaces/project/src/index.ts
export const index = 0;
// @Filename: /home/src/workspaces/project/src/blah.ts
export const blah = 0;
// @Filename: /home/src/workspaces/project/src/foo/onlyInFooFolder.ts
export const foo = 0;
// @Filename: /home/src/workspaces/project/src/subfolder/one.ts
export const one = 0;
// @Filename: /home/src/workspaces/project/src/a.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#a.mjs", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#a.mjs"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#index.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#index.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#foo", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "#foo"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#subfolder", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "#subfolder"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#bar", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "#bar"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#exact-match", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#exact-match.d.ts"})}})}));
		f->Insert(t, "#foo/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "a.mjs", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "a.mjs"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "blah.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "blah.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "index.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "index.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "foo"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "subfolder", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "subfolder"})}})}));
		f->Insert(t, "foo/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "onlyInFooFolder.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "onlyInFooFolder.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsSrcNoDistWildcard4, TestPathCompletionsPackageJsonImportsSrcNoDistWildcard4);


static void TestPathCompletionsPackageJsonImportsSrcNoDistWildcard3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "rootDir": "src",
    "outDir": "dist",
    "declarationDir": "types"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "types": "index.d.ts",
  "imports": {
    "#component-*": {
      "types@>=4.3.5": "types/components/*.d.ts"
    }
  }
}
// @Filename: /home/src/workspaces/project/nope.ts
export const nope = 0;
// @Filename: /home/src/workspaces/project/src/components/index.ts
export const index = 0;
// @Filename: /home/src/workspaces/project/src/components/blah.ts
export const blah = 0;
// @Filename: /home/src/workspaces/project/src/components/subfolder/one.ts
export const one = 0;
// @Filename: /home/src/workspaces/project/src/a.ts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#component-blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#component-blah.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#component-index", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#component-index.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#component-subfolder", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "#component-subfolder"})}})}));
		f->Insert(t, "#component-subfolder/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "one", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "one.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsSrcNoDistWildcard3, TestPathCompletionsPackageJsonImportsSrcNoDistWildcard3);


static void TestPathCompletionsPackageJsonImportsSrcNoDistWildcard2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "rootDir": "src",
    "outDir": "dist"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "name": "salesforce-pageobjects",
  "version": "1.0.0",
  "imports": {
    "#*": {
      "types": "./dist/*.d.ts",
      "import": "./dist/*.mjs",
      "default": "./dist/*.js"
    }
  }
}
// @Filename: /home/src/workspaces/project/src/action/pageObjects/actionRenderer.ts
export const actionRenderer = 0;
// @Filename: /home/src/workspaces/project/src/index.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#action", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "#action"})}})}));
		f->Insert(t, "#action/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "pageObjects", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "pageObjects"})}})}));
		f->Insert(t, "pageObjects/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "actionRenderer", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "actionRenderer.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsSrcNoDistWildcard2, TestPathCompletionsPackageJsonImportsSrcNoDistWildcard2);


static void TestPathCompletionsPackageJsonImportsSrcNoDistWildcard1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/tsconfig.json
{
  "compilerOptions": {
    "module": "nodenext",
    "rootDir": "src",
    "outDir": "dist"
  }
}
// @Filename: /home/src/workspaces/project/package.json
{
  "name": "foo",
  "main": "dist/index.js",
  "module": "dist/index.mjs",
  "types": "dist/index.d.ts",
  "imports": {
    "#*": {
      "types": "./dist/*.d.ts",
      "import": "./dist/*.mjs",
      "default": "./dist/*.js"
    },
    "#arguments": {
      "types": "./dist/arguments/index.d.ts",
      "import": "./dist/arguments/index.mjs",
      "default": "./dist/arguments/index.js"
    }
  }
}
// @Filename: /home/src/workspaces/project/src/index.ts
export const index = 0;
// @Filename: /home/src/workspaces/project/src/blah.ts
export const blah = 0;
// @Filename: /home/src/workspaces/project/src/arguments/index.ts
export const arguments = 0;
// @Filename: /home/src/workspaces/project/src/m.mts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#blah.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#index", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#index.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#arguments", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#arguments.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsSrcNoDistWildcard1, TestPathCompletionsPackageJsonImportsSrcNoDistWildcard1);


static void TestPathCompletionsPackageJsonImportsOnlyFromClosestScope1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#thing": "./src/something.ts"
  }
}
// @Filename: /src/package.json
{}
// @Filename: /src/something.ts
export function something(name: string): any;
// @Filename: /src/a.ts
import {} from "/*1*/";
// @Filename: /a.ts
import {} from "/*2*/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"1"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {}})}));
		f->VerifyCompletions(t, std::vector<std::string>{"2"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"#thing"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsOnlyFromClosestScope1, TestPathCompletionsPackageJsonImportsOnlyFromClosestScope1);


static void TestPathCompletionsPackageJsonImportsIgnoreMatchingNodeModule2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /package.json
{
  "imports": {
    "#internal/*": "./src/*.ts"
  }
}
// @Filename: /src/something.ts
export function something(name: string): any;
// @Filename: /src/node_modules/#internal/package.json
{}
// @Filename: /src/a.ts
import {} from "#internal//*1*/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"1"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"a", "something"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsIgnoreMatchingNodeModule2, TestPathCompletionsPackageJsonImportsIgnoreMatchingNodeModule2);


static void TestPathCompletionsPackageJsonImportsIgnoreMatchingNodeModule1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /src/node_modules/#internal/package.json
{
  "imports": {
    "#thing": "./dist/something.js"
  }
}
// @Filename: /src/node_modules/#internal/dist/something.d.ts
export function something(name: string): any;
// @Filename: /src/a.ts
import {} from "#internal//*1*/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"1"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsIgnoreMatchingNodeModule1, TestPathCompletionsPackageJsonImportsIgnoreMatchingNodeModule1);


static void TestPathCompletionsPackageJsonImportsCustomConditions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @customConditions: custom-condition
// @Filename: /package.json
{
  "name": "foo",
  "imports": {
    "#only-with-custom-conditions": {
      "custom-condition": "./something.js"
    }
  }
}
// @Filename: /something.d.ts
export const index = 0;
// @Filename: /index.ts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#only-with-custom-conditions", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#only-with-custom-conditions.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsCustomConditions, TestPathCompletionsPackageJsonImportsCustomConditions);


static void TestPathCompletionsPackageJsonImportsBundlerNoNodeCondition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /package.json
{
  "name": "foo",
  "imports": {
    "#only-for-node": {
      "node": "./something.js"
    },
    "#for-everywhere": "./other.js"
  }
}
// @Filename: /something.d.ts
export const index = 0;
// @Filename: /other.d.ts
export const index = 0;
// @Filename: /index.ts
import { } from "/**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#for-everywhere", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "#for-everywhere.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonImportsBundlerNoNodeCondition, TestPathCompletionsPackageJsonImportsBundlerNoNodeCondition);


static void TestPathCompletionsPackageJsonExportsWildcard9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @allowJs: true
// @maxNodeModuleJsDepth: 1
// @Filename: /node_modules/foo/package.json
{
  "name": "foo",
  "exports": {
    "./*": "./dist/*.js"
  }
}
// @Filename: /node_modules/foo/dist/blah.js
export const blah = 0;
// @Filename: /index.mts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "blah.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard9, TestPathCompletionsPackageJsonExportsWildcard9);


static void TestPathCompletionsPackageJsonExportsWildcard8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/foo/package.json
{
  "name": "foo",
  "exports": {
    "./*": "./dist/*.js"
  }
}
// @Filename: /node_modules/foo/dist/blah.js
export const blah = 0;
// @Filename: /node_modules/foo/dist/blah.d.ts
export declare const blah: 0;
// @Filename: /index.mts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "blah.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard8, TestPathCompletionsPackageJsonExportsWildcard8);


static void TestPathCompletionsPackageJsonExportsWildcard7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/foo/package.json
{
  "name": "foo",
  "exports": {
    "./*": "./dist/*.js"
  }
}
// @Filename: /node_modules/foo/dist/blah.d.ts
export const blah = 0;
// @Filename: /index.mts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "blah.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard7, TestPathCompletionsPackageJsonExportsWildcard7);


static void TestPathCompletionsPackageJsonExportsWildcard6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/foo/package.json
{
  "name": "foo",
  "main": "dist/index.js",
  "module": "dist/index.mjs",
  "types": "dist/index.d.ts",
  "exports": {
    "./*": "./dist/*?.d.ts"
  }
}
// @Filename: /node_modules/foo/dist/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/dist/blah?.d.ts
export const blah = 0;
// @Filename: /index.mts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "blah.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard6, TestPathCompletionsPackageJsonExportsWildcard6);


static void TestPathCompletionsPackageJsonExportsWildcard5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/foo/package.json
{
  "name": "foo",
  "main": "dist/index.js",
  "module": "dist/index.mjs",
  "types": "dist/index.d.ts",
  "exports": {
    ".": {
      "import": {
        "types": "./dist/types/index.d.mts",
        "default": "./dist/esm/index.mjs"
      },
      "default": {
        "types": "./dist/types/index.d.ts",
        "default": "./dist/cjs/index.js"
      }
    },
    "./*": {
      "import": {
        "types": "./dist/types/*.d.mts",
        "default": "./dist/esm/*.mjs"
      },
      "default": {
        "types": "./dist/types/*.d.ts",
        "default": "./dist/cjs/*.js"
      }
    },
    "./only-in-cjs": {
      "require": {
        "types": "./dist/types/only-in-cjs/index.d.ts",
        "default": "./dist/cjs/only-in-cjs/index.js"
      }
    }
  }
}
// @Filename: /node_modules/foo/dist/types/index.d.mts
export const index = 0;
// @Filename: /node_modules/foo/dist/types/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/dist/types/blah.d.mts
export const blah = 0;
// @Filename: /node_modules/foo/dist/types/blah.d.ts
export const blah = 0;
// @Filename: /node_modules/foo/dist/types/only-in-cjs/index.d.ts
export const onlyInCjs = 0;
// @Filename: /index.mts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "blah.d.mts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "index", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "index.d.mts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard5, TestPathCompletionsPackageJsonExportsWildcard5);


static void TestPathCompletionsPackageJsonExportsWildcard4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/foo/package.json
{
  "types": "index.d.ts",
  "exports": {
    "./*": "dist/*",
    "./foo/*": "dist/*",
    "./bar/*": "dist/*",
    "./exact-match": "dist/index.d.ts"
  }
}
// @Filename: /node_modules/foo/nope.d.ts
export const nope = 0;
// @Filename: /node_modules/foo/dist/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/dist/blah.d.ts
export const blah = 0;
// @Filename: /node_modules/foo/dist/foo/onlyInFooFolder.d.ts
export const foo = 0;
// @Filename: /node_modules/foo/dist/subfolder/one.d.ts
export const one = 0;
// @Filename: /a.mts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "blah.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "blah.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "index.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "index.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "foo"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "subfolder", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "subfolder"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "bar", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "bar"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "exact-match", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "exact-match.d.ts"})}})}));
		f->Insert(t, "foo/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "blah.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "blah.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "index.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "index.js"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "foo"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "subfolder", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "subfolder"})}})}));
		f->Insert(t, "foo/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "onlyInFooFolder.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "onlyInFooFolder.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard4, TestPathCompletionsPackageJsonExportsWildcard4);


static void TestPathCompletionsPackageJsonExportsWildcard3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/foo/package.json
{
  "types": "index.d.ts",
  "exports": {
    "./component-*": {
      "types@>=4.3.5": "types/components/*.d.ts"
    }
  }
}
// @Filename: /node_modules/foo/nope.d.ts
export const nope = 0;
// @Filename: /node_modules/foo/types/components/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/types/components/blah.d.ts
export const blah = 0;
// @Filename: /node_modules/foo/types/components/subfolder/one.d.ts
export const one = 0;
// @Filename: /a.ts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "component-blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "component-blah.d.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "component-index", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "component-index.d.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "component-subfolder", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "component-subfolder"})}})}));
		f->Insert(t, "component-subfolder/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "one", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "one.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard3, TestPathCompletionsPackageJsonExportsWildcard3);


static void TestPathCompletionsPackageJsonExportsWildcard2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/salesforce-pageobjects/package.json
{
  "name": "salesforce-pageobjects",
  "version": "1.0.0",
  "exports": {
    "./*": {
      "types": "./dist/*.d.ts",
      "import": "./dist/*.mjs",
      "default": "./dist/*.js"
    }
  }
}
// @Filename: /node_modules/salesforce-pageobjects/dist/action/pageObjects/actionRenderer.d.ts
export const actionRenderer = 0;
// @Filename: /index.mts
import { } from "salesforce-pageobjects//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "action", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "action"})}})}));
		f->Insert(t, "action/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "pageObjects", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "pageObjects"})}})}));
		f->Insert(t, "pageObjects/");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "actionRenderer", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "actionRenderer.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard2, TestPathCompletionsPackageJsonExportsWildcard2);


static void TestPathCompletionsPackageJsonExportsWildcard1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/foo/package.json
{
  "name": "foo",
  "main": "dist/index.js",
  "module": "dist/index.mjs",
  "types": "dist/index.d.ts",
  "exports": {
    ".": {
      "types": "./dist/index.d.ts",
      "import": "./dist/index.mjs",
      "default": "./dist/index.js"
    },
    "./*": {
      "types": "./dist/*.d.ts",
      "import": "./dist/*.mjs",
      "default": "./dist/*.js"
    },
    "./arguments": {
      "types": "./dist/arguments/index.d.ts",
      "import": "./dist/arguments/index.mjs",
      "default": "./dist/arguments/index.js"
    }
  }
}
// @Filename: /node_modules/foo/dist/index.d.ts
export const index = 0;
// @Filename: /node_modules/foo/dist/blah.d.ts
export const blah = 0;
// @Filename: /node_modules/foo/dist/arguments/index.d.ts
export const arguments = 0;
// @Filename: /index.mts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Unsorted = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "blah", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "blah.d.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "index", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "index.d.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "arguments", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "arguments.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard1, TestPathCompletionsPackageJsonExportsWildcard1);


static void TestPathCompletionsPackageJsonExportsWildcard12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @Filename: /node_modules/foo/package.json
 {
   "name": "foo",
   "exports": {
     "./bar/_*/suffix": "./dist/*.js"
   }
 }
// @Filename: /node_modules/foo/dist/b.d.ts
export const x = 0;
// @Filename: /node_modules/foo/dist/dir/x.d.ts
/export const x = 0;
// @Filename: /a.mts
import {} from "foo/bar//*0*/";
import {} from "foo/bar/dir//*1*/"; // invalid
import {} from "foo/bar/[|_|]/*2*/";
import {} from "foo/bar/_dir//*3*/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "0", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "_b/suffix", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "_b/suffix.d.ts"}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "_dir", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "_dir"})}})}));
		f->VerifyCompletions(t, "1", nullptr);
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "_b/suffix", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "_b/suffix.d.ts", .TextEdit = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(lsproto::TextEditOrInsertReplaceEdit{.TextEdit = std::make_shared<lsproto::TextEdit>(lsproto::TextEdit{.Range = f->Ranges()[0]->LSRange, .NewText = "_b/suffix"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "_dir", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFolder), .Detail = "_dir", .TextEdit = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(lsproto::TextEditOrInsertReplaceEdit{.TextEdit = std::make_shared<lsproto::TextEdit>(lsproto::TextEdit{.Range = f->Ranges()[0]->LSRange, .NewText = "_dir"})})})}})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x/suffix", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "x/suffix.d.ts"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard12, TestPathCompletionsPackageJsonExportsWildcard12);


static void TestPathCompletionsPackageJsonExportsWildcard11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: preserve
// @moduleResolution: bundler
// @jsx: react
// @Filename: /node_modules/repo/package.json
{
  "name": "repo",
  "exports": {
    "./*": "./src/*"
  }
}
// @Filename: /node_modules/repo/src/card.tsx
export {};
// @Filename: /main.ts
import { } from "repo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "card.js", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "card.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard11, TestPathCompletionsPackageJsonExportsWildcard11);


static void TestPathCompletionsPackageJsonExportsWildcard10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: preserve
// @moduleResolution: bundler
// @allowImportingTsExtensions: true
// @jsx: react
// @Filename: /node_modules/repo/package.json
{
  "name": "repo",
  "exports": {
    "./*": "./src/*"
  }
}
// @Filename: /node_modules/repo/src/card.tsx
export {};
// @Filename: /main.ts
import { } from "repo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "card.tsx", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "card.tsx"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsWildcard10, TestPathCompletionsPackageJsonExportsWildcard10);


static void TestPathCompletionsPackageJsonExportsCustomConditions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
// @customConditions: custom-condition
// @Filename: /node_modules/foo/package.json
{
  "name": "foo",
  "exports": {
    "./only-with-custom-conditions": {
      "custom-condition": "./something.js"
    }
  }
}
// @Filename: /node_modules/foo/something.d.ts
export const index = 0;
// @Filename: /index.ts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "only-with-custom-conditions", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "only-with-custom-conditions.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsCustomConditions, TestPathCompletionsPackageJsonExportsCustomConditions);


static void TestPathCompletionsPackageJsonExportsBundlerNoNodeCondition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @Filename: /node_modules/foo/package.json
{
  "name": "foo",
  "exports": {
    "./only-for-node": {
      "node": "./something.js"
    },
    "./for-everywhere": "./other.js"
  }
}
// @Filename: /node_modules/foo/something.d.ts
export const index = 0;
// @Filename: /node_modules/foo/other.d.ts
export const index = 0;
// @Filename: /index.ts
import { } from "foo//**/";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "for-everywhere", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = "for-everywhere.js"})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsPackageJsonExportsBundlerNoNodeCondition, TestPathCompletionsPackageJsonExportsBundlerNoNodeCondition);


static void TestPathCompletionsAllowTsExtensions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @moduleResolution: bundler
// @allowImportingTsExtensions: true
// @noEmit: true
// @Filename: /project/foo.ts
export const foo = 0;
// @Filename: /project/main.ts
import {} from ".//**/")TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"foo"}})}));
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"foo.ts"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ImportModuleSpecifierEnding = "js"})}));
		f->Insert(t, R"TS(foo.ts"
import {} from "./)TS");
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"foo.ts"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsAllowTsExtensions, TestPathCompletionsAllowTsExtensions);


static void TestPathCompletionsAllowModuleAugmentationExtensions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /project/foo.css
export const foo = 0;
// @Filename: declarations.d.ts
declare module "*.css" {}
// @Filename: /project/main.ts
import {} from ".//**/")TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"foo.css"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestPathCompletionsAllowModuleAugmentationExtensions, TestPathCompletionsAllowModuleAugmentationExtensions);
} // namespace
