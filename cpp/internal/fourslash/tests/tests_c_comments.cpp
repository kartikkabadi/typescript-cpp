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

// commentsBlocks_test.go

// commentsBlocks_test.go
static void TestCommentsBlocks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/// 1
var x,
    /*2*/// 2
    y,
/*3*/     /* %3 */
    z;

/*4*/ // 4
switch (x) {
/*5*/     // 5
    case 1:
/*6*/         // 6
        break;
/*7*/     // 7
    case 2:
/*8*/     // 8
}

/*9*/ // 9
if (true)
/*10*/     // 10
    ;
/*11*/ // 11
else {
/*12*/     // 12
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(// 1)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    // 2)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    /* %3 */)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(// 4)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    // 5)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(        // 6)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(    // 7)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(    // 8)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(// 9)TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS(    // 10)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS(// 11)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(    // 12)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestCommentsBlocks, TestCommentsBlocks);

// commentsEnumsFourslash_test.go

// commentsEnumsFourslash_test.go
static void TestCommentsEnumsFourslash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** Enum of colors*/
enum /*1*/Colors {
    /** Fancy name for 'blue'*/
    /*2*/Cornflower,
    /** Fancy name for 'pink'*/
    /*3*/FancyPink
}
var /*4*/x = /*5*/Colors./*6*/Cornflower;
x = Colors./*7*/FancyPink;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "enum Colors", "Enum of colors");
		f->VerifyQuickInfoAt(t, "2", "(enum member) Colors.Cornflower = 0", "Fancy name for 'blue'");
		f->VerifyQuickInfoAt(t, "3", "(enum member) Colors.FancyPink = 1", "Fancy name for 'pink'");
		f->VerifyQuickInfoAt(t, "4", "var x: Colors", "");
		f->VerifyQuickInfoAt(t, "5", "enum Colors", "Enum of colors");
		f->VerifyQuickInfoAt(t, "6", "(enum member) Colors.Cornflower = 0", "Fancy name for 'blue'");
		f->VerifyQuickInfoAt(t, "7", "(enum member) Colors.FancyPink = 1", "Fancy name for 'pink'");
		f->VerifyCompletions(t, "5", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Colors", .Detail = std::string("enum Colors"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "Enum of colors"})})})}})}));
		f->VerifyCompletions(t, std::vector<std::string>{"6", "7"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Cornflower", .Detail = std::string("(enum member) Colors.Cornflower = 0"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "Fancy name for 'blue'"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "FancyPink", .Detail = std::string("(enum member) Colors.FancyPink = 1"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "Fancy name for 'pink'"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCommentsEnumsFourslash, TestCommentsEnumsFourslash);

// commentsExternalModulesFourslash_test.go

// commentsExternalModulesFourslash_test.go
static void TestCommentsExternalModulesFourslash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: commentsExternalModules_file0.ts
/** Namespace comment*/
export namespace m/*1*/1 {
    /** b's comment*/
    export var b: number;
    /** foo's comment*/
    function foo() {
        return /*2*/b;
    }
    /** m2 comments*/
    export namespace m2 {
        /** class comment;*/
        export class c {
        };
        /** i*/
        export var i = new c();
    }
    /** exported function*/
    export function fooExport() {
        return f/*3q*/oo(/*3*/);
    }
}
/*4*/m1./*5*/fooEx/*6q*/port(/*6*/);
var my/*7*/var = new m1.m2./*8*/c();
// @Filename: commentsExternalModules_file1.ts
/**This is on import declaration*/
import ex/*9*/tMod = require("./commentsExternalModules_file0");
/*10*/extMod./*11*/m1./*12*/fooExp/*13q*/ort(/*13*/);
var new/*14*/Var = new extMod.m1.m2./*15*/c();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "commentsExternalModules_file0.ts");
		f->VerifyQuickInfoAt(t, "1", "namespace m1", "Namespace comment");
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b", .Detail = std::string("var b: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "b's comment"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .Detail = std::string("function foo(): number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "foo's comment"})})})}})}));
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "foo's comment"});
		f->VerifyQuickInfoAt(t, "3q", "function foo(): number", "foo's comment");
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "m1", .Detail = std::string("namespace m1"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "Namespace comment"})})})}})}));
		f->VerifyCompletions(t, "5", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b", .Detail = std::string("var m1.b: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "b's comment"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooExport", .Detail = std::string("function m1.fooExport(): number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "exported function"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "m2", .Detail = std::string("namespace m1.m2"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "m2 comments"})})})}})}));
		f->GoToMarker(t, "6");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "exported function"});
		f->VerifyQuickInfoAt(t, "6q", "function m1.fooExport(): number", "exported function");
		f->VerifyQuickInfoAt(t, "7", "var myvar: m1.m2.c", "");
		f->VerifyCompletions(t, "8", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c", .Detail = std::string("constructor m1.m2.c(): m1.m2.c"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "class comment;"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i", .Detail = std::string("var m1.m2.i: m1.m2.c"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i"})})})}})}));
		f->GoToFile(t, "commentsExternalModules_file1.ts");
		f->VerifyQuickInfoAt(t, "9", R"TS(import extMod = require("./commentsExternalModules_file0"))TS", "This is on import declaration");
		f->VerifyCompletions(t, "10", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "extMod", .Detail = std::string(R"TS(import extMod = require("./commentsExternalModules_file0"))TS"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "This is on import declaration"})})})}})}));
		f->VerifyCompletions(t, "11", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "m1", .Detail = std::string("namespace extMod.m1"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "Namespace comment"})})})}})}));
		f->VerifyCompletions(t, "12", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b", .Detail = std::string("var extMod.m1.b: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "b's comment"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooExport", .Detail = std::string("function extMod.m1.fooExport(): number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "exported function"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "m2", .Detail = std::string("namespace extMod.m1.m2"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "m2 comments"})})})}})}));
		f->GoToMarker(t, "13");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "exported function"});
		f->VerifyQuickInfoAt(t, "13q", "function extMod.m1.fooExport(): number", "exported function");
		f->VerifyQuickInfoAt(t, "14", "var newVar: extMod.m1.m2.c", "");
		f->VerifyCompletions(t, "15", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c", .Detail = std::string("constructor extMod.m1.m2.c(): extMod.m1.m2.c"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "class comment;"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i", .Detail = std::string("var extMod.m1.m2.i: extMod.m1.m2.c"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCommentsExternalModulesFourslash, TestCommentsExternalModulesFourslash);

// commentsImportDeclaration_test.go

// commentsImportDeclaration_test.go
static void TestCommentsImportDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: commentsImportDeclaration_file0.ts
/** NamespaceComment*/
export namespace m/*2*/1 {
    /** b's comment*/
    export var b: number;
    /** m2 comments*/
    export namespace m2 {
        /** class comment;*/
        export class c {
        };
        /** i*/
        export var i: c;;
    }
    /** exported function*/
    export function fooExport(): number;
}
// @Filename: commentsImportDeclaration_file1.ts
///<reference path='commentsImportDeclaration_file0.ts'/>
/** Import declaration*/
import /*3*/extMod = require("./commentsImportDeclaration_file0/*4*/");
extMod./*6*/m1./*7*/fooEx/*8q*/port(/*8*/);
var new/*9*/Var = new extMod.m1.m2./*10*/c();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "2", "namespace m1", "NamespaceComment");
		f->VerifyQuickInfoAt(t, "3", R"TS(import extMod = require("./commentsImportDeclaration_file0"))TS", "Import declaration");
		f->VerifyCompletions(t, "6", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "m1", .Detail = std::string("namespace extMod.m1"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "NamespaceComment"})})})}})}));
		f->VerifyCompletions(t, "7", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b", .Detail = std::string("var extMod.m1.b: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "b's comment"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooExport", .Detail = std::string("function extMod.m1.fooExport(): number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "exported function"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "m2", .Detail = std::string("namespace extMod.m1.m2"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "m2 comments"})})})}})}));
		f->GoToMarker(t, "8");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "exported function"});
		f->VerifyQuickInfoAt(t, "8q", "function extMod.m1.fooExport(): number", "exported function");
		f->VerifyQuickInfoAt(t, "9", "var newVar: extMod.m1.m2.c", "");
		f->VerifyCompletions(t, "10", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c", .Detail = std::string("constructor extMod.m1.m2.c(): extMod.m1.m2.c"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "class comment;"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i", .Detail = std::string("var extMod.m1.m2.i: extMod.m1.m2.c"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i"})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCommentsImportDeclaration, TestCommentsImportDeclaration);

// commentsInheritanceFourslash_test.go

// commentsInheritanceFourslash_test.go
static void TestCommentsInheritanceFourslash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** i1 is interface with properties*/
interface i1 {
    /** i1_p1*/
    i1_p1: number;
    /** i1_f1*/
    i1_f1(): void;
    /** i1_l1*/
    i1_l1: () => void;
    i1_nc_p1: number;
    i1_nc_f1(): void;
    i1_nc_l1: () => void;
    p1: number;
    f1(): void;
    l1: () => void;
    nc_p1: number;
    nc_f1(): void;
    nc_l1: () => void;
}
class c1 implements i1 {
    public i1_p1: number;
    public i1_f1() {
    }
    public i1_l1: () => void;
    public i1_nc_p1: number;
    public i1_nc_f1() {
    }
    public i1_nc_l1: () => void;
    /** c1_p1*/
    public p1: number;
    /** c1_f1*/
    public f1() {
    }
    /** c1_l1*/
    public l1: () => void;
    /** c1_nc_p1*/
    public nc_p1: number;
    /** c1_nc_f1*/
    public nc_f1() {
    }
    /** c1_nc_l1*/
    public nc_l1: () => void;
}
var i1/*1iq*/_i: /*16i*/i1;
i1_i./*1*/i/*2q*/1_f1(/*2*/);
i1_i.i1_n/*3q*/c_f1(/*3*/);
i1_i.f/*4q*/1(/*4*/);
i1_i.nc/*5q*/_f1(/*5*/);
i1_i.i1/*l2q*/_l1(/*l2*/);
i1_i.i1_/*l3q*/nc_l1(/*l3*/);
i1_i.l/*l4q*/1(/*l4*/);
i1_i.nc/*l5q*/_l1(/*l5*/);
var c1/*6iq*/_i = new c1();
c1_i./*6*/i1/*7q*/_f1(/*7*/);
c1_i.i1_nc/*8q*/_f1(/*8*/);
c1_i.f/*9q*/1(/*9*/);
c1_i.nc/*10q*/_f1(/*10*/);
c1_i.i1/*l7q*/_l1(/*l7*/);
c1_i.i1_n/*l8q*/c_l1(/*l8*/);
c1_i.l/*l9q*/1(/*l9*/);
c1_i.nc/*l10q*/_l1(/*l10*/);
// assign to interface
i1_i = c1_i;
i1_i./*11*/i1/*12q*/_f1(/*12*/);
i1_i.i1_nc/*13q*/_f1(/*13*/);
i1_i.f/*14q*/1(/*14*/);
i1_i.nc/*15q*/_f1(/*15*/);
i1_i.i1/*l12q*/_l1(/*l12*/);
i1_i.i1/*l13q*/_nc_l1(/*l13*/);
i1_i.l/*l14q*/1(/*l14*/);
i1_i.nc/*l15q*/_l1(/*l15*/);
/*16*/
class c2 {
    /** c2 c2_p1*/
    public c2_p1: number;
    /** c2 c2_f1*/
    public c2_f1() {
    }
    /** c2 c2_prop*/
    public get c2_prop() {
        return 10;
    }
    public c2_nc_p1: number;
    public c2_nc_f1() {
    }
    public get c2_nc_prop() {
        return 10;
    }
    /** c2 p1*/
    public p1: number;
    /** c2 f1*/
    public f1() {
    }
    /** c2 prop*/
    public get prop() {
        return 10;
    }
    public nc_p1: number;
    public nc_f1() {
    }
    public get nc_prop() {
        return 10;
    }
    /** c2 constructor*/
    constr/*55*/uctor(a: number) {
        this.c2_p1 = a;
    }
}
class c3 extends c2 {
    cons/*56*/tructor() {
        su/*18sq*/per(10);
        this.p1 = s/*18spropq*/uper./*18spropProp*/c2_p1;
    }
    /** c3 p1*/
    public p1: number;
    /** c3 f1*/
    public f1() {
    }
    /** c3 prop*/
    public get prop() {
        return 10;
    }
    public nc_p1: number;
    public nc_f1() {
    }
    public get nc_prop() {
        return 10;
    }
}
var c/*17iq*/2_i = new c/*17q*/2(/*17*/10);
var c/*18iq*/3_i = new c/*18q*/3(/*18*/);
c2_i./*19*/c2/*20q*/_f1(/*20*/);
c2_i.c2_nc/*21q*/_f1(/*21*/);
c2_i.f/*22q*/1(/*22*/);
c2_i.nc/*23q*/_f1(/*23*/);
c3_i./*24*/c2/*25q*/_f1(/*25*/);
c3_i.c2_nc/*26q*/_f1(/*26*/);
c3_i.f/*27q*/1(/*27*/);
c3_i.nc/*28q*/_f1(/*28*/);
// assign
c2_i = c3_i;
c2_i./*29*/c2/*30q*/_f1(/*30*/);
c2_i.c2_nc_/*31q*/f1(/*31*/);
c2_i.f/*32q*/1(/*32*/);
c2_i.nc/*33q*/_f1(/*33*/);
class c4 extends c2 {
}
var c4/*34iq*/_i = new c/*34q*/4(/*34*/10);
/*35*/
interface i2 {
    /** i2_p1*/
    i2_p1: number;
    /** i2_f1*/
    i2_f1(): void;
    /** i2_l1*/
    i2_l1: () => void;
    i2_nc_p1: number;
    i2_nc_f1(): void;
    i2_nc_l1: () => void;
    /** i2 p1*/
    p1: number;
    /** i2 f1*/
    f1(): void;
    /** i2 l1*/
    l1: () => void;
    nc_p1: number;
    nc_f1(): void;
    nc_l1: () => void;
}
interface i3 extends i2 {
    /** i3 p1*/
    p1: number;
    /** i3 f1*/
    f1(): void;
    /** i3 l1*/
    l1: () => void;
    nc_p1: number;
    nc_f1(): void;
    nc_l1: () => void;
}
var i2/*36iq*/_i: /*51i*/i2;
var i3/*37iq*/_i: i3;
i2_i./*36*/i2/*37q*/_f1(/*37*/);
i2_i.i2_n/*38q*/c_f1(/*38*/);
i2_i.f/*39q*/1(/*39*/);
i2_i.nc/*40q*/_f1(/*40*/);
i2_i.i2_/*l37q*/l1(/*l37*/);
i2_i.i2_nc/*l38q*/_l1(/*l38*/);
i2_i.l/*l39q*/1(/*l39*/);
i2_i.nc_/*l40q*/l1(/*l40*/);
i3_i./*41*/i2_/*42q*/f1(/*42*/);
i3_i.i2_nc/*43q*/_f1(/*43*/);
i3_i.f/*44q*/1(/*44*/);
i3_i.nc_/*45q*/f1(/*45*/);
i3_i.i2_/*l42q*/l1(/*l42*/);
i3_i.i2_nc/*l43q*/_l1(/*l43*/);
i3_i.l/*l44q*/1(/*l44*/);
i3_i.nc_/*l45q*/l1(/*l45*/);
// assign to interface
i2_i = i3_i;
i2_i./*46*/i2/*47q*/_f1(/*47*/);
i2_i.i2_nc_/*48q*/f1(/*48*/);
i2_i.f/*49q*/1(/*49*/);
i2_i.nc/*50q*/_f1(/*50*/);
i2_i.i2_/*l47q*/l1(/*l47*/);
i2_i.i2_nc/*l48q*/_l1(/*l48*/);
i2_i.l/*l49q*/1(/*l49*/);
i2_i.nc_/*l50q*/l1(/*l50*/);
/*51*/
/**c5 class*/
class c5 {
    public b: number;
}
class c6 extends c5 {
    public d;
    const/*57*/ructor() {
        /*52*/super();
        this.d = /*53*/super./*54*/b;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"1", "11"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_p1", .Detail = std::string("(property) i1.i1_p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i1_p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_f1", .Detail = std::string("(method) i1.i1_f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i1_f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_l1", .Detail = std::string("(property) i1.i1_l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i1_l1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_nc_p1", .Detail = std::string("(property) i1.i1_nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_nc_f1", .Detail = std::string("(method) i1.i1_nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_nc_l1", .Detail = std::string("(property) i1.i1_nc_l1: () => void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "p1", .Detail = std::string("(property) i1.p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f1", .Detail = std::string("(method) i1.f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "l1", .Detail = std::string("(property) i1.l1: () => void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_p1", .Detail = std::string("(property) i1.nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_f1", .Detail = std::string("(method) i1.nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_l1", .Detail = std::string("(property) i1.nc_l1: () => void")})}})}));
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "i1_f1"});
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "4");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "5");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l4");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l5");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "1iq", "var i1_i: i1", "");
		f->VerifyQuickInfoAt(t, "2q", "(method) i1.i1_f1(): void", "i1_f1");
		f->VerifyQuickInfoAt(t, "3q", "(method) i1.i1_nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "4q", "(method) i1.f1(): void", "");
		f->VerifyQuickInfoAt(t, "5q", "(method) i1.nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "l2q", "(property) i1.i1_l1: () => void", "i1_l1");
		f->VerifyQuickInfoAt(t, "l3q", "(property) i1.i1_nc_l1: () => void", "");
		f->VerifyQuickInfoAt(t, "l4q", "(property) i1.l1: () => void", "");
		f->VerifyQuickInfoAt(t, "l5q", "(property) i1.nc_l1: () => void", "");
		f->VerifyCompletions(t, "6", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_p1", .Detail = std::string("(property) c1.i1_p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i1_p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_f1", .Detail = std::string("(method) c1.i1_f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i1_f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_l1", .Detail = std::string("(property) c1.i1_l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i1_l1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_nc_p1", .Detail = std::string("(property) c1.i1_nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_nc_f1", .Detail = std::string("(method) c1.i1_nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_nc_l1", .Detail = std::string("(property) c1.i1_nc_l1: () => void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "p1", .Detail = std::string("(property) c1.p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c1_p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f1", .Detail = std::string("(method) c1.f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c1_f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "l1", .Detail = std::string("(property) c1.l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c1_l1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_p1", .Detail = std::string("(property) c1.nc_p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c1_nc_p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_f1", .Detail = std::string("(method) c1.nc_f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c1_nc_f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_l1", .Detail = std::string("(property) c1.nc_l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c1_nc_l1"})})})}})}));
		f->GoToMarker(t, "7");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "i1_f1"});
		f->GoToMarker(t, "9");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c1_f1"});
		f->GoToMarker(t, "10");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c1_nc_f1"});
		f->GoToMarker(t, "l9");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c1_l1"});
		f->GoToMarker(t, "l10");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c1_nc_l1"});
		f->GoToMarker(t, "8");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l7");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l8");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "6iq", "var c1_i: c1", "");
		f->VerifyQuickInfoAt(t, "7q", "(method) c1.i1_f1(): void", "i1_f1");
		f->VerifyQuickInfoAt(t, "8q", "(method) c1.i1_nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "9q", "(method) c1.f1(): void", "c1_f1");
		f->VerifyQuickInfoAt(t, "10q", "(method) c1.nc_f1(): void", "c1_nc_f1");
		f->VerifyQuickInfoAt(t, "l7q", "(property) c1.i1_l1: () => void", "i1_l1");
		f->VerifyQuickInfoAt(t, "l8q", "(property) c1.i1_nc_l1: () => void", "");
		f->VerifyQuickInfoAt(t, "l9q", "(property) c1.l1: () => void", "c1_l1");
		f->VerifyQuickInfoAt(t, "l10q", "(property) c1.nc_l1: () => void", "c1_nc_l1");
		f->VerifyCompletions(t, "11", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_p1", .Detail = std::string("(property) i1.i1_p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i1_p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_f1", .Detail = std::string("(method) i1.i1_f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i1_f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_l1", .Detail = std::string("(property) i1.i1_l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i1_l1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_nc_p1", .Detail = std::string("(property) i1.i1_nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_nc_f1", .Detail = std::string("(method) i1.i1_nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_nc_l1", .Detail = std::string("(property) i1.i1_nc_l1: () => void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "p1", .Detail = std::string("(property) i1.p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f1", .Detail = std::string("(method) i1.f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "l1", .Detail = std::string("(property) i1.l1: () => void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_p1", .Detail = std::string("(property) i1.nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_f1", .Detail = std::string("(method) i1.nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_l1", .Detail = std::string("(property) i1.nc_l1: () => void")})}})}));
		f->GoToMarker(t, "12");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "i1_f1"});
		f->GoToMarker(t, "13");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "14");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "15");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l12");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l13");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l14");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l15");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "12q", "(method) i1.i1_f1(): void", "i1_f1");
		f->VerifyQuickInfoAt(t, "13q", "(method) i1.i1_nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "14q", "(method) i1.f1(): void", "");
		f->VerifyQuickInfoAt(t, "15q", "(method) i1.nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "l12q", "(property) i1.i1_l1: () => void", "i1_l1");
		f->VerifyQuickInfoAt(t, "l13q", "(property) i1.i1_nc_l1: () => void", "");
		f->VerifyQuickInfoAt(t, "l14q", "(property) i1.l1: () => void", "");
		f->VerifyQuickInfoAt(t, "l15q", "(property) i1.nc_l1: () => void", "");
		f->VerifyCompletions(t, "16", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_i", .Detail = std::string("var i1_i: i1")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c1", .Detail = std::string("class c1")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c1_i", .Detail = std::string("var c1_i: c1")})}, .Excludes = std::vector<std::string>{"i1"}})}));
		f->VerifyCompletions(t, "16i", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1", .Detail = std::string("interface i1"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i1 is interface with properties"})})})}})}));
		f->VerifyQuickInfoAt(t, "17iq", "var c2_i: c2", "");
		f->VerifyQuickInfoAt(t, "18iq", "var c3_i: c3", "");
		f->GoToMarker(t, "17");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c2 constructor"});
		f->GoToMarker(t, "18");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "18sq", "constructor c2(a: number): c2", "c2 constructor");
		f->VerifyQuickInfoAt(t, "18spropq", "class c2", "");
		f->VerifyQuickInfoAt(t, "18spropProp", "(property) c2.c2_p1: number", "c2 c2_p1");
		f->VerifyQuickInfoAt(t, "17q", "constructor c2(a: number): c2", "c2 constructor");
		f->VerifyQuickInfoAt(t, "18q", "constructor c3(): c3", "");
		f->VerifyCompletions(t, std::vector<std::string>{"19", "29"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_p1", .Detail = std::string("(property) c2.c2_p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c2 c2_p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_f1", .Detail = std::string("(method) c2.c2_f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c2 c2_f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_prop", .Detail = std::string("(property) c2.c2_prop: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c2 c2_prop"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_nc_p1", .Detail = std::string("(property) c2.c2_nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_nc_f1", .Detail = std::string("(method) c2.c2_nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_nc_prop", .Detail = std::string("(property) c2.c2_nc_prop: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "p1", .Detail = std::string("(property) c2.p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c2 p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f1", .Detail = std::string("(method) c2.f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c2 f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "prop", .Detail = std::string("(property) c2.prop: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c2 prop"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_p1", .Detail = std::string("(property) c2.nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_f1", .Detail = std::string("(method) c2.nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_prop", .Detail = std::string("(property) c2.nc_prop: number")})}})}));
		f->GoToMarker(t, "20");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c2 c2_f1"});
		f->GoToMarker(t, "22");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c2 f1"});
		f->GoToMarker(t, "21");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "23");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "20q", "(method) c2.c2_f1(): void", "c2 c2_f1");
		f->VerifyQuickInfoAt(t, "21q", "(method) c2.c2_nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "22q", "(method) c2.f1(): void", "c2 f1");
		f->VerifyQuickInfoAt(t, "23q", "(method) c2.nc_f1(): void", "");
		f->VerifyCompletions(t, "24", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_p1", .Detail = std::string("(property) c2.c2_p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c2 c2_p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_f1", .Detail = std::string("(method) c2.c2_f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c2 c2_f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_prop", .Detail = std::string("(property) c2.c2_prop: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c2 c2_prop"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_nc_p1", .Detail = std::string("(property) c2.c2_nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_nc_f1", .Detail = std::string("(method) c2.c2_nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_nc_prop", .Detail = std::string("(property) c2.c2_nc_prop: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "p1", .Detail = std::string("(property) c3.p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c3 p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f1", .Detail = std::string("(method) c3.f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c3 f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "prop", .Detail = std::string("(property) c3.prop: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "c3 prop"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_p1", .Detail = std::string("(property) c3.nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_f1", .Detail = std::string("(method) c3.nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_prop", .Detail = std::string("(property) c3.nc_prop: number")})}})}));
		f->GoToMarker(t, "25");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c2 c2_f1"});
		f->GoToMarker(t, "27");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c3 f1"});
		f->GoToMarker(t, "26");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "28");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "25q", "(method) c2.c2_f1(): void", "c2 c2_f1");
		f->VerifyQuickInfoAt(t, "26q", "(method) c2.c2_nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "27q", "(method) c3.f1(): void", "c3 f1");
		f->VerifyQuickInfoAt(t, "28q", "(method) c3.nc_f1(): void", "");
		f->GoToMarker(t, "30");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c2 c2_f1"});
		f->GoToMarker(t, "32");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c2 f1"});
		f->GoToMarker(t, "31");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "33");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "30q", "(method) c2.c2_f1(): void", "c2 c2_f1");
		f->VerifyQuickInfoAt(t, "31q", "(method) c2.c2_nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "32q", "(method) c2.f1(): void", "c2 f1");
		f->VerifyQuickInfoAt(t, "33q", "(method) c2.nc_f1(): void", "");
		f->GoToMarker(t, "34");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c2 constructor"});
		f->VerifyQuickInfoAt(t, "34iq", "var c4_i: c4", "");
		f->VerifyQuickInfoAt(t, "34q", "constructor c4(a: number): c4", "c2 constructor");
		f->VerifyCompletions(t, "35", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2", .Detail = std::string("class c2")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_i", .Detail = std::string("var c2_i: c2")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c3", .Detail = std::string("class c3")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c3_i", .Detail = std::string("var c3_i: c3")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c4", .Detail = std::string("class c4")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c4_i", .Detail = std::string("var c4_i: c4")})}})}));
		f->VerifyCompletions(t, std::vector<std::string>{"36", "46"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_p1", .Detail = std::string("(property) i2.i2_p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2_p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_f1", .Detail = std::string("(method) i2.i2_f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2_f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_l1", .Detail = std::string("(property) i2.i2_l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2_l1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_nc_p1", .Detail = std::string("(property) i2.i2_nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_nc_f1", .Detail = std::string("(method) i2.i2_nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_nc_l1", .Detail = std::string("(property) i2.i2_nc_l1: () => void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "p1", .Detail = std::string("(property) i2.p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2 p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f1", .Detail = std::string("(method) i2.f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2 f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "l1", .Detail = std::string("(property) i2.l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2 l1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_p1", .Detail = std::string("(property) i2.nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_f1", .Detail = std::string("(method) i2.nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_l1", .Detail = std::string("(property) i2.nc_l1: () => void")})}})}));
		f->GoToMarker(t, "37");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "i2_f1"});
		f->GoToMarker(t, "39");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "i2 f1"});
		f->GoToMarker(t, "38");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "40");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l37");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l37");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l39");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l40");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "36iq", "var i2_i: i2", "");
		f->VerifyQuickInfoAt(t, "37iq", "var i3_i: i3", "");
		f->VerifyQuickInfoAt(t, "37q", "(method) i2.i2_f1(): void", "i2_f1");
		f->VerifyQuickInfoAt(t, "38q", "(method) i2.i2_nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "39q", "(method) i2.f1(): void", "i2 f1");
		f->VerifyQuickInfoAt(t, "40q", "(method) i2.nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "l37q", "(property) i2.i2_l1: () => void", "i2_l1");
		f->VerifyQuickInfoAt(t, "l38q", "(property) i2.i2_nc_l1: () => void", "");
		f->VerifyQuickInfoAt(t, "l39q", "(property) i2.l1: () => void", "i2 l1");
		f->VerifyQuickInfoAt(t, "l40q", "(property) i2.nc_l1: () => void", "");
		f->VerifyCompletions(t, "41", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_p1", .Detail = std::string("(property) i2.i2_p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2_p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_f1", .Detail = std::string("(method) i2.i2_f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2_f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_l1", .Detail = std::string("(property) i2.i2_l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2_l1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_nc_p1", .Detail = std::string("(property) i2.i2_nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_nc_f1", .Detail = std::string("(method) i2.i2_nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_nc_l1", .Detail = std::string("(property) i2.i2_nc_l1: () => void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "p1", .Detail = std::string("(property) i3.p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i3 p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f1", .Detail = std::string("(method) i3.f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i3 f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "l1", .Detail = std::string("(property) i3.l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i3 l1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_p1", .Detail = std::string("(property) i3.nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_f1", .Detail = std::string("(method) i3.nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_l1", .Detail = std::string("(property) i3.nc_l1: () => void")})}})}));
		f->GoToMarker(t, "42");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "i2_f1"});
		f->GoToMarker(t, "44");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "i3 f1"});
		f->GoToMarker(t, "43");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "45");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l42");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l43");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l44");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l45");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "42q", "(method) i2.i2_f1(): void", "i2_f1");
		f->VerifyQuickInfoAt(t, "43q", "(method) i2.i2_nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "44q", "(method) i3.f1(): void", "i3 f1");
		f->VerifyQuickInfoAt(t, "45q", "(method) i3.nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "l42q", "(property) i2.i2_l1: () => void", "i2_l1");
		f->VerifyQuickInfoAt(t, "l43q", "(property) i2.i2_nc_l1: () => void", "");
		f->VerifyQuickInfoAt(t, "l44q", "(property) i3.l1: () => void", "i3 l1");
		f->VerifyQuickInfoAt(t, "l45q", "(property) i3.nc_l1: () => void", "");
		f->VerifyCompletions(t, "46", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_p1", .Detail = std::string("(property) i2.i2_p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2_p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_f1", .Detail = std::string("(method) i2.i2_f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2_f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_l1", .Detail = std::string("(property) i2.i2_l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2_l1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_nc_p1", .Detail = std::string("(property) i2.i2_nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_nc_f1", .Detail = std::string("(method) i2.i2_nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_nc_l1", .Detail = std::string("(property) i2.i2_nc_l1: () => void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "p1", .Detail = std::string("(property) i2.p1: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2 p1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f1", .Detail = std::string("(method) i2.f1(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2 f1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "l1", .Detail = std::string("(property) i2.l1: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i2 l1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_p1", .Detail = std::string("(property) i2.nc_p1: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_f1", .Detail = std::string("(method) i2.nc_f1(): void")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_l1", .Detail = std::string("(property) i2.nc_l1: () => void")})}})}));
		f->GoToMarker(t, "47");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "i2_f1"});
		f->GoToMarker(t, "49");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "i2 f1"});
		f->GoToMarker(t, "48");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l47");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l48");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l49");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->GoToMarker(t, "l50");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "47q", "(method) i2.i2_f1(): void", "i2_f1");
		f->VerifyQuickInfoAt(t, "48q", "(method) i2.i2_nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "49q", "(method) i2.f1(): void", "i2 f1");
		f->VerifyQuickInfoAt(t, "50q", "(method) i2.nc_f1(): void", "");
		f->VerifyQuickInfoAt(t, "l47q", "(property) i2.i2_l1: () => void", "i2_l1");
		f->VerifyQuickInfoAt(t, "l48q", "(property) i2.i2_nc_l1: () => void", "");
		f->VerifyQuickInfoAt(t, "l49q", "(property) i2.l1: () => void", "i2 l1");
		f->VerifyQuickInfoAt(t, "l40q", "(property) i2.nc_l1: () => void", "");
		f->VerifyCompletions(t, "51", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i", .Detail = std::string("var i2_i: i2")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i3_i", .Detail = std::string("var i3_i: i3")})}, .Excludes = std::vector<std::string>{"i2", "i3"}})}));
		f->VerifyCompletions(t, "51i", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2", .Detail = std::string("interface i2")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i3", .Detail = std::string("interface i3")})}})}));
		f->VerifyQuickInfoAt(t, "52", "constructor c5(): c5", "c5 class");
		f->VerifyQuickInfoAt(t, "53", "class c5", "c5 class");
		f->VerifyQuickInfoAt(t, "54", "(property) c5.b: number", "");
		f->VerifyQuickInfoAt(t, "55", "constructor c2(a: number): c2", "c2 constructor");
		f->VerifyQuickInfoAt(t, "56", "constructor c3(): c3", "");
		f->VerifyQuickInfoAt(t, "57", "constructor c6(): c6", "");
	});
}
REGISTER_FOURSLASH_TEST(TestCommentsInheritanceFourslash, TestCommentsInheritanceFourslash);

