// Port of tsc/internal/api/session_textedit_test.go (package api).
#include <memory>
#include <vector>

#include "internal/api/proto.h"
#include "internal/api/session.h"
#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/parser/parser.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;

} // namespace

// TestToAPITextEditsUsesOriginalCoordinates — session_textedit_test.go:13.
void TestToAPITextEditsUsesOriginalCoordinates(T* t) {
	t->Parallel();

	SourceFileParseOptions opts;
	opts.FileName = "/app.vue";
	opts.Path = tspath::Path("/app.vue");
	auto* sourceFile =
	    tsc::parseSourceFile(opts, "const transformed = true;",
	                            tsc::ScriptKind::TS);
	ContentMapperSourceFileInfo info;
	info.OriginalText = "😀\nabc";
	info.ContentMapper = "mapper";
	sourceFile->SetContentMapperInfo(info);

	auto edit = std::make_shared<lsp::lsproto::TextEdit>();
	edit->Range = lsp::lsproto::Range{lsp::lsproto::Position{1, 1},
	                                lsp::lsproto::Position{1, 2}};
	edit->NewText = "x";
	auto edits = toAPITextEdits(
	    sourceFile,
	    lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>>{
	        std::vector<std::shared_ptr<lsp::lsproto::TextEdit>>{edit}});

	assert::Assert(t, edits.size() == 1, "expected one edit");
	assert::Equal(t, (int)edits[0]->Pos, 4);
	assert::Equal(t, (int)edits[0]->End, 5);
	assert::Equal(t, edits[0]->NewText, std::string("x"));
}
REGISTER_UNIT_TEST("api.TestToAPITextEditsUsesOriginalCoordinates",
                   TestToAPITextEditsUsesOriginalCoordinates);

} // namespace tsc::api
