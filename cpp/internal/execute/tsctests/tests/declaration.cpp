// declaration.cpp — ported tscInput scenarios from
// tsc/internal/execute/tsctests/tsc_test.go: TestTscDeclarationEmit
// (scenario "declarationEmit").

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

// getBuildDeclarationEmitDtsReferenceAsTrippleSlashMap — tsc_test.go:519.
FileMap dtsReferenceAsTrippleSlashMap(bool useNoRef) {
	FileMap files{
	    {"/home/src/workspaces/solution/tsconfig.base.json",
	     Dedent(R"TS(
			{
				"compilerOptions": {
					"rootDir": "./",
					"outDir": "lib",
				},
			})TS")},
	    {"/home/src/workspaces/solution/tsconfig.json",
	     Dedent(R"TS(
			{
				"compilerOptions": { "composite": true },
				"references": [{ "path": "./src" }],
				"include": [],
			})TS")},
	    {"/home/src/workspaces/solution/src/tsconfig.json",
	     Dedent(R"TS(
			{
				"compilerOptions": { "composite": true },
				"references": [{ "path": "./subProject" }, { "path": "./subProject2" }],
				"include": [],
			})TS")},
	    {"/home/src/workspaces/solution/src/subProject/tsconfig.json",
	     Dedent(R"TS(
			{
				"extends": "../../tsconfig.base.json",
				"compilerOptions": { "composite": true },
				"references": [{ "path": "../common" }],
				"include": ["./index.ts"],
			})TS")},
	    {"/home/src/workspaces/solution/src/subProject/index.ts",
	     Dedent(R"TS(
			import { Nominal } from '../common/nominal';
			export type MyNominal = Nominal<string, 'MyNominal'>;)TS")},
	    {"/home/src/workspaces/solution/src/subProject2/tsconfig.json",
	     Dedent(R"TS(
			{
				"extends": "../../tsconfig.base.json",
				"compilerOptions": { "composite": true },
				"references": [{ "path": "../subProject" }],
				"include": ["./index.ts"],
			})TS")},
	    {"/home/src/workspaces/solution/src/subProject2/index.ts",
	     Dedent(R"TS(
			import { MyNominal } from '../subProject/index';
			const variable = {
				key: 'value' as MyNominal,
			};
			export function getVar(): keyof typeof variable {
				return 'key';
			})TS")},
	    {"/home/src/workspaces/solution/src/common/tsconfig.json",
	     Dedent(R"TS(
			{
				"extends": "../../tsconfig.base.json",
				"compilerOptions": { "composite": true },
				"include": ["./nominal.ts"],
			})TS")},
	    {"/home/src/workspaces/solution/src/common/nominal.ts",
	     Dedent(R"TS(
			/// <reference path="./types.d.ts" preserve="true" />
			export declare type Nominal<T, Name extends string> = MyNominal<T, Name>;)TS")},
	    {"/home/src/workspaces/solution/src/common/types.d.ts",
	     Dedent(R"TS(
			declare type MyNominal<T, Name extends string> = T & {
				specialKey: Name;
			};)TS")},
	};
	if (useNoRef) {
		files["/home/src/workspaces/solution/tsconfig.json"] =
		    Dedent(R"TS(
		{
			"extends": "./tsconfig.base.json",
			"compilerOptions": { "composite": true },
			"include": ["./src/**/*.ts"],
		})TS");
	}
	return files;
}

// getTscDeclarationEmitDtsErrorsFileMap — tsc_test.go:590.
FileMap dtsErrorsFileMap(bool composite, bool incremental) {
	return {
	    {"/home/src/workspaces/project/tsconfig.json",
	     Dedent(std::string(R"TS(
			{
				"compilerOptions": {
					"module": "NodeNext",
					"moduleResolution": "NodeNext",
					"composite": )TS") +
	            (composite ? "true" : "false") + ",\n\t\t\t\t\t\"incremental\": " +
	            (incremental ? "true" : "false") + R"TS(,
					"declaration": true,
					"skipLibCheck": true,
					"skipDefaultLibCheck": true,
				},
			})TS")},
	    {"/home/src/workspaces/project/index.ts",
	     Dedent(R"TS(
			import ky from 'ky';
			export const api = ky.extend({});
		)TS")},
	    {"/home/src/workspaces/project/package.json",
	     Dedent(R"TS(
			{
				"type": "module"
			})TS")},
	    {"/home/src/workspaces/project/node_modules/ky/distribution/"
	     "index.d.ts",
	     Dedent(R"TS(
			type KyInstance = {
				extend(options: Record<string,unknown>): KyInstance;
			}
			declare const ky: KyInstance;
			export default ky;
		)TS")},
	    {"/home/src/workspaces/project/node_modules/ky/package.json",
	     Dedent(R"TS(
			{
				"name": "ky",
				"type": "module",
				"main": "./distribution/index.js"
			}
		)TS")},
	};
}

// plugin helpers — tsc_test.go:629-700.
std::string pluginOneConfig() {
	return Dedent(R"TS(
	{
		"compilerOptions": {
			"target": "es5",
			"declaration": true,
			"traceResolution": true,
		},
	})TS");
}

std::string pluginOneIndex() {
	return "import pluginTwo from \"plugin-two\"; // include this to add "
	       "reference to symlink";
}

std::string pluginOneAction() {
	return Dedent(R"TS(
		import { actionCreatorFactory } from "typescript-fsa"; // Include version of shared lib
		const action = actionCreatorFactory("somekey");
		const featureOne = action<{ route: string }>("feature-one");
		export const actions = { featureOne };)TS");
}

std::string pluginTwoDts() {
	return Dedent(R"TS(
		declare const _default: {
			features: {
				featureOne: {
					actions: {
						featureOne: {
							(payload: {
								name: string;
								order: number;
							}, meta?: {
								[key: string]: any;
							}): import("typescript-fsa").Action<{
								name: string;
								order: number;
							}>;
						};
					};
					path: string;
				};
			};
		};
		export default _default;)TS");
}

std::string fsaPackageJson() {
	return Dedent(R"TS(
		{
			"name": "typescript-fsa",
			"version": "3.0.0-beta-2"
		})TS");
}

std::string fsaIndex() {
	return Dedent(R"TS(
		export interface Action<Payload> {
			type: string;
			payload: Payload;
		}
		export declare type ActionCreator<Payload> = {
			type: string;
			(payload: Payload): Action<Payload>;
		}
		export interface ActionCreatorFactory {
			<Payload = void>(type: string): ActionCreator<Payload>;
		}
		export declare function actionCreatorFactory(prefix?: string | null): ActionCreatorFactory;
		export default actionCreatorFactory;)TS");
}

}  // namespace

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::when declaration file is referenced through "
    "triple slash",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when declaration file is referenced through "
	                       "triple slash",
	        .commandLineArgs = commandLineArgsV({"--b", "--verbose"}),
	        .files = dtsReferenceAsTrippleSlashMap(false),
	        .cwd = "/home/src/workspaces/solution",
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::when declaration file is referenced through "
    "triple slash but uses no references",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "when declaration file is referenced through triple slash "
	            "but uses no references",
	        .commandLineArgs = commandLineArgsV({"--b", "--verbose"}),
	        .files = dtsReferenceAsTrippleSlashMap(true),
	        .cwd = "/home/src/workspaces/solution",
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::when ts file is referenced through triple "
    "slash from another project",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when ts file is referenced through triple slash "
	                       "from another project",
	        .commandLineArgs = commandLineArgsV({"--b", "src", "--verbose"}),
	        .files =
	            {
	                {"/home/src/workspaces/solution/include/tsconfig.json",
	                 Dedent(R"TS(
					{
						"compilerOptions": { "composite": true, "declaration": true },
					})TS")},
	                {"/home/src/workspaces/solution/include/include.ts",
	                 Dedent(R"TS(
					export const include = 1;)TS")},
	                {"/home/src/workspaces/solution/src/tsconfig.json",
	                 Dedent(R"TS(
					{
						"compilerOptions": { "composite": true, "declaration": true },
						"references": [{ "path": "../include" }],
					})TS")},
	                {"/home/src/workspaces/solution/src/main.ts",
	                 Dedent(R"TS(
					/// <reference path="../include/include.ts" preserve="true" />
					export const main = 23;)TS")},
	            },
	        .cwd = "/home/src/workspaces/solution",
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::when declaration file used inferred type "
    "from referenced project",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when declaration file used inferred type from "
	                       "referenced project",
	        .commandLineArgs = {"--b", "packages/pkg2/tsconfig.json",
	                            "--verbose"},
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
					{
						"compilerOptions": {
							"composite": true,
							"paths": { "@fluentui/*": ["./packages/*/src"] },
						},
					})TS")},
	                {"/home/src/workspaces/project/packages/pkg1/src/"
	                 "index.ts",
	                 Dedent(R"TS(
					export interface IThing {
						a: string;
					}
					export interface IThings {
						thing1: IThing;
					}
				)TS")},
	                {"/home/src/workspaces/project/packages/pkg1/"
	                 "tsconfig.json",
	                 Dedent(R"TS(
					{
						"extends": "../../tsconfig",
						"compilerOptions": { "outDir": "lib" },
						"include": ["src"],
					}
				)TS")},
	                {"/home/src/workspaces/project/packages/pkg2/src/"
	                 "index.ts",
	                 Dedent(R"TS(
					import { IThings } from '@fluentui/pkg1';
					export function fn4() {
						const a: IThings = { thing1: { a: 'b' } };
						return a.thing1;
					}
				)TS")},
	                {"/home/src/workspaces/project/packages/pkg2/"
	                 "tsconfig.json",
	                 Dedent(R"TS(
					{
						"extends": "../../tsconfig",
						"compilerOptions": { "outDir": "lib" },
						"include": ["src"],
						"references": [{ "path": "../pkg1" }],
					}
				)TS")},
	            },
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::when inferred export should reuse imported "
    "type alias across a module boundary",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when inferred export should reuse imported type "
	                       "alias across a module boundary",
	        .commandLineArgs = commandLineArgsV({"--p", "tsconfig.json"}),
	        .files =
	            {
	                {"/home/src/workspaces/project/tsconfig.json",
	                 Dedent(R"TS(
					{
						"compilerOptions": {
							"strict": true,
							"declaration": true,
							"emitDeclarationOnly": true,
							"target": "es2022",
							"module": "esnext",
						},
						"files": ["./a.ts", "./factory.ts", "./state.ts"],
					})TS")},
	                {std::string(tscLibPath) + "/lib.es2022.full.d.ts",
	                 tscDefaultLibContent + "\n" +
	                     Dedent(R"TS(
					type Partial<T> = {
						[K in keyof T]?: T[K];
					};
				)TS")},
	                {"/home/src/workspaces/project/a.ts",
	                 Dedent(R"TS(
					interface ISettings {
						age: number;
					}

					export type Settings = Partial<ISettings>;
				)TS")},
	                {"/home/src/workspaces/project/factory.ts",
	                 Dedent(R"TS(
					import type { Settings } from "./a";

					export const makeObj = () => ({
						fn: (s?: Settings): Settings | undefined => s,
					});
				)TS")},
	                {"/home/src/workspaces/project/state.ts",
	                 Dedent(R"TS(
					import { makeObj } from "./factory";

					export const obj = makeObj();
				)TS")},
	            },
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::reports dts generation errors",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "reports dts generation errors",
	        .commandLineArgs = commandLineArgsV(
						{"-b", "--explainFiles", "--listEmittedFiles", "--v"}),
	        .files = dtsErrorsFileMap(false, false),
	        .edits = noChangeOnlyEdit,
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::reports dts generation errors with "
    "incremental",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "reports dts generation errors with incremental",
	        .commandLineArgs = commandLineArgsV(
						{"-b", "--explainFiles", "--listEmittedFiles", "--v"}),
	        .files = dtsErrorsFileMap(false, true),
	        .edits = noChangeOnlyEdit,
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::reports dts generation errors [tsc]",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "reports dts generation errors",
	        .commandLineArgs = commandLineArgsV({"--explainFiles", "--listEmittedFiles"}),
	        .files = dtsErrorsFileMap(false, false),
	        .edits =
	            {
	                noChange,
	                new tscEdit{
	                    .caption = "build -b",
	                    .commandLineArgs = commandLineArgsV(
						{"-b", "--explainFiles", "--listEmittedFiles", "--v"}),
	                },
	            },
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::reports dts generation errors with "
    "incremental [tsc]",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "reports dts generation errors with incremental",
	        .commandLineArgs = commandLineArgsV({"--explainFiles", "--listEmittedFiles"}),
	        .files = dtsErrorsFileMap(true, true),
	        .edits =
	            {
	                noChange,
	                new tscEdit{
	                    .caption = "build -b",
	                    .commandLineArgs = commandLineArgsV(
						{"-b", "--explainFiles", "--listEmittedFiles", "--v"}),
	                },
	            },
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::when using Windows paths and uppercase "
    "letters",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario =
	            "when using Windows paths and uppercase letters",
	        .commandLineArgs = commandLineArgsV({"-p", "D:\\Work\\pkg1", "--explainFiles"}),
	        .files =
	            {
	                {"D:/Work/pkg1/package.json",
	                 Dedent(R"TS(
				{
					"name": "ts-specifier-bug",
					"version": "1.0.0",
					"main": "index.js"
				})TS")},
	                {"D:/Work/pkg1/tsconfig.json",
	                 Dedent(R"TS(
				{
					"compilerOptions": {
						"declaration": true,
						"target": "es2017",
						"outDir": "./dist",
					},
					"include": ["src"],
				})TS")},
	                {"D:/Work/pkg1/src/main.ts",
	                 Dedent(R"TS(
					import { PartialType } from './utils';

					class Common {}

					export class Sub extends PartialType(Common) {
						id: string;
					}
				)TS")},
	                {"D:/Work/pkg1/src/utils/index.ts",
	                 Dedent(R"TS(
					import { MyType, MyReturnType } from './type-helpers';

					export function PartialType<T>(classRef: MyType<T>) {
						abstract class PartialClassType {
							constructor() {}
						}

						return PartialClassType as MyReturnType;
					}
				)TS")},
	                {"D:/Work/pkg1/src/utils/type-helpers.ts",
	                 Dedent(R"TS(
					export type MyReturnType = {	
						new (...args: any[]): any;
					};

					export interface MyType<T = any> extends Function {
						new (...args: any[]): T;
					}
				)TS")},
	            },
	        .cwd = "D:/Work/pkg1",
	        .ignoreCase = true,
	        .windowsStyleRoot = "D:/",
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::when same version is referenced through "
    "source and another symlinked package",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when same version is referenced through source "
	                       "and another symlinked package",
	        .commandLineArgs = commandLineArgsV({"-p", "plugin-one", "--explainFiles"}),
	        .files =
	            {
	                {"/user/username/projects/myproject/plugin-two/"
	                 "index.d.ts",
	                 pluginTwoDts()},
	                {"/user/username/projects/myproject/plugin-two/"
	                 "node_modules/typescript-fsa/package.json",
	                 fsaPackageJson()},
	                {"/user/username/projects/myproject/plugin-two/"
	                 "node_modules/typescript-fsa/index.d.ts",
	                 fsaIndex()},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "tsconfig.json",
	                 pluginOneConfig()},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "index.ts",
	                 pluginOneIndex()},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "action.ts",
	                 pluginOneAction()},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "node_modules/typescript-fsa/package.json",
	                 fsaPackageJson()},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "node_modules/typescript-fsa/index.d.ts",
	                 fsaIndex()},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "node_modules/plugin-two",
	                 vfs::vfstest::Symlink(
	                     "/user/username/projects/myproject/plugin-two")},
	            },
	        .cwd = "/user/username/projects/myproject",
	    }
	        .run(t, "declarationEmit");
    });

REGISTER_TSCTEST(
    "TestTscDeclarationEmit::when same version is referenced through "
    "source and another symlinked package with indirect link",
    [](gostd::testing::T* t) {
	    tscInput{
	        .subScenario = "when same version is referenced through source "
	                       "and another symlinked package with indirect "
	                       "link",
	        .commandLineArgs = commandLineArgsV({"-p", "plugin-one", "--explainFiles"}),
	        .files =
	            {
	                {"/user/username/projects/myproject/plugin-two/"
	                 "package.json",
	                 Dedent(R"TS(
				{
					"name": "plugin-two",
					"version": "0.1.3",
					"main": "dist/commonjs/index.js"
				})TS")},
	                {"/user/username/projects/myproject/plugin-two/dist/"
	                 "commonjs/index.d.ts",
	                 pluginTwoDts()},
	                {"/user/username/projects/myproject/plugin-two/"
	                 "node_modules/typescript-fsa/package.json",
	                 fsaPackageJson()},
	                {"/user/username/projects/myproject/plugin-two/"
	                 "node_modules/typescript-fsa/index.d.ts",
	                 fsaIndex()},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "tsconfig.json",
	                 pluginOneConfig()},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "index.ts",
	                 pluginOneIndex() + "\n" + pluginOneAction()},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "node_modules/typescript-fsa/package.json",
	                 fsaPackageJson()},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "node_modules/typescript-fsa/index.d.ts",
	                 fsaIndex()},
	                {"/temp/yarn/data/link/plugin-two",
	                 vfs::vfstest::Symlink(
	                     "/user/username/projects/myproject/plugin-two")},
	                {"/user/username/projects/myproject/plugin-one/"
	                 "node_modules/plugin-two",
	                 vfs::vfstest::Symlink(
	                     "/temp/yarn/data/link/plugin-two")},
	            },
	        .cwd = "/user/username/projects/myproject",
	    }
	        .run(t, "declarationEmit");
    });

}  // namespace tsc::execute::tsctests::tests
