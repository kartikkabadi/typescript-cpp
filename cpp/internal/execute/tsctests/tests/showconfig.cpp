// showconfig.cpp — ported tscInput scenarios from
// tsc/internal/execute/tsctests/showconfig_test.go (TestShowConfig).
// Registered in the tsctestrunner (baseline-driven), not unittestrunner.

#include "internal/execute/tsctests/tests/registry.h"
#include "internal/execute/tsctests/tests/util.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/gostd/testing.h"

namespace tsc::execute::tsctests::tests {

REGISTER_TSCTEST("TestShowConfig::Default initialized TSConfig",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario = "Default initialized TSConfig",
	                     .commandLineArgs = {"--showConfig"},
	                 }
	                     .run(t, "showConfig");
                 });

REGISTER_TSCTEST("TestShowConfig::Show TSConfig with files options",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario = "Show TSConfig with files options",
	                     .commandLineArgs = {"--showConfig", "file0.ts",
	                                         "file1.ts", "file2.ts"},
	                 }
	                     .run(t, "showConfig");
                 });

REGISTER_TSCTEST(
    "TestShowConfig::Show TSConfig with boolean value compiler options",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "Show TSConfig with boolean value compiler options",
	        .commandLineArgs = {"--showConfig", "--noUnusedLocals"},
	    }
	        .run(t, "showConfig");
    });

REGISTER_TSCTEST(
    "TestShowConfig::Show TSConfig with enum value compiler options",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Show TSConfig with enum value compiler options",
	        .commandLineArgs = {"--showConfig", "--target", "es5", "--jsx",
	                            "react"},
	    }
	        .run(t, "showConfig");
    });

REGISTER_TSCTEST("TestShowConfig::Show TSConfig with list compiler options",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario =
	                         "Show TSConfig with list compiler options",
	                     .commandLineArgs = {"--showConfig", "--types",
	                                         "jquery,mocha"},
	                 }
	                     .run(t, "showConfig");
                 });

REGISTER_TSCTEST(
    "TestShowConfig::Show TSConfig with list compiler options with enum "
    "value",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Show TSConfig with list compiler options with "
	                       "enum value",
	        .commandLineArgs = {"--showConfig", "--lib",
	                            "es5,es2015.core"},
	    }
	        .run(t, "showConfig");
    });

REGISTER_TSCTEST("TestShowConfig::Show TSConfig with incorrect compiler "
                 "option",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario =
	                         "Show TSConfig with incorrect compiler option",
	                     .commandLineArgs = {"--showConfig",
	                                         "--someNonExistOption"},
	                 }
	                     .run(t, "showConfig");
                 });

REGISTER_TSCTEST(
    "TestShowConfig::Show TSConfig with incorrect compiler option value",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Show TSConfig with incorrect compiler option "
	                       "value",
	        .commandLineArgs = {"--showConfig", "--lib",
	                            "nonExistLib,es5,es2015.promise"},
	    }
	        .run(t, "showConfig");
    });

REGISTER_TSCTEST("TestShowConfig::Show TSConfig with advanced options",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario = "Show TSConfig with advanced options",
	                     .commandLineArgs = {"--showConfig", "--declaration",
	                                         "--declarationDir", "lib",
	                                         "--skipLibCheck",
	                                         "--noErrorTruncation"},
	                 }
	                     .run(t, "showConfig");
                 });

REGISTER_TSCTEST("TestShowConfig::Show TSConfig with compileOnSave and more",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario =
	                         "Show TSConfig with compileOnSave and more",
	                     .commandLineArgs = {"-p", "tsconfig.json",
	                                         "--showConfig"},
	                     .files =
	                         {
	                             {"/home/src/workspaces/project/src/"
	                              "index.ts",
	                              "export const a = 1;"},
	                             {"/home/src/workspaces/project/"
	                              "tsconfig.json",
	                              Dedent(R"(
								{
									"compilerOptions": {
										"esModuleInterop": true,
										"target": "es5",
										"module": "commonjs",
										"strict": true
									},
									"compileOnSave": true,
									"exclude": [
										"dist"
									],
									"files": [],
									"include": [
										"src/*"
									],
									"references": [
										{ "path": "./test" }
									]
								})")},
	                         },
	                 }
	                     .run(t, "showConfig");
                 });

REGISTER_TSCTEST("TestShowConfig::Show TSConfig with paths and more",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario = "Show TSConfig with paths and more",
	                     .commandLineArgs = {"-p", "tsconfig.json",
	                                         "--showConfig"},
	                     .files =
	                         {
	                             {"/home/src/workspaces/project/src/"
	                              "index.ts",
	                              "export const a = 1;"},
	                             {"/home/src/workspaces/project/"
	                              "tsconfig.json",
	                              Dedent(R"(
								{
									"compilerOptions": {
										"allowJs": true,
										"outDir": "./lib",
										"esModuleInterop": true,
										"module": "commonjs",
										"moduleResolution": "node",
										"target": "ES2017",
										"sourceMap": true,
										"baseUrl": ".",
										"paths": {
											"@root/*": ["./*"],
											"@configs/*": ["src/configs/*"],
											"@common/*": ["src/common/*"],
											"*": [
												"node_modules/*",
												"src/types/*"
											]
										},
										"experimentalDecorators": true,
										"emitDecoratorMetadata": true,
										"resolveJsonModule": true
									},
									"include": [
										"./src/**/*"
									]
								})")},
	                         },
	                 }
	                     .run(t, "showConfig");
                 });

