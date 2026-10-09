// tests_sourcephase.cpp — port of the source-phase-import tests added to
// tsc/internal/parser/parser_test.go by upstream 2f9fd09a.
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/kind.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/parsetestutil/parsetestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace parsetestutil = tsc::testutil::parsetestutil;
using namespace tsc;

namespace {

void TestParseStaticSourcePhaseImport(T* t) {
	t->Parallel();
	struct test {
		std::string name;
		std::string source;
		Kind phaseModifier;
		std::string bindingName;
		bool hasAttributes;
	};
	std::vector<test> tests = {
	    {"source phase", "import source a from \"./a.wasm\";",
	     Kind::SourceKeyword, "a", false},
	    {"source phase with import attributes",
	     "import source a from \"./a.wasm\" with { type: \"webassembly\" };",
	     Kind::SourceKeyword, "a", true},
	    {"from as source phase binding",
	     "import source from from \"./module.js\";", Kind::SourceKeyword,
	     "from", false},
	    {"source as ordinary default binding",
	     "import source from \"./module.js\";", Kind::Unknown, "source",
	     false},
	    {"source as ordinary default binding with named imports",
	     "import source, { value } from \"./module.js\";", Kind::Unknown,
	     "source", false},
	    {"escaped source as ordinary default binding",
	     "import s\\u006furce from \"./module.js\";", Kind::Unknown, "source",
	     false},
	    {"escaped defer as ordinary default binding",
	     "import d\\u0065fer from \"./module.js\";", Kind::Unknown, "defer",
	     false},
	};
	for (auto& test : tests) {
		t->Run(test.name, [&test](gostd::testing::T* t) {
			t->Parallel();
			auto* file = parsetestutil::ParseTypeScript(test.source, false);
			parsetestutil::CheckDiagnostics(t, file);
			assert::Equal(t, file->Statements->nodes.size(), size_t(1));

			Node* statement = file->Statements->nodes[0];
			assert::Assert(t, isImportDeclaration(statement));

			auto* declaration = statement->as<ImportDeclaration>();
			assert::Assert(t, declaration->ImportClause != nullptr);

			auto* clause = declaration->ImportClause->as<ImportClause>();
			assert::Equal(t, clause->PhaseModifier, test.phaseModifier);
			assert::Assert(t, clause->name != nullptr);
			assert::Equal(t, std::string(clause->name->text()),
			              test.bindingName);
			assert::Equal(t, declaration->Attributes != nullptr,
			              test.hasAttributes);
		});
	}
}

void TestParseSourceAsImportEqualsBinding(T* t) {
	t->Parallel();
	auto* file = parsetestutil::ParseTypeScript(
	    "import source = require(\"./module.js\");", false);
	parsetestutil::CheckDiagnostics(t, file);
	assert::Equal(t, file->Statements->nodes.size(), size_t(1));

	Node* statement = file->Statements->nodes[0];
	assert::Assert(t, isImportEqualsDeclaration(statement));
	assert::Equal(t, std::string(statement->name()->text()),
	              std::string("source"));
}

void TestParseInvalidStaticSourcePhaseImports(T* t) {
	t->Parallel();
	struct test {
		std::string source;
		bool hasName;
		bool hasNamedBindings;
	};
	std::vector<test> tests = {
	    {"import source \"./a.js\";", false, false},
	    {"import source * as a from \"./a.js\";", false, true},
	    {"import source { a } from \"./a.js\";", false, true},
	    {"import source a, { b } from \"./a.js\";", true, true},
	};
	for (auto& test : tests) {
		auto* file = parsetestutil::ParseTypeScript(test.source, false);
		parsetestutil::CheckDiagnostics(t, file);
		assert::Equal(t, file->Statements->nodes.size(), size_t(1));

		Node* statement = file->Statements->nodes[0];
		assert::Assert(t, isImportDeclaration(statement));

		auto* declaration = statement->as<ImportDeclaration>();
		assert::Assert(t, declaration->ImportClause != nullptr);

		auto* clause = declaration->ImportClause->as<ImportClause>();
		assert::Equal(t, clause->PhaseModifier, Kind::SourceKeyword);
		assert::Equal(t, clause->name != nullptr, test.hasName);
		assert::Equal(t, clause->NamedBindings != nullptr,
		              test.hasNamedBindings);
	}
}

void TestParseDynamicSourcePhaseImport(T* t) {
	t->Parallel();
	auto* file = parsetestutil::ParseTypeScript(
	    "import.source(\"./a.wasm\", { with: { type: \"webassembly\" } });",
	    false);
	parsetestutil::CheckDiagnostics(t, file);
	assert::Equal(t, file->Statements->nodes.size(), size_t(1));

	Node* statement = file->Statements->nodes[0];
	assert::Assert(t, isExpressionStatement(statement));

	Node* call = statement->as<ExpressionStatement>()->Expression;
	assert::Assert(t, isCallExpression(call));
	assert::Assert(t, isImportCall(call));
	assert::Assert(t, call->subtreeFacts() & SubtreeContainsDynamicImport);

	auto* callExpr = call->as<CallExpression>();
	Node* metaProperty = callExpr->Expression;
	assert::Assert(t, isMetaProperty(metaProperty));
	assert::Equal(t, metaProperty->as<MetaProperty>()->KeywordToken,
	              Kind::ImportKeyword);
	assert::Equal(t, std::string(metaProperty->text()), std::string("source"));
	assert::Equal(t, callExpr->Arguments->nodes.size(), size_t(2));
	assert::Equal(t, (file->flags & NodeFlagsPossiblyContainsDynamicImport) != 0,
	              true);
	assert::Equal(t, (file->flags & NodeFlagsPossiblyContainsImportMeta) != 0,
	              false);
	assert::Equal(t, file->ExternalModuleIndicator == nullptr, true);
}

void TestParseEscapedDynamicImportPhase(T* t) {
	t->Parallel();
	struct test {
		std::string source;
		std::string phaseName;
	};
	std::vector<test> tests = {
	    {"import.d\\u0065fer(\"./a.js\");", "defer"},
	    {"import.s\\u006furce(\"./a.wasm\");", "source"},
	};
	for (auto& test : tests) {
		t->Run(test.phaseName, [&test](gostd::testing::T* t) {
			t->Parallel();
			auto* file = parsetestutil::ParseTypeScript(test.source, false);
			auto diags = file->diagnostics;
			assert::Equal(t, diags.size(), size_t(1));
			assert::Equal(t, diags[0]->code,
			              Keywords_cannot_contain_escape_characters->code);
			assert::Equal(t, file->Statements->nodes.size(), size_t(1));

			Node* statement = file->Statements->nodes[0];
			assert::Assert(t, isExpressionStatement(statement));

			Node* call = statement->as<ExpressionStatement>()->Expression;
			assert::Assert(t, isCallExpression(call));
			assert::Assert(t, isImportCall(call));

			Node* metaProperty = call->as<CallExpression>()->Expression;
			assert::Assert(t, isMetaProperty(metaProperty));
			assert::Equal(t, std::string(metaProperty->text()),
			              test.phaseName);
			assert::Equal(
			    t, (file->flags & NodeFlagsPossiblyContainsDynamicImport) != 0,
			    true);
			assert::Equal(
			    t, (file->flags & NodeFlagsPossiblyContainsImportMeta) != 0,
			    false);
		});
	}
}

} // namespace

REGISTER_UNIT_TEST("parser.TestParseStaticSourcePhaseImport",
                   TestParseStaticSourcePhaseImport);
REGISTER_UNIT_TEST("parser.TestParseSourceAsImportEqualsBinding",
                   TestParseSourceAsImportEqualsBinding);
REGISTER_UNIT_TEST("parser.TestParseInvalidStaticSourcePhaseImports",
                   TestParseInvalidStaticSourcePhaseImports);
REGISTER_UNIT_TEST("parser.TestParseDynamicSourcePhaseImport",
                   TestParseDynamicSourcePhaseImport);
REGISTER_UNIT_TEST("parser.TestParseEscapedDynamicImportPhase",
                   TestParseEscapedDynamicImportPhase);
