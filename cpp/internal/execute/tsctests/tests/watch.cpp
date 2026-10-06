// watch.cpp — ported tscInput scenarios from
// tsc/internal/execute/tsctests/tscwatch_test.go: TestWatch (subset),
// plus all of TestTscNoEmitWatch via the ported noEmitWatchTestInput.

#include <functional>
#include <string>
#include <vector>

#include "internal/execute/tsctests/tests/registry.h"
#include "internal/execute/tsctests/tests/util.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/gostd/testing.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::execute::tsctests::tests {

namespace {

// listToTsconfig — tscwatch_test.go:608.
std::pair<std::string, std::string> listToTsconfig(
    const std::string& base, std::vector<std::string> tsconfigOpts = {}) {
	std::string optionString;
	for (size_t i = 0; i < tsconfigOpts.size(); i++) {
		if (i) optionString += ",\n            ";
		optionString += tsconfigOpts[i];
	}
	std::string tsconfigText = "{\n\t\"compilerOptions\": {\n";
	std::string after = "            ";
	if (!base.empty()) {
		tsconfigText += "            " + base;
		after = ",\n            ";
	}
	if (!tsconfigOpts.empty()) {
		tsconfigText += after + optionString;
	}
	tsconfigText += "\n\t}\n}";
	return {tsconfigText, optionString};
}

// toTsconfig — tscwatch_test.go:627.
std::string toTsconfig(const std::string& base,
                       const std::string& compilerOpts) {
	return listToTsconfig(base, {compilerOpts}).first;
}

// noEmitWatchTestInput — tscwatch_test.go:632.
tscInput noEmitWatchTestInput(
    const std::string& subScenario,
    std::vector<std::string> commandLineArgs, const std::string& aText,
    std::vector<std::string> tsconfigOptions = {}) {
	static const std::string noEmitOpt = "\"noEmit\": true";
	// clang-15: structured bindings can't be captured by the edit lambdas,
	// so bind the pair members to named locals.
	auto tsconfig = listToTsconfig(noEmitOpt, tsconfigOptions);
	auto& tsconfigText = tsconfig.first;
	auto optionString = tsconfig.second;
	return tscInput{
	    .subScenario = subScenario,
	    .commandLineArgs = std::move(commandLineArgs),
	    .files =
	        {
	            {"/home/src/workspaces/project/a.ts", aText},
	            {"/home/src/workspaces/project/tsconfig.json",
	             tsconfigText},
	        },
	    .edits =
	        {
	            newTscEdit("fix error",
	                       [](TestSys* sys) {
		                       sys->writeFileNoError(
		                           "/home/src/workspaces/project/a.ts",
		                           "const a = \"hello\";");
	                       }),
	            newTscEdit(
	                "emit after fixing error",
	                [optionString](TestSys* sys) {
		                sys->writeFileNoError(
		                    "/home/src/workspaces/project/tsconfig.json",
		                    toTsconfig("", optionString));
	                }),
	            newTscEdit(
	                "no emit run after fixing error",
	                [optionString](TestSys* sys) {
		                sys->writeFileNoError(
		                    "/home/src/workspaces/project/tsconfig.json",
		                    toTsconfig(noEmitOpt, optionString));
	                }),
	            newTscEdit("introduce error",
	                       [aText](TestSys* sys) {
		                       sys->writeFileNoError(
		                           "/home/src/workspaces/project/a.ts",
		                           aText);
	                       }),
	            newTscEdit(
	                "emit when error",
	                [optionString](TestSys* sys) {
		                sys->writeFileNoError(
		                    "/home/src/workspaces/project/tsconfig.json",
		                    toTsconfig("", optionString));
	                }),
	            newTscEdit(
	                "no emit run when error",
	                [optionString](TestSys* sys) {
		                sys->writeFileNoError(
		                    "/home/src/workspaces/project/tsconfig.json",
		                    toTsconfig(noEmitOpt, optionString));
	                }),
	        },
	};
}

}  // namespace

// === TestWatch — scenario "commandLineWatch" ===

