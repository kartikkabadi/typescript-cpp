// tests_importelision.cpp — port of
// tsc/internal/transformers/tstransforms/importelision_test.go.
//
// fakeProgram implements checker::Program's pure virtuals just far enough
// for checker::Checker + the import-elision transformer, mirroring the Go
// fake whose unused methods panic("unimplemented").
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/checker/checker.h"
#include "internal/core/types.h"
#include "internal/core/utilities.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/module/types.h"
#include "internal/printer/emitcontext.h"
#include "internal/testutil/emittestutil/emittestutil.h"
#include "internal/testutil/parsetestutil/parsetestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/transformers/tstransforms/tstransforms.h"
#include "internal/transformers/transformers.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;
namespace parsetestutil = tsc::testutil::parsetestutil;
namespace emittestutil = tsc::testutil::emittestutil;
namespace tstransforms = tsc::transformers::tstransforms;
namespace transformers = tsc::transformers;

namespace {

struct fakeProgram : checker::Program {
	bool singleThreaded = false;
	CompilerOptions* compilerOptions = nullptr;
	std::vector<SourceFile*> files;
	std::function<ModuleKind(SourceFile*)> getEmitModuleFormatOfFile;
	std::function<ModuleKind(SourceFile*)> getImpliedNodeFormatForEmit;
	std::function<std::optional<checker::ResolvedModule>(
	    SourceFile*, const std::string&, ResolutionMode)>
	    getResolvedModule;
	std::function<SourceFile*(const std::string&)> getSourceFile;
	std::function<SourceFile*(const std::string&)>
	    getSourceFileForResolvedModule;

