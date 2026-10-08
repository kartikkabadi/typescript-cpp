// Port of tsc/internal/format/format_test.go.
#include <string>

#include "internal/ast/ast.h"
#include "internal/core/textchange.h"
#include "internal/core/types.h"
#include "internal/format/format.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/parser/parser.h"
#include "internal/stringutil/stringutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;
namespace format = tsc::format;
namespace lsutil = tsc::ls::lsutil;

static void TestFormatNoTrailingSpace(T* t) {
	t->Parallel();

	struct {
		const char* name;
		const char* text;
	} testCases[] = {
	    {"simple statement without trailing newline", "1;"},
	    {"function call without trailing newline", "console.log('hello');"},
	    {"if block on single line", "if (true) { }"},
	    {"class declaration", "class A {\n    // Class Contents Go Here\n}"},
	    {"class declaration with trailing newline",
	     "class A {\n    // Class Contents Go Here\n}\n"},
	    {"empty block", "if (true) {}"},
	    {"module declaration", "module M { }"},
	    {"enum declaration", "enum E { A, B }"},
	};

	for (auto& tc : testCases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			lsutil::FormatCodeSettings settings;
			settings.TabSize = 4;
			settings.IndentSize = 4;
			settings.NewLineCharacter = "\n";
			settings.ConvertTabsToSpaces = Tristate::True;
			settings.IndentStyle = lsutil::IndentStyleSmart;
			settings.TrimTrailingWhitespace = Tristate::True;
			auto ctx = format::WithFormatCodeSettings(
			    format::FormatRequestContext{}, settings, "\n");
			SourceFileParseOptions parseOptions;
			parseOptions.FileName = "/test.ts";
			parseOptions.Path = "/test.ts";
			auto* sourceFile = tsc::parseSourceFile(
			    parseOptions, tc.text, ScriptKind::TS);
			auto edits = format::FormatDocument(ctx, sourceFile);
			std::string newText = ApplyBulkEdits(tc.text, edits);
			// Formatting should not add trailing whitespace at end of file
			// (strings.Split(newText, "\n")).
			int i = 0;
			size_t pos = 0;
			while (true) {
				size_t nl = newText.find('\n', pos);
				std::string line = newText.substr(
				    pos, nl == std::string::npos
				             ? std::string::npos : nl - pos);
				auto trimmedEnd = line.find_last_not_of(" \t");
				std::string trimmed =
				    trimmedEnd == std::string::npos
				        ? ""
				        : line.substr(0, trimmedEnd + 1);
				gotest::assert::Equal(
				    t, line, trimmed,
				    gostd::sprintf(
				        "Formatter should not add trailing whitespace on "
				        "line %d",
				        {i + 1}));
				i++;
				if (nl == std::string::npos) break;
				pos = nl + 1;
			}
		});
	}
}
REGISTER_UNIT_TEST("format.TestFormatNoTrailingSpace",
                   TestFormatNoTrailingSpace);
