// build.cpp — ported tscInput scenarios from
// tsc/internal/execute/tsctests/tscbuild_test.go: TestBuildClean,
// TestBuildConfigFileErrors, TestBuildDemoProject.

#include <functional>
#include <string>
#include <vector>

#include "internal/execute/tsctests/tests/registry.h"
#include "internal/execute/tsctests/tests/util.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/gostd/testing.h"

namespace tsc::execute::tsctests::tests {

namespace {

// getBuildDemoFileMap — tscbuild_test.go:602.
FileMap getBuildDemoFileMap(std::function<void(FileMap&)> modify = {}) {
	FileMap files{
	    {"/user/username/projects/demo/animals/animal.ts",
	     Dedent(R"TS(
			export type Size = "small" | "medium" | "large";
			export default interface Animal {
				size: Size;
			}
		)TS")},
	    {"/user/username/projects/demo/animals/dog.ts",
	     Dedent(R"TS(
			import Animal from '.';
			import { makeRandomName } from '../core/utilities';

			export interface Dog extends Animal {
				woof(): void;
				name: string;
			}

			export function createDog(): Dog {
				return ({
					size: "medium",
					woof: function(this: Dog) {
						console.log(`${ this.name } says "Woof"!`);
					},
					name: makeRandomName()
				});
			}
		)TS")},
	    {"/user/username/projects/demo/animals/index.ts",
	     Dedent(R"TS(
			import Animal from './animal';

			export default Animal;
			import { createDog, Dog } from './dog';
			export { createDog, Dog };
		)TS")},
	    {"/user/username/projects/demo/animals/tsconfig.json",
	     Dedent(R"TS(
			{
				"extends": "../tsconfig-base.json",
				"compilerOptions": {
					"outDir": "../lib/animals",
					"rootDir": "."
				},
				"references": [
					{ "path": "../core" }
				]
			}
		)TS")},
	    {"/user/username/projects/demo/core/utilities.ts",
	     Dedent(R"TS(

			export function makeRandomName() {
				return "Bob!?! ";
			}

			export function lastElementOf<T>(arr: T[]): T | undefined {
				if (arr.length === 0) return undefined;
				return arr[arr.length - 1];
			}
		)TS")},
	    {"/user/username/projects/demo/core/tsconfig.json",
	     Dedent(R"TS(
			{
				"extends": "../tsconfig-base.json",
				"compilerOptions": {
					"outDir": "../lib/core",
					"rootDir": "."
				},
			}
		)TS")},
	    {"/user/username/projects/demo/zoo/zoo.ts",
	     Dedent(R"TS(
			import { Dog, createDog } from '../animals/index';

			export function createZoo(): Array<Dog> {
				return [
					createDog()
				];
			}
		)TS")},
	    {"/user/username/projects/demo/zoo/tsconfig.json",
	     Dedent(R"TS(
			{
				"extends": "../tsconfig-base.json",
				"compilerOptions": {
					"outDir": "../lib/zoo",
					"rootDir": "."
				},
				"references": [
					{
						"path": "../animals"
					}
				]
			}
		)TS")},
	    {"/user/username/projects/demo/tsconfig-base.json",
	     Dedent(R"TS(
			{
				"compilerOptions": {
					"declaration": true,
					"target": "es5",
					"module": "commonjs",
					"strict": true,
					"noUnusedLocals": true,
					"noUnusedParameters": true,
					"noImplicitReturns": true,
					"noFallthroughCasesInSwitch": true,
					"composite": true,
				},
			}
		)TS")},
	    {"/user/username/projects/demo/tsconfig.json",
	     Dedent(R"TS(
			{
				"files": [],
				"references": [
					{
						"path": "./core"
					},
					{
						"path": "./animals",
					},
					{
						"path": "./zoo",
					},
				],
			}
		)TS")},
	};
	if (modify) {
		modify(files);
	}
	return files;
}

// modify: replace demo/core/tsconfig.json with one referencing ../zoo
// (the "circular branch" rewrite, tscbuild_test.go:739 and :827).
void demoCoreRefsZoo(FileMap& files) {
	files["/user/username/projects/demo/core/tsconfig.json"] =
	    Dedent(R"TS(
		{
			"extends": "../tsconfig-base.json",
			"compilerOptions": {
				"outDir": "../lib/core",
				"rootDir": "."
			},
			"references": [
				{
					"path": "../zoo",
				}
			]
		}
	)TS");
}

// modify: prepend `import * as A from '../animals'` to core/utilities.ts
// (the "bad-ref branch" rewrite, tscbuild_test.go:761).
void demoBadRefUtilities(FileMap& files) {
	files["/user/username/projects/demo/core/utilities.ts"] =
	    std::string("import * as A from '../animals'\n") +
	    std::get<std::string>(
	        files["/user/username/projects/demo/core/utilities.ts"]);
}

// shared syntax-error tsconfig (missing comma) — tscbuild_test.go:491.
constexpr std::string_view kSyntaxErrorTsconfig = R"TS(
		{
			"compilerOptions": {
				"composite": true,
			},
			"files": [
				"a.ts"
				"b.ts"
			]
		})TS";

constexpr std::string_view kFixedTsconfig = R"TS(
		{
			"compilerOptions": {
				"composite": true, "declaration": true
			},
			"files": [
				"a.ts",
				"b.ts"
			]
		})TS";

}  // namespace

// === TestBuildClean — scenario "clean" ===

REGISTER_TSCTEST(
    "TestBuildClean::file name and output name clashing",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "file name and output name clashing",
	        .commandLineArgs = commandLineArgsV({"--b", "--clean"}),
	        .files =
	            {
	                {"/home/src/workspaces/solution/index.js", ""},
	                {"/home/src/workspaces/solution/bar.ts", ""},
	                {"/home/src/workspaces/solution/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": { "allowJs": true }
				})TS")},
	            },
	        .cwd = "/home/src/workspaces/solution",
	    }
	        .run(t, "clean");
    });

REGISTER_TSCTEST(
    "TestBuildClean::tsx with dts emit",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "tsx with dts emit",
	        .commandLineArgs = commandLineArgsV({"--b", "project", "-v", "--explainFiles"}),
	        .files =
	            {
	                {"/home/src/workspaces/solution/project/src/main.tsx",
	                 "export const x = 10;"},
	                {"/home/src/workspaces/solution/project/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": { "declaration": true },
					"include": ["src/**/*.tsx", "src/**/*.ts"]
				})TS")},
	            },
	        .cwd = "/home/src/workspaces/solution",
	        .edits =
	            {
	                noChange,
	                new tscEdit{
	                    .caption = "clean build",
	                    .commandLineArgs = commandLineArgsV({"-b", "project", "--clean"}),
	                },
	            },
	    }
	        .run(t, "clean");
    });

// === TestBuildConfigFileErrors — scenario "configFileErrors" ===

REGISTER_TSCTEST(
    "TestBuildConfigFileErrors::when tsconfig extends the missing file",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when tsconfig extends the missing file",
	        .commandLineArgs = commandLineArgsV({"--b"}),
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.first.json",
	                 Dedent(R"TS(
					{
						"extends": "./foobar.json",
						"compilerOptions": {
							"composite": true
						}
					})TS")},
	                {"/home/src/workspaces/project/tsconfig.second.json",
	                 Dedent(R"TS(
					{
						"extends": "./foobar.json",
						"compilerOptions": {
							"composite": true
						}
					})TS")},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
					{
						"compilerOptions": {
							"composite": true
						},
						"references": [
							{ "path": "./tsconfig.first.json" },
							{ "path": "./tsconfig.second.json" }
						]
					})TS")},
	            },
	    }
	        .run(t, "configFileErrors");
    });

