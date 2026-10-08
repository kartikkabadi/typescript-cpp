// Port of tsc/internal/printer/utilities_test.go.
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/printer/printer.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;
using tsc::printer::EscapeString;
using tsc::printer::escapeJsxAttributeString;
using tsc::printer::escapeNonAsciiString;
using tsc::printer::IsRecognizedTripleSlashComment;
using tsc::printer::QuoteChar;
using tsc::printer::QuoteCharBacktick;
using tsc::printer::QuoteCharDoubleQuote;
using tsc::printer::QuoteCharSingleQuote;

namespace {

void TestEscapeString(T* t) {
	t->Parallel();
	struct {
		std::string s;
		QuoteChar quoteChar;
		std::string expected;
	} data[] = {
	    {"", QuoteCharDoubleQuote, ""},
	    {"abc", QuoteCharDoubleQuote, "abc"},
	    {"ab\"c", QuoteCharDoubleQuote, "ab\\\"c"},
	    {"ab\tc", QuoteCharDoubleQuote, "ab\\tc"},
	    {"ab\nc", QuoteCharDoubleQuote, "ab\\nc"},
	    {"ab'c", QuoteCharDoubleQuote, "ab'c"},
	    {"ab'c", QuoteCharSingleQuote, "ab\\'c"},
	    {"ab\"c", QuoteCharSingleQuote, "ab\"c"},
	    {"ab`c", QuoteCharBacktick, "ab\\`c"},
	    {"\x1f", QuoteCharBacktick, "\\u001F"},
	};
	for (int i = 0; i < (int)std::size(data); ++i) {
		auto rec = data[i];
		t->Run(gostd::sprintf("[%d] escapeString(%q, %v)",
		                      {i, rec.s, std::string(1, static_cast<char>(rec.quoteChar))}),
		       [rec](T* t) {
			       t->Parallel();
			       auto actual = EscapeString(rec.s, rec.quoteChar);
			       gotest::assert::Equal(t, actual, rec.expected);
		       });
	}
}
REGISTER_UNIT_TEST("printer.TestEscapeString", TestEscapeString);

void TestEscapeNonAsciiString(T* t) {
	t->Parallel();
	struct {
		std::string s;
		QuoteChar quoteChar;
		std::string expected;
	} data[] = {
	    {"", QuoteCharDoubleQuote, ""},
	    {"abc", QuoteCharDoubleQuote, "abc"},
	    {"ab\"c", QuoteCharDoubleQuote, "ab\\\"c"},
	    {"ab\tc", QuoteCharDoubleQuote, "ab\\tc"},
	    {"ab\nc", QuoteCharDoubleQuote, "ab\\nc"},
	    {"ab'c", QuoteCharDoubleQuote, "ab'c"},
	    {"ab'c", QuoteCharSingleQuote, "ab\\'c"},
	    {"ab\"c", QuoteCharSingleQuote, "ab\"c"},
	    {"ab`c", QuoteCharBacktick, "ab\\`c"},
	    {"ab\xc2\x8f""c", QuoteCharDoubleQuote, "ab\\u008Fc"},
	    {"𝟘𝟙", QuoteCharDoubleQuote, "\\uD835\\uDFD8\\uD835\\uDFD9"},
	};
	for (int i = 0; i < (int)std::size(data); ++i) {
		auto rec = data[i];
		t->Run(gostd::sprintf("[%d] escapeNonAsciiString(%q, %v)",
		                      {i, rec.s, std::string(1, static_cast<char>(rec.quoteChar))}),
		       [rec](T* t) {
			       t->Parallel();
			       auto actual = escapeNonAsciiString(rec.s, rec.quoteChar);
			       gotest::assert::Equal(t, actual, rec.expected);
		       });
	}
}
REGISTER_UNIT_TEST("printer.TestEscapeNonAsciiString", TestEscapeNonAsciiString);

void TestEscapeJsxAttributeString(T* t) {
	t->Parallel();
	struct {
		std::string s;
		QuoteChar quoteChar;
		std::string expected;
	} data[] = {
	    {"", QuoteCharDoubleQuote, ""},
	    {"abc", QuoteCharDoubleQuote, "abc"},
	    {"ab\"c", QuoteCharDoubleQuote, "ab&quot;c"},
	    {"ab\tc", QuoteCharDoubleQuote, "ab&#x9;c"},
	    {"ab\nc", QuoteCharDoubleQuote, "ab&#xA;c"},
	    {"ab'c", QuoteCharDoubleQuote, "ab'c"},
	    {"ab'c", QuoteCharSingleQuote, "ab&apos;c"},
	    {"ab\"c", QuoteCharSingleQuote, "ab\"c"},
	    {"ab\xc2\x8f""c", QuoteCharDoubleQuote, "ab\xc2\x8f""c"},
	    {"𝟘𝟙", QuoteCharDoubleQuote, "𝟘𝟙"},
	};
	for (int i = 0; i < (int)std::size(data); ++i) {
		auto rec = data[i];
		t->Run(gostd::sprintf("[%d] escapeJsxAttributeString(%q, %v)",
		                      {i, rec.s, std::string(1, static_cast<char>(rec.quoteChar))}),
		       [rec](T* t) {
			       t->Parallel();
			       auto actual = escapeJsxAttributeString(rec.s, rec.quoteChar);
			       gotest::assert::Equal(t, actual, rec.expected);
		       });
	}
}
REGISTER_UNIT_TEST("printer.TestEscapeJsxAttributeString",
                   TestEscapeJsxAttributeString);

void TestIsRecognizedTripleSlashComment(T* t) {
	t->Parallel();
	struct {
		std::string s;
		CommentRange commentRange;
		bool expected;
	} data[] = {
	    {"", {{0, 0}, Kind::MultiLineCommentTrivia}, false},
	    {"", {{0, 0}, Kind::SingleLineCommentTrivia}, false},
	    {"/a", {}, false},
	    {"//", {}, false},
	    {"//a", {}, false},
	    {"///", {}, false},
	    {"///a", {}, false},
	    {"///<reference path=\"foo\" />", {}, true},
	    {"///<reference types=\"foo\" />", {}, true},
	    {"///<reference lib=\"foo\" />", {}, true},
	    {"///<reference no-default-lib=\"foo\" />", {}, true},
	    {"///<amd-dependency path=\"foo\" />", {}, true},
	    {"///<amd-module />", {}, true},
	    {"/// <reference path=\"foo\" />", {}, true},
	    {"/// <reference types=\"foo\" />", {}, true},
	    {"/// <reference lib=\"foo\" />", {}, true},
	    {"/// <reference no-default-lib=\"foo\" />", {}, true},
	    {"/// <amd-dependency path=\"foo\" />", {}, true},
	    {"/// <amd-module />", {}, true},
	    {"/// <reference path=\"foo\"/>", {}, true},
	    {"/// <reference types=\"foo\"/>", {}, true},
	    {"/// <reference lib=\"foo\"/>", {}, true},
	    {"/// <reference no-default-lib=\"foo\"/>", {}, true},
	    {"/// <amd-dependency path=\"foo\"/>", {}, true},
	    {"/// <amd-module/>", {}, true},
	    {"/// <reference path='foo' />", {}, true},
	    {"/// <reference types='foo' />", {}, true},
	    {"/// <reference lib='foo' />", {}, true},
	    {"/// <reference no-default-lib='foo' />", {}, true},
	    {"/// <amd-dependency path='foo' />", {}, true},
	    {"/// <reference path=\"foo\" />  ", {}, true},
	    {"/// <reference types=\"foo\" />  ", {}, true},
	    {"/// <reference lib=\"foo\" />  ", {}, true},
	    {"/// <reference no-default-lib=\"foo\" />  ", {}, true},
	    {"/// <amd-dependency path=\"foo\" />  ", {}, true},
	    {"/// <amd-module />  ", {}, true},
	    {"/// <foo />", {}, false},
	    {"/// <reference />", {}, false},
	    {"/// <amd-dependency />", {}, false},
	};
	for (int i = 0; i < (int)std::size(data); ++i) {
		auto rec = data[i];
		t->Run(gostd::sprintf("[%d] isRecognizedTripleSlashComment()", {i}),
		       [rec](T* t) {
			       t->Parallel();
			       auto commentRange = rec.commentRange;
			       if (commentRange.kind == Kind::Unknown) {
				       commentRange.kind = Kind::SingleLineCommentTrivia;
				       static_cast<TextRange&>(commentRange) = TextRange{
				           0, static_cast<TextPos>(rec.s.size())};
			       }
			       auto actual = IsRecognizedTripleSlashComment(rec.s, commentRange);
			       gotest::assert::Equal(t, actual, rec.expected);
		       });
	}
}
REGISTER_UNIT_TEST("printer.TestIsRecognizedTripleSlashComment",
                   TestIsRecognizedTripleSlashComment);

}  // namespace
