// Port of tsc/internal/printer/namegenerator_test.go.
#include <string>

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/gostd/testing.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"
#include "internal/testutil/parsetestutil/parsetestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;

namespace parsetestutil = tsc::testutil::parsetestutil;

namespace {

// getTextOfNode — (*ast.Node).Text (namegenerator_test.go).
std::string getTextOfNode(Node* n) { return n->text(); }

void TestTempVariable1(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newTempVariable();
	auto* name2 = ec->factory.newTempVariable();

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name2);

	gotest::assert::Equal(t, std::string("_a"), text1);
	gotest::assert::Equal(t, std::string("_b"), text2);
}
REGISTER_UNIT_TEST("printer.TestTempVariable1", TestTempVariable1);

void TestTempVariable2(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newTempVariable(
	    printer::AutoGenerateOptions{.Prefix = "A", .Suffix = "B"});
	auto* name2 = ec->factory.newTempVariable(
	    printer::AutoGenerateOptions{.Prefix = "A", .Suffix = "B"});

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name2);

	gotest::assert::Equal(t, std::string("A_aB"), text1);
	gotest::assert::Equal(t, std::string("A_bB"), text2);
}
REGISTER_UNIT_TEST("printer.TestTempVariable2", TestTempVariable2);

void TestTempVariable3(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newTempVariable();

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("_a"), text1);
	gotest::assert::Equal(t, std::string("_a"), text2);
}
REGISTER_UNIT_TEST("printer.TestTempVariable3", TestTempVariable3);

void TestTempVariableScoped(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newTempVariable();
	auto* name2 = ec->factory.newTempVariable();

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	g.PushScope(false);
	auto text2 = g.GenerateName(name2);
	g.PopScope(false);

	gotest::assert::Equal(t, std::string("_a"), text1);
	gotest::assert::Equal(t, std::string("_a"), text2);
}
REGISTER_UNIT_TEST("printer.TestTempVariableScoped", TestTempVariableScoped);

void TestTempVariableScopedReserved(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newTempVariable(
	    printer::AutoGenerateOptions{
	        .Flags = printer::GeneratedIdentifierFlagsReservedInNestedScopes});
	auto* name2 = ec->factory.newTempVariable();

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	g.PushScope(false);
	auto text2 = g.GenerateName(name2);
	g.PopScope(false);

	gotest::assert::Equal(t, std::string("_a"), text1);
	gotest::assert::Equal(t, std::string("_b"), text2);
}
REGISTER_UNIT_TEST("printer.TestTempVariableScopedReserved",
                   TestTempVariableScopedReserved);

void TestLoopVariable1(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newLoopVariable();
	auto* name2 = ec->factory.newLoopVariable();

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name2);

	gotest::assert::Equal(t, std::string("_i"), text1);
	gotest::assert::Equal(t, std::string("_a"), text2);
}
REGISTER_UNIT_TEST("printer.TestLoopVariable1", TestLoopVariable1);

void TestLoopVariable2(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newLoopVariable(
	    printer::AutoGenerateOptions{.Prefix = "A", .Suffix = "B"});
	auto* name2 = ec->factory.newLoopVariable(
	    printer::AutoGenerateOptions{.Prefix = "A", .Suffix = "B"});

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name2);

	gotest::assert::Equal(t, std::string("A_iB"), text1);
	gotest::assert::Equal(t, std::string("A_aB"), text2);
}
REGISTER_UNIT_TEST("printer.TestLoopVariable2", TestLoopVariable2);

void TestLoopVariable3(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newLoopVariable();

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("_i"), text1);
	gotest::assert::Equal(t, std::string("_i"), text2);
}
REGISTER_UNIT_TEST("printer.TestLoopVariable3", TestLoopVariable3);

void TestLoopVariableScoped(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newLoopVariable();
	auto* name2 = ec->factory.newLoopVariable();

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	g.PushScope(false);
	auto text2 = g.GenerateName(name2);
	g.PopScope(false);

	gotest::assert::Equal(t, std::string("_i"), text1);
	gotest::assert::Equal(t, std::string("_i"), text2);
}
REGISTER_UNIT_TEST("printer.TestLoopVariableScoped", TestLoopVariableScoped);

void TestUniqueName1(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newUniqueName("foo");
	auto* name2 = ec->factory.newUniqueName("foo");

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name2);

	gotest::assert::Equal(t, std::string("foo_1"), text1);
	gotest::assert::Equal(t, std::string("foo_2"), text2);
}
REGISTER_UNIT_TEST("printer.TestUniqueName1", TestUniqueName1);

void TestUniqueName2(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newUniqueName("foo");

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("foo_1"), text1);
	// Expected to be same because GenerateName goes off object identity
	gotest::assert::Equal(t, std::string("foo_1"), text2);
}
REGISTER_UNIT_TEST("printer.TestUniqueName2", TestUniqueName2);

