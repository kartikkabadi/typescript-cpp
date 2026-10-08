// Ported fourslash tests -- batch B (linkediting). One static void TestX(gostd::testing::T*)
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

// linkedEditingJsxTag10_test.go
static void TestLinkedEditingJsxTag10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /jsx0.tsx
const jsx = </*0*/>
// @Filename: /jsx1.tsx
const jsx = <//*1*/>
// @Filename: /jsx2.tsx
const jsx = </*2*/div>
// @Filename: /jsx3.tsx
const jsx = <//*3*/div>
// @Filename: /jsx4.tsx
const jsx = </*4*/div> <//*4a*/>;
// @Filename: /jsx5.tsx
const jsx = </*5*/> <//*5a*/div>;
// @Filename: /jsx6.tsx
const jsx = /*6*/div> <//*6a*/div>;
// @Filename: /jsx7.tsx
const jsx = </*7*/div> //*7a*/div>;
// @Filename: /jsx8.tsx
const jsx = </*8*/div <//*8a*/div>;
// @Filename: /jsx9.tsx
const jsx = </*9*/div> <//*9a*/div;
// @Filename: /jsx10.tsx
const jsx = </*10*/> <//*10a*/;
// @Filename: /jsx11.tsx
const jsx = </*11*/ <//*11a*/>;
// @Filename: /jsx12.tsx
const jsx = /*12*/> <//*12a*/>;
// @Filename: /jsx13.tsx
const jsx = </*13*/> //*13a*/>;
// @Filename: /jsx14.tsx
const jsx = </*14*/> </*14a*/div> <//*14b*/> <//*14c*/div>;
// @Filename: /jsx15.tsx
const jsx = </*15*/div> </*15a*/> <//*15b*/div> <//*15c*/>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineLinkedEditing(t);
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag10, TestLinkedEditingJsxTag10);

// linkedEditingJsxTag11_test.go
static void TestLinkedEditingJsxTag11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /customElements.tsx
const jsx = <fbt:enum knownProp="accepted"
    unknownProp="rejected">
</fbt:enum>;

const customElement = <custom-element></custom-element>;

const standardElement = 
   <Link href="/hello" passHref>
       <Button component="a">
           Next
       </Button>
   </Link>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineLinkedEditing(t);
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag11, TestLinkedEditingJsxTag11);

// linkedEditingJsxTag12_test.go
static void TestLinkedEditingJsxTag12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /incomplete.tsx
function Test() {
    return <div>
        </*0*/
        <div {...{}}>
        </div>
    </div>
}
// @Filename: /incompleteMismatched.tsx
function Test() {
    return <div>
        <T
        <div {...{}}>
        </div>
    </div>
}
// @Filename: /incompleteMismatched2.tsx
function Test() {
    return <div>
        <T
        <div {...{}}>
        T</div>
    </div>
}
// @Filename: /incompleteMismatched3.tsx
function Test() {
    return <div>
        <div {...{}}>
        </div>
        <T
    </div>
}
// @Filename: /mismatched.tsx
function Test() {
    return <div>
        <T>
        <div {...{}}>
        </div>
    </div>
}
// @Filename: /matched.tsx
function Test() {
    return <div>

        <div {...{}}>
        </div>
    </div>
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyLinkedEditing(t, std::unordered_map<std::string, std::vector<lsproto::Range>>{{"0", std::vector<lsproto::Range>{}}});
		f->VerifyBaselineLinkedEditing(t);
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag12, TestLinkedEditingJsxTag12);

