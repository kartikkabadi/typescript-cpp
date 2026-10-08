// tests_tokens.cpp — port of tsc/internal/astnav/tokens_test.go.
//
// The Go test file relies on Node.js (jstest.EvalNodeScriptWithTS) for its
// TS-oracle baselines and calls jstest.SkipIfNoNodeJS(t) at the top of
// TestGetTokenAtPosition / TestGetTouchingPropertyName / TestFindPrecedingToken.
// On this box those tests are all SKIP upstream ("Node.js not found"), so
// they are registered as SKIP with the same reason — the Node baseline
// harness is not ported.
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/kind.h"
#include "internal/astnav/tokens.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/parser/parser.h"
#include "internal/repo/paths.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace baseline = tsc::testutil::baseline;
using namespace tsc;

namespace {

// SKIP'd upstream on this box: no Node.js for the TS-oracle baselines.
void TestGetTokenAtPosition(T* t) {
	t->Skip({"Node.js not found"});
}

void TestGetTouchingPropertyName(T* t) {
	t->Skip({"Node.js not found"});
}

void TestFindPrecedingToken(T* t) {
	t->Skip({"Node.js not found"});
}

// --- the Node-independent portions of tokens_test.go ---

struct tokenRun {
	int startPos;
	int endPos;
	std::string kind;
	int nodePos;
	int nodeEnd;
};

struct tokenInfo {
	std::string kind;
	int pos;
	int end;
};

std::optional<tokenInfo> toTokenInfo(Node* node) {
	if (node == nullptr) {
		return std::nullopt;
	}
	std::string kind{kindToString(node->kind)};
	if (kind == "EndOfFile") {
		kind = "EndOfFileToken";
	}
	return tokenInfo{kind, node->pos(), node->end()};
}

std::vector<std::string> testFiles() {
	return {tspath::combinePaths(repo::testDataPath(),
	                                {"fixtures", "services", "mapCode.ts"})};
}

void baselineGoTokensJSON(
    T* t, const std::string& testName,
    const std::function<std::optional<tokenInfo>(SourceFile*, int)>&
        getGoToken) {
	for (auto& fileName : testFiles()) {
		t->Run(tspath::getBaseFileName(fileName),
		       [&, fileName](T* t) {
			       t->Parallel();
			       std::ifstream in(fileName, std::ios::binary);
			       assert::Assert(t, in.is_open());
			       std::ostringstream ss;
			       ss << in.rdbuf();
			       std::string fileText = ss.str();
			       SourceFileParseOptions opts;
			       opts.FileName = "/file.ts";
			       opts.Path = "/file.ts";
			       SourceFile* file = parseSourceFile(
			           opts, fileText, ScriptKind::TS);

			       int maxPos = (int)fileText.size();
			       std::vector<tokenRun> runs;
			       std::optional<tokenRun> current;
			       for (int pos = 0; pos < maxPos; pos++) {
				       auto token = getGoToken(file, pos);
				       if (current.has_value() && token.has_value() &&
				           current->kind == token->kind &&
				           current->nodePos == token->pos &&
				           current->nodeEnd == token->end) {
					       current->endPos = pos;
				       } else {
					       if (current.has_value()) {
						       runs.push_back(*current);
					       }
					       if (token.has_value()) {
						       current = tokenRun{
						           pos, pos, token->kind, token->pos,
						           token->end};
					       } else {
						       current = std::nullopt;
					       }
				       }
			       }
			       if (current.has_value()) {
				       runs.push_back(*current);
			       }

			       std::vector<json::Value> elements;
			       for (auto& r : runs) {
				       elements.push_back(json::marshalObject(
				           {{"startPos",
				             json::marshalInt64(r.startPos)},
				            {"endPos", json::marshalInt64(r.endPos)},
				            {"kind", json::marshalString(r.kind)},
				            {"nodePos",
				             json::marshalInt64(r.nodePos)},
				            {"nodeEnd",
				             json::marshalInt64(r.nodeEnd)}}));
			       }
			       auto [output, jerr] = json::marshalIndent(
			           json::marshalArray(elements), "", "  ");
			       assert::Assert(t, jerr.empty());

			       baseline::Run(
			           t,
			           testName + "." +
			               std::string(tspath::getBaseFileName(fileName)) +
			               ".baseline.json",
			           output, baseline::Options{.Subfolder = "astnav"});
		       });
	}
}

void TestFindNextToken(T* t) {
	t->Parallel();

	t->Run("go baseline json", [](T* t) {
		t->Parallel();
		baselineGoTokensJSON(
		    t, "FindNextToken", [](SourceFile* file,
		                           int pos) -> std::optional<tokenInfo> {
			    // FindNextToken panics (like Go's assert) when the scanner
			    // finds trivia between previousToken.End() and the next
			    // syntactic token. Catch those to avoid crashing the
			    // baseline generator; those positions will be absent from
			    // the baseline.
			    try {
				    Node* token = astnav::getTokenAtPosition(file, pos);
				    Node* next = astnav::findNextToken(
				        token, file->asNode(), file);
				    return toTokenInfo(next);
			    } catch (...) {
				    return std::nullopt;
			    }
		    });
	});
}

void TestUnitFindPrecedingToken(T* t) {
	t->Parallel();
	struct {
		const char* name;
		const char* fileContent;
		int position;
		Kind expectedKind;
	} testCases[] = {
	    {
	        "after dot in jsdoc",
	        R"(import {
    CharacterCodes,
    compareStringsCaseInsensitive,
    compareStringsCaseSensitive,
    compareValues,
    Comparison,
    Debug,
    endsWith,
    equateStringsCaseInsensitive,
    equateStringsCaseSensitive,
    GetCanonicalFileName,
    getDeclarationFileExtension,
    getStringComparer,
    identity,
    lastOrUndefined,
    Path,
    some,
    startsWith,
} from "./_namespaces/ts.js";

/**
 * Internally, we represent paths as strings with '/' as the directory separator.
 * When we make system calls (eg: LanguageServiceHost.getDirectory()),
 * we expect the host to correctly handle paths in our specified format.
 *
 * @internal
 */
export const directorySeparator = "/";
/** @internal */
export const altDirectorySeparator = "\\";
const urlSchemeSeparator = "://";
const backslashRegExp = /\\/g;


backslashRegExp.

//Path Tests

/**
 * Determines whether a charCode corresponds to '/' or '\'.
 *
 * @internal
 */
export function isAnyDirectorySeparator(charCode: number): boolean {
    return charCode === CharacterCodes.slash || charCode === CharacterCodes.backslash;
})",
	        839,
	        Kind::DotToken,
	    },
	    {
	        "after comma in parameter list",
	        "takesCb((n, s, ))",
	        15,
	        Kind::CommaToken,
	    },
	};
	for (auto& testCase : testCases) {
		t->Run(testCase.name, [testCase](T* t) {
			t->Parallel();
			SourceFileParseOptions opts;
			opts.FileName = "/file.ts";
			opts.Path = "/file.ts";
			SourceFile* file = parseSourceFile(
			    opts, testCase.fileContent, ScriptKind::TS);
			Node* token =
			    astnav::findPrecedingToken(file, testCase.position);
			assert::Equal(t, (int)token->kind,
			              (int)testCase.expectedKind);
		});
	}
}

} // namespace

REGISTER_UNIT_TEST("astnav.TestGetTokenAtPosition", TestGetTokenAtPosition);
REGISTER_UNIT_TEST("astnav.TestGetTouchingPropertyName",
                   TestGetTouchingPropertyName);
REGISTER_UNIT_TEST("astnav.TestFindPrecedingToken", TestFindPrecedingToken);
REGISTER_UNIT_TEST("astnav.TestFindNextToken", TestFindNextToken);
REGISTER_UNIT_TEST("astnav.TestUnitFindPrecedingToken",
                   TestUnitFindPrecedingToken);
