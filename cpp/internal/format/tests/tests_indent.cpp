// Port of tsc/internal/format/indent_test.go.
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/format/format.h"
#include "internal/gostd/testing.h"
#include "internal/parser/parser.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;
namespace format = tsc::format;

static void forEachDescendantOfKind(
    Node* node, Kind kind, const std::function<void(Node*)>& action) {
	node->forEachChild([&](Node* child) -> bool {
		if (child->kind == kind) {
			action(child);
		}
		forEachDescendantOfKind(child, kind, action);
		return false;
	});
}

static void TestGetContainingList_NamedImports(T* t) {
	t->Parallel();

	const char* text = R"TS(import type {
    AAA,
    BBB,
} from "./bar";)TS";

	SourceFileParseOptions parseOptions;
	parseOptions.FileName = "/test.ts";
	parseOptions.Path = "/test.ts";
	auto* sourceFile =
	    tsc::parseSourceFile(parseOptions, text, ScriptKind::TS);

	// Find ImportSpecifier nodes (AAA and BBB)
	std::vector<Node*> importSpecifiers;
	forEachDescendantOfKind(sourceFile->asNode(), Kind::ImportSpecifier,
	                        [&](Node* node) {
		                        importSpecifiers.push_back(node);
	                        });

	gotest::assert::Assert(
	    t, importSpecifiers.size() == 2,
	    gostd::sprintf("Expected 2 import specifiers, got %d",
	                   {int(importSpecifiers.size())}));

	// Test GetContainingList for each import specifier
	for (auto* specifier : importSpecifiers) {
		auto* list = format::GetContainingList(specifier, sourceFile);
		gotest::assert::Assert(
		    t, list != nullptr,
		    "GetContainingList should return non-nil for import specifier");
		gotest::assert::Assert(
		    t, list->nodes.size() == 2,
		    gostd::sprintf("Expected list with 2 elements, got %d",
		                   {int(list->nodes.size())}));
	}
}
REGISTER_UNIT_TEST("format.TestGetContainingList_NamedImports",
                   TestGetContainingList_NamedImports);