// commentsInterfaceFourslash_test.go

// commentsInterfaceFourslash_test.go
static void TestCommentsInterfaceFourslash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
/** this is interface 1*/
interface i/*1*/1 {
}
var i1/*2*/_i: i1;
interface nc_/*3*/i1 {
}
var nc_/*4*/i1_i: nc_i1;
/** this is interface 2 with members*/
interface i/*5*/2 {
    /** this is x*/
    x: number;
    /** this is foo*/
    foo: (/**param help*/b: number) => string;
    /** this is indexer*/
    [/**string param*/i: string]: number;
    /**new method*/
    new (/** param*/i: i1);
    nc_x: number;
    nc_foo: (b: number) => string;
    [i: number]: number;
    /** this is call signature*/
    (/**paramhelp a*/a: number,/**paramhelp b*/ b: number) : number;
    /** this is fnfoo*/
    fnfoo(/**param help*/b: number): string;
    nc_fnfoo(b: number): string;
}
var i2/*6*/_i: /*34i*/i2;
var i2_i/*7*/_x = i2_i./*8*/x;
var i2_i/*9*/_foo = i2_i.f/*10*/oo;
var i2_i_f/*11*/oo_r = i2_i.f/*12q*/oo(/*12*/30);
var i2_i_i2_/*13*/si = i2/*13q*/_i["hello"];
var i2_i_i2/*14*/_ii = i2/*14q*/_i[30];
var i2_/*15*/i_n = new i2/*16q*/_i(/*16*/i1_i);
var i2_i/*17*/_nc_x = i2_i.n/*18*/c_x;
var i2_i_/*19*/nc_foo = i2_i.n/*20*/c_foo;
var i2_i_nc_f/*21*/oo_r = i2_i.nc/*22q*/_foo(/*22*/30);
var i2/*23*/_i_r = i2/*24q*/_i(/*24*/10, /*25*/20);
var i2_i/*26*/_fnfoo = i2_i.fn/*27*/foo;
var i2_i_/*28*/fnfoo_r = i2_i.fn/*29q*/foo(/*29*/10);
var i2_i/*30*/_nc_fnfoo = i2_i.nc_fn/*31*/foo;
var i2_i_nc_/*32*/fnfoo_r = i2_i.nc/*33q*/_fnfoo(/*33*/10);
/*34*/
interface i3 {
    /** Comment i3 x*/
    x: number;
    /** Function i3 f*/
    f(/**number parameter*/a: number): string;
    /** i3 l*/
    l: (/**comment i3 l b*/b: number) => string;
    nc_x: number;
    nc_f(a: number): string;
    nc_l: (b: number) => string;
}
var i3_i: i3;
i3_i = {
    /*35*/f: /**own f*/ (/**i3_i a*/a: number) => "Hello" + /*36*/a,
    l: this./*37*/f,
    /** own x*/
    x: this.f(/*38*/10),
    nc_x: this.l(/*39*/this.x),
    nc_f: this.f,
    nc_l: this.l
};
/*40*/i/*40q*/3_i./*41*/f(/*42*/10);
i3_i./*43q*/l(/*43*/10);
i3_i.nc_/*44q*/f(/*44*/10);
i3_i.nc/*45q*/_l(/*45*/10);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "interface i1", "this is interface 1");
		f->VerifyQuickInfoAt(t, "2", "var i1_i: i1", "");
		f->VerifyQuickInfoAt(t, "3", "interface nc_i1", "");
		f->VerifyQuickInfoAt(t, "4", "var nc_i1_i: nc_i1", "");
		f->VerifyQuickInfoAt(t, "5", "interface i2", "this is interface 2 with members");
		f->VerifyQuickInfoAt(t, "6", "var i2_i: i2", "");
		f->VerifyQuickInfoAt(t, "7", "var i2_i_x: number", "");
		f->VerifyQuickInfoAt(t, "8", "(property) i2.x: number", "this is x");
		f->VerifyCompletions(t, "8", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionFunctionMembersWithPrototypePlus(std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .Detail = std::string("(property) i2.x: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "this is x"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .Detail = std::string("(property) i2.foo: (b: number) => string"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "this is foo"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_x", .Detail = std::string("(property) i2.nc_x: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_foo", .Detail = std::string("(property) i2.nc_foo: (b: number) => string")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fnfoo", .Detail = std::string("(method) i2.fnfoo(b: number): string"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "this is fnfoo"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_fnfoo", .Detail = std::string("(method) i2.nc_fnfoo(b: number): string")})})})}));
		f->VerifyQuickInfoAt(t, "9", "var i2_i_foo: (b: number) => string", "");
		f->VerifyQuickInfoAt(t, "10", "(property) i2.foo: (b: number) => string", "this is foo");
		f->VerifyQuickInfoAt(t, "11", "var i2_i_foo_r: string", "");
		f->GoToMarker(t, "12");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "", .ParameterDocComment = "param help"});
		f->VerifyQuickInfoAt(t, "12q", "(property) i2.foo: (b: number) => string", "this is foo");
		f->VerifyQuickInfoAt(t, "13", "var i2_i_i2_si: number", "");
		f->VerifyQuickInfoAt(t, "13q", "var i2_i: i2", "");
		f->VerifyQuickInfoAt(t, "14", "var i2_i_i2_ii: number", "");
		f->VerifyQuickInfoAt(t, "14q", "var i2_i: i2", "");
		f->VerifyQuickInfoAt(t, "15", "var i2_i_n: any", "");
		f->GoToMarker(t, "16");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "new method", .ParameterDocComment = "param"});
		f->VerifyQuickInfoAt(t, "16q", R"TS(var i2_i: i2
new (i: i1) => any)TS", "new method");
		f->VerifyQuickInfoAt(t, "17", "var i2_i_nc_x: number", "");
		f->VerifyQuickInfoAt(t, "18", "(property) i2.nc_x: number", "");
		f->VerifyQuickInfoAt(t, "19", "var i2_i_nc_foo: (b: number) => string", "");
		f->VerifyQuickInfoAt(t, "20", "(property) i2.nc_foo: (b: number) => string", "");
		f->VerifyQuickInfoAt(t, "21", "var i2_i_nc_foo_r: string", "");
		f->GoToMarker(t, "22");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "22q", "(property) i2.nc_foo: (b: number) => string", "");
		f->VerifyQuickInfoAt(t, "23", "var i2_i_r: number", "");
		f->GoToMarker(t, "24");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this is call signature", .ParameterDocComment = "paramhelp a"});
		f->VerifyQuickInfoAt(t, "24q", R"TS(var i2_i: i2
(a: number, b: number) => number)TS", "this is call signature");
		f->GoToMarker(t, "25");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this is call signature", .ParameterDocComment = "paramhelp b"});
		f->VerifyQuickInfoAt(t, "26", "var i2_i_fnfoo: (b: number) => string", "");
		f->VerifyQuickInfoAt(t, "27", "(method) i2.fnfoo(b: number): string", "this is fnfoo");
		f->VerifyQuickInfoAt(t, "28", "var i2_i_fnfoo_r: string", "");
		f->GoToMarker(t, "29");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this is fnfoo", .ParameterDocComment = "param help"});
		f->VerifyQuickInfoAt(t, "29q", "(method) i2.fnfoo(b: number): string", "this is fnfoo");
		f->VerifyQuickInfoAt(t, "30", "var i2_i_nc_fnfoo: (b: number) => string", "");
		f->VerifyQuickInfoAt(t, "31", "(method) i2.nc_fnfoo(b: number): string", "");
		f->VerifyQuickInfoAt(t, "32", "var i2_i_nc_fnfoo_r: string", "");
		f->GoToMarker(t, "33");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "33q", "(method) i2.nc_fnfoo(b: number): string", "");
		f->VerifyCompletions(t, "34", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_i", .Detail = std::string("var i1_i: i1")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_i1_i", .Detail = std::string("var nc_i1_i: nc_i1"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = ""})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i", .Detail = std::string("var i2_i: i2")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_x", .Detail = std::string("var i2_i_x: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_foo", .Detail = std::string("var i2_i_foo: (b: number) => string")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_foo_r", .Detail = std::string("var i2_i_foo_r: string")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_i2_si", .Detail = std::string("var i2_i_i2_si: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_i2_ii", .Detail = std::string("var i2_i_i2_ii: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_n", .Detail = std::string("var i2_i_n: any")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_nc_x", .Detail = std::string("var i2_i_nc_x: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_nc_foo", .Detail = std::string("var i2_i_nc_foo: (b: number) => string")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_nc_foo_r", .Detail = std::string("var i2_i_nc_foo_r: string")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_r", .Detail = std::string("var i2_i_r: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_fnfoo", .Detail = std::string("var i2_i_fnfoo: (b: number) => string")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_fnfoo_r", .Detail = std::string("var i2_i_fnfoo_r: string")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_nc_fnfoo", .Detail = std::string("var i2_i_nc_fnfoo: (b: number) => string")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i_nc_fnfoo_r", .Detail = std::string("var i2_i_nc_fnfoo_r: string")})}, .Excludes = std::vector<std::string>{"i1", "nc_i1", "i2"}})}));
		f->VerifyCompletions(t, "34i", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1", .Detail = std::string("interface i1"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "this is interface 1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_i1", .Detail = std::string("interface nc_i1")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2", .Detail = std::string("interface i2"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "this is interface 2 with members"})})})}})}));
		f->VerifyCompletions(t, "36", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "a", .Detail = std::string("(parameter) a: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i3_i a"})})})}})}));
		f->VerifyQuickInfoAt(t, "40q", "var i3_i: i3", "");
		f->VerifyCompletions(t, "40", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i3_i", .Detail = std::string("var i3_i: i3")})}, .Excludes = std::vector<std::string>{"i3"}})}));
		f->GoToMarker(t, "41");
		f->VerifyQuickInfoIs(t, "(method) i3.f(a: number): string", "Function i3 f");
		f->VerifyCompletions(t, "41", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f", .Detail = std::string("(method) i3.f(a: number): string"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "Function i3 f"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "l", .Detail = std::string("(property) i3.l: (b: number) => string"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "i3 l"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_f", .Detail = std::string("(method) i3.nc_f(a: number): string")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_l", .Detail = std::string("(property) i3.nc_l: (b: number) => string")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nc_x", .Detail = std::string("(property) i3.nc_x: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .Detail = std::string("(property) i3.x: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "Comment i3 x"})})})}})}));
		f->GoToMarker(t, "42");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "Function i3 f", .ParameterDocComment = "number parameter"});
		f->GoToMarker(t, "43");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "", .ParameterDocComment = "comment i3 l b"});
		f->VerifyQuickInfoAt(t, "43q", "(property) i3.l: (b: number) => string", "i3 l");
		f->GoToMarker(t, "44");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "44q", "(method) i3.nc_f(a: number): string", "");
		f->GoToMarker(t, "45");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = ""});
		f->VerifyQuickInfoAt(t, "45q", "(property) i3.nc_l: (b: number) => string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestCommentsInterfaceFourslash, TestCommentsInterfaceFourslash);

// commentsLinePreservation_test.go

// commentsLinePreservation_test.go
static void TestCommentsLinePreservation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** This is firstLine
  * This is second Line
  * 
  * This is fourth Line
  */
var /*a*/a: string;
/** 
  * This is firstLine
  * This is second Line
  * 
  * This is fourth Line
  */
var /*b*/b: string;
/** 
  * This is firstLine
  * This is second Line
  * 
  * This is fourth Line
  *
  */
var /*c*/c: string;
/** 
  * This is firstLine
  * This is second Line
  * @param param
  * @random tag This should be third line
  */
function /*d*/d(param: string) { /*1*/param = "hello"; }
/** 
  * This is firstLine
  * This is second Line
  * @param param
  */
function /*e*/e(param: string) { /*2*/param = "hello"; }
/** 
  * This is firstLine
  * This is second Line
  * @param param1 first line of param
  *
  *  param information third line
  * @random tag This should be third line
  */
function /*f*/f(param1: string) { /*3*/param1 = "hello"; }
/** 
  * This is firstLine
  * This is second Line
  * @param param1
  *
  *  param information first line
  * @random tag This should be third line
  */
function /*g*/g(param1: string) { /*4*/param1 = "hello"; }
/** 
  * This is firstLine
  * This is second Line
  * @param param1
  *
  *  param information first line
  *
  *  param information third line
  * @random tag This should be third line
  */
function /*h*/h(param1: string) { /*5*/param1 = "hello"; }
/** 
  * This is firstLine
  * This is second Line
  * @param param1
  *
  *  param information first line
  *
  *  param information third line
  *
  */
function /*i*/i(param1: string) { /*6*/param1 = "hello"; }
/** 
  * This is firstLine
  * This is second Line
  * @param param1
  *
  *  param information first line
  *
  *  param information third line
  */
function /*j*/j(param1: string) { /*7*/param1 = "hello"; }
/** 
  * This is firstLine
  * This is second Line
  * @param param1 hello   @randomtag 
  *
  *  random information first line
  *
  *  random information third line
  */
function /*k*/k(param1: string) { /*8*/param1 = "hello"; }
/** 
  * This is firstLine
  * This is second Line
  * @param param1 first Line text
  *
  * @param param1 
  *
  * blank line that shouldnt be shown when starting this 
  * second time information about the param again
  */
function /*l*/l(param1: string) { /*9*/param1 = "hello"; }
     /** 
       * This is firstLine
 This is second Line
 [1]: third * line
 @param param1 first Line text
 second line text
 */
function /*m*/m(param1: string) { /*10*/param1 = "hello"; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "a", "var a: string", R"TS(This is firstLine
This is second Line

This is fourth Line)TS");
		f->VerifyQuickInfoAt(t, "b", "var b: string", R"TS(This is firstLine
This is second Line

This is fourth Line)TS");
		f->VerifyQuickInfoAt(t, "c", "var c: string", R"TS(This is firstLine
This is second Line

This is fourth Line)TS");
		f->VerifyQuickInfoAt(t, "d", "function d(param: string): void", R"TS(This is firstLine
This is second Line)TS");
		f->VerifyQuickInfoAt(t, "1", "(parameter) param: string", "");
		f->VerifyQuickInfoAt(t, "e", "function e(param: string): void", R"TS(This is firstLine
This is second Line)TS");
		f->VerifyQuickInfoAt(t, "2", "(parameter) param: string", "");
		f->VerifyQuickInfoAt(t, "f", "function f(param1: string): void", R"TS(This is firstLine
This is second Line)TS");
		f->VerifyQuickInfoAt(t, "3", "(parameter) param1: string", R"TS(first line of param

param information third line)TS");
		f->VerifyQuickInfoAt(t, "g", "function g(param1: string): void", R"TS(This is firstLine
This is second Line)TS");
		f->VerifyQuickInfoAt(t, "4", "(parameter) param1: string", " param information first line");
		f->VerifyQuickInfoAt(t, "h", "function h(param1: string): void", R"TS(This is firstLine
This is second Line)TS");
		f->VerifyQuickInfoAt(t, "5", "(parameter) param1: string", R"TS( param information first line

 param information third line)TS");
		f->VerifyQuickInfoAt(t, "i", "function i(param1: string): void", R"TS(This is firstLine
This is second Line)TS");
		f->VerifyQuickInfoAt(t, "6", "(parameter) param1: string", R"TS( param information first line

 param information third line)TS");
		f->VerifyQuickInfoAt(t, "j", "function j(param1: string): void", R"TS(This is firstLine
This is second Line)TS");
		f->VerifyQuickInfoAt(t, "7", "(parameter) param1: string", R"TS( param information first line

 param information third line)TS");
		f->VerifyQuickInfoAt(t, "k", "function k(param1: string): void", R"TS(This is firstLine
This is second Line)TS");
		f->VerifyQuickInfoAt(t, "8", "(parameter) param1: string", "hello");
		f->VerifyQuickInfoAt(t, "l", "function l(param1: string): void", R"TS(This is firstLine
This is second Line)TS");
		f->VerifyQuickInfoAt(t, "9", "(parameter) param1: string", R"TS(first Line text
blank line that shouldnt be shown when starting this 
second time information about the param again)TS");
		f->VerifyQuickInfoAt(t, "m", "function m(param1: string): void", R"TS(This is firstLine
This is second Line
[1]: third * line)TS");
		f->VerifyQuickInfoAt(t, "10", "(parameter) param1: string", R"TS(first Line text
second line text)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestCommentsLinePreservation, TestCommentsLinePreservation);

