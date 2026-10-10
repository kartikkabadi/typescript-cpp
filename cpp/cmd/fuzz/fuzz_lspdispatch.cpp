// fuzz_lspdispatch — libFuzzer driver for LSP request dispatch.
//
// fuzz_lspframing stops at jsonrpc framing + lsproto::Message decode.
// This driver continues into dispatch: it decodes the fuzz bytes as a
// JSON-RPC message body, then — when it is a request/notification —
// calls Server::handleRequestOrNotification on a persistent, initialized
// Server (the same entry dispatchLoop uses). That exercises:
//   lsproto::Message::unmarshalJSON — id/method/params field typing
//     (wrong JSON types per field, missing fields)
//   per-method RequestMessage::UnmarshalParams<Req> — wrong-typed or
//     malformed params for every registered method
//   handler dispatch itself — method lookup, session routing, response
//     marshalling into the outgoing queue
//
// The shared server is initialized by dispatching a real `initialize`
// request through handleRequestOrNotification (which is what sets
// initStarted and initializationOptions — the server's own invariants),
// followed by a didOpen for /test/index.ts so document methods have a
// project to work in. A detached thread drains the outgoing queue so
// responses don't accumulate. exit/shutdown are dispatched like
// everything else — exit returns io.EOF as in Go, which we ignore.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/gostd/gostd.h"
#include "internal/json/json.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

#include "cmd/fuzz/fuzz_common.h"

namespace {

struct nullReader : tsc::lsp::Reader {
	std::pair<std::shared_ptr<tsc::lsp::lsproto::Message>,
	          tsc::gostd::Error>
	Read() override {
		return {nullptr, tsc::gostd::io::errEOF};
	}
};

struct nullWriter : tsc::lsp::Writer {
	tsc::gostd::Error Write(
	    const std::shared_ptr<tsc::lsp::lsproto::Message>&) override {
		return nullptr;
	}
};

void dispatchJson(tsc::lsp::Server* server, std::string_view json) {
	tsc::lsp::lsproto::Message msg;
	if (!tsc::json::unmarshal(json, &msg).empty()) {
		return;
	}
	auto req = msg.AsRequest();
	if (req == nullptr) {
		return;
	}
	auto [doAsyncWork, err] =
	    server->handleRequestOrNotification(server->backgroundCtx, req);
	if (doAsyncWork != nullptr) {
		(void)doAsyncWork();
	}
}

// Lazily-initialized shared server, brought up by dispatching real
// initialize + didOpen messages so server state matches the Go protocol
// invariants (initStarted, initializationOptions, session).
std::shared_ptr<tsc::lsp::Server> getServer() {
	static std::shared_ptr<tsc::lsp::Server> server = [] {
		tsc::lsp::ServerOptions opts;
		opts.In = std::make_shared<nullReader>();
		opts.Out = std::make_shared<nullWriter>();
		opts.Err = tsc::gostd::io::discard();
		opts.Cwd = "/test";
		opts.FS = tsc::bundled::WrapFS(tsc::vfs::vfstest::FromMap(
		    {{"/test/tsconfig.json", "{}"},
		     {"/test/index.ts", "const x = 1;"}},
		    false));
		opts.DefaultLibraryPath = tsc::bundled::LibPath();
		auto server = tsc::lsp::NewServer(opts);
		server->backgroundCtx = tsc::gostd::contextBackground();

		// Drain outgoingQueue: play the client side — reply to
		// server→client requests (e.g. client/registerCapability) the way
		// readLoop resolves them through pendingServerRequests.
		std::thread([server] {
			for (;;) {
				auto [m, err] =
				    server->outgoingQueue.Get(server->backgroundCtx);
				if (err != nullptr || !m.has_value() || *m == nullptr) {
					continue;
				}
				auto& msg = **m;
				if (msg.Kind != tsc::jsonrpc::MessageKind::Request) {
					continue;
				}
				auto req = msg.AsRequest();
				if (req == nullptr || req->ID == nullptr) {
					continue;
				}
				std::string respJson =
				    "{\"jsonrpc\":\"2.0\",\"id\":" +
				    req->ID->marshalJSON() + ",\"result\":null}";
				tsc::lsp::lsproto::Message respMsg;
				if (!tsc::json::unmarshal(respJson, &respMsg).empty()) {
					continue;
				}
				auto resp = respMsg.AsResponse();
				if (resp == nullptr || resp->ID == nullptr) {
					continue;
				}
				std::lock_guard<std::mutex> lk(
				    server->pendingServerRequestsMu);
				if (auto it = server->pendingServerRequests.find(*resp->ID);
				    it != server->pendingServerRequests.end()) {
					it->second->send(resp);
					it->second->close();
					server->pendingServerRequests.erase(it);
				}
			}
		}).detach();

		dispatchJson(
		    server.get(),
		    R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"processId":null,"rootUri":"file:///test","capabilities":{"workspace":{"didChangeWatchedFiles":{"dynamicRegistration":false}}},"clientInfo":{"name":"fuzz"}}})");
		dispatchJson(
		    server.get(),
		    R"({"jsonrpc":"2.0","method":"initialized","params":{}})");
		dispatchJson(
		    server.get(),
		    R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///test/index.ts","languageId":"typescript","version":1,"text":"const x = 1;"}}})");
		if (server->session != nullptr) {
			server->session->WaitForBackgroundTasks();
		}
		return server;
	}();
	return server;
}

// Build the server at program startup, before libFuzzer begins timing
// units — session spin-up and the initial project load take seconds,
// like a real LSP process coming up.
struct warmup {
	warmup() { (void)getServer(); }
};
warmup g_warmup;

void run(const uint8_t* data, size_t size) {
	std::string_view text(reinterpret_cast<const char*>(data), size);
	auto server = getServer();

	// Decode through the same path lspReader::Read uses after framing.
	tsc::lsp::lsproto::Message msg;
	auto err = tsc::json::unmarshal(text, &msg);
	if (!err.empty()) {
		return;
	}
	if (msg.Kind != tsc::jsonrpc::MessageKind::Request &&
	    msg.Kind != tsc::jsonrpc::MessageKind::Notification) {
		return;
	}
	auto req = msg.AsRequest();
	if (req == nullptr) {
		return;
	}
	auto [doAsyncWork, herr] = server->handleRequestOrNotification(
	    server->backgroundCtx, req);
	if (doAsyncWork != nullptr) {
		(void)doAsyncWork();
	}
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size == 0) {
		return 0;
	}
	fuzz::runOnBigStack([](const uint8_t* d, size_t n) { run(d, n); }, data,
	                  size);
	return 0;
}
