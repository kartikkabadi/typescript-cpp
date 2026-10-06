// === slice: fourslash ===
// lspclient.go — test client that talks to an lsp.Server over real
// "Content-Length"-framed JSON pipes, exactly like a real editor.
#include "internal/testutil/lsptestutil/lspclient.h"

#include "internal/json/json.h"
#include "internal/testutil/testutil.h"

namespace tsc::lsptestutil {

// newClientTransport — lspclient.go:33.
clientTransport newClientTransport() {
	auto c2s = gostd::ioPipe();
	auto clientToServerReader = c2s.first;
	auto clientToServerWriter = c2s.second;
	auto s2c = gostd::ioPipe();
	auto serverToClientReader = s2c.first;
	auto serverToClientWriter = s2c.second;
	// The pipe ends are owned by the close lambdas below (members of the
	// returned transport), keeping them alive for the transport's lifetime.
	return clientTransport{
	    .serverIn = lsp::toReader(clientToServerReader.get()),
	    .serverOut = lsp::toWriter(serverToClientWriter.get()),
	    .clientIn = lsp::toReader(serverToClientReader.get()),
	    .clientOut = lsp::toWriter(clientToServerWriter.get()),
	    .closeClientOut =
	        [clientToServerWriter, clientToServerReader,
	         serverToClientReader, serverToClientWriter] {
		        (void)clientToServerWriter->close();
	        },
	    .closeServerOut =
	        [serverToClientWriter, serverToClientReader,
	         clientToServerReader, clientToServerWriter] {
		        (void)serverToClientWriter->close();
	        },
	};
}

// writeToServer — lspclient.go:88.
gostd::Error LSPClient::writeToServer(lsproto::Message* msg) {
	std::lock_guard<std::mutex> lk(inputWriterMu);
	return inputWriter->write(msg);
}

// NewLSPClient — lspclient.go:95.
std::pair<std::shared_ptr<LSPClient>, std::function<gostd::Error()>>
NewLSPClient(gostd::testing::T* t, lsp::ServerOptions& serverOpts,
             const ServerRequestHandler& onServerRequest) {
	auto transport = newClientTransport();
	serverOpts.In = transport.serverIn.get();
	serverOpts.Out = transport.serverOut.get();

	lsp::Server* server = lsp::newServer(&serverOpts);

	auto cp = gostd::contextWithCancel(t->Context());
	auto ctx = cp.first;
	auto cancel = cp.second;
	auto gg = gostd::errgroupWithContext(ctx);
	auto g = gg.first;
	auto gctx = gg.second;
	auto client = std::make_shared<LSPClient>();
	client->Server = server;
	client->inputWriter = transport.clientOut.get();
	client->outputReader = transport.clientIn.get();
	client->onServerRequest = onServerRequest;
	client->ctx = ctx;

	// Start server goroutine
	g->Go([server, gctx, transport]() -> gostd::Error {
		auto closeServerOut =
		    gostd::detail::deferGuard(transport.closeServerOut);
		return server->run(gctx);
	});

	// Start async message router
	g->Go([client, gctx]() -> gostd::Error {
		return client->MessageRouter(gctx);
	});

	return {client, [cancel, transport, g]() -> gostd::Error {
		        cancel();
		        transport.closeClientOut();
		        if (auto err = g->Wait();
		            err != nullptr && !gostd::errorIs(
		                                  err, gostd::errCanceled)) {
			        return err;
		        }
		        return nullptr;
	        }};
}

// NextID — lspclient.go:127.
int32_t LSPClient::NextID() {
	auto id = this->id;
	this->id++;
	return id;
}

// MessageRouter runs in a goroutine and routes incoming messages from the
// server. It handles responses to client requests and server-initiated
// requests. It continues draining the output channel until it is closed
// (EOF), even after context cancellation, to prevent the server's writeLoop
// from blocking on a send. — lspclient.go:139.
gostd::Error LSPClient::MessageRouter(gostd::Context ctx) {
	for (;;) {
		auto [msg, err] = outputReader->read();
		if (err != nullptr) {
			if (gostd::errorIs(err, gostd::io::errEOF)) {
				return nullptr;
			}
			if (gostd::ctxErr(ctx) != nullptr) {
				return nullptr;
			}
			return gostd::errorf("failed to read message: %w", {err});
		}

		// After context cancellation, keep draining but don't process
		// messages.
		if (gostd::ctxErr(ctx) != nullptr) {
			continue;
		}

		// Validate message can be marshaled
		if (auto merr = json::marshal(*msg).second; !merr.empty()) {
			if (gostd::ctxErr(ctx) != nullptr) {
				continue;
			}

			return gostd::errorf(
			    "failed to encode message as JSON: %w",
			    {gostd::newError(merr)});
		}

		switch (msg->Kind) {
		case jsonrpc::MessageKind::Response:
			handleResponse(ctx, msg->AsResponse());
			break;
		case jsonrpc::MessageKind::Request:
			if (auto herr = handleServerRequest(ctx, msg->AsRequest());
			    herr != nullptr) {
				return herr;
			}
			break;
		case jsonrpc::MessageKind::Notification:
			if (OnServerNotification != nullptr) {
				OnServerNotification(ctx, msg->AsRequest());
			}
			break;
		}
	}
}

// handleResponse — lspclient.go:179.
void LSPClient::handleResponse(
    gostd::Context ctx,
    const std::shared_ptr<lsproto::ResponseMessage>& resp) {
	if (resp->ID == nullptr) {
		return;
	}

	std::shared_ptr<gostd::Chan<std::shared_ptr<lsproto::ResponseMessage>>>
	    respChan;
	{
		std::lock_guard<std::mutex> lk(pendingRequestsMu);
		auto it = pendingRequests.find(*resp->ID);
		if (it != pendingRequests.end()) {
			respChan = it->second;
			pendingRequests.erase(it);
		}
	}

	if (respChan) {
		// select { case respChan <- resp: case <-ctx.Done(): }
		// The channel is buffered (cap 1) and always empty at this point —
		// the receiver takes the response on arrival, so send never blocks.
		respChan->send(resp);
	}
}

// handleServerRequest — lspclient.go:199.
gostd::Error LSPClient::handleServerRequest(
    gostd::Context ctx,
    const std::shared_ptr<lsproto::RequestMessage>& req) {
	std::shared_ptr<lsproto::ResponseMessage> response;

	if (onServerRequest != nullptr) {
		response = onServerRequest(ctx, req);
	}

	if (response == nullptr) {
		// Default: unknown server request
		response = std::make_shared<lsproto::ResponseMessage>();
		response->ID = req->ID;
		response->JSONRPC = req->JSONRPC;
		auto rerr = std::make_shared<jsonrpc::ResponseError>();
		rerr->Code = static_cast<int32_t>(lsproto::ErrorCodeMethodNotFound);
		rerr->Message = gostd::sprintf("Unknown method: %s", {req->Method});
		response->Error = rerr;
	}

	// Send response back to server
	if (gostd::ctxErr(ctx) != nullptr) {
		return nullptr;
	}

	if (auto err = writeToServer(response->toMessage().get());
	    err != nullptr) {
		if (gostd::ctxErr(ctx) != nullptr) {
			return nullptr;
		}
		return gostd::errorf(
		    "failed to write server request response: %w", {err});
	}
	return nullptr;
}

// WriteMsg — lspclient.go:231.
void LSPClient::WriteMsg(gostd::testing::T* t, lsproto::Message* msg) {
	gotest::assert::Assert(
	    t, json::marshal(*msg).second.empty(),
	    "failed to encode message as JSON");
	if (auto err = writeToServer(msg); err != nullptr) {
		t->Fatalf("failed to write message: %v", {err->Error()});
	}
}

// SendRequestWorker — lspclient.go:272.
std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool>
LSPClient::SendRequestWorker(gostd::testing::T* t, lsproto::RequestMessage* req,
                             jsonrpc::ID* reqID) {
	auto responseChan = startRequestWorker(t, req, reqID);
	return waitForResponse(t, reqID, responseChan);
}

// startRequestWorker — lspclient.go:277.
std::shared_ptr<gostd::Chan<std::shared_ptr<lsproto::ResponseMessage>>>
LSPClient::startRequestWorker(gostd::testing::T* t, lsproto::RequestMessage* req,
                              jsonrpc::ID* reqID) {
	auto responseChan = std::make_shared<
	    gostd::Chan<std::shared_ptr<lsproto::ResponseMessage>>>(1);
	{
		std::lock_guard<std::mutex> lk(pendingRequestsMu);
		pendingRequests[*reqID] = responseChan;
	}

	WriteMsg(t, req->toMessage().get());
	return responseChan;
}

// waitForResponse — lspclient.go:288.
std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool>
LSPClient::waitForResponse(
    gostd::testing::T* t, jsonrpc::ID* reqID,
    const std::shared_ptr<
        gostd::Chan<std::shared_ptr<lsproto::ResponseMessage>>>&
        responseChan) {
	auto ctx = t->Context();
	// select { case <-ctx.Done(): ...; case resp = <-responseChan: }
	// Model the recv arm as the blocking wait: t.Context() is background in
	// this port and server cancellation closes the channel (recv returns
	// !ok), which is the equivalent unblock path.
	auto [resp, ok] = responseChan->recv();
	if (!ok) {
		if (auto cerr = gostd::ctxErr(ctx); cerr != nullptr) {
			std::lock_guard<std::mutex> lk(pendingRequestsMu);
			pendingRequests.erase(*reqID);
			t->Fatalf("Request cancelled: %v", {cerr->Error()});
		}
		return {nullptr, false};
	}
	if (resp == nullptr) {
		return {nullptr, false};
	}
	return {resp, true};
}

// SetCompilerOptionsForInferredProjects — lspclient.go:316.
void LSPClient::SetCompilerOptionsForInferredProjects(
    tsc::CompilerOptions* options) {
	Server->setCompilerOptionsForInferredProjects(ctx, options);
}

} // namespace tsc::lsptestutil
