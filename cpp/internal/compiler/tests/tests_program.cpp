// Port of tsc/internal/compiler/program_test.go.
// BenchmarkNewProgram is not ported — it does not run under `go test`.
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/bundled/bundled.h"
#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
using namespace tsc;

struct testFile {
	std::string fileName;
	std::string contents;
};

struct programTest {
	std::string testName;
	std::vector<testFile> files;
	std::vector<std::string> expectedFiles;
	ScriptTarget target;
};

static const std::vector<std::string> esnextLibs = {
	"lib.es5.d.ts",
	"lib.es2015.d.ts",
	"lib.es2016.d.ts",
	"lib.es2017.d.ts",
	"lib.es2018.d.ts",
	"lib.es2019.d.ts",
	"lib.es2020.d.ts",
	"lib.es2021.d.ts",
	"lib.es2022.d.ts",
	"lib.es2023.d.ts",
	"lib.es2024.d.ts",
	"lib.es2025.d.ts",
	"lib.es2026.d.ts",
	"lib.esnext.d.ts",
	"lib.dom.d.ts",
	"lib.dom.iterable.d.ts",
	"lib.dom.asynciterable.d.ts",
	"lib.webworker.importscripts.d.ts",
	"lib.scripthost.d.ts",
	"lib.es2015.core.d.ts",
	"lib.es2015.collection.d.ts",
	"lib.es2015.generator.d.ts",
	"lib.es2015.iterable.d.ts",
	"lib.es2015.promise.d.ts",
	"lib.es2015.proxy.d.ts",
	"lib.es2015.reflect.d.ts",
	"lib.es2015.symbol.d.ts",
	"lib.es2015.symbol.wellknown.d.ts",
	"lib.es2016.array.include.d.ts",
	"lib.es2016.intl.d.ts",
	"lib.es2017.arraybuffer.d.ts",
	"lib.es2017.date.d.ts",
	"lib.es2017.object.d.ts",
	"lib.es2017.sharedmemory.d.ts",
	"lib.es2017.string.d.ts",
	"lib.es2017.intl.d.ts",
	"lib.es2017.typedarrays.d.ts",
	"lib.es2018.asyncgenerator.d.ts",
	"lib.es2018.asynciterable.d.ts",
	"lib.es2018.intl.d.ts",
	"lib.es2018.promise.d.ts",
	"lib.es2018.regexp.d.ts",
	"lib.es2019.array.d.ts",
	"lib.es2019.object.d.ts",
	"lib.es2019.string.d.ts",
	"lib.es2019.symbol.d.ts",
	"lib.es2019.intl.d.ts",
	"lib.es2020.bigint.d.ts",
	"lib.es2020.date.d.ts",
	"lib.es2020.promise.d.ts",
	"lib.es2020.sharedmemory.d.ts",
	"lib.es2020.string.d.ts",
	"lib.es2020.symbol.wellknown.d.ts",
	"lib.es2020.intl.d.ts",
	"lib.es2020.number.d.ts",
	"lib.es2021.promise.d.ts",
	"lib.es2021.string.d.ts",
	"lib.es2021.weakref.d.ts",
	"lib.es2021.intl.d.ts",
	"lib.es2022.array.d.ts",
	"lib.es2022.error.d.ts",
	"lib.es2022.intl.d.ts",
	"lib.es2022.object.d.ts",
	"lib.es2022.string.d.ts",
	"lib.es2022.regexp.d.ts",
	"lib.es2023.array.d.ts",
	"lib.es2023.collection.d.ts",
	"lib.es2023.intl.d.ts",
	"lib.es2024.arraybuffer.d.ts",
	"lib.es2024.collection.d.ts",
	"lib.es2024.object.d.ts",
	"lib.es2024.promise.d.ts",
	"lib.es2024.regexp.d.ts",
	"lib.es2024.sharedmemory.d.ts",
	"lib.es2024.string.d.ts",
	"lib.es2025.collection.d.ts",
	"lib.es2025.float16.d.ts",
	"lib.es2025.intl.d.ts",
	"lib.es2025.iterator.d.ts",
	"lib.es2025.promise.d.ts",
	"lib.es2025.regexp.d.ts",
	"lib.es2026.array.d.ts",
	"lib.es2026.collection.d.ts",
	"lib.es2026.error.d.ts",
	"lib.es2026.iterator.d.ts",
	"lib.es2026.json.d.ts",
	"lib.es2026.math.d.ts",
	"lib.es2026.typedarrays.d.ts",
	"lib.esnext.iterator.d.ts",
	"lib.esnext.promise.d.ts",
	"lib.esnext.date.d.ts",
	"lib.esnext.decorators.d.ts",
	"lib.esnext.disposable.d.ts",
	"lib.esnext.intl.d.ts",
	"lib.esnext.modulesource.d.ts",
	"lib.esnext.sharedmemory.d.ts",
	"lib.esnext.temporal.d.ts",
	"lib.decorators.d.ts",
	"lib.decorators.legacy.d.ts",
	"lib.esnext.full.d.ts",};

