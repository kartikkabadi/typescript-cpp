// noemit.cpp — ported tscInput scenarios from
// tsc/internal/execute/tsctests/tsc_test.go: TestTscNoEmit (expanded from
// the getTscNoEmitAndErrors* generators) and TestTscNoEmitOnError.

#include <functional>
#include <string>
#include <vector>

#include "internal/execute/tsctests/tests/registry.h"
#include "internal/execute/tsctests/tests/util.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/gostd/testing.h"

namespace tsc::execute::tsctests::tests {

namespace {

// getTscNoEmitAndErrorsFileMap — tsc_test.go:3567, expanded.
FileMap noEmitAndErrorsFileMap(const std::string& aText, bool incremental,
                               bool dtsEnabled, bool asModules) {
	FileMap files{
	    {"/home/src/projects/project/a.ts",
	     (asModules ? "export " : "") + aText},
	    {"/home/src/projects/project/tsconfig.json",
	     Dedent(std::string(R"TS(
				{
					"compilerOptions": {
						"incremental": )TS") +
	            (incremental ? "true" : "false") + R"TS(,
						"declaration": )TS" +
	            (dtsEnabled ? "true" : "false") + R"TS(
					}
				}
		)TS")},
	};
	if (asModules) {
		files["/home/src/projects/project/b.ts"] = "export const b = 10;";
	}
	return files;
}

// edits generator — tsc_test.go:3626, for the non-watch cases.
std::vector<tscEdit*> noEmitAndErrorsEdits(const std::string& aText,
                                         const std::string& fixedATsContent,
                                         std::vector<std::string> emitArgs) {
	return {
	    noChange,
	    new tscEdit{
	        .caption = "Fix error",
	        .edit =
	            [fixedATsContent](TestSys* sys) {
		            sys->writeFileNoError(
		                "/home/src/projects/project/a.ts",
		                fixedATsContent);
	            },
	    },
	    noChange,
	    new tscEdit{
	        .caption = "Emit after fixing error",
	        .commandLineArgs = emitArgs,
	    },
	    noChange,
	    new tscEdit{
	        .caption = "Introduce error",
	        .edit =
	            [aText](TestSys* sys) {
		            sys->writeFileNoError(
		                "/home/src/projects/project/a.ts", aText);
	            },
	    },
	    new tscEdit{
	        .caption = "Emit when error",
	        .commandLineArgs = std::move(emitArgs),
	    },
	    noChange,
	};
}

// getTscNoEmitOnErrorFileMap — tsc_test.go:4018, expanded.
FileMap noEmitOnErrorFileMap(const std::string& mainErrorContent,
                             bool declaration, bool incremental) {
	return {
	    {"/user/username/projects/noEmitOnError/tsconfig.json",
	     Dedent(std::string(R"TS(
			{
				"compilerOptions": {
					"outDir": "./dev-build",
					"declaration": )TS") +
	            (declaration ? "true" : "false") + R"TS(,
					"incremental": )TS" +
	            (incremental ? "true" : "false") + R"TS(,
					"noEmitOnError": true,
				},
			})TS")},
	    {"/user/username/projects/noEmitOnError/shared/types/db.ts",
	     Dedent(R"TS(
			export interface A {
				name: string;
			}
		)TS")},
	    {"/user/username/projects/noEmitOnError/src/main.ts",
	     mainErrorContent},
	    {"/user/username/projects/noEmitOnError/src/other.ts",
	     Dedent(R"TS(
			console.log("hi");
			export { }
		)TS")},
	};
}

}  // namespace

REGISTER_TSCTEST(
    "TestTscNoEmit::when project has strict true",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when project has strict true",
	        .commandLineArgs = commandLineArgsV({"--noEmit"}),
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
						{
							"compilerOptions": {
								"incremental": true,
								"strict": true
							}
						})TS")},
	                {"/home/src/workspaces/project/class1.ts",
	                 "export class class1 {}"},
	            },
	        .edits = noChangeOnlyEdit,
	    }
	        .run(t, "noEmit");
    });

