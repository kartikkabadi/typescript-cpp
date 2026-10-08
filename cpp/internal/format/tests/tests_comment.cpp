// Port of tsc/internal/format/comment_test.go.
#include <string>

#include "internal/ast/ast.h"
#include "internal/core/textchange.h"
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

static lsutil::FormatCodeSettings smartSpaces() {
	lsutil::FormatCodeSettings s;
	s.TabSize = 4;
	s.IndentSize = 4;
	s.BaseIndentSize = 4;
	s.NewLineCharacter = "\n";
	s.ConvertTabsToSpaces = Tristate::True;
	s.IndentStyle = lsutil::IndentStyleSmart;
	s.TrimTrailingWhitespace = Tristate::True;
	s.InsertSpaceBeforeTypeAnnotation = Tristate::True;
	return s;
}

static lsutil::FormatCodeSettings smartTabs() {
	lsutil::FormatCodeSettings s;
	s.TabSize = 4;
	s.IndentSize = 4;
	s.BaseIndentSize = 0;
	s.NewLineCharacter = "\n";
	s.ConvertTabsToSpaces = Tristate::False; // Use tabs
	s.IndentStyle = lsutil::IndentStyleSmart;
	s.TrimTrailingWhitespace = Tristate::True;
	s.InsertSpaceBeforeTypeAnnotation = Tristate::True;
	return s;
}

static bool contains(std::string_view haystack, std::string_view needle) {
	return haystack.find(needle) != std::string_view::npos;
}

