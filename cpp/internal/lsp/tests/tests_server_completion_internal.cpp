// Port of tsc/internal/lsp/server_completion_internal_test.go.
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

void TestCompletionItemResolveRejectsInvalidFileName(
    tsc::gostd::testing::T* t) {
	namespace assert = tsc::gotest::assert;
	using tsc::lsp::Server;
	namespace lsproto = tsc::lsp::lsproto;
	using tsc::gostd::contextBackground;

	t->Parallel();

	struct testCase {
		std::string fileName;
		std::string message;
	};
	for (const auto& test : std::vector<testCase>{
	         {"relative.ts",
	          "completion item data fileName must be absolute"},
	         {"^/invalid",
	          "completion item data fileName must be a valid dynamic "
	          "path"},
	         {"^/~ts-uri~/scheme/authority/~ts-uri-escape~zz~",
	          "completion item data fileName must be a valid dynamic "
	          "path"},
	     }) {
		t->Run(test.fileName, [&](tsc::gostd::testing::T* t) {
			t->Parallel();
			tsc::lsp::ServerOptions serverOpts;
			serverOpts.Cwd = "/";
			auto server = tsc::lsp::NewServer(serverOpts);
			auto item = std::make_shared<lsproto::CompletionItem>();
			item->Data = std::make_shared<lsproto::CompletionItemData>();
			item->Data->FileName = test.fileName;
			auto [resp, err] = server->handleCompletionItemResolve(
			    contextBackground(), item, nullptr);
			assert::Error(t, err, test.message);
		});
	}
}

} // namespace

REGISTER_UNIT_TEST("lsp.TestCompletionItemResolveRejectsInvalidFileName",
                   TestCompletionItemResolveRejectsInvalidFileName);
