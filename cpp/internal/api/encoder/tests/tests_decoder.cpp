// Port of tsc/internal/api/encoder/decoder_test.go (package encoder_test).
#include <string>
#include <vector>

#include "internal/api/encoder/encoder.h"
#include "internal/ast/ast.h"
#include "internal/ast/visitor.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/parser/parser.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::api::encoder {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;

SourceFile* parseTestFile(const std::string& code) {
	return parseSourceFile(
	    SourceFileParseOptions{
	        .FileName = "/test.ts",
	        .Path = "/test.ts",
	    },
	    code, ScriptKind::TS);
}

void TestDecodeSourceFile_Basic(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("let x = 1;");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);
	assert::Equal(t, decoded->asNode()->kind, Kind::SourceFile);
	assert::Equal(t, decoded->FileName(), std::string("/test.ts"));
	assert::Equal(t, decoded->Text(), std::string("let x = 1;"));
	assert::Assert(t, decoded->Statements != nullptr);
	assert::Assert(t, decoded->EndOfFileToken != nullptr);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_Basic",
                   TestDecodeSourceFile_Basic);

void TestDecodeSourceFile_Metadata(T* t) {
	t->Parallel();

	struct testCase {
		std::string name;
		std::string fileName;
		ScriptKind scriptKind;
		std::string code;
	};
	std::vector<testCase> tests{
	    {"JSON", "/test.json", ScriptKind::JSON, "{\"x\": 1}"},
	    {"JSX", "/test.jsx", ScriptKind::JSX, "const x = <div />;"},
	    {"declaration", "/test.d.ts", ScriptKind::TS,
	     "declare const x: number;"},
	};

	for (const auto& tt : tests) {
		t->Run(tt.name, [tt](T* t) {
			t->Parallel();
			SourceFile* sourceFile = parseSourceFile(
			    SourceFileParseOptions{
			        .FileName = tt.fileName,
			        .Path = tspath::Path(tt.fileName),
			    },
			    tt.code, tt.scriptKind);
			auto [buf, table, err] = EncodeSourceFile(sourceFile);
			assert::NilError(t, err);

			auto [decoded, derr] = DecodeSourceFile(buf);
			assert::NilError(t, derr);
			assert::Equal(t, decoded->ScriptKind,
			              sourceFile->ScriptKind);
			assert::Equal(t, decoded->LanguageVariant,
			              sourceFile->LanguageVariant);
			assert::Equal(t, decoded->IsDeclarationFile,
			              sourceFile->IsDeclarationFile);
		});
	}
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_Metadata",
                   TestDecodeSourceFile_Metadata);

void TestDecodeSourceFile_Statements(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("let a = 1;\nlet b = 2;\nlet c = 3;");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);
	assert::Equal(t, int(decoded->Statements->nodes.size()), 3);
	for (size_t i = 0; i < decoded->Statements->nodes.size(); i++) {
		assert::Equal(t, decoded->Statements->nodes[i]->kind,
		              Kind::VariableStatement,
		              gostd::sprintf("statement %d", {int(i)}));
	}
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_Statements",
                   TestDecodeSourceFile_Statements);

void TestDecodeSourceFile_VariableDeclaration(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("let x = 1;");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* varStmt =
	    decoded->Statements->nodes[0]->as<VariableStatement>();
	assert::Assert(t, varStmt->DeclarationList != nullptr);
	auto* declList = varStmt->DeclarationList->as<VariableDeclarationList>();
	assert::Assert(t, declList->Declarations != nullptr);
	assert::Equal(t, int(declList->Declarations->nodes.size()), 1);

	auto* decl = declList->Declarations->nodes[0]->as<VariableDeclaration>();
	assert::Equal(t, decl->name->kind, Kind::Identifier);
	assert::Equal(t, decl->name->as<Identifier>()->Text,
	              std::string("x"));
	assert::Assert(t, decl->Initializer != nullptr);
	assert::Equal(t, decl->Initializer->kind, Kind::NumericLiteral);
	assert::Equal(t, decl->Initializer->as<NumericLiteral>()->Text,
	              std::string("1"));
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_VariableDeclaration",
                   TestDecodeSourceFile_VariableDeclaration);