// commentsOverloadsFourslash_test.go

// commentsOverloadsFourslash_test.go
static void TestCommentsOverloadsFourslash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** this is signature 1*/
function /*1*/f1(/**param a*/a: number): number;
function /*2*/f1(b: string): number;
function /*3*/f1(aOrb: any) {
    return 10;
}
f/*4q*/1(/*4*/"hello");
f/*o4q*/1(/*o4*/10);
function /*5*/f2(/**param a*/a: number): number;
/** this is signature 2*/
function /*6*/f2(b: string): number;
/** this is f2 var comment*/
function /*7*/f2(aOrb: any) {
    return 10;
}
f/*8q*/2(/*8*/"hello");
f/*o8q*/2(/*o8*/10);
function /*9*/f3(a: number): number;
function /*10*/f3(b: string): number;
function /*11*/f3(aOrb: any) {
    return 10;
}
f/*12q*/3(/*12*/"hello");
f/*o12q*/3(/*o12*/10);
/** this is signature 4 - with number parameter*/
function /*13*/f4(/**param a*/a: number): number;
/** this is signature 4 - with string parameter*/
function /*14*/f4(b: string): number;
function /*15*/f4(aOrb: any) {
    return 10;
}
f/*16q*/4(/*16*/"hello");
f/*o16q*/4(/*o16*/10);
/*17*/
interface i1 {
    /**this signature 1*/
    (/**param a*/ a: number): number;
    /**this is signature 2*/
    (b: string): number;
    /** foo 1*/
    foo(a: number): number;
    /** foo 2*/
    foo(b: string): number;
    foo2(a: number): number;
    /** foo2 2*/
    foo2(b: string): number;
    foo3(a: number): number;
    foo3(b: string): number;
    /** foo4 1*/
    foo4(a: number): number;
    foo4(b: string): number;
    /** new 1*/
    new (a: string);
    new (b: number);
}
var i1_i: i1;
interface i2 {
    new (a: string);
    /** new 2*/
    new (b: number);
    (a: number): number;
    /**this is signature 2*/
    (b: string): number;
}
var i2_i: i2;
interface i3 {
    /** new 1*/
    new (a: string);
    /** new 2*/
    new (b: number);
    /**this is signature 1*/
    (a: number): number;
    (b: string): number;
}
var i3_i: i3;
interface i4 {
    new (a: string);
    new (b: number);
    (a: number): number;
    (b: string): number;
}
var i4_i: i4;
new /*18*/i1/*19q*/_i(/*19*/10);
new i/*20q*/1_i(/*20*/"Hello");
i/*21q*/1_i(/*21*/10);
i/*22q*/1_i(/*22*/"hello");
i1_i./*23*/f/*24q*/oo(/*24*/10);
i1_i.f/*25q*/oo(/*25*/"hello");
i1_i.fo/*26q*/o2(/*26*/10);
i1_i.fo/*27q*/o2(/*27*/"hello");
i1_i.fo/*28q*/o3(/*28*/10);
i1_i.fo/*29q*/o3(/*29*/"hello");
i1_i.fo/*30q*/o4(/*30*/10);
i1_i.fo/*31q*/o4(/*31*/"hello");
new i2/*32q*/_i(/*32*/10);
new i2/*33q*/_i(/*33*/"Hello");
i/*34q*/2_i(/*34*/10);
i2/*35q*/_i(/*35*/"hello");
new i/*36q*/3_i(/*36*/10);
new i3/*37q*/_i(/*37*/"Hello");
i3/*38q*/_i(/*38*/10);
i3/*39q*/_i(/*39*/"hello");
new i4/*40q*/_i(/*40*/10);
new i/*41q*/4_i(/*41*/"Hello");
i4/*42q*/_i(/*42*/10);
i4/*43q*/_i(/*43*/"hello");
class c {
    public /*93*/prop1(a: number): number;
    public /*94*/prop1(b: string): number;
    public /*95*/prop1(aorb: any) {
        return 10;
    }
    /** prop2 1*/
    public /*96*/prop2(a: number): number;
    public /*97*/prop2(b: string): number;
    public /*98*/prop2(aorb: any) {
        return 10;
    }
    public /*99*/prop3(a: number): number;
    /** prop3 2*/
    public /*100*/prop3(b: string): number;
    public /*101*/prop3(aorb: any) {
        return 10;
    }
    /** prop4 1*/
    public /*102*/prop4(a: number): number;
    /** prop4 2*/
    public /*103*/prop4(b: string): number;
    public /*104*/prop4(aorb: any) {
        return 10;
    }
    /** prop5 1*/
    public /*105*/prop5(a: number): number;
    /** prop5 2*/
    public /*106*/prop5(b: string): number;
    /** Prop5 implementaion*/
    public /*107*/prop5(aorb: any) {
        return 10;
    }
}
class c1 {
    /*78*/constructor(a: number);
    /*79*/constructor(b: string);
    /*80*/constructor(aorb: any) {
    }
}
class c2 {
    /** c2 1*/
    /*81*/constructor(a: number);
    /*82*/constructor(b: string);
    /*83*/constructor(aorb: any) {
    }
}
class c3 {
    /*84*/constructor(a: number);
    /** c3 2*/
    /*85*/constructor(b: string);
    /*86*/constructor(aorb: any) {
    }
}
class c4 {
    /** c4 1*/
    /*87*/constructor(a: number);
    /** c4 2*/
    /*88*/constructor(b: string);
    /*89*/constructor(aorb: any) {
    }
}
class c5 {
    /** c5 1*/
    /*90*/constructor(a: number);
    /** c5 2*/
    /*91*/constructor(b: string);
    /** c5 implementation*/
    /*92*/constructor(aorb: any) {
    }
}
var c_i = new c();
c_i./*44*/pro/*45q*/p1(/*45*/10);
c_i.pr/*46q*/op1(/*46*/"hello");
c_i.pr/*47q*/op2(/*47*/10);
c_i.pr/*48q*/op2(/*48*/"hello");
c_i.pro/*49q*/p3(/*49*/10);
c_i.pr/*50q*/op3(/*50*/"hello");
c_i.pr/*51q*/op4(/*51*/10);
c_i.pr/*52q*/op4(/*52*/"hello");
c_i.pr/*53q*/op5(/*53*/10);
c_i.pr/*54q*/op5(/*54*/"hello");
var c1/*66*/_i_1 = new c/*55q*/1(/*55*/10);
var c1_i_2 = new c/*56q*/1(/*56*/"hello");
var c2_i_1 = new c/*57q*/2(/*57*/10);
var c/*67*/2_i_2 = new c/*58q*/2(/*58*/"hello");
var c3_i_1 = new c/*59q*/3(/*59*/10);
var c/*68*/3_i_2 = new c/*60q*/3(/*60*/"hello");
var c4/*69*/_i_1 = new c/*61q*/4(/*61*/10);
var c4_i_2 = new c/*62q*/4(/*62*/"hello");
var c/*70*/5_i_1 = new c/*63q*/5(/*63*/10);
var c5_i_2 = new c/*64q*/5(/*64*/"hello");
/** This is multiOverload F1 1*/
function multiOverload(a: number): string;
/** This is multiOverload F1 2*/
function multiOverload(b: string): string;
/** This is multiOverload F1 3*/
function multiOverload(c: boolean): string;
/** This is multiOverload Implementation */
function multiOverload(d): string {
    return "Hello";
}
multiOverl/*71*/oad(10);
multiOverl/*72*/oad("hello");
multiOverl/*73*/oad(true);
/** This is ambient F1 1*/
declare function ambientF1(a: number): string;
/** This is ambient F1 2*/
declare function ambientF1(b: string): string;
/** This is ambient F1 3*/
declare function ambientF1(c: boolean): boolean;
/*65*/
ambient/*74*/F1(10);
ambient/*75*/F1("hello");
ambient/*76*/F1(true);
function foo(a/*77*/a: i3) {
}
foo(null);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "function f1(a: number): number (+1 overload)", "this is signature 1");
		f->VerifyQuickInfoAt(t, "2", "function f1(b: string): number (+1 overload)", "this is signature 1");
		f->VerifyQuickInfoAt(t, "3", "function f1(a: number): number (+1 overload)", "this is signature 1");
		f->VerifyQuickInfoAt(t, "4q", "function f1(b: string): number (+1 overload)", "this is signature 1");
		f->VerifyQuickInfoAt(t, "o4q", "function f1(a: number): number (+1 overload)", "this is signature 1");
		f->GoToMarker(t, "4");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->GoToMarker(t, "o4");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this is signature 1", .ParameterDocComment = "param a", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "5", "function f2(a: number): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "6", "function f2(b: string): number (+1 overload)", "this is signature 2");
		f->VerifyQuickInfoAt(t, "7", "function f2(a: number): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "8q", "function f2(b: string): number (+1 overload)", "this is signature 2");
		f->VerifyQuickInfoAt(t, "o8q", "function f2(a: number): number (+1 overload)", "");
		f->GoToMarker(t, "8");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this is signature 2", .OverloadsCount = 2});
		f->GoToMarker(t, "o8");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterDocComment = "param a", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "9", "function f3(a: number): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "10", "function f3(b: string): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "11", "function f3(a: number): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "12q", "function f3(b: string): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "o12q", "function f3(a: number): number (+1 overload)", "");
		f->GoToMarker(t, "12");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->GoToMarker(t, "o12");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "13", "function f4(a: number): number (+1 overload)", "this is signature 4 - with number parameter");
		f->VerifyQuickInfoAt(t, "14", "function f4(b: string): number (+1 overload)", "this is signature 4 - with string parameter");
		f->VerifyQuickInfoAt(t, "15", "function f4(a: number): number (+1 overload)", "this is signature 4 - with number parameter");
		f->VerifyQuickInfoAt(t, "16q", "function f4(b: string): number (+1 overload)", "this is signature 4 - with string parameter");
		f->VerifyQuickInfoAt(t, "o16q", "function f4(a: number): number (+1 overload)", "this is signature 4 - with number parameter");
		f->GoToMarker(t, "16");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this is signature 4 - with string parameter", .OverloadsCount = 2});
		f->GoToMarker(t, "o16");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this is signature 4 - with number parameter", .ParameterDocComment = "param a", .OverloadsCount = 2});
		f->VerifyCompletions(t, "17", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f1", .Detail = std::string("function f1(a: number): number (+1 overload)"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "this is signature 1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f2", .Detail = std::string("function f2(a: number): number (+1 overload)")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f3", .Detail = std::string("function f3(a: number): number (+1 overload)")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "f4", .Detail = std::string("function f4(a: number): number (+1 overload)"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "this is signature 4 - with number parameter"})})})}})}));
		f->VerifyCompletions(t, "18", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_i", .Detail = std::string(R"TS(var i1_i: i1
new (b: number) => any (+1 overload))TS"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "new 1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i2_i", .Detail = std::string(R"TS(var i2_i: i2
new (a: string) => any (+1 overload))TS")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i3_i", .Detail = std::string(R"TS(var i3_i: i3
new (a: string) => any (+1 overload))TS"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "new 1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i4_i", .Detail = std::string(R"TS(var i4_i: i4
new (a: string) => any (+1 overload))TS")})}, .Excludes = std::vector<std::string>{"i1", "i2", "i3", "i4"}})}));
		f->GoToMarker(t, "19");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "19q", R"TS(var i1_i: i1
new (b: number) => any (+1 overload))TS", "new 1");
		f->GoToMarker(t, "20");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "new 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "20q", R"TS(var i1_i: i1
new (a: string) => any (+1 overload))TS", "new 1");
		f->GoToMarker(t, "21");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this signature 1", .ParameterDocComment = "param a", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "21q", R"TS(var i1_i: i1
(a: number) => number (+1 overload))TS", "this signature 1");
		f->GoToMarker(t, "22");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this is signature 2", .OverloadsCount = 2});
		f->GoToMarker(t, "22q");
		f->VerifyQuickInfoAt(t, "22q", R"TS(var i1_i: i1
(b: string) => number (+1 overload))TS", "this is signature 2");
		f->VerifyCompletions(t, "23", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .Detail = std::string("(method) i1.foo(a: number): number (+1 overload)"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "foo 1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo2", .Detail = std::string("(method) i1.foo2(a: number): number (+1 overload)")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo3", .Detail = std::string("(method) i1.foo3(a: number): number (+1 overload)")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo4", .Detail = std::string("(method) i1.foo4(a: number): number (+1 overload)"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "foo4 1"})})})}})}));
		f->GoToMarker(t, "24");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "foo 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "24q", "(method) i1.foo(a: number): number (+1 overload)", "foo 1");
		f->GoToMarker(t, "25");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "foo 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "25q", "(method) i1.foo(b: string): number (+1 overload)", "foo 2");
		f->GoToMarker(t, "26");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "26q", "(method) i1.foo2(a: number): number (+1 overload)", "");
		f->GoToMarker(t, "27");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "foo2 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "27q", "(method) i1.foo2(b: string): number (+1 overload)", "foo2 2");
		f->GoToMarker(t, "28");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "28q", "(method) i1.foo3(a: number): number (+1 overload)", "");
		f->GoToMarker(t, "29");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "29q", "(method) i1.foo3(b: string): number (+1 overload)", "");
		f->GoToMarker(t, "30");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "foo4 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "30q", "(method) i1.foo4(a: number): number (+1 overload)", "foo4 1");
		f->GoToMarker(t, "31");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "31q", "(method) i1.foo4(b: string): number (+1 overload)", "foo4 1");
		f->GoToMarker(t, "32");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "new 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "32q", R"TS(var i2_i: i2
new (b: number) => any (+1 overload))TS", "new 2");
		f->GoToMarker(t, "33");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "33q", R"TS(var i2_i: i2
