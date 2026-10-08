// Port of tsc/internal/lsp/server_test.go (internal package test).
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/session.h"
#include "internal/project/sessiontypes.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

namespace bundled = tsc::bundled;
namespace gostd = tsc::gostd;
namespace jsonrpc = tsc::jsonrpc;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace vfstest = tsc::vfs::vfstest;
using tsc::gostd::testing::T;
using tsc::lsp::Server;
using tsc::lsp::ServerOptions;

struct shutdownTestReader : tsc::lsp::Reader {
	std::pair<std::shared_ptr<lsproto::Message>, gostd::Error>
	Read() override {
		return {nullptr, gostd::io::errEOF};
	}
};

struct shutdownTestWriter : tsc::lsp::Writer {
	gostd::Error Write(
	    const std::shared_ptr<lsproto::Message>& msg) override {
		return nullptr;
	}
};

struct cancelingTestWriter : tsc::lsp::Writer {
	std::shared_ptr<tsc::lsp::Writer> writer;
	gostd::CancelFunc cancel;
	std::vector<std::shared_ptr<lsproto::Message>> messages;

	gostd::Error Write(
	    const std::shared_ptr<lsproto::Message>& msg) override {
		if (auto err = writer->Write(msg); err != nullptr) {
			return err;
		}
		messages.push_back(msg);
		if (messages.size() == 2) {
			cancel();
		}
		return nullptr;
	}
};

// TestServerShutdownNoDeadlock verifies that operations after shutdown
// don't block.
void TestServerShutdownNoDeadlock(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
		return;
	}

	auto fs = bundled::WrapFS(vfstest::FromMap(
	    {{"/test/tsconfig.json", "{}"}, {"/test/index.ts", "const x = 1;"}},
	    false));

	ServerOptions opts;
	opts.In = std::make_shared<shutdownTestReader>();
	opts.Out = std::make_shared<shutdownTestWriter>();
	opts.Err = gostd::io::discard();
	opts.Cwd = "/test";
	opts.FS = fs;
	opts.DefaultLibraryPath = bundled::LibPath();
	auto server = std::make_shared<Server>(opts);

	auto [bgCtx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
	gostd::Context ctx = bgCtx;
	server->backgroundCtx = ctx;

	// Start write loop to drain queue
	std::atomic<bool> writeLoopDone{false};
	std::thread writeLoopThread([&server, ctx, &writeLoopDone] {
		(void)server->writeLoop(ctx);
		writeLoopDone.store(true);
	});

	// Create session with the server's lifecycle context
	server->initStarted.store(true);
	project::SessionOptions sessOpts;
	sessOpts.CurrentDirectory = "/test";
	sessOpts.DefaultLibraryPath = bundled::LibPath();
	sessOpts.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	sessOpts.WatchEnabled = false;
	sessOpts.LoggingEnabled = true;
	project::SessionInit init;
	init.BackgroundCtx = ctx;
	init.Options = &sessOpts;
	init.FS = fs;
	init.Logger = server->logger.get();
	init.KeepAlive.push_back(server->logger);
	server->session = project::NewSession(&init);

	// Open a file to establish a project
	server->session->DidOpenFile(ctx, "file:///test/index.ts", 1,
	                             "const x = 1;",
	                             lsproto::LanguageKindTypeScript);
	server->session->WaitForBackgroundTasks();

	// Shutdown (cancel context and wait for write loop to exit)
	cancel();
	writeLoopThread.join();

	// Trigger operations that would log (these should not block)
	server->session->DidChangeFile(
	    ctx, "file:///test/index.ts", 2,
	    std::vector<
	        lsproto::TextDocumentContentChangePartialOrWholeDocument>{
	        [] {
		        lsproto::TextDocumentContentChangePartialOrWholeDocument
		            change;
		        change.WholeDocument = std::make_shared<
		            lsproto::TextDocumentContentChangeWholeDocument>();
		        change.WholeDocument->Text = "const x = 2;";
		        return change;
	        }(),
	    });
	(void)server->session->GetLanguageService(ctx, "file:///test/index.ts");
	server->session->WaitForBackgroundTasks();

	server->session->Close();
}
REGISTER_UNIT_TEST("lsp.TestServerShutdownNoDeadlock",
                   TestServerShutdownNoDeadlock);

void TestServerOutgoingQueueDoesNotBlockWithoutWriter(T* t) {
	t->Parallel();

	ServerOptions opts;
	opts.In = std::make_shared<shutdownTestReader>();
	opts.Out = std::make_shared<shutdownTestWriter>();
	opts.Err = gostd::io::discard();
	opts.Cwd = "/test";
	auto server = std::make_shared<Server>(opts);
	server->backgroundCtx = t->Context();

	auto params = std::make_shared<lsproto::LogMessageParams>();
	params->Type = lsproto::MessageTypeInfo;
	params->Message = "queued";
	auto msg = lsproto::WindowLogMessageInfo.NewNotificationMessage(params)
	               ->toMessage();

	// Go runs the sends in a goroutine selecting on `done` vs
	// t.Context().Done() (only cancelled at test end) — a blocked send is a
	// hang, surfaced by the runner's per-test timeout.
	for (int i = 0; i < 1000; i++) {
		if (auto err = server->send(msg); err != nullptr) {
			t->Fatal({err});
			return;
		}
	}
}
REGISTER_UNIT_TEST("lsp.TestServerOutgoingQueueDoesNotBlockWithoutWriter",
                   TestServerOutgoingQueueDoesNotBlockWithoutWriter);