// linkedEditingJsxTag1_test.go
static void TestLinkedEditingJsxTag1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /basic.tsx
/*a*/const j/*b*/sx = (
    /*c*/</*0*/d/*1*/iv/*2*/>/*3*/
    </*4*///*5*/di/*6*/v/*7*/>/*8*/
);
const jsx2 = (
    </*9start*/d/*9*/iv/*9end*/>
        </*10start*/d/*10*/iv/*10end*/>
            </*11start*/p/*11*/>
            <//*12*/p/*12end*/>        
        <//*13start*/d/*13*/iv/*13end*/>
    <//*14start*/d/*14*/iv/*14end*/>
);/*d*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto linkedCursors1 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "0")->LSPosition, .End = f->MarkerByName(t, "2")->LSPosition}, {.Start = f->MarkerByName(t, "5")->LSPosition, .End = f->MarkerByName(t, "7")->LSPosition}};
		auto linkedCursors2 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "9start")->LSPosition, .End = f->MarkerByName(t, "9end")->LSPosition},
		{.Start = f->MarkerByName(t, "14start")->LSPosition, .End = f->MarkerByName(t, "14end")->LSPosition}};
		auto linkedCursors3 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "10start")->LSPosition, .End = f->MarkerByName(t, "10end")->LSPosition},
		{.Start = f->MarkerByName(t, "13start")->LSPosition, .End = f->MarkerByName(t, "13end")->LSPosition}};
		auto linkedCursors4 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "11start")->LSPosition, .End = f->MarkerByName(t, "11")->LSPosition},
		{.Start = f->MarkerByName(t, "12")->LSPosition, .End = f->MarkerByName(t, "12end")->LSPosition}};
		f->VerifyLinkedEditing(t, std::unordered_map<std::string, std::vector<lsproto::Range>>{{
		"0",  linkedCursors1},{
		"1",  linkedCursors1},{
		"2",  linkedCursors1},{
		"3",  std::vector<lsproto::Range>{}},{
		"4",  std::vector<lsproto::Range>{}},{
		"5",  linkedCursors1},{
		"6",  linkedCursors1},{
		"7",  linkedCursors1},{
		"8",  std::vector<lsproto::Range>{}},{
		"9",  linkedCursors2},{
		"10", linkedCursors3},{
		"11", linkedCursors4},{
		"12", linkedCursors4},{
		"13", linkedCursors3},{
		"14", linkedCursors2},{
		"a",  std::vector<lsproto::Range>{}},{
		"b",  std::vector<lsproto::Range>{}},{
		"c",  std::vector<lsproto::Range>{}},{
		"d",  std::vector<lsproto::Range>{}}});
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag1, TestLinkedEditingJsxTag1);

// linkedEditingJsxTag2_test.go
static void TestLinkedEditingJsxTag2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /attrs.tsx
const jsx = (
   </*0*/div/*1*/ /*2*/styl/*3*/e={{ color: 'red' }}/*4*/>/*5*/
      <p>
         <img />
      </p>
   <//*6start*/di/*6*/v/*6end*/>
);
// @Filename: /attrsError.tsx
const jsx = (
   </*10*/div/*11*/ /*12*/styl/*13*/e={{ color: 'red' }/*14*/>/*15*/
         </*16*/p />
   <//*17*/div>
);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		// Test file content (for readability):;
		// const jsx = (;
		//    <div style={{ color: 'red' }}>;
		//       <p>;
		//          <img />;
		//       </p>;
		//    </div>;
		// );
		auto linkedCursors = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "0")->LSPosition, .End = f->MarkerByName(t, "1")->LSPosition},
		{.Start = f->MarkerByName(t, "6start")->LSPosition, .End = f->MarkerByName(t, "6end")->LSPosition}};
		f->VerifyLinkedEditing(t, std::unordered_map<std::string, std::vector<lsproto::Range>>{{
		"0",  linkedCursors},{
		"1",  linkedCursors},{
		"2",  std::vector<lsproto::Range>{}},{
		"3",  std::vector<lsproto::Range>{}},{
		"4",  std::vector<lsproto::Range>{}},{
		"5",  std::vector<lsproto::Range>{}},{
		"6",  linkedCursors},{
		"10", std::vector<lsproto::Range>{}},{
		"11", std::vector<lsproto::Range>{}},{
		"12", std::vector<lsproto::Range>{}},{
		"13", std::vector<lsproto::Range>{}},{
		"14", std::vector<lsproto::Range>{}},{
		"15", std::vector<lsproto::Range>{}},{
		"16", std::vector<lsproto::Range>{}}});
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag2, TestLinkedEditingJsxTag2);

