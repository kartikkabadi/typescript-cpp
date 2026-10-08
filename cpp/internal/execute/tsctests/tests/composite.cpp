// composite.cpp — ported tscInput scenarios from
// tsc/internal/execute/tsctests/tsc_test.go: TestTscComposite
// (scenario "composite").

#include <functional>
#include <string>
#include <vector>

#include "internal/execute/tsctests/tests/registry.h"
#include "internal/execute/tsctests/tests/util.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/gostd/testing.h"

namespace tsc::execute::tsctests::tests {

namespace {

// compositeTargetModuleTsconfig — the shared tsconfig of the first four
// sub-scenarios (tsc_test.go:365+), with `composite` always true.
std::string compositeTsconfig(bool withTsBuildInfoFile) {
	return Dedent(withTsBuildInfoFile
	                  ? R"TS(
				{
					"compilerOptions": {
						"target": "es5",
						"module": "commonjs",
						"composite": true,
						"tsBuildInfoFile": "tsconfig.json.tsbuildinfo",
					},
					"include": [
						"src/**/*.ts",
					],
				})TS"
	                  : R"TS(
				{
					"compilerOptions": {
						"target": "es5",
						"module": "commonjs",
						"composite": true,
					},
					"include": [
						"src/**/*.ts",
					],
				})TS");
}

}  // namespace

REGISTER_TSCTEST(
    "TestTscComposite::when setting composite false on command line",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when setting composite false on command line",
	        .commandLineArgs = {"--composite", "false"},
	        .files =
	            {
	                {"/home/src/workspaces/project/src/main.ts",
	                 "export const x = 10;"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 compositeTsconfig(false)},
	            },
	    }
	        .run(t, "composite");
    });

REGISTER_TSCTEST(
    "TestTscComposite::when setting composite null on command line",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when setting composite null on command line",
	        .commandLineArgs = {"--composite", "null"},
	        .files =
	            {
	                {"/home/src/workspaces/project/src/main.ts",
	                 "export const x = 10;"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 compositeTsconfig(false)},
	            },
	    }
	        .run(t, "composite");
    });

REGISTER_TSCTEST(
    "TestTscComposite::when setting composite false on command line but "
    "has tsbuild info in config",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when setting composite false on command line but "
	                       "has tsbuild info in config",
	        .commandLineArgs = {"--composite", "false"},
	        .files =
	            {
	                {"/home/src/workspaces/project/src/main.ts",
	                 "export const x = 10;"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 compositeTsconfig(true)},
	            },
	    }
	        .run(t, "composite");
    });

REGISTER_TSCTEST(
    "TestTscComposite::when setting composite false and tsbuildinfo as "
    "null on command line but has tsbuild info in config",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "when setting composite false and tsbuildinfo as null on "
	            "command line but has tsbuild info in config",
	        .commandLineArgs = {"--composite", "false", "--tsBuildInfoFile",
	                            "null"},
	        .files =
	            {
	                {"/home/src/workspaces/project/src/main.ts",
	                 "export const x = 10;"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 compositeTsconfig(true)},
	            },
	    }
	        .run(t, "composite");
    });

REGISTER_TSCTEST(
    "TestTscComposite::converting to modules",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "converting to modules",
	        .files =
	            {
	                {"/home/src/workspaces/project/src/main.ts",
	                 "const x = 10;"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": {
						"module": "none",
						"composite": true,
					},
				})TS")},
	            },
	        .edits =
	            {
	                new tscEdit{
	                    .caption = "convert to modules",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->replaceFileText(
		                            "/home/src/workspaces/project/"
		                            "tsconfig.json",
		                            "none", "es2015");
	                        },
	                },
	            },
	    }
	        .run(t, "composite");
    });

REGISTER_TSCTEST(
    "TestTscComposite::synthetic jsx import of ESM module from CJS module "
    "no crash no jsx element",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "synthetic jsx import of ESM module from CJS "
	                       "module no crash no jsx element",
	        .files =
	            {
	                {"/home/src/projects/project/src/main.ts",
	                 "export default 42;"},
	                {"/home/src/projects/project/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": {
						"composite": true,
						"module": "Node16",
						"jsx": "react-jsx",
						"jsxImportSource": "solid-js",
					},
				})TS")},
	                {"/home/src/projects/project/node_modules/solid-js/"
	                 "package.json",
	                 Dedent(R"TS(
					{
						"name": "solid-js",
						"type": "module"
					}
				)TS")},
	                {"/home/src/projects/project/node_modules/solid-js/"
	                 "jsx-runtime.d.ts",
	                 Dedent(R"TS(
					export namespace JSX {
						type IntrinsicElements = { div: {}; };
					}
				)TS")},
	            },
	        .cwd = "/home/src/projects/project",
	    }
	        .run(t, "composite");
    });

REGISTER_TSCTEST(
    "TestTscComposite::synthetic jsx import of ESM module from CJS module "
    "error on jsx element",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "synthetic jsx import of ESM module from CJS "
	                       "module error on jsx element",
	        .files =
	            {
	                {"/home/src/projects/project/src/main.tsx",
	                 "export default <div/>;"},
	                {"/home/src/projects/project/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": {
						"composite": true,
						"module": "Node16",
						"jsx": "react-jsx",
						"jsxImportSource": "solid-js",
					},
				})TS")},
	                {"/home/src/projects/project/node_modules/solid-js/"
	                 "package.json",
	                 Dedent(R"TS(
					{
						"name": "solid-js",
						"type": "module"
					}
				)TS")},
	                {"/home/src/projects/project/node_modules/solid-js/"
	                 "jsx-runtime.d.ts",
	                 Dedent(R"TS(
					export namespace JSX {
						type IntrinsicElements = { div: {}; };
					}
				)TS")},
	            },
	        .cwd = "/home/src/projects/project",
	    }
	        .run(t, "composite");
    });

}  // namespace tsc::execute::tsctests::tests