REGISTER_TSCTEST(
    "TestShowConfig::Show TSConfig with include filtering files",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "Show TSConfig with include filtering files",
	        .commandLineArgs = {"-p", "tsconfig.json", "--showConfig"},
	        .files =
	            {
	                {"/home/src/workspaces/project/src/main.ts",
	                 "export const a = 1;"},
	                {"/home/src/workspaces/project/src/util.ts",
	                 "export const b = 2;"},
	                {"/home/src/workspaces/project/extra.ts",
	                 "export const c = 3;"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"(
					{
						"compilerOptions": {
							"strict": true
						},
						"include": [
							"src/**/*"
						]
					})")},
	            },
	    }
	        .run(t, "showConfig");
    });

REGISTER_TSCTEST("TestShowConfig::Show TSConfig with references",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario = "Show TSConfig with references",
	                     .commandLineArgs = {"-p", "tsconfig.json",
	                                         "--showConfig"},
	                     .files =
	                         {
	                             {"/home/src/workspaces/project/src/"
	                              "index.ts",
	                              "export const a = 1;"},
	                             {"/home/src/workspaces/project/"
	                              "tsconfig.json",
	                              Dedent(R"(
								{
									"compilerOptions": {
										"composite": true,
										"strict": true
									},
									"references": [
										{ "path": "./packages/a" },
										{ "path": "./packages/b" }
									]
								})")},
	                         },
	                 }
	                     .run(t, "showConfig");
                 });

REGISTER_TSCTEST("TestShowConfig::Show TSConfig with exclude",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario = "Show TSConfig with exclude",
	                     .commandLineArgs = {"-p", "tsconfig.json",
	                                         "--showConfig"},
	                     .files =
	                         {
	                             {"/home/src/workspaces/project/src/"
	                              "index.ts",
	                              "export const a = 1;"},
	                             {"/home/src/workspaces/project/test/"
	                              "test1.ts",
	                              "import { a } from \"../src\";"},
	                             {"/home/src/workspaces/project/"
	                              "tsconfig.json",
	                              Dedent(R"(
								{
									"compilerOptions": {
										"strict": true
									},
									"exclude": [
										"test"
									]
								})")},
	                         },
	                 }
	                     .run(t, "showConfig");
                 });

REGISTER_TSCTEST("TestShowConfig::Show TSConfig with files and include",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario = "Show TSConfig with files and include",
	                     .commandLineArgs = {"-p", "tsconfig.json",
	                                         "--showConfig"},
	                     .files =
	                         {
	                             {"/home/src/workspaces/project/src/"
	                              "main.ts",
	                              "export const a = 1;"},
	                             {"/home/src/workspaces/project/extra.ts",
	                              "export const c = 3;"},
	                             {"/home/src/workspaces/project/"
	                              "tsconfig.json",
	                              Dedent(R"(
								{
									"compilerOptions": {
										"strict": true
									},
									"files": [
										"extra.ts"
									],
									"include": [
										"src/**/*"
									]
								})")},
	                         },
	                 }
	                     .run(t, "showConfig");
                 });

REGISTER_TSCTEST(
    "TestShowConfig::Show TSConfig with transitively implied options",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "Show TSConfig with transitively implied options",
	        .commandLineArgs = {"-p", "tsconfig.json", "--showConfig"},
	        .files =
	            {
	                {"/home/src/workspaces/project/src/index.ts",
	                 "export const a = 1;"},
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"(
					{
						"compilerOptions": {
							"module": "nodenext"
						}
					})")},
	            },
	    }
	        .run(t, "showConfig");
    });

REGISTER_TSCTEST("TestShowConfig::Show TSConfig with exclude and outDir",
                 [](gostd::testing::T* t) {
	                 tscInput{
	                     .subScenario =
	                         "Show TSConfig with exclude and outDir",
	                     .commandLineArgs = {"-p", "tsconfig.json",
	                                         "--showConfig"},
	                     .files =
	                         {
	                             {"/home/src/workspaces/project/src/"
	                              "index.ts",
	                              "export const a = 1;"},
	                             {"/home/src/workspaces/project/src/bin/"
	                              "tool.ts",
	                              "export const b = 2;"},
	                             {"/home/src/workspaces/project/"
	                              "tsconfig.json",
	                              Dedent(R"(
								{
									"compilerOptions": {
										"strict": true,
										"outDir": "./build"
									},
									"exclude": [
										"build"
									]
								})")},
	                         },
	                 }
	                     .run(t, "showConfig");
                 });

} // namespace tsc::execute::tsctests::tests
