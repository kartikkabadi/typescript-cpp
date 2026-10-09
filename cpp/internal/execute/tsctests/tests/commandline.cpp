// commandline.cpp — ported tscInput scenarios from
// tsc/internal/execute/tsctests/tsc_test.go: TestTscCommandline (partial)
// and TestTscMissingFiles.
//
// Registered name convention: "<GoTestFunc>::<subScenario>".

#include <functional>
#include <string>
#include <vector>

#include "internal/execute/tsctests/tests/registry.h"
#include "internal/execute/tsctests/tests/util.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/gostd/testing.h"

namespace tsc::execute::tsctests::tests {

namespace {

// colorTest — tsc_test.go:17.
tscInput* colorTest(const std::string& subScenario,
                    std::unordered_map<std::string, std::string> env,
                    bool outputIsTTY) {
	return new tscInput{
	    .subScenario = subScenario,
	    .commandLineArgs = {"index.ts", "--noEmit"},
	    .files =
	        {
	            {"/home/src/workspaces/project/index.ts",
	             "const x: string = 1;"},
	        },
	    .env = std::move(env),
	    .outputIsTTY = outputIsTTY,
	};
}

}  // namespace

REGISTER_TSCTEST(
    "TestTscCommandline::global diagnostics produced during ordinary semantic checking",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "global diagnostics produced during ordinary "
	                       "semantic checking",
	        .commandLineArgs = {"index.ts", "--noEmit"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts",
	                 "export function* values() { yield 1; }"},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::show help with "
    "ExitStatus.DiagnosticsPresent_OutputsSkipped",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "show help with "
	                       "ExitStatus.DiagnosticsPresent_OutputsSkipped",
	        .commandLineArgs = {},
	        .env = {{"TS_TEST_TERMINAL_WIDTH", "120"}},
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::show help with "
    "ExitStatus.DiagnosticsPresent_OutputsSkipped when host cannot provide "
    "terminal width",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "show help with "
	            "ExitStatus.DiagnosticsPresent_OutputsSkipped when host "
	            "cannot provide terminal width",
	        .commandLineArgs = {},
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::does not add color when NO_COLOR is set",
    [](gostd::testing::T* t) {
	    colorTest("does not add color when NO_COLOR is set",
	              {{"NO_COLOR", "true"}}, true)
	        ->run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::adds color when NO_COLOR is empty",
    [](gostd::testing::T* t) {
	    colorTest("adds color when NO_COLOR is empty", {{"NO_COLOR", ""}},
	              true)
	        ->run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::does not add color when TERM is dumb",
    [](gostd::testing::T* t) {
	    colorTest("does not add color when TERM is dumb", {{"TERM", "dumb"}},
	              true)
	        ->run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::FORCE_COLOR overrides NO_COLOR",
    [](gostd::testing::T* t) {
	    colorTest("FORCE_COLOR overrides NO_COLOR",
	              {{"NO_COLOR", "true"}, {"FORCE_COLOR", "true"}}, false)
	        ->run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::when build not first argument",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when build not first argument",
	        .commandLineArgs = {"--verbose", "--build"},
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::malformed tsconfig property without value",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "malformed tsconfig property without value",
	        .commandLineArgs = {},
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json", "{\"\" }"},
	                {"/home/src/workspaces/project/index.ts", ""},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Initialized TSConfig with files options",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Initialized TSConfig with files options",
	        .commandLineArgs = {"--init", "file0.st", "file1.ts", "file2.ts"},
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Initialized TSConfig with boolean value compiler "
    "options",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Initialized TSConfig with boolean value compiler "
	                       "options",
	        .commandLineArgs = {"--init", "--noUnusedLocals"},
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Initialized TSConfig with enum value compiler "
    "options",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Initialized TSConfig with enum value compiler "
	                       "options",
	        .commandLineArgs = {"--init", "--target", "es5", "--jsx", "react"},
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Initialized TSConfig with tsconfig.json",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Initialized TSConfig with tsconfig.json",
	        .commandLineArgs = {"--init"},
	        .files =
	            {
	                {"/home/src/workspaces/project/first.ts",
	                 "export const a = 1"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": {
						"strict": true,
						"noEmit": true
					}
				})TS")},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST("TestTscCommandline::help", [](gostd::testing::T* t) {
	tscInput{
	    .subScenario = "help",
	    .commandLineArgs = {"--help"},
	}
	    .run(t, "commandLine");
});

REGISTER_TSCTEST("TestTscCommandline::help all", [](gostd::testing::T* t) {
	tscInput{
	    .subScenario = "help all",
	    .commandLineArgs = {"--help", "--all"},
	}
	    .run(t, "commandLine");
});

REGISTER_TSCTEST(
    "TestTscCommandline::Parse --lib option with file name",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Parse --lib option with file name",
	        .commandLineArgs = {"--lib", "es6 ", "first.ts"},
	        .files =
	            {
	                {"/home/src/workspaces/project/first.ts",
	                 "export const Key = Symbol()"},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::noEmit with type error",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "noEmit with type error",
	        .commandLineArgs = {"--noEmit", "index.ts"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts", "x = 5;"},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::option diagnostics are suppressed when there are "
    "syntactic errors",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "option diagnostics are suppressed when there are "
	                       "syntactic errors",
	        .commandLineArgs = {"--strictPropertyInitialization",
	                            "--strictNullChecks", "false", "a.ts"},
	        .files =
	            {
	                {"/home/src/workspaces/project/a.ts", "const x: = 1;"},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::non-object config root",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "non-object config root",
	        .commandLineArgs = {},
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json", "[]"},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Project is empty string",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Project is empty string",
	        .commandLineArgs = {},
	        .files =
	            {
	                {"/home/src/workspaces/project/first.ts",
	                 "export const a = 1"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": {
						"strict": true,
						"noEmit": true
					}
				})TS")},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST("TestTscCommandline::Parse -p", [](gostd::testing::T* t) {
	tscInput{
	    .subScenario = "Parse -p",
	    .commandLineArgs = {"-p", "."},
	    .files =
	        {
	            {"/home/src/workspaces/project/first.ts",
	             "export const a = 1"},
	            {"/home/src/workspaces/project/tsconfig.json",
	             Dedent(R"TS(
				{
					"compilerOptions": {
						"strict": true,
						"noEmit": true
					}
				})TS")},
	        },
	}
	    .run(t, "commandLine");
});

REGISTER_TSCTEST(
    "TestTscCommandline::Parse -p with path to tsconfig file",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Parse -p with path to tsconfig file",
	        .commandLineArgs =
	            {"-p", "/home/src/workspaces/project/tsconfig.json"},
	        .files =
	            {
	                {"/home/src/workspaces/project/first.ts",
	                 "export const a = 1"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": {
						"strict": true,
						"noEmit": true
					}
				})TS")},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Parse -p with empty tsconfig file",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Parse -p with empty tsconfig file",
	        .commandLineArgs = {"-p", "."},
	        .files =
	            {
	                {"/home/src/workspaces/project/first.ts",
	                 "export const a = 1"},
	                {"/home/src/workspaces/project/tsconfig.json", ""},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Parse -p with path to tsconfig folder",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Parse -p with path to tsconfig folder",
	        .commandLineArgs = {"-p", "/home/src/workspaces/project"},
	        .files =
	            {
	                {"/home/src/workspaces/project/first.ts",
	                 "export const a = 1"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": {
						"strict": true,
						"noEmit": true
					}
				})TS")},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Reject removed watch interval option",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Reject removed watch interval option",
	        .commandLineArgs = {"-w", "--watchInterval", "1000"},
	        .files =
	            {
	                {"/home/src/workspaces/project/first.ts",
	                 "export const a = 1"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": {
						"strict": true,
						"noEmit": true
					}
				})TS")},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Reject removed watch interval option without "
    "tsconfig.json",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Reject removed watch interval option without "
	                       "tsconfig.json",
	        .commandLineArgs = {"-w", "--watchInterval", "1000"},
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Config with references and empty file and refers "
    "to config with noEmit",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Config with references and empty file and refers "
	                       "to config with noEmit",
	        .commandLineArgs = {"-p", "."},
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS({
					"files": [],
					"references": [
						{
							"path": "./packages/pkg1"
						},
					],
				})TS")},
	                {"/home/src/workspaces/project/packages/pkg1/"
	                 "tsconfig.json",
	                 Dedent(R"TS({
					"compilerOptions": {
						"composite": true,
						"noEmit": true
					},
					"files": [
						"./index.ts",
					],
				})TS")},
	                {"/home/src/workspaces/project/packages/pkg1/index.ts",
	                 "export const a = 1;"},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::compiler option at top level of tsconfig",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "compiler option at top level of tsconfig",
	        .commandLineArgs = {"--pretty", "false"},
	        .files =
	            {
	                {"/home/src/workspaces/project/index.ts", ""},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 "{ \"strict\": true }"},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::Parse enum type options",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Parse enum type options",
	        .commandLineArgs = {"--moduleResolution", "nodenext ", "first.ts",
	                            "--module",          "nodenext", "--target",
	                            "esnext",            "--moduleDetection",
	                            "auto",              "--jsx",      "react",
	                            "--newLine",         "crlf"},
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::locale",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "locale",
	        .commandLineArgs = {"--locale", "cs", "--version"},
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscCommandline::bad locale",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "bad locale",
	        .commandLineArgs = {"--locale", "whoops", "--version"},
	    }
	        .run(t, "commandLine");
    });

// === TestTscMissingFiles — tsc_test.go:290 (scenario "commandLine") ===

REGISTER_TSCTEST(
    "TestTscMissingFiles::file in tsconfig does not exist",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "file in tsconfig does not exist",
	        .commandLineArgs = {"-p", "./tsconfig.json"},
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS({
					"files": ["./src/doesNotExist.ts"]
					})TS")},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscMissingFiles::extensionless file in tsconfig does not exist",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "extensionless file in tsconfig does not exist",
	        .commandLineArgs = {"-p", "./tsconfig.json"},
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS({
					"files": ["./src/doesNotExist"]
					})TS")},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscMissingFiles::extensionless file in tsconfig exists",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "extensionless file in tsconfig exists",
	        .commandLineArgs = {"-p", "./tsconfig.json"},
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS({
					"files": ["./src/script"]
					})TS")},
	                {"/home/src/workspaces/project/src/script",
	                 "const n: number = \"s\";"},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscMissingFiles::extensionless file on command line exists",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "extensionless file on command line exists",
	        .commandLineArgs = {"script"},
	        .files =
	            {
	                {"/home/src/workspaces/project/script",
	                 "const n: number = \"s\";"},
	            },
	    }
	        .run(t, "commandLine");
    });

REGISTER_TSCTEST(
    "TestTscMissingFiles::extensionless file in extended tsconfig in "
    "different folder does not exist",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "extensionless file in extended tsconfig in different folder "
	            "does not exist",
	        .commandLineArgs = {"-p", "./src/tsconfig.json"},
	        .files =
	            {
	                {"/home/src/workspaces/project/src/tsconfig.json",
	                 Dedent(R"TS({
					"extends": "./../tsconfig.base.json",
					})TS")},
	                {"/home/src/workspaces/project/src/oops.ts",
	                 "export const abc = 10;"},
	                {"/home/src/workspaces/project/tsconfig.base.json",
	                 Dedent(R"TS({
					"files": ["./oops"],
					})TS")},
	            },
	    }
	        .run(t, "commandLine");
    });

}  // namespace tsc::execute::tsctests::tests