REGISTER_TSCTEST(
    "TestWatch::watch with no tsconfig",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch with no tsconfig",
	        .commandLineArgs = {"index.ts", "--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts", ""},
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch with tsconfig and incremental",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch with tsconfig and incremental",
	        .commandLineArgs = {"--watch", "--incremental"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts", ""},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch handles many bun dependency files",
    [](gostd::testing::T* t) {
	    // bunDependencyTest — tscwatch_test.go:13.
	    FileMap files;
	    std::string index;
	    std::string fileNames = "\"index.ts\"";
	    for (int i = 0; i < 12; i++) {
		    auto name = "pkg" + std::to_string(i);
		    auto value = "value" + std::to_string(i);
		    index += "import { " + value + " } from \"./node_modules/.bun/" +
		             name + "/index\"; " + value + ";\n";
		    files["/home/src/workspaces/project/node_modules/.bun/" + name +
		          "/index.ts"] =
		        "export const " + value + " = " + std::to_string(i) + ";";
		    fileNames +=
		        ", \"node_modules/.bun/" + name + "/index.ts\"";
	    }
	    files["/home/src/workspaces/project/index.ts"] = index;
	    files["/home/src/workspaces/project/tsconfig.json"] =
	        "{\n\t\"compilerOptions\": {},\n\t\"files\": [" + fileNames +
	        "]\n}";
	    tscInput{
	        .subScenario = "watch handles many bun dependency files",
	        .commandLineArgs = {"--watch"},
	        .files = std::move(files),
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch skips build when no files change",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch skips build when no files change",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts",
	                 "const x: number = 1;"},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits = {noChange},
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch rebuilds when file is modified",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch rebuilds when file is modified",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts",
	                 "const x: number = 1;"},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits =
	            {
	                newTscEdit("modify file",
	                           [](TestSys* sys) {
		                           sys->writeFileNoError(
		                               "/home/src/workspaces/project/"
		                               "index.ts",
		                               "const x: number = 2;");
	                           }),
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch rebuilds when source file is deleted",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch rebuilds when source file is deleted",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/a.ts",
	                 "import { b } from \"./b\";"},
	                {"/home/src/workspaces/project/b.ts",
	                 "export const b = 1;"},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits =
	            {
	                new tscEdit{
	                    .caption = "delete imported file",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->removeNoError(
		                            "/home/src/workspaces/project/b.ts");
	                        },
	                    .expectedDiff =
	                        "incremental resolves to .js output from prior "
	                        "build (TS7016) while clean build cannot find "
	                        "module at all (TS2307)",
	                },
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch detects new file resolving failed import",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch detects new file resolving failed import",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/a.ts",
	                 "import { b } from \"./b\";"},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits =
	            {
	                newTscEdit("create missing file",
	                           [](TestSys* sys) {
		                           sys->writeFileNoError(
		                               "/home/src/workspaces/project/b.ts",
		                               "export const b = 1;");
	                           }),
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch detects imported file added in new directory",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "watch detects imported file added in new directory",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts",
	                 "import { util } from \"./lib/util\";"},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits =
	            {
	                newTscEdit("create directory and imported file",
	                           [](TestSys* sys) {
		                           sys->writeFileNoError(
		                               "/home/src/workspaces/project/lib/"
		                               "util.ts",
		                               "export const util = \"hello\";");
	                           }),
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch rebuilds when tsconfig include pattern adds file",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "watch rebuilds when tsconfig include pattern adds file",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts",
	                 "const x = 1;"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 "{\n\t\"compilerOptions\": {},\n\t\"include\": "
	                 "[\"*.ts\"]\n}"},
	            },
	        .edits =
	            {
	                newTscEdit(
	                    "widen include pattern to add src dir",
	                    [](TestSys* sys) {
		                    sys->writeFileNoError(
		                        "/home/src/workspaces/project/src/extra.ts",
		                        "export const extra = 2;");
		                    sys->writeFileNoError(
		                        "/home/src/workspaces/project/tsconfig.json",
		                        "{\n\t\"compilerOptions\": {},\n\t\"include\": "
		                        "[\"*.ts\", \"src/**/*.ts\"]\n}");
	                    }),
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch rebuilds when tsconfig is modified to change strict",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch rebuilds when tsconfig is modified to "
	                       "change strict",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts",
	                 "const x = null; const y: string = x;"},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits =
	            {
	                newTscEdit(
	                    "enable strict mode",
	                    [](TestSys* sys) {
		                    sys->writeFileNoError(
		                        "/home/src/workspaces/project/tsconfig.json",
		                        "{\"compilerOptions\": {\"strict\": true}}");
	                    }),
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch handles tsconfig deleted",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch handles tsconfig deleted",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts",
	                 "const x = 1;"},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits =
	            {
	                new tscEdit{
	                    .caption = "delete tsconfig",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->removeNoError(
		                            "/home/src/workspaces/project/"
		                            "tsconfig.json");
	                        },
	                    .expectedDiff =
	                        "incremental reports config read error while "
	                        "clean build without tsconfig prints usage "
	                        "help",
	                },
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch handles tsconfig with extends base modified",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "watch handles tsconfig with extends base modified",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts",
	                 "const x = null; const y: string = x;"},
	                {"/home/src/workspaces/project/base.json",
	                 "{\n\t\"compilerOptions\": { \"strict\": false }\n}"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 "{\n\t\"extends\": \"./base.json\"\n}"},
	            },
	        .edits =
	            {
	                newTscEdit(
	                    "modify base config to enable strict",
	                    [](TestSys* sys) {
		                    sys->writeFileNoError(
		                        "/home/src/workspaces/project/base.json",
		                        "{\n\t\"compilerOptions\": { \"strict\": "
		                        "true }\n}");
	                    }),
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch detects file renamed and renamed back",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch detects file renamed and renamed back",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts",
	                 "import { helper } from \"./helper\";"},
	                {"/home/src/workspaces/project/helper.ts",
	                 "export const helper = 1;"},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits =
	            {
	                new tscEdit{
	                    .caption = "rename helper to helper2",
	                    .edit =
	                        [](TestSys* sys) {
		                        sys->renameFileNoError(
		                            "/home/src/workspaces/project/helper.ts",
		                            "/home/src/workspaces/project/"
		                            "helper2.ts");
	                        },
	                    .expectedDiff =
	                        "incremental resolves to .js output from prior "
	                        "build while clean build cannot find module",
	                },
	                newTscEdit(
	                    "rename back to helper",
	                    [](TestSys* sys) {
		                    sys->renameFileNoError(
		                        "/home/src/workspaces/project/helper2.ts",
		                        "/home/src/workspaces/project/helper.ts");
	                    }),
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch detects file deleted and new file added "
    "simultaneously",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch detects file deleted and new file added "
	                       "simultaneously",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/a.ts",
	                 "import { b } from \"./b\";"},
	                {"/home/src/workspaces/project/b.ts",
	                 "export const b = 1;"},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits =
	            {
	                newTscEdit(
	                    "delete b.ts and create c.ts with updated import",
	                    [](TestSys* sys) {
		                    sys->removeNoError(
		                        "/home/src/workspaces/project/b.ts");
		                    sys->writeFileNoError(
		                        "/home/src/workspaces/project/c.ts",
		                        "export const c = 2;");
		                    sys->writeFileNoError(
		                        "/home/src/workspaces/project/a.ts",
		                        "import { c } from \"./c\";");
	                    }),
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch detects change in symlinked node_modules file",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "watch detects change in symlinked node_modules file",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts",
	                 "import { shared } from \"shared\";"},
	                {"/home/src/workspaces/shared/index.ts",
	                 "export const shared = \"v1\";"},
	                {"/home/src/workspaces/project/node_modules/shared/"
	                 "index.ts",
	                 vfs::vfstest::Symlink(
	                     "/home/src/workspaces/shared/index.ts")},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits =
	            {
	                newTscEdit("modify symlink target",
	                           [](TestSys* sys) {
		                           sys->writeFileNoError(
		                               "/home/src/workspaces/shared/"
		                               "index.ts",
		                               "export const shared = \"v2\";");
	                           }),
	            },
	    }
	        .run(t, "commandLineWatch");
    });

