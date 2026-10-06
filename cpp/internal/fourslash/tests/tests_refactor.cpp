// Ported fourslash tests -- batch B (refactor). One static void TestX(gostd::testing::T*)
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

// refactorConvertToEsModule_module_node12_test.go
static void TestRefactorConvertToEsModule_module_node12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @target: esnext
// @module: node16
// @Filename: /a.js
module.exports = 0;
// @Filename: /b.ts
module.exports = 0;
// @Filename: /c.cjs
module.exports = 0;
// @Filename: /d.cts
module.exports = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.js");
		f->VerifyCodeFixNotAvailable(t, std::vector<std::string>{});
		f->GoToFile(t, "/b.ts");
		f->VerifyCodeFixNotAvailable(t, std::vector<std::string>{});
		f->GoToFile(t, "/c.cjs");
		f->VerifyCodeFixNotAvailable(t, std::vector<std::string>{});
		f->GoToFile(t, "/d.cts");
		f->VerifyCodeFixNotAvailable(t, std::vector<std::string>{});
	});
}
REGISTER_FOURSLASH_TEST(TestRefactorConvertToEsModule_module_node12, TestRefactorConvertToEsModule_module_node12);

// refactorConvertToEsModule_module_nodenext_test.go
static void TestRefactorConvertToEsModule_module_nodenext(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @target: esnext
// @module: node18
// @Filename: /a.js
module.exports = 0;
// @Filename: /b.ts
module.exports = 0;
// @Filename: /c.cjs
module.exports = 0;
// @Filename: /d.cts
module.exports = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.js");
		f->VerifyCodeFixNotAvailable(t, std::vector<std::string>{});
		f->GoToFile(t, "/b.ts");
		f->VerifyCodeFixNotAvailable(t, std::vector<std::string>{});
		f->GoToFile(t, "/c.cjs");
		f->VerifyCodeFixNotAvailable(t, std::vector<std::string>{});
		f->GoToFile(t, "/d.cts");
		f->VerifyCodeFixNotAvailable(t, std::vector<std::string>{});
	});
}
REGISTER_FOURSLASH_TEST(TestRefactorConvertToEsModule_module_nodenext, TestRefactorConvertToEsModule_module_nodenext);

// refactorConvertToEsModule_notAtTopLevel_test.go
static void TestRefactorConvertToEsModule_notAtTopLevel(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @target: esnext
// @Filename: /a.js
(function() {
    module.exports = 0;
})();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, std::vector<std::shared_ptr<lsproto::Diagnostic>>{});
	});
}
REGISTER_FOURSLASH_TEST(TestRefactorConvertToEsModule_notAtTopLevel, TestRefactorConvertToEsModule_notAtTopLevel);

// refactorConvertToEsModule_notInCommonjsProject_test.go
static void TestRefactorConvertToEsModule_notInCommonjsProject(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @target: es5
// @Filename: /a.js
exports.x = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, std::vector<std::shared_ptr<lsproto::Diagnostic>>{});
	});
}
REGISTER_FOURSLASH_TEST(TestRefactorConvertToEsModule_notInCommonjsProject, TestRefactorConvertToEsModule_notInCommonjsProject);


}  // namespace
