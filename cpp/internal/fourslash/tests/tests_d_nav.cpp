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

static void TestNavigateToSymbolIterator(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
class C {
    [|[Symbol.iterator]|]() {}
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, {std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "iterator", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "iterator", .Kind = lsproto::SymbolKindMethod, .ContainerName = "C", .Location = f->Ranges()[0]->LSLocation()})}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavigateToSymbolIterator, TestNavigateToSymbolIterator);

static void TestNavigateToImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @Filename: library.ts
export function [|foo|]() {}
export function [|bar|]() {}
// @Filename: user.ts
import {foo, bar as [|baz|]} from './library';)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, {std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "foo", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "foo", .Kind = lsproto::SymbolKindFunction, .Location = f->Ranges()[0]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "bar", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "bar", .Kind = lsproto::SymbolKindFunction, .Location = f->Ranges()[1]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "baz", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "baz", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[2]->LSLocation()})}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavigateToImport, TestNavigateToImport);

static void TestNavigateItemsLet(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
let [|c|] = 10;
function foo() {
    let [|d|] = 10;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, {std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "c", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "c", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[0]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "d", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "d", .Kind = lsproto::SymbolKindVariable, .ContainerName = "foo", .Location = f->Ranges()[1]->LSLocation()})}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavigateItemsLet, TestNavigateItemsLet);

static void TestNavbar_let(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = "let c = 0;";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavbar_let, TestNavbar_let);

static void TestNavbar_exportDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
export default class { }
// @Filename: b.ts
export default class C { }
// @Filename: c.ts
export default function { }
// @Filename: d.ts
export default function Func { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "a.ts");
		f->VerifyBaselineDocumentSymbol(t);
		f->GoToFile(t, "b.ts");
		f->VerifyBaselineDocumentSymbol(t);
		f->GoToFile(t, "c.ts");
		f->VerifyBaselineDocumentSymbol(t);
		f->GoToFile(t, "d.ts");
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavbar_exportDefault, TestNavbar_exportDefault);

static void TestNavbar_contains_no_duplicates(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare namespace Windows {
    export namespace Foundation {
        export var A;
        export class Test {
            public wow();
        }
    }
}

declare namespace Windows {
    export namespace Foundation {
        export var B;
        export namespace Test {
            export function Boom(): number;
        }
    }
}

class ABC {
    public foo() {
        return 3;
    }
}

namespace ABC {
    export var x = 3;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavbar_contains_no_duplicates, TestNavbar_contains_no_duplicates);

static void TestNavbar_const(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = "const c = 0;";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavbar_const, TestNavbar_const);

static void TestNavbarNestedCommonJsExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
exports.a = exports.b = exports.c = 0;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavbarNestedCommonJsExports, TestNavbarNestedCommonJsExports);

static void TestNavbar01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// Interface
interface IPoint {
    getDist(): number;
    new(): IPoint;
    (): any;
    [x:string]: number;
    prop: string;
}

/// Module
namespace Shapes {
    // Class
    export class Point implements IPoint {
        constructor (public x: number, public y: number) { }

        // Instance member
        getDist() { return Math.sqrt(this.x * this.x + this.y * this.y); }

        // Getter
        get value(): number { return 0; }

        // Setter
        set value(newValue: number) { return; }

        // Static member
        static origin = new Point(0, 0);

        // Static method
        private static getOrigin() { return Point.origin;}
    }

    enum Values { value1, value2, value3 }
}

// Local variables
var p: IPoint = new Shapes.Point(3, 4);
var dist = p.getDist();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavbar01, TestNavbar01);

static void TestDocumentSymbolTopLevelImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @typedef {number} ImportedType */
import DefaultComponent from "./component";
/** @callback ImportedCallback
 * @param {string} value
 * @returns {number}
 */
import * as utils from "./utils";
import { value, original as renamed } from "./values";
import type { Options } from "./types";

const local = 1;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentSymbolTopLevelImports, TestDocumentSymbolTopLevelImports);

} // namespace
