// Ported fourslash tests — source phase imports (upstream 2f9fd09a).
// One static void TestX(gostd::testing::T*) per Go `func TestX` in the new
// *_test.go files; each self-registers in the fourslashrunner registry.
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

// autoImport_sourcePhase_test.go
static void TestAutoImportDoesNotAddToSourcePhaseImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: esnext
// @Filename: /a.ts
const a = 0;
export default a;
export const b = 1;

// @Filename: /b.ts
import source a from "./a";
a;
b/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyImportFixAtPosition(t, std::vector<std::string>{R"TS(import source a from "./a";
import { b } from "./a";
a;
b)TS"}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestAutoImportDoesNotAddToSourcePhaseImport, TestAutoImportDoesNotAddToSourcePhaseImport);

// goToDefinition_sourcePhase_test.go
static void TestGoToDefinition_sourcePhase(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: esnext
// @target: esnext

// @filename: /b.ts
import source b from "./a.wasm";
[|/*bUse*/b|];
export { b };

// @filename: /c.ts
import source c from "./a.wasm";
[|/*cUse*/c|];

// @filename: /d.ts
import { /*dImport*/b as d } from "./b";
/*dUse*/d;

// @filename: /e.ts
export { b as /*eExport*/e } from "./b";

// @filename: /f.ts
import { e as f } from "./e";
/*fUse*/f;
import * as ns from "./e";
ns./*property*/e;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineGoToDefinition(t, true, {"bUse", "cUse", "dImport", "dUse", "eExport", "fUse", "property"});
		f->VerifyBaselineGoToSourceDefinition(t, {"bUse", "cUse", "dImport", "dUse", "eExport", "fUse", "property"});
		f->VerifyBaselineFindAllReferences(t, {"bUse", "cUse"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToDefinition_sourcePhase, TestGoToDefinition_sourcePhase);

// goToSourceDefinitionImportSource_test.go
static void TestGoToSourceDefinitionImportSource(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: esnext
// @target: esnext
// @allowJs: true

// @filename: /a.js
const x = 1;
export default function f() {}
const g = 2;

// @filename: /a.wasm
wasm

// @filename: /b.ts
import /*f*/f from "./a.js";
/*fUse*/f();
import source /*g*/g from "/*js*/./a.js";
/*gUse*/g;
import source /*h*/h from "/*wasm*/./a.wasm";
/*hUse*/h;
import.source("/*dynamic*/./a.js");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"f", "fUse", "g", "gUse", "h", "hUse", "js", "wasm", "dynamic"});
		f->VerifyBaselineGoToSourceDefinition(t, {"f", "fUse", "g", "gUse", "h", "hUse", "js", "wasm", "dynamic"});
	});
}
REGISTER_FOURSLASH_TEST(TestGoToSourceDefinitionImportSource, TestGoToSourceDefinitionImportSource);

// referencesImportSource2_test.go
static void TestReferencesImportSource2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: esnext
// @target: esnext
// @allowJs: true

// @filename: /a.js
export default function /*a*/f() {}

// @filename: /b.ts
import /*b*/f from "./a.js";
f();

// @filename: /c.ts
import source /*c*/f from "/*module*/./a.js";
f;
import.source("./a.js");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"a", "b", "c", "module"});
		f->VerifyBaselineRename(t, nullptr, {"a", "b", "c"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesImportSource2, TestReferencesImportSource2);

// referencesImportSource_test.go
static void TestReferencesImportSource(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: esnext
// @target: esnext

// @filename: /a.d.ts
declare module "m" {
    export default function /*a*/f(): void;
}

// @filename: /b.ts
import /*b*/f from "m";
/*bUse*/f();

// @filename: /c.ts
import source /*c*/f from "m";
/*cUse*/f;
export { f };

// @filename: /d.ts
import { /*d*/f } from "./c";
/*dUse*/f;

// @filename: /e.ts
import source /*e*/g from "m";
/*eUse*/g;
import.source("m");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"a", "b", "c", "cUse", "d", "e"});
		f->VerifyBaselineRename(t, nullptr, {"a", "b", "c", "cUse", "d", "e"});
	});
}
REGISTER_FOURSLASH_TEST(TestReferencesImportSource, TestReferencesImportSource);

// organizeImports_sourcePhase_test.go
static void TestOrganizeImports_sourcePhaseImportsAreNotCoalesced(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import source b from "lib";
import source a from "lib";
a; b;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import source a from "lib";
import source b from "lib";
a; b;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_sourcePhaseImportsAreNotCoalesced, TestOrganizeImports_sourcePhaseImportsAreNotCoalesced);

static void TestOrganizeImports_sourceAndEvaluationPhaseImportsAreNotCoalesced(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { c } from "lib";
import source b from "lib";
import { a } from "lib";
a; b; c;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import source b from "lib";
import { a, c } from "lib";
a; b; c;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_sourceAndEvaluationPhaseImportsAreNotCoalesced, TestOrganizeImports_sourceAndEvaluationPhaseImportsAreNotCoalesced);

static void TestOrganizeImports_sourcePhaseImportSorting(gostd::testing::T* t) {
	const std::string imports = R"TS(import source a from "lib";
import source b from "lib";
import source c from "lib";
import source d from "lib";
import source e from "lib";
import source f from "lib";
import source g from "lib";
import source h from "lib";
import source i from "lib";
import source j from "lib";
import source k from "lib";
import source l from "lib";
import source m from "lib";
)TS";
	struct test {
		std::string name;
		std::string content;
		std::string expected;
	};
	std::vector<test> tests = {
	    {"case insensitive bindings",
	     R"TS(import { b, D } from "B";
import "a";
import source C from "lib";
import source a from "lib";)TS",
	     R"TS(import { b, D } from "B";
import "a";
import source a from "lib";
import source C from "lib";)TS"},
	    {"case sensitive bindings",
	     R"TS(import { D, b } from "a";
import "B";
import source a from "lib";
import source C from "lib";)TS",
	     R"TS(import { D, b } from "a";
import "B";
import source C from "lib";
import source a from "lib";)TS"},
	    {"no named imports",
	     R"TS(import source a from "lib";
import source B from "lib";)TS",
	     R"TS(import source B from "lib";
import source a from "lib";)TS"},
	    {"mixed import kinds",
	     std::string("import { n } from \"lib\";\n") + imports +
	         "import * as o from \"lib\";\nimport type { T } from \"lib\";",
	     std::string("import type { T } from \"lib\";\nimport * as o from \"lib\";\n") + imports +
	         "import { n } from \"lib\";"},
	};
	for (auto& test : tests) {
		t->Run(test.name, [&test](gostd::testing::T* t) {
			t->Parallel();
			tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
				auto __fsp = fourslash::NewFourslash(t, nullptr, test.content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
				f->VerifyOrganizeImports(t, test.expected + "\n",
				    lsproto::CodeActionKindSourceSortImportsTs,
				    std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortAuto}));
			});
		});
	}
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_sourcePhaseImportSorting, TestOrganizeImports_sourcePhaseImportSorting);
}