void TestUniqueNameScoped(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newUniqueName("foo");
	auto* name2 = ec->factory.newUniqueName("foo");

	printer::NameGenerator g;
	g.Context = ec;
	gotest::assert::Equal(t, std::string("foo_1"), g.GenerateName(name1));

	g.PushScope(false);
	gotest::assert::Equal(t, std::string("foo_2"),
	                      g.GenerateName(name2)); // Matches Strada, but is
	                                              // incorrect
	// gotest::assert::Equal(t, std::string("foo_1"), g.GenerateName(name2)) //
	// TODO: Fix after Strada port is complete.
	g.PopScope(false);
}
REGISTER_UNIT_TEST("printer.TestUniqueNameScoped", TestUniqueNameScoped);

void TestUniquePrivateName1(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newUniquePrivateName("#foo");
	auto* name2 = ec->factory.newUniquePrivateName("#foo");

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name2);

	gotest::assert::Equal(t, std::string("#foo_1"), text1);
	gotest::assert::Equal(t, std::string("#foo_2"), text2);
}
REGISTER_UNIT_TEST("printer.TestUniquePrivateName1", TestUniquePrivateName1);

void TestUniquePrivateName2(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newUniquePrivateName("#foo");

	printer::NameGenerator g;
	g.Context = ec;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("#foo_1"), text1);
	gotest::assert::Equal(t, std::string("#foo_1"), text2);
}
REGISTER_UNIT_TEST("printer.TestUniquePrivateName2", TestUniquePrivateName2);

void TestUniquePrivateNameScoped(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();
	auto* name1 = ec->factory.newUniquePrivateName("#foo");
	auto* name2 = ec->factory.newUniquePrivateName("#foo");

	printer::NameGenerator g;
	g.Context = ec;
	gotest::assert::Equal(t, std::string("#foo_1"), g.GenerateName(name1));

	g.PushScope(false); // private names are always reserved in nested scopes
	gotest::assert::Equal(t, std::string("#foo_2"), g.GenerateName(name2));
	g.PopScope(false);
}
REGISTER_UNIT_TEST("printer.TestUniquePrivateNameScoped",
                   TestUniquePrivateNameScoped);