REGISTER_TSCTEST(
    "TestWatch::watch detects error across global script files when "
    "global decl removed",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "watch detects error across global script files "
	                       "when global decl removed",
	        .commandLineArgs = {"--watch"},
	        .files =
	            {
	                {"/home/src/workspaces/project/a.ts",
	                 "console.log(a);"},
	                {"/home/src/workspaces/project/x.ts", "const a = 1;"},
	                {"/home/src/workspaces/project/tsconfig.json", "{}"},
	            },
	        .edits =
	            {
	                newTscEdit("comment out global declaration",
	                           [](TestSys* sys) {
		                           sys->writeFileNoError(
		                               "/home/src/workspaces/project/x.ts",
		                               "// const a = 1;");
	                           }),
	                newTscEdit("restore global declaration",
	                           [](TestSys* sys) {
		                           sys->writeFileNoError(
		                               "/home/src/workspaces/project/x.ts",
		                               "const a = 1;");
	                           }),
	            },
	    }
	        .run(t, "commandLineWatch");
    });

// === TestTscNoEmitWatch — scenario "noEmit" (tscWatch subfolder) ===

REGISTER_TSCTEST(
    "TestTscNoEmitWatch::syntax errors",
    [](gostd::testing::T* t) {
	    noEmitWatchTestInput("syntax errors", {"-w"},
	                         "const a = \"hello")
	        .run(t, "noEmit");
    });

REGISTER_TSCTEST(
    "TestTscNoEmitWatch::semantic errors",
    [](gostd::testing::T* t) {
	    noEmitWatchTestInput("semantic errors", {"-w"},
	                         "const a: number = \"hello\"")
	        .run(t, "noEmit");
    });

REGISTER_TSCTEST(
    "TestTscNoEmitWatch::dts errors without dts enabled",
    [](gostd::testing::T* t) {
	    noEmitWatchTestInput("dts errors without dts enabled", {"-w"},
	                         "const a = class { private p = 10; };")
	        .run(t, "noEmit");
    });

REGISTER_TSCTEST(
    "TestTscNoEmitWatch::dts errors",
    [](gostd::testing::T* t) {
	    noEmitWatchTestInput("dts errors", {"-w"},
	                         "const a = class { private p = 10; };",
	                         {"\"declaration\": true"})
	        .run(t, "noEmit");
    });

}  // namespace tsc::execute::tsctests::tests