new (a: string) => any (+1 overload))TS", "");
		f->GoToMarker(t, "34");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "34q", R"TS(var i2_i: i2
(a: number) => number (+1 overload))TS", "");
		f->GoToMarker(t, "35");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this is signature 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "35q", R"TS(var i2_i: i2
(b: string) => number (+1 overload))TS", "this is signature 2");
		f->GoToMarker(t, "36");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "new 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "36q", R"TS(var i3_i: i3
new (b: number) => any (+1 overload))TS", "new 2");
		f->GoToMarker(t, "37");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "new 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "37q", R"TS(var i3_i: i3
new (a: string) => any (+1 overload))TS", "new 1");
		f->GoToMarker(t, "38");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "this is signature 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "38q", R"TS(var i3_i: i3
(a: number) => number (+1 overload))TS", "this is signature 1");
		f->GoToMarker(t, "39");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "39q", R"TS(var i3_i: i3
(b: string) => number (+1 overload))TS", "this is signature 1");
		f->GoToMarker(t, "40");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "40q", R"TS(var i4_i: i4
new (b: number) => any (+1 overload))TS", "");
		f->GoToMarker(t, "41");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "41q", R"TS(var i4_i: i4
new (a: string) => any (+1 overload))TS", "");
		f->GoToMarker(t, "42");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "42q", R"TS(var i4_i: i4