REGISTER_TSCTEST(
    "TestBuildConfigFileErrors::reports invalid project reference fields",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "reports invalid project reference fields",
	        .commandLineArgs = commandLineArgsV({"--b", "--dry"}),
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
					{
						"compilerOptions": {
							"composite": true
						},
						"files": ["index.ts"],
						"references": [
							{ "path": true },
							{ "circular": true },
							{ "path": "./utils", "circular": "yes" },
							{ "path": "" },
							{ "path": "./valid", "circular": true }
						]
					})TS")},
	                {"/home/src/workspaces/project/index.ts",
	                 "export const x = 10;"},
	                {"/home/src/workspaces/project/utils/tsconfig.json",
	                 Dedent(R"TS(
					{
						"compilerOptions": {
							"composite": true
						},
						"files": ["index.ts"]
					})TS")},
	                {"/home/src/workspaces/project/utils/index.ts",
	                 "export const y = 10;"},
	                {"/home/src/workspaces/project/valid/tsconfig.json",
	                 Dedent(R"TS(
					{
						"compilerOptions": {
							"composite": true
						},
						"files": ["index.ts"]
					})TS")},
	                {"/home/src/workspaces/project/valid/index.ts",
	                 "export const z = 10;"},
	            },
	    }
	        .run(t, "configFileErrors");
    });

