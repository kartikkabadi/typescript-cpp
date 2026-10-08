// Port of tsc/internal/lsp/lsproto/lsp_test.go.
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace json = tsc::json;
namespace lsproto = tsc::lsp::lsproto;
using tsc::gostd::testing::T;

void TestUnmarshalCompletionItem(T* t) {
	t->Parallel();

	const std::string message = R"({
    "label": "pageXOffset",
    "insertTextFormat": 1,
    "textEdit": {
        "newText": "pageXOffset",
        "insert": {
            "start": {
                "line": 4,
                "character": 0
            },
            "end": {
                "line": 4,
                "character": 4
            }
        },
        "replace": {
            "start": {
                "line": 4,
                "character": 0
            },
            "end": {
                "line": 4,
                "character": 4
            }
        }
    },
    "kind": 6,
    "sortText": "15",
    "commitCharacters": [
        ".",
        ",",
        ";"
    ]
})";

	lsproto::CompletionItem result;
	auto err = json::unmarshal(message, &result);
	assert::Assert(t, err.empty(),
	               {"json.Unmarshal failed: " + err});

	// Go asserts DeepEqual against the fully-populated struct; spelled out
	// field-by-field (generated structs have no operator==).
	assert::Equal(t, result.Label, std::string("pageXOffset"));
	assert::Assert(t, result.InsertTextFormat != nullptr &&
	                      *result.InsertTextFormat ==
	                          lsproto::InsertTextFormatPlainText,
	               {"InsertTextFormat != PlainText"});
	assert::Assert(t, result.TextEdit != nullptr &&
	                      result.TextEdit->InsertReplaceEdit != nullptr,
	               {"TextEdit is not an InsertReplaceEdit"});
	auto& ire = *result.TextEdit->InsertReplaceEdit;
	assert::Equal(t, ire.NewText, std::string("pageXOffset"));
	assert::Assert(t, ire.Insert.Start.Line == 4 &&
	                      ire.Insert.Start.Character == 0 &&
	                      ire.Insert.End.Line == 4 &&
	                      ire.Insert.End.Character == 4,
	               {"Insert range mismatch"});
	assert::Assert(t, ire.Replace.Start.Line == 4 &&
	                      ire.Replace.Start.Character == 0 &&
	                      ire.Replace.End.Line == 4 &&
	                      ire.Replace.End.Character == 4,
	               {"Replace range mismatch"});
	assert::Assert(t, result.Kind != nullptr &&
	                      *result.Kind == lsproto::CompletionItemKindVariable,
	               {"Kind != Variable"});
	assert::Assert(t, result.SortText.has_value() &&
	                      *result.SortText == "15",
	               {"SortText != \"15\""});
	assert::Assert(t, result.CommitCharacters != nullptr &&
	                      result.CommitCharacters->has_value(),
	               {"CommitCharacters is nil"});
	assert::DeepEqual(t, result.CommitCharacters->value(),
	                  std::vector<std::string>({".", ",", ";"}));
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalCompletionItem",
                   TestUnmarshalCompletionItem);

}  // namespace
