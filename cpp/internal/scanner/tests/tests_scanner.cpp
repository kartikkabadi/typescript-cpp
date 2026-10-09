// tests_scanner.cpp — port of tsc/internal/scanner/scanner_test.go (package
// scanner — the test reaches package-internal helpers).
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/text.h"
#include "internal/gostd/testing.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

void TestScanStringPreservesLoneSurrogates(T* t) {
	t->Parallel();
	Scanner s;
	s.setText("\"🦀\\ud7ff\\ud800\\ud801\\uD83E\\uDD80\"");
	assert::Equal(t, s.scan(), Kind::StringLiteral);
	char buf[4];
	auto enc = [&](char32_t r) {
		return std::string(buf, encodeJSStringRune(r, buf));
	};
	assert::Equal(t, std::string(s.tokenValue()),
	              "🦀" + enc(0xD7FF) + enc(0xD800) + enc(0xD801) + "🦀");
}

void TestNormalizeJSDocTypeSourceText(T* t) {
	t->Parallel();

	struct TestCase {
		std::string name;
		std::string text;
		std::vector<std::string> expectedLines;
	};
	std::vector<TestCase> tests{
	    {"single line", " \t* \tFoo", {"Foo"}},
	    {"ECMAScript line breaks",
	     "Foo\r\n * Bar\r\t* Baz\xE2\x80\xA8 * Qux\xE2\x80\xA9* Quux",
	     {"Foo", "Bar", "Baz", "Qux", "Quux"}},
	    {"blank and trailing lines", "Foo\r\n *\r\n", {"Foo", "", ""}},
	    {"line without marker", "Foo\n  Bar", {"Foo", "Bar"}},
	    {"only leading marker", "**Foo", {"*Foo"}},
	};

	for (const auto& test : tests) {
		auto rec = test;
		t->Run(test.name, [rec](T* t) {
			t->Parallel();
			std::string expected;
			for (size_t i = 0; i < rec.expectedLines.size(); i++) {
				if (i) expected += '\n';
				expected += rec.expectedLines[i];
			}
			assert::Equal(t, normalizeJSDocTypeSourceText(rec.text),
			              expected);
		});
	}
}

void TestIsJSDocTypeExpressionOrChild(T* t) {
	t->Parallel();

	auto* jsDocType = new Node{Kind::TypeReference, NodeFlagsJSDoc};
	auto* jsDocTypeChild =
	    new Node{Kind::Identifier, NodeFlagsJSDoc, {}, jsDocType};
	auto* reparsedType = new Node{Kind::TypeLiteral, NodeFlagsReparsed};
	auto* reparsedTypeChild =
	    new Node{Kind::Identifier, NodeFlagsReparsed, {}, reparsedType};
	auto* ordinaryType = new Node{Kind::TypeReference};
	auto* jsDocTag = new Node{Kind::JSDocParameterTag, NodeFlagsJSDoc};
	auto* jsDocTagChild =
	    new Node{Kind::Identifier, NodeFlagsJSDoc, {}, jsDocTag};

	struct TestCase {
		std::string name;
		Node* node;
		bool expected;
	};
	std::vector<TestCase> tests{
	    {"type expression", new Node{Kind::JSDocTypeExpression}, true},
	    {"JSDoc type", jsDocType, true},
	    {"JSDoc type child", jsDocTypeChild, true},
	    {"reparsed type", reparsedType, true},
	    {"reparsed type child", reparsedTypeChild, true},
	    {"ordinary type", ordinaryType, false},
	    {"other JSDoc child", jsDocTagChild, false},
	};

	for (const auto& test : tests) {
		auto rec = test;
		t->Run(test.name, [rec](T* t) {
			t->Parallel();
			assert::Equal(t, isJSDocTypeExpressionOrChild(rec.node),
			              rec.expected);
		});
	}
}

void TestGetTextOfNodeFromJSDocTypePreservesAsteriskType(T* t) {
	t->Parallel();

	std::string sourceText = "\n * *";
	Node node{Kind::JSDocAllType, NodeFlagsJSDoc,
	          TextRange{0, (int)sourceText.size()}, nullptr};

	assert::Equal(t,
	              getTextOfNodeFromSourceText(sourceText, &node,
	                                                   false /*includeTrivia*/),
	              "*");
}


void TestScanSourceKeyword(T* t) {
	t->Parallel();
	Scanner s;
	s.setText("source sourceValue");
	assert::Equal(t, s.scan(), Kind::SourceKeyword);
	assert::Equal(t, std::string(tokenToString(Kind::SourceKeyword)),
	              std::string("source"));
	assert::Equal(t, stringToToken("source"), Kind::SourceKeyword);
	assert::Equal(t, s.scan(), Kind::Identifier);
	assert::Equal(t, std::string(s.tokenValue()), std::string("sourceValue"));
}
} // namespace

REGISTER_UNIT_TEST("scanner.TestScanStringPreservesLoneSurrogates",
                   TestScanStringPreservesLoneSurrogates);
REGISTER_UNIT_TEST("scanner.TestNormalizeJSDocTypeSourceText",
                   TestNormalizeJSDocTypeSourceText);
REGISTER_UNIT_TEST("scanner.TestIsJSDocTypeExpressionOrChild",
                   TestIsJSDocTypeExpressionOrChild);
REGISTER_UNIT_TEST("scanner.TestGetTextOfNodeFromJSDocTypePreservesAsteriskType",
                   TestGetTextOfNodeFromJSDocTypePreservesAsteriskType);

REGISTER_UNIT_TEST("scanner.TestScanSourceKeyword", TestScanSourceKeyword);