static const std::vector<programTest> programTestCases = {
	{"BasicFileOrdering", {
		{"c:/dev/src/index.ts", "/// <reference path='c:/dev/src2/a/5.ts' />\n/// <reference path='c:/dev/src2/a/10.ts' />"},
		{"c:/dev/src2/a/5.ts", "/// <reference path='4.ts' />"},
		{"c:/dev/src2/a/4.ts", "/// <reference path='b/3.ts' />"},
		{"c:/dev/src2/a/b/3.ts", "/// <reference path='2.ts' />"},
		{"c:/dev/src2/a/b/2.ts", "/// <reference path='c/1.ts' />"},
		{"c:/dev/src2/a/b/c/1.ts", "console.log('hello');"},
		{"c:/dev/src2/a/10.ts", "/// <reference path='b/c/d/9.ts' />"},
		{"c:/dev/src2/a/b/c/d/9.ts", "/// <reference path='e/8.ts' />"},
		{"c:/dev/src2/a/b/c/d/e/8.ts", "/// <reference path='7.ts' />"},
		{"c:/dev/src2/a/b/c/d/e/7.ts", "/// <reference path='f/6.ts' />"},
		{"c:/dev/src2/a/b/c/d/e/f/6.ts", "console.log('world!');"},
	}, {
		"c:/dev/src2/a/b/c/1.ts",
		"c:/dev/src2/a/b/2.ts",
		"c:/dev/src2/a/b/3.ts",
		"c:/dev/src2/a/4.ts",
		"c:/dev/src2/a/5.ts",
		"c:/dev/src2/a/b/c/d/e/f/6.ts",
		"c:/dev/src2/a/b/c/d/e/7.ts",
		"c:/dev/src2/a/b/c/d/e/8.ts",
		"c:/dev/src2/a/b/c/d/9.ts",
		"c:/dev/src2/a/10.ts",
		"c:/dev/src/index.ts",
	}, ScriptTarget::ESNext},
	{"FileOrderingImports", {
		{"c:/dev/src/index.ts", "import * as five from '../src2/a/5.ts';\nimport * as ten from '../src2/a/10.ts';"},
		{"c:/dev/src2/a/5.ts", "import * as four from './4.ts';"},
		{"c:/dev/src2/a/4.ts", "import * as three from './b/3.ts';"},
		{"c:/dev/src2/a/b/3.ts", "import * as two from './2.ts';"},
		{"c:/dev/src2/a/b/2.ts", "import * as one from './c/1.ts';"},
		{"c:/dev/src2/a/b/c/1.ts", "console.log('hello');"},
		{"c:/dev/src2/a/10.ts", "import * as nine from './b/c/d/9.ts';"},
		{"c:/dev/src2/a/b/c/d/9.ts", "import * as eight from './e/8.ts';"},
		{"c:/dev/src2/a/b/c/d/e/8.ts", "import * as seven from './7.ts';"},
		{"c:/dev/src2/a/b/c/d/e/7.ts", "import * as six from './f/6.ts';"},
		{"c:/dev/src2/a/b/c/d/e/f/6.ts", "console.log('world!');"},
	}, {
		"c:/dev/src2/a/b/c/1.ts",
		"c:/dev/src2/a/b/2.ts",
		"c:/dev/src2/a/b/3.ts",
		"c:/dev/src2/a/4.ts",
		"c:/dev/src2/a/5.ts",
		"c:/dev/src2/a/b/c/d/e/f/6.ts",
		"c:/dev/src2/a/b/c/d/e/7.ts",
		"c:/dev/src2/a/b/c/d/e/8.ts",
		"c:/dev/src2/a/b/c/d/9.ts",
		"c:/dev/src2/a/10.ts",
		"c:/dev/src/index.ts",
	}, ScriptTarget::ESNext},
	{"FileOrderingCycles", {
		{"c:/dev/src/index.ts", "import * as five from '../src2/a/5.ts';\nimport * as ten from '../src2/a/10.ts';"},
		{"c:/dev/src2/a/5.ts", "import * as four from './4.ts';"},
		{"c:/dev/src2/a/4.ts", "import * as three from './b/3.ts';"},
		{"c:/dev/src2/a/b/3.ts", "import * as two from './2.ts';\nimport * as cycle from 'c:/dev/src/index.ts'; "},
		{"c:/dev/src2/a/b/2.ts", "import * as one from './c/1.ts';"},
		{"c:/dev/src2/a/b/c/1.ts", "console.log('hello');"},
		{"c:/dev/src2/a/10.ts", "import * as nine from './b/c/d/9.ts';"},
		{"c:/dev/src2/a/b/c/d/9.ts", "import * as eight from './e/8.ts';\nimport * as cycle from 'c:/dev/src/index.ts';"},
		{"c:/dev/src2/a/b/c/d/e/8.ts", "import * as seven from './7.ts';"},
		{"c:/dev/src2/a/b/c/d/e/7.ts", "import * as six from './f/6.ts';"},
		{"c:/dev/src2/a/b/c/d/e/f/6.ts", "console.log('world!');"},
	}, {
		"c:/dev/src2/a/b/c/1.ts",
		"c:/dev/src2/a/b/2.ts",
		"c:/dev/src2/a/b/3.ts",
		"c:/dev/src2/a/4.ts",
		"c:/dev/src2/a/5.ts",
		"c:/dev/src2/a/b/c/d/e/f/6.ts",
		"c:/dev/src2/a/b/c/d/e/7.ts",
		"c:/dev/src2/a/b/c/d/e/8.ts",
		"c:/dev/src2/a/b/c/d/9.ts",
		"c:/dev/src2/a/10.ts",
		"c:/dev/src/index.ts",
	}, ScriptTarget::ESNext},
};