static void TestCommentFormatting(T* t) {
	t->Parallel();

	t->Run("format comment issue reproduction", [](T* t) {
		t->Parallel();
		auto ctx = format::WithFormatCodeSettings(
		    format::FormatRequestContext{}, smartSpaces(), "\n");

		// Original code that causes the bug
		std::string originalText = R"TS(class C {
    /**
     *
    */
    async x() {}
})TS";

		SourceFileParseOptions parseOptions;
		parseOptions.FileName = "/test.ts";
		parseOptions.Path = "/test.ts";
		auto* sourceFile =
		    tsc::parseSourceFile(parseOptions, originalText, ScriptKind::TS);

		// Apply formatting once
		auto edits = format::FormatDocument(ctx, sourceFile);
		std::string firstFormatted = ApplyBulkEdits(originalText, edits);

		// Check that the asterisk is not corrupted
		gotest::assert::Check(t, !contains(firstFormatted, "*/\n   /"),
		                      "should not corrupt */ to /");
		gotest::assert::Check(t, contains(firstFormatted, "*/"),
		                      "should preserve */ token");
		gotest::assert::Check(t, contains(firstFormatted, "async"),
		                      "should preserve async keyword");

		// Apply formatting a second time to test stability
		auto* sourceFile2 =
		    tsc::parseSourceFile(parseOptions, firstFormatted, ScriptKind::TS);

		auto edits2 = format::FormatDocument(ctx, sourceFile2);
		std::string secondFormatted = ApplyBulkEdits(firstFormatted, edits2);

		// Check that second formatting doesn't introduce corruption
		gotest::assert::Check(t, !contains(secondFormatted, " sync x()"),
		                      "should not corrupt async to sync");
		gotest::assert::Check(
		    t, contains(secondFormatted, "async"),
		    "should preserve async keyword on second pass");
	});

	t->Run("format JSDoc with tab indentation", [](T* t) {
		t->Parallel();
		auto ctx = format::WithFormatCodeSettings(
		    format::FormatRequestContext{}, smartTabs(), "\n");

		// Original code with tab indentation (tabs represented as \t)
		std::string originalText =
		    "class Foo {\n\t/**\n\t * @param {string} argument - This is a "
		    "param description.\n\t */\n\texample(argument) "
		    "{\nconsole.log(argument);\n\t}\n}";

		SourceFileParseOptions parseOptions;
		parseOptions.FileName = "/test.ts";
		parseOptions.Path = "/test.ts";
		auto* sourceFile =
		    tsc::parseSourceFile(parseOptions, originalText, ScriptKind::TS);

		// Apply formatting
		auto edits = format::FormatDocument(ctx, sourceFile);
		std::string formatted = ApplyBulkEdits(originalText, edits);

		// Check that tabs come before spaces (not spaces before tabs)
		// The comment lines should have format: tab followed by space and
		// asterisk. NOT: space followed by tab and asterisk
		gotest::assert::Check(
		    t, !contains(formatted, " \t*"),
		    "should not have space before tab before asterisk");
		gotest::assert::Check(t, contains(formatted, "\t *"),
		                      "should have tab before space before asterisk");

		// Verify console.log is properly indented with tabs
		gotest::assert::Check(
		    t, contains(formatted, "\t\tconsole.log"),
		    "console.log should be indented with two tabs");
	});

	t->Run("format comment inside multi-line argument list", [](T* t) {
		t->Parallel();
		auto ctx = format::WithFormatCodeSettings(
		    format::FormatRequestContext{}, smartTabs(), "\n");

		// Original code with proper indentation
		std::string originalText =
		    "console.log(\n\t\"a\",\n\t// the second arg\n\t\"b\"\n);";

		SourceFileParseOptions parseOptions;
		parseOptions.FileName = "/test.ts";
		parseOptions.Path = "/test.ts";
		auto* sourceFile =
		    tsc::parseSourceFile(parseOptions, originalText, ScriptKind::TS);

		// Apply formatting
		auto edits = format::FormatDocument(ctx, sourceFile);
		std::string formatted = ApplyBulkEdits(originalText, edits);

		// The comment should remain indented with a tab
		gotest::assert::Check(t, contains(formatted, "\t// the second arg"),
		                      "comment should be indented with tab");
		// The comment should not lose its indentation
		gotest::assert::Check(
		    t, !contains(formatted, "\n// the second arg"),
		    "comment should not lose indentation");
	});

	t->Run("format comment in chained method calls", [](T* t) {
		t->Parallel();
		auto ctx = format::WithFormatCodeSettings(
		    format::FormatRequestContext{}, smartTabs(), "\n");

		// Original code with proper indentation
		std::string originalText =
		    "foo\n\t.bar()\n\t// A second call\n\t.baz();";

		SourceFileParseOptions parseOptions;
		parseOptions.FileName = "/test.ts";
		parseOptions.Path = "/test.ts";
		auto* sourceFile =
		    tsc::parseSourceFile(parseOptions, originalText, ScriptKind::TS);

		// Apply formatting
		auto edits = format::FormatDocument(ctx, sourceFile);
		std::string formatted = ApplyBulkEdits(originalText, edits);

		// The comment should remain indented
		gotest::assert::Check(
		    t,
		    contains(formatted, "\t// A second call") ||
		        contains(formatted, "   // A second call"),
		    "comment should be indented");
		// The comment should not lose its indentation
		gotest::assert::Check(
		    t, !contains(formatted, "\n// A second call"),
		    "comment should not lose indentation");
	});

	// Regression test for issue #1928 - panic when formatting chained method
	// call with comment
	t->Run("format chained method call with comment (issue #1928)", [](T* t) {
		t->Parallel();
		auto ctx = format::WithFormatCodeSettings(
		    format::FormatRequestContext{}, smartTabs(), "\n");

		// This code previously caused a panic with "strings: negative Repeat
		// count" because tokenIndentation was -1 and was being used directly
		// for indentation
		std::string originalText =
		    "foo\n\t.bar()\n\t// A second call\n\t.baz();";

		SourceFileParseOptions parseOptions;
		parseOptions.FileName = "/test.ts";
		parseOptions.Path = "/test.ts";
		auto* sourceFile =
		    tsc::parseSourceFile(parseOptions, originalText, ScriptKind::TS);

		// Apply formatting - should not panic
		auto edits = format::FormatDocument(ctx, sourceFile);
		std::string formatted = ApplyBulkEdits(originalText, edits);

		// Verify the comment maintains proper indentation and doesn't lose it
		gotest::assert::Check(
		    t,
		    contains(formatted, "\t// A second call") ||
		        contains(formatted, "   // A second call"),
		    "comment should be indented");
		gotest::assert::Check(t, !contains(formatted, "\n// A second call"),
		                      "comment should not be at column 0");
	});

	t->Run(
	    "multiline comment inside block that opens on first line (issue "
	    "#2649)",
	    [](T* t) {
		    t->Parallel();
		    lsutil::FormatCodeSettings settings;
		    settings.TabSize = 4;
		    settings.IndentSize = 4;
		    settings.BaseIndentSize = 0;
		    settings.NewLineCharacter = "\n";
		    settings.ConvertTabsToSpaces = Tristate::False;
		    settings.IndentStyle = lsutil::IndentStyleSmart;
		    settings.TrimTrailingWhitespace = Tristate::True;
		    auto ctx = format::WithFormatCodeSettings(
		        format::FormatRequestContext{}, settings, "\n");

		    std::string originalText =
		        "document.addEventListener('DOMContentLoaded', () => {\n"
		        "    /** @type {NodeListOf<HTMLSpanElement>} */\n"
		        "    const elements = document.querySelectorAll('.test')\n"
		        "});";

		    SourceFileParseOptions parseOptions;
		    parseOptions.FileName = "/test.js";
		    parseOptions.Path = "/test.js";
		    auto* sourceFile = tsc::parseSourceFile(
		        parseOptions, originalText, ScriptKind::JS);

		    auto edits = format::FormatDocument(ctx, sourceFile);
		    std::string formatted = ApplyBulkEdits(originalText, edits);
		    gotest::assert::Check(t, !formatted.empty(),
		                          "formatted text should not be empty");
	    });

	t->Run(
	    "single-line comment inside block that opens on first line (issue "
	    "#2649)",
	    [](T* t) {
		    t->Parallel();
		    lsutil::FormatCodeSettings settings;
		    settings.TabSize = 4;
		    settings.IndentSize = 4;
		    settings.BaseIndentSize = 0;
		    settings.NewLineCharacter = "\n";
		    settings.ConvertTabsToSpaces = Tristate::False;
		    settings.IndentStyle = lsutil::IndentStyleSmart;
		    settings.TrimTrailingWhitespace = Tristate::True;
		    auto ctx = format::WithFormatCodeSettings(
		        format::FormatRequestContext{}, settings, "\n");

		    std::string originalText =
		        "document.addEventListener('DOMContentLoaded', () => {\n"
		        "    // a comment\n"
		        "    const x = 1\n"
		        "});";

		    SourceFileParseOptions parseOptions;
		    parseOptions.FileName = "/test.ts";
		    parseOptions.Path = "/test.ts";
		    auto* sourceFile = tsc::parseSourceFile(
		        parseOptions, originalText, ScriptKind::TS);

		    auto edits = format::FormatDocument(ctx, sourceFile);
		    std::string formatted = ApplyBulkEdits(originalText, edits);
		    gotest::assert::Check(t, !formatted.empty(),
		                          "formatted text should not be empty");
	    });
}
REGISTER_UNIT_TEST("format.TestCommentFormatting", TestCommentFormatting);