// A response that exceeds the JSON encoder's nesting limit must fail only its
// request. The write loop must remain available to deliver subsequent
// responses.
void TestWriteLoopRecoversFromUnserializableResponse(T* t) {
	t->Parallel();

	auto [ctx, cancel] = gostd::contextWithCancel(t->Context());
	auto writer = std::make_shared<cancelingTestWriter>();
	writer->writer = tsc::lsp::ToWriter(gostd::io::discard());
	writer->cancel = cancel;
	ServerOptions opts;
	opts.In = std::make_shared<shutdownTestReader>();
	opts.Out = writer;
	opts.Err = gostd::io::discard();
	opts.Cwd = "/test";
	auto server = std::make_shared<Server>(opts);

	// defer cancel()
	struct scopeCancel {
		gostd::CancelFunc f;
		~scopeCancel() { f(); }
	} deferCancel{cancel};
	server->backgroundCtx = ctx;

	// A selection range whose parent chain is far deeper than the JSON
	// encoder's nesting limit.
	std::shared_ptr<lsproto::SelectionRange> deep;
	for (int i = 0; i < 20000; i++) {
		auto next = std::make_shared<lsproto::SelectionRange>();
		next->Parent = deep;
		deep = std::move(next);
	}
	std::vector<std::shared_ptr<lsproto::SelectionRange>> badResult{deep};
	auto badID = jsonrpc::NewIDString("bad");
	auto badResp = std::make_shared<lsproto::ResponseMessage>();
	badResp->ID = std::make_shared<jsonrpc::ID>(badID);
	badResp->Result = lsproto::AnyValue::of(badResult);
	if (auto err = server->send(badResp->toMessage()); err != nullptr) {
		t->Fatalf("failed to enqueue bad response: %v", {err});
		return;
	}

	// A subsequent well-formed response must still be delivered.
	auto goodID = jsonrpc::NewIDString("good");
	auto goodResp = std::make_shared<lsproto::ResponseMessage>();
	goodResp->ID = std::make_shared<jsonrpc::ID>(goodID);
	goodResp->Result = lsproto::AnyValue::of(lsproto::SelectionRangesOrNull{});
	if (auto err = server->send(goodResp->toMessage()); err != nullptr) {
		t->Fatalf("failed to enqueue good response: %v", {err});
		return;
	}

	auto loopErr = server->writeLoop(ctx);
	if (!gostd::errorIs(loopErr, gostd::errCanceled)) {
		t->Fatalf("write loop exited unexpectedly: %v", {loopErr});
		return;
	}

	bool sawError = false;
	bool sawGood = false;
	for (auto& msg : writer->messages) {
		if (msg->Kind != jsonrpc::MessageKind::Response) {
			t->Errorf("unexpected non-response message kind: %v",
			          {static_cast<int>(msg->Kind)});
			continue;
		}
		auto resp = msg->AsResponse();
		if (resp->ID != nullptr && resp->ID->str == badID.str &&
		    resp->ID->int_ == badID.int_) {
			if (resp->Error == nullptr) {
				t->Error({"expected an error response for the "
				          "unserializable request, got a result"});
			} else if (resp->Error->Code !=
			           static_cast<int32_t>(
			               lsproto::ErrorCodeInternalError)) {
				t->Errorf("error response code = %d, want %d",
				          {resp->Error->Code,
				           static_cast<int32_t>(
				               lsproto::ErrorCodeInternalError)});
			}
			sawError = true;
		} else if (resp->ID != nullptr && resp->ID->str == goodID.str &&
		           resp->ID->int_ == goodID.int_) {
			if (resp->Error != nullptr) {
				t->Errorf("expected a successful response for the good "
				          "request, got error: %v",
				          {resp->Error->String()});
			}
			sawGood = true;
		} else {
			t->Errorf("unexpected response id: %v",
			          {resp->ID ? resp->ID->String() : "<nil>"});
		}
	}

	if (!sawError) {
		t->Error({"did not receive an error response for the "
		          "unserializable request"});
	}
	if (!sawGood) {
		t->Error({"did not receive the subsequent well-formed response "
		          "(write loop likely died)"});
	}
}
REGISTER_UNIT_TEST("lsp.TestWriteLoopRecoversFromUnserializableResponse",
                   TestWriteLoopRecoversFromUnserializableResponse);

}  // namespace