(a: number) => number (+1 overload))TS", "");
		f->GoToMarker(t, "43");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "43q", R"TS(var i4_i: i4
(b: string) => number (+1 overload))TS", "");
		f->VerifyCompletions(t, "44", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "prop1", .Detail = std::string("(method) c.prop1(a: number): number (+1 overload)")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "prop2", .Detail = std::string("(method) c.prop2(a: number): number (+1 overload)"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "prop2 1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "prop3", .Detail = std::string("(method) c.prop3(a: number): number (+1 overload)")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "prop4", .Detail = std::string("(method) c.prop4(a: number): number (+1 overload)"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "prop4 1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "prop5", .Detail = std::string("(method) c.prop5(a: number): number (+1 overload)"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "prop5 1"})})})}})}));
		f->GoToMarker(t, "45");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "45q", "(method) c.prop1(a: number): number (+1 overload)", "");
		f->GoToMarker(t, "46");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "46q", "(method) c.prop1(b: string): number (+1 overload)", "");
		f->GoToMarker(t, "47");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "prop2 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "47q", "(method) c.prop2(a: number): number (+1 overload)", "prop2 1");
		f->GoToMarker(t, "48");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "48q", "(method) c.prop2(b: string): number (+1 overload)", "prop2 1");
		f->GoToMarker(t, "49");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "49q", "(method) c.prop3(a: number): number (+1 overload)", "");
		f->GoToMarker(t, "50");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "prop3 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "50q", "(method) c.prop3(b: string): number (+1 overload)", "prop3 2");
		f->GoToMarker(t, "51");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "prop4 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "51q", "(method) c.prop4(a: number): number (+1 overload)", "prop4 1");
		f->GoToMarker(t, "52");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "prop4 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "52q", "(method) c.prop4(b: string): number (+1 overload)", "prop4 2");
		f->GoToMarker(t, "53");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "prop5 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "53q", "(method) c.prop5(a: number): number (+1 overload)", "prop5 1");
		f->GoToMarker(t, "54");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "prop5 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "54q", "(method) c.prop5(b: string): number (+1 overload)", "prop5 2");
		f->GoToMarker(t, "55");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "55q", "constructor c1(a: number): c1 (+1 overload)", "");
		f->GoToMarker(t, "56");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "56q", "constructor c1(b: string): c1 (+1 overload)", "");
		f->GoToMarker(t, "57");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c2 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "57q", "constructor c2(a: number): c2 (+1 overload)", "c2 1");
		f->GoToMarker(t, "58");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "58q", "constructor c2(b: string): c2 (+1 overload)", "c2 1");
		f->GoToMarker(t, "59");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "59q", "constructor c3(a: number): c3 (+1 overload)", "");
		f->GoToMarker(t, "60");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c3 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "60q", "constructor c3(b: string): c3 (+1 overload)", "c3 2");
		f->GoToMarker(t, "61");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c4 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "61q", "constructor c4(a: number): c4 (+1 overload)", "c4 1");
		f->GoToMarker(t, "62");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c4 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "62q", "constructor c4(b: string): c4 (+1 overload)", "c4 2");
		f->GoToMarker(t, "63");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c5 1", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "63q", "constructor c5(a: number): c5 (+1 overload)", "c5 1");
		f->GoToMarker(t, "64");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "c5 2", .OverloadsCount = 2});
		f->VerifyQuickInfoAt(t, "64q", "constructor c5(b: string): c5 (+1 overload)", "c5 2");
		f->VerifyCompletions(t, "65", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c", .Detail = std::string("class c")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c1", .Detail = std::string("class c1")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2", .Detail = std::string("class c2")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c3", .Detail = std::string("class c3")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c4", .Detail = std::string("class c4")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c5", .Detail = std::string("class c5")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c_i", .Detail = std::string("var c_i: c")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c1_i_1", .Detail = std::string("var c1_i_1: c1")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_i_1", .Detail = std::string("var c2_i_1: c2")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c3_i_1", .Detail = std::string("var c3_i_1: c3")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c4_i_1", .Detail = std::string("var c4_i_1: c4")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c5_i_1", .Detail = std::string("var c5_i_1: c5")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c1_i_2", .Detail = std::string("var c1_i_2: c1")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c2_i_2", .Detail = std::string("var c2_i_2: c2")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c3_i_2", .Detail = std::string("var c3_i_2: c3")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c4_i_2", .Detail = std::string("var c4_i_2: c4")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "c5_i_2", .Detail = std::string("var c5_i_2: c5")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "multiOverload", .Detail = std::string("function multiOverload(a: number): string (+2 overloads)"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "This is multiOverload F1 1"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "ambientF1", .Detail = std::string("function ambientF1(a: number): string (+2 overloads)"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "This is ambient F1 1"})})})}})}));
		f->VerifyQuickInfoAt(t, "66", "var c1_i_1: c1", "");
		f->VerifyQuickInfoAt(t, "67", "var c2_i_2: c2", "");
		f->VerifyQuickInfoAt(t, "68", "var c3_i_2: c3", "");
		f->VerifyQuickInfoAt(t, "69", "var c4_i_1: c4", "");
		f->VerifyQuickInfoAt(t, "70", "var c5_i_1: c5", "");
		f->VerifyQuickInfoAt(t, "71", "function multiOverload(a: number): string (+2 overloads)", "This is multiOverload F1 1");
		f->VerifyQuickInfoAt(t, "72", "function multiOverload(b: string): string (+2 overloads)", "This is multiOverload F1 2");
		f->VerifyQuickInfoAt(t, "73", "function multiOverload(c: boolean): string (+2 overloads)", "This is multiOverload F1 3");
		f->VerifyQuickInfoAt(t, "74", "function ambientF1(a: number): string (+2 overloads)", "This is ambient F1 1");
		f->VerifyQuickInfoAt(t, "75", "function ambientF1(b: string): string (+2 overloads)", "This is ambient F1 2");
		f->VerifyQuickInfoAt(t, "76", "function ambientF1(c: boolean): boolean (+2 overloads)", "This is ambient F1 3");
		f->VerifyQuickInfoAt(t, "77", "(parameter) aa: i3", "");
		f->VerifyQuickInfoAt(t, "78", "constructor c1(a: number): c1 (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "79", "constructor c1(b: string): c1 (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "80", "constructor c1(a: number): c1 (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "81", "constructor c2(a: number): c2 (+1 overload)", "c2 1");
		f->VerifyQuickInfoAt(t, "82", "constructor c2(b: string): c2 (+1 overload)", "c2 1");
		f->VerifyQuickInfoAt(t, "83", "constructor c2(a: number): c2 (+1 overload)", "c2 1");
		f->VerifyQuickInfoAt(t, "84", "constructor c3(a: number): c3 (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "85", "constructor c3(b: string): c3 (+1 overload)", "c3 2");
		f->VerifyQuickInfoAt(t, "86", "constructor c3(a: number): c3 (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "87", "constructor c4(a: number): c4 (+1 overload)", "c4 1");
		f->VerifyQuickInfoAt(t, "88", "constructor c4(b: string): c4 (+1 overload)", "c4 2");
		f->VerifyQuickInfoAt(t, "89", "constructor c4(a: number): c4 (+1 overload)", "c4 1");
		f->VerifyQuickInfoAt(t, "90", "constructor c5(a: number): c5 (+1 overload)", "c5 1");
		f->VerifyQuickInfoAt(t, "91", "constructor c5(b: string): c5 (+1 overload)", "c5 2");
		f->VerifyQuickInfoAt(t, "92", "constructor c5(a: number): c5 (+1 overload)", "c5 1");
		f->VerifyQuickInfoAt(t, "93", "(method) c.prop1(a: number): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "94", "(method) c.prop1(b: string): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "95", "(method) c.prop1(a: number): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "96", "(method) c.prop2(a: number): number (+1 overload)", "prop2 1");
		f->VerifyQuickInfoAt(t, "97", "(method) c.prop2(b: string): number (+1 overload)", "prop2 1");
		f->VerifyQuickInfoAt(t, "98", "(method) c.prop2(a: number): number (+1 overload)", "prop2 1");
		f->VerifyQuickInfoAt(t, "99", "(method) c.prop3(a: number): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "100", "(method) c.prop3(b: string): number (+1 overload)", "prop3 2");
		f->VerifyQuickInfoAt(t, "101", "(method) c.prop3(a: number): number (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "102", "(method) c.prop4(a: number): number (+1 overload)", "prop4 1");
		f->VerifyQuickInfoAt(t, "103", "(method) c.prop4(b: string): number (+1 overload)", "prop4 2");
		f->VerifyQuickInfoAt(t, "104", "(method) c.prop4(a: number): number (+1 overload)", "prop4 1");
		f->VerifyQuickInfoAt(t, "105", "(method) c.prop5(a: number): number (+1 overload)", "prop5 1");
		f->VerifyQuickInfoAt(t, "106", "(method) c.prop5(b: string): number (+1 overload)", "prop5 2");
		f->VerifyQuickInfoAt(t, "107", "(method) c.prop5(a: number): number (+1 overload)", "prop5 1");
	});
}
REGISTER_FOURSLASH_TEST(TestCommentsOverloadsFourslash, TestCommentsOverloadsFourslash);

// commentsUnion_test.go

// commentsUnion_test.go
static void TestCommentsUnion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var a: Array<string> | Array<number>;
a./*1*/length)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) Array<T>.length: number", "Gets or sets the length of the array. This is a number one higher than the highest index in the array.");
	});
}
REGISTER_FOURSLASH_TEST(TestCommentsUnion, TestCommentsUnion);

