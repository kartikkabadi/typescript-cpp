// Port of tsc/internal/format/indent_getindentation_test.go.
#include <string>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/format/format.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/parser/parser.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;
namespace format = tsc::format;
namespace lsutil = tsc::ls::lsutil;

static void TestGetIndentationForNamedImportsPosition(T* t) {
	t->Parallel();

	std::string text =
	    "import {\n    type SomeInterface,\n} from \"./exports.js\";";
	// Position 9: \n
	// Position 10: first space of "    type SomeInterface"

	SourceFileParseOptions parseOptions;
	parseOptions.FileName = "/test.ts";
	parseOptions.Path = "/test.ts";
	auto* sourceFile =
	    tsc::parseSourceFile(parseOptions, text, ScriptKind::TS);

	auto options = lsutil::GetDefaultFormatCodeSettings();

	// The line that contains "    type SomeInterface" starts at position 9 (the \n).
	// The getAdjustedStartPosition with LeadingTriviaOptionNone returns line start.
	// Let's test at position 9 (start of line containing the specifier)
	int lineStart = format::GetLineStartPositionForPosition(
	    14, sourceFile); // 14 is somewhere in "    type"

	int indent = format::GetIndentation(lineStart, sourceFile, options, true);
	t->Logf("lineStart=%d, text[lineStart:]=%q",
	        {lineStart, text.substr(size_t(lineStart), 10)});
	t->Logf("GetIndentation at lineStart %d = %d", {lineStart, indent});

	if (indent != 4) {
		t->Errorf("Expected indentation 4, got %d", {indent});
	}
}
REGISTER_UNIT_TEST("format.TestGetIndentationForNamedImportsPosition",
                   TestGetIndentationForNamedImportsPosition);