// linkedEditingJsxTag3_test.go
static void TestLinkedEditingJsxTag3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /selfClosing.tsx
/*0*/const jsx = /*1*/(
   <div> /*2*/
      <p>/*3*/
         No lin/*4*/ked cursors here!
         /*5*/</*6*/img/*7*/ /*8*///*9*/>
     /*10*/ </p>/*11*/
   /*12*/</div>
/*13*/)/*14*/;/*15*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyLinkedEditing(t, std::unordered_map<std::string, std::vector<lsproto::Range>>{{
		"0",  std::vector<lsproto::Range>{}},{
		"1",  std::vector<lsproto::Range>{}},{
		"2",  std::vector<lsproto::Range>{}},{
		"3",  std::vector<lsproto::Range>{}},{
		"4",  std::vector<lsproto::Range>{}},{
		"5",  std::vector<lsproto::Range>{}},{
		"6",  std::vector<lsproto::Range>{}},{
		"7",  std::vector<lsproto::Range>{}},{
		"8",  std::vector<lsproto::Range>{}},{
		"9",  std::vector<lsproto::Range>{}},{
		"10", std::vector<lsproto::Range>{}},{
		"11", std::vector<lsproto::Range>{}},{
		"12", std::vector<lsproto::Range>{}},{
		"13", std::vector<lsproto::Range>{}},{
		"14", std::vector<lsproto::Range>{}},{
		"15", std::vector<lsproto::Range>{}}});
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag3, TestLinkedEditingJsxTag3);

// linkedEditingJsxTag4_test.go
static void TestLinkedEditingJsxTag4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /typeTag.tsx
const jsx = (
   </*0*/div/*1*/</*2*/T/*3*/>/*4*/>/*5*/
      <p>
         <img />
      </p>
   <//*6*/div/*7*/>
);
// @Filename: /typeTagError.tsx
const jsx = (
   </*10*/div/*11*/</*12*/T/*13*/>/*14*/
      </*15*/p />
   <//*16*/div>
);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto linkedCursors = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "0")->LSPosition, .End = f->MarkerByName(t, "1")->LSPosition},
		{.Start = f->MarkerByName(t, "6")->LSPosition, .End = f->MarkerByName(t, "7")->LSPosition}};
		f->VerifyLinkedEditing(t, std::unordered_map<std::string, std::vector<lsproto::Range>>{{
		"0",  linkedCursors},{
		"1",  linkedCursors},{
		"2",  std::vector<lsproto::Range>{}},{
		"3",  std::vector<lsproto::Range>{}},{
		"4",  std::vector<lsproto::Range>{}},{
		"5",  std::vector<lsproto::Range>{}},{
		"6",  linkedCursors},{
		"10", std::vector<lsproto::Range>{}},{
		"11", std::vector<lsproto::Range>{}},{
		"12", std::vector<lsproto::Range>{}},{
		"13", std::vector<lsproto::Range>{}},{
		"14", std::vector<lsproto::Range>{}},{
		"15", std::vector<lsproto::Range>{}},{
		"16", std::vector<lsproto::Range>{}}});
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag4, TestLinkedEditingJsxTag4);

