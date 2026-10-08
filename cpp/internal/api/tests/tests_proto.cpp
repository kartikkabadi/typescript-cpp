// Port of tsc/internal/api/proto_test.go (package api_test).
#include <memory>
#include <string>
#include <vector>

#include "internal/api/proto.h"
#include "internal/ast/ast.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/parser/parser.h"
#include "internal/project/project.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

namespace {

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace api = tsc::api;
namespace json = tsc::json;
namespace tspath = tsc::tspath;

} // namespace

// TestDocumentIdentifierUnmarshalJSON — proto_test.go:16.
void TestDocumentIdentifierUnmarshalJSON(T* t) {
	t->Parallel();
	struct tc {
		const char* name;
		const char* input;
		const char* fileName;
		const char* uri;
		const char* err;
	};
	const tc tests[] = {
	    {"plain string", "\"foo.ts\"", "foo.ts", "", ""},
	    {"uri object", "{\"uri\":\"file:///foo.ts\"}", "",
	     "file:///foo.ts", ""},
	    {"uri object with unknown fields",
	     "{\"uri\":\"file:///foo.ts\",\"extra\":true}", "",
	     "file:///foo.ts", ""},
	    {"empty object", "{}", "", "", ""},
	    {"invalid type", "42", "", "",
	     "expected string or object, got number"},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [tt](T* t) {
			t->Parallel();
			api::DocumentIdentifier d;
			auto err = json::unmarshal(std::string_view(tt.input), &d);
			if (tt.err[0] != '\0') {
				assert::Assert(t, err.find(tt.err) != std::string::npos,
				               "expected error containing " +
				                   std::string(tt.err) + ", got: " + err);
				return;
			}
			assert::Assert(t, err.empty(), "unexpected error: " + err);
			assert::Equal(t, d.FileName, std::string(tt.fileName));
			assert::Equal(t, std::string(d.URI), std::string(tt.uri));
		});
	}
}
REGISTER_UNIT_TEST("api.TestDocumentIdentifierUnmarshalJSON",
                   TestDocumentIdentifierUnmarshalJSON);

// TestEnsureProgramsUnmarshalJSON — proto_test.go:57.
void TestEnsureProgramsUnmarshalJSON(T* t) {
	t->Parallel();

	api::EnsurePrograms all;
	assert::Assert(t, json::unmarshal("true", &all).empty());
	assert::Equal(t, all.All, true);

	api::EnsurePrograms projects;
	assert::Assert(t, json::unmarshal(
	                          "[\"/tsconfig.json\",\"/dev/null/synthetic/1\"]",
	                          &projects)
	                      .empty());
	assert::DeepEqual(
	    t, projects.Projects,
	    std::vector<tsc::project::ID>{
	        tsc::project::ConfiguredProjectID(tspath::Path("/tsconfig.json"))
	            .AsID(),
	        tsc::project::NewSyntheticProjectID(1).AsID(),
	    });

	api::EnsurePrograms invalid;
	auto err = json::unmarshal("false", &invalid);
	assert::Assert(t, err.find("must be true or an array") != std::string::npos,
	               "expected 'must be true or an array' error, got: " + err);
}
REGISTER_UNIT_TEST("api.TestEnsureProgramsUnmarshalJSON",
                   TestEnsureProgramsUnmarshalJSON);

// TestNewDiagnosticResponseIncludesFormattingContext — proto_test.go:73.
void TestNewDiagnosticResponseIncludesFormattingContext(T* t) {
	t->Parallel();

	std::string text = "const 💩 = 1;";
	tsc::SourceFileParseOptions opts;
	opts.FileName = "/unicode.ts";
	auto* file = tsc::parseSourceFile(opts, text,
	                                        tsc::ScriptKind::TS);
	auto pos = text.find("=");
	assert::Assert(t, pos != std::string::npos && pos > 0);
	auto end = pos + std::string("=").size();

	auto* diag = tsc::newDiagnostic(file, tsc::TextRange{(int)pos, (int)end},
	                                tsc::Expression_expected);
	auto resp = api::NewDiagnosticResponse(diag);

	assert::Equal(t, (int)resp->Pos, 9);
	assert::Equal(t, (int)resp->End, 10);
	assert::Assert(t, resp->StartPosition != nullptr);
	assert::Equal(t, (int)resp->StartPosition->Line, 0);
	assert::Equal(t, (int)resp->StartPosition->Character, 9);
	assert::Assert(t, resp->EndPosition != nullptr);
	assert::Equal(t, (int)resp->EndPosition->Line, 0);
	assert::Equal(t, (int)resp->EndPosition->Character, 10);
	assert::Assert(t, resp->SourceLines.size() == 1);
	assert::Equal(t, (int)resp->SourceLines[0]->Line, 0);
	assert::Equal(t, resp->SourceLines[0]->Text, text);
	assert::Equal(t, (int)resp->Pos,
	              (int)file->GetPositionMap()->UTF8ToUTF16((int)pos));
	assert::Equal(t, (int)resp->End,
	              (int)file->GetPositionMap()->UTF8ToUTF16((int)end));
}
REGISTER_UNIT_TEST("api.TestNewDiagnosticResponseIncludesFormattingContext",
                   TestNewDiagnosticResponseIncludesFormattingContext);

// TestNewDiagnosticResponseTruncatesLongFormattingContext —
// proto_test.go:93.
void TestNewDiagnosticResponseTruncatesLongFormattingContext(T* t) {
	t->Parallel();

	std::string text = "one\ntwo\nthree\nfour\nfive\nsix\nseven";
	tsc::SourceFileParseOptions opts;
	opts.FileName = "/multiline.ts";
	auto* file = tsc::parseSourceFile(opts, text,
	                                        tsc::ScriptKind::TS);
	auto* diag =
	    tsc::newDiagnostic(file, tsc::TextRange{0, (int)text.size()},
	                       tsc::Expression_expected);
	auto resp = api::NewDiagnosticResponse(diag);

	assert::Assert(t, resp->StartPosition != nullptr);
	assert::Equal(t, (int)resp->StartPosition->Line, 0);
	assert::Equal(t, (int)resp->StartPosition->Character, 0);
	assert::Assert(t, resp->EndPosition != nullptr);
	assert::Equal(t, (int)resp->EndPosition->Line, 6);
	assert::Equal(t, (int)resp->EndPosition->Character, 5);

	assert::Assert(t, resp->SourceLines.size() == 4);
	assert::Equal(t, (int)resp->SourceLines[0]->Line, 0);
	assert::Equal(t, resp->SourceLines[0]->Text, std::string("one\n"));
	assert::Equal(t, (int)resp->SourceLines[1]->Line, 1);
	assert::Equal(t, resp->SourceLines[1]->Text, std::string("two\n"));
	assert::Equal(t, (int)resp->SourceLines[2]->Line, 5);
	assert::Equal(t, resp->SourceLines[2]->Text, std::string("six\n"));
	assert::Equal(t, (int)resp->SourceLines[3]->Line, 6);
	assert::Equal(t, resp->SourceLines[3]->Text, std::string("seven"));
}
REGISTER_UNIT_TEST("api.TestNewDiagnosticResponseTruncatesLongFormattingContext",
                   TestNewDiagnosticResponseTruncatesLongFormattingContext);