// commentsVariables_test.go

// commentsVariables_test.go
static void TestCommentsVariables(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** This is my variable*/
var myV/*1*/ariable = 10;
/*2*/
/** d variable*/
var d = 10;
myVariable = d;
/*3*/
/** foos comment*/
function foo() {
}
/** fooVar comment*/
var foo/*12*/Var: () => void;
/*4*/
f/*5q*/oo(/*5*/);
fo/*6q*/oVar(/*6*/);
fo/*13*/oVar = f/*14*/oo;
/*7*/
f/*8q*/oo(/*8*/);
foo/*9q*/Var(/*9*/);
var fooVarVar = /*9aq*/fooVar;
/**class comment*/
class c {
    /** constructor comment*/
    constructor() {
    }
}
/**instance comment*/
var i = new c();
/*10*/
/** interface comments*/
interface i1 {
}
/**interface instance comments*/
var i1_i: i1;
/*11*/
function foo2(a: number): void;
function foo2(b: string): void;
function foo2(aOrb) {
}
var x = fo/*15*/o2;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var myVariable: number", "This is my variable");
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "myVariable", .Detail = std::string("var myVariable: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "This is my variable"})})})}})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "myVariable", .Detail = std::string("var myVariable: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "This is my variable"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "d", .Detail = std::string("var d: number"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "d variable"})})})}})}));
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .Detail = std::string("function foo(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "foos comment"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooVar", .Detail = std::string("var fooVar: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "fooVar comment"})})})}})}));
		f->GoToMarker(t, "5");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "foos comment"});
		f->VerifyQuickInfoAt(t, "5q", "function foo(): void", "foos comment");
		f->GoToMarker(t, "6");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "fooVar comment"});
		f->VerifyQuickInfoAt(t, "6q", "var fooVar: () => void", "fooVar comment");
		f->VerifyCompletions(t, "7", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "foo", .Detail = std::string("function foo(): void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "foos comment"})})}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "fooVar", .Detail = std::string("var fooVar: () => void"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "fooVar comment"})})})}})}));
		f->GoToMarker(t, "8");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "foos comment"});
		f->VerifyQuickInfoAt(t, "8q", "function foo(): void", "foos comment");
		f->GoToMarker(t, "9");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "fooVar comment"});
		f->VerifyQuickInfoAt(t, "9q", "var fooVar: () => void", "fooVar comment");
		f->VerifyQuickInfoAt(t, "9aq", "var fooVar: () => void", "fooVar comment");
		f->VerifyCompletions(t, "10", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i", .Detail = std::string("var i: c"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "instance comment"})})})}})}));
		f->VerifyCompletions(t, "11", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "i1_i", .Detail = std::string("var i1_i: i1"), .Documentation = std::make_shared<lsproto::StringOrMarkupContent>(lsproto::StringOrMarkupContent{.MarkupContent = std::make_shared<lsproto::MarkupContent>(lsproto::MarkupContent{.Kind = lsproto::MarkupKindMarkdown, .Value = "interface instance comments"})})})}})}));
		f->VerifyQuickInfoAt(t, "12", "var fooVar: () => void", "fooVar comment");
		f->VerifyQuickInfoAt(t, "13", "var fooVar: () => void", "fooVar comment");
		f->VerifyQuickInfoAt(t, "14", "function foo(): void", "foos comment");
		f->VerifyQuickInfoAt(t, "15", "function foo2(a: number): void (+1 overload)", "");
	});
}
REGISTER_FOURSLASH_TEST(TestCommentsVariables, TestCommentsVariables);