// linkedEditingJsxTag5_test.go
static void TestLinkedEditingJsxTag5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @FileName: /unclosedElement.tsx
const jsx = (
    <div/*0*/>
        </*1start*/div/*1*/>
    <//*2start*/div/*2*/>/*3*/
);/*4*/
// @FileName: /mismatchedElement.tsx
const jsx = (
    /*5*/</*6start*/div/*6*/>
        <//*7start*/div/*7*/>
    </*8*//div/*9*/>/*10*/
);
// @Filename: /invalidClosing.tsx
const jsx = (
   <di/*11*/v>
   </*12*/ //*13*/div>
);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto linkedCursors1 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "1start")->LSPosition, .End = f->MarkerByName(t, "1")->LSPosition},
		{.Start = f->MarkerByName(t, "2start")->LSPosition, .End = f->MarkerByName(t, "2")->LSPosition}};
		auto linkedCursors2 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "6start")->LSPosition, .End = f->MarkerByName(t, "6")->LSPosition},
		{.Start = f->MarkerByName(t, "7start")->LSPosition, .End = f->MarkerByName(t, "7")->LSPosition}};
		f->VerifyLinkedEditing(t, std::unordered_map<std::string, std::vector<lsproto::Range>>{{
		"0",  std::vector<lsproto::Range>{}},{
		"1",  linkedCursors1},{
		"2",  linkedCursors1},{
		"3",  std::vector<lsproto::Range>{}},{
		"4",  std::vector<lsproto::Range>{}},{
		"5",  std::vector<lsproto::Range>{}},{
		"6",  linkedCursors2},{
		"7",  linkedCursors2},{
		"8",  std::vector<lsproto::Range>{}},{
		"9",  std::vector<lsproto::Range>{}},{
		"10", std::vector<lsproto::Range>{}},{
		"11", std::vector<lsproto::Range>{}},{ // this tag does not parse as a closing tag
		"12", std::vector<lsproto::Range>{}},{
		"13", std::vector<lsproto::Range>{}}});
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag5, TestLinkedEditingJsxTag5);

// linkedEditingJsxTag6_test.go
static void TestLinkedEditingJsxTag6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /namespace.tsx
const jsx = (
    </*start*/someNamespa/*3*/ce./*2*/Thing/*startend*/>
    <//*end*/someNamespace/*1*/.Thing/*endend*/>
);
 const jsx1 = </*4*/foo/*5*/  /*6*/./*7*/ /*8*/ba/*9*/r><//*10*/foo.bar>;
 const jsx2 = <foo./*11*/bar><//*12*/ /*13*/f/*14*/oo /*15*/./*16*/b/*17*/ar/*18*/>;
 const jsx3 = </*19*/foo/*20*/ //*21*// /*22*/some comment
     /*23*/./*24*/bar>
     </f/*25*/oo.bar>;
 let jsx4 =
     </*26*/foo  /*27*/ .// hi/*28*/
     /*29*/bar/*26end*/>
     <//*30*/foo  /*31*/ .// hi/*32*/
     /*33*/bar/*30end*/>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto linkedCursors1 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "start")->LSPosition, .End = f->MarkerByName(t, "startend")->LSPosition},
		{.Start = f->MarkerByName(t, "end")->LSPosition, .End = f->MarkerByName(t, "endend")->LSPosition}};
		auto linkedCursors2 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "26")->LSPosition, .End = f->MarkerByName(t, "26end")->LSPosition},
		{.Start = f->MarkerByName(t, "30")->LSPosition, .End = f->MarkerByName(t, "30end")->LSPosition}};
		f->VerifyLinkedEditing(t, std::unordered_map<std::string, std::vector<lsproto::Range>>{{
		"1",  linkedCursors1},{
		"2",  linkedCursors1},{
		"3",  linkedCursors1},{
		"4",  std::vector<lsproto::Range>{}},{
		"5",  std::vector<lsproto::Range>{}},{
		"6",  std::vector<lsproto::Range>{}},{
		"7",  std::vector<lsproto::Range>{}},{
		"8",  std::vector<lsproto::Range>{}},{
		"9",  std::vector<lsproto::Range>{}},{
		"10", std::vector<lsproto::Range>{}},{
		"11", std::vector<lsproto::Range>{}},{
		"12", std::vector<lsproto::Range>{}},{
		"13", std::vector<lsproto::Range>{}},{
		"14", std::vector<lsproto::Range>{}},{
		"15", std::vector<lsproto::Range>{}},{
		"16", std::vector<lsproto::Range>{}},{
		"17", std::vector<lsproto::Range>{}},{
		"18", std::vector<lsproto::Range>{}},{
		"19", std::vector<lsproto::Range>{}},{
		"20", std::vector<lsproto::Range>{}},{
		"21", std::vector<lsproto::Range>{}},{
		"22", std::vector<lsproto::Range>{}},{
		"23", std::vector<lsproto::Range>{}},{
		"24", std::vector<lsproto::Range>{}},{
		"25", std::vector<lsproto::Range>{}},{
		"26", linkedCursors2},{
		"27", linkedCursors2},{
		"28", linkedCursors2},{
		"29", linkedCursors2},{
		"30", linkedCursors2},{
		"31", linkedCursors2},{
		"32", linkedCursors2},{
		"33", linkedCursors2}});
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag6, TestLinkedEditingJsxTag6);

