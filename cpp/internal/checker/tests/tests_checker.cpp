// tests_checker.cpp — port of tsc/internal/checker/checker_test.go
// (TestGetSymbolAtLocation; BenchmarkNewChecker is not ported).
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/nodes_generated.h"
#include "internal/bundled/bundled.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

void TestGetSymbolAtLocation(T* t) {
	t->Parallel();

	const char* content = R"(interface Foo {
  bar: string;
}
declare const foo: Foo;
foo.bar;)";
	auto fs = vfs::vfstest::FromMap(
	    {
	        {"/foo.ts", content},
	        {"/tsconfig.json", R"(
				{
					"compilerOptions": {},
					"files": ["foo.ts"]
				}
			)"},
	    },
	    false /*useCaseSensitiveFileNames*/);
	fs = bundled::WrapFS(fs);

	std::string cd = "/";
	auto* host = compiler::NewCompilerHost(cd, fs, bundled::LibPath(),
	                                       nullptr, nullptr, nullptr);

	CompilerOptions opts;
	auto [parsed, errors] = tsoptions::GetParsedCommandLineOfConfigFile(
	    "/tsconfig.json", &opts, nullptr, host, nullptr);
	assert::Equal(t, errors.size(), 0,
	              "Expected no errors in parsed command line");

	compiler::ProgramOptions programOpts;
	programOpts.Config = parsed;
	programOpts.Host = host;
	auto* p = compiler::NewProgram(programOpts);
	p->BindSourceFiles();
	auto [c, done] = p->GetTypeChecker(ContextPtr{});
	(void)done;
	auto* file = p->GetSourceFile("/foo.ts");
	auto* interfaceId = file->Statements->nodes[0]->name();
	auto* varId = file->Statements->nodes[1]
	                  ->as<VariableStatement>()
	                  ->DeclarationList->as<VariableDeclarationList>()
	                  ->Declarations->nodes[0]
	                  ->name();
	auto* propAccess = file->Statements->nodes[2]->expression();
	std::vector<Node*> nodes = {interfaceId, varId, propAccess};
	for (auto* node : nodes) {
		Symbol* symbol = c->GetSymbolAtLocation(node);
		if (symbol == nullptr) {
			t->Fatalf("Expected symbol to be non-nil", {});
		}
	}
}

} // namespace

REGISTER_UNIT_TEST("checker.TestGetSymbolAtLocation", TestGetSymbolAtLocation);

namespace {

void TestGetTypeAtLocationOfTypeOnlyImportClause(T* t) {
	t->Parallel();

	auto fs = vfs::vfstest::FromMap(
	    {
	        {"/types.ts", "export type U = number;\nexport default interface D { x: number }"},
	        {"/main.ts", "import type { U } from \"./types\";\nimport type * as types from \"./types\";\nimport { U as V } from \"./types\";\nimport type D from \"./types\";\nexport const u: U = 1;\nexport const v: V = 1;\nexport type W = types.U;\nexport type E = D;"},
	        {"/tsconfig.json", R"(
				{
					"compilerOptions": {},
					"files": ["types.ts", "main.ts"]
				}
			)"},
	    },
	    false /*useCaseSensitiveFileNames*/);
	fs = bundled::WrapFS(fs);

	std::string cd = "/";
	auto* host = compiler::NewCompilerHost(cd, fs, bundled::LibPath(),
	                                       nullptr, nullptr, nullptr);

	CompilerOptions opts;
	auto [parsed, errors] = tsoptions::GetParsedCommandLineOfConfigFile(
	    "/tsconfig.json", &opts, nullptr, host, nullptr);
	assert::Equal(t, errors.size(), 0,
	              "Expected no errors in parsed command line");

	compiler::ProgramOptions programOpts;
	programOpts.Config = parsed;
	programOpts.Host = host;
	auto* p = compiler::NewProgram(programOpts);
	p->BindSourceFiles();
	auto [c, done] = p->GetTypeChecker(ContextPtr{});
	(void)done;
	auto* file = p->GetSourceFile("/main.ts");
	auto importClauseAt = [file](int index) -> Node* {
		return file->Statements->nodes[index]
		    ->as<ImportDeclaration>()
		    ->ImportClause;
	};
	// An import clause without a default binding has no symbol of its own. A
	// type-only one should get the same type as the equivalent regular import
	// instead of crashing.
	checker::Type* regular = c->GetTypeAtLocation(importClauseAt(2));
	for (int index : {0, 1}) {
		checker::Type* typ = c->GetTypeAtLocation(importClauseAt(index));
		if (typ == nullptr) {
			t->Fatalf("Expected type of import clause %d to be non-nil",
			          {std::to_string(index)});
		}
		assert::Equal(t, typ, regular);
	}

	checker::Type* defaultClause = c->GetTypeAtLocation(importClauseAt(3));
	checker::Type* defaultReference = c->GetTypeAtLocation(
	    file->Statements->nodes[7]->as<TypeAliasDeclaration>()->Type);
	assert::Equal(t, defaultClause, defaultReference);
}

} // namespace

REGISTER_UNIT_TEST("checker.TestGetTypeAtLocationOfTypeOnlyImportClause",
                   TestGetTypeAtLocationOfTypeOnlyImportClause);