REGISTER_TSCTEST(
    "TestBuildConfigFileErrors::reports syntax errors in config file",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "reports syntax errors in config file",
	        .commandLineArgs = commandLineArgsV({"--b"}),
	        .files =
	            {
	                {"/home/src/workspaces/project/a.ts",
	                 "export function foo() { }"},
	                {"/home/src/workspaces/project/b.ts",
	                 "export function bar() { }"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(kSyntaxErrorTsconfig)},
	            },
	        .edits =
	            {
	                new tscEdit{
	                    .caption = "reports syntax errors after change to "
	                               "config file",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->replaceFileText(
		                            "/home/src/workspaces/project/"
		                            "tsconfig.json",
		                            ",", ", \"declaration\": true");
	                        },
	                },
	                new tscEdit{
	                    .caption =
	                        "reports syntax errors after change to ts file",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->appendFile(
		                            "/home/src/workspaces/project/a.ts",
		                            "export function fooBar() { }");
	                        },
	                },
	                noChange,
	                new tscEdit{
	                    .caption =
	                        "builds after fixing config file errors",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->writeFileNoError(
		                            "/home/src/workspaces/project/"
		                            "tsconfig.json",
		                            Dedent(kFixedTsconfig));
	                        },
	                },
	            },
	    }
	        .run(t, "configFileErrors");
    });

REGISTER_TSCTEST(
    "TestBuildConfigFileErrors::missing config file",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "missing config file",
	        .commandLineArgs = commandLineArgsV({"--b", "bogus.json"}),
	    }
	        .run(t, "configFileErrors");
    });

REGISTER_TSCTEST(
    "TestBuildConfigFileErrors::reports syntax errors in config file "
    "[watch]",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "reports syntax errors in config file",
	        .commandLineArgs = commandLineArgsV({"--b", "-w"}),
	        .files =
	            {
	                {"/home/src/workspaces/project/a.ts",
	                 "export function foo() { }"},
	                {"/home/src/workspaces/project/b.ts",
	                 "export function bar() { }"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(kSyntaxErrorTsconfig)},
	            },
	        .edits =
	            {
	                new tscEdit{
	                    .caption = "reports syntax errors after change to "
	                               "config file",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->replaceFileText(
		                            "/home/src/workspaces/project/"
		                            "tsconfig.json",
		                            ",", ", \"declaration\": true");
	                        },
	                },
	                new tscEdit{
	                    .caption =
	                        "reports syntax errors after change to ts file",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->appendFile(
		                            "/home/src/workspaces/project/a.ts",
		                            "export function fooBar() { }");
	                        },
	                },
	                new tscEdit{
	                    .caption = "reports error when there is no change to "
	                               "tsconfig file",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->replaceFileText(
		                            "/home/src/workspaces/project/"
		                            "tsconfig.json",
		                            "", "");
	                        },
	                },
	                new tscEdit{
	                    .caption =
	                        "builds after fixing config file errors",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->writeFileNoError(
		                            "/home/src/workspaces/project/"
		                            "tsconfig.json",
		                            Dedent(kFixedTsconfig));
	                        },
	                },
	            },
	    }
	        .run(t, "configFileErrors");
    });

// === TestBuildDemoProject — scenario "demo" ===

REGISTER_TSCTEST(
    "TestBuildDemoProject::in master branch with everything setup "
    "correctly and reports no error",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "in master branch with everything setup correctly "
	                       "and reports no error",
	        .commandLineArgs = commandLineArgsV({"--b", "--verbose"}),
	        .files = getBuildDemoFileMap(),
	        .cwd = "/user/username/projects/demo",
	        .edits = noChangeOnlyEdit,
	    }
	        .run(t, "demo");
    });