static void TestFormatSelectionPreservesComments(T* t) {
	t->Parallel();

	t->Run(
	    "format selection should not delete block comment when selection ends "
	    "inside comment",
	    [](T* t) {
		    t->Parallel();
		    lsutil::FormatCodeSettings settings;
		    settings.TabSize = 4;
		    settings.IndentSize = 4;
		    settings.BaseIndentSize = 0;
		    settings.NewLineCharacter = "\n";
		    settings.ConvertTabsToSpaces = Tristate::True;
		    settings.IndentStyle = lsutil::IndentStyleSmart;
		    settings.TrimTrailingWhitespace = Tristate::True;
		    auto ctx = format::WithFormatCodeSettings(
		        format::FormatRequestContext{}, settings, "\n");

		    // Reproduce: const test/* comment */=5;
		    // When selecting a range that ends inside the comment (before
		    // */), format selection should not delete the comment.
		    std::string originalText = "const test/* comment */=5;";

		    SourceFileParseOptions parseOptions;
		    parseOptions.FileName = "/test.ts";
		    parseOptions.Path = "/test.ts";
		    auto* sourceFile = tsc::parseSourceFile(
		        parseOptions, originalText, ScriptKind::TS);

		    // Select a range that starts at the beginning of the line and
		    // ends inside the block comment. This covers
		    // `const test/* comment`, stopping before the closing `*/`.
		    auto commentStart = originalText.find("/*");
		    int selectionEnd =
		        int(commentStart) + int(std::strlen("/* comment"));

		    auto edits = format::FormatSelection(ctx, sourceFile, 0,
		                                         selectionEnd);
		    std::string formatted = ApplyBulkEdits(originalText, edits);

		    // The entire statement should be preserved unchanged
		    gotest::assert::Equal(
		        t, formatted, originalText,
		        "format selection should not delete the block comment or "
		        "alter the statement");
	    });

	t->Run(
	    "format selection should not delete block comment when selection "
	    "starts inside comment",
	    [](T* t) {
		    t->Parallel();
		    lsutil::FormatCodeSettings settings;
		    settings.TabSize = 4;
		    settings.IndentSize = 4;
		    settings.BaseIndentSize = 0;
		    settings.NewLineCharacter = "\n";
		    settings.ConvertTabsToSpaces = Tristate::True;
		    settings.IndentStyle = lsutil::IndentStyleSmart;
		    settings.TrimTrailingWhitespace = Tristate::True;
		    auto ctx = format::WithFormatCodeSettings(
		        format::FormatRequestContext{}, settings, "\n");

		    std::string originalText = "const test/* comment */=5;";

		    SourceFileParseOptions parseOptions;
		    parseOptions.FileName = "/test.ts";
		    parseOptions.Path = "/test.ts";
		    auto* sourceFile = tsc::parseSourceFile(
		        parseOptions, originalText, ScriptKind::TS);

		    // Select from inside the comment to the end
		    auto commentStart = originalText.find("/*");
		    int selectionStart = int(commentStart) + 3; // inside the comment

		    auto edits = format::FormatSelection(
		        ctx, sourceFile, selectionStart, int(originalText.size()));
		    std::string formatted = ApplyBulkEdits(originalText, edits);

		    // The entire statement should be preserved unchanged
		    gotest::assert::Equal(
		        t, formatted, originalText,
		        "format selection should not delete the block comment or "
		        "alter the statement");
	    });

	t->Run(
	    "full document format should preserve block comment and add spaces",
	    [](T* t) {
		    t->Parallel();
		    lsutil::FormatCodeSettings settings;
		    settings.TabSize = 4;
		    settings.IndentSize = 4;
		    settings.BaseIndentSize = 0;
		    settings.NewLineCharacter = "\n";
		    settings.ConvertTabsToSpaces = Tristate::True;
		    settings.IndentStyle = lsutil::IndentStyleSmart;
		    settings.TrimTrailingWhitespace = Tristate::True;
		    settings.InsertSpaceBeforeAndAfterBinaryOperators =
		        Tristate::True;
		    auto ctx = format::WithFormatCodeSettings(
		        format::FormatRequestContext{}, settings, "\n");

		    std::string originalText = "const test/* comment */=5;";

		    SourceFileParseOptions parseOptions;
		    parseOptions.FileName = "/test.ts";
		    parseOptions.Path = "/test.ts";
		    auto* sourceFile = tsc::parseSourceFile(
		        parseOptions, originalText, ScriptKind::TS);

		    auto edits = format::FormatDocument(ctx, sourceFile);
		    std::string formatted = ApplyBulkEdits(originalText, edits);

		    // Full document format should preserve the comment and add
		    // spaces around `=`
		    gotest::assert::Equal(
		        t, formatted, std::string("const test/* comment */ = 5;"),
		        "full format should preserve the block comment and add "
		        "spaces");
	    });
}
REGISTER_UNIT_TEST("format.TestFormatSelectionPreservesComments",
                   TestFormatSelectionPreservesComments);