// docCommentTemplateClassDecl01_test.go

// docCommentTemplateClassDecl01_test.go
static void TestDocCommentTemplateClassDecl01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/*decl*/class C {
    private p;
    constructor(a, b, c, d);
    constructor(public a, private b, protected c, d, e?) {
    }

    foo();
    foo(a?, b?, ...args) {
    }
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "decl", 3, R"TS(/** */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateClassDecl01, TestDocCommentTemplateClassDecl01);

// docCommentTemplateClassDeclMethods01_test.go

// docCommentTemplateClassDeclMethods01_test.go
static void TestDocCommentTemplateClassDeclMethods01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(class C {
/*0*/    /*1*/
    foo();
    /*2*/foo(a);
    /*3*/foo(a, b);
    /*4*/foo(a, {x: string}, [c]);
    /*5*/foo(a?, b?, ...args) {
    }
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "0", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "1", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "2", 11, R"TS(/**
     * 
     * @param a
     */
    )TS", nullptr);
		f->VerifyJSDocCompletion(t, "3", 11, R"TS(/**
     * 
     * @param a
     * @param b
     */
    )TS", nullptr);
		f->VerifyJSDocCompletion(t, "4", 11, R"TS(/**
     * 
     * @param a
     * @param param1
     * @param param2
     */
    )TS", nullptr);
		f->VerifyJSDocCompletion(t, "5", 11, R"TS(/**
     * 
     * @param a
     * @param b
     * @param args
     */
    )TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateClassDeclMethods01, TestDocCommentTemplateClassDeclMethods01);

// docCommentTemplateClassDeclMethods02_test.go

// docCommentTemplateClassDeclMethods02_test.go
static void TestDocCommentTemplateClassDeclMethods02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    /*0*/
    [Symbol.iterator]() {
        return undefined;
    }
    /*1*/
    [1 + 2 + 3 + Math.rand()](x: number, y: string, z = true) { }
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "0", 11, R"TS(/**
     * 
     * @returns
     */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "1", 11, R"TS(/**
     * 
     * @param x
     * @param y
     * @param z
     */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateClassDeclMethods02, TestDocCommentTemplateClassDeclMethods02);

// docCommentTemplateClassDeclProperty01_test.go

// docCommentTemplateClassDeclProperty01_test.go
static void TestDocCommentTemplateClassDeclProperty01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    /** /*0*/  */
    foo = (p0) => {
        return p0;
    };
    /*1*/
    bar = (p1) => {
        return p1;
    }
    /*2*/
    baz = function (p2, p3) {
        return p2;
    }
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "0", 11, R"TS(/**
     * 
     * @param p0
     * @returns
     */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "1", 11, R"TS(/**
     * 
     * @param p1
     * @returns
     */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "2", 11, R"TS(/**
     * 
     * @param p2
     * @param p3
     * @returns
     */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateClassDeclProperty01, TestDocCommentTemplateClassDeclProperty01);

// docCommentTemplateConstructor01_test.go

// docCommentTemplateConstructor01_test.go
static void TestDocCommentTemplateConstructor01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    private p;
    /*0*/
    constructor(a, b, c, d);
    /*1*/
    constructor(public a, private b, protected c, d, e?) {
    }

    foo();
    foo(a?, b?, ...args) {
    }
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "0", 11, R"TS(/**
     * 
     * @param a
     * @param b
     * @param c
     * @param d
     */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "1", 11, R"TS(/**
     * 
     * @param a
     * @param b
     * @param c
     * @param d
     * @param e
     */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateConstructor01, TestDocCommentTemplateConstructor01);

// docCommentTemplateEmptyFile_test.go

// docCommentTemplateEmptyFile_test.go
static void TestDocCommentTemplateEmptyFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: emptyFile.ts
/*0*/)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoJSDocCompletion(t, "0");
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateEmptyFile, TestDocCommentTemplateEmptyFile);

// docCommentTemplateExportAssignmentJS_test.go

// docCommentTemplateExportAssignmentJS_test.go
static void TestDocCommentTemplateExportAssignmentJS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @allowJs: true
// @checkJs: true
// @Filename: index.js
/** /**/ */
exports.foo = (a) => {};)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "", 7, R"TS(/**
 * 
 * @param {any} a
 */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateExportAssignmentJS, TestDocCommentTemplateExportAssignmentJS);

// docCommentTemplateFunctionExpression_test.go

// docCommentTemplateFunctionExpression_test.go
static void TestDocCommentTemplateFunctionExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/*above*/
const x = /*next*/ function f(p) {})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		{
			int _ = 0;
			for (auto&& marker : f->MarkerNames()) {
				f->VerifyJSDocCompletion(t, marker, 7, R"TS(/**
 * 
 * @param p
 */)TS", nullptr);
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateFunctionExpression, TestDocCommentTemplateFunctionExpression);

// docCommentTemplateFunctionWithParameters_js_test.go

// docCommentTemplateFunctionWithParameters_js_test.go
static void TestDocCommentTemplateFunctionWithParameters_js(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/*0*/
function f(a, ...b): boolean {})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "0", 7, R"TS(/**
 * 
 * @param {any} a
 * @param {...any} b
 */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateFunctionWithParameters_js, TestDocCommentTemplateFunctionWithParameters_js);

// docCommentTemplateFunctionWithParameters_test.go

// docCommentTemplateFunctionWithParameters_test.go
static void TestDocCommentTemplateFunctionWithParameters(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: functionWithParams.ts
/*0*/
    /*1*/
        function foo(x: number, y: string): boolean {})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "0");
		f->VerifyJSDocCompletion(t, "0", 7, R"TS(/**
 * 
 * @param x
 * @param y
 */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "1", 0, R"TS(/**
     * 
     * @param x
     * @param y
     */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateFunctionWithParameters, TestDocCommentTemplateFunctionWithParameters);

// docCommentTemplateInMultiLineComment_test.go

// docCommentTemplateInMultiLineComment_test.go
static void TestDocCommentTemplateInMultiLineComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: justAComment.ts
/* /*0*/ */)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoJSDocCompletion(t, "0");
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateInMultiLineComment, TestDocCommentTemplateInMultiLineComment);

// docCommentTemplateInSingleLineComment_test.go

// docCommentTemplateInSingleLineComment_test.go
static void TestDocCommentTemplateInSingleLineComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: justAComment.ts
// We want to check off-by-one errors in assessing the end of the comment, so we check twice,
// first with a trailing space and then without.
// /*0*/ 
// /*1*/
// We also want to check EOF handling at the end of a comment
// /*2*/)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		{
			int _ = 0;
			for (auto&& marker : f->Markers()) {
				f->VerifyNoJSDocCompletion(t, marker);
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateInSingleLineComment, TestDocCommentTemplateInSingleLineComment);

// docCommentTemplateIndentation_test.go

// docCommentTemplateIndentation_test.go
static void TestDocCommentTemplateIndentation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: indents.ts
    a   /*2*/
    /*1*/
/*0*/        function foo() { })TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "0", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "1", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "2", 3, R"TS(/** */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateIndentation, TestDocCommentTemplateIndentation);

// docCommentTemplateInsideFunctionDeclaration_test.go

// docCommentTemplateInsideFunctionDeclaration_test.go
static void TestDocCommentTemplateInsideFunctionDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: functionDecl.ts
f/*0*/unction /*1*/foo/*2*/(/*3*/) /*4*/{ /*5*/})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		{
			int _ = 0;
			for (auto&& marker : f->Markers()) {
				f->VerifyNoJSDocCompletion(t, marker);
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateInsideFunctionDeclaration, TestDocCommentTemplateInsideFunctionDeclaration);

// docCommentTemplateInterfacePropertyFunctionType_test.go

// docCommentTemplateInterfacePropertyFunctionType_test.go
static void TestDocCommentTemplateInterfacePropertyFunctionType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    /**/
    foo: (a: number, b: string) => void;
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "", 11, R"TS(/**
     * 
     * @param a
     * @param b
     * @returns
     */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "", 11, R"TS(/**
     * 
     * @param a
     * @param b
     */)TS", std::make_shared<bool>(false));
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateInterfacePropertyFunctionType, TestDocCommentTemplateInterfacePropertyFunctionType);