REGISTER_TSCTEST(
    "TestBuildDemoProject::in circular branch reports the error about it "
    "by stopping build",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "in circular branch reports the error about it by "
	                       "stopping build",
	        .commandLineArgs = commandLineArgsV({"--b", "--verbose"}),
	        .files = getBuildDemoFileMap(demoCoreRefsZoo),
	        .cwd = "/user/username/projects/demo",
	    }
	        .run(t, "demo");
    });

REGISTER_TSCTEST(
    "TestBuildDemoProject::in bad-ref branch reports the error about "
    "files not in rootDir at the import location",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "in bad-ref branch reports the error about files "
	                       "not in rootDir at the import location",
	        .commandLineArgs = commandLineArgsV({"--b", "--verbose"}),
	        .files = getBuildDemoFileMap(demoBadRefUtilities),
	        .cwd = "/user/username/projects/demo",
	    }
	        .run(t, "demo");
    });

REGISTER_TSCTEST(
    "TestBuildDemoProject::in circular is set in the reference",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "in circular is set in the reference",
	        .commandLineArgs = commandLineArgsV({"--b", "--verbose"}),
	        .files =
	            getBuildDemoFileMap([](FileMap& files) {
		            files["/user/username/projects/demo/a/tsconfig.json"] =
		                Dedent(R"TS(
				{
					"extends": "../tsconfig-base.json",
					"compilerOptions": {
						"outDir": "../lib/a",
						"rootDir": "."
					},
					"references": [
						{
							"path": "../b",
							"circular": true
						}
					]
				})TS");
		            files["/user/username/projects/demo/b/tsconfig.json"] =
		                Dedent(R"TS(
				{
					"extends": "../tsconfig-base.json",
					"compilerOptions": {
						"outDir": "../lib/b",
						"rootDir": "."
					},
					"references": [
						{
							"path": "../a",
						}
					]
				})TS");
		            files["/user/username/projects/demo/a/index.ts"] =
		                "export const a = 10;";
		            files["/user/username/projects/demo/b/index.ts"] =
		                "export const b = 10;";
		            files["/user/username/projects/demo/tsconfig.json"] =
		                Dedent(R"TS(
				{
					"files": [],
					"references": [
						{
							"path": "./core"
						},
						{
							"path": "./animals",
						},
						{
							"path": "./zoo",
						},
						{
							"path": "./a",
						},
						{
							"path": "./b",
						},
					],
				})TS");
	            }),
	        .cwd = "/user/username/projects/demo",
	    }
	        .run(t, "demo");
    });

REGISTER_TSCTEST(
    "TestBuildDemoProject::updates with circular reference",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "updates with circular reference",
	        .commandLineArgs = commandLineArgsV({"--b", "-w", "--verbose"}),
	        .files = getBuildDemoFileMap(demoCoreRefsZoo),
	        .cwd = "/user/username/projects/demo",
	        .edits =
	            {
	                new tscEdit{
	                    .caption = "Fix error",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->writeFileNoError(
		                            "/user/username/projects/demo/core/"
		                            "tsconfig.json",
		                            Dedent(R"TS(
							{
								"extends": "../tsconfig-base.json",
								"compilerOptions": {
									"outDir": "../lib/core",
									"rootDir": "."
								},
							}
						)TS"));
	                        },
	                },
	            },
	    }
	        .run(t, "demo");
    });

REGISTER_TSCTEST(
    "TestBuildDemoProject::updates with bad reference",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "updates with bad reference",
	        .commandLineArgs = commandLineArgsV({"--b", "-w", "--verbose"}),
	        .files = getBuildDemoFileMap(demoBadRefUtilities),
	        .cwd = "/user/username/projects/demo",
	        .edits =
	            {
	                new tscEdit{
	                    .caption = "Prepend a line",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->prependFile(
		                            "/user/username/projects/demo/core/"
		                            "utilities.ts",
		                            "\n");
	                        },
	                },
	            },
	    }
	        .run(t, "demo");
    });

}  // namespace tsc::execute::tsctests::tests