	const CompilerOptions* Options() override { return compilerOptions; }
	std::vector<SourceFile*> SourceFiles() override { return files; }
	void BindSourceFiles() override {
		std::unique_ptr<workGroup> wg(newWorkGroup(singleThreaded));
		for (auto* file : files) {
			if (!file->IsBound()) {
				wg->Queue([file] { bindSourceFile(file); });
			}
		}
		wg->RunAndWait();
	}
	bool FileExists(const std::string&) override { return false; }
	SourceFile* GetSourceFile(const std::string& fileName) override {
		return getSourceFile(fileName);
	}
	SourceFile* GetSourceFileForResolvedModule(
	    const std::string& fileName) override {
		return getSourceFileForResolvedModule(fileName);
	}
	ModuleKind GetEmitModuleFormatOfFile(SourceFile* sourceFile) override {
		return getEmitModuleFormatOfFile(sourceFile);
	}
	ResolutionMode GetEmitSyntaxForUsageLocation(
	    SourceFile*, Node*) override {
		throw std::string("unimplemented");
	}
	ModuleKind GetImpliedNodeFormatForEmit(SourceFile* sourceFile) override {
		return getImpliedNodeFormatForEmit(sourceFile);
	}
	bool SourceFileMayBeEmitted(SourceFile*, bool) override {
		throw std::string("unimplemented");
	}
	std::string CommonSourceDirectory() override {
		throw std::string("unimplemented");
	}
	std::optional<checker::ResolvedModule> GetResolvedModule(
	    SourceFile* file, const std::string& moduleReference,
	    ResolutionMode mode) override {
		return getResolvedModule(file, moduleReference, mode);
	}
	ResolutionMode GetModeForUsageLocation(SourceFile* file,
	                                       Node*) override {
		return getEmitModuleFormatOfFile(file);
	}
	ResolutionMode GetDefaultResolutionModeForFile(
	    SourceFile* file) override {
		return getEmitModuleFormatOfFile(file);
	}
	std::string GetCurrentDirectory() override { return ""; }
	bool UseCaseSensitiveFileNames() override { return true; }
	bool IsSourceFileFromExternalLibrary(SourceFile*) const override {
		return false;
	}
};

void TestImportElision(T* t) {
	t->Parallel();
	struct rec {
		const char* title;
		const char* input;
		const char* output;
		const char* other;
		bool jsx;
	};
	rec data[] = {
	    {"ImportEquals#1", "import x = require(\"other\"); x;",
	     "import x = require(\"other\");\nx;", "", false},
	    {"ImportEquals#2", "import x = require(\"other\");", "", "", false},
	    {"ImportDeclaration#1", "import \"m\";", "import \"m\";", "", false},
	    {"ImportDeclaration#2", "import * as x from \"other\"; x;",
	     "import * as x from \"other\";\nx;", "", false},
	    {"ImportDeclaration#3", "import x from \"other\"; x;",
	     "import x from \"other\";\nx;", "", false},
	    {"ImportDeclaration#4", "import { x } from \"other\"; x;",
	     "import { x } from \"other\";\nx;", "", false},
	    {"ImportDeclaration#5", "import * as x from \"other\";", "", "", false},
	    {"ImportDeclaration#6", "import x from \"other\";", "", "", false},
	    {"ImportDeclaration#7", "import { x } from \"other\";", "", "", false},
	    {"ExportDeclaration#1", "export * from \"other\";",
	     "export * from \"other\";", "export let x;", false},
	    {"ExportDeclaration#2", "export * as x from \"other\";",
	     "export * as x from \"other\";", "export let x;", false},
	    {"ExportDeclaration#3", "export * from \"other\";",
	     "export * from \"other\";", "export let x;", false},
	    {"ExportDeclaration#4", "export * as x from \"other\";",
	     "export * as x from \"other\";", "export let x;", false},
	    {"ExportDeclaration#5", "export { x } from \"other\";",
	     "export { x } from \"other\";", "export let x;", false},
	    {"ExportDeclaration#6", "export { x } from \"other\";", "",
	     "export type x = any;", false},
	    {"ExportDeclaration#7", "export { x }; let x;",
	     "export { x };\nlet x;", "", false},
	    {"ExportDeclaration#8", "export { x }; type x = any;", "", "", false},
	    {"ExportDeclaration#9", "import { x } from \"other\"; export { x };",
	     "", "export type x = any;", false},
	    {"ExportAssignment#1", "let x; export default x;",
	     "let x;\nexport default x;", "", false},
	    {"ExportAssignment#2", "type x = any; export default x;", "", "",
	     false},
	};

	for (auto& r : data) {
		t->Run(r.title, [&r](T* t) {
			t->Parallel();

			auto* file =
			    parsetestutil::ParseTypeScript(r.input, r.jsx);
			parsetestutil::CheckDiagnostics(t, file);
			std::vector<SourceFile*> files{file};

			SourceFile* other = nullptr;
			if (r.other[0] != '\0') {
				other = parsetestutil::ParseTypeScript(r.other, r.jsx);
				parsetestutil::CheckDiagnostics(t, other);
				files.push_back(other);
			}

			auto* compilerOptions = new CompilerOptions();

			fakeProgram program;
			program.singleThreaded = true;
			program.compilerOptions = compilerOptions;
			program.files = files;
			program.getEmitModuleFormatOfFile =
			    [](SourceFile*) { return ModuleKind::ESNext; };
			program.getImpliedNodeFormatForEmit =
			    [](SourceFile*) { return ModuleKind::ESNext; };
			program.getSourceFile = [other](const std::string& fileName) {
				if (fileName == "other.ts") {
					return other;
				}
				return (SourceFile*)nullptr;
			};
			program.getSourceFileForResolvedModule =
			    [other](const std::string& fileName) {
				    if (fileName == "other.ts") {
					    return other;
				    }
				    return (SourceFile*)nullptr;
			    };
			program.getResolvedModule =
			    [file](SourceFile* currentSourceFile,
			           const std::string& moduleReference,
			           ResolutionMode)
			    -> std::optional<checker::ResolvedModule> {
				if (currentSourceFile == file &&
				    moduleReference == "other") {
					checker::ResolvedModule rm;
					rm.resolved = true;
					rm.resolvedFileName = "other.ts";
					rm.extension = std::string(tspath::extensionTs);
					return rm;
				}
				return std::nullopt;
			};

			auto* c = new checker::Checker();
			c->init(&program);

			auto* emitContext = printer::NewEmitContext();
			auto* emitResolver = c->NewEmitResolver(emitContext);

			transformers::TransformOptions opts;
			opts.CompilerOptions = compilerOptions;
			opts.Context = emitContext;
			opts.EmitResolver = emitResolver;
			opts.Resolver = emitResolver;
			file = tstransforms::NewTypeEraserTransformer(&opts)
			           ->transformSourceFile(file);
			file = tstransforms::NewImportElisionTransformer(&opts)
			           ->transformSourceFile(file);
			emittestutil::CheckEmit(t, nullptr, file, r.output);
		});
	}
}

} // namespace

REGISTER_UNIT_TEST("tstransforms.TestImportElision", TestImportElision);