// docCommentTemplateInterfacesEnumsAndTypeAliases_test.go

// docCommentTemplateInterfacesEnumsAndTypeAliases_test.go
static void TestDocCommentTemplateInterfacesEnumsAndTypeAliases(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*interfaceFoo*/
interface Foo {
    /*propertybar*/
    bar: any;

    /*methodbaz*/
    baz(message: any): void;

    /*methodUnit*/
    unit(): void;
}

/*enumStatus*/
const enum Status {
    /*memberOpen*/
    Open,

    /*memberClosed*/
    Closed
}

/*aliasBar*/
type Bar = Foo & any;)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "interfaceFoo", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "propertybar", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "methodbaz", 11, R"TS(/**
     * 
     * @param message
     */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "methodUnit", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "enumStatus", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "memberOpen", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "memberClosed", 3, R"TS(/** */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateInterfacesEnumsAndTypeAliases, TestDocCommentTemplateInterfacesEnumsAndTypeAliases);

// docCommentTemplateJsSpecialPropertyAssignment_test.go

// docCommentTemplateJsSpecialPropertyAssignment_test.go
static void TestDocCommentTemplateJsSpecialPropertyAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/*0*/module.exports = function(a) {};
const myNamespace  = {};
/*1*/myNamespace.myExport = function(x) {};)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "0", 7, R"TS(/**
 * 
 * @param {any} a
 */
)TS", nullptr);
		f->VerifyJSDocCompletion(t, "1", 7, R"TS(/**
 * 
 * @param {any} x
 */
)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateJsSpecialPropertyAssignment, TestDocCommentTemplateJsSpecialPropertyAssignment);

// docCommentTemplateNamespacesAndModules01_test.go

// docCommentTemplateNamespacesAndModules01_test.go
static void TestDocCommentTemplateNamespacesAndModules01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*namespaceN*/
namespace n {
}

/*namespaceM*/
namespace m {
}

/*ambientModule*/
module "ambientModule" {
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "namespaceN", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "namespaceM", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "ambientModule", 3, R"TS(/** */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateNamespacesAndModules01, TestDocCommentTemplateNamespacesAndModules01);

// docCommentTemplateNamespacesAndModules02_test.go

// docCommentTemplateNamespacesAndModules02_test.go
static void TestDocCommentTemplateNamespacesAndModules02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*top*/
namespace n1.
    /*n2*/ n2.
    /*n3*/ n3 {
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "top", 3, R"TS(/** */)TS", nullptr);
		f->VerifyNoJSDocCompletion(t, "n2");
		f->VerifyNoJSDocCompletion(t, "n3");
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateNamespacesAndModules02, TestDocCommentTemplateNamespacesAndModules02);

// docCommentTemplateObjectLiteralMethods01_test.go

// docCommentTemplateObjectLiteralMethods01_test.go
static void TestDocCommentTemplateObjectLiteralMethods01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = {
    /*0*/
    foo() {
        return undefined;
    }

    /*1*/
    [1 + 2 + 3 + Math.rand()](x: number, y: string, z = true) { }

    /*2*/
    m1: function(a) {}

    /*3*/
    m2: (a: string, b: string) => {}
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "0", 11, R"TS(/**
     * 
     * @returns
     */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "1", 11, R"TS(/**
     * 
     * @param x
     * @param y
     * @param z
     */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "2", 11, R"TS(/**
     * 
     * @param a
     */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "3", 11, R"TS(/**
     * 
     * @param a
     * @param b
     */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateObjectLiteralMethods01, TestDocCommentTemplateObjectLiteralMethods01);

// docCommentTemplatePrototypeMethod_test.go

// docCommentTemplatePrototypeMethod_test.go
static void TestDocCommentTemplatePrototypeMethod(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: foo.js
/** @class */
function C() { }
/*above*/
C.prototype.method = /*next*/ function (p) {})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		{
			int _ = 0;
			for (auto&& marker : f->MarkerNames()) {
				f->VerifyJSDocCompletion(t, marker, 7, R"TS(/**
 * 
 * @param {any} p
 */)TS", nullptr);
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplatePrototypeMethod, TestDocCommentTemplatePrototypeMethod);

// docCommentTemplateRegex_test.go

// docCommentTemplateRegex_test.go
static void TestDocCommentTemplateRegex(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var regex = /*0*///*1*/asdf/*2*/ /*3*///*4*/;)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		{
			int _ = 0;
			for (auto&& marker : f->Markers()) {
				f->VerifyNoJSDocCompletion(t, marker);
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateRegex, TestDocCommentTemplateRegex);

// docCommentTemplateReturnsTag1_test.go

// docCommentTemplateReturnsTag1_test.go
static void TestDocCommentTemplateReturnsTag1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*0*/
function f1() {}
/*1*/
function f2() {
    return 1;
}
/*2*/
const f3 = () => 1;
/*3*/
const f3 = () => {
    return 1;
}
class Foo {
    /*4*/
    m1() {}

    /*5*/
    m2() {
       return 1;
    }
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "0", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "1", 7, R"TS(/**
 * 
 * @returns
 */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "2", 7, R"TS(/**
 * 
 * @returns
 */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "3", 7, R"TS(/**
 * 
 * @returns
 */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "4", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "5", 11, R"TS(/**
     * 
     * @returns
     */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateReturnsTag1, TestDocCommentTemplateReturnsTag1);

// docCommentTemplateReturnsTag2_test.go

// docCommentTemplateReturnsTag2_test.go
static void TestDocCommentTemplateReturnsTag2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*0*/
function f1(x: number, y: number) {
    return 1;
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "0", 7, R"TS(/**
 * 
 * @param x
 * @param y
 * @returns
 */)TS", std::make_shared<bool>(true));
		f->VerifyJSDocCompletion(t, "0", 7, R"TS(/**
 * 
 * @param x
 * @param y
 */)TS", std::make_shared<bool>(false));
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateReturnsTag2, TestDocCommentTemplateReturnsTag2);

// docCommentTemplateVariableStatements01_test.go

// docCommentTemplateVariableStatements01_test.go
static void TestDocCommentTemplateVariableStatements01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*a*/
var a = 10;

/*b*/
let b = "";

/*c*/
const c = 30;

/*d*/
let d = {
    foo: 10,
    bar: "20"
};

/*e*/
let e = function e(x, y, z) {
    return +(x + y + z);
};

/*f*/
let f = class F {
    constructor(a, b, c) {
        this.a = a;
        this.b = b || (this.c = c);
    }
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		{
			int _ = 0;
			for (auto&& varName : std::vector<std::string>{"a", "b", "c", "d"}) {
				f->VerifyJSDocCompletion(t, varName, 3, R"TS(/** */)TS", nullptr);
				_++;
			}
		}
		f->VerifyJSDocCompletion(t, "e", 7, R"TS(/**
 * 
 * @param x
 * @param y
 * @param z
 * @returns
 */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "f", 7, R"TS(/**
 * 
 * @param a
 * @param b
 * @param c
 */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateVariableStatements01, TestDocCommentTemplateVariableStatements01);

// docCommentTemplateVariableStatements02_test.go

// docCommentTemplateVariableStatements02_test.go
static void TestDocCommentTemplateVariableStatements02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*a*/
var a1 = 10, a2 = 20;

/*b*/
let b1 = "", b2 = true;

/*c*/
const c1 = 30, c2 = 40;

/*d*/
let d1 = function d(x, y, z) {
    return +(x + y + z);
}, d2 = 50;

/*e*/
let e1 = class E {
    constructor(a, b, c) {
        this.a = a;
        this.b = b || (this.c = c);
    }
}, e2 = () => 100;

/*f*/
let f1 = {
    foo: 10,
    bar: "20"
}, f2 = null;)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		{
			int _ = 0;
			for (auto&& varName : std::vector<std::string>{"a", "b", "c", "d", "e", "f"}) {
				f->VerifyJSDocCompletion(t, varName, 3, R"TS(/** */)TS", nullptr);
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateVariableStatements02, TestDocCommentTemplateVariableStatements02);

// docCommentTemplateVariableStatements03_test.go

// docCommentTemplateVariableStatements03_test.go
static void TestDocCommentTemplateVariableStatements03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*a*/
var a = x => x

/*b*/
let b = (x,y,z) => x + y + z;

/*c*/
const c = ((x => +x))

/*d*/
let d = (function () { })

/*e*/
let e = function e([a,b,c]) {
    return "hello"
};

/*f*/
let f = class {
}

/*g*/
const g = ((class G {
    constructor(private x);
    constructor(x,y,z);
    constructor(x,y,z, ...okayThatsEnough) {
    }
})))TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "a", 7, R"TS(/**
 * 
 * @param x
 * @returns
 */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "b", 7, R"TS(/**
 * 
 * @param x
 * @param y
 * @param z
 * @returns
 */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "c", 7, R"TS(/**
 * 
 * @param x
 * @returns
 */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "d", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "e", 7, R"TS(/**
 * 
 * @param param0
 * @returns
 */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "f", 3, R"TS(/** */)TS", nullptr);
		f->VerifyJSDocCompletion(t, "g", 7, R"TS(/**
 * 
 * @param x
 */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateVariableStatements03, TestDocCommentTemplateVariableStatements03);

// docCommentTemplateWithExistingJSDoc_test.go

// docCommentTemplateWithExistingJSDoc_test.go
static void TestDocCommentTemplateWithExistingJSDoc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** /**/ */

/**
 * @param {string} a
 * @param {string} b
 */
function foo(a, b) {
    return a + b;
})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoJSDocCompletion(t, "");
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateWithExistingJSDoc, TestDocCommentTemplateWithExistingJSDoc);

// docCommentTemplateWithMultipleJSDoc1_test.go

// docCommentTemplateWithMultipleJSDoc1_test.go
static void TestDocCommentTemplateWithMultipleJSDoc1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** */
/*/**/
function foo() {})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "", 3, R"TS(/** */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateWithMultipleJSDoc1, TestDocCommentTemplateWithMultipleJSDoc1);

// docCommentTemplateWithMultipleJSDoc2_test.go

// docCommentTemplateWithMultipleJSDoc2_test.go
static void TestDocCommentTemplateWithMultipleJSDoc2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @typedef {string} Id */

/** /**/ */
function foo(x, y, z) {})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "", 7, R"TS(/**
 * 
 * @param x
 * @param y
 * @param z
 */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateWithMultipleJSDoc2, TestDocCommentTemplateWithMultipleJSDoc2);

// docCommentTemplateWithMultipleJSDoc3_test.go

// docCommentTemplateWithMultipleJSDoc3_test.go
static void TestDocCommentTemplateWithMultipleJSDoc3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** @param p */
/*/**/
function foo(p) {})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "", 3, R"TS(/** */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateWithMultipleJSDoc3, TestDocCommentTemplateWithMultipleJSDoc3);

// docCommentTemplateWithMultipleJSDocAndParameters_test.go

// docCommentTemplateWithMultipleJSDocAndParameters_test.go
static void TestDocCommentTemplateWithMultipleJSDocAndParameters(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** */
/**
 * 
 * @param p 
 */
/** */
/*/**/
function foo(p) {})TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "", 7, R"TS(/**
 * 
 * @param p
 */)TS", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplateWithMultipleJSDocAndParameters, TestDocCommentTemplateWithMultipleJSDocAndParameters);

// docCommentTemplate_insideEmptyComment_test.go

// docCommentTemplate_insideEmptyComment_test.go
static void TestDocCommentTemplate_insideEmptyComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** /**/ */
function f(p) { return p; }

/** Doc/*1*/ */
function g(p) { return p; })TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = bool(false);
		auto __fsp = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyJSDocCompletion(t, "", 7, R"TS(/**
 * 
 * @param p
 * @returns
 */)TS", nullptr);
		f->VerifyNoJSDocCompletion(t, "1");
	});
}
REGISTER_FOURSLASH_TEST(TestDocCommentTemplate_insideEmptyComment, TestDocCommentTemplate_insideEmptyComment);

// unclosedCommentsInConstructor_test.go

// unclosedCommentsInConstructor_test.go
static void TestUnclosedCommentsInConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    constructor(/* /**/) { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestUnclosedCommentsInConstructor, TestUnclosedCommentsInConstructor);

} // namespace
