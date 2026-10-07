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


static void TestFixingTypeParametersQuickInfo(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
declare function f<T>(x: T, y: (p: T) => T, z: (p: T) => T): T;
var /*1*/result = /*2*/f(0, /*3*/x => null, /*4*/x => x.blahblah);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var result: number", "");
		f->VerifyQuickInfoAt(t, "2", "function f<number>(x: number, y: (p: number) => number, z: (p: number) => number): number", "");
		f->VerifyQuickInfoAt(t, "3", "(parameter) x: number", "");
		f->VerifyQuickInfoAt(t, "4", "(parameter) x: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestFixingTypeParametersQuickInfo, TestFixingTypeParametersQuickInfo);


static void TestFixExactOptionalUnassignableProperties9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strictNullChecks: true
// @exactOptionalPropertyTypes: true
interface IAny {
    a?: any
}
interface J {
    a?: number | undefined
}
declare var iany: IAny
declare var j: J
iany/**/ = j)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCodeFixNotAvailable(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestFixExactOptionalUnassignableProperties9, TestFixExactOptionalUnassignableProperties9);


static void TestFixExactOptionalUnassignableProperties7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strictNullChecks: true
// @exactOptionalPropertyTypes: true
// @Filename: fixExactOptionalUnassignableProperties6.ts
class Feh {
    _requestFinished(error?: string) {
        this._finishedPromiseCallback({ error/**/ });
    }
    private _finishedPromiseCallback: (arg: { error?: string }) => void = () => {};
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCodeFixNotAvailable(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestFixExactOptionalUnassignableProperties7, TestFixExactOptionalUnassignableProperties7);


static void TestFixExactOptionalUnassignableProperties3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strictNullChecks: true
// @exactOptionalPropertyTypes: true
// @Filename: fixExactOptionalUnassignableProperties2.ts
import { INodeModules } from 'foo'
interface J {
    a?: number | undefined
}
declare var inm: INodeModules
declare var j: J
inm/**/ = j
console.log(inm)
// @Filename: node_modules/@types/foo/index.d.ts
export interface INodeModules {
    a?: number
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCodeFixNotAvailable(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestFixExactOptionalUnassignableProperties3, TestFixExactOptionalUnassignableProperties3);
} // namespace