REGISTER_TSCTEST(
    "TestTscNoEmit::syntax errors",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "syntax errors",
	        .commandLineArgs = commandLineArgsV({"--noEmit"}),
	        .files = noEmitAndErrorsFileMap("const a = \"hello", false, false,
	                                        false),
	        .cwd = "/home/src/projects/project",
	        .edits = noEmitAndErrorsEdits("const a = \"hello",
	                                      "const a = \"hello\";", {}),
	    }
	        .run(t, "noEmit");
    });

REGISTER_TSCTEST(
    "TestTscNoEmit::semantic errors with incremental",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "semantic errors with incremental",
	        .commandLineArgs = commandLineArgsV({"--noEmit"}),
	        .files = noEmitAndErrorsFileMap("const a: number = \"hello\"",
	                                        true, false, false),
	        .cwd = "/home/src/projects/project",
	        .edits = noEmitAndErrorsEdits("const a: number = \"hello\"",
	                                      "const a = \"hello\";", {}),
	    }
	        .run(t, "noEmit");
    });

REGISTER_TSCTEST(
    "TestTscNoEmit::dts errors",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "dts errors",
	        .commandLineArgs = commandLineArgsV({"--noEmit"}),
	        .files = noEmitAndErrorsFileMap(
	            "const a = class { private p = 10; };", false, true, false),
	        .cwd = "/home/src/projects/project",
	        .edits = noEmitAndErrorsEdits(
	            "const a = class { private p = 10; };",
	            "const a = \"hello\";", {}),
	    }
	        .run(t, "noEmit");
    });

REGISTER_TSCTEST(
    "TestTscNoEmit::dts errors without dts enabled with incremental as "
    "modules",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "dts errors without dts enabled with incremental "
	                       "as modules",
	        .commandLineArgs = commandLineArgsV({"--noEmit"}),
	        .files = noEmitAndErrorsFileMap(
	            "const a = class { private p = 10; };", true, false, true),
	        .cwd = "/home/src/projects/project",
	        .edits = noEmitAndErrorsEdits(
	            "const a = class { private p = 10; };",
	            "export const a = \"hello\";", {}),
	    }
	        .run(t, "noEmit");
    });

// -b -v variant — tsc_test.go:3997 (getTscNoEmitAndErrorsTestCases with
// {"-b", "-v"}): baseline lands under tsbuild/noEmit/.
REGISTER_TSCTEST(
    "TestTscNoEmit::syntax errors [b -v]",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "syntax errors",
	        .commandLineArgs = commandLineArgsV({"-b", "-v", "--noEmit"}),
	        .files = noEmitAndErrorsFileMap("const a = \"hello", false, false,
	                                        false),
	        .cwd = "/home/src/projects/project",
	        .edits = noEmitAndErrorsEdits("const a = \"hello",
	                                      "const a = \"hello\";",
	                                      {"-b", "-v"}),
	    }
	        .run(t, "noEmit");
    });

// === TestTscNoEmitOnError — tsc_test.go:4011 (scenario "noEmitOnError") ===

REGISTER_TSCTEST(
    "TestTscNoEmitOnError::syntax errors",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "syntax errors",
	        .commandLineArgs = commandLineArgsV({}),
	        .files = noEmitOnErrorFileMap(
	            Dedent(R"TS(
                import { A } from "../shared/types/db";
                const a = {
                    lastName: 'sdsd'
                ;
            )TS"),
	            false, false),
	        .cwd = "/user/username/projects/noEmitOnError",
	        .edits =
	            {
	                noChange,
	                new tscEdit{
	                    .caption = "Fix error",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->writeFileNoError(
		                            "/user/username/projects/noEmitOnError/"
		                            "src/main.ts",
		                            Dedent(R"TS(
                import { A } from "../shared/types/db";
                const a = {
                    lastName: 'sdsd'
                };)TS"));
	                        },
	                },
	                noChange,
	            },
	    }
	        .run(t, "noEmitOnError");
    });

