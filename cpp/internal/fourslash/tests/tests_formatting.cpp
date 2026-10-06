// Ported fourslash tests — batch A. One static void TestX(gostd::testing::T*)
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
#include "internal/testutil/testutil.h"

namespace {
using namespace tsc;
namespace tsu = tsc::fourslash::tests::util;

// formattingHexLiteral_test.go
static void TestFormattingHexLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(var x =  0x1,y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingHexLiteral, TestFormattingHexLiteral);

// formattingConditionalOperator_test.go
static void TestFormattingConditionalOperator(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(var x=true?1:2)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToBOF(t);
		f->VerifyCurrentLineContent(t, R"TS(var x = true ? 1 : 2)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingConditionalOperator, TestFormattingConditionalOperator);

// formattingDoubleLessThan_test.go
static void TestFormattingDoubleLessThan(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/if (<number>foo < <number>bar) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(if (<number>foo < <number>bar) { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingDoubleLessThan, TestFormattingDoubleLessThan);

// formattingEqualsBeforeBracketInTypeAlias_test.go
static void TestFormattingEqualsBeforeBracketInTypeAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type X    =     [number]/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(type X = [number];)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingEqualsBeforeBracketInTypeAlias, TestFormattingEqualsBeforeBracketInTypeAlias);

// formattingForIn_test.go
static void TestFormattingForIn(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**/for (var i    in[]   )  {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(for (var i in []) { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingForIn, TestFormattingForIn);

// formattingForOfKeyword_test.go
static void TestFormattingForOfKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**/for ([]of[]) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "");
		f->VerifyCurrentLineContent(t, R"TS(for ([] of []) { })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingForOfKeyword, TestFormattingForOfKeyword);

// formattingKeywordAsIdentifier_test.go
static void TestFormattingKeywordAsIdentifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(declare var module/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ";");
		f->VerifyCurrentLineContent(t, R"TS(declare var module;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingKeywordAsIdentifier, TestFormattingKeywordAsIdentifier);

// formattingOnSemiColon_test.go
static void TestFormattingOnSemiColon(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(var  a=b+c^d-e*++f)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToEOF(t);
		f->Insert(t, ";");
		f->VerifyCurrentFileContent(t, R"TS(var a = b + c ^ d - e * ++f;)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestFormattingOnSemiColon, TestFormattingOnSemiColon);

}  // namespace