// linkedEditingJsxTag7_test.go
static void TestLinkedEditingJsxTag7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @FileName: /fragment.tsx
/*a*/const j/*b*/sx =/*c*/ (
    /*5*/</*0*/>/*1*/
        <img />
    /*6*/</*2*///*3*/>/*4*/
)/*d*/;
const jsx2 = (
    /* this is comment *//*13*/</*10*//* /*11*/more comment *//*12*/>/*8*/Hello/*9*/
    <//*14*/ /*18*///*17*/* even/*15*/ more comment *//*16*/>
);
const jsx3 = (
    <>/*7*/
    </>
);/*e*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto startRange = f->MarkerByName(t, "0")->LSPosition;
		auto endRange = f->MarkerByName(t, "3")->LSPosition;
		auto linkedCursors1 = std::vector<lsproto::Range>{
		{.Start = startRange, .End = startRange},
		{.Start = endRange, .End = endRange}};
		auto startRange2 = f->MarkerByName(t, "10")->LSPosition;
		auto endRange2 = f->MarkerByName(t, "14")->LSPosition;
		auto linkedCursors2 = std::vector<lsproto::Range>{
		{.Start = startRange2, .End = startRange2},
		{.Start = endRange2, .End = endRange2}};
		f->VerifyLinkedEditing(t, std::unordered_map<std::string, std::vector<lsproto::Range>>{{
		"0",  linkedCursors1},{
		"1",  std::vector<lsproto::Range>{}},{
		"2",  std::vector<lsproto::Range>{}},{
		"3",  linkedCursors1},{
		"4",  std::vector<lsproto::Range>{}},{
		"5",  std::vector<lsproto::Range>{}},{
		"6",  std::vector<lsproto::Range>{}},{
		"7",  std::vector<lsproto::Range>{}},{
		"8",  std::vector<lsproto::Range>{}},{
		"9",  std::vector<lsproto::Range>{}},{
		"10", linkedCursors2},{
		"11", std::vector<lsproto::Range>{}},{
		"12", std::vector<lsproto::Range>{}},{
		"13", std::vector<lsproto::Range>{}},{
		"14", linkedCursors2},{
		"15", std::vector<lsproto::Range>{}},{
		"16", std::vector<lsproto::Range>{}},{
		"17", std::vector<lsproto::Range>{}},{
		"18", std::vector<lsproto::Range>{}},{
		"a",  std::vector<lsproto::Range>{}},{
		"b",  std::vector<lsproto::Range>{}},{
		"c",  std::vector<lsproto::Range>{}},{
		"d",  std::vector<lsproto::Range>{}},{
		"e",  std::vector<lsproto::Range>{}}});
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag7, TestLinkedEditingJsxTag7);

// linkedEditingJsxTag8_test.go
static void TestLinkedEditingJsxTag8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @FileName: /mismatchedNames.tsx
const A = thing;
const B = thing;
const jsx = (
    </*8*/A>
    </B>
);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyLinkedEditing(t, std::unordered_map<std::string, std::vector<lsproto::Range>>{{
		"8", std::vector<lsproto::Range>{}}});
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag8, TestLinkedEditingJsxTag8);