REGISTER_TSCTEST(
    "TestTscNoEmitOnError::semantic errors with incremental",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "semantic errors with incremental",
	        .commandLineArgs = commandLineArgsV({}),
	        .files = noEmitOnErrorFileMap(
	            Dedent(R"TS(
                import { A } from "../shared/types/db";
                const a: string = 10;)TS"),
	            false, true),
	        .cwd = "/user/username/projects/noEmitOnError",
	        .edits =
	            {
	                noChange,
	                new tscEdit{
	                    .caption = "Fix error",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->writeFileNoError(
		                            "/user/username/projects/noEmitOnError/"
		                            "src/main.ts",
		                            Dedent(R"TS(
                import { A } from "../shared/types/db";
                const a: string = "hello";)TS"));
	                        },
	                },
	                noChange,
	            },
	    }
	        .run(t, "noEmitOnError");
    });

REGISTER_TSCTEST(
    "TestTscNoEmitOnError::dts errors with declaration [b -v]",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "dts errors with declaration",
	        .commandLineArgs = commandLineArgsV({"-b", "-v"}),
	        .files = noEmitOnErrorFileMap(
	            Dedent(R"TS(
                import { A } from "../shared/types/db";
                export const a = class { private p = 10; };
            )TS"),
	            true, false),
	        .cwd = "/user/username/projects/noEmitOnError",
	        .edits =
	            {
	                noChange,
	                new tscEdit{
	                    .caption = "Fix error",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->writeFileNoError(
		                            "/user/username/projects/noEmitOnError/"
		                            "src/main.ts",
		                            Dedent(R"TS(
                import { A } from "../shared/types/db";
                export const a = class { p = 10; };
            )TS"));
	                        },
	                },
	                noChange,
	            },
	    }
	        .run(t, "noEmitOnError");
    });

REGISTER_TSCTEST(
    "TestTscNoEmitOnError::when declarationMap changes",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when declarationMap changes",
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
						{
							"compilerOptions": {
								"noEmitOnError": true,
								"declaration": true,
								"composite": true,
							},
						})TS")},
	                {"/home/src/workspaces/project/a.ts", "const x = 10;"},
	                {"/home/src/workspaces/project/b.ts", "const y = 10;"},
	            },
	        .edits =
	            {
	                new tscEdit{
	                    .caption = "error and enable declarationMap",
	                    .commandLineArgs = commandLineArgsV({"--declarationMap"}),
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->replaceFileText(
		                            "/home/src/workspaces/project/a.ts",
		                            "x", "x: 20");
	                        },
	                },
	                new tscEdit{
	                    .caption = "fix error declarationMap",
	                    .commandLineArgs = commandLineArgsV({"--declarationMap"}),
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->replaceFileText(
		                            "/home/src/workspaces/project/a.ts",
		                            "x: 20", "x");
	                        },
	                },
	            },
	    }
	        .run(t, "noEmitOnError");
    });

REGISTER_TSCTEST(
    "TestTscNoEmitOnError::file deleted before fixing error with "
    "noEmitOnError",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "file deleted before fixing error with "
	                       "noEmitOnError",
	        .commandLineArgs = commandLineArgsV({"-i"}),
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
						{
							"compilerOptions": {
								"outDir": "outDir",
								"noEmitOnError": true,
							},
						})TS")},
	                {"/home/src/workspaces/project/file1.ts",
	                 "export const x: 30 = \"hello\";"},
	                {"/home/src/workspaces/project/file2.ts",
	                 "export class D { }"},
	            },
	        .edits =
	            {
	                new tscEdit{
	                    .caption = "delete file without error",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->removeNoError(
		                            "/home/src/workspaces/project/file2.ts");
	                        },
	                },
	            },
	    }
	        .run(t, "noEmitOnError");
    });

}  // namespace tsc::execute::tsctests::tests
