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


static void TestFoldingRangeLineFoldingOnly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (EMPTY_TAGs.has(tag)) {
  output += "/>";
} else {
  output += ">";

  if (!html && kidcount > 0) {
    //
  }
}

export function use<T>(ctx: any): T | undefined {
  //
})TS";
		auto ptrTrue = true;
		auto capabilities = std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.TextDocument = std::make_shared<lsproto::TextDocumentClientCapabilities>(lsproto::TextDocumentClientCapabilities{.FoldingRange = std::make_shared<lsproto::FoldingRangeClientCapabilities>(lsproto::FoldingRangeClientCapabilities{.LineFoldingOnly = ptrTrue, .FoldingRange = std::make_shared<lsproto::ClientFoldingRangeOptions>(lsproto::ClientFoldingRangeOptions{.CollapsedText = ptrTrue})})})});
		auto __fsp1 = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyFoldingRangeLines(t, {fourslash::FoldingRangeLineExpected{.StartLine = 0, .EndLine = 1}, fourslash::FoldingRangeLineExpected{.StartLine = 2, .EndLine = 7}, fourslash::FoldingRangeLineExpected{.StartLine = 5, .EndLine = 6}, fourslash::FoldingRangeLineExpected{.StartLine = 10, .EndLine = 11}});
	});
}
REGISTER_FOURSLASH_TEST(TestFoldingRangeLineFoldingOnly, TestFoldingRangeLineFoldingOnly);

static void TestFoldingRangeLineFoldingOnlyWithRegions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// #region MyRegion
const x = 1;
function foo() {
  return x;
}
// #endregion

// #region Outer
const y = 2;
// #region Inner
const z = 3;
// #endregion
// #endregion)TS";
		auto ptrTrue = true;
		auto capabilities = std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.TextDocument = std::make_shared<lsproto::TextDocumentClientCapabilities>(lsproto::TextDocumentClientCapabilities{.FoldingRange = std::make_shared<lsproto::FoldingRangeClientCapabilities>(lsproto::FoldingRangeClientCapabilities{.LineFoldingOnly = ptrTrue, .FoldingRange = std::make_shared<lsproto::ClientFoldingRangeOptions>(lsproto::ClientFoldingRangeOptions{.CollapsedText = ptrTrue})})})});
		auto __fsp1 = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyFoldingRangeLines(t, {fourslash::FoldingRangeLineExpected{.StartLine = 0, .EndLine = 5}, fourslash::FoldingRangeLineExpected{.StartLine = 2, .EndLine = 3}, fourslash::FoldingRangeLineExpected{.StartLine = 7, .EndLine = 12}, fourslash::FoldingRangeLineExpected{.StartLine = 9, .EndLine = 11}});
	});
}
REGISTER_FOURSLASH_TEST(TestFoldingRangeLineFoldingOnlyWithRegions, TestFoldingRangeLineFoldingOnlyWithRegions);


static void TestFoldingRangeJSXPropertyAccess(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: preserve
// @Filename: /a.tsx
const Components =[| {
  Nested: () => null
}|];

export const Test = () =>[| {
  return [|<Components.Nested></Components.Nested>|];
}|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyOutliningSpans(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestFoldingRangeJSXPropertyAccess, TestFoldingRangeJSXPropertyAccess);
} // namespace