void TestDecodeSourceFile_VariableDeclarationListFlags(T* t) {
	t->Parallel();

	struct testCase {
		std::string name;
		std::string code;
		NodeFlags expected;
	};
	std::vector<testCase> tests{
	    {"const", "const x = 1;", NodeFlagsConst},
	    {"let", "let x = 1;", NodeFlagsLet},
	    {"var", "var x = 1;", NodeFlagsNone},
	};

	for (const auto& tt : tests) {
		t->Run(tt.name, [tt](T* t) {
			t->Parallel();
			SourceFile* sf = parseTestFile(tt.code);
			auto [buf, table, err] = EncodeSourceFile(sf);
			assert::NilError(t, err);

			auto [decoded, derr] = DecodeSourceFile(buf);
			assert::NilError(t, derr);

			auto* declList = decoded->Statements->nodes[0]
			                     ->as<VariableStatement>()
			                     ->DeclarationList
			                     ->as<VariableDeclarationList>();
			NodeFlags got = declList->flags &
			                (NodeFlagsLet | NodeFlagsConst);
			assert::Equal(t, int(got), int(tt.expected),
			              gostd::sprintf("flags for %q: got %d, want %d",
			                             {tt.code, int(got),
			                              int(tt.expected)}));
		});
	}
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_VariableDeclarationListFlags",
                   TestDecodeSourceFile_VariableDeclarationListFlags);

void TestDecodeSourceFile_FunctionDeclaration(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile(
	    "function add(a: number, b: number): number { return a + b; }");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* funcDecl =
	    decoded->Statements->nodes[0]->as<FunctionDeclaration>();
	assert::Assert(t, funcDecl->name != nullptr);
	assert::Equal(t, funcDecl->name->as<Identifier>()->Text,
	              std::string("add"));
	assert::Assert(t, funcDecl->Parameters != nullptr);
	assert::Equal(t, int(funcDecl->Parameters->nodes.size()), 2);
	assert::Assert(t, funcDecl->Type != nullptr);
	assert::Assert(t, funcDecl->Body != nullptr);

	auto* param0 = funcDecl->Parameters->nodes[0]->as<ParameterDeclaration>();
	assert::Equal(t, param0->name->as<Identifier>()->Text,
	              std::string("a"));
	assert::Assert(t, param0->Type != nullptr);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_FunctionDeclaration",
                   TestDecodeSourceFile_FunctionDeclaration);

void TestDecodeSourceFile_ImportDeclaration(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("import { bar } from \"bar\";");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* imp = decoded->Statements->nodes[0]->as<ImportDeclaration>();
	assert::Assert(t, imp->ImportClause != nullptr);
	assert::Assert(t, imp->ModuleSpecifier != nullptr);
	assert::Equal(t, imp->ModuleSpecifier->as<StringLiteral>()->Text,
	              std::string("bar"));

	auto* clause = imp->ImportClause->as<ImportClause>();
	assert::Assert(t, clause->NamedBindings != nullptr);
	auto* namedImports = clause->NamedBindings->as<NamedImports>();
	assert::Assert(t, namedImports->Elements != nullptr);
	assert::Equal(t, int(namedImports->Elements->nodes.size()), 1);
	auto* spec = namedImports->Elements->nodes[0]->as<ImportSpecifier>();
	assert::Equal(t, spec->name->as<Identifier>()->Text,
	              std::string("bar"));
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_ImportDeclaration",
                   TestDecodeSourceFile_ImportDeclaration);

void TestDecodeSourceFile_IfStatement(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("if (true) { } else { }");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* ifStmt = decoded->Statements->nodes[0]->as<IfStatement>();
	assert::Assert(t, ifStmt->Expression != nullptr);
	assert::Assert(t, ifStmt->ThenStatement != nullptr);
	assert::Assert(t, ifStmt->ElseStatement != nullptr);
	assert::Equal(t, ifStmt->ThenStatement->kind, Kind::Block);
	assert::Equal(t, ifStmt->ElseStatement->kind, Kind::Block);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_IfStatement",
                   TestDecodeSourceFile_IfStatement);