static void TestProgram(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		// Without embedding, we'd need to read all of the lib files out from disk into the MapFS.
		// Just skip this for now.
		t->Skip({"bundled files are not embedded"});
	}

	for (auto& testCase : programTestCases) {
		t->Run(testCase.testName, [&testCase](T* t) {
			t->Parallel();
			std::string libPrefix = bundled::LibPath() + "/";
			std::shared_ptr<vfs::FS> fs =
				vfs::vfstest::FromMap({}, false /*useCaseSensitiveFileNames*/);
			fs = bundled::WrapFS(fs);

			for (auto& testFile : testCase.files) {
				(void)fs->WriteFile(testFile.fileName, testFile.contents);
			}

			CompilerOptions opts;
			opts.Target = testCase.target;

			tsoptions::ParsedOptions parsedOpts;
			parsedOpts.FileNames = {"c:/dev/src/index.ts"};
			parsedOpts.CompilerOptions = &opts;
			tsoptions::ParsedCommandLine config;
			config.ParsedConfig = &parsedOpts;

			compiler::ProgramOptions programOpts;
			programOpts.Config = &config;
			programOpts.Host = compiler::NewCompilerHost(
				"c:/dev/src", fs, bundled::LibPath(), nullptr, nullptr, nullptr);

			auto* program = compiler::NewProgram(programOpts);

			std::vector<std::string> actualFiles;
			for (auto* file : program->GetSourceFiles()) {
				std::string name = file->FileName();
				if (name.starts_with(libPrefix)) {
					name = name.substr(libPrefix.size());
				}
				actualFiles.push_back(name);
			}

			// Go: expectedFiles = slices.Concat(esnextLibs, [...])
			std::vector<std::string> expectedFiles = esnextLibs;
			expectedFiles.insert(expectedFiles.end(),
								 testCase.expectedFiles.begin(),
								 testCase.expectedFiles.end());
			gotest::assert::Equal(t, actualFiles, expectedFiles);
		});
	}
}

static void TestIncludeProcessorDiagnosticsWithMissingFileCasing(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	// Use case-sensitive file names so that /src/MyFile.ts and /src/myFile.ts
	// have different canonical paths but the same lower-case path, triggering
	// file casing diagnostics in the include processor.
	std::shared_ptr<vfs::FS> fs =
		vfs::vfstest::FromMap({}, true /*useCaseSensitiveFileNames*/);
	fs = bundled::WrapFS(fs);

	// Only create the lowercase version; /src/MyFile.ts does not exist.
	(void)fs->WriteFile("/src/myFile.ts", "export const y = 2;");

	CompilerOptions opts;
	opts.SkipDefaultLibCheck = Tristate::True;

	// List both casings as root files. The first one (/src/MyFile.ts) will fail
	// to load because it does not exist on the case-sensitive filesystem.
	tsoptions::ParsedOptions parsedOpts;
	parsedOpts.FileNames = {"/src/MyFile.ts", "/src/myFile.ts"};
	parsedOpts.CompilerOptions = &opts;
	tsoptions::ParsedCommandLine config;
	config.ParsedConfig = &parsedOpts;

	compiler::ProgramOptions programOpts;
	programOpts.Config = &config;
	programOpts.Host = compiler::NewCompilerHost("/", fs, bundled::LibPath(),
												 nullptr, nullptr, nullptr);

	auto* program = compiler::NewProgram(programOpts);

	// GetProgramDiagnostics triggers getDiagnostics which processes all
	// include processor diagnostics including the casing diagnostic whose
	// file path points to the missing /src/MyFile.ts. Before the fix this
	// panicked with a nil pointer dereference.
	try {
		program->GetProgramDiagnostics();
	} catch (const std::exception& e) {
		t->Fatalf("panic: %s", {std::string(e.what())});
	} catch (...) {
		t->Fatalf("panic", {});
	}
}

