// Port of tsc/internal/ls/format_test.go.
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/core/utilities.h"
#include "internal/gostd/testing.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/ls/tests/testaccess.h"
#include "internal/parser/parser.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc::ls;
using namespace tsc;

static void TestNonOverlappingFormattingRanges(T* t) {
	t->Parallel();
	struct testCase {
		std::string name;
		std::vector<TextRange> candidates;
		std::vector<TextRange> want;
	};
	std::vector<testCase> tests = {
	    {"sorts disjoint ranges",
	     {TextRange{10, 15}, TextRange{0, 5}},
	     {TextRange{0, 5}, TextRange{10, 15}}},
	    {"prefers longest range with same start",
	     {TextRange{0, 10}, TextRange{0, 20}},
	     {TextRange{0, 20}}},
	    {"discards fully covered range",
	     {TextRange{5, 15}, TextRange{0, 20}},
	     {TextRange{0, 20}}},
	    {"trims overlapping prefix",
	     {TextRange{5, 15}, TextRange{0, 10}},
	     {TextRange{0, 10}, TextRange{10, 15}}},
	};
	for (auto& test : tests) {
		t->Run(test.name, [test](T* t) {
			t->Parallel();
			auto candidates = Map(
			    test.candidates, [](const TextRange& r) {
				    mappedFormattingRange m;
				    m.originalRange = r;
				    return m;
			    });
			auto result = Map(
			    nonOverlappingFormattingRanges(candidates),
			    [](const mappedFormattingRange& r) {
				    return r.originalRange;
			    });
			gotest::assert::Equal(t, result, test.want);
		});
	}
}
REGISTER_UNIT_TEST("ls.TestNonOverlappingFormattingRanges",
                   TestNonOverlappingFormattingRanges);

// Test for issue: Panic Handling textDocument/onTypeFormatting
// This reproduces the panic when pressing enter in an empty file
static void TestGetFormattingEditsAfterKeystroke_EmptyFile(T* t) {
	t->Parallel();
	// Create an empty file
	std::string text = "";
	auto* sourceFile = parseSourceFile(
	    SourceFileParseOptions{.FileName = "/index.ts",
	                           .Path = "/index.ts"},
	    text, ScriptKind::TS);

	// Create language service with nil program (we're only testing the
	// formatting function)
	auto* langService = LSTestAccess::NewEmpty();

	// Test formatting after keystroke with newline character at position 0
	auto ctx = gostd::contextBackground();
	auto options = lsutil::GetDefaultFormatCodeSettings();

	// This should not panic
	auto edits = LSTestAccess::getFormattingEditsAfterKeystroke(
	    langService, ctx, sourceFile, options,
	    0,   // position
	    "\n");

	// Should return nil or empty edits, not panic
	(void)edits;
}
REGISTER_UNIT_TEST("ls.TestGetFormattingEditsAfterKeystroke_EmptyFile",
                   TestGetFormattingEditsAfterKeystroke_EmptyFile);

// Test with a simple statement
static void TestGetFormattingEditsAfterKeystroke_SimpleStatement(T* t) {
	t->Parallel();
	// Create a file with a simple statement
	std::string text = "const x = 1";
	auto* sourceFile = parseSourceFile(
	    SourceFileParseOptions{.FileName = "/index.ts",
	                           .Path = "/index.ts"},
	    text, ScriptKind::TS);

	// Create language service with nil program
	auto* langService = LSTestAccess::NewEmpty();

	// Test formatting after keystroke with newline character at end of
	// statement
	auto ctx = gostd::contextBackground();
	auto options = lsutil::GetDefaultFormatCodeSettings();

	// This should not panic
	auto edits = LSTestAccess::getFormattingEditsAfterKeystroke(
	    langService, ctx, sourceFile, options,
	    int(text.size()),  // position at end of file
	    "\n");

	// Should return nil or empty edits, not panic
	(void)edits;
}
REGISTER_UNIT_TEST("ls.TestGetFormattingEditsAfterKeystroke_SimpleStatement",
                   TestGetFormattingEditsAfterKeystroke_SimpleStatement);

// Test for issue: Crash in range formatting when requested on a line that is
// different from the containing function. This reproduces the panic when
// formatting a range inside a function body.
static void TestGetFormattingEditsForRange_FunctionBody(T* t) {
	t->Parallel();
	struct testCase {
		std::string name;
		std::string text;
		int startPos;
		int endPos;
	};
	std::vector<testCase> testCases = {
	    {"return statement in function",
	     "function foo() {\n    return (1  + 2);\n}",
	     21,  // Start of "return"
	     38}, // End of ");"
	    {"function with newline after keyword",
	     "function\nf() {\n}",
	     9,  // After "function\n"
	     13},// Inside or after function
	    {"empty function body",
	     "function f() {\n  \n}",
	     15, // Inside body
	     17},// Inside body
	    {"after function closing brace",
	     "function f() {\n}",
	     15, // After closing brace
	     15},
	};

	for (auto& tc : testCases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			auto* sourceFile = parseSourceFile(
			    SourceFileParseOptions{.FileName = "/test.ts",
			                           .Path = "/test.ts"},
			    tc.text, ScriptKind::TS);

			auto* langService = LSTestAccess::NewEmpty();
			auto ctx = gostd::contextBackground();
			auto options = lsutil::GetDefaultFormatCodeSettings();

			// This should not panic
			auto edits = LSTestAccess::getFormattingEditsForRange(
			    langService, ctx, sourceFile, options,
			    TextRange{tc.startPos, tc.endPos});

			// Should not panic
			(void)edits;  // Just ensuring no panic
		});
	}
}
REGISTER_UNIT_TEST("ls.TestGetFormattingEditsForRange_FunctionBody",
                   TestGetFormattingEditsForRange_FunctionBody);
