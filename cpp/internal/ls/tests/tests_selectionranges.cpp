// Port of tsc/internal/ls/selectionranges_test.go.
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/tests/testaccess.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/parser/parser.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc::ls;
using namespace tsc;
namespace lsproto = tsc::lsp::lsproto;

// selectionranges.go package-level constants; redeclared here because the
// C++ port keeps them TU-local in selectionranges.cpp.
constexpr int maxSelectionRangeDepth = 1000;
namespace tsc::ls {
// selectionranges.go:185 — defined in selectionranges.cpp, not declared in
// ls.h.
lsp::lsproto::SelectionRange* getSmartSelectionRange(
    LanguageService* l, SourceFile* sourceFile, int pos);
}

static void TestSelectionRangeDepthIsLimited(T* t) {
	t->Parallel();

	const int nestingDepth = 12000;
	std::string text = "const x = " + std::string(nestingDepth, '(') + "1" +
	                   std::string(nestingDepth, ')') + ";";
	auto* sourceFile = parseSourceFile(
	    SourceFileParseOptions{.FileName = "/index.ts",
	                           .Path = "/index.ts"},
	    text, ScriptKind::TS);
	auto* lineMap = lsconv::ComputeLSPLineStarts(text);
	auto* languageService = LSTestAccess::NewWithConverters(
	    lsconv::NewConverters(lsproto::PositionEncodingKindUTF16,
	                          [lineMap](const std::string&)
	                              -> lsconv::LSPLineMap* {
		                          return lineMap;
	                          }));

	auto* result = getSmartSelectionRange(
	    languageService, sourceFile,
	    int(std::string("const x = ").size()) + nestingDepth);
	int depth = 0;
	lsproto::SelectionRange* outermost = nullptr;
	for (auto* current = result; current != nullptr;
	     current = current->Parent.get()) {
		depth++;
		outermost = current;
	}

	if (depth != maxSelectionRangeDepth) {
		t->Fatalf("selection range depth = %d, want %d",
		          {depth, maxSelectionRangeDepth});
	}
	auto [innerRange, _i] = languageService->Converters()->ToLSPRange(
	    sourceFile,
	    TextRange{int(std::string("const x = ").size()) + nestingDepth,
	              int(std::string("const x = ").size()) + nestingDepth + 1});
	if (!(result->Range == innerRange)) {
		t->Fatal({"innermost selection range != inner range"});
	}
	auto [fullRange, _f] = languageService->Converters()->ToLSPRange(
	    sourceFile,
	    TextRange{int(sourceFile->pos()), int(sourceFile->end())});
	if (!(outermost->Range == fullRange)) {
		t->Fatal({"outermost selection range != full file range"});
	}
	std::vector<std::shared_ptr<lsproto::SelectionRange>> results{
	    std::shared_ptr<lsproto::SelectionRange>(result)};
	lsproto::SelectionRangesOrNull response;
	response.SelectionRanges =
	    std::make_shared<lsproto::Slice<
	        std::shared_ptr<lsproto::SelectionRange>>>(results);
	lsproto::ResponseMessage responseMessage;
	responseMessage.ID = std::make_shared<jsonrpc::ID>(
	    jsonrpc::NewIDString("selectionRange"));
	responseMessage.Result = lsproto::AnyValue::of(response);
	auto message = responseMessage.toMessage();
	auto [_m, err] = json::marshal(*message);
	if (!err.empty()) {
		t->Fatalf("failed to marshal limited selection range: %s", {err});
	}
}
REGISTER_UNIT_TEST("ls.TestSelectionRangeDepthIsLimited",
                   TestSelectionRangeDepthIsLimited);
