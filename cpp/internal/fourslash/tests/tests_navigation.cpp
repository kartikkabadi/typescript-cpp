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

// navigationItemsExportDefaultExpression_test.go
static void TestNavigationItemsExportDefaultExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export default function () {}
export default function () {
    return class Foo {
    }
}

export default () => ""
export default () => {
    return class Foo {
    }
}

export default function f1() {}
export default function f2() {
    return class Foo {
    }
}

const abc = 12;
export default abc;
export default class AB {}
export default {
    a: 1,
    b: 1,
    c: {
        d: 1
    }
}

function foo(props: { x: number; y: number }) {}
export default foo({ x: 1, y: 1 });)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationItemsExportDefaultExpression, TestNavigationItemsExportDefaultExpression);

// navigationItemsExportEqualsExpression_test.go
static void TestNavigationItemsExportEqualsExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export = function () {}
export = function () {
    return class Foo {
    }
}

export = () => ""
export = () => {
    return class Foo {
    }
}

export = function f1() {}
export = function f2() {
    return class Foo {
    }
}

const abc = 12;
export = abc;
export = class AB {}
export = {
    a: 1,
    b: 1,
    c: {
        d: 1
    }
}

function foo(props: { x: number; y: number }) {}
export = foo({ x: 1, y: 1 });)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationItemsExportEqualsExpression, TestNavigationItemsExportEqualsExpression);

// navigationItemsExportDefaultExpression2_test.go
static void TestNavigationItemsExportDefaultExpression2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export const foo = {
  foo: {},
};

export default {
  foo: {},
};

export default {
  foo: {},
};

type Type = typeof foo;

export default {
  foo: {},
} as Type;

export default {
  foo: {},
} satisfies Type;

export default (class {
  prop = 42;
});

export default (class Cls {
  prop = 42;
});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationItemsExportDefaultExpression2, TestNavigationItemsExportDefaultExpression2);

// navto_emptyPattern_test.go
static void TestNavto_emptyPattern(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: foo.ts
const [|x|]: number = 1;
function [|y|](x: string): string { return x; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "x", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[0]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "y", .Kind = lsproto::SymbolKindFunction, .Location = f->Ranges()[1]->LSLocation()})}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavto_emptyPattern, TestNavto_emptyPattern);

// navto_excludeLib1_test.go
static void TestNavto_excludeLib1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: /index.ts
import { weirdName as otherName } from "bar";
const [|weirdName|]: number = 1;
// @filename: /tsconfig.json
{}
// @filename: /node_modules/bar/index.d.ts
export const [|weirdName|];
// @filename: /node_modules/bar/package.json
{})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "weirdName", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "weirdName", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[0]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "weirdName", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[1]->LSLocation()})}), .Preferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ExcludeLibrarySymbolsInNavTo = Tristate::False})})});
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "weirdName", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "weirdName", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[0]->LSLocation()})}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavto_excludeLib1, TestNavto_excludeLib1);

}  // namespace