void TestGeneratedNameForIdentifier1(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("function f() {}", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0]->name();
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("f_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForIdentifier1",
                   TestGeneratedNameForIdentifier1);

void TestGeneratedNameForIdentifier2(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("function f() {}", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0]->name();
	auto* name1 = ec->factory.newGeneratedNameForNode(
	    n, printer::AutoGenerateOptions{.Prefix = "a", .Suffix = "b"});

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("afb"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForIdentifier2",
                   TestGeneratedNameForIdentifier2);

void TestGeneratedNameForIdentifier3(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("function f() {}", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0]->name();
	auto* name1 = ec->factory.newGeneratedNameForNode(
	    n, printer::AutoGenerateOptions{.Prefix = "a", .Suffix = "b"});
	auto* name2 = ec->factory.newGeneratedNameForNode(name1);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name2);

	gotest::assert::Equal(t, std::string("afb_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForIdentifier3",
                   TestGeneratedNameForIdentifier3);

// namespace reuses name if it does not collide with locals
void TestGeneratedNameForNamespace1(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("namespace foo { }", false /*jsx*/);
	bindSourceFile(file);

	auto* ns1 = file->Statements->nodes[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(ns1);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("foo"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForNamespace1",
                   TestGeneratedNameForNamespace1);

// namespace uses generated name if it collides with locals
void TestGeneratedNameForNamespace2(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("namespace foo { var foo; }",
	                                          false /*jsx*/);
	bindSourceFile(file);

	auto* ns1 = file->Statements->nodes[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(ns1);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("foo_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForNamespace2",
                   TestGeneratedNameForNamespace2);

// avoids collisions when unscoped
void TestGeneratedNameForNamespace3(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript(
	    "namespace ns1 { namespace foo { var foo; } } namespace ns2 { "
	    "namespace foo { var foo; } }",
	    false /*jsx*/);
	bindSourceFile(file);

	auto* ns1 = file->Statements->nodes[0]->body()->statements()[0];
	auto* ns2 = file->Statements->nodes[1]->body()->statements()[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(ns1);
	auto* name2 = ec->factory.newGeneratedNameForNode(ns2);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name2);

	gotest::assert::Equal(t, std::string("foo_1"), text1);
	gotest::assert::Equal(t, std::string("foo_2"), text2);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForNamespace3",
                   TestGeneratedNameForNamespace3);

// reuse name when scoped
void TestGeneratedNameForNamespace4(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript(
	    "namespace ns1 { namespace foo { var foo; } } namespace ns2 { "
	    "namespace foo { var foo; } }",
	    false /*jsx*/);
	bindSourceFile(file);

	auto* ns1 = file->Statements->nodes[0]->body()->statements()[0];
	auto* ns2 = file->Statements->nodes[1]->body()->statements()[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(ns1);
	auto* name2 = ec->factory.newGeneratedNameForNode(ns2);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	g.PushScope(false);
	auto text1 = g.GenerateName(name1);
	g.PopScope(false);

	g.PushScope(false);
	auto text2 = g.GenerateName(name2);
	g.PopScope(false);

	gotest::assert::Equal(t, std::string("foo_1"), text1);
	gotest::assert::Equal(t, std::string("foo_2"),
	                      text2); // Matches Strada, but is incorrect
	// gotest::assert::Equal(t, std::string("foo_1"), text2) // TODO: Fix after
	// Strada port is complete.
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForNamespace4",
                   TestGeneratedNameForNamespace4);

void TestGeneratedNameForNodeCached(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("namespace foo { var foo; }",
	                                          false /*jsx*/);
	bindSourceFile(file);

	auto* ns1 = file->Statements->nodes[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(ns1);
	auto* name2 = ec->factory.newGeneratedNameForNode(ns1);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);
	auto text2 = g.GenerateName(name2);

	gotest::assert::Equal(t, std::string("foo_1"), text1);
	gotest::assert::Equal(t, std::string("foo_1"), text2);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForNodeCached",
                   TestGeneratedNameForNodeCached);

void TestGeneratedNameForImport(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("import * as foo from 'foo'",
	                                          false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("foo_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForImport",
                   TestGeneratedNameForImport);

void TestGeneratedNameForExport(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("export * as foo from 'foo'",
	                                          false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("foo_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForExport",
                   TestGeneratedNameForExport);

void TestGeneratedNameForFunctionDeclaration1(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file =
	    parsetestutil::ParseTypeScript("export function f() {}", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("f_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForFunctionDeclaration1",
                   TestGeneratedNameForFunctionDeclaration1);

void TestGeneratedNameForFunctionDeclaration2(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("export default function () {}",
	                                          false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("default_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForFunctionDeclaration2",
                   TestGeneratedNameForFunctionDeclaration2);

void TestGeneratedNameForClassDeclaration1(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("export class C {}", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("C_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForClassDeclaration1",
                   TestGeneratedNameForClassDeclaration1);

void TestGeneratedNameForClassDeclaration2(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("export default class {}",
	                                          false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("default_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForClassDeclaration2",
                   TestGeneratedNameForClassDeclaration2);

void TestGeneratedNameForExportAssignment(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("export default 0", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("default_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForExportAssignment",
                   TestGeneratedNameForExportAssignment);

void TestGeneratedNameForClassExpression(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("(class {})", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0]->expression()->expression();
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("class_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForClassExpression",
                   TestGeneratedNameForClassExpression);

void TestGeneratedNameForMethod1(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file =
	    parsetestutil::ParseTypeScript("class C { m() {} }", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0]->members()[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("m_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForMethod1",
                   TestGeneratedNameForMethod1);

void TestGeneratedNameForMethod2(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file =
	    parsetestutil::ParseTypeScript("class C { 0() {} }", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0]->members()[0];
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("_a"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForMethod2",
                   TestGeneratedNameForMethod2);

void TestGeneratedPrivateNameForMethod(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file =
	    parsetestutil::ParseTypeScript("class C { m() {} }", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0]->members()[0];
	auto* name1 = ec->factory.newGeneratedPrivateNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("#m_1"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedPrivateNameForMethod",
                   TestGeneratedPrivateNameForMethod);

void TestGeneratedNameForComputedPropertyName(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("class C { [x] }", false /*jsx*/);
	bindSourceFile(file);

	auto* n = file->Statements->nodes[0]->members()[0]->name();
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("_a"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForComputedPropertyName",
                   TestGeneratedNameForComputedPropertyName);

void TestGeneratedNameForOther(T* t) {
	t->Parallel();

	auto* ec = printer::NewEmitContext();

	auto* file = parsetestutil::ParseTypeScript("class C { [x] }", false /*jsx*/);
	bindSourceFile(file);

	auto* n = ec->factory.newObjectLiteralExpression(
	    ec->factory.newNodeList({}),
	    false /*multiLine*/);
	auto* name1 = ec->factory.newGeneratedNameForNode(n);

	printer::NameGenerator g;
	g.Context = ec;
	g.GetTextOfNode = getTextOfNode;
	auto text1 = g.GenerateName(name1);

	gotest::assert::Equal(t, std::string("_a"), text1);
}
REGISTER_UNIT_TEST("printer.TestGeneratedNameForOther",
                   TestGeneratedNameForOther);

}  // namespace