void TestDecodeSourceFile_TemplateExpression(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("let x = `hello ${name} world`;");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* varDecl = decoded->Statements->nodes[0]
	                    ->as<VariableStatement>()
	                    ->DeclarationList->as<VariableDeclarationList>()
	                    ->Declarations->nodes[0]
	                    ->as<VariableDeclaration>();
	auto* tmplExpr = varDecl->Initializer->as<TemplateExpression>();
	assert::Assert(t, tmplExpr->Head != nullptr);
	assert::Equal(t, tmplExpr->Head->as<TemplateHead>()->Text,
	              std::string("hello "));
	assert::Assert(t, tmplExpr->TemplateSpans != nullptr);
	assert::Equal(t, int(tmplExpr->TemplateSpans->nodes.size()), 1);

	auto* span = tmplExpr->TemplateSpans->nodes[0]->as<TemplateSpan>();
	assert::Assert(t, span->Expression != nullptr);
	assert::Equal(t, span->Expression->kind, Kind::Identifier);
	assert::Assert(t, span->Literal != nullptr);
	assert::Equal(t, span->Literal->as<TemplateTail>()->Text,
	              std::string(" world"));
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_TemplateExpression",
                   TestDecodeSourceFile_TemplateExpression);

void TestDecodeSourceFile_ExportModifier(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("export function foo() {}");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* funcDecl =
	    decoded->Statements->nodes[0]->as<FunctionDeclaration>();
	assert::Assert(t, funcDecl->modifiers != nullptr);
	assert::Equal(t, int(funcDecl->modifiers->nodes.size()), 1);
	assert::Equal(t, funcDecl->modifiers->nodes[0]->kind,
	              Kind::ExportKeyword);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_ExportModifier",
                   TestDecodeSourceFile_ExportModifier);

void TestDecodeSourceFile_Positions(T* t) {
	t->Parallel();
	std::string code = "let x = 1;";
	SourceFile* sf = parseTestFile(code);
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	assert::Equal(t, int(decoded->asNode()->pos()), 0);
	assert::Equal(t, int(decoded->asNode()->end()), int(code.size()));
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_Positions",
                   TestDecodeSourceFile_Positions);

void TestDecodeSourceFile_ClassDeclaration(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("class Foo { bar(): void {} }");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* classDecl =
	    decoded->Statements->nodes[0]->as<ClassDeclaration>();
	assert::Assert(t, classDecl->name != nullptr);
	assert::Equal(t, classDecl->name->as<Identifier>()->Text,
	              std::string("Foo"));
	assert::Assert(t, classDecl->Members != nullptr);
	assert::Equal(t, int(classDecl->Members->nodes.size()), 1);
	assert::Equal(t, classDecl->Members->nodes[0]->kind,
	              Kind::MethodDeclaration);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_ClassDeclaration",
                   TestDecodeSourceFile_ClassDeclaration);

