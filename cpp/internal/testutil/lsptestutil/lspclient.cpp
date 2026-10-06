// lspclient.cpp — port of tsc/internal/testutil/lsptestutil/lspclient.go.
#include "internal/testutil/lsptestutil/lspclient.h"

#include <ostream>
#include <streambuf>
#include <thread>

#include "internal/json/json.h"
#include "internal/testutil/testutil.h" // gotest::assert

namespace tsc::testutil::lsptestutil {

// ---------------------------------------------------------------------------
// io.Pipe — synchronous unbuffered byte pipe.
// ---------------------------------------------------------------------------

std::pair<int, gostd::Error> pipeReader::read(std::span<char> out) {
	std::unique_lock lk(p->mu);
	p->cv.wait(lk, [&] { return !p->buf.empty() || p->closed; });
	if (p->buf.empty()) {
		// closed with nothing buffered → io.EOF
		return {0, gostd::io::errEOF};
	}
	size_t n = std::min(out.size(), p->buf.size());
	std::copy_n(p->buf.begin(), n, out.begin());
	p->buf.erase(0, n);
	lk.unlock();
	p->cv.notify_all();
	return {(int)n, nullptr};
}

std::pair<int, gostd::Error> pipeWriter::write(std::string_view data) {
	std::unique_lock lk(p->mu);
	int total = 0;
	while (!data.empty()) {
		p->cv.wait(lk, [&] { return p->buf.empty() || p->closed; });
		if (p->closed) {
			return {total,
			        gostd::newError("io: write on closed pipe")};
		}
		p->buf = std::string(data);
		lk.unlock();
		p->cv.notify_all();
		lk.lock();
		// A Write blocks until all bytes have been Read.
		p->cv.wait(lk, [&] { return p->buf.empty() || p->closed; });
		if (p->closed && !p->buf.empty()) {
			return {total,
			        gostd::newError("io: write on closed pipe")};
		}
		total += (int)data.size();
		data = {};
	}
	return {total, nullptr};
}

gostd::Error pipeWriter::close() {
	{
		std::lock_guard lk(p->mu);
		p->closed = true;
	}
	p->cv.notify_all();
	return nullptr;
}

// ioPipe — io.Pipe().
std::pair<std::shared_ptr<pipeReader>, std::shared_ptr<pipeWriter>>
ioPipe() {
	auto p = std::make_shared<pipe>();
	auto r = std::make_shared<pipeReader>();
	r->p = p;
	auto w = std::make_shared<pipeWriter>();
	w->p = p;
	return {r, w};
}

// ---------------------------------------------------------------------------
// clientTransport — lspclient.go:27-47.
// ---------------------------------------------------------------------------

clientTransport newClientTransport() {
	// Structured bindings can't be captured by lambdas (clang-15) — use
	// named pairs.
	auto c2s = ioPipe();
	auto clientToServerReader = c2s.first;
	auto clientToServerWriter = c2s.second;
	auto s2c = ioPipe();
	auto serverToClientReader = s2c.first;
	auto serverToClientWriter = s2c.second;
	clientTransport t;
	t.serverIn = lsp::ToReader(clientToServerReader);
	t.serverOut = lsp::ToWriter(serverToClientWriter);
	t.clientIn = lsp::ToReader(serverToClientReader);
	t.clientOut = lsp::ToWriter(clientToServerWriter);
	t.closeClientOut = [clientToServerWriter] {
		clientToServerWriter->close();
	};
	t.closeServerOut = [serverToClientWriter] {
		serverToClientWriter->close();
	};
	return t;
}

// ---------------------------------------------------------------------------
// responseChan — buffered (cap 1) channel of *lsproto.ResponseMessage.
// ---------------------------------------------------------------------------

void responseChan::send(std::shared_ptr<lsproto::ResponseMessage> v) {
	{
		std::lock_guard lk(mu);
		value = std::move(v);
		has = true;
	}
	cv.notify_all();
}

std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool>
responseChan::recv(const gostd::Context& ctx) {
	std::unique_lock lk(mu);
	// select { resp = <-responseChan; <-ctx.Done() }: register a wake on
	// ctx cancellation.
	auto stop = gostd::contextAfterFunc(ctx, [&] { cv.notify_all(); });
	cv.wait(lk, [&] { return has || gostd::ctxErr(ctx) != nullptr; });
	if (stop) stop();
	if (has) {
		return {std::move(value), true};
	}
	return {nullptr, false};
}

// ---------------------------------------------------------------------------
// errGroup — golang.org/x/sync/errgroup.
// ---------------------------------------------------------------------------

void errGroup::Go(std::function<gostd::Error()> fn) {
	std::lock_guard lk(mu);
	threads.emplace_back([this, fn = std::move(fn)] {
		gostd::Error err = fn();
		bool first = false;
		if (err != nullptr) {
			std::lock_guard lk2(mu);
			if (firstErr == nullptr) {
				firstErr = err;
				first = true;
			}
		}
		// WithContext: cancel the derived ctx on the first error,
		// outside the lock.
		if (first && cancel) {
			cancel();
		}
	});
}

gostd::Error errGroup::Wait() {
	std::vector<std::thread> ts;
	{
		std::lock_guard lk(mu);
		ts.swap(threads);
	}
	for (auto& t : ts) {
		if (t.joinable()) t.join();
	}
	std::lock_guard lk(mu);
	// WithContext: Wait cancels the ctx too.
	if (cancel) cancel();
	return firstErr;
}

// errgroupWithContext — errgroup.WithContext.
std::pair<std::shared_ptr<errGroup>, gostd::Context>
errgroupWithContext(const gostd::Context& ctx) {
	auto [child, cancel] = gostd::contextWithCancel(ctx);
	auto g = std::make_shared<errGroup>();
	g->cancel = cancel;
	return {g, child};
}

// ---------------------------------------------------------------------------
// LSPClient — lspclient.go:56-326.
// ---------------------------------------------------------------------------

namespace {

// discard — io.Discard.
struct discardBuf final : std::streambuf {
	int overflow(int c) override { return c; }
};
std::ostream& discardStream() {
	static discardBuf buf;
	static std::ostream os(&buf);
	return os;
}

// marshalWriteErr — json.MarshalWrite(io.Discard, msg) as a gostd::Error.
gostd::Error marshalDiscard(const lsproto::Message& msg) {
	std::string err = json::marshalWrite(discardStream(), msg);
	return err.empty() ? nullptr : gostd::newError(err);
}

}  // namespace

// writeToServer — lspclient.go:84.
gostd::Error
LSPClient::writeToServer(const std::shared_ptr<lsproto::Message>& msg) {
	std::lock_guard lk(inputWriterMu);
	return inputWriter->Write(msg);
}

// NewLSPClient — lspclient.go:91.
std::pair<std::shared_ptr<LSPClient>, std::function<gostd::Error()>>
NewLSPClient(gostd::testing::T* t, lsp::ServerOptions serverOpts,
             ServerRequestHandler onServerRequest) {
	clientTransport transport = newClientTransport();
	serverOpts.In = transport.serverIn;
	serverOpts.Out = transport.serverOut;

	auto server = lsp::NewServer(serverOpts);

	auto ctxPair = gostd::contextWithCancel(t->Context());
	auto cancel = ctxPair.second;
	auto eg = errgroupWithContext(ctxPair.first);
	auto g = eg.first;
	auto ctx = eg.second;
	auto client = std::make_shared<LSPClient>();
	client->Server = server;
	client->inputWriter = transport.clientOut;
	client->outputReader = transport.clientIn;
	client->pendingRequests = {};
	client->onServerRequest = std::move(onServerRequest);
	client->ctx = ctx;

	// Start server goroutine
	auto closeServerOut = transport.closeServerOut;
	g->Go([server, ctx, closeServerOut] {
		struct deferClose {
			const std::function<void()>& f;
			~deferClose() { f(); }
		} d{closeServerOut};
		auto e = server->Run(ctx);
		return e;
	});

	// Start async message router
	g->Go([client, ctx] { return client->MessageRouter(ctx); });

	std::function<gostd::Error()> closeFn =
	    [cancel, closeClientOut = transport.closeClientOut,
	     g]() -> gostd::Error {
		cancel();
		closeClientOut();
		gostd::Error err = g->Wait();
		if (err != nullptr && err != gostd::errCanceled) {
			return err;
		}
		return nullptr;
	};
	return {client, std::move(closeFn)};
}

// MessageRouter — lspclient.go:141.
gostd::Error LSPClient::MessageRouter(const gostd::Context& ctx) {
	for (;;) {
		auto [msg, err] = outputReader->Read();
		if (err != nullptr) {
			if (err == gostd::io::errEOF) {
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
		if (gostd::Error merr = marshalDiscard(*msg); merr != nullptr) {
			if (gostd::ctxErr(ctx) != nullptr) {
				continue;
			}
			return gostd::errorf(
			    "failed to encode message as JSON: %w", {merr});
		}

		switch (msg->Kind) {
		case jsonrpc::MessageKind::Response:
			handleResponse(ctx, msg->AsResponse());
			break;
		case jsonrpc::MessageKind::Request:
			if (gostd::Error rerr =
			        handleServerRequest(ctx, msg->AsRequest());
			    rerr != nullptr) {
				return rerr;
			}
			break;
		case jsonrpc::MessageKind::Notification:
			if (OnServerNotification) {
				OnServerNotification(ctx, msg->AsRequest());
			}
			break;
		}
	}
}

// handleResponse — lspclient.go:184.
void LSPClient::handleResponse(
    const gostd::Context& ctx,
    const std::shared_ptr<lsproto::ResponseMessage>& resp) {
	if (!resp->ID) {
		return;
	}

	std::shared_ptr<responseChan> respChan;
	{
		std::lock_guard lk(pendingRequestsMu);
		auto it = pendingRequests.find(*resp->ID);
		if (it != pendingRequests.end()) {
			respChan = it->second;
			pendingRequests.erase(it);
		}
	}

	if (respChan) {
		// select { respChan <- resp; <-ctx.Done() }
		if (gostd::ctxErr(ctx) == nullptr) {
			respChan->send(resp);
		}
	}
}

// handleServerRequest — lspclient.go:207.
gostd::Error LSPClient::handleServerRequest(
    const gostd::Context& ctx,
    const std::shared_ptr<lsproto::RequestMessage>& req) {
	std::shared_ptr<lsproto::ResponseMessage> response;

	if (onServerRequest) {
		response = onServerRequest(ctx, req);
	}

	if (!response) {
		// Default: unknown server request
		response = std::make_shared<lsproto::ResponseMessage>();
		response->ID = req->ID;
		response->JSONRPC = req->JSONRPC;
		auto rerr = std::make_shared<jsonrpc::ResponseError>();
		rerr->Code = static_cast<int32_t>(lsproto::ErrorCodeMethodNotFound);
		rerr->Message = gostd::sprintf("Unknown method: %s",
		                             {req->Method});
		response->Error = rerr;
	}

	// Send response back to server
	if (gostd::ctxErr(ctx) != nullptr) {
		return nullptr;
	}

	if (gostd::Error err = writeToServer(response->toMessage());
	    err != nullptr) {
		if (gostd::ctxErr(ctx) != nullptr) {
			return nullptr;
		}
		return gostd::errorf(
		    "failed to write server request response: %w", {err});
	}
	return nullptr;
}

// WriteMsg — lspclient.go:242.
void LSPClient::WriteMsg(gostd::testing::T* t,
                         const std::shared_ptr<lsproto::Message>& msg) {
	gotest::assert::NilError(t, marshalDiscard(*msg),
	                         "failed to encode message as JSON");
	if (gostd::Error err = writeToServer(msg); err != nullptr) {
		t->Fatalf("failed to write message: %v", {err});
	}
}

// SendRequestWorker — lspclient.go:282.
std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool>
LSPClient::SendRequestWorker(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::RequestMessage>& req,
    const std::shared_ptr<jsonrpc::ID>& reqID) {
	auto respChan = startRequestWorker(t, req, reqID);
	return waitForResponse(t, reqID, respChan);
}

// startRequestWorker — lspclient.go:287.
std::shared_ptr<responseChan> LSPClient::startRequestWorker(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::RequestMessage>& req,
    const std::shared_ptr<jsonrpc::ID>& reqID) {
	auto respChan = std::make_shared<responseChan>();
	{
		std::lock_guard lk(pendingRequestsMu);
		pendingRequests[*reqID] = respChan;
	}
	WriteMsg(t, req->toMessage());
	return respChan;
}

// waitForResponse — lspclient.go:297.
std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool>
LSPClient::waitForResponse(
    gostd::testing::T* t, const std::shared_ptr<jsonrpc::ID>& reqID,
    const std::shared_ptr<responseChan>& respChan) {
	auto ctx = t->Context();
	auto [resp, ok] = respChan->recv(ctx);
	if (!ok) {
		// <-ctx.Done()
		{
			std::lock_guard lk(pendingRequestsMu);
			pendingRequests.erase(*reqID);
		}
		t->Fatalf("Request cancelled: %v", {gostd::ctxErr(ctx)});
	}
	if (resp == nullptr) {
		return {nullptr, false};
	}
	return {resp, true};
}

// SetCompilerOptionsForInferredProjects — lspclient.go:324.
void LSPClient::SetCompilerOptionsForInferredProjects(
    CompilerOptions* options) {
	Server->SetCompilerOptionsForInferredProjects(ctx, options);
}

}  // namespace tsc::testutil::lsptestutil
