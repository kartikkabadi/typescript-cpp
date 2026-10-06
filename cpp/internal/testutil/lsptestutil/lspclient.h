// === slice: fourslash ===
// lspclient.go — test client that talks to an lsp.Server over real
// "Content-Length"-framed JSON pipes, exactly like a real editor.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "internal/core/types.h"
#include "internal/fourslash/fourslash_deps.h"
#include "internal/fourslash/goutil.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/lsp/lsproto/lsproto.h"

namespace tsc::lsptestutil {

// ServerRequestHandler handles server-initiated requests and returns the
// response to send back. — lspclient.go:48.
using ServerRequestHandler =
    std::function<std::shared_ptr<lsproto::ResponseMessage>(
        gostd::Context, const std::shared_ptr<lsproto::RequestMessage>&)>;

// ServerNotificationHandler handles server-initiated notifications (e.g.,
// $/progress). — lspclient.go:51.
using ServerNotificationHandler =
    std::function<void(gostd::Context,
                       const std::shared_ptr<lsproto::RequestMessage>&)>;

// LSPClient provides infrastructure for communicating with an LSP server in
// tests. — lspclient.go:54.
struct LSPClient {
	lsp::Server* Server = nullptr;
	lsp::Writer* inputWriter = nullptr;
	lsp::Reader* outputReader = nullptr;
	int32_t id = 0;
	gostd::Context ctx;

	// inputWriterMu serializes writes to the server. The test goroutine
	// (sending requests/notifications) and the MessageRouter goroutine
	// (sending responses to server-initiated requests) both write to the
	// same stream; a single message is written as multiple underlying writes
	// (header, body, flush), so concurrent writers must not interleave.
	std::mutex inputWriterMu;

	// onServerRequest handles server-initiated requests (e.g.
	// workspace/configuration). If nil, all server requests receive a
	// MethodNotFound error.
	ServerRequestHandler onServerRequest;

	// OnServerNotification handles server-initiated notifications (e.g.,
	// $/progress). If nil, notifications are ignored.
	ServerNotificationHandler OnServerNotification;

	// Async message handling
	std::unordered_map<jsonrpc::ID,
	                   std::shared_ptr<gostd::Chan<
	                       std::shared_ptr<lsproto::ResponseMessage>>>,
	                   jsonrpc::IDHash>
	    pendingRequests;
	std::mutex pendingRequestsMu;

	// writeToServer writes a message to the server, serializing concurrent
	// writers. — lspclient.go:88.
	gostd::Error writeToServer(lsproto::Message* msg);

	// NextID returns the next request ID. — lspclient.go:127.
	int32_t NextID();

	// MessageRouter runs in a goroutine and routes incoming messages from
	// the server. — lspclient.go:139.
	gostd::Error MessageRouter(gostd::Context ctx);

	// handleResponse routes a response message to the waiting request
	// goroutine. — lspclient.go:179.
	void handleResponse(
	    gostd::Context ctx,
	    const std::shared_ptr<lsproto::ResponseMessage>& resp);

	// handleServerRequest handles requests initiated by the server (e.g.,
	// workspace/configuration). — lspclient.go:199.
	gostd::Error handleServerRequest(
	    gostd::Context ctx, const std::shared_ptr<lsproto::RequestMessage>& req);

	// WriteMsg validates and sends a message to the server.
	// This is an untyped low-level method; prefer SendRequest and
	// SendNotification for typed interactions. — lspclient.go:231.
	void WriteMsg(gostd::testing::T* t, lsproto::Message* msg);

	// SendRequest sends a typed request and waits for the response.
	// — lspclient.go:237.
	template <typename Params, typename Resp>
	std::tuple<std::shared_ptr<lsproto::Message>, Resp, bool>
	SendRequest(gostd::testing::T* t,
	            const lsproto::RequestInfo<Params, Resp>& info,
	            const Params& params) {
		auto id = NextID();
		auto reqID = lsproto::NewID(lsproto::IntegerOrString{
		.Integer = std::make_shared<int32_t>(id)});
		auto req = info.NewRequestMessage(reqID, params);

		auto [resp, ok] = SendRequestWorker(t, req.get(), reqID.get());
		if (!ok) {
			return {nullptr, Resp{}, false};
		}
		// The result arrives as a raw json.Value; decode it into Resp.
		auto [result, err] = info.UnmarshalResult(resp->Result);
		return {resp->toMessage(), std::move(result), err == nullptr};
	}