REGISTER_UNIT_TEST("compiler.TestProgram", TestProgram);
REGISTER_UNIT_TEST("compiler.TestIncludeProcessorDiagnosticsWithMissingFileCasing",
				   TestIncludeProcessorDiagnosticsWithMissingFileCasing);

static void TestImportSourceProgram(T* t) {
	t->Parallel();
	struct test {
		std::string name;
		std::string source;
		std::string evaluation;
	};
	std::vector<test> tests = {
	    {"static", "import source a from \"./a.js\";",
	     "import { a as value } from \"./a.js\";"},
	    {"dynamic", "import.source(\"./a.js\");", "import(\"./a.js\");"},
	};
	for (auto& test : tests) {
		t->Run(test.name, [&test](T* t) {
			t->Parallel();
			std::string content =
			    test.source +
			    "import source b from \"missing\"; import.source(\"other\");";
			std::unordered_map<std::string, vfs::vfstest::MapFileInput> files = {
			    {"/src/tsconfig.json",
			     "{\"compilerOptions\":{\"module\":\"esnext\",\"noLib\":true},"
			     "\"files\":[\"index.ts\"]}"},
			    {"/src/index.ts", content},
			    {"/src/a.ts", "export const a = 1;"},
			};
			compiler::CompilerHost* host = compiler::NewCompilerHost(
			    "/", vfs::vfstest::FromMap(files, true), "", nullptr,
			    nullptr, nullptr);
			auto [config, diagnostics] =
			    tsoptions::GetParsedCommandLineOfConfigFile(
			        "/src/tsconfig.json", nullptr, nullptr, host, nullptr);
			gotest::assert::Equal(t, diagnostics.size(), size_t(0));
			compiler::ProgramOptions programOpts;
			programOpts.Config = config;
			programOpts.Host = host;
			auto* program = compiler::NewProgram(programOpts);
			[[maybe_unused]] auto* file = program->GetSourceFile("/src/index.ts");
			gotest::assert::Assert(t, program->GetSourceFile("/src/a.ts") ==
			                    nullptr);
			gotest::assert::Equal(t, program->GetResolvedModules().size(),
			              size_t(0));
			gotest::assert::Equal(t, program->GetUnresolvedImports()->Len(),
			              size_t(0));
			gotest::assert::Equal(t,
			              program->collectPackageNames()->unresolved.Len(),
			              size_t(0));

			std::string evaluation = test.evaluation +
				content.substr(test.source.size());
			files["/src/index.ts"] = evaluation;
			host = compiler::NewCompilerHost(
			    "/", vfs::vfstest::FromMap(files, true), "", nullptr,
			    nullptr, nullptr);
			auto [program2, file2, reused] = program->UpdateProgram(
			    "/src/index.ts", host, nullptr, nullptr);
			gotest::assert::Assert(t, !reused);
			program = program2;
			gotest::assert::Assert(t, program->GetSourceFile("/src/a.ts") !=
			                    nullptr);
			for (auto* specifier : file2->imports) {
				auto* resolved =
				    program->GetResolvedModuleFromModuleSpecifier(
				        file2, specifier);
				gotest::assert::Equal(t, resolved != nullptr && resolved->IsResolved(),
				              !isSourcePhaseImport(specifier->parent));
			}

			files["/src/index.ts"] = content;
			host = compiler::NewCompilerHost(
			    "/", vfs::vfstest::FromMap(files, true), "", nullptr,
			    nullptr, nullptr);
			auto [program3, file3, reused3] = program->UpdateProgram(
			    "/src/index.ts", host, nullptr, nullptr);
			gotest::assert::Assert(t, !reused3);
			program = program3;
			gotest::assert::Assert(t, program->GetSourceFile("/src/a.ts") ==
			                    nullptr);

			files["/src/index.ts"] = test.evaluation + content;
			host = compiler::NewCompilerHost(
			    "/", vfs::vfstest::FromMap(files, true), "", nullptr,
			    nullptr, nullptr);
			auto [program4, file4, reused4] = program->UpdateProgram(
			    "/src/index.ts", host, nullptr, nullptr);
			program = program4;
			for (auto* specifier : file4->imports) {
				auto* resolved =
				    program->GetResolvedModuleFromModuleSpecifier(
				        file4, specifier);
				gotest::assert::Equal(t, resolved != nullptr && resolved->IsResolved(),
				              !isSourcePhaseImport(specifier->parent));
			}
		});
	}
}
REGISTER_UNIT_TEST("compiler.TestImportSourceProgram",
                   TestImportSourceProgram);
