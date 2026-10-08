// Port of tsc/internal/format/api_test.go.
// (BenchmarkFormat is a Go benchmark and does not run under `go test`; it is
// not ported.)
#include <fstream>
#include <sstream>
#include <string>

#include "internal/ast/ast.h"
#include "internal/core/textchange.h"
#include "internal/core/types.h"
#include "internal/format/format.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/parser/parser.h"
#include "internal/repo/paths.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;
namespace format = tsc::format;
namespace lsutil = tsc::ls::lsutil;

static void TestFormat(T* t) {
	t->Parallel();

	t->Run("format checker.ts", [](T* t) {
		t->Parallel();
		lsutil::FormatCodeSettings settings;
		settings.TabSize = 4;
		settings.IndentSize = 4;
		settings.BaseIndentSize = 4;
		settings.NewLineCharacter = "\n";
		settings.ConvertTabsToSpaces = Tristate::True;
		settings.IndentStyle = lsutil::IndentStyleSmart;
		settings.TrimTrailingWhitespace = Tristate::True;
		settings.InsertSpaceBeforeTypeAnnotation = Tristate::True;
		auto ctx = format::WithFormatCodeSettings(
		    format::FormatRequestContext{}, settings, "\n");
		std::string filePath =
		    repo::testDataPath() + "/fixtures/compiler/checker.ts";
		std::ifstream in(filePath);
		if (!in) {
			t->Fatalf("read checker.ts: %s", {filePath});
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		std::string text = ss.str();
		SourceFileParseOptions parseOptions;
		parseOptions.FileName = "/checker.ts";
		parseOptions.Path = "/checker.ts";
		auto* sourceFile =
		    tsc::parseSourceFile(parseOptions, text, ScriptKind::TS);
		auto edits = format::FormatDocument(ctx, sourceFile);
		std::string newText = ApplyBulkEdits(text, edits);
		gotest::assert::Assert(t, !newText.empty());
		gotest::assert::Assert(t, text != newText);
	});
}
REGISTER_UNIT_TEST("format.TestFormat", TestFormat);