	// SendRequestAsync sends a typed request and returns a waiter for its
	// response. — lspclient.go:252.
	template <typename Params, typename Resp>
	std::function<std::tuple<std::shared_ptr<lsproto::Message>, Resp, bool>()>
	SendRequestAsync(gostd::testing::T* t,
	                 const lsproto::RequestInfo<Params, Resp>& info,
	                 const Params& params) {
		auto id = NextID();
		auto reqID = lsproto::NewID(lsproto::IntegerOrString{
		.Integer = std::make_shared<int32_t>(id)});
		auto req = info.NewRequestMessage(reqID, params);

		auto responseChan = startRequestWorker(t, req.get(), reqID.get());
		return [this, t, reqID, responseChan, &info]()
		           -> std::tuple<std::shared_ptr<lsproto::Message>, Resp,
		                         bool> {
			auto [resp, ok] = waitForResponse(t, reqID.get(), responseChan);
			if (!ok) {
				return {nullptr, Resp{}, false};
			}
			auto [result, err] = info.UnmarshalResult(resp->Result);
			return {resp->toMessage(), std::move(result), err == nullptr};
		};
	}

	// SendRequestWorker is an untyped version of SendRequest. Prefer to use
	// SendRequest when possible. — lspclient.go:272.
	std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool>
	SendRequestWorker(gostd::testing::T* t, lsproto::RequestMessage* req,
	                  jsonrpc::ID* reqID);

	// startRequestWorker — lspclient.go:277.
	std::shared_ptr<gostd::Chan<std::shared_ptr<lsproto::ResponseMessage>>>
	startRequestWorker(gostd::testing::T* t, lsproto::RequestMessage* req,
	                   jsonrpc::ID* reqID);

	// waitForResponse — lspclient.go:288.
	std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool>
	waitForResponse(
	    gostd::testing::T* t, jsonrpc::ID* reqID,
	    const std::shared_ptr<
	        gostd::Chan<std::shared_ptr<lsproto::ResponseMessage>>>&
	        responseChan);

	// SendNotification sends a typed notification. — lspclient.go:307.
	template <typename Params>
	void SendNotification(gostd::testing::T* t,
	                      const lsproto::NotificationInfo<Params>& info,
	                      const Params& params) {
		auto notification = info.NewNotificationMessage(params);
		WriteMsg(t, notification->toMessage().get());
	}

	// SetCompilerOptionsForInferredProjects — lspclient.go:316.
	void SetCompilerOptionsForInferredProjects(
	    tsc::CompilerOptions* options);
};

// clientTransport wires a test client to a server using real LSP
// "Content-Length"-framed JSON streamed over byte pipes, exactly like
// communication with a real editor. This exercises the full
// marshal/unmarshal round-trip of every protocol data structure. The two
// directions are:
//
//	client --(clientOut)--> serverIn --> server
//	server --(serverOut)--> clientIn --> client
//
// — lspclient.go:25.
struct clientTransport {
	std::shared_ptr<lsp::Reader> serverIn;
	std::shared_ptr<lsp::Writer> serverOut;
	std::shared_ptr<lsp::Reader> clientIn;
	std::shared_ptr<lsp::Writer> clientOut;
	std::function<void()> closeClientOut;
	std::function<void()> closeServerOut;
};

clientTransport newClientTransport();

// NewLSPClient creates an LSPClient wrapping the given server and pipes.
// — lspclient.go:95.
std::pair<std::shared_ptr<LSPClient>, std::function<gostd::Error()>>
NewLSPClient(gostd::testing::T* t, lsp::ServerOptions& serverOpts,
             const ServerRequestHandler& onServerRequest);

} // namespace tsc::lsptestutil