// linkedEditingJsxTag9_test.go
static void TestLinkedEditingJsxTag9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /whitespace.tsx
const whitespaceOpening = (
   </*0*/ /*1*/div/*2*/ /*3*/> /*4*/
   <//*5*/di/*6*/v/*5end*/>
);
const whitespaceClosing = (
   </*7*/di/*8*/v/*8end*/>
   <//*9*/ /*10*/div/*11*/ /*12*/> /*13*/
);
const triviaOpening = (
    /* this is/*14*/ comment *//*15*/</*16*//* /*17*/more/*18*/ comment *//*19*/ /*20start*/di/*20*/v/*20end*/ /* comments */>/*21*/Hello/*22*/
    <//*23*/ /*24*///*25*/* even/*26*/ more comment *//*27*/ /*28start*/d/*28*/iv/*28end*/ /* b/*29*/ye */>
);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto linkedCursors1 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "1")->LSPosition, .End = f->MarkerByName(t, "2")->LSPosition},
		{.Start = f->MarkerByName(t, "5")->LSPosition, .End = f->MarkerByName(t, "5end")->LSPosition}};
		auto linkedCursors2 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "7")->LSPosition, .End = f->MarkerByName(t, "8end")->LSPosition},
		{.Start = f->MarkerByName(t, "10")->LSPosition, .End = f->MarkerByName(t, "11")->LSPosition}};
		auto linkedCursors3 = std::vector<lsproto::Range>{
		{.Start = f->MarkerByName(t, "20start")->LSPosition, .End = f->MarkerByName(t, "20end")->LSPosition},
		{.Start = f->MarkerByName(t, "28start")->LSPosition, .End = f->MarkerByName(t, "28end")->LSPosition}};
		f->VerifyLinkedEditing(t, std::unordered_map<std::string, std::vector<lsproto::Range>>{{
		"0",  std::vector<lsproto::Range>{}},{
		"1",  linkedCursors1},{
		"2",  linkedCursors1},{
		"3",  std::vector<lsproto::Range>{}},{
		"4",  std::vector<lsproto::Range>{}},{
		"5",  linkedCursors1},{
		"6",  linkedCursors1},{
		"7",  linkedCursors2},{
		"8",  linkedCursors2},{
		"9",  std::vector<lsproto::Range>{}},{
		"10", linkedCursors2},{
		"11", linkedCursors2},{
		"12", std::vector<lsproto::Range>{}},{
		"13", std::vector<lsproto::Range>{}},{
		"14", std::vector<lsproto::Range>{}},{
		"15", std::vector<lsproto::Range>{}},{
		"16", std::vector<lsproto::Range>{}},{
		"17", std::vector<lsproto::Range>{}},{
		"18", std::vector<lsproto::Range>{}},{
		"19", std::vector<lsproto::Range>{}},{
		"20", linkedCursors3},{
		"21", std::vector<lsproto::Range>{}},{
		"22", std::vector<lsproto::Range>{}},{
		"23", std::vector<lsproto::Range>{}},{
		"24", std::vector<lsproto::Range>{}},{
		"25", std::vector<lsproto::Range>{}},{
		"26", std::vector<lsproto::Range>{}},{
		"27", std::vector<lsproto::Range>{}},{
		"28", linkedCursors3},{
		"29", std::vector<lsproto::Range>{}}});
	});
}
REGISTER_FOURSLASH_TEST(TestLinkedEditingJsxTag9, TestLinkedEditingJsxTag9);


}  // namespace
