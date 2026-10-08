// Port of tsc/internal/lsp/server_projectinfo_test.go (package lsp_test).
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/lsptestutil/lspclient.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::lsp {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace lsptestutil = tsc::testutil::lsptestutil;

// expectedCodeActionKinds — server_projectinfo_test.go:16.
std::vector<lsproto::CodeActionKind> expectedCodeActionKinds() {
	return {
	    lsproto::CodeActionKindQuickFix,
	    "source.organizeImports.ts",
	    "source.removeUnusedImports.ts",
	    "source.sortImports.ts",
	    "source.fixAll.ts",
	};
}

// initProjectInfoClient — server_projectinfo_test.go:26.
std::shared_ptr<lsptestutil::LSPClient>
initProjectInfoClient(T* t,
                      const std::unordered_map<std::string, vfs::vfstest::MapFileInput>& files) {
	t->Helper();

	auto fs = bundled::WrapFS(vfs::vfstest::FromMap(files, false));

	auto onServerRequest =
	    [](const gostd::Context&,
	       const std::shared_ptr<lsproto::RequestMessage>& req)
	    -> std::shared_ptr<lsproto::ResponseMessage> {
		if (req->Method == lsproto::MethodClientRegisterCapability ||
		    req->Method == lsproto::MethodClientUnregisterCapability ||
		    req->Method == lsproto::MethodWindowWorkDoneProgressCreate) {
			auto resp = std::make_shared<lsproto::ResponseMessage>();
			resp->ID = req->ID;
			resp->JSONRPC = req->JSONRPC;
			resp->Result = lsproto::AnyValue::of(lsproto::Null{});
			return resp;
		}
		return nullptr;
	};

	auto [client, closeClient] =
	    lsptestutil::NewLSPClient(t,
	                              lsp::ServerOptions{.Err = gostd::io::discard(),
	                                                 .Cwd = "/home/projects",
	                                                 .FS = fs,
	                                                 .DefaultLibraryPath =
	                                                     bundled::LibPath()},
	                              onServerRequest);
	std::function<gostd::Error()> closeClientFn = closeClient;
	t->Cleanup([closeClientFn] { closeClientFn(); });

	auto initParams = std::make_shared<lsproto::InitializeParams>();
	initParams->Capabilities =
	    std::make_shared<lsproto::ClientCapabilities>();
	auto [initMsg, _1, ok] =
	    client->SendRequest(t, lsproto::InitializeInfo, initParams);
	assert::Assert(t, ok && initMsg->AsResponse()->Error == nullptr,
	               "Initialize failed");
	client->SendNotification(t, lsproto::InitializedInfo,
	                         std::make_shared<lsproto::InitializedParams>());
	client->Server->InitComplete()->wait();

	return client;
}

void openFile(lsptestutil::LSPClient* client, T* t,
              const lsproto::DocumentUri& uri, const std::string& text) {
	auto params = std::make_shared<lsproto::DidOpenTextDocumentParams>();
	params->TextDocument = std::make_shared<lsproto::TextDocumentItem>();
	params->TextDocument->Uri = uri;
	params->TextDocument->LanguageId = "typescript";
	params->TextDocument->Text = text;
	client->SendNotification(t, lsproto::TextDocumentDidOpenInfo, params);
}

} // namespace

// TestInitializeCodeActionKinds — server_projectinfo_test.go:58.
void TestInitializeCodeActionKinds(T* t) {
	t->Parallel();

	auto [client, closeClient] = lsptestutil::NewLSPClient(
	    t,
	    lsp::ServerOptions{.Err = gostd::io::discard(),
	                       .Cwd = "/home/projects",
	                       .FS = bundled::WrapFS(vfs::vfstest::FromMap({}, false)),
	                       .DefaultLibraryPath = bundled::LibPath()},
	    nullptr);
	std::function<gostd::Error()> closeClientFn = closeClient;
	t->Cleanup([closeClientFn] { closeClientFn(); });

	auto initParams = std::make_shared<lsproto::InitializeParams>();
	initParams->Capabilities =
	    std::make_shared<lsproto::ClientCapabilities>();
	auto [message, result, ok] =
	    client->SendRequest(t, lsproto::InitializeInfo, initParams);
	assert::Assert(t, ok && message->AsResponse()->Error == nullptr,
	               "Initialize failed");
	assert::DeepEqual(
	    t,
	    *result->Capabilities->CodeActionProvider->CodeActionOptions
	         ->CodeActionKinds,
	    expectedCodeActionKinds());
}
REGISTER_UNIT_TEST("lsp.TestInitializeCodeActionKinds",
                   TestInitializeCodeActionKinds);

// TestProjectInfoConfiguredProject — server_projectinfo_test.go:75.
void TestProjectInfoConfiguredProject(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto client = initProjectInfoClient(
	    t,
	    {
	        {"/home/projects/tsconfig.json", "{}"},
	        {"/home/projects/index.ts", "export const x = 1;"},
	    });

	auto uri = lsproto::DocumentUri("file:///home/projects/index.ts");
	openFile(client.get(), t, uri, "export const x = 1;");

	auto params = std::make_shared<lsproto::ProjectInfoParams>();
	params->TextDocument = lsproto::TextDocumentIdentifier{uri};
	auto [msg, resp, ok] =
	    client->SendRequest(t, lsproto::CustomProjectInfoInfo, params);
	assert::Assert(t, ok, "expected a response");
	assert::Assert(t, msg->AsResponse()->Error == nullptr);
	assert::Equal(t, resp->ConfigFilePath,
	              std::string("/home/projects/tsconfig.json"));
}
REGISTER_UNIT_TEST("lsp.TestProjectInfoConfiguredProject",
                   TestProjectInfoConfiguredProject);

// TestProjectInfoInferredProject — server_projectinfo_test.go:103.
void TestProjectInfoInferredProject(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto client = initProjectInfoClient(
	    t, {{"/home/projects/index.ts", "export const x = 1;"}});

	auto uri = lsproto::DocumentUri("file:///home/projects/index.ts");
	openFile(client.get(), t, uri, "export const x = 1;");

	auto params = std::make_shared<lsproto::ProjectInfoParams>();
	params->TextDocument = lsproto::TextDocumentIdentifier{uri};
	auto [msg, resp, ok] =
	    client->SendRequest(t, lsproto::CustomProjectInfoInfo, params);
	assert::Assert(t, ok, "expected a response");
	assert::Assert(t, msg->AsResponse()->Error == nullptr);
	assert::Equal(t, resp->ConfigFilePath, std::string(""));
}
REGISTER_UNIT_TEST("lsp.TestProjectInfoInferredProject",
                   TestProjectInfoInferredProject);

} // namespace tsc::lsp
