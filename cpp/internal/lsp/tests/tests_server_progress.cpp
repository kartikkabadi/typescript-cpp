// Port of tsc/internal/lsp/server_progress_test.go (package lsp_test).
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
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

// tokenString — server_progress_test.go:126.
std::string tokenString(const lsproto::IntegerOrString& t) {
	if (t.String != nullptr) {
		return *t.String;
	}
	return "";
}

} // namespace

// TestProgressNotificationsEndToEnd — server_progress_test.go:18.
void TestProgressNotificationsEndToEnd(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto fs = bundled::WrapFS(vfs::vfstest::FromMap(
	    {
	        {"/home/projects/tsconfig.json", "{}"},
	        {"/home/projects/index.ts", "export const x = 1;"},
	    },
	    false));

	// Collect $/progress notifications. Signal when "end" arrives.
	std::mutex mu;
	std::condition_variable endCv;
	bool endReceived = false;
	std::vector<std::shared_ptr<lsproto::ProgressParams>>
	    progressNotifications;

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

	auto [client, closeClient] = lsptestutil::NewLSPClient(
	    t,
	    lsp::ServerOptions{.Err = gostd::io::discard(),
	                       .Cwd = "/home/projects",
	                       .FS = fs,
	                       .DefaultLibraryPath = bundled::LibPath()},
	    onServerRequest);

	client->OnServerNotification =
	    [&](const gostd::Context&,
	        const std::shared_ptr<lsproto::RequestMessage>& req) {
		    if (req->Method == lsproto::MethodProgress) {
			    if (auto [params, err] =
			            req->UnmarshalParams<
			                std::shared_ptr<lsproto::ProgressParams>>();
			        err == nullptr && params != nullptr) {
				    bool isEnd;
				    {
					    std::lock_guard<std::mutex> lk(mu);
					    progressNotifications.push_back(params);
					    isEnd = params->Value.End != nullptr;
				    }
				    if (isEnd) {
					    std::lock_guard<std::mutex> lk(mu);
					    endReceived = true;
					    endCv.notify_all();
				    }
			    }
		    }
	    };

	auto initParams = std::make_shared<lsproto::InitializeParams>();
	initParams->Capabilities =
	    std::make_shared<lsproto::ClientCapabilities>();
	initParams->Capabilities->Window =
	    std::make_shared<lsproto::WindowClientCapabilities>();
	initParams->Capabilities->Window->WorkDoneProgress = true;
	auto [initMsg, _1, ok] =
	    client->SendRequest(t, lsproto::InitializeInfo, initParams);
	assert::Assert(t, ok && initMsg->AsResponse()->Error == nullptr,
	               "Initialize failed");
	client->SendNotification(t, lsproto::InitializedInfo,
	                         std::make_shared<lsproto::InitializedParams>());
	client->Server->InitComplete()->wait();

	auto uri = lsproto::DocumentUri("file:///home/projects/index.ts");
	auto openParams = std::make_shared<lsproto::DidOpenTextDocumentParams>();
	openParams->TextDocument =
	    std::make_shared<lsproto::TextDocumentItem>();
	openParams->TextDocument->Uri = uri;
	openParams->TextDocument->LanguageId = "typescript";
	openParams->TextDocument->Text = "export const x = 1;";
	client->SendNotification(t, lsproto::TextDocumentDidOpenInfo, openParams);

	// Send a request to ensure the server has processed the didOpen and
	// loaded the project.
	auto piParams = std::make_shared<lsproto::ProjectInfoParams>();
	piParams->TextDocument = lsproto::TextDocumentIdentifier{uri};
	auto [msg, resp, ok2] =
	    client->SendRequest(t, lsproto::CustomProjectInfoInfo, piParams);
	assert::Assert(t, ok2, "expected a response");
	assert::Assert(t, msg->AsResponse()->Error == nullptr);
	assert::Equal(t, resp->ConfigFilePath,
	              std::string("/home/projects/tsconfig.json"));

	// Wait for the "end" progress notification before reading.
	{
		std::unique_lock<std::mutex> lk(mu);
		if (!endCv.wait_for(lk, std::chrono::seconds(60),
		                    [&] { return endReceived; })) {
			t->Fatal({"timed out waiting for progress end notification"});
		}
	}

	std::vector<std::shared_ptr<lsproto::ProgressParams>> notifications;
	{
		std::lock_guard<std::mutex> lk(mu);
		notifications = progressNotifications;
	}

	assert::Assert(
	    t, notifications.size() >= 2,
	    "expected at least begin+end progress notifications");

	// First notification should be a "begin".
	assert::Assert(t, notifications[0]->Value.Begin != nullptr,
	               "expected first progress notification to be 'begin'");
	assert::Equal(t, notifications[0]->Value.Begin->Title,
	              std::string("Loading"));

	// Last notification should be an "end".
	auto& last = notifications.back();
	assert::Assert(t, last->Value.End != nullptr,
	               "expected last progress notification to be 'end'");

	// All notifications should share the same token.
	auto firstToken = tokenString(notifications[0]->Token);
	assert::Assert(t, !firstToken.empty(),
	               "expected non-empty progress token");
	for (size_t i = 0; i < notifications.size(); i++) {
		assert::Equal(t, tokenString(notifications[i]->Token), firstToken,
		              "notification has different token");
	}

	assert::NilError(t, closeClient());
}
REGISTER_UNIT_TEST("lsp.TestProgressNotificationsEndToEnd",
                   TestProgressNotificationsEndToEnd);

} // namespace tsc::lsp
