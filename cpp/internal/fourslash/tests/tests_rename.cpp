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

// renameAlias_test.go
static void TestRenameAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(namespace SomeModule { export class SomeClass { } }
[|import [|{| "contextRangeIndex": 0 |}M|] = SomeModule;|]
import C = [|M|].SomeClass;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"M"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameAlias, TestRenameAlias);

// renameAlias2_test.go
static void TestRenameAlias2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS([|module [|{| "contextRangeIndex": 0 |}SomeModule|] { export class SomeClass { } }|]
import M = [|SomeModule|];
import C = M.SomeClass;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"SomeModule"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameAlias2, TestRenameAlias2);

// renameAlias3_test.go
static void TestRenameAlias3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(namespace SomeModule { [|export class [|{| "contextRangeIndex": 0 |}SomeClass|] { }|] }
import M = SomeModule;
import C = M.[|SomeClass|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"SomeClass"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameAlias3, TestRenameAlias3);

// renameImportAndExport_test.go
static void TestRenameImportAndExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS([|import [|{| "contextRangeIndex": 0 |}a|] from "module";|]
[|export { [|{| "contextRangeIndex": 2 |}a|] };|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportAndExport, TestRenameImportAndExport);

// renameImportAndShorthand_test.go
static void TestRenameImportAndShorthand(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS([|import [|{| "contextRangeIndex": 0 |}foo|] from 'bar';|]
const bar = { [|foo|] };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[2]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportAndShorthand, TestRenameImportAndShorthand);

// renameImportNamespaceAndShorthand_test.go
static void TestRenameImportNamespaceAndShorthand(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS([|import * as [|{| "contextRangeIndex": 0 |}foo|] from 'bar';|]
const bar = { [|foo|] };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[2]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportNamespaceAndShorthand, TestRenameImportNamespaceAndShorthand);

// renameModuleExportsProperties2_test.go
static void TestRenameModuleExportsProperties2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS([|class [|{| "contextRangeIndex": 0 |}A|] {}|]
module.exports = { B: [|A|] })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[2]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameModuleExportsProperties2, TestRenameModuleExportsProperties2);

// renameNumericalIndex_test.go
static void TestRenameNumericalIndex(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const foo = { [|0|]: true };
foo[[|0|]];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"0"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameNumericalIndex, TestRenameNumericalIndex);

}  // namespace