void TestDecodeNodes_SubtreeRoundTrip(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile(
	    "function greet(name: string) { return `Hello, ${name}!`; }");

	Node* funcNode = nullptr;
	NodeVisitor visitor;
	visitor.visit = [&funcNode](Node* node) -> Node* {
		if (node->kind == Kind::FunctionDeclaration &&
		    funcNode == nullptr) {
			funcNode = node;
		}
		return node;
	};
	visitor.visitEachChild(sf->asNode());
	assert::Assert(t, funcNode != nullptr);

	auto [buf, table, err] = EncodeNode(funcNode, sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeNodes(buf);
	assert::NilError(t, derr);

	assert::Equal(t, decoded->kind, Kind::FunctionDeclaration);
	auto* funcDecl = decoded->as<FunctionDeclaration>();
	assert::Assert(t, funcDecl->name != nullptr);
	assert::Equal(t, funcDecl->name->as<Identifier>()->Text,
	              std::string("greet"));
	assert::Assert(t, funcDecl->Parameters != nullptr);
	assert::Equal(t, int(funcDecl->Parameters->nodes.size()), 1);
	assert::Assert(t, funcDecl->Body != nullptr);
}
REGISTER_UNIT_TEST("encoder.TestDecodeNodes_SubtreeRoundTrip",
                   TestDecodeNodes_SubtreeRoundTrip);

void TestDecodeSourceFile_BinaryExpression(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("let x = 1 + 2;");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* decl = decoded->Statements->nodes[0]
	                 ->as<VariableStatement>()
	                 ->DeclarationList->as<VariableDeclarationList>()
	                 ->Declarations->nodes[0]
	                 ->as<VariableDeclaration>();
	auto* binExpr = decl->Initializer->as<BinaryExpression>();
	assert::Assert(t, binExpr->Left != nullptr);
	assert::Assert(t, binExpr->Right != nullptr);
	assert::Assert(t, binExpr->OperatorToken != nullptr);
	assert::Equal(t, binExpr->Left->kind, Kind::NumericLiteral);
	assert::Equal(t, binExpr->Right->kind, Kind::NumericLiteral);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_BinaryExpression",
                   TestDecodeSourceFile_BinaryExpression);

void TestDecodeSourceFile_KeywordExpressions(T* t) {
	t->Parallel();
	// "this" must decode as KeywordExpression, not Token, or the printer
	// panics
	SourceFile* sf = parseTestFile("const x = this;");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	// Navigate: const x = this -> VariableStatement -> declaration ->
	// initializer
	auto* decl = decoded->Statements->nodes[0]
	                 ->as<VariableStatement>()
	                 ->DeclarationList->as<VariableDeclarationList>()
	                 ->Declarations->nodes[0]
	                 ->as<VariableDeclaration>();
	Node* thisExpr = decl->Initializer;
	assert::Equal(t, thisExpr->kind, Kind::ThisKeyword);
	// This would panic if decoded as Token instead of KeywordExpression
	assert::Assert(t, thisExpr->as<KeywordExpression>() != nullptr);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_KeywordExpressions",
                   TestDecodeSourceFile_KeywordExpressions);

void TestDecodeSourceFile_EmptyModuleBlock(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("namespace N { }");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	// Navigate: namespace N { } -> ModuleDeclaration -> ModuleBlock
	auto* mod = decoded->Statements->nodes[0]->as<ModuleDeclaration>();
	assert::Assert(t, mod->Body != nullptr);
	auto* block = mod->Body->as<ModuleBlock>();
	// Statements must be non-nil even when empty, otherwise the printer
	// panics
	assert::Assert(t, block->Statements != nullptr);
	assert::Equal(t, int(block->Statements->nodes.size()), 0);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_EmptyModuleBlock",
                   TestDecodeSourceFile_EmptyModuleBlock);

void TestDecodeSourceFile_EmptyBlockAndParams(T* t) {
	t->Parallel();
	// Empty blocks and parameter lists must decode with non-nil NodeLists
	// (not nil), matching parser behavior. Previously the decoder left them
	// nil, crashing the printer.
	SourceFile* sf = parseTestFile("function foo() {}");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* funcDecl =
	    decoded->Statements->nodes[0]->as<FunctionDeclaration>();
	assert::Assert(t, funcDecl->Parameters != nullptr,
	               "FunctionDeclaration.Parameters must be non-nil for "
	               "foo()");
	assert::Equal(t, int(funcDecl->Parameters->nodes.size()), 0);
	assert::Assert(t, funcDecl->Body != nullptr);
	auto* block = funcDecl->Body->as<Block>();
	assert::Assert(t, block->Statements != nullptr,
	               "Block.Statements must be non-nil for empty blocks");
	assert::Equal(t, int(block->Statements->nodes.size()), 0);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_EmptyBlockAndParams",
                   TestDecodeSourceFile_EmptyBlockAndParams);

void TestDecodeSourceFile_ArrowFunctionEmptyParams(T* t) {
	t->Parallel();
	// `() => {}` must decode with non-nil Parameters (empty NodeList),
	// matching parser behavior. Previously the decoder left it nil, crashing
	// the printer.
	SourceFile* sf = parseTestFile("const f = () => {};");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* decl = decoded->Statements->nodes[0]
	                 ->as<VariableStatement>()
	                 ->DeclarationList->as<VariableDeclarationList>()
	                 ->Declarations->nodes[0]
	                 ->as<VariableDeclaration>();
	auto* arrow = decl->Initializer->as<ArrowFunction>();
	assert::Assert(t, arrow->Parameters != nullptr,
	               "ArrowFunction.Parameters must be non-nil for () => {}");
	assert::Equal(t, int(arrow->Parameters->nodes.size()), 0);
	assert::Assert(t, arrow->Body != nullptr);
	auto* block = arrow->Body->as<Block>();
	assert::Assert(t, block->Statements != nullptr,
	               "Block.Statements must be non-nil for empty body");
	assert::Equal(t, int(block->Statements->nodes.size()), 0);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_ArrowFunctionEmptyParams",
                   TestDecodeSourceFile_ArrowFunctionEmptyParams);

void TestDecodeSourceFile_FunctionExpressionEmptyParams(T* t) {
	t->Parallel();
	// `function() {}` must decode with non-nil Parameters (empty NodeList).
	SourceFile* sf = parseTestFile("const f = function() {};");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* decl = decoded->Statements->nodes[0]
	                 ->as<VariableStatement>()
	                 ->DeclarationList->as<VariableDeclarationList>()
	                 ->Declarations->nodes[0]
	                 ->as<VariableDeclaration>();
	auto* funcExpr = decl->Initializer->as<FunctionExpression>();
	assert::Assert(t, funcExpr->Parameters != nullptr,
	               "FunctionExpression.Parameters must be non-nil for "
	               "function() {}");
	assert::Equal(t, int(funcExpr->Parameters->nodes.size()), 0);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_FunctionExpressionEmptyParams",
                   TestDecodeSourceFile_FunctionExpressionEmptyParams);

void TestDecodeSourceFile_PostfixUnaryOperator(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("let i = 0; i++;");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* exprStmt =
	    decoded->Statements->nodes[1]->as<ExpressionStatement>();
	auto* postfix = exprStmt->Expression->as<PostfixUnaryExpression>();
	assert::Equal(t, postfix->Operator, Kind::PlusPlusToken);
	assert::Equal(t, postfix->Operand->kind, Kind::Identifier);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_PostfixUnaryOperator",
                   TestDecodeSourceFile_PostfixUnaryOperator);

void TestDecodeSourceFile_PrefixUnaryOperator(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("let x = true; !x;");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* exprStmt =
	    decoded->Statements->nodes[1]->as<ExpressionStatement>();
	auto* prefix = exprStmt->Expression->as<PrefixUnaryExpression>();
	assert::Equal(t, prefix->Operator, Kind::ExclamationToken);
	assert::Equal(t, prefix->Operand->kind, Kind::Identifier);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_PrefixUnaryOperator",
                   TestDecodeSourceFile_PrefixUnaryOperator);

void TestDecodeSourceFile_PostfixDecrement(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("let n = 5; n--;");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* exprStmt =
	    decoded->Statements->nodes[1]->as<ExpressionStatement>();
	auto* postfix = exprStmt->Expression->as<PostfixUnaryExpression>();
	assert::Equal(t, postfix->Operator, Kind::MinusMinusToken);
}
REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_PostfixDecrement",
                   TestDecodeSourceFile_PostfixDecrement);


void TestDecodeSourceFile_SourcePhaseImport(T* t) {
	t->Parallel();
	SourceFile* sf = parseTestFile("import source a from \"./a.wasm\";");
	auto [buf, table, err] = EncodeSourceFile(sf);
	assert::NilError(t, err);

	auto [decoded, derr] = DecodeSourceFile(buf);
	assert::NilError(t, derr);

	auto* clause = decoded->Statements->nodes[0]
	                   ->as<ImportDeclaration>()
	                   ->ImportClause->as<ImportClause>();
	assert::Equal(t, clause->PhaseModifier, Kind::SourceKeyword);
	assert::Equal(t, std::string(clause->name->text()), std::string("a"));
}

}  // namespace

}  // namespace tsc::api::encoder
namespace tsc::api::encoder { REGISTER_UNIT_TEST("encoder.TestDecodeSourceFile_SourcePhaseImport", TestDecodeSourceFile_SourcePhaseImport); }
