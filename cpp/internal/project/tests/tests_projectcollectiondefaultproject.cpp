// Port of tsc/internal/project/projectcollectiondefaultproject_test.go.
#include <string>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/project/session.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

namespace lsproto = tsc::lsp::lsproto;
namespace projecttestutil = tsc::testutil::projecttestutil;
using tsc::gostd::testing::T;

void TestProjectCollectionDefaultProject(T* t) {
	t->Parallel();

	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	// Project 1 references project 2, which does not have open files.
	// File project1/dist/index.d.ts does not belong to any
	// tsconfig.json, but is included in programs for projects 3 and 4
	// via project 1's output. When looking for a default project for
	// project1/dist/index.d.ts, we should not try to unconditionally
	// access project 2, which isn't loaded because of
	// `disableReferencedProjectLoad`.
	projecttestutil::FileMap files{
	    {"/project1/tsconfig.json", std::string(R"({
			"extends": "../tsconfig.json",
			"files": [],
			"include": ["src/**/*"],
			"references": [
				{
					"path": "../project2"
				}
			],
			"compilerOptions": {
				"composite": true,
				"outDir": "./dist",
				"rootDir": "./src",
			}
		})")},
	    {"/project1/src/index.ts",
	     std::string("export const foo = 42;\n"
	                 "		export type Bar = { a: string };")},
	    {"/project1/dist/index.d.ts",
	     std::string("export declare const foo = 42;\n"
	                 "			export type Bar = {\n"
	                 "				a: string;\n"
	                 "			};")},
	    {"/project2/tsconfig.json", std::string(R"({
			"extends": "../tsconfig.json",
			"files": [],
			"include": ["src/**/*"],
			"compilerOptions": {
				"composite": true,
				"outDir": "./dist",
				"rootDir": "./src"
			}
		})")},
	    {"/project3/tsconfig.json", std::string(R"({
			"extends": "../tsconfig.json",
			"files": [],
			"include": ["src/**/*"],
			"references": [
				{
					"path": "../project1"
				}
			],
			"compilerOptions": {
				"composite": true,
				"outDir": "./dist",
				"rootDir": "./src",
			}
		})")},
	    {"/project3/src/index.ts",
	     std::string(
	         "import { Bar } from \"../../project1/dist/index.js\";\n"
	         "			declare const b: Bar;\n"
	         "			const x: string = b.a;")},
	    {"/project4/tsconfig.json", std::string(R"({
			"extends": "../tsconfig.json",
			"files": [],
			"include": ["src/**/*"],
			"references": [
				{
					"path": "../project1"
				}
			],
			"compilerOptions": {
				"composite": true,
				"outDir": "./dist",
				"rootDir": "./src",
			}
		})")},
	    {"/project4/src/index.ts",
	     std::string(
	         "import { Bar } from \"../../project1/dist/index.js\";\n"
	         "declare const b: Bar;\n"
	         "const x: string = b.a;")},
	    {"/tsconfig.json", std::string(R"({
			"compilerOptions": {
				"disableReferencedProjectLoad": true,
				"disableSolutionSearching": true,
				"disableSourceOfProjectReferenceRedirect": true
			},
			"files": [],
			"references": [
				{
					"path": "./project1"
				},
				{
					"path": "./project2"
				},
				{
					"path": "./project3"
				},
				{
					"path": "./project4"
				}
			]
		})")},
	};
	std::vector<lsproto::DocumentUri> uris{
	    "file:///project1/dist/index.d.ts",
	    "file:///project1/src/index.ts",
	    "file:///project3/src/index.ts",
	    "file:///project4/src/index.ts",
	};
	auto [session, sessionUtils] = projecttestutil::Setup(files);
	auto ctx = t->Context();
	// Should not crash.
	for (auto& uri : uris) {
		std::string fileName = uri.substr(7); // strip "file://"
		session->DidOpenFile(
		    ctx, uri, 1,
		    std::get<std::string>(files[fileName]),
		    lsproto::LanguageKindTypeScript);
	}
}

REGISTER_UNIT_TEST("project.TestProjectCollectionDefaultProject",
                   TestProjectCollectionDefaultProject);

} // namespace
