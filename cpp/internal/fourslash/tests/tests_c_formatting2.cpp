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

// format01_test.go

// format01_test.go
static void TestFormat01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
/**/namespace Default{var x= ( { } ) ;})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->FormatDocument(t, "");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(namespace Default { var x = ({}); })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormat01, TestFormat01);

// formatAfterMultilineComment_test.go

// formatAfterMultilineComment_test.go
static void TestFormatAfterMultilineComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*foo
*/"123123";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(/*foo
*/"123123";)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatAfterMultilineComment, TestFormatAfterMultilineComment);

// formatAfterObjectLiteral_test.go

// formatAfterObjectLiteral_test.go
static void TestFormatAfterObjectLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**/namespace Default{var x= ( { } ) ;})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(namespace Default { var x = ({}); })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatAfterObjectLiteral, TestFormatAfterObjectLiteral);

// formatAfterPasteInString_test.go

// formatAfterPasteInString_test.go
static void TestFormatAfterPasteInString(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*2*/const x = f('aa/*1*/a').x())TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Paste(t, "bb");
		f->FormatDocument(t, "");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(const x = f('aabba').x())TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatAfterPasteInString, TestFormatAfterPasteInString);

// formatAfterWhitespace_test.go

// formatAfterWhitespace_test.go
static void TestFormatAfterWhitespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo()
{
    var bar;
    /*1*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->InsertLine(t, "");
		f->VerifyCurrentFileContent(t, R"TS(function foo()
{
    var bar;


})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatAfterWhitespace, TestFormatAfterWhitespace);

// formatAnyTypeLiteral_test.go

// formatAnyTypeLiteral_test.go
static void TestFormatAnyTypeLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(x: { } /*objLit*/){
/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, "}");
		f->GoToMarker(t, "objLit");
		f->VerifyCurrentLineContent(t, R"TS(function foo(x: {}) {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatAnyTypeLiteral, TestFormatAnyTypeLiteral);

// formatArrayLiteralExpression_test.go

// formatArrayLiteralExpression_test.go
static void TestFormatArrayLiteralExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export let Things = [{
    Hat: 'hat', /*1*/
    Glove: 'glove',
    Umbrella: 'umbrella'
},{/*2*/
        Salad: 'salad', /*3*/
        Burrito: 'burrito',
        Pie: 'pie'
    }];/*4*/

export let Things2 = [
{
    Hat: 'hat', /*5*/
    Glove: 'glove',
    Umbrella: 'umbrella'
}/*6*/,
    {
        Salad: 'salad', /*7*/
        Burrito: ['burrito', 'carne asada', 'tinga de res', 'tinga de pollo'], /*8*/
        Pie: 'pie'
    }];/*9*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    Hat: 'hat',)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(}, {)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    Salad: 'salad',)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(}];)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(        Hat: 'hat',)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    },)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(        Salad: 'salad',)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(        Burrito: ['burrito', 'carne asada', 'tinga de res', 'tinga de pollo'],)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(    }];)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatArrayLiteralExpression, TestFormatArrayLiteralExpression);

// formatArrayOrObjectLiteralsInVariableList_test.go

// formatArrayOrObjectLiteralsInVariableList_test.go
static void TestFormatArrayOrObjectLiteralsInVariableList(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var v30 = [1, 2], v31, v32, v33 = [0], v34 = {'a': true}, v35;/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(var v30 = [1, 2], v31, v32, v33 = [0], v34 = { 'a': true }, v35;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatArrayOrObjectLiteralsInVariableList, TestFormatArrayOrObjectLiteralsInVariableList);

// formatAsyncClassMethod1_test.go

// formatAsyncClassMethod1_test.go
static void TestFormatAsyncClassMethod1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    async     foo() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(class Foo {
    async foo() { }
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatAsyncClassMethod1, TestFormatAsyncClassMethod1);

// formatAsyncClassMethod2_test.go

// formatAsyncClassMethod2_test.go
static void TestFormatAsyncClassMethod2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    private    async     foo() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(class Foo {
    private async foo() { }
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatAsyncClassMethod2, TestFormatAsyncClassMethod2);

// formatAsyncComputedMethod_test.go

// formatAsyncComputedMethod_test.go
static void TestFormatAsyncComputedMethod(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    /*method*/async [0]() { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "method");
		f->VerifyCurrentLineContent(t, R"TS(    async [0]() { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatAsyncComputedMethod, TestFormatAsyncComputedMethod);

// formatAsyncKeyword_test.go

// formatAsyncKeyword_test.go
static void TestFormatAsyncKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/let x = async         () => 1;
/*2*/let y = async() => 1;
/*3*/let z = async    function   () { return 1; };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(let x = async () => 1;)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(let y = async () => 1;)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(let z = async function() { return 1; };)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatAsyncKeyword, TestFormatAsyncKeyword);

// formatBracketInSwitchCase_test.go

// formatBracketInSwitchCase_test.go
static void TestFormatBracketInSwitchCase(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
switch (x) {
    case[]:
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(switch (x) {
    case []:
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatBracketInSwitchCase, TestFormatBracketInSwitchCase);

// formatColonAndQMark_test.go

// formatColonAndQMark_test.go
static void TestFormatColonAndQMark(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class foo {/*1*/
    constructor (n?: number, m = 5, o?: string) { }/*2*/
    x:number = 1?2:3;/*3*/
}/*4*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(class foo {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    constructor(n?: number, m = 5, o?: string) { })TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    x: number = 1 ? 2 : 3;)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatColonAndQMark, TestFormatColonAndQMark);

// formatComments_test.go

// formatComments_test.go
static void TestFormatComments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(_.chain()
// wow/*callChain1*/
  .then()
// waa/*callChain2*/
    .then();
wow(
  3,
// uaa/*argument1*/
    4
// wua/*argument2*/
);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "callChain1");
		f->VerifyCurrentLineContent(t, R"TS(    // wow)TS");
		f->GoToMarker(t, "callChain2");
		f->VerifyCurrentLineContent(t, R"TS(    // waa)TS");
		f->GoToMarker(t, "argument1");
		f->VerifyCurrentLineContent(t, R"TS(    // uaa)TS");
		f->GoToMarker(t, "argument2");
		f->VerifyCurrentLineContent(t, R"TS(    // wua)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatComments, TestFormatComments);

// formatConflictDiff3Marker1_test.go

// formatConflictDiff3Marker1_test.go
static void TestFormatConflictDiff3Marker1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
<<<<<<< HEAD
v = 1;
||||||| merged common ancestors
v = 3;
=======
v = 2;
>>>>>>> Branch - a
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(class C {
<<<<<<< HEAD
v = 1;
||||||| merged common ancestors
v = 3;
=======
v = 2;
>>>>>>> Branch - a
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatConflictDiff3Marker1, TestFormatConflictDiff3Marker1);

// formatConflictMarker1_test.go

// formatConflictMarker1_test.go
static void TestFormatConflictMarker1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
<<<<<<< HEAD
v = 1;
=======
v = 2;
>>>>>>> Branch - a
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(class C {
<<<<<<< HEAD
v = 1;
=======
v = 2;
>>>>>>> Branch - a
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatConflictMarker1, TestFormatConflictMarker1);

// formatControlFlowConstructs_test.go

// formatControlFlowConstructs_test.go
static void TestFormatControlFlowConstructs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true)/**/
{     
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(if (true) {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatControlFlowConstructs, TestFormatControlFlowConstructs);

// formatDebuggerStatement_test.go

// formatDebuggerStatement_test.go
static void TestFormatDebuggerStatement(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if(false){debugger;}
  if    (   false   )   {    debugger  ;   })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToBOF(t);
		f->VerifyCurrentLineContent(t, R"TS(if (false) { debugger; })TS");
		f->GoToEOF(t);
		f->VerifyCurrentLineContent(t, R"TS(if (false) { debugger; })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDebuggerStatement, TestFormatDebuggerStatement);

// formatDocumentGrammarErrorInitializer_test.go

// formatDocumentGrammarErrorInitializer_test.go
static void TestFormatDocumentGrammarErrorInitializer(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = (((((((((std::string(R"TS(// @Filename: /a.ts
)TS") + R"TS(const f = () => {
)TS") + std::string(R"TS(  const v = x as A & {
)TS")) + std::string(R"TS(    a: { b: C
)TS")) + std::string(R"TS(  }
)TS")) + std::string(R"TS(  const m: T[] = [
)TS")) + std::string(R"TS(    { g: () => { nav(`${z}`) } },
)TS")) + std::string(R"TS(  ]
)TS")) + std::string(R"TS(  const n: T[] =
)TS")) + std::string(R"TS(}
)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "/a.ts");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentGrammarErrorInitializer, TestFormatDocumentGrammarErrorInitializer);

// formatDocumentNoCrashJsxAttrUnterminatedString_test.go

// formatDocumentNoCrashJsxAttrUnterminatedString_test.go
static void TestFormatDocumentNoCrashJsxAttrUnterminatedString(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.tsx
const x = <HangupButton customClass = 'ha
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(const x = <HangupButton customClass='ha
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentNoCrashJsxAttrUnterminatedString, TestFormatDocumentNoCrashJsxAttrUnterminatedString);

// formatDocumentNoCrashJsxNamespacedName1_test.go

// formatDocumentNoCrashJsxNamespacedName1_test.go
static void TestFormatDocumentNoCrashJsxNamespacedName1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.tsx
const x = <foo:bar />;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(const x = <foo:bar />;
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentNoCrashJsxNamespacedName1, TestFormatDocumentNoCrashJsxNamespacedName1);

// formatDocumentNoCrashJsxNamespacedName2_test.go

// formatDocumentNoCrashJsxNamespacedName2_test.go
static void TestFormatDocumentNoCrashJsxNamespacedName2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.tsx
const x = <A my-ns:attr="val" />;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(const x = <A my-ns:attr="val" />;
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentNoCrashJsxNamespacedName2, TestFormatDocumentNoCrashJsxNamespacedName2);

// formatDocumentNoCrashLeadingWhitespace1_test.go

// formatDocumentNoCrashLeadingWhitespace1_test.go
static void TestFormatDocumentNoCrashLeadingWhitespace1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS( 
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentNoCrashLeadingWhitespace1, TestFormatDocumentNoCrashLeadingWhitespace1);

// formatDocumentNoCrashLeadingWhitespace2_test.go

// formatDocumentNoCrashLeadingWhitespace2_test.go
static void TestFormatDocumentNoCrashLeadingWhitespace2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS( 
;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentNoCrashLeadingWhitespace2, TestFormatDocumentNoCrashLeadingWhitespace2);

// formatDocumentNoCrashShortLastLine_test.go

// formatDocumentNoCrashShortLastLine_test.go
static void TestFormatDocumentNoCrashShortLastLine(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type X = {
	b})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentNoCrashShortLastLine, TestFormatDocumentNoCrashShortLastLine);

// formatDocumentPreserveTrailingWhitespace_test.go

// formatDocumentPreserveTrailingWhitespace_test.go
static void TestFormatDocumentPreserveTrailingWhitespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
var a;     
var b     
     
//     
function b(){     
    while(true){     
    }     
}     
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts233 = f->GetOptions();
		opts233.FormatCodeSettings.TrimTrailingWhitespace = Tristate::False;
		f->Configure(t, opts233);
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
var a;     
var b     
     
//     
function b() {     
    while (true) {     
    }     
}     
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentPreserveTrailingWhitespace, TestFormatDocumentPreserveTrailingWhitespace);

// formatDocumentWithJSDoc_test.go

// formatDocumentWithJSDoc_test.go
static void TestFormatDocumentWithJSDoc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * JSDoc for things
 */
function f() {
    /** more
        jsdoc */
    var t;
    /**
     * multiline
     */
    var multiline;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(/**
 * JSDoc for things
 */
function f() {
    /** more
        jsdoc */
    var t;
    /**
     * multiline
     */
    var multiline;
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentWithJSDoc, TestFormatDocumentWithJSDoc);

// formatDocumentWithTrivia_test.go

// formatDocumentWithTrivia_test.go
static void TestFormatDocumentWithTrivia(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(  
// 1 below   
    
// 2 above   
    
let x;
  
// abc
  
let y;
  
// 3 above
   
while (true) {
    while (true) {
    }
      
    // 4 above   
}
  
// 5 above  
   
   )TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
// 1 below   

// 2 above   

let x;

// abc

let y;

// 3 above

while (true) {
    while (true) {
    }

    // 4 above   
}

// 5 above  

)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentWithTrivia, TestFormatDocumentWithTrivia);

// formatDocumentZeroTabSize_test.go

// formatDocumentZeroTabSize_test.go
static void TestFormatDocumentZeroTabSize(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo() {
    if (true) {
        var x = 1;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts = f->GetOptions();
		opts.FormatCodeSettings.TabSize = 0;
		opts.FormatCodeSettings.IndentSize = 0;
		opts.FormatCodeSettings.ConvertTabsToSpaces = Tristate::True;
		f->Configure(t, opts);
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(function foo() {
if (true) {
var x = 1;
}
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDocumentZeroTabSize, TestFormatDocumentZeroTabSize);

// formatDotAfterNumber_test.go

// formatDotAfterNumber_test.go
static void TestFormatDotAfterNumber(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(1+ 2 .toString() +3/*1*/
1+ 2. .toString() +3/*2*/
1+ 2.0 .toString() +3/*3*/
1+ (2) .toString() +3/*4*/
1+ 2_000 .toString() +3/*5*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(1 + 2 .toString() + 3)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(1 + 2..toString() + 3)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(1 + 2.0.toString() + 3)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(1 + (2).toString() + 3)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(1 + 2_000 .toString() + 3)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatDotAfterNumber, TestFormatDotAfterNumber);

// formatEmptyBlock_test.go

// formatEmptyBlock_test.go
static void TestFormatEmptyBlock(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS({})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToEOF(t);
		f->Insert(t, R"TS(
)TS");
		f->GoToBOF(t);
		f->VerifyCurrentLineContent(t, R"TS({ })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatEmptyBlock, TestFormatEmptyBlock);

// formatEmptyParamList_test.go

// formatEmptyParamList_test.go
static void TestFormatEmptyParamList(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f( f: function){/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, "}");
		f->VerifyCurrentLineContent(t, R"TS(function f(f: function) { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatEmptyParamList, TestFormatEmptyParamList);

// formatExportAssignment_test.go

// formatExportAssignment_test.go
static void TestFormatExportAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export='foo';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(export = 'foo';)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatExportAssignment, TestFormatExportAssignment);

// formatIfTryCatchBlocks_test.go

// formatIfTryCatchBlocks_test.go
static void TestFormatIfTryCatchBlocks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(try {
}
catch {
}

try {
}
catch (e) {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts187 = f->GetOptions();
		opts187.FormatCodeSettings.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::True;
		f->Configure(t, opts187);
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(try
{
}
catch
{
}

try
{
}
catch (e)
{
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatIfTryCatchBlocks, TestFormatIfTryCatchBlocks);

// formatIfWithEmptyCondition_test.go

// formatIfWithEmptyCondition_test.go
static void TestFormatIfWithEmptyCondition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if () {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts123 = f->GetOptions();
		opts123.FormatCodeSettings.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::True;
		f->Configure(t, opts123);
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(if ()
{
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatIfWithEmptyCondition, TestFormatIfWithEmptyCondition);

// formatImplicitModule_test.go

// formatImplicitModule_test.go
static void TestFormatImplicitModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(       export class A {

       })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToBOF(t);
		f->VerifyCurrentLineContent(t, R"TS(export class A {)TS");
		f->GoToEOF(t);
		f->VerifyCurrentLineContent(t, R"TS(})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatImplicitModule, TestFormatImplicitModule);

// formatImportDeclaration_test.go

// formatImportDeclaration_test.go
static void TestFormatImportDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Foo {/*1*/
}/*2*/

import bar  =    Foo;/*3*/

import bar2=Foo;/*4*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(namespace Foo {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(import bar = Foo;)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(import bar2 = Foo;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatImportDeclaration, TestFormatImportDeclaration);

// formatInTryCatchFinally_test.go

// formatInTryCatchFinally_test.go
static void TestFormatInTryCatchFinally(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(try 
{
    var x = 1/*1*/
}
catch (e) 
{
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(    var x = 1;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatInTryCatchFinally, TestFormatInTryCatchFinally);

// formatInTsxFiles_test.go

// formatInTsxFiles_test.go
static void TestFormatInTsxFiles(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: file.tsx
interface I<T1, T2> {
    next: I</* */
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatInTsxFiles, TestFormatInTsxFiles);

// formatInsertSpaceAfterCloseBraceBeforeCloseBracket_test.go

// formatInsertSpaceAfterCloseBraceBeforeCloseBracket_test.go
static void TestFormatInsertSpaceAfterCloseBraceBeforeCloseBracket(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([{}])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts122 = f->GetOptions();
		opts122.FormatCodeSettings.InsertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets = Tristate::True;
		f->Configure(t, opts122);
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS([ {} ])TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatInsertSpaceAfterCloseBraceBeforeCloseBracket, TestFormatInsertSpaceAfterCloseBraceBeforeCloseBracket);

// formatInterfaceWithMissingBraceAndLaterTemplateString1_test.go

// formatInterfaceWithMissingBraceAndLaterTemplateString1_test.go
static void TestFormatInterfaceWithMissingBraceAndLaterTemplateString1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(
// @Filename: /resource-card.tsx
interface Props {
  iconOnly?: boolean


const ResourceCard: React.FC<Props> = (props) => {
  return (
    <IoLayersOutline
      className={)TS") + "`") + std::string(R"TS(${match ? 'text-primary-foreground' : 'text-foreground'})TS")) + std::string("`")) + std::string(R"TS(}
    />
  )
}

export default ResourceCard
)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, ((((std::string(R"TS(interface Props {
    iconOnly?: boolean


const ResourceCard: React.FC<Props> = (props) => {
    return (
        <IoLayersOutline
            className={)TS") + "`") + std::string(R"TS(${match ? 'text-primary-foreground' : 'text-foreground'})TS")) + std::string("`")) + std::string(R"TS(}
        />
    )
}

export default ResourceCard
)TS")));
	});
}
REGISTER_FOURSLASH_TEST(TestFormatInterfaceWithMissingBraceAndLaterTemplateString1, TestFormatInterfaceWithMissingBraceAndLaterTemplateString1);

// formatInterfaceWithMissingBraceAndLaterTemplateString2_test.go

// formatInterfaceWithMissingBraceAndLaterTemplateString2_test.go
static void TestFormatInterfaceWithMissingBraceAndLaterTemplateString2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(
// @Filename: /FormCheck.tsx
interface FormCheckProps {

const FormCheck: DynamicRefForwardingComponent<'input', FormCheckProps> =
  React.forwardRef(
	    () => {
	      return <div className={)TS") + "`") + std::string(R"TS(${bsPrefix}-reverse)TS")) + std::string("`")) + std::string(R"TS(} />;
    },
  );

FormCheck.displayName = 'FormCheck';
)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, ((((std::string(R"TS(interface FormCheckProps {

const FormCheck: DynamicRefForwardingComponent<'input', FormCheckProps> =
    React.forwardRef(
        () => {
            return <div className={)TS") + "`") + std::string(R"TS(${bsPrefix}-reverse)TS")) + std::string("`")) + std::string(R"TS(} />;
        },
    );

FormCheck.displayName = 'FormCheck';
)TS")));
	});
}
REGISTER_FOURSLASH_TEST(TestFormatInterfaceWithMissingBraceAndLaterTemplateString2, TestFormatInterfaceWithMissingBraceAndLaterTemplateString2);

// formatJsxDottedTagName_test.go

// formatJsxDottedTagName_test.go
static void TestFormatJsxDottedTagName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: file.tsx
const x = (
<a-b.c>
<a-b.c></a-b.c>
</a-b.c>
);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(const x = (
    <a-b.c>
        <a-b.c></a-b.c>
    </a-b.c>
);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatJsxDottedTagName, TestFormatJsxDottedTagName);

// formatJsxWithKeywordInIdentifier_test.go

// formatJsxWithKeywordInIdentifier_test.go
static void TestFormatJsxWithKeywordInIdentifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.tsx
<div module-layout=""></div>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(<div module-layout=""></div>)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatJsxWithKeywordInIdentifier, TestFormatJsxWithKeywordInIdentifier);

// formatLiteralTypeInUnionOrIntersectionType_test.go

// formatLiteralTypeInUnionOrIntersectionType_test.go
static void TestFormatLiteralTypeInUnionOrIntersectionType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type NumberAndString = {
    a: number
} & {
    b: string
};

type NumberOrString = {
    a: number
} | {
    b: string
};

type Complexed =
    Foo &
    Bar |
    Baz;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(type NumberAndString = {
    a: number
} & {
    b: string
};

type NumberOrString = {
    a: number
} | {
    b: string
};

type Complexed =
    Foo &
    Bar |
    Baz;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatLiteralTypeInUnionOrIntersectionType, TestFormatLiteralTypeInUnionOrIntersectionType);

// formatMultilineComment_test.go

// formatMultilineComment_test.go
static void TestFormatMultilineComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*//** 1
 */*2*/2
/*3*/ 3*/

class Foo {
/*4*//**4
    */*5*/5
/*6*/                *6
/*7*/          7*/
    bar() {
/*8*/                /**8
    */*9*/9
/*10*/                *10
/*11*/                           *11
/*12*/          12*/
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(/** 1)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS( *2)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS( 3*/)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(    /**4)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(        *5)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(                    *6)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(              7*/)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(        /**8)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(*9)TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS(        *10)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS(                   *11)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(  12*/)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatMultilineComment, TestFormatMultilineComment);

// formatMultilineImportBrace_test.go

// formatMultilineImportBrace_test.go
static void TestFormatMultilineImportBrace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import {
	basename,
	extname, joinPath } from '../base/resources.js';
import { URI } from '../base/uri.js';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(import {
    basename,
    extname, joinPath
} from '../base/resources.js';
import { URI } from '../base/uri.js';)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatMultilineImportBrace, TestFormatMultilineImportBrace);

// formatMultilineTypesWithMapped_test.go

// formatMultilineTypesWithMapped_test.go
static void TestFormatMultilineTypesWithMapped(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Z = 'z'
type A = {
  a: 'a'
} | {
      [index in Z]: string
  }
type B = {
  b: 'b'
} & {
      [index in Z]: string
  }

const c = {
  c: 'c'
} as const satisfies {
    [index in Z]: string
  }

const d = {
  d: 'd'
} as const satisfies {
  [index: string]: string
}

const e = {
  e: 'e'
} satisfies {
    [index in Z]: string
  }

const f = {
  f: 'f'
} satisfies {
  [index: string]: string
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(type Z = 'z'
type A = {
    a: 'a'
} | {
    [index in Z]: string
}
type B = {
    b: 'b'
} & {
    [index in Z]: string
}

const c = {
    c: 'c'
} as const satisfies {
    [index in Z]: string
}

const d = {
    d: 'd'
} as const satisfies {
    [index: string]: string
}

const e = {
    e: 'e'
} satisfies {
    [index in Z]: string
}

const f = {
    f: 'f'
} satisfies {
    [index: string]: string
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatMultilineTypesWithMapped, TestFormatMultilineTypesWithMapped);

// formatMultipleFunctionArguments_test.go

// formatMultipleFunctionArguments_test.go
static void TestFormatMultipleFunctionArguments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
 someRandomFunction({
   prop1: 1,
   prop2: 2
 }, {
   prop3: 3,
   prop4: 4
 }, {
   prop5: 5,
   prop6: 6
 });

 someRandomFunction(
     { prop7: 1, prop8: 2 },
     { prop9: 3, prop10: 4 },
     {
       prop11: 5,
       prop2: 6
     }
 );)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
someRandomFunction({
    prop1: 1,
    prop2: 2
}, {
    prop3: 3,
    prop4: 4
}, {
    prop5: 5,
    prop6: 6
});

someRandomFunction(
    { prop7: 1, prop8: 2 },
    { prop9: 3, prop10: 4 },
    {
        prop11: 5,
        prop2: 6
    }
);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatMultipleFunctionArguments, TestFormatMultipleFunctionArguments);

// formatNestedClassWithOpenBraceOnNewLines_test.go

// formatNestedClassWithOpenBraceOnNewLines_test.go
static void TestFormatNestedClassWithOpenBraceOnNewLines(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(module A
{
    class B {
        /*1*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts168 = f->GetOptions();
		opts168.FormatCodeSettings.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::True;
		f->Configure(t, opts168);
		auto opts232 = f->GetOptions();
		opts232.FormatCodeSettings.PlaceOpenBraceOnNewLineForFunctions = Tristate::True;
		f->Configure(t, opts232);
		f->GoToMarker(t, "1");
		f->Insert(t, "}");
		f->VerifyCurrentFileContent(t, R"TS(module A
{
    class B
    {
    }
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatNestedClassWithOpenBraceOnNewLines, TestFormatNestedClassWithOpenBraceOnNewLines);

// formatNoSpaceAfterTemplateHeadAndMiddle_test.go

// formatNoSpaceAfterTemplateHeadAndMiddle_test.go
static void TestFormatNoSpaceAfterTemplateHeadAndMiddle(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((((((((((((((((((std::string(R"TS(const a1 = )TS") + "`") + std::string(R"TS(${ 1 }${ 1 })TS")) + std::string("`")) + std::string(R"TS(;
const a2 = )TS")) + std::string("`")) + std::string(R"TS(
    ${ 1 }${ 1 }
)TS")) + std::string("`")) + std::string(R"TS(;
const a3 = )TS")) + std::string("`")) + std::string(R"TS(


    ${ 1 }${ 1 }
)TS")) + std::string("`")) + std::string(R"TS(;
const a4 = )TS")) + std::string("`")) + std::string(R"TS(

    ${ 1 }${ 1 }

)TS")) + std::string("`")) + std::string(R"TS(;
const a5 = )TS")) + std::string("`")) + std::string(R"TS(text ${ 1 } text ${ 1 } text)TS")) + std::string("`")) + std::string(R"TS(;
const a6 = )TS")) + std::string("`")) + std::string(R"TS(
    text ${ 1 }
    text ${ 1 }
    text
)TS")) + std::string("`")) + std::string(R"TS(;)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts429 = f->GetOptions();
		opts429.FormatCodeSettings.InsertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces = Tristate::False;
		f->Configure(t, opts429);
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, (((((((((((((((((((std::string(R"TS(const a1 = `${1}${1}`;
)TS") + R"TS(const a2 = `
)TS") + std::string(R"TS(    ${1}${1}
)TS")) + std::string(R"TS(`;
)TS")) + std::string(R"TS(const a3 = `
)TS")) + std::string(R"TS(
)TS")) + std::string(R"TS(
)TS")) + std::string(R"TS(    ${1}${1}
)TS")) + std::string(R"TS(`;
)TS")) + std::string(R"TS(const a4 = `
)TS")) + std::string(R"TS(
)TS")) + std::string(R"TS(    ${1}${1}
)TS")) + std::string(R"TS(
)TS")) + std::string(R"TS(`;
)TS")) + std::string(R"TS(const a5 = `text ${1} text ${1} text`;
)TS")) + std::string(R"TS(const a6 = `
)TS")) + std::string(R"TS(    text ${1}
)TS")) + std::string(R"TS(    text ${1}
)TS")) + std::string(R"TS(    text
)TS")) + std::string("`;")));
	});
}
REGISTER_FOURSLASH_TEST(TestFormatNoSpaceAfterTemplateHeadAndMiddle, TestFormatNoSpaceAfterTemplateHeadAndMiddle);

// formatNoSpaceBeforeCloseBrace1_test.go

// formatNoSpaceBeforeCloseBrace1_test.go
static void TestFormatNoSpaceBeforeCloseBrace1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(new Foo(1, /* comment */    );)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(new Foo(1, /* comment */);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatNoSpaceBeforeCloseBrace1, TestFormatNoSpaceBeforeCloseBrace1);

// formatNoSpaceBeforeCloseBrace2_test.go

// formatNoSpaceBeforeCloseBrace2_test.go
static void TestFormatNoSpaceBeforeCloseBrace2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(new Foo(1,     );)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(new Foo(1,);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatNoSpaceBeforeCloseBrace2, TestFormatNoSpaceBeforeCloseBrace2);

// formatNoSpaceBeforeCloseBrace3_test.go

// formatNoSpaceBeforeCloseBrace3_test.go
static void TestFormatNoSpaceBeforeCloseBrace3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(foo( 
 1, /* comment */    );)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(foo(
    1, /* comment */);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatNoSpaceBeforeCloseBrace3, TestFormatNoSpaceBeforeCloseBrace3);

// formatNoSpaceBeforeCloseBrace4_test.go

// formatNoSpaceBeforeCloseBrace4_test.go
static void TestFormatNoSpaceBeforeCloseBrace4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(new Foo(1
, /* comment */    );)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(new Foo(1
    , /* comment */);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatNoSpaceBeforeCloseBrace4, TestFormatNoSpaceBeforeCloseBrace4);

// formatNoSpaceBeforeCloseBrace5_test.go

// formatNoSpaceBeforeCloseBrace5_test.go
static void TestFormatNoSpaceBeforeCloseBrace5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(new Foo(1, 
    /* comment */    );)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(new Foo(1,
    /* comment */);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatNoSpaceBeforeCloseBrace5, TestFormatNoSpaceBeforeCloseBrace5);

// formatNoSpaceBeforeCloseBrace6_test.go

// formatNoSpaceBeforeCloseBrace6_test.go
static void TestFormatNoSpaceBeforeCloseBrace6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(new Foo(1, /* comment */  
  );)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(new Foo(1, /* comment */
);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatNoSpaceBeforeCloseBrace6, TestFormatNoSpaceBeforeCloseBrace6);

// formatNoSpaceBeforeCloseBrace_test.go

// formatNoSpaceBeforeCloseBrace_test.go
static void TestFormatNoSpaceBeforeCloseBrace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(foo(1, /* comment */    );)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(foo(1, /* comment */);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatNoSpaceBeforeCloseBrace, TestFormatNoSpaceBeforeCloseBrace);

// formatNoSpaceBetweenClosingParenAndTemplateString_test.go

// formatNoSpaceBetweenClosingParenAndTemplateString_test.go
static void TestFormatNoSpaceBetweenClosingParenAndTemplateString(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((((((std::string(R"TS(foo() )TS") + "`") + std::string(R"TS(abc)TS")) + std::string("`")) + std::string(R"TS(;
bar())TS")) + std::string("`")) + std::string(R"TS(def)TS")) + std::string("`")) + std::string(R"TS(;
baz())TS")) + std::string("`")) + std::string(R"TS(a${x}b)TS")) + std::string("`")) + std::string(R"TS(;)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(foo()`abc`;
bar()`def`;
baz()`a${x}b`;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatNoSpaceBetweenClosingParenAndTemplateString, TestFormatNoSpaceBetweenClosingParenAndTemplateString);

// formatObjectBindingPattern_restElementWithPropertyName_test.go

// formatObjectBindingPattern_restElementWithPropertyName_test.go
static void TestFormatObjectBindingPattern_restElementWithPropertyName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const { ...a: b } = {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(const { ...a: b } = {};)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatObjectBindingPattern_restElementWithPropertyName, TestFormatObjectBindingPattern_restElementWithPropertyName);

// formatObjectBindingPattern_test.go

// formatObjectBindingPattern_test.go
static void TestFormatObjectBindingPattern(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const {
x,
y,
} = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(const {
    x,
    y,
} = 0;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatObjectBindingPattern, TestFormatObjectBindingPattern);

// formatOnEnterFunctionDeclaration_test.go

// formatOnEnterFunctionDeclaration_test.go
static void TestFormatOnEnterFunctionDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*0*/function listAPIFiles(path: string): string[] {/*1*/ })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->InsertLine(t, "");
		f->GoToMarker(t, "0");
		f->VerifyCurrentLineContent(t, R"TS(function listAPIFiles(path: string): string[] {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatOnEnterFunctionDeclaration, TestFormatOnEnterFunctionDeclaration);

// formatOnEnterInComment_test.go

// formatOnEnterInComment_test.go
static void TestFormatOnEnterInComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(   /**
    * /*1*/
    */)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->InsertLine(t, "");
		f->VerifyCurrentFileContent(t, R"TS(  /**
   * 

   */)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatOnEnterInComment, TestFormatOnEnterInComment);

// formatOnEnterOpenBraceAddNewLine_test.go

// formatOnEnterOpenBraceAddNewLine_test.go
static void TestFormatOnEnterOpenBraceAddNewLine(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if(true) {/*0*/}
if(false)/*1*/{
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts148 = f->GetOptions();
		opts148.FormatCodeSettings.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::True;
		f->Configure(t, opts148);
		f->GoToMarker(t, "0");
		f->InsertLine(t, "");
		f->VerifyCurrentFileContent(t, R"TS(if (true)
{
}
if(false){
})TS");
		f->GoToMarker(t, "1");
		f->InsertLine(t, "");
		f->VerifyCurrentFileContent(t, R"TS(if (true)
{
}
if (false)
{
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatOnEnterOpenBraceAddNewLine, TestFormatOnEnterOpenBraceAddNewLine);

// formatOnOpenCurlyBraceRemoveNewLine_test.go

// formatOnOpenCurlyBraceRemoveNewLine_test.go
static void TestFormatOnOpenCurlyBraceRemoveNewLine(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if(true)
/**/ })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts124 = f->GetOptions();
		opts124.FormatCodeSettings.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::False;
		f->Configure(t, opts124);
		f->GoToMarker(t, "");
		f->Insert(t, "{");
		f->VerifyCurrentFileContent(t, R"TS(if (true) { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatOnOpenCurlyBraceRemoveNewLine, TestFormatOnOpenCurlyBraceRemoveNewLine);

// formatOnSemiColonAfterBreak_test.go

// formatOnSemiColonAfterBreak_test.go
static void TestFormatOnSemiColonAfterBreak(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(for (var a in b) {
break/**/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(    break;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatOnSemiColonAfterBreak, TestFormatOnSemiColonAfterBreak);

// formatParameter_test.go

// formatParameter_test.go
static void TestFormatParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(
    first:
    number,/*first*/
    second: (
    string/*second*/
    ),
    third:
    (
    boolean/*third*/
    )
) {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "first");
		f->VerifyCurrentLineContent(t, R"TS(        number,)TS");
		f->GoToMarker(t, "second");
		f->VerifyCurrentLineContent(t, R"TS(        string)TS");
		f->GoToMarker(t, "third");
		f->VerifyCurrentLineContent(t, R"TS(            boolean)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatParameter, TestFormatParameter);

// formatRangeEndingAfterCommaOfCall_test.go

// formatRangeEndingAfterCommaOfCall_test.go
static void TestFormatRangeEndingAfterCommaOfCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(someCall(
    /*start*/"firstParameter",/*end*/
    "something else"
);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "start", "end");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatRangeEndingAfterCommaOfCall, TestFormatRangeEndingAfterCommaOfCall);

// formatRemoveNewLineAfterOpenBrace_test.go

// formatRemoveNewLineAfterOpenBrace_test.go
static void TestFormatRemoveNewLineAfterOpenBrace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo()
{
}
if (true)
{
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(function foo() {
}
if (true) {
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatRemoveNewLineAfterOpenBrace, TestFormatRemoveNewLineAfterOpenBrace);

// formatRemoveSpaceBetweenDotDotDotAndTypeName_test.go

// formatRemoveSpaceBetweenDotDotDotAndTypeName_test.go
static void TestFormatRemoveSpaceBetweenDotDotDotAndTypeName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let a: [... any[]];
let b: [...   number[]];
let c: [...     string[]];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(let a: [...any[]];
let b: [...number[]];
let c: [...string[]];)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatRemoveSpaceBetweenDotDotDotAndTypeName, TestFormatRemoveSpaceBetweenDotDotDotAndTypeName);

// formatSatisfiesExpression_test.go

// formatSatisfiesExpression_test.go
static void TestFormatSatisfiesExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Foo = "a" | "b" | "c";
const foo1 = ["a"] satisfies Foo[];
const foo2 = ["a"]satisfies Foo[];
const foo3 = ["a"]  satisfies Foo[];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(type Foo = "a" | "b" | "c";
const foo1 = ["a"] satisfies Foo[];
const foo2 = ["a"] satisfies Foo[];
const foo3 = ["a"] satisfies Foo[];)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSatisfiesExpression, TestFormatSatisfiesExpression);

// formatSelectionAfterTemplateLiteral1_test.go

// formatSelectionAfterTemplateLiteral1_test.go
static void TestFormatSelectionAfterTemplateLiteral1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const a = `head${"x"};
`;

/*begin*/export const f = () => {
    return `world`;
/*end*/}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, R"TS(const a = `head${"x"};
`;

export const f = () => {
    return `world`;
}
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionAfterTemplateLiteral1, TestFormatSelectionAfterTemplateLiteral1);

// formatSelectionDocCommentInBlock_test.go

// formatSelectionDocCommentInBlock_test.go
static void TestFormatSelectionDocCommentInBlock(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS({
    /*1*//**
     * Some doc comment
     *//*2*/
    const a = 1;
}

while (true) {
/*3*//**
 * Some doc comment
 *//*4*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "1", "2");
		f->VerifyCurrentFileContent(t, R"TS({
    /**
     * Some doc comment
     */
    const a = 1;
}

while (true) {
/**
 * Some doc comment
 */
})TS");
		f->FormatSelection(t, "3", "4");
		f->VerifyCurrentFileContent(t, R"TS({
    /**
     * Some doc comment
     */
    const a = 1;
}

while (true) {
    /**
     * Some doc comment
     */
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionDocCommentInBlock, TestFormatSelectionDocCommentInBlock);

// formatSelectionEditAtEndOfRange_test.go

// formatSelectionEditAtEndOfRange_test.go
static void TestFormatSelectionEditAtEndOfRange(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/var x = 1;/*2*/
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts110 = f->GetOptions();
		opts110.FormatCodeSettings.Semicolons = lsutil::SemicolonPreferenceRemove;
		f->Configure(t, opts110);
		f->FormatSelection(t, "1", "2");
		f->VerifyCurrentFileContent(t, R"TS(var x = 1
void 0;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionEditAtEndOfRange, TestFormatSelectionEditAtEndOfRange);

// formatSelectionInJSDocTypeLiteralNoCrash1_test.go

// formatSelectionInJSDocTypeLiteralNoCrash1_test.go
static void TestFormatSelectionInJSDocTypeLiteralNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = (((((((((((std::string(R"TS(// @allowJs: true
)TS") + R"TS(// @filename: index.js
)TS") + std::string(R"TS(/**
)TS")) + std::string(R"TS( *
)TS")) + std::string(R"TS( *
)TS")) + std::string(R"TS( * @typedef {Object} Fixture
)TS")) + std::string(R"TS( * @property {typeof build} build
)TS")) + std::string(R"TS(/*begin*/ * @property {(url: string) => string} resolveUrl
)TS")) + std::string(R"TS( * @property {() => Promise<void>} clean
)TS")) + std::string(R"TS(/*end*/ * @property {(streaming?: boolean) => Promise<App>} loadTestAdapterApp
)TS")) + std::string(R"TS( */
)TS")) + std::string(R"TS(
)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, (((((((((std::string(R"TS(/**
)TS") + R"TS( *
)TS") + std::string(R"TS( *
)TS")) + std::string(R"TS( * @typedef {Object} Fixture
)TS")) + std::string(R"TS( * @property {typeof build} build
)TS")) + std::string(R"TS( * @property {(url: string) => string} resolveUrl
)TS")) + std::string(R"TS( * @property {() => Promise<void>} clean
)TS")) + std::string(R"TS( * @property {(streaming?: boolean) => Promise<App>} loadTestAdapterApp
)TS")) + std::string(R"TS( */
)TS")) + std::string(R"TS(
)TS")));
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionInJSDocTypeLiteralNoCrash1, TestFormatSelectionInJSDocTypeLiteralNoCrash1);

// formatSelectionJsxWithBinaryExpression_test.go

// formatSelectionJsxWithBinaryExpression_test.go
static void TestFormatSelectionJsxWithBinaryExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: file.tsx
function TestWidget() {
    const test = true;
    return (
        <div>
            {test &&
                <div>
 /*1*/                <div>some text</div>/*2*/
                    <div>some text</div>
                    <div>some text</div>
                </div>
            }
            <div>some text</div>
        </div>
    );
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "1", "2");
		f->VerifyCurrentFileContent(t, R"TS(function TestWidget() {
    const test = true;
    return (
        <div>
            {test &&
                <div>
                    <div>some text</div>
                    <div>some text</div>
                    <div>some text</div>
                </div>
            }
            <div>some text</div>
        </div>
    );
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionJsxWithBinaryExpression, TestFormatSelectionJsxWithBinaryExpression);

// formatSelectionPreserveTrailingWhitespace_test.go

// formatSelectionPreserveTrailingWhitespace_test.go
static void TestFormatSelectionPreserveTrailingWhitespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
/*begin*/;    
    
/*end*/    
    
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts154 = f->GetOptions();
		opts154.FormatCodeSettings.TrimTrailingWhitespace = Tristate::False;
		f->Configure(t, opts154);
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, R"TS(
;    
    
    
    
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionPreserveTrailingWhitespace, TestFormatSelectionPreserveTrailingWhitespace);

// formatSelectionSingleProperty_test.go

// formatSelectionSingleProperty_test.go
static void TestFormatSelectionSingleProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(console.log({
}, {
/*1*/    a: 1,
/*2*/    b: 2
}))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "1", "2");
		f->VerifyCurrentFileContent(t, R"TS(console.log({
}, {
    a: 1,
    b: 2
}))TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionSingleProperty, TestFormatSelectionSingleProperty);

// formatSelectionUnterminatedBacktickInJSDocNoCrash1_test.go

// formatSelectionUnterminatedBacktickInJSDocNoCrash1_test.go
static void TestFormatSelectionUnterminatedBacktickInJSDocNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = (((((((((((((((std::string(R"TS(// @allowJs: true
)TS") + R"TS(// @filename: index.js
)TS") + std::string(R"TS(export class Manifest {
)TS")) + std::string(R"TS(  /**
)TS")) + std::string(R"TS(   * @template {ExtensionType} ExtType
)TS")) + std::string(R"TS(   * @param {ExtType} extType - `dri
)TS")) + std::string(R"TS(/*begin*/   * @param {string} extName
)TS")) + std::string(R"TS(   */
)TS")) + std::string(R"TS(  setExtension(extType, extName, extData) {
)TS")) + std::string(R"TS(        const data = _.cloneDeep(extData);
)TS")) + std::string(R"TS(    this.#data[`${extType}s`][extName] = data;
)TS")) + std::string(R"TS(    return data;
)TS")) + std::string(R"TS(  }
)TS")) + std::string(R"TS(/*end*/
)TS")) + std::string(R"TS(}
)TS")) + std::string(R"TS(
)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, (((((((((((((std::string(R"TS(export class Manifest {
)TS") + R"TS(  /**
)TS") + std::string(R"TS(   * @template {ExtensionType} ExtType
)TS")) + std::string(R"TS(   * @param {ExtType} extType - `dri
)TS")) + std::string(R"TS(   * @param {string} extName
)TS")) + std::string(R"TS(   */
)TS")) + std::string(R"TS(    setExtension(extType, extName, extData) {
)TS")) + std::string(R"TS(        const data = _.cloneDeep(extData);
)TS")) + std::string(R"TS(        this.#data[`${extType}s`][extName] = data;
)TS")) + std::string(R"TS(        return data;
)TS")) + std::string(R"TS(    }
)TS")) + std::string(R"TS(
)TS")) + std::string(R"TS(}
)TS")) + std::string(R"TS(
)TS")));
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionUnterminatedBacktickInJSDocNoCrash1, TestFormatSelectionUnterminatedBacktickInJSDocNoCrash1);

// formatSelectionWithTrivia2_test.go

// formatSelectionWithTrivia2_test.go
static void TestFormatSelectionWithTrivia2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*begin*/;    
    
/*end*/    
    )TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, R"TS(;


    )TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionWithTrivia2, TestFormatSelectionWithTrivia2);

// formatSelectionWithTrivia3_test.go

// formatSelectionWithTrivia3_test.go
static void TestFormatSelectionWithTrivia3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) {
/*begin*/// test comment
/*end*/})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, R"TS(if (true) {
    // test comment
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionWithTrivia3, TestFormatSelectionWithTrivia3);

// formatSelectionWithTrivia4_test.go

// formatSelectionWithTrivia4_test.go
static void TestFormatSelectionWithTrivia4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) {
/*begin*/// test comment
/*end*/console.log();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, R"TS(if (true) {
    // test comment
console.log();
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionWithTrivia4, TestFormatSelectionWithTrivia4);

// formatSelectionWithTrivia5_test.go

// formatSelectionWithTrivia5_test.go
static void TestFormatSelectionWithTrivia5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) {
/*begin*/// test comment
/*end*/    console.log();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, R"TS(if (true) {
    // test comment
    console.log();
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionWithTrivia5, TestFormatSelectionWithTrivia5);

// formatSelectionWithTrivia6_test.go

// formatSelectionWithTrivia6_test.go
static void TestFormatSelectionWithTrivia6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*begin*/    // test comment
/*end*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, R"TS(// test comment
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionWithTrivia6, TestFormatSelectionWithTrivia6);

// formatSelectionWithTrivia7_test.go

// formatSelectionWithTrivia7_test.go
static void TestFormatSelectionWithTrivia7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) {
/*begin*/// test comment/*end*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, R"TS(if (true) {
    // test comment
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionWithTrivia7, TestFormatSelectionWithTrivia7);

// formatSelectionWithTrivia8_test.go

// formatSelectionWithTrivia8_test.go
static void TestFormatSelectionWithTrivia8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*begin*/;
    
/*end*/console.log();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, R"TS(;

console.log();)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionWithTrivia8, TestFormatSelectionWithTrivia8);

// formatSelectionWithTrivia_test.go

// formatSelectionWithTrivia_test.go
static void TestFormatSelectionWithTrivia(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) {     
  //   
   /*begin*/   
     //    
     ;    
       
      }/*end*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "begin", "end");
		f->VerifyCurrentFileContent(t, R"TS(if (true) {     
  //   

    //    
    ;

})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSelectionWithTrivia, TestFormatSelectionWithTrivia);

// formatSimulatingScriptBlocks_test.go

// formatSimulatingScriptBlocks_test.go
static void TestFormatSimulatingScriptBlocks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/* BEGIN EXTERNAL SOURCE */
/*begin5*/
                        var a = 1;
                        alert("/*end5*//********//*begin4*/");
                    /*end4*/
/* END EXTERNAL SOURCE */

/* BEGIN EXTERNAL SOURCE */
/*begin3*/
                            var b = 1;

                        var c = "/*end3*//********//*begin2*/";
       var d = 1;

            var e = "/*end2*//********//*begin1*/";
            var f = 1;
        /*end1*/
/* END EXTERNAL SOURCE */)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts640 = f->GetOptions();
		opts640.FormatCodeSettings.BaseIndentSize = 12;
		f->Configure(t, opts640);
		f->FormatSelection(t, "begin1", "end1");
		f->FormatSelection(t, "begin2", "end2");
		f->FormatSelection(t, "begin3", "end3");
		auto opts794 = f->GetOptions();
		opts794.FormatCodeSettings.BaseIndentSize = 24;
		f->Configure(t, opts794);
		f->FormatSelection(t, "begin4", "end4");
		f->FormatSelection(t, "begin5", "end5");
		f->VerifyCurrentFileContent(t, R"TS(/* BEGIN EXTERNAL SOURCE */

                        var a = 1;
                        alert("/********/");

/* END EXTERNAL SOURCE */

/* BEGIN EXTERNAL SOURCE */

            var b = 1;

            var c = "/********/";
            var d = 1;

            var e = "/********/";
            var f = 1;

/* END EXTERNAL SOURCE */)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSimulatingScriptBlocks, TestFormatSimulatingScriptBlocks);

// formatSpaceAfterImplementsExtends_test.go

// formatSpaceAfterImplementsExtends_test.go
static void TestFormatSpaceAfterImplementsExtends(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C1 implements Array<string>{
}

class C2 implements Number{
}

class C3 extends Array<string>{
}

class C4 extends Number{
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(class C1 implements Array<string> {
}

class C2 implements Number {
}

class C3 extends Array<string> {
}

class C4 extends Number {
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSpaceAfterImplementsExtends, TestFormatSpaceAfterImplementsExtends);

// formatSpaceAfterTemplateHeadAndMiddle_test.go

// formatSpaceAfterTemplateHeadAndMiddle_test.go
static void TestFormatSpaceAfterTemplateHeadAndMiddle(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((((((((((((((((((std::string(R"TS(const a1 = )TS") + "`") + std::string(R"TS(${1}${1})TS")) + std::string("`")) + std::string(R"TS(;
const a2 = )TS")) + std::string("`")) + std::string(R"TS(
    ${1}${1}
)TS")) + std::string("`")) + std::string(R"TS(;
const a3 = )TS")) + std::string("`")) + std::string(R"TS(


    ${1}${1}
)TS")) + std::string("`")) + std::string(R"TS(;
const a4 = )TS")) + std::string("`")) + std::string(R"TS(

    ${1}${1}

)TS")) + std::string("`")) + std::string(R"TS(;
const a5 = )TS")) + std::string("`")) + std::string(R"TS(text ${1} text ${1} text)TS")) + std::string("`")) + std::string(R"TS(;
const a6 = )TS")) + std::string("`")) + std::string(R"TS(
    text ${1}
    text ${1}
    text
)TS")) + std::string("`")) + std::string(R"TS(;)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts405 = f->GetOptions();
		opts405.FormatCodeSettings.InsertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces = Tristate::True;
		f->Configure(t, opts405);
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, (((((((((((((((((((std::string(R"TS(const a1 = `${ 1 }${ 1 }`;
)TS") + R"TS(const a2 = `
)TS") + std::string(R"TS(    ${ 1 }${ 1 }
)TS")) + std::string(R"TS(`;
)TS")) + std::string(R"TS(const a3 = `
)TS")) + std::string(R"TS(
)TS")) + std::string(R"TS(
)TS")) + std::string(R"TS(    ${ 1 }${ 1 }
)TS")) + std::string(R"TS(`;
)TS")) + std::string(R"TS(const a4 = `
)TS")) + std::string(R"TS(
)TS")) + std::string(R"TS(    ${ 1 }${ 1 }
)TS")) + std::string(R"TS(
)TS")) + std::string(R"TS(`;
)TS")) + std::string(R"TS(const a5 = `text ${ 1 } text ${ 1 } text`;
)TS")) + std::string(R"TS(const a6 = `
)TS")) + std::string(R"TS(    text ${ 1 }
)TS")) + std::string(R"TS(    text ${ 1 }
)TS")) + std::string(R"TS(    text
)TS")) + std::string("`;")));
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSpaceAfterTemplateHeadAndMiddle, TestFormatSpaceAfterTemplateHeadAndMiddle);

// formatSpaceBetweenFunctionAndArrayIndex_test.go

// formatSpaceBetweenFunctionAndArrayIndex_test.go
static void TestFormatSpaceBetweenFunctionAndArrayIndex(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5

function test() {
    return [];
}

test() [0]
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
function test() {
    return [];
}

test()[0]
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatSpaceBetweenFunctionAndArrayIndex, TestFormatSpaceBetweenFunctionAndArrayIndex);

// formatTSXWithInlineComment_test.go

// formatTSXWithInlineComment_test.go
static void TestFormatTSXWithInlineComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.tsx
const a = <div>
    // <a />
</div>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(const a = <div>
    // <a />
</div>)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTSXWithInlineComment, TestFormatTSXWithInlineComment);

// formatTemplateStringOnPaste_test.go

// formatTemplateStringOnPaste_test.go
static void TestFormatTemplateStringOnPaste(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(const x = )TS") + "`") + std::string(R"TS(${0}/*0*/abc/*1*/)TS")) + std::string("`")) + std::string(R"TS(;)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "0", "1");
		f->VerifyCurrentFileContent(t, "const x = `${0}abc`;");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTemplateStringOnPaste, TestFormatTemplateStringOnPaste);

// formatTrimRemainingRange_test.go

// formatTrimRemainingRange_test.go
static void TestFormatTrimRemainingRange(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
    ;
    /*
    
*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(;
/*
 
*/)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTrimRemainingRange, TestFormatTrimRemainingRange);

// formatTryCatch_test.go

// formatTryCatch_test.go
static void TestFormatTryCatch(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function test() {
    /*try*/try {
    }
    /*catch*/catch (e) {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->FormatDocument(t, "");
		f->FormatDocument(t, "");
		f->GoToMarker(t, "try");
		f->VerifyCurrentLineContent(t, R"TS(    try {)TS");
		f->GoToMarker(t, "catch");
		f->VerifyCurrentLineContent(t, R"TS(    catch (e) {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTryCatch, TestFormatTryCatch);

// formatTryFinally_test.go

// formatTryFinally_test.go
static void TestFormatTryFinally(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) try  {
    // ...
}   finally    {
    // ...
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(if (true) try {
    // ...
} finally {
    // ...
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTryFinally, TestFormatTryFinally);

// formatTsxClosingAfterJsxText_test.go

// formatTsxClosingAfterJsxText_test.go
static void TestFormatTsxClosingAfterJsxText(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.tsx

const a = (
    <div>
        text
               </div>
)
const b = (
    <div>
        text
      twice
               </div>
)
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
const a = (
    <div>
        text
    </div>
)
const b = (
    <div>
        text
        twice
    </div>
)
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTsxClosingAfterJsxText, TestFormatTsxClosingAfterJsxText);

// formatTsxMultilineAttributeString_test.go

// formatTsxMultilineAttributeString_test.go
static void TestFormatTsxMultilineAttributeString(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.tsx
(
    <input
        value="x
        x"
    />
);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS((
    <input
        value="x
        x"
    />
);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTsxMultilineAttributeString, TestFormatTsxMultilineAttributeString);

// formatTsx_test.go

// formatTsx_test.go
static void TestFormatTsx(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.tsx
<div><p>'</p><p>{function(){return 1;}]}</p></div>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(<div><p>'</p><p>{function() { return 1; }]}</p></div>)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTsx, TestFormatTsx);

// formatTypeAnnotation1_test.go

// formatTypeAnnotation1_test.go
static void TestFormatTypeAnnotation1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(x: number, y?: string): number {}
interface Foo {
    x: number;
    y?: number;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts207 = f->GetOptions();
		opts207.FormatCodeSettings.InsertSpaceBeforeTypeAnnotation = Tristate::True;
		f->Configure(t, opts207);
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(function foo(x : number, y ?: string) : number { }
interface Foo {
    x : number;
    y ?: number;
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTypeAnnotation1, TestFormatTypeAnnotation1);

// formatTypeAnnotation2_test.go

// formatTypeAnnotation2_test.go
static void TestFormatTypeAnnotation2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(x : number, y ?: string) : number {}
interface Foo {
    x : number;
    y ?: number;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(function foo(x: number, y?: string): number { }
interface Foo {
    x: number;
    y?: number;
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTypeAnnotation2, TestFormatTypeAnnotation2);

// formatTypeArgumentOnNewLine_test.go

// formatTypeArgumentOnNewLine_test.go
static void TestFormatTypeArgumentOnNewLine(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const genericObject = new GenericObject<
  /*1*/{}
>();
const genericObject2 = new GenericObject2<
  /*2*/{},
  /*3*/{}
>();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    {})TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    {},)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    {})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTypeArgumentOnNewLine, TestFormatTypeArgumentOnNewLine);

// formatTypeParameters_test.go

// formatTypeParameters_test.go
static void TestFormatTypeParameters(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**/type Bar<T extends any[]= any[]> = T)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(type Bar<T extends any[] = any[]> = T)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatTypeParameters, TestFormatTypeParameters);

// formatV8Directive_test.go

// formatV8Directive_test.go
static void TestFormatV8Directive(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.js
function foo() {}
/*1*/%PrepareFunctionForOptimization(foo)/*2*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "1", "2");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatV8Directive, TestFormatV8Directive);

// formatVariableDeclarationList_test.go

// formatVariableDeclarationList_test.go
static void TestFormatVariableDeclarationList(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/var   fun1   =   function   (     )     {
/*2*/            var               x   =   'foo'             ,
/*3*/                z   =   'bar'           ;
/*4*/                return  x            ;
/*5*/},

/*6*/fun2   =   (                function        (   f               )   {
/*7*/            var   fun   =   function   (        )       {
/*8*/                        console         .  log             (           f     (  )  )       ;
/*9*/            },
/*10*/            x   =   'Foo'           ;
/*11*/                return   fun            ;
/*12*/}   (           fun1            )   )       ;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(var fun1 = function() {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    var x = 'foo',)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(        z = 'bar';)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(    return x;)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(},)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    fun2 = (function(f) {)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(        var fun = function() {)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(            console.log(f());)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(        },)TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS(            x = 'Foo';)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS(        return fun;)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(    }(fun1));)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatVariableDeclarationList, TestFormatVariableDeclarationList);

// formatWithStatement_test.go

// formatWithStatement_test.go
static void TestFormatWithStatement(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(with /*1*/(foo.bar)

   {/*2*/

     }/*3*/

with (bar.blah)/*4*/
{/*5*/
}/*6*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts227 = f->GetOptions();
		opts227.FormatCodeSettings.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::False;
		f->Configure(t, opts227);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(with (foo.bar) {)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(with (bar.blah) {)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		auto opts565 = f->GetOptions();
		opts565.FormatCodeSettings.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::True;
		f->Configure(t, opts565);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(with (foo.bar))TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS({)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(with (bar.blah))TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS({)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatWithStatement, TestFormatWithStatement);

// formatonkey01_test.go

// formatonkey01_test.go
static void TestFormatonkey01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
switch (1) {
    case 1:
        {
            /*1*/
        break;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "1");
		f->Insert(t, "}");
		f->VerifyCurrentLineContent(t, R"TS(        })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormatonkey01, TestFormatonkey01);

// formattingAfterChainedFatArrow_test.go

// formattingAfterChainedFatArrow_test.go
static void TestFormattingAfterChainedFatArrow(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = n => p => {
    while (true) {
        void 0;
    }/**/
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->FormatDocument(t, "");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingAfterChainedFatArrow, TestFormattingAfterChainedFatArrow);

// formattingAfterMultiLineIfCondition_test.go

// formattingAfterMultiLineIfCondition_test.go
static void TestFormattingAfterMultiLineIfCondition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS( var foo;
 if (foo &&
     foo) {
/*comment*/     // This is a comment
     foo.toString();
 /**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, "}");
		f->GoToMarker(t, "comment");
		f->VerifyCurrentLineContent(t, R"TS(    // This is a comment)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingAfterMultiLineIfCondition, TestFormattingAfterMultiLineIfCondition);

// formattingAfterMultiLineString_test.go

// formattingAfterMultiLineString_test.go
static void TestFormattingAfterMultiLineString(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class foo {
    stop() {
        var s = "hello\/*1*/
"/*2*/
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "2");
		f->InsertLine(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(        var s = "hello\)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingAfterMultiLineString, TestFormattingAfterMultiLineString);

// formattingArrayLiteral_test.go

// formattingArrayLiteral_test.go
static void TestFormattingArrayLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/x= [];
y = [
/*2*/           1,
/*3*/  2
/*4*/ ];

z = [[
/*5*/  1,
/*6*/             2
/*7*/      ]  ];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(x = [];)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    1,)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    2)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(];)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    1,)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    2)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(]];)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingArrayLiteral, TestFormattingArrayLiteral);

// formattingAwait_test.go

// formattingAwait_test.go
static void TestFormattingAwait(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(async function f() {
    for          await (const x of g()) {
        console.log(x);
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(async function f() {
    for await (const x of g()) {
        console.log(x);
    }
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingAwait, TestFormattingAwait);

// formattingBlockInCaseClauses_test.go

// formattingBlockInCaseClauses_test.go
static void TestFormattingBlockInCaseClauses(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(switch (1) {
    case 1:
        {
            /*1*/
        break;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, "}");
		f->VerifyCurrentLineContent(t, R"TS(        })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingBlockInCaseClauses, TestFormattingBlockInCaseClauses);

// formattingChainingMethods_test.go

// formattingChainingMethods_test.go
static void TestFormattingChainingMethods(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS( z$ = this.store.select(this.fake())
     .ofType(
      'ACTION',
      'ACTION-2'
     )
     .pipe(
         filter(x => !!x),
         switchMap(() =>
          this.store.select(this.menuSelector.getAll('x'))
           .pipe(
             tap(x => {
             this.x = !x;
             })
           )
         )
     );

1
    .toFixed(
        2);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(z$ = this.store.select(this.fake())
    .ofType(
        'ACTION',
        'ACTION-2'
    )
    .pipe(
        filter(x => !!x),
        switchMap(() =>
            this.store.select(this.menuSelector.getAll('x'))
                .pipe(
                    tap(x => {
                        this.x = !x;
                    })
                )
        )
    );

1
    .toFixed(
        2);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingChainingMethods, TestFormattingChainingMethods);

// formattingComma_test.go

// formattingComma_test.go
static void TestFormattingComma(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = [1 , 2];/*x*/
var y = ( 1  , 2 );/*y*/
var z1 = 1 , zz = 2;/*z1*/
var z2 = {
    x: 1 ,/*z2*/
    y: 2
};
var z3 = (
    () => { }  ,/*z3*/
    () => { }
    );
var z4 = [
    () => { } ,/*z4*/
    () => { }
];
var z5 = {
    x: () => { } ,/*z5*/
    y: () => { }
}; )TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "x");
		f->VerifyCurrentLineContent(t, R"TS(var x = [1, 2];)TS");
		f->GoToMarker(t, "y");
		f->VerifyCurrentLineContent(t, R"TS(var y = (1, 2);)TS");
		f->GoToMarker(t, "z1");
		f->VerifyCurrentLineContent(t, R"TS(var z1 = 1, zz = 2;)TS");
		f->GoToMarker(t, "z2");
		f->VerifyCurrentLineContent(t, R"TS(    x: 1,)TS");
		f->GoToMarker(t, "z3");
		f->VerifyCurrentLineContent(t, R"TS(    () => { },)TS");
		f->GoToMarker(t, "z4");
		f->VerifyCurrentLineContent(t, R"TS(    () => { },)TS");
		f->GoToMarker(t, "z5");
		f->VerifyCurrentLineContent(t, R"TS(    x: () => { },)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingComma, TestFormattingComma);

// formattingCommentsBeforeErrors_test.go

// formattingCommentsBeforeErrors_test.go
static void TestFormattingCommentsBeforeErrors(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace A {
    interface B {
        // a
        // b
        baz();
/*0*/        // d /*1*/asd a
        // e
        foo();
        // f asd
        // g as
        bar();
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, R"TS(
)TS");
		f->GoToMarker(t, "0");
		f->VerifyCurrentLineContent(t, R"TS(        // d )TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingCommentsBeforeErrors, TestFormattingCommentsBeforeErrors);

// formattingConditionalTypes_test.go

// formattingConditionalTypes_test.go
static void TestFormattingConditionalTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*L1*/type Diff1<T, U> = T extends U?never:T;
/*L2*/type Diff2<T, U> = T    extends    U  ?    never   :     T;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "L1");
		f->VerifyCurrentLineContent(t, R"TS(type Diff1<T, U> = T extends U ? never : T;)TS");
		f->GoToMarker(t, "L2");
		f->VerifyCurrentLineContent(t, R"TS(type Diff2<T, U> = T extends U ? never : T;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingConditionalTypes, TestFormattingConditionalTypes);

// formattingCrash_test.go

// formattingCrash_test.go
static void TestFormattingCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**/module Default{ 
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts131 = f->GetOptions();
		opts131.FormatCodeSettings.PlaceOpenBraceOnNewLineForFunctions = Tristate::True;
		f->Configure(t, opts131);
		auto opts199 = f->GetOptions();
		opts199.FormatCodeSettings.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::True;
		f->Configure(t, opts199);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(module Default)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingCrash, TestFormattingCrash);

// formattingDecorators_test.go

// formattingDecorators_test.go
static void TestFormattingDecorators(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/        @    decorator1    
/*2*/            @        decorator2
/*3*/    @decorator3
/*4*/        @    decorator4    @            decorator5
/*5*/class C {
/*6*/            @    decorator6    
/*7*/                @        decorator7
/*8*/        @decorator8
/*9*/    method1() { }

/*10*/        @    decorator9    @            decorator10 @decorator11            method2() { }

    method3(
/*11*/                @    decorator12    
/*12*/                    @        decorator13
/*13*/            @decorator14
/*14*/        x) { }

    method4(
/*15*/            @    decorator15    @            decorator16 @decorator17             x) { }

/*16*/            @    decorator18    
/*17*/                @        decorator19
/*18*/        @decorator20    
/*19*/    ["computed1"]() { }

/*20*/        @    decorator21    @            decorator22 @decorator23            ["computed2"]() { }

/*21*/            @    decorator24    
/*22*/                @        decorator25
/*23*/        @decorator26
/*24*/    get accessor1() { }

/*25*/        @    decorator27    @            decorator28 @decorator29            get accessor2() { }

/*26*/            @    decorator30    
/*27*/                @        decorator31
/*28*/        @decorator32
/*29*/    property1;

/*30*/        @    decorator33    @            decorator34 @decorator35            property2;
/*31*/function test(@decorator36@decorator37 param) {};
/*32*/function test2(@decorator38()@decorator39()param) {};
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(@decorator1)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(@decorator2)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(@decorator3)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(@decorator4 @decorator5)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(class C {)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator6)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator7)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator8)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(    method1() { })TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator9 @decorator10 @decorator11 method2() { })TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS(        @decorator12)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(        @decorator13)TS");
		f->GoToMarker(t, "13");
		f->VerifyCurrentLineContent(t, R"TS(        @decorator14)TS");
		f->GoToMarker(t, "14");
		f->VerifyCurrentLineContent(t, R"TS(        x) { })TS");
		f->GoToMarker(t, "15");
		f->VerifyCurrentLineContent(t, R"TS(        @decorator15 @decorator16 @decorator17 x) { })TS");
		f->GoToMarker(t, "16");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator18)TS");
		f->GoToMarker(t, "17");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator19)TS");
		f->GoToMarker(t, "18");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator20)TS");
		f->GoToMarker(t, "19");
		f->VerifyCurrentLineContent(t, R"TS(    ["computed1"]() { })TS");
		f->GoToMarker(t, "20");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator21 @decorator22 @decorator23 ["computed2"]() { })TS");
		f->GoToMarker(t, "21");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator24)TS");
		f->GoToMarker(t, "22");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator25)TS");
		f->GoToMarker(t, "23");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator26)TS");
		f->GoToMarker(t, "24");
		f->VerifyCurrentLineContent(t, R"TS(    get accessor1() { })TS");
		f->GoToMarker(t, "25");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator27 @decorator28 @decorator29 get accessor2() { })TS");
		f->GoToMarker(t, "26");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator30)TS");
		f->GoToMarker(t, "27");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator31)TS");
		f->GoToMarker(t, "28");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator32)TS");
		f->GoToMarker(t, "29");
		f->VerifyCurrentLineContent(t, R"TS(    property1;)TS");
		f->GoToMarker(t, "30");
		f->VerifyCurrentLineContent(t, R"TS(    @decorator33 @decorator34 @decorator35 property2;)TS");
		f->GoToMarker(t, "31");
		f->VerifyCurrentLineContent(t, R"TS(function test(@decorator36 @decorator37 param) { };)TS");
		f->GoToMarker(t, "32");
		f->VerifyCurrentLineContent(t, R"TS(function test2(@decorator38() @decorator39() param) { };)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingDecorators, TestFormattingDecorators);

// formattingElseInsideAFunction_test.go

// formattingElseInsideAFunction_test.go
static void TestFormattingElseInsideAFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = function() {
    if (true) {
    /*1*/} else {/*2*/
}

// newline at the end of the file)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "2");
		f->InsertLine(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    } else {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingElseInsideAFunction, TestFormattingElseInsideAFunction);

// formattingExpressionsInIfCondition_test.go

// formattingExpressionsInIfCondition_test.go
static void TestFormattingExpressionsInIfCondition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (a === 1 ||
    /*0*/b === 2 ||/*1*/
    c === 3) {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, R"TS(
)TS");
		f->GoToMarker(t, "0");
		f->VerifyCurrentLineContent(t, R"TS(    b === 2 ||)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingExpressionsInIfCondition, TestFormattingExpressionsInIfCondition);

// formattingFatArrowFunctions_test.go

// formattingFatArrowFunctions_test.go
static void TestFormattingFatArrowFunctions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// valid
    (         )           =>    1  ;/*1*/
    (        arg )           =>    2  ;/*2*/
        arg       =>    2  ;/*3*/
        arg=>2  ;/*3a*/
      (        arg     = 1 )           =>    3  ;/*4*/
    (        arg    ?        )           =>    4  ;/*5*/
    (        arg    :    number )           =>    5  ;/*6*/
      (        arg    :    number     = 0 )           =>    6  ;/*7*/
    (        arg        ?                  :    number )           =>    7  ;/*8*/
    (                 ...     arg    :    number   [      ]    )           =>    8  ;/*9*/
      (        arg1   ,    arg2 )           =>    12  ;/*10*/
    (        arg1     = 1   ,    arg2     =3 )           =>    13  ;/*11*/
      (        arg1    ?          ,    arg2    ?        )           =>    14  ;/*12*/
    (        arg1    :    number   ,    arg2    :    number )           =>    15  ;/*13*/
    (        arg1    :    number     = 0   ,    arg2    :    number     = 1 )           =>    16  ;/*14*/
      (        arg1    ?           :    number   ,    arg2    ?           :    number )           =>    17  ;/*15*/
    (        arg1   ,             ...     arg2    :    number   [      ]    )           =>    18  ;/*16*/
      (        arg1   ,    arg2    ?           :    number )           =>    19  ;/*17*/

// in paren
    (            (         )           =>    21 )      ;/*18*/
    (            (        arg )           =>    22 )      ;/*19*/
    (            (        arg     = 1 )           =>    23 )      ;/*20*/
    (            (        arg    ?        )           =>    24 )      ;/*21*/
    (            (        arg    :    number )           =>    25 )      ;/*22*/
    (            (        arg    :    number     = 0 )           =>    26 )      ;/*23*/
    (            (        arg    ?           :    number )           =>    27 )      ;/*24*/
    (            (                 ...     arg    :    number   [      ]    )           =>    28 )      ;/*25*/

// in multiple paren
    (            (            (            (            (        arg )           =>    { return 32  ;    } )     )     )     )      ;/*26*/

// in ternary exression
      false        ?            (         )           =>    41     :    null  ;/*27*/
   false        ?            (        arg )           =>    42     :    null  ;/*28*/
    false        ?            (        arg     = 1 )           =>    43     :    null  ;/*29*/
      false        ?            (        arg    ?        )           =>    44     :    null  ;/*30*/
    false        ?            (        arg    :    number )           =>    45     :    null  ;/*31*/
   false        ?            (        arg    ?           :    number )           =>    46     :    null  ;/*32*/
      false        ?            (        arg    ?           :    number     = 0 )           =>    47     :    null  ;/*33*/
   false        ?            (                 ...     arg    :    number   [      ]    )           =>    48     :    null  ;/*34*/

// in ternary exression within paren
   false        ?            (            (         )           =>    51 )         :    null  ;/*35*/
    false        ?            (            (        arg )           =>    52 )         :    null  ;/*36*/
    false        ?            (            (        arg     = 1 )           =>    53 )         :    null  ;/*37*/
      false        ?            (            (        arg    ?        )           =>    54 )         :    null  ;/*38*/
    false        ?            (            (        arg    :    number )           =>    55 )         :    null  ;/*39*/
      false        ?            (            (        arg    ?           :    number )           =>    56 )         :    null  ;/*40*/
    false        ?            (            (        arg    ?           :    number     = 0 )           =>    57 )         :    null  ;/*41*/
   false        ?            (            (                 ...     arg    :    number   [      ]    )           =>    58 )         :    null  ;/*42*/

// ternary exression's else clause
   false        ?        null     :        (         )           =>    61  ;/*43*/
        false        ?        null     :        (        arg )           =>    62  ;/*44*/
   false        ?        null     :        (        arg     = 1 )           =>    63  ;/*45*/
      false        ?        null     :        (        arg    ?        )           =>    64  ;/*46*/
   false        ?        null     :        (        arg    :    number )           =>    65  ;/*47*/
    false        ?        null     :        (        arg    ?           :    number )           =>    66  ;/*48*/
        false        ?        null     :        (        arg    ?           :    number     = 0 )           =>    67  ;/*49*/
    false        ?        null     :        (                 ...     arg    :    number   [      ]    )           =>    68  ;/*50*/


// nested ternary expressions
    ((        a    ?        )           =>    { return a  ;    })     ?            (        b    ?         )           =>    { return b  ;    }     :        (        c    ?         )           =>    { return c  ;    }  ;/*51*/

//multiple levels
    ((        a    ?        )           =>    { return a  ;    })     ?            (        b )          =>       (        c )          =>   81     :        (        c )          =>       (        d )          =>   82  ;/*52*/


// In Expressions
    (            (        arg )           =>    90 )     instanceof Function  ;/*53*/
      (            (        arg     = 1 )           =>    91 )     instanceof Function  ;/*54*/
        (            (        arg    ?         )           =>    92 )     instanceof Function  ;/*55*/
      (            (        arg    :    number )           =>    93 )     instanceof Function  ;/*56*/
    (            (        arg    :    number     = 1 )           =>    94 )     instanceof Function  ;/*57*/
        (            (        arg    ?           :    number )           =>    95 )     instanceof Function  ;/*58*/
      (            (                 ...     arg    :    number   [      ]    )           =>    96 )     instanceof Function  ;/*59*/

''    +        ((        arg )           =>    100)  ;/*60*/
        (            (        arg )           =>    0 )        +    ''    +        ((        arg )           =>    101)  ;/*61*/
          (            (        arg     = 1 )           =>    0 )        +    ''    +        ((        arg     = 2 )           =>    102)  ;/*62*/
    (            (        arg    ?        )           =>    0 )        +    ''    +        ((        arg    ?        )           =>    103)  ;/*63*/
      (            (        arg    :   number )           =>    0 )        +    ''    +        ((        arg    :   number )           =>    104)  ;/*64*/
        (            (        arg    :   number     = 1 )           =>    0 )        +    ''    +        ((        arg    :   number     = 2 )           =>    105)  ;/*65*/
    (            (        arg    ?           :   number     )           =>    0 )        +    ''    +        ((        arg    ?           :   number     )           =>    106)  ;/*66*/
      (            (                 ...     arg    :   number   [      ]    )           =>    0 )        +    ''    +        ((                 ...     arg    :   number   [      ]    )           =>    107)  ;/*67*/
    (            (        arg1   ,    arg2    ?        )           =>    0 )        +    ''    +        ((        arg1   ,   arg2    ?        )           =>    108)  ;/*68*/
      (            (        arg1   ,             ...     arg2    :   number   [      ]    )           =>    0 )        +    ''    +        ((        arg1   ,             ...     arg2    :   number   [      ]    )           =>    108)  ;/*69*/


// Function Parameters
/*70*/function foo    (                 ...     arg    :    any   [      ]    )     { }

/*71*/foo    (
/*72*/        (        a )           =>    110   ,
/*73*/        (            (        a )           =>    111 )       ,
/*74*/        (        a )           =>    {
        return /*75*/112  ;
/*76*/    }   ,
/*77*/        (        a    ?         )           =>    113   ,
/*78*/        (        a   ,    b    ?         )           =>    114   ,
/*79*/        (        a    :    number )           =>    115   ,
/*80*/        (        a    :    number     = 0 )           =>    116   ,
/*81*/        (        a     = 0 )           =>    117   ,
/*82*/        (        a               :    number     = 0 )           =>    118   ,
/*83*/        (        a    ?    ,   b   ?          :    number      )           =>    118   ,
/*84*/        (                 ...     a    :    number   [      ]    )           =>    119   ,
/*85*/        (        a   ,    b                = 0   ,             ...     c    :    number   [      ]    )           =>    120   ,
/*86*/        (        a )           =>        (        b )           =>        (        c )           =>    121   ,
/*87*/        false       ?            (        a )           =>    0     :        (        b )           =>    122
 /*88*/)      ;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(() => 1;)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS((arg) => 2;)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(arg => 2;)TS");
		f->GoToMarker(t, "3a");
		f->VerifyCurrentLineContent(t, R"TS(arg => 2;)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS((arg = 1) => 3;)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS((arg?) => 4;)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS((arg: number) => 5;)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS((arg: number = 0) => 6;)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS((arg?: number) => 7;)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS((...arg: number[]) => 8;)TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS((arg1, arg2) => 12;)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS((arg1 = 1, arg2 = 3) => 13;)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS((arg1?, arg2?) => 14;)TS");
		f->GoToMarker(t, "13");
		f->VerifyCurrentLineContent(t, R"TS((arg1: number, arg2: number) => 15;)TS");
		f->GoToMarker(t, "14");
		f->VerifyCurrentLineContent(t, R"TS((arg1: number = 0, arg2: number = 1) => 16;)TS");
		f->GoToMarker(t, "15");
		f->VerifyCurrentLineContent(t, R"TS((arg1?: number, arg2?: number) => 17;)TS");
		f->GoToMarker(t, "16");
		f->VerifyCurrentLineContent(t, R"TS((arg1, ...arg2: number[]) => 18;)TS");
		f->GoToMarker(t, "17");
		f->VerifyCurrentLineContent(t, R"TS((arg1, arg2?: number) => 19;)TS");
		f->GoToMarker(t, "18");
		f->VerifyCurrentLineContent(t, R"TS((() => 21);)TS");
		f->GoToMarker(t, "19");
		f->VerifyCurrentLineContent(t, R"TS(((arg) => 22);)TS");
		f->GoToMarker(t, "20");
		f->VerifyCurrentLineContent(t, R"TS(((arg = 1) => 23);)TS");
		f->GoToMarker(t, "21");
		f->VerifyCurrentLineContent(t, R"TS(((arg?) => 24);)TS");
		f->GoToMarker(t, "22");
		f->VerifyCurrentLineContent(t, R"TS(((arg: number) => 25);)TS");
		f->GoToMarker(t, "23");
		f->VerifyCurrentLineContent(t, R"TS(((arg: number = 0) => 26);)TS");
		f->GoToMarker(t, "24");
		f->VerifyCurrentLineContent(t, R"TS(((arg?: number) => 27);)TS");
		f->GoToMarker(t, "25");
		f->VerifyCurrentLineContent(t, R"TS(((...arg: number[]) => 28);)TS");
		f->GoToMarker(t, "26");
		f->VerifyCurrentLineContent(t, R"TS((((((arg) => { return 32; }))));)TS");
		f->GoToMarker(t, "27");
		f->VerifyCurrentLineContent(t, R"TS(false ? () => 41 : null;)TS");
		f->GoToMarker(t, "28");
		f->VerifyCurrentLineContent(t, R"TS(false ? (arg) => 42 : null;)TS");
		f->GoToMarker(t, "29");
		f->VerifyCurrentLineContent(t, R"TS(false ? (arg = 1) => 43 : null;)TS");
		f->GoToMarker(t, "30");
		f->VerifyCurrentLineContent(t, R"TS(false ? (arg?) => 44 : null;)TS");
		f->GoToMarker(t, "31");
		f->VerifyCurrentLineContent(t, R"TS(false ? (arg: number) => 45 : null;)TS");
		f->GoToMarker(t, "32");
		f->VerifyCurrentLineContent(t, R"TS(false ? (arg?: number) => 46 : null;)TS");
		f->GoToMarker(t, "33");
		f->VerifyCurrentLineContent(t, R"TS(false ? (arg?: number = 0) => 47 : null;)TS");
		f->GoToMarker(t, "34");
		f->VerifyCurrentLineContent(t, R"TS(false ? (...arg: number[]) => 48 : null;)TS");
		f->GoToMarker(t, "35");
		f->VerifyCurrentLineContent(t, R"TS(false ? (() => 51) : null;)TS");
		f->GoToMarker(t, "36");
		f->VerifyCurrentLineContent(t, R"TS(false ? ((arg) => 52) : null;)TS");
		f->GoToMarker(t, "37");
		f->VerifyCurrentLineContent(t, R"TS(false ? ((arg = 1) => 53) : null;)TS");
		f->GoToMarker(t, "38");
		f->VerifyCurrentLineContent(t, R"TS(false ? ((arg?) => 54) : null;)TS");
		f->GoToMarker(t, "39");
		f->VerifyCurrentLineContent(t, R"TS(false ? ((arg: number) => 55) : null;)TS");
		f->GoToMarker(t, "40");
		f->VerifyCurrentLineContent(t, R"TS(false ? ((arg?: number) => 56) : null;)TS");
		f->GoToMarker(t, "41");
		f->VerifyCurrentLineContent(t, R"TS(false ? ((arg?: number = 0) => 57) : null;)TS");
		f->GoToMarker(t, "42");
		f->VerifyCurrentLineContent(t, R"TS(false ? ((...arg: number[]) => 58) : null;)TS");
		f->GoToMarker(t, "43");
		f->VerifyCurrentLineContent(t, R"TS(false ? null : () => 61;)TS");
		f->GoToMarker(t, "44");
		f->VerifyCurrentLineContent(t, R"TS(false ? null : (arg) => 62;)TS");
		f->GoToMarker(t, "45");
		f->VerifyCurrentLineContent(t, R"TS(false ? null : (arg = 1) => 63;)TS");
		f->GoToMarker(t, "46");
		f->VerifyCurrentLineContent(t, R"TS(false ? null : (arg?) => 64;)TS");
		f->GoToMarker(t, "47");
		f->VerifyCurrentLineContent(t, R"TS(false ? null : (arg: number) => 65;)TS");
		f->GoToMarker(t, "48");
		f->VerifyCurrentLineContent(t, R"TS(false ? null : (arg?: number) => 66;)TS");
		f->GoToMarker(t, "49");
		f->VerifyCurrentLineContent(t, R"TS(false ? null : (arg?: number = 0) => 67;)TS");
		f->GoToMarker(t, "50");
		f->VerifyCurrentLineContent(t, R"TS(false ? null : (...arg: number[]) => 68;)TS");
		f->GoToMarker(t, "51");
		f->VerifyCurrentLineContent(t, R"TS(((a?) => { return a; }) ? (b?) => { return b; } : (c?) => { return c; };)TS");
		f->GoToMarker(t, "52");
		f->VerifyCurrentLineContent(t, R"TS(((a?) => { return a; }) ? (b) => (c) => 81 : (c) => (d) => 82;)TS");
		f->GoToMarker(t, "53");
		f->VerifyCurrentLineContent(t, R"TS(((arg) => 90) instanceof Function;)TS");
		f->GoToMarker(t, "54");
		f->VerifyCurrentLineContent(t, R"TS(((arg = 1) => 91) instanceof Function;)TS");
		f->GoToMarker(t, "55");
		f->VerifyCurrentLineContent(t, R"TS(((arg?) => 92) instanceof Function;)TS");
		f->GoToMarker(t, "56");
		f->VerifyCurrentLineContent(t, R"TS(((arg: number) => 93) instanceof Function;)TS");
		f->GoToMarker(t, "57");
		f->VerifyCurrentLineContent(t, R"TS(((arg: number = 1) => 94) instanceof Function;)TS");
		f->GoToMarker(t, "58");
		f->VerifyCurrentLineContent(t, R"TS(((arg?: number) => 95) instanceof Function;)TS");
		f->GoToMarker(t, "59");
		f->VerifyCurrentLineContent(t, R"TS(((...arg: number[]) => 96) instanceof Function;)TS");
		f->GoToMarker(t, "60");
		f->VerifyCurrentLineContent(t, R"TS('' + ((arg) => 100);)TS");
		f->GoToMarker(t, "61");
		f->VerifyCurrentLineContent(t, R"TS(((arg) => 0) + '' + ((arg) => 101);)TS");
		f->GoToMarker(t, "62");
		f->VerifyCurrentLineContent(t, R"TS(((arg = 1) => 0) + '' + ((arg = 2) => 102);)TS");
		f->GoToMarker(t, "63");
		f->VerifyCurrentLineContent(t, R"TS(((arg?) => 0) + '' + ((arg?) => 103);)TS");
		f->GoToMarker(t, "64");
		f->VerifyCurrentLineContent(t, R"TS(((arg: number) => 0) + '' + ((arg: number) => 104);)TS");
		f->GoToMarker(t, "65");
		f->VerifyCurrentLineContent(t, R"TS(((arg: number = 1) => 0) + '' + ((arg: number = 2) => 105);)TS");
		f->GoToMarker(t, "66");
		f->VerifyCurrentLineContent(t, R"TS(((arg?: number) => 0) + '' + ((arg?: number) => 106);)TS");
		f->GoToMarker(t, "67");
		f->VerifyCurrentLineContent(t, R"TS(((...arg: number[]) => 0) + '' + ((...arg: number[]) => 107);)TS");
		f->GoToMarker(t, "68");
		f->VerifyCurrentLineContent(t, R"TS(((arg1, arg2?) => 0) + '' + ((arg1, arg2?) => 108);)TS");
		f->GoToMarker(t, "69");
		f->VerifyCurrentLineContent(t, R"TS(((arg1, ...arg2: number[]) => 0) + '' + ((arg1, ...arg2: number[]) => 108);)TS");
		f->GoToMarker(t, "70");
		f->VerifyCurrentLineContent(t, R"TS(function foo(...arg: any[]) { })TS");
		f->GoToMarker(t, "71");
		f->VerifyCurrentLineContent(t, R"TS(foo()TS");
		f->GoToMarker(t, "72");
		f->VerifyCurrentLineContent(t, R"TS(    (a) => 110,)TS");
		f->GoToMarker(t, "73");
		f->VerifyCurrentLineContent(t, R"TS(    ((a) => 111),)TS");
		f->GoToMarker(t, "74");
		f->VerifyCurrentLineContent(t, R"TS(    (a) => {)TS");
		f->GoToMarker(t, "75");
		f->VerifyCurrentLineContent(t, R"TS(        return 112;)TS");
		f->GoToMarker(t, "76");
		f->VerifyCurrentLineContent(t, R"TS(    },)TS");
		f->GoToMarker(t, "77");
		f->VerifyCurrentLineContent(t, R"TS(    (a?) => 113,)TS");
		f->GoToMarker(t, "78");
		f->VerifyCurrentLineContent(t, R"TS(    (a, b?) => 114,)TS");
		f->GoToMarker(t, "79");
		f->VerifyCurrentLineContent(t, R"TS(    (a: number) => 115,)TS");
		f->GoToMarker(t, "80");
		f->VerifyCurrentLineContent(t, R"TS(    (a: number = 0) => 116,)TS");
		f->GoToMarker(t, "81");
		f->VerifyCurrentLineContent(t, R"TS(    (a = 0) => 117,)TS");
		f->GoToMarker(t, "82");
		f->VerifyCurrentLineContent(t, R"TS(    (a: number = 0) => 118,)TS");
		f->GoToMarker(t, "83");
		f->VerifyCurrentLineContent(t, R"TS(    (a?, b?: number) => 118,)TS");
		f->GoToMarker(t, "84");
		f->VerifyCurrentLineContent(t, R"TS(    (...a: number[]) => 119,)TS");
		f->GoToMarker(t, "85");
		f->VerifyCurrentLineContent(t, R"TS(    (a, b = 0, ...c: number[]) => 120,)TS");
		f->GoToMarker(t, "86");
		f->VerifyCurrentLineContent(t, R"TS(    (a) => (b) => (c) => 121,)TS");
		f->GoToMarker(t, "87");
		f->VerifyCurrentLineContent(t, R"TS(    false ? (a) => 0 : (b) => 122)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingFatArrowFunctions, TestFormattingFatArrowFunctions);

// formattingForLoopSemicolons_test.go

// formattingForLoopSemicolons_test.go
static void TestFormattingForLoopSemicolons(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/for (;;) { }
/*2*/for (var x;x<0;x++) { }
/*3*/for (var x ;x<0 ;x++) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(for (; ;) { })TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(for (var x; x < 0; x++) { })TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(for (var x; x < 0; x++) { })TS");
		auto opts444 = f->GetOptions();
		opts444.FormatCodeSettings.InsertSpaceAfterSemicolonInForStatements = Tristate::False;
		f->Configure(t, opts444);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(for (;;) { })TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(for (var x;x < 0;x++) { })TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(for (var x;x < 0;x++) { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingForLoopSemicolons, TestFormattingForLoopSemicolons);

// formattingGlobalAugmentation1_test.go

// formattingGlobalAugmentation1_test.go
static void TestFormattingGlobalAugmentation1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/declare          global                      {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(declare global {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingGlobalAugmentation1, TestFormattingGlobalAugmentation1);

// formattingGlobalAugmentation2_test.go

// formattingGlobalAugmentation2_test.go
static void TestFormattingGlobalAugmentation2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare module "A" {
/*1*/                  global                {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    global {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingGlobalAugmentation2, TestFormattingGlobalAugmentation2);

// formattingIfInElseBlock_test.go

// formattingIfInElseBlock_test.go
static void TestFormattingIfInElseBlock(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) {
}
else {
    if (true) {
        /*1*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, "}");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingIfInElseBlock, TestFormattingIfInElseBlock);

// formattingIllegalImportClause_test.go

// formattingIllegalImportClause_test.go
static void TestFormattingIllegalImportClause(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var expect = require('expect.js');
import React   from 'react'/*1*/;
import { mount } from 'enzyme';
require('../setup');
var Amount = require('../../src/js/components/amount');
describe('<Failed />', () => {
  var history
  beforeEach(() => {
    history = createMemoryHistory();
    sinon.spy(history, 'pushState');
  });
  afterEach(() => {
  })
  it('redirects to order summary', () => {
  });
});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(import React from 'react';)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingIllegalImportClause, TestFormattingIllegalImportClause);

// formattingInComment_test.go

// formattingInComment_test.go
static void TestFormattingInComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
foo(              ); // /*1*/
}
function foo() {       var x;       } // /*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(foo(              ); // ;)TS");
		f->GoToMarker(t, "2");
		f->Insert(t, "}");
		f->VerifyCurrentLineContent(t, R"TS(function foo() {       var x;       } // })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingInComment, TestFormattingInComment);

// formattingInDestructuring1_test.go

// formattingInDestructuring1_test.go
static void TestFormattingInDestructuring1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface let { }
/*1*/var x: let         [];

function foo() {
    'use strict'
/*2*/    let        [x] = [];
/*3*/    const      [x] = [];
/*4*/    for (let[x] = [];x < 1;) {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(var x: let[];)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    let [x] = [];)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    const [x] = [];)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(    for (let [x] = []; x < 1;) {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingInDestructuring1, TestFormattingInDestructuring1);

// formattingInDestructuring2_test.go

// formattingInDestructuring2_test.go
static void TestFormattingInDestructuring2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/function   drawText(    { text = "", location: [x, y]=           [0, 0], bold = false }) {
    // Draw text  
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(function drawText({ text = "", location: [x, y] = [0, 0], bold = false }) {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingInDestructuring2, TestFormattingInDestructuring2);

// formattingInDestructuring3_test.go

// formattingInDestructuring3_test.go
static void TestFormattingInDestructuring3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/const {
/*2*/    a,
/*3*/    b,
/*4*/} = {a: 1, b: 2};
/*5*/const {a: c} = {a: 1, b: 2};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(const {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    a,)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    b,)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(} = { a: 1, b: 2 };)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(const { a: c } = { a: 1, b: 2 };)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingInDestructuring3, TestFormattingInDestructuring3);

// formattingInDestructuring4_test.go

// formattingInDestructuring4_test.go
static void TestFormattingInDestructuring4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/const { 
/*2*/    a,
/*3*/    b,
/*4*/} = { a: 1, b: 2 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts198 = f->GetOptions();
		opts198.FormatCodeSettings.InsertSpaceAfterOpeningAndBeforeClosingNonemptyBraces = Tristate::False;
		f->Configure(t, opts198);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(const {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    a,)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    b,)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(} = {a: 1, b: 2};)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingInDestructuring4, TestFormattingInDestructuring4);

// formattingInDestructuring5_test.go

// formattingInDestructuring5_test.go
static void TestFormattingInDestructuring5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let a, b;
/*1*/if (false)[a, b] = [1, 2];
/*2*/if (true)        [a, b] = [1, 2];
/*3*/var a = [1, 2, 3].map(num => num) [0];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(if (false) [a, b] = [1, 2];)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(if (true) [a, b] = [1, 2];)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(var a = [1, 2, 3].map(num => num)[0];)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingInDestructuring5, TestFormattingInDestructuring5);

// formattingInExpressionsInTsx_test.go

// formattingInExpressionsInTsx_test.go
static void TestFormattingInExpressionsInTsx(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: test.tsx
import * as React from "react";
<div
    autoComplete={(function () {
return true/*1*/
    })() }
    >
</div>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(        return true;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingInExpressionsInTsx, TestFormattingInExpressionsInTsx);

// formattingInMultilineComments_test.go

// formattingInMultilineComments_test.go
static void TestFormattingInMultilineComments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = function() {
    if (true) {
    /*1*/} else {/*2*/
}

// newline at the end of the file)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "2");
		f->InsertLine(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    } else {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingInMultilineComments, TestFormattingInMultilineComments);

// formattingJsxTexts1_test.go

// formattingJsxTexts1_test.go
static void TestFormattingJsxTexts1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: file.tsx
<option>
    homu   ;      homu
    homu;homu
    homu   :    homu
    homu:homu
    homu    ?     homu
    homu    .    homu

    homu    [   homu   ]   homu

    !     homu
    --    Type
    homu    --
    homu    ++
    ++     homu

    homu  ,   homu

    var    homu
    throw    homu
    new    homu
    delete   homu
    return       homu
    typeof     homu
    await     homu

    abstract  homu
    class     homu
    declare   homu
    default   homu
    enum      homu
    export    homu
    homu    extends   homu
    get       homu
    homu    implements     homu
    interface      homu
    module    homu
    namespace      homu
    private   homu
    public    homu
    protected      homu
    set       homu
    static    homu
    type      homu

    homu    =>    homu
    homu=>homu

    ...       homu

    homu     @     homu
    homu@homu

    (    homu   )    homu
</option>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(<option>
    homu   ;      homu
    homu;homu
    homu   :    homu
    homu:homu
    homu    ?     homu
    homu    .    homu

    homu    [   homu   ]   homu

    !     homu
    --    Type
    homu    --
    homu    ++
    ++     homu

    homu  ,   homu

    var    homu
    throw    homu
    new    homu
    delete   homu
    return       homu
    typeof     homu
    await     homu

    abstract  homu
    class     homu
    declare   homu
    default   homu
    enum      homu
    export    homu
    homu    extends   homu
    get       homu
    homu    implements     homu
    interface      homu
    module    homu
    namespace      homu
    private   homu
    public    homu
    protected      homu
    set       homu
    static    homu
    type      homu

    homu    =>    homu
    homu=>homu

    ...       homu

    homu     @     homu
    homu@homu

    (    homu   )    homu
</option>;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingJsxTexts1, TestFormattingJsxTexts1);

// formattingJsxTexts2_test.go

// formattingJsxTexts2_test.go
static void TestFormattingJsxTexts2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: file.tsx
const a = (
    <div>
  foo
          </div>
);

const b = (
    <div>
  {     foo  }
          </div>
);

const c = (
    <div>
    foo
  {     foobar  }
  bar
          </div>
);

const d = 
    <div>
  foo
          </div>;

const e = 
    <div>
  {     foo  }
          </div>

const f = 
    <div>
    foo
  {     foobar  }
  bar
          </div>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(const a = (
    <div>
        foo
    </div>
);

const b = (
    <div>
        {foo}
    </div>
);

const c = (
    <div>
        foo
        {foobar}
        bar
    </div>
);

const d =
    <div>
        foo
    </div>;

const e =
    <div>
        {foo}
    </div>

const f =
    <div>
        foo
        {foobar}
        bar
    </div>)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingJsxTexts2, TestFormattingJsxTexts2);

// formattingJsxTexts3_test.go

// formattingJsxTexts3_test.go
static void TestFormattingJsxTexts3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: file.tsx
function foo() {
const bar = "Oh no";

return (
<div>"{bar}"</div>
)
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(function foo() {
    const bar = "Oh no";

    return (
        <div>"{bar}"</div>
    )
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingJsxTexts3, TestFormattingJsxTexts3);

// formattingJsxTexts4_test.go

// formattingJsxTexts4_test.go
static void TestFormattingJsxTexts4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: file.tsx
function foo() {
const a = <ns: foobar   x : test1   x :test2="string"  x:test3={true?1:0}  />;

return a;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(function foo() {
    const a = <ns:foobar x:test1 x:test2="string" x:test3={true ? 1 : 0} />;

    return a;
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingJsxTexts4, TestFormattingJsxTexts4);

// formattingMappedType_test.go

// formattingMappedType_test.go
static void TestFormattingMappedType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*generic*/type t  < T  > =   {
/*map*/   [   P   in   keyof    T  ]   :   T  [  P  ]
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "generic");
		f->VerifyCurrentLineContent(t, R"TS(type t<T> = {)TS");
		f->GoToMarker(t, "map");
		f->VerifyCurrentLineContent(t, R"TS(    [P in keyof T]: T[P])TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingMappedType, TestFormattingMappedType);

// formattingMultilineCommentsWithTabs1_test.go

// formattingMultilineCommentsWithTabs1_test.go
static void TestFormattingMultilineCommentsWithTabs1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var f = function (j) {

	switch (j) {
		case 1:
/*1*/				/* when current checkbox has focus, Firefox has changed check state already
/*2*/				on SPACE bar press only
/*3*/				IE does not have issue, use the CSS class
/*4*/				input:focus[type=checkbox] (z-index = 31290)
/*5*/				to determine whether checkbox has focus or not
				*/
			break;
		case 2:
		break;
	}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(            /* when current checkbox has focus, Firefox has changed check state already)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(            on SPACE bar press only)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(            IE does not have issue, use the CSS class)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(            input:focus[type=checkbox] (z-index = 31290))TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(            to determine whether checkbox has focus or not)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingMultilineCommentsWithTabs1, TestFormattingMultilineCommentsWithTabs1);

// formattingMultilineTemplateLiterals_test.go

// formattingMultilineTemplateLiterals_test.go
static void TestFormattingMultilineTemplateLiterals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(/*1*/new Error()TS") + "`") + std::string(R"TS(Failed to expand glob: ${projectSpec.filesGlob}
/*2*/                at projectPath : ${projectFile}
/*3*/                with error: ${ex.message})TS")) + std::string("`")) + std::string(R"TS())TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, "new Error(`Failed to expand glob: ${projectSpec.filesGlob}");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(                at projectPath : ${projectFile})TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, "                with error: ${ex.message}`)");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingMultilineTemplateLiterals, TestFormattingMultilineTemplateLiterals);

// formattingNestedScopes_test.go

// formattingNestedScopes_test.go
static void TestFormattingNestedScopes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/        namespace      My.App      {
/*2*/export      var appModule =      angular.module("app", [
/*3*/            ]).config([() =>            {
/*4*/                        configureStates
/*5*/($stateProvider);
/*6*/}]).run(My.App.setup);
/*7*/      })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(namespace My.App {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    export var appModule = angular.module("app", [)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    ]).config([() => {)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(        configureStates)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(            ($stateProvider);)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    }]).run(My.App.setup);)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingNestedScopes, TestFormattingNestedScopes);

// formattingNonNullAssertionOperator_test.go

// formattingNonNullAssertionOperator_test.go
static void TestFormattingNonNullAssertionOperator(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/ 'bar' ! ;
/*2*/ ( 'bar' ) ! ;
/*3*/ 'bar' [ 1 ] ! ;
/*4*/ var  bar  =  'bar' . foo ! ;
/*5*/ var  foo  =  bar ! ;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS('bar'!;)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(('bar')!;)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS('bar'[1]!;)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(var bar = 'bar'.foo!;)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(var foo = bar!;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingNonNullAssertionOperator, TestFormattingNonNullAssertionOperator);

// formattingObjectLiteralOpenCurlyNewlineAssignment_test.go

// formattingObjectLiteralOpenCurlyNewlineAssignment_test.go
static void TestFormattingObjectLiteralOpenCurlyNewlineAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
var obj = {};
obj =
{
    prop: 3
};
 
var obj2 = obj ||
{
    prop: 0
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
var obj = {};
obj =
{
    prop: 3
};

var obj2 = obj ||
{
    prop: 0
}
)TS");
		auto opts400 = f->GetOptions();
		opts400.FormatCodeSettings.IndentMultiLineObjectLiteralBeginningOnBlankLine = Tristate::True;
		f->Configure(t, opts400);
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
var obj = {};
obj =
    {
        prop: 3
    };

var obj2 = obj ||
    {
        prop: 0
    }
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingObjectLiteralOpenCurlyNewlineAssignment, TestFormattingObjectLiteralOpenCurlyNewlineAssignment);

// formattingObjectLiteralOpenCurlyNewlineTyping_test.go

// formattingObjectLiteralOpenCurlyNewlineTyping_test.go
static void TestFormattingObjectLiteralOpenCurlyNewlineTyping(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(
var varName =/**/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, R"TS(
{)TS");
		f->VerifyCurrentFileContent(t, R"TS(
var varName =
    {
)TS");
		f->Insert(t, R"TS(
a: 1)TS");
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
var varName =
{
    a: 1
)TS");
		f->Insert(t, R"TS(
};)TS");
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
var varName =
{
    a: 1
};
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingObjectLiteralOpenCurlyNewlineTyping, TestFormattingObjectLiteralOpenCurlyNewlineTyping);

// formattingObjectLiteralOpenCurlyNewline_test.go

// formattingObjectLiteralOpenCurlyNewline_test.go
static void TestFormattingObjectLiteralOpenCurlyNewline(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
var clear =
{
    outerKey:
    {
        innerKey: 1,
        innerKey2:
            2
    }
};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
var clear =
{
    outerKey:
    {
        innerKey: 1,
        innerKey2:
            2
    }
};
)TS");
		auto opts444 = f->GetOptions();
		opts444.FormatCodeSettings.IndentMultiLineObjectLiteralBeginningOnBlankLine = Tristate::True;
		f->Configure(t, opts444);
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
var clear =
    {
        outerKey:
            {
                innerKey: 1,
                innerKey2:
                    2
            }
    };
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingObjectLiteralOpenCurlyNewline, TestFormattingObjectLiteralOpenCurlyNewline);

// formattingObjectLiteralOpenCurlySingleLine_test.go

// formattingObjectLiteralOpenCurlySingleLine_test.go
static void TestFormattingObjectLiteralOpenCurlySingleLine(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(
let obj1 =
{ x: 10 };

let obj2 =
    // leading trivia
{ y: 10 };
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(
let obj1 =
    { x: 10 };

let obj2 =
    // leading trivia
    { y: 10 };
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingObjectLiteralOpenCurlySingleLine, TestFormattingObjectLiteralOpenCurlySingleLine);

// formattingObjectLiteral_test.go

// formattingObjectLiteral_test.go
static void TestFormattingObjectLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var clear = {
"a": 1/**/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(    "a": 1)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingObjectLiteral, TestFormattingObjectLiteral);

// formattingOfChainedLambda_test.go

// formattingOfChainedLambda_test.go
static void TestFormattingOfChainedLambda(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var fn = (x: string) => ()=> alert(x)/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(var fn = (x: string) => () => alert(x);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOfChainedLambda, TestFormattingOfChainedLambda);

// formattingOfExportDefault_test.go

// formattingOfExportDefault_test.go
static void TestFormattingOfExportDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Foo {
/*1*/    export        default        class        Test { }
}
/*2*/export        default        function        bar() { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    export default class Test { })TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(export default function bar() { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOfExportDefault, TestFormattingOfExportDefault);

// formattingOfMultilineBlockConstructs_test.go

// formattingOfMultilineBlockConstructs_test.go
static void TestFormattingOfMultilineBlockConstructs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace InternalModule/*1*/
{
}
interface MyInterface/*2*/
{
}
enum E/*3*/
{
}
class MyClass/*4*/
{
constructor()/*cons*/
{ }
        public MyFunction()/*5*/
        {
                return 0;
        }
public get Getter()/*6*/
{
}
public set Setter(x)/*7*/
{
}
}
function foo()/*8*/
{
{}/*9*/
}
(function()/*10*/
{
});
(() =>/*11*/
{
});
var x :/*12*/
{};/*13*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(namespace InternalModule {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(interface MyInterface {)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(enum E {)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(class MyClass {)TS");
		f->GoToMarker(t, "cons");
		f->VerifyCurrentLineContent(t, R"TS(    constructor() { })TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    public MyFunction() {)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    public get Getter() {)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(    public set Setter(x) {)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(function foo() {)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(    { })TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS((function() {)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS((() => {)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(var x:)TS");
		f->GoToMarker(t, "13");
		f->VerifyCurrentLineContent(t, R"TS(    {};)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOfMultilineBlockConstructs, TestFormattingOfMultilineBlockConstructs);

// formattingOnChainedCallbacksAndPropertyAccesses_test.go

// formattingOnChainedCallbacksAndPropertyAccesses_test.go
static void TestFormattingOnChainedCallbacksAndPropertyAccesses(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = 1;
x
/*1*/.toFixed
x
/*2*/.toFixed()
x
/*3*/.toFixed()
/*4*/.length
/*5*/.toString();
x
/*6*/.toFixed
/*7*/.toString()
/*8*/.length;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    .toFixed)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    .toFixed())TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    .toFixed())TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(    .length)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    .toString();)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    .toFixed)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(    .toString())TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(    .length;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnChainedCallbacksAndPropertyAccesses, TestFormattingOnChainedCallbacksAndPropertyAccesses);

// formattingOnClasses_test.go

// formattingOnClasses_test.go
static void TestFormattingOnClasses(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/         class                    a                  {
/*2*/                                                        constructor       (       n   :                 number    )             ;
/*3*/                                                        constructor       (       s   :                 string    )             ;
/*4*/                                                        constructor       (       ns   :                 any    )                            {

/*5*/                                                        }

/*6*/                                                            public                 pgF       (           )                            {                  }

/*7*/                                                            public                 pv   ;
/*8*/                                                            public                 get              d       (           )                            {
/*9*/                                                                                                                return              30   ;
/*10*/                                                        }
/*11*/                                                            public                 set              d       (       number        )                            {
/*12*/                                                        }

/*13*/                                                            public                 static                    get              p2       (           )                            {
/*14*/                                                                                                                return                  {                  x   :                 30   ,                  y   :                 40              }   ;
/*15*/                                                        }

/*16*/                                                                         private                static                    d2       (           )                            {
/*17*/                                                        }
/*18*/                                                                         private                static                    get              p3       (           )                            {
/*19*/                                                                                                                return              "string"   ;
/*20*/                                                        }
/*21*/                                                                         private                pv3   ;

/*22*/                                                                         private                foo       (       n   :                 number    )             :                 string   ;
/*23*/                                                                         private                foo       (       s   :                 string    )             :                 string   ;
/*24*/                                                                         private                foo       (       ns   :                 any    )                            {
/*25*/                                                                                                                return              ns.toString       (           )             ;
/*26*/                                                        }
/*27*/}

/*28*/         class                    b              extends              a                  {
/*29*/}

/*30*/         class   m1b      {

/*31*/}

/*32*/                                                interface   m1ib                               {

/*33*/  }
/*34*/         class                    c              extends              m1b                  {
/*35*/}

/*36*/         class                    ib2              implements              m1ib                  {
/*37*/}

/*38*/    declare                            class                    aAmbient                  {
/*39*/                                                        constructor                     (       n   :                 number    )             ;
/*40*/                                                        constructor                     (       s   :                 string    )             ;
/*41*/                                                            public                 pgF       (           )             :                 void   ;
/*42*/                                                            public                 pv   ;
/*43*/                                                            public                 d                 :                 number   ;
/*44*/                                                        static                    p2                 :                     {                  x   :                 number   ;              y   :                 number   ;              }   ;
/*45*/                                                        static                    d2       (           )             ;
/*46*/                                                        static                    p3   ;
/*47*/                                                                         private                pv3   ;
/*48*/                                                                         private                foo       (       s    )             ;
/*49*/}

/*50*/         class                    d                  {
/*51*/                                                                         private                foo       (       n   :                 number    )             :                 string   ;
/*52*/                                                                         private                foo       (       s   :                 string    )             :                 string   ;
/*53*/                                                                         private                foo       (       ns   :                 any    )                            {
/*54*/                                                                                                                return              ns.toString       (           )             ;
/*55*/                                                        }
/*56*/}

/*57*/         class                    e                  {
/*58*/                                                                         private                foo       (       s   :                 string    )             :                 string   ;
/*59*/                                                                         private                foo       (       n   :                 number    )             :                 string   ;
/*60*/                                                                         private                foo       (       ns   :                 any    )                            {
/*61*/                                                                                                                return              ns.toString       (           )             ;
/*62*/                                                        }
/*63*/                                                                         protected              bar        (            )  {                 }
/*64*/                                                                         protected     static   bar2       (            )  {                 }
/*65*/                                                                         private                pv4  :    number =
/*66*/                                                                         {};
/*END*/})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(class a {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    constructor(n: number);)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    constructor(s: string);)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(    constructor(ns: any) {)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    public pgF() { })TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(    public pv;)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(    public get d() {)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(        return 30;)TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS(    public set d(number) {)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "13");
		f->VerifyCurrentLineContent(t, R"TS(    public static get p2() {)TS");
		f->GoToMarker(t, "14");
		f->VerifyCurrentLineContent(t, R"TS(        return { x: 30, y: 40 };)TS");
		f->GoToMarker(t, "15");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "16");
		f->VerifyCurrentLineContent(t, R"TS(    private static d2() {)TS");
		f->GoToMarker(t, "17");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "18");
		f->VerifyCurrentLineContent(t, R"TS(    private static get p3() {)TS");
		f->GoToMarker(t, "19");
		f->VerifyCurrentLineContent(t, R"TS(        return "string";)TS");
		f->GoToMarker(t, "20");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "21");
		f->VerifyCurrentLineContent(t, R"TS(    private pv3;)TS");
		f->GoToMarker(t, "22");
		f->VerifyCurrentLineContent(t, R"TS(    private foo(n: number): string;)TS");
		f->GoToMarker(t, "23");
		f->VerifyCurrentLineContent(t, R"TS(    private foo(s: string): string;)TS");
		f->GoToMarker(t, "24");
		f->VerifyCurrentLineContent(t, R"TS(    private foo(ns: any) {)TS");
		f->GoToMarker(t, "25");
		f->VerifyCurrentLineContent(t, R"TS(        return ns.toString();)TS");
		f->GoToMarker(t, "26");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "27");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "28");
		f->VerifyCurrentLineContent(t, R"TS(class b extends a {)TS");
		f->GoToMarker(t, "29");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "30");
		f->VerifyCurrentLineContent(t, R"TS(class m1b {)TS");
		f->GoToMarker(t, "31");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "32");
		f->VerifyCurrentLineContent(t, R"TS(interface m1ib {)TS");
		f->GoToMarker(t, "33");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "34");
		f->VerifyCurrentLineContent(t, R"TS(class c extends m1b {)TS");
		f->GoToMarker(t, "35");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "36");
		f->VerifyCurrentLineContent(t, R"TS(class ib2 implements m1ib {)TS");
		f->GoToMarker(t, "37");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "38");
		f->VerifyCurrentLineContent(t, R"TS(declare class aAmbient {)TS");
		f->GoToMarker(t, "39");
		f->VerifyCurrentLineContent(t, R"TS(    constructor(n: number);)TS");
		f->GoToMarker(t, "40");
		f->VerifyCurrentLineContent(t, R"TS(    constructor(s: string);)TS");
		f->GoToMarker(t, "41");
		f->VerifyCurrentLineContent(t, R"TS(    public pgF(): void;)TS");
		f->GoToMarker(t, "42");
		f->VerifyCurrentLineContent(t, R"TS(    public pv;)TS");
		f->GoToMarker(t, "43");
		f->VerifyCurrentLineContent(t, R"TS(    public d: number;)TS");
		f->GoToMarker(t, "44");
		f->VerifyCurrentLineContent(t, R"TS(    static p2: { x: number; y: number; };)TS");
		f->GoToMarker(t, "45");
		f->VerifyCurrentLineContent(t, R"TS(    static d2();)TS");
		f->GoToMarker(t, "46");
		f->VerifyCurrentLineContent(t, R"TS(    static p3;)TS");
		f->GoToMarker(t, "47");
		f->VerifyCurrentLineContent(t, R"TS(    private pv3;)TS");
		f->GoToMarker(t, "48");
		f->VerifyCurrentLineContent(t, R"TS(    private foo(s);)TS");
		f->GoToMarker(t, "49");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "50");
		f->VerifyCurrentLineContent(t, R"TS(class d {)TS");
		f->GoToMarker(t, "51");
		f->VerifyCurrentLineContent(t, R"TS(    private foo(n: number): string;)TS");
		f->GoToMarker(t, "52");
		f->VerifyCurrentLineContent(t, R"TS(    private foo(s: string): string;)TS");
		f->GoToMarker(t, "53");
		f->VerifyCurrentLineContent(t, R"TS(    private foo(ns: any) {)TS");
		f->GoToMarker(t, "54");
		f->VerifyCurrentLineContent(t, R"TS(        return ns.toString();)TS");
		f->GoToMarker(t, "55");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "56");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "57");
		f->VerifyCurrentLineContent(t, R"TS(class e {)TS");
		f->GoToMarker(t, "58");
		f->VerifyCurrentLineContent(t, R"TS(    private foo(s: string): string;)TS");
		f->GoToMarker(t, "59");
		f->VerifyCurrentLineContent(t, R"TS(    private foo(n: number): string;)TS");
		f->GoToMarker(t, "60");
		f->VerifyCurrentLineContent(t, R"TS(    private foo(ns: any) {)TS");
		f->GoToMarker(t, "61");
		f->VerifyCurrentLineContent(t, R"TS(        return ns.toString();)TS");
		f->GoToMarker(t, "62");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "63");
		f->VerifyCurrentLineContent(t, R"TS(    protected bar() { })TS");
		f->GoToMarker(t, "64");
		f->VerifyCurrentLineContent(t, R"TS(    protected static bar2() { })TS");
		f->GoToMarker(t, "65");
		f->VerifyCurrentLineContent(t, R"TS(    private pv4: number =)TS");
		f->GoToMarker(t, "66");
		f->VerifyCurrentLineContent(t, R"TS(        {};)TS");
		f->GoToMarker(t, "END");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnClasses, TestFormattingOnClasses);

// formattingOnCloseBrace_test.go

// formattingOnCloseBrace_test.go
static void TestFormattingOnCloseBrace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class foo    {
    /**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, "}");
		f->GoToBOF(t);
		f->VerifyCurrentLineContent(t, R"TS(class foo {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnCloseBrace, TestFormattingOnCloseBrace);

// formattingOnClosingBracket_test.go

// formattingOnClosingBracket_test.go
static void TestFormattingOnClosingBracket(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f( ) {/*1*/
var     x = 3;/*2*/
    var z = 2   ;/*3*/
    a  = z  ++ - 2 *  x ;/*4*/
        for ( ; ; ) {/*5*/
    a+=(g +g)*a%t;/*6*/
        b --                          ;/*7*/
}/*8*/

    switch ( a  )/*9*/
    {
        case 1  :     {/*10*/
    a ++  ;/*11*/
        b--;/*12*/
    if(a===a)/*13*/
                return;/*14*/
    else/*15*/
        {
            for(a in b)/*16*/
                if(a!=a)/*17*/
    {
    for(a in b)/*18*/
            {
a++;/*19*/
        }/*20*/
                }/*21*/
    }/*22*/
        }/*23*/
    default:/*24*/
        break;/*25*/
    }/*26*/
}/*27*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts874 = f->GetOptions();
		opts874.FormatCodeSettings.InsertSpaceAfterSemicolonInForStatements = Tristate::True;
		f->Configure(t, opts874);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(function f() {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    var x = 3;)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    var z = 2;)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(    a = z++ - 2 * x;)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    for (; ;) {)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(        a += (g + g) * a % t;)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(        b--;)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(    switch (a) {)TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS(        case 1: {)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS(            a++;)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(            b--;)TS");
		f->GoToMarker(t, "13");
		f->VerifyCurrentLineContent(t, R"TS(            if (a === a))TS");
		f->GoToMarker(t, "14");
		f->VerifyCurrentLineContent(t, R"TS(                return;)TS");
		f->GoToMarker(t, "15");
		f->VerifyCurrentLineContent(t, R"TS(            else {)TS");
		f->GoToMarker(t, "16");
		f->VerifyCurrentLineContent(t, R"TS(                for (a in b))TS");
		f->GoToMarker(t, "17");
		f->VerifyCurrentLineContent(t, R"TS(                    if (a != a) {)TS");
		f->GoToMarker(t, "18");
		f->VerifyCurrentLineContent(t, R"TS(                        for (a in b) {)TS");
		f->GoToMarker(t, "19");
		f->VerifyCurrentLineContent(t, R"TS(                            a++;)TS");
		f->GoToMarker(t, "20");
		f->VerifyCurrentLineContent(t, R"TS(                        })TS");
		f->GoToMarker(t, "21");
		f->VerifyCurrentLineContent(t, R"TS(                    })TS");
		f->GoToMarker(t, "22");
		f->VerifyCurrentLineContent(t, R"TS(            })TS");
		f->GoToMarker(t, "23");
		f->VerifyCurrentLineContent(t, R"TS(        })TS");
		f->GoToMarker(t, "24");
		f->VerifyCurrentLineContent(t, R"TS(        default:)TS");
		f->GoToMarker(t, "25");
		f->VerifyCurrentLineContent(t, R"TS(            break;)TS");
		f->GoToMarker(t, "26");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "27");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnClosingBracket, TestFormattingOnClosingBracket);

// formattingOnCommaOperator_test.go

// formattingOnCommaOperator_test.go
static void TestFormattingOnCommaOperator(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var v1 = ((1, 2, 3), 4, 5, (6, 7));/*1*/
function f1() {
    var a = 1;
    return a, v1, a;/*2*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(var v1 = ((1, 2, 3), 4, 5, (6, 7));)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    return a, v1, a;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnCommaOperator, TestFormattingOnCommaOperator);

// formattingOnConstructorSignature_test.go

// formattingOnConstructorSignature_test.go
static void TestFormattingOnConstructorSignature(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/interface Gourai { new   () {} }
/*2*/type Stylet = { new   () {} })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(interface Gourai { new() { } })TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(type Stylet = { new() { } })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnConstructorSignature, TestFormattingOnConstructorSignature);

// formattingOnDoWhileNoSemicolon_test.go

// formattingOnDoWhileNoSemicolon_test.go
static void TestFormattingOnDoWhileNoSemicolon(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*2*/do {
/*3*/    for (var i = 0; i < 10; i++)
/*4*/        i -= 2
/*5*/        }/*1*/while (1 !== 1))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, R"TS(
)TS");
		f->VerifyCurrentLineContent(t, R"TS(while (1 !== 1))TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(do {)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    for (var i = 0; i < 10; i++))TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(        i -= 2)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnDoWhileNoSemicolon, TestFormattingOnDoWhileNoSemicolon);

// formattingOnDocumentReadyFunction_test.go

// formattingOnDocumentReadyFunction_test.go
static void TestFormattingOnDocumentReadyFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/$    (   document   )   .  ready  (   function   (   )   {
/*2*/    alert    (           'i am ready'  )   ;
/*3*/           }                 );)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS($(document).ready(function() {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    alert('i am ready');)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(});)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnDocumentReadyFunction, TestFormattingOnDocumentReadyFunction);

// formattingOnEmptyInterfaceLiteral_test.go

// formattingOnEmptyInterfaceLiteral_test.go
static void TestFormattingOnEmptyInterfaceLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/    function    foo  (  x  :    {    }    )    {    }

/*2*/foo    (  {     }   )    ;



/*3*/            interface    bar    {
/*4*/                x   :    {     }   ;
/*5*/       y  :       (         )    =>    {     }   ;
/*6*/                                                    })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(function foo(x: {}) { })TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(foo({});)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(interface bar {)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(    x: {};)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    y: () => {};)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnEmptyInterfaceLiteral, TestFormattingOnEmptyInterfaceLiteral);

// formattingOnEnterInComments_test.go

// formattingOnEnterInComments_test.go
static void TestFormattingOnEnterInComments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace me {
    class A {
        /*
         */*1*/
    /*2*/}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->InsertLine(t, "");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnEnterInComments, TestFormattingOnEnterInComments);

// formattingOnEnterInStrings_test.go

// formattingOnEnterInStrings_test.go
static void TestFormattingOnEnterInStrings(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = /*1*/"unclosed string literal\/*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "2");
		f->InsertLine(t, "");
		f->InsertLine(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(var x = "unclosed string literal\)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnEnterInStrings, TestFormattingOnEnterInStrings);

// formattingOnEnter_test.go

// formattingOnEnter_test.go
static void TestFormattingOnEnter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class foo { }
class bar {/**/ }
// new line here)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->InsertLine(t, "");
		f->VerifyCurrentFileContent(t, R"TS(class foo { }
class bar {
}
// new line here)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnEnter, TestFormattingOnEnter);

// formattingOnInterfaces_test.go

// formattingOnInterfaces_test.go
static void TestFormattingOnInterfaces(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/interface Blah 
{
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(interface Blah {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnInterfaces, TestFormattingOnInterfaces);

// formattingOnInvalidCodes_test.go

// formattingOnInvalidCodes_test.go
static void TestFormattingOnInvalidCodes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/*1*/var a;var c          , b;var  $d
/*2*/var $e
/*3*/var f
/*4*/a++;b++;

/*5*/function        f     (     )        {
/*6*/    for (i = 0; i < 10; i++) {
/*7*/        k = abc + 123 ^ d;
/*8*/        a = XYZ[m  (a[b[c][d]])];
/*9*/        break;

/*10*/        switch ( variable){
/*11*/       case  1: abc += 425;
/*12*/break;
/*13*/case 404 : a [x--/2]%=3 ;
/*14*/                    break ;
/*15*/                case vari : v[--x ] *=++y*( m + n / k[z]);
/*16*/                for (a in b){
/*17*/             for (a = 0; a < 10; ++a) {
/*18*/              a++;--a;
/*19*/                   if (a == b) {
/*20*/                          a++;b--;
/*21*/                     }
/*22*/else
/*23*/if (a == c){
/*24*/++a;
/*25*/(--c)+=d;
/*26*/$c = $a + --$b;
/*27*/}
/*28*/if (a == b)
/*29*/if (a != b) {
/*30*/ if (a !== b)
/*31*/ if (a === b)
/*32*/ --a;
/*33*/ else
/*34*/  --a;
/*35*/  else {
/*36*/  a--;++b;
/*37*/a++
/*38*/                    }
/*39*/                    }
/*40*/                    }
/*41*/                    for (x in y) {
/*42*/m-=m;
/*43*/k=1+2+3+4;
/*44*/}
/*45*/}
/*46*/    break;

/*47*/    }
/*48*/    }
/*49*/    var a  ={b:function(){}};
/*50*/    return {a:1,b:2}
/*51*/}

/*52*/var z = 1;
/*53*/            for (i = 0; i < 10; i++)
/*54*/     for (j = 0; j < 10; j++)
/*55*/for (k = 0; k < 10; ++k) {
/*56*/z++;
/*57*/}

/*58*/for (k = 0; k < 10; k += 2) {
/*59*/z++;
/*60*/}

/*61*/    $(document).ready ();


/*62*/ function  pageLoad() {
/*63*/ $('#TextBox1' ) .     unbind   (  ) ;
/*64*/$('#TextBox1' ) . datepicker ( ) ;
/*65*/}

/*66*/        function pageLoad    (     )    {
/*67*/    var webclass=[
/*68*/                { 'student'     :/*69*/
/*70*/                { 'id': '1', 'name': 'Linda Jones', 'legacySkill': 'Access, VB 5.0' }
/*71*/        }   ,
/*72*/{    'student':/*73*/
/*74*/{'id':'2','name':'Adam Davidson','legacySkill':'Cobol,MainFrame'}
/*75*/}      ,
/*76*/    { 'student':/*77*/
/*78*/{   'id':'3','name':'Charles Boyer' ,'legacySkill':'HTML, XML'}
/*79*/}
/*80*/    ];

/*81*/$create(Sys.UI.DataView,{data:webclass},null,null,$get('SList'));

/*82*/}

/*83*/$( document ).ready(function(){
/*84*/alert('hello');
/*85*/    } ) ;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(var a; var c, b; var $d)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(var $e)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(var f)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(a++; b++;)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(function f() {)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    for (i = 0; i < 10; i++) {)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(        k = abc + 123 ^ d;)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(        a = XYZ[m(a[b[c][d]])];)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(        break;)TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS(        switch (variable) {)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS(            case 1: abc += 425;)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(                break;)TS");
		f->GoToMarker(t, "13");
		f->VerifyCurrentLineContent(t, R"TS(            case 404: a[x-- / 2] %= 3;)TS");
		f->GoToMarker(t, "14");
		f->VerifyCurrentLineContent(t, R"TS(                break;)TS");
		f->GoToMarker(t, "15");
		f->VerifyCurrentLineContent(t, R"TS(            case vari: v[--x] *= ++y * (m + n / k[z]);)TS");
		f->GoToMarker(t, "16");
		f->VerifyCurrentLineContent(t, R"TS(                for (a in b) {)TS");
		f->GoToMarker(t, "17");
		f->VerifyCurrentLineContent(t, R"TS(                    for (a = 0; a < 10; ++a) {)TS");
		f->GoToMarker(t, "18");
		f->VerifyCurrentLineContent(t, R"TS(                        a++; --a;)TS");
		f->GoToMarker(t, "19");
		f->VerifyCurrentLineContent(t, R"TS(                        if (a == b) {)TS");
		f->GoToMarker(t, "20");
		f->VerifyCurrentLineContent(t, R"TS(                            a++; b--;)TS");
		f->GoToMarker(t, "21");
		f->VerifyCurrentLineContent(t, R"TS(                        })TS");
		f->GoToMarker(t, "22");
		f->VerifyCurrentLineContent(t, R"TS(                        else)TS");
		f->GoToMarker(t, "23");
		f->VerifyCurrentLineContent(t, R"TS(                            if (a == c) {)TS");
		f->GoToMarker(t, "24");
		f->VerifyCurrentLineContent(t, R"TS(                                ++a;)TS");
		f->GoToMarker(t, "25");
		f->VerifyCurrentLineContent(t, R"TS(                                (--c) += d;)TS");
		f->GoToMarker(t, "26");
		f->VerifyCurrentLineContent(t, R"TS(                                $c = $a + --$b;)TS");
		f->GoToMarker(t, "27");
		f->VerifyCurrentLineContent(t, R"TS(                            })TS");
		f->GoToMarker(t, "28");
		f->VerifyCurrentLineContent(t, R"TS(                        if (a == b))TS");
		f->GoToMarker(t, "29");
		f->VerifyCurrentLineContent(t, R"TS(                            if (a != b) {)TS");
		f->GoToMarker(t, "30");
		f->VerifyCurrentLineContent(t, R"TS(                                if (a !== b))TS");
		f->GoToMarker(t, "31");
		f->VerifyCurrentLineContent(t, R"TS(                                    if (a === b))TS");
		f->GoToMarker(t, "32");
		f->VerifyCurrentLineContent(t, R"TS(                                        --a;)TS");
		f->GoToMarker(t, "33");
		f->VerifyCurrentLineContent(t, R"TS(                                    else)TS");
		f->GoToMarker(t, "34");
		f->VerifyCurrentLineContent(t, R"TS(                                        --a;)TS");
		f->GoToMarker(t, "35");
		f->VerifyCurrentLineContent(t, R"TS(                                else {)TS");
		f->GoToMarker(t, "36");
		f->VerifyCurrentLineContent(t, R"TS(                                    a--; ++b;)TS");
		f->GoToMarker(t, "37");
		f->VerifyCurrentLineContent(t, R"TS(                                    a++)TS");
		f->GoToMarker(t, "38");
		f->VerifyCurrentLineContent(t, R"TS(                                })TS");
		f->GoToMarker(t, "39");
		f->VerifyCurrentLineContent(t, R"TS(                            })TS");
		f->GoToMarker(t, "40");
		f->VerifyCurrentLineContent(t, R"TS(                    })TS");
		f->GoToMarker(t, "41");
		f->VerifyCurrentLineContent(t, R"TS(                    for (x in y) {)TS");
		f->GoToMarker(t, "42");
		f->VerifyCurrentLineContent(t, R"TS(                        m -= m;)TS");
		f->GoToMarker(t, "43");
		f->VerifyCurrentLineContent(t, R"TS(                        k = 1 + 2 + 3 + 4;)TS");
		f->GoToMarker(t, "44");
		f->VerifyCurrentLineContent(t, R"TS(                    })TS");
		f->GoToMarker(t, "45");
		f->VerifyCurrentLineContent(t, R"TS(                })TS");
		f->GoToMarker(t, "46");
		f->VerifyCurrentLineContent(t, R"TS(                break;)TS");
		f->GoToMarker(t, "47");
		f->VerifyCurrentLineContent(t, R"TS(        })TS");
		f->GoToMarker(t, "48");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "49");
		f->VerifyCurrentLineContent(t, R"TS(    var a = { b: function() { } };)TS");
		f->GoToMarker(t, "50");
		f->VerifyCurrentLineContent(t, R"TS(    return { a: 1, b: 2 })TS");
		f->GoToMarker(t, "51");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "52");
		f->VerifyCurrentLineContent(t, R"TS(var z = 1;)TS");
		f->GoToMarker(t, "53");
		f->VerifyCurrentLineContent(t, R"TS(for (i = 0; i < 10; i++))TS");
		f->GoToMarker(t, "54");
		f->VerifyCurrentLineContent(t, R"TS(    for (j = 0; j < 10; j++))TS");
		f->GoToMarker(t, "55");
		f->VerifyCurrentLineContent(t, R"TS(        for (k = 0; k < 10; ++k) {)TS");
		f->GoToMarker(t, "56");
		f->VerifyCurrentLineContent(t, R"TS(            z++;)TS");
		f->GoToMarker(t, "57");
		f->VerifyCurrentLineContent(t, R"TS(        })TS");
		f->GoToMarker(t, "58");
		f->VerifyCurrentLineContent(t, R"TS(for (k = 0; k < 10; k += 2) {)TS");
		f->GoToMarker(t, "59");
		f->VerifyCurrentLineContent(t, R"TS(    z++;)TS");
		f->GoToMarker(t, "60");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "61");
		f->VerifyCurrentLineContent(t, R"TS($(document).ready();)TS");
		f->GoToMarker(t, "62");
		f->VerifyCurrentLineContent(t, R"TS(function pageLoad() {)TS");
		f->GoToMarker(t, "63");
		f->VerifyCurrentLineContent(t, R"TS(    $('#TextBox1').unbind();)TS");
		f->GoToMarker(t, "64");
		f->VerifyCurrentLineContent(t, R"TS(    $('#TextBox1').datepicker();)TS");
		f->GoToMarker(t, "65");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "66");
		f->VerifyCurrentLineContent(t, R"TS(function pageLoad() {)TS");
		f->GoToMarker(t, "67");
		f->VerifyCurrentLineContent(t, R"TS(    var webclass = [)TS");
		f->GoToMarker(t, "68");
		f->VerifyCurrentLineContent(t, R"TS(        {)TS");
		f->GoToMarker(t, "69");
		f->VerifyCurrentLineContent(t, R"TS(            'student':)TS");
		f->GoToMarker(t, "70");
		f->VerifyCurrentLineContent(t, R"TS(                { 'id': '1', 'name': 'Linda Jones', 'legacySkill': 'Access, VB 5.0' })TS");
		f->GoToMarker(t, "71");
		f->VerifyCurrentLineContent(t, R"TS(        },)TS");
		f->GoToMarker(t, "72");
		f->VerifyCurrentLineContent(t, R"TS(        {)TS");
		f->GoToMarker(t, "73");
		f->VerifyCurrentLineContent(t, R"TS(            'student':)TS");
		f->GoToMarker(t, "74");
		f->VerifyCurrentLineContent(t, R"TS(                { 'id': '2', 'name': 'Adam Davidson', 'legacySkill': 'Cobol,MainFrame' })TS");
		f->GoToMarker(t, "75");
		f->VerifyCurrentLineContent(t, R"TS(        },)TS");
		f->GoToMarker(t, "76");
		f->VerifyCurrentLineContent(t, R"TS(        {)TS");
		f->GoToMarker(t, "77");
		f->VerifyCurrentLineContent(t, R"TS(            'student':)TS");
		f->GoToMarker(t, "78");
		f->VerifyCurrentLineContent(t, R"TS(                { 'id': '3', 'name': 'Charles Boyer', 'legacySkill': 'HTML, XML' })TS");
		f->GoToMarker(t, "79");
		f->VerifyCurrentLineContent(t, R"TS(        })TS");
		f->GoToMarker(t, "80");
		f->VerifyCurrentLineContent(t, R"TS(    ];)TS");
		f->GoToMarker(t, "81");
		f->VerifyCurrentLineContent(t, R"TS(    $create(Sys.UI.DataView, { data: webclass }, null, null, $get('SList'));)TS");
		f->GoToMarker(t, "82");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "83");
		f->VerifyCurrentLineContent(t, R"TS($(document).ready(function() {)TS");
		f->GoToMarker(t, "84");
		f->VerifyCurrentLineContent(t, R"TS(    alert('hello');)TS");
		f->GoToMarker(t, "85");
		f->VerifyCurrentLineContent(t, R"TS(});)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnInvalidCodes, TestFormattingOnInvalidCodes);

// formattingOnModuleIndentation_test.go

// formattingOnModuleIndentation_test.go
static void TestFormattingOnModuleIndentation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(  namespace     Foo    {
    export    namespace    A  .   B  .   C     {      }/**/
               })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToBOF(t);
		f->VerifyCurrentLineContent(t, R"TS(namespace Foo {)TS");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(    export namespace A.B.C { })TS");
		f->GoToEOF(t);
		f->VerifyCurrentLineContent(t, R"TS(})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnModuleIndentation, TestFormattingOnModuleIndentation);

// formattingOnNestedDoWhileByEnter_test.go

// formattingOnNestedDoWhileByEnter_test.go
static void TestFormattingOnNestedDoWhileByEnter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*2*/do{
/*3*/do/*1*/{
/*4*/do{
/*5*/}while(a!==b)
/*6*/}while(a!==b)
/*7*/}while(a!==b))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, R"TS(
)TS");
		f->VerifyCurrentLineContent(t, R"TS(    {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(do{)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    do)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(do{)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(}while(a!==b))TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(}while(a!==b))TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(}while(a!==b))TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnNestedDoWhileByEnter, TestFormattingOnNestedDoWhileByEnter);

// formattingOnNestedStatements_test.go

// formattingOnNestedStatements_test.go
static void TestFormattingOnNestedStatements(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS({
/*1*/{
/*3*/test
}/*2*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatSelection(t, "1", "2");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    {)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(        test)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnNestedStatements, TestFormattingOnNestedStatements);

// formattingOnObjectLiteral_test.go

// formattingOnObjectLiteral_test.go
static void TestFormattingOnObjectLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(var x = /*1*/{foo:/*2*/ 1,
bar: "tt",/*3*/
boo: /*4*/1 + 5}/*5*/;

var x2 = /*6*/{foo/*7*/: 1,
bar: /*8*/"tt",boo:1+5}/*9*/;

function Foo() {/*10*/
var typeICalc = {/*11*/
clear: {/*12*/
"()": [1, 2, 3]/*13*/
}/*14*/
}/*15*/
}/*16*/

// Rule for object literal members for the "value" of the memebr to follow the indent/*17*/
// of the member, i.e. the relative position of the value is maintained when the member/*18*/
// is indented./*19*/
var x2 = {/*20*/
  foo:/*21*/
3,/*22*/
          'bar':/*23*/
                    { a: 1, b : 2}/*24*/
};/*25*/

var x={    };/*26*/
var y = {};/*27*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(var x = {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    foo: 1,)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    bar: "tt",)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(    boo: 1 + 5)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(};)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(var x2 = {)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(    foo: 1,)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(    bar: "tt", boo: 1 + 5)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(};)TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS(function Foo() {)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS(    var typeICalc = {)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(        clear: {)TS");
		f->GoToMarker(t, "13");
		f->VerifyCurrentLineContent(t, R"TS(            "()": [1, 2, 3])TS");
		f->GoToMarker(t, "14");
		f->VerifyCurrentLineContent(t, R"TS(        })TS");
		f->GoToMarker(t, "15");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "16");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "17");
		f->VerifyCurrentLineContent(t, R"TS(// Rule for object literal members for the "value" of the memebr to follow the indent)TS");
		f->GoToMarker(t, "18");
		f->VerifyCurrentLineContent(t, R"TS(// of the member, i.e. the relative position of the value is maintained when the member)TS");
		f->GoToMarker(t, "19");
		f->VerifyCurrentLineContent(t, R"TS(// is indented.)TS");
		f->GoToMarker(t, "20");
		f->VerifyCurrentLineContent(t, R"TS(var x2 = {)TS");
		f->GoToMarker(t, "21");
		f->VerifyCurrentLineContent(t, R"TS(    foo:)TS");
		f->GoToMarker(t, "22");
		f->VerifyCurrentLineContent(t, R"TS(        3,)TS");
		f->GoToMarker(t, "23");
		f->VerifyCurrentLineContent(t, R"TS(    'bar':)TS");
		f->GoToMarker(t, "24");
		f->VerifyCurrentLineContent(t, R"TS(        { a: 1, b: 2 })TS");
		f->GoToMarker(t, "25");
		f->VerifyCurrentLineContent(t, R"TS(};)TS");
		f->GoToMarker(t, "26");
		f->VerifyCurrentLineContent(t, R"TS(var x = {};)TS");
		f->GoToMarker(t, "27");
		f->VerifyCurrentLineContent(t, R"TS(var y = {};)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnObjectLiteral, TestFormattingOnObjectLiteral);

// formattingOnOpenBraceOfFunctions_test.go

// formattingOnOpenBraceOfFunctions_test.go
static void TestFormattingOnOpenBraceOfFunctions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**/function T2_y()
{
Plugin.T1.t1_x();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(function T2_y() {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnOpenBraceOfFunctions, TestFormattingOnOpenBraceOfFunctions);

// formattingOnSingleLineBlocks_test.go

// formattingOnSingleLineBlocks_test.go
static void TestFormattingOnSingleLineBlocks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C
{}
if (true)
{})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(class C { }
if (true) { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnSingleLineBlocks, TestFormattingOnSingleLineBlocks);

// formattingOnStatementsWithNoSemicolon_test.go

// formattingOnStatementsWithNoSemicolon_test.go
static void TestFormattingOnStatementsWithNoSemicolon(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/do
     { var a/*2*/
/*3*/}   while (1)
/*4*/function f() {
/*5*/    var s = 1
/*6*/            }
/*7*/switch (t) {
/*8*/    case 1:
/*9*/{
/*10*/test
/*11*/}
/*12*/}
/*13*/do{do{do{}while(a!==b)}while(a!==b)}while(a!==b)
/*14*/do{
/*15*/do{
/*16*/do{
/*17*/}while(a!==b)
/*18*/}while(a!==b)
/*19*/}while(a!==b)
/*20*/for(var i=0;i<10;i++){
/*21*/for(var j=0;j<10;j++){
/*22*/j-=i
/*23*/}/*24*/}
/*25*/function foo() {
/*26*/try {
/*27*/x+=2
/*28*/}
/*29*/catch( e){
/*30*/x+=2
/*31*/}finally {
/*32*/x+=2
/*33*/}
/*34*/}
/*35*/do     { var a }   while (1)
    foo(function (file) {/*49*/
        return 0/*50*/
    }).then(function (doc) {/*51*/
        return 1/*52*/
    });/*53*/
/*54*/if(1)
/*55*/if(1)
/*56*/x++
/*57*/else
/*58*/if(1)
/*59*/x+=2
/*60*/else
/*61*/x+=2



/*62*/;
         do do do do/*63*/
                test;/*64*/
            while (0)/*65*/
         while (0)/*66*/
            while (0)/*67*/
         while (0)/*68*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(do {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    var a)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(} while (1))TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(function f() {)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    var s = 1)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(switch (t) {)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(    case 1:)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(        {)TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS(            test)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS(        })TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "13");
		f->VerifyCurrentLineContent(t, R"TS(do { do { do { } while (a !== b) } while (a !== b) } while (a !== b))TS");
		f->GoToMarker(t, "14");
		f->VerifyCurrentLineContent(t, R"TS(do {)TS");
		f->GoToMarker(t, "15");
		f->VerifyCurrentLineContent(t, R"TS(    do {)TS");
		f->GoToMarker(t, "16");
		f->VerifyCurrentLineContent(t, R"TS(        do {)TS");
		f->GoToMarker(t, "17");
		f->VerifyCurrentLineContent(t, R"TS(        } while (a !== b))TS");
		f->GoToMarker(t, "18");
		f->VerifyCurrentLineContent(t, R"TS(    } while (a !== b))TS");
		f->GoToMarker(t, "19");
		f->VerifyCurrentLineContent(t, R"TS(} while (a !== b))TS");
		f->GoToMarker(t, "20");
		f->VerifyCurrentLineContent(t, R"TS(for (var i = 0; i < 10; i++) {)TS");
		f->GoToMarker(t, "21");
		f->VerifyCurrentLineContent(t, R"TS(    for (var j = 0; j < 10; j++) {)TS");
		f->GoToMarker(t, "22");
		f->VerifyCurrentLineContent(t, R"TS(        j -= i)TS");
		f->GoToMarker(t, "23");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "24");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "25");
		f->VerifyCurrentLineContent(t, R"TS(function foo() {)TS");
		f->GoToMarker(t, "26");
		f->VerifyCurrentLineContent(t, R"TS(    try {)TS");
		f->GoToMarker(t, "27");
		f->VerifyCurrentLineContent(t, R"TS(        x += 2)TS");
		f->GoToMarker(t, "28");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "29");
		f->VerifyCurrentLineContent(t, R"TS(    catch (e) {)TS");
		f->GoToMarker(t, "30");
		f->VerifyCurrentLineContent(t, R"TS(        x += 2)TS");
		f->GoToMarker(t, "31");
		f->VerifyCurrentLineContent(t, R"TS(    } finally {)TS");
		f->GoToMarker(t, "32");
		f->VerifyCurrentLineContent(t, R"TS(        x += 2)TS");
		f->GoToMarker(t, "33");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "34");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "35");
		f->VerifyCurrentLineContent(t, R"TS(do { var a } while (1))TS");
		f->GoToMarker(t, "49");
		f->VerifyCurrentLineContent(t, R"TS(foo(function(file) {)TS");
		f->GoToMarker(t, "50");
		f->VerifyCurrentLineContent(t, R"TS(    return 0)TS");
		f->GoToMarker(t, "51");
		f->VerifyCurrentLineContent(t, R"TS(}).then(function(doc) {)TS");
		f->GoToMarker(t, "52");
		f->VerifyCurrentLineContent(t, R"TS(    return 1)TS");
		f->GoToMarker(t, "53");
		f->VerifyCurrentLineContent(t, R"TS(});)TS");
		f->GoToMarker(t, "54");
		f->VerifyCurrentLineContent(t, R"TS(if (1))TS");
		f->GoToMarker(t, "55");
		f->VerifyCurrentLineContent(t, R"TS(    if (1))TS");
		f->GoToMarker(t, "56");
		f->VerifyCurrentLineContent(t, R"TS(        x++)TS");
		f->GoToMarker(t, "57");
		f->VerifyCurrentLineContent(t, R"TS(    else)TS");
		f->GoToMarker(t, "58");
		f->VerifyCurrentLineContent(t, R"TS(        if (1))TS");
		f->GoToMarker(t, "59");
		f->VerifyCurrentLineContent(t, R"TS(            x += 2)TS");
		f->GoToMarker(t, "60");
		f->VerifyCurrentLineContent(t, R"TS(        else)TS");
		f->GoToMarker(t, "61");
		f->VerifyCurrentLineContent(t, R"TS(            x += 2)TS");
		f->GoToMarker(t, "62");
		f->VerifyCurrentLineContent(t, R"TS(                ;)TS");
		f->GoToMarker(t, "63");
		f->VerifyCurrentLineContent(t, R"TS(do do do do)TS");
		f->GoToMarker(t, "64");
		f->VerifyCurrentLineContent(t, R"TS(    test;)TS");
		f->GoToMarker(t, "65");
		f->VerifyCurrentLineContent(t, R"TS(while (0))TS");
		f->GoToMarker(t, "66");
		f->VerifyCurrentLineContent(t, R"TS(while (0))TS");
		f->GoToMarker(t, "67");
		f->VerifyCurrentLineContent(t, R"TS(while (0))TS");
		f->GoToMarker(t, "68");
		f->VerifyCurrentLineContent(t, R"TS(while (0))TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnStatementsWithNoSemicolon, TestFormattingOnStatementsWithNoSemicolon);

// formattingOnTabAfterCloseCurly_test.go

// formattingOnTabAfterCloseCurly_test.go
static void TestFormattingOnTabAfterCloseCurly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Tools {/*1*/
    export enum NodeType {/*2*/
        Error,/*3*/
        Comment,/*4*/
    }   /*5*/
    export enum foob/*6*/
    {
        Blah=1, Bleah=2/*7*/
    }/*8*/
}/*9*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(namespace Tools {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    export enum NodeType {)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(        Error,)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(        Comment,)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    export enum foob {)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(        Blah = 1, Bleah = 2)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnTabAfterCloseCurly, TestFormattingOnTabAfterCloseCurly);

// formattingOnVariety_test.go

// formattingOnVariety_test.go
static void TestFormattingOnVariety(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(a,b,c,d){/*1*/
for(var i=0;i<10;i++){/*2*/
var a=0;/*3*/
var b=a+a+a*a%a/2-1;/*4*/
b+=a;/*5*/
++b;/*6*/
f(a,b,c,d);/*7*/
if(1===1){/*8*/
var m=function(e,f){/*9*/
return e^f;/*10*/
}/*11*/
}/*12*/
}/*13*/
}/*14*/

for (var i = 0   ; i < this.foo(); i++) {/*15*/
}/*16*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(function f(a, b, c, d) {)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    for (var i = 0; i < 10; i++) {)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(        var a = 0;)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(        var b = a + a + a * a % a / 2 - 1;)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(        b += a;)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(        ++b;)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(        f(a, b, c, d);)TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(        if (1 === 1) {)TS");
		f->GoToMarker(t, "9");
		f->VerifyCurrentLineContent(t, R"TS(            var m = function(e, f) {)TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS(                return e ^ f;)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS(            })TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(        })TS");
		f->GoToMarker(t, "13");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
		f->GoToMarker(t, "14");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
		f->GoToMarker(t, "15");
		f->VerifyCurrentLineContent(t, R"TS(for (var i = 0; i < this.foo(); i++) {)TS");
		f->GoToMarker(t, "16");
		f->VerifyCurrentLineContent(t, R"TS(})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnVariety, TestFormattingOnVariety);

// formattingOverrideKeyword_test.go

// formattingOverrideKeyword_test.go
static void TestFormattingOverrideKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class MyClass {
  override     myMethod() { };/*1*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    override myMethod() { };)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOverrideKeyword, TestFormattingOverrideKeyword);

// formattingQMark_test.go

// formattingQMark_test.go
static void TestFormattingQMark(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A {
/*1*/    foo?     ();
/*2*/    foo?             <T>();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    foo?();)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    foo?<T>();)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingQMark, TestFormattingQMark);

// formattingReadonly_test.go

// formattingReadonly_test.go
static void TestFormattingReadonly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
  readonly    property1: {};/*1*/
  public readonly   property2: {};/*2*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    readonly property1: {};)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    public readonly property2: {};)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingReadonly, TestFormattingReadonly);

// formattingRegexes_test.go

// formattingRegexes_test.go
static void TestFormattingRegexes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(removeAllButLast(sortedTypes, undefinedType, /keepNullableType**/ true)/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(removeAllButLast(sortedTypes, undefinedType, /keepNullableType**/ true);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingRegexes, TestFormattingRegexes);

// formattingReplaceTabsWithSpaces_test.go

// formattingReplaceTabsWithSpaces_test.go
static void TestFormattingReplaceTabsWithSpaces(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Foo {
/*1*/				class Test { }
/*2*/			class Test { }
/*3*/class Test { }
/*4*/			 class Test { }
/*5*/   class Test { }
/*6*/    class Test { }
/*7*/     class Test { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    class Test { })TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    class Test { })TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    class Test { })TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(    class Test { })TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    class Test { })TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    class Test { })TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(    class Test { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingReplaceTabsWithSpaces, TestFormattingReplaceTabsWithSpaces);

// formattingSingleLineWithNewLineOptionSet_test.go

// formattingSingleLineWithNewLineOptionSet_test.go
static void TestFormattingSingleLineWithNewLineOptionSet(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/namespace Default{}
/*2*/function foo(){}
/*3*/if (true){}
/*4*/function boo() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts211 = f->GetOptions();
		opts211.FormatCodeSettings.PlaceOpenBraceOnNewLineForFunctions = Tristate::True;
		f->Configure(t, opts211);
		auto opts279 = f->GetOptions();
		opts279.FormatCodeSettings.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::True;
		f->Configure(t, opts279);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(namespace Default { })TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(function foo() { })TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(if (true) { })TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(function boo())TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingSingleLineWithNewLineOptionSet, TestFormattingSingleLineWithNewLineOptionSet);

// formattingSkippedTokens_test.go

// formattingSkippedTokens_test.go
static void TestFormattingSkippedTokens(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/foo(): Bar { }
/*2*/function Foo      () #   { }
/*3*/4+:5
 namespace M {
function a(
/*4*/    : T) { }
}
/*5*/var x       =)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(foo(): Bar { })TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(function Foo() #   { })TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(4 +: 5)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(    : T) { })TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(var x =)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingSkippedTokens, TestFormattingSkippedTokens);

// formattingSpaceAfterCommaBeforeOpenParen_test.go

// formattingSpaceAfterCommaBeforeOpenParen_test.go
static void TestFormattingSpaceAfterCommaBeforeOpenParen(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(foo(a,(b))/*1*/
foo(a,(<b>c).d)/*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(foo(a, (b));)TS");
		f->GoToMarker(t, "2");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(foo(a, (<b>c).d);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingSpaceAfterCommaBeforeOpenParen, TestFormattingSpaceAfterCommaBeforeOpenParen);

// formattingSpaceBeforeCloseParen_test.go

// formattingSpaceBeforeCloseParen_test.go
static void TestFormattingSpaceBeforeCloseParen(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/({});
/*2*/(  {});
/*3*/({foo:42});
/*4*/(  {foo:42}  );
/*5*/var bar = (function (a) { });)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts235 = f->GetOptions();
		opts235.FormatCodeSettings.InsertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis = Tristate::True;
		f->Configure(t, opts235);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(( {} );)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(( {} );)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(( { foo: 42 } );)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(( { foo: 42 } );)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(var bar = ( function( a ) { } );)TS");
		auto opts674 = f->GetOptions();
		opts674.FormatCodeSettings.InsertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis = Tristate::False;
		f->Configure(t, opts674);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(({});)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(({});)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(({ foo: 42 });)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(({ foo: 42 });)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(var bar = (function(a) { });)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingSpaceBeforeCloseParen, TestFormattingSpaceBeforeCloseParen);

// formattingSpaceBeforeFunctionParen_test.go

// formattingSpaceBeforeFunctionParen_test.go
static void TestFormattingSpaceBeforeFunctionParen(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/function foo() { }
/*2*/function boo  () { }
/*3*/var bar = function foo() { };
/*4*/var foo = { bar() { } };
/*5*/function tmpl <T> () { }
/*6*/var f = function*() { };
/*7*/function* g () { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts333 = f->GetOptions();
		opts333.FormatCodeSettings.InsertSpaceBeforeFunctionParenthesis = Tristate::True;
		f->Configure(t, opts333);
		auto opts414 = f->GetOptions();
		opts414.FormatCodeSettings.InsertSpaceAfterFunctionKeywordForAnonymousFunctions = Tristate::False;
		f->Configure(t, opts414);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(function foo () { })TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(function boo () { })TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(var bar = function foo () { };)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(var foo = { bar () { } };)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(function tmpl<T> () { })TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(var f = function*() { };)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(function* g () { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingSpaceBeforeFunctionParen, TestFormattingSpaceBeforeFunctionParen);

// formattingSpaceBetweenOptionalChaining_test.go

// formattingSpaceBetweenOptionalChaining_test.go
static void TestFormattingSpaceBetweenOptionalChaining(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/a    ?.    b   ?.   c   .   d;
/*2*/o    .  m()   ?.   length;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(a?.b?.c.d;)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(o.m()?.length;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingSpaceBetweenOptionalChaining, TestFormattingSpaceBetweenOptionalChaining);

// formattingSpaceBetweenParent_test.go

// formattingSpaceBetweenParent_test.go
static void TestFormattingSpaceBetweenParent(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/foo(() => 1);
/*2*/foo(1);
/*3*/if((true)){})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto opts180 = f->GetOptions();
		opts180.FormatCodeSettings.InsertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis = Tristate::True;
		f->Configure(t, opts180);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(foo( () => 1 );)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(foo( 1 );)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(if ( ( true ) ) { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingSpaceBetweenParent, TestFormattingSpaceBetweenParent);

// formattingSpacesAfterConstructor_test.go

// formattingSpacesAfterConstructor_test.go
static void TestFormattingSpacesAfterConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/class test { constructor                   () { } }
/*2*/class test { constructor                   () { } })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(class test { constructor() { } })TS");
		auto opts319 = f->GetOptions();
		opts319.FormatCodeSettings.InsertSpaceAfterConstructor = Tristate::True;
		f->Configure(t, opts319);
		f->FormatDocument(t, "");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(class test { constructor () { } })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingSpacesAfterConstructor, TestFormattingSpacesAfterConstructor);

// formattingTemplatesWithNewline_test.go

// formattingTemplatesWithNewline_test.go
static void TestFormattingTemplatesWithNewline(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((std::string(R"TS()TS") + "`") + std::string(R"TS(${1})TS")) + std::string("`")) + std::string(R"TS(;
)TS")) + std::string("`")) + std::string(R"TS(
)TS")) + std::string("`")) + std::string(R"TS(;/**/1)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, R"TS(
)TS");
		f->VerifyCurrentLineContent(t, R"TS(1)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingTemplatesWithNewline, TestFormattingTemplatesWithNewline);

// formattingTemplates_test.go

// formattingTemplates_test.go
static void TestFormattingTemplates(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((std::string(R"TS(String.call )TS") + "`") + std::string(R"TS(${123})TS")) + std::string("`")) + std::string(R"TS(/*1*/
String.call )TS")) + std::string("`")) + std::string(R"TS(${123} ${456})TS")) + std::string("`")) + std::string(R"TS(/*2*/)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, "String.call`${123}`;");
		f->GoToMarker(t, "2");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, "String.call`${123} ${456}`;");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingTemplates, TestFormattingTemplates);

// formattingTypeInfer_test.go

// formattingTypeInfer_test.go
static void TestFormattingTypeInfer(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
/*L1*/type C<T> = T extends Array<infer U> ? U : never;

/*L2*/  type   C  <  T  >   =   T   extends   Array   <   infer     U  >  ?   U   :   never  ; 

/*L3*/type C<T> = T extends Array<infer U> ? U : T;

/*L4*/  type   C  <  T  >   =   T   extends   Array   <   infer     U  >  ?   U   :   T  ;  

/*L5*/type Foo<T> = T extends { a: infer U, b: infer U } ? U : never;

/*L6*/  type   Foo  <  T  > = T   extends   {   a  :   infer   U  ,   b  :   infer   U   }   ?   U   :   never  ;  

/*L7*/type Bar<T> = T extends { a: (x: infer U) => void, b: (x: infer U) => void } ? U : never;

/*L8*/  type   Bar  <  T  >   =   T   extends   {   a  :   (x  :  infer  U  ) =>   void  ,   b  :   (x  :   infer   U  )   =>   void   }    ?   U   :   never  ;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "L1");
		f->VerifyCurrentLineContent(t, R"TS(type C<T> = T extends Array<infer U> ? U : never;)TS");
		f->GoToMarker(t, "L2");
		f->VerifyCurrentLineContent(t, R"TS(type C<T> = T extends Array<infer U> ? U : never;)TS");
		f->GoToMarker(t, "L3");
		f->VerifyCurrentLineContent(t, R"TS(type C<T> = T extends Array<infer U> ? U : T;)TS");
		f->GoToMarker(t, "L4");
		f->VerifyCurrentLineContent(t, R"TS(type C<T> = T extends Array<infer U> ? U : T;)TS");
		f->GoToMarker(t, "L5");
		f->VerifyCurrentLineContent(t, R"TS(type Foo<T> = T extends { a: infer U, b: infer U } ? U : never;)TS");
		f->GoToMarker(t, "L6");
		f->VerifyCurrentLineContent(t, R"TS(type Foo<T> = T extends { a: infer U, b: infer U } ? U : never;)TS");
		f->GoToMarker(t, "L7");
		f->VerifyCurrentLineContent(t, R"TS(type Bar<T> = T extends { a: (x: infer U) => void, b: (x: infer U) => void } ? U : never;)TS");
		f->GoToMarker(t, "L8");
		f->VerifyCurrentLineContent(t, R"TS(type Bar<T> = T extends { a: (x: infer U) => void, b: (x: infer U) => void } ? U : never;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingTypeInfer, TestFormattingTypeInfer);

// formattingVoid_test.go

// formattingVoid_test.go
static void TestFormattingVoid(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/  var x: () =>           void    ;
/*2*/  var y:     void    ;
/*3*/  function test(a:void,b:string){}
/*4*/  var a, b, c, d;
/*5*/  void    a    ;
/*6*/  void        (0);
/*7*/  b=void(c=1,d=2);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(var x: () => void;)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(var y: void;)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(function test(a: void, b: string) { })TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(void a;)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(void (0);)TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(b = void (c = 1, d = 2);)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingVoid, TestFormattingVoid);

// formattingWithMultilineComments_test.go

// formattingWithMultilineComments_test.go
static void TestFormattingWithMultilineComments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(f(/*
/*2*/         */() => { /*1*/ });)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->InsertLine(t, "");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(         */() => {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingWithMultilineComments, TestFormattingWithMultilineComments);

// formattingofSingleLineBlockConstructs_test.go

// formattingofSingleLineBlockConstructs_test.go
static void TestFormattingofSingleLineBlockConstructs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace InternalModule/*1*/{}
interface MyInterface/*2*/{}
enum E/*3*/{}
class MyClass/*4*/{
constructor()/*cons*/{}
        public MyFunction()/*5*/{return 0;}
public get Getter()/*6*/{}
public set Setter(x)/*7*/{}}
function foo()/*8*/{{}}
(function()/*10*/{});
(() =>/*11*/{});
var x :/*12*/{};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(namespace InternalModule { })TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(interface MyInterface { })TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(enum E { })TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS(class MyClass {)TS");
		f->GoToMarker(t, "cons");
		f->VerifyCurrentLineContent(t, R"TS(    constructor() { })TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS(    public MyFunction() { return 0; })TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(    public get Getter() { })TS");
		f->GoToMarker(t, "7");
		f->VerifyCurrentLineContent(t, R"TS(    public set Setter(x) { })TS");
		f->GoToMarker(t, "8");
		f->VerifyCurrentLineContent(t, R"TS(function foo() { { } })TS");
		f->GoToMarker(t, "10");
		f->VerifyCurrentLineContent(t, R"TS((function() { });)TS");
		f->GoToMarker(t, "11");
		f->VerifyCurrentLineContent(t, R"TS((() => { });)TS");
		f->GoToMarker(t, "12");
		f->VerifyCurrentLineContent(t, R"TS(var x: {};)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingofSingleLineBlockConstructs, TestFormattingofSingleLineBlockConstructs);

// semicolonFormattingAfterArrayLiteral_test.go

// semicolonFormattingAfterArrayLiteral_test.go
static void TestSemicolonFormattingAfterArrayLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([1,2]/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS([1, 2];)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSemicolonFormattingAfterArrayLiteral, TestSemicolonFormattingAfterArrayLiteral);

// semicolonFormattingInsideAComment_test.go

// semicolonFormattingInsideAComment_test.go
static void TestSemicolonFormattingInsideAComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(    ///**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(   //;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSemicolonFormattingInsideAComment, TestSemicolonFormattingInsideAComment);

// semicolonFormattingInsideAStringLiteral_test.go

// semicolonFormattingInsideAStringLiteral_test.go
static void TestSemicolonFormattingInsideAStringLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(    var x = "string/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(   var x = "string;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSemicolonFormattingInsideAStringLiteral, TestSemicolonFormattingInsideAStringLiteral);

// semicolonFormattingNestedStatements_test.go

// semicolonFormattingNestedStatements_test.go
static void TestSemicolonFormattingNestedStatements(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true)
if (true)/*parentOutsideBlock*/
if (true) {
if (true)/*directParent*/
var x = 0/*innermost*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "innermost");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(        var x = 0;)TS");
		f->GoToMarker(t, "directParent");
		f->VerifyCurrentLineContent(t, R"TS(    if (true))TS");
		f->GoToMarker(t, "parentOutsideBlock");
		f->VerifyCurrentLineContent(t, R"TS(if (true))TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSemicolonFormattingNestedStatements, TestSemicolonFormattingNestedStatements);

// semicolonFormatting_test.go

// semicolonFormatting_test.go
static void TestSemicolonFormatting(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**/function of1 (b:{r:{c:number)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToEOF(t);
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(function of1(b: { r: { c: number;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSemicolonFormatting, TestSemicolonFormatting);

// singleLineTypeLiteralFormatting_test.go

// singleLineTypeLiteralFormatting_test.go
static void TestSingleLineTypeLiteralFormatting(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function of1(b: { r: { c: number/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(function of1(b: { r: { c: number;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSingleLineTypeLiteralFormatting, TestSingleLineTypeLiteralFormatting);

// spaceAfterConstructor_test.go

// spaceAfterConstructor_test.go
static void TestSpaceAfterConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export class myController {
    private _processId;
    constructor (processId: number) {/*1*/
        this._processId = processId;
    }/*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "2");
		f->Insert(t, "}");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    constructor(processId: number) {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSpaceAfterConstructor, TestSpaceAfterConstructor);

// spaceAfterReturn_test.go

// spaceAfterReturn_test.go
static void TestSpaceAfterReturn(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f( ) {
return       1;/*1*/
return[1];/*2*/
return    ;/*3*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    return 1;)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    return [1];)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(    return;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSpaceAfterReturn, TestSpaceAfterReturn);

// spaceAfterStatementConditions_test.go

// spaceAfterStatementConditions_test.go
static void TestSpaceAfterStatementConditions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let i = 0;

if(i<0) ++i;
if(i<0) --i;

while(i<0) ++i;
while(i<0) --i;

do ++i;
while(i<0)
do --i;
while(i<0)

for(let prop in { foo: 1 }) ++i;
for(let prop in { foo: 1 }) --i;

for(let foo of [1, 2]) ++i;
for(let foo of [1, 2]) --i;

for(let j = 0; j < 10; j++) ++i;
for(let j = 0; j < 10; j++) --i;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(let i = 0;

if (i < 0) ++i;
if (i < 0) --i;

while (i < 0) ++i;
while (i < 0) --i;

do ++i;
while (i < 0)
do --i;
while (i < 0)

for (let prop in { foo: 1 }) ++i;
for (let prop in { foo: 1 }) --i;

for (let foo of [1, 2]) ++i;
for (let foo of [1, 2]) --i;

for (let j = 0; j < 10; j++) ++i;
for (let j = 0; j < 10; j++) --i;
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSpaceAfterStatementConditions, TestSpaceAfterStatementConditions);

// spaceBeforeAndAfterBinaryOperators_test.go

// spaceBeforeAndAfterBinaryOperators_test.go
static void TestSpaceBeforeAndAfterBinaryOperators(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let i = 0;
/*1*/(i++,i++);
/*2*/(i++,++i);
/*3*/(1,2);
/*4*/(i++,2);
/*5*/(i++,i++,++i,i--,2);
let s = 'foo';
/*6*/for (var i = 0,ii = 2; i < s.length; ii++,i++) {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS((i++, i++);)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS((i++, ++i);)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS((1, 2);)TS");
		f->GoToMarker(t, "4");
		f->VerifyCurrentLineContent(t, R"TS((i++, 2);)TS");
		f->GoToMarker(t, "5");
		f->VerifyCurrentLineContent(t, R"TS((i++, i++, ++i, i--, 2);)TS");
		f->GoToMarker(t, "6");
		f->VerifyCurrentLineContent(t, R"TS(for (var i = 0, ii = 2; i < s.length; ii++, i++) {)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSpaceBeforeAndAfterBinaryOperators, TestSpaceBeforeAndAfterBinaryOperators);

// tabbingAfterNewlineInsertedBeforeWhile_test.go

// tabbingAfterNewlineInsertedBeforeWhile_test.go
static void TestTabbingAfterNewlineInsertedBeforeWhile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo() {
    /**/while (true) { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->InsertLine(t, "");
		f->VerifyCurrentLineContent(t, R"TS(    while (true) { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestTabbingAfterNewlineInsertedBeforeWhile, TestTabbingAfterNewlineInsertedBeforeWhile);

// typeAssertionsFormatting_test.go

// typeAssertionsFormatting_test.go
static void TestTypeAssertionsFormatting(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(( <  any   >      publisher);/*1*/
 <  any  >      3;/*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS((<any>publisher);)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(<any>3;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestTypeAssertionsFormatting, TestTypeAssertionsFormatting);

// whiteSpaceBeforeReturnTypeFormatting_test.go

// whiteSpaceBeforeReturnTypeFormatting_test.go
static void TestWhiteSpaceBeforeReturnTypeFormatting(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x: () =>     string/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(var x: () => string;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestWhiteSpaceBeforeReturnTypeFormatting, TestWhiteSpaceBeforeReturnTypeFormatting);

// whiteSpaceTrimming2_test.go

// whiteSpaceTrimming2_test.go
static void TestWhiteSpaceTrimming2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((((((((((std::string(R"TS(let noSubTemplate = )TS") + "`") + std::string(R"TS(/*    /*1*/)TS")) + std::string("`")) + std::string(R"TS(;
let templateHead = )TS")) + std::string("`")) + std::string(R"TS(/*    /*2*/${1 + 2})TS")) + std::string("`")) + std::string(R"TS(;
let templateMiddle = )TS")) + std::string("`")) + std::string(R"TS(/*    ${1 + 2    /*3*/})TS")) + std::string("`")) + std::string(R"TS(;
let templateTail = )TS")) + std::string("`")) + std::string(R"TS(/*    ${1 + 2}    /*4*/)TS")) + std::string("`")) + std::string(R"TS(;)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, R"TS(
)TS");
		f->GoToMarker(t, "2");
		f->Insert(t, R"TS(
)TS");
		f->GoToMarker(t, "3");
		f->Insert(t, R"TS(
)TS");
		f->GoToMarker(t, "4");
		f->Insert(t, R"TS(
)TS");
		f->VerifyCurrentFileContent(t, R"TS(let noSubTemplate = `/*    
`;
let templateHead = `/*    
${1 + 2}`;
let templateMiddle = `/*    ${1 + 2
    }`;
let templateTail = `/*    ${1 + 2}    
`;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestWhiteSpaceTrimming2, TestWhiteSpaceTrimming2);

// whiteSpaceTrimming3_test.go

// whiteSpaceTrimming3_test.go
static void TestWhiteSpaceTrimming3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let t = "foo \
bar     \   
"/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ";");
		f->VerifyCurrentFileContent(t, R"TS(let t = "foo \
bar     \   
";)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestWhiteSpaceTrimming3, TestWhiteSpaceTrimming3);

// whiteSpaceTrimming4_test.go

// whiteSpaceTrimming4_test.go
static void TestWhiteSpaceTrimming4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var re = /\w+   /*1*//;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, R"TS(
)TS");
		f->VerifyCurrentFileContent(t, R"TS(var re = /\w+
    /;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestWhiteSpaceTrimming4, TestWhiteSpaceTrimming4);

// whiteSpaceTrimming_test.go

// whiteSpaceTrimming_test.go
static void TestWhiteSpaceTrimming(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) {     
  //    
   /*err*/})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "err");
		f->Insert(t, R"TS(
)TS");
		f->VerifyCurrentFileContent(t, R"TS(if (true) {     
  //    

})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestWhiteSpaceTrimming, TestWhiteSpaceTrimming);

} // namespace
