// Port of tsc/internal/ast/symbol_test.go.
// TestSymbolFieldsArePrivate has no C++ analog: it reflects over Go's
// private fields, while the port keeps `flags`/`checkFlags`/`id`/`data`
// public by design. The accessor-contract tests below map 1:1.
#include <string>

#include "internal/ast/ast.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;

static void TestSymbolAccessors(T* t) {
	t->Parallel();
	Symbol* symbol = newSymbol();
	if (symbol->flags != SymbolFlagsNone) t->Error({"flags != None"});
	if (symbol->checkFlags != CheckFlags(0)) t->Error({"checkFlags != 0"});
	if (!symbol->data->name.empty()) t->Error({"name not empty"});
	if (!symbol->data->declarations.empty())
		t->Error({"declarations not empty"});
	if (symbol->data->valueDeclaration != nullptr)
		t->Error({"valueDeclaration != nil"});
	if (!symbol->data->members.empty()) t->Error({"members not empty"});
	if (!symbol->data->exports.empty()) t->Error({"exports not empty"});
	if (symbol->data->parent != nullptr) t->Error({"parent != nil"});
	if (symbol->data->exportSymbol != nullptr)
		t->Error({"exportSymbol != nil"});

	auto* declaration = new Node();
	Symbol* member = newSymbol();
	Symbol* exportSym = newSymbol();
	Symbol* parent = newSymbol();
	Symbol* exportSymbol = newSymbol();
	std::vector<Node*> declarations{declaration};
	SymbolTable members{{"member", member}};
	SymbolTable exports{{"export", exportSym}};

	symbol->flags = SymbolFlagsClass | SymbolFlagsTransient;
	symbol->checkFlags = CheckFlagsReadonly;
	symbol->data->name = "C";
	symbol->data->declarations = declarations;
	symbol->data->valueDeclaration = declaration;
	symbol->data->members = members;
	symbol->data->exports = exports;
	symbol->data->parent = parent;
	symbol->data->exportSymbol = exportSymbol;

	if (symbol->flags != (SymbolFlagsClass | SymbolFlagsTransient))
		t->Error({"flags mismatch"});
	if (symbol->checkFlags != CheckFlagsReadonly)
		t->Error({"checkFlags mismatch"});
	if (symbol->data->name != "C") t->Error({"name != C"});
	if (symbol->data->declarations[0] != declaration)
		t->Error({"declarations[0] != declaration"});
	if (symbol->data->valueDeclaration != declaration)
		t->Error({"valueDeclaration != declaration"});
	if (symbol->data->members["member"] != member)
		t->Error({"members[member] != member"});
	if (symbol->data->exports["export"] != exportSym)
		t->Error({"exports[export] != export"});
	if (symbol->data->parent != parent) t->Error({"parent != parent"});
	if (symbol->data->exportSymbol != exportSymbol)
		t->Error({"exportSymbol != exportSymbol"});

	// Go's slice-identity assertions are dropped: SetDeclarations stores a
	// slice (shared backing array) while the port stores a std::vector
	// (copy semantics). Data sharing that matters — members/exports maps
	// and whole-data sharing via setSymbolData — is covered below and by
	// the instantiateSymbol path.
	symbol->data->members["added"] = exportSym;
	if (symbol->data->members["added"] != exportSym)
		t->Error({"added member missing"});
	symbol->data->exports["added"] = member;
	if (symbol->data->exports["added"] != member)
		t->Error({"added export missing"});
}
REGISTER_UNIT_TEST("ast.TestSymbolAccessors", TestSymbolAccessors);

static void TestSymbolTableInitialization(T* t) {
	t->Parallel();
	Symbol* symbol = newSymbol();
	Symbol* member = newSymbol();
	Symbol* exportSym = newSymbol();

	auto& members = getMembers(symbol);
	members["member"] = member;
	if (symbol->data->members["member"] != member)
		t->Error({"members[member] != member"});
	if (getMembers(symbol)["member"] != member)
		t->Error({"getMembers[member] != member"});

	auto& exports = getExports(symbol);
	exports["export"] = exportSym;
	if (symbol->data->exports["export"] != exportSym)
		t->Error({"exports[export] != export"});
	if (getExports(symbol)["export"] != exportSym)
		t->Error({"getExports[export] != export"});
}
REGISTER_UNIT_TEST("ast.TestSymbolTableInitialization",
                   TestSymbolTableInitialization);
