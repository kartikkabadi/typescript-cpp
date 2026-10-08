// reverseMappedDeclarationQuickInfo_test.go
#include "internal/fourslash/fourslash.h"
#include "internal/fourslash/goutil.h"
#include "internal/fourslash/test_parser.h"
#include "internal/fourslash/tests/registry.h"
#include "internal/fourslash/tests/util/util.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"

namespace {
using namespace tsc;

// reverseMappedDeclarationQuickInfo_test.go
static void TestReverseMappedDeclarationQuickInfo(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(
declare function unwrap<T>(input: { [K in keyof T]: { value: T[K] } }): T;
declare const input: { [key: string]: { value: string } };
const /*index*/indexed = unwrap(input);
indexed["name"].charAt/*member*/(0);

type Validator<T> = ((input: unknown) => T | undefined) | {
    [K in keyof T]: Validator<T[K]>;
};
declare function decode<T>(input: { [K in keyof T]: Validator<T[K]> }): T;
declare const stringValidator: (input: unknown) => string | undefined;
const /*deep*/deep = decode({ a: { b: { c: { d: stringValidator } } } });
deep.a.b.c./*leaf*/d;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestReverseMappedDeclarationQuickInfo, TestReverseMappedDeclarationQuickInfo);

} // namespace