static void TestSliceBoundsPanic(T* t) {
	t->Parallel();

	t->Run("format code with trailing semicolon should not panic", [](T* t) {
		t->Parallel();
		auto ctx = format::WithFormatCodeSettings(
		    format::FormatRequestContext{}, smartSpaces(), "\n");

		// Code from the issue that causes slice bounds panic
		std::string originalText =
		    "const _enableDisposeWithListenerWarning = false\n"
		    "\t// || Boolean(\"TRUE\") // causes a linter warning so that it "
		    "cannot be pushed\n"
		    "\t;\n";

		SourceFileParseOptions parseOptions;
		parseOptions.FileName = "/test.ts";
		parseOptions.Path = "/test.ts";
		auto* sourceFile =
		    tsc::parseSourceFile(parseOptions, originalText, ScriptKind::TS);

		// This should not panic
		auto edits = format::FormatDocument(ctx, sourceFile);
		std::string formatted = ApplyBulkEdits(originalText, edits);

		// Basic sanity checks
		gotest::assert::Check(t, !formatted.empty(),
		                      "formatted text should not be empty");
		gotest::assert::Check(
		    t, contains(formatted, "_enableDisposeWithListenerWarning"),
		    "should preserve variable name");
	});
}
REGISTER_UNIT_TEST("format.TestSliceBoundsPanic", TestSliceBoundsPanic);
