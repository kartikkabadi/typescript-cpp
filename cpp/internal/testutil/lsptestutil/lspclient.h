// lspclient.h — port of tsc/internal/testutil/lsptestutil/lspclient.go:
// an in-test LSP client wired to a Server over real Content-Length-framed
// byte pipes, with an async message router for server-initiated traffic.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"

namespace tsc {
struct CompilerOptions;
}

namespace tsc::testutil::lsptestutil {

// pipe — io.Pipe: a synchronous in-memory byte pipe. A Write blocks until
// all of its bytes have been consumed by Reads; Close makes pending and
// future Reads return EOF and Writes fail.
struct pipe {
	std::mutex mu;
	std::condition_variable cv;
	std::string buf;
	bool closed = false;
};

// pipeReader — *io.PipeReader.
struct pipeReader final : gostd::io::Reader {
	std::shared_ptr<pipe> p;
	std::pair<int, gostd::Error> read(std::span<char> out) override;
};

// pipeWriter — *io.PipeWriter.
struct pipeWriter final : gostd::io::Writer,
                          gostd::io::Closer {
	std::shared_ptr<pipe> p;
	std::pair<int, gostd::Error> write(std::string_view data) override;
	gostd::Error close() override;
};

// ioPipe — io.Pipe().
std::pair<std::shared_ptr<pipeReader>, std::shared_ptr<pipeWriter>>
ioPipe();

// clientTransport — lspclient.go:27.
struct clientTransport {
	std::shared_ptr<lsp::Reader> serverIn;
	std::shared_ptr<lsp::Writer> serverOut;
	std::shared_ptr<lsp::Reader> clientIn;
	std::shared_ptr<lsp::Writer> clientOut;
	std::function<void()> closeClientOut;
	std::function<void()> closeServerOut;
};

// newClientTransport — lspclient.go:36.
clientTransport newClientTransport();

// ServerRequestHandler — lspclient.go:50.
using ServerRequestHandler =
    std::function<std::shared_ptr<lsproto::ResponseMessage>(
        const gostd::Context& ctx,
        const std::shared_ptr<lsproto::RequestMessage>& req)>;

// ServerNotificationHandler — lspclient.go:53.
using ServerNotificationHandler =
    std::function<void(const gostd::Context& ctx,
                       const std::shared_ptr<lsproto::RequestMessage>& req)>;

// responseChan — a buffered (cap 1) channel of *lsproto.ResponseMessage.
struct responseChan {
	std::mutex mu;
	std::condition_variable cv;
	bool has = false;
	std::shared_ptr<lsproto::ResponseMessage> value;

	// send — the channel has cap 1; the producer never blocks.
	void send(std::shared_ptr<lsproto::ResponseMessage> v);
	// recv — waits for a value or ctx cancellation; returns (value, ok).
	std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool>
	recv(const gostd::Context& ctx);
};

// errGroup — golang.org/x/sync/errgroup: runs each fn on its own thread;
// Wait joins all and returns the first non-nil error. WithContext cancels
// the derived context on the first error.
struct errGroup {
	std::mutex mu;
	std::vector<std::thread> threads;
	gostd::Error firstErr;
	gostd::CancelFunc cancel;

	void Go(std::function<gostd::Error()> fn);
	gostd::Error Wait();
};

// errgroupWithContext — errgroup.WithContext.
std::pair<std::shared_ptr<errGroup>, gostd::Context>
errgroupWithContext(const gostd::Context& ctx);

// LSPClient — lspclient.go:56.
struct LSPClient {
	std::shared_ptr<lsp::Server> Server;
	std::shared_ptr<lsp::Writer> inputWriter;
	std::shared_ptr<lsp::Reader> outputReader;
	int32_t id = 0;
	gostd::Context ctx;

	// inputWriterMu serializes writes to the server (lspclient.go:63-68).
	std::mutex inputWriterMu;

	ServerRequestHandler onServerRequest;
	ServerNotificationHandler OnServerNotification;

	std::unordered_map<jsonrpc::ID, std::shared_ptr<responseChan>,
	                   jsonrpc::IDHash>
	    pendingRequests;
	std::mutex pendingRequestsMu;

	// writeToServer — lspclient.go:84.
	gostd::Error writeToServer(const std::shared_ptr<lsproto::Message>& msg);
	// NextID — lspclient.go:131.
	int32_t NextID() {
		int32_t id_ = id;
		id++;
		return id_;
	}
	// MessageRouter — lspclient.go:141.
	gostd::Error MessageRouter(const gostd::Context& ctx);
	// handleResponse — lspclient.go:184.
	void handleResponse(const gostd::Context& ctx,
	                    const std::shared_ptr<lsproto::ResponseMessage>&
	                        resp);
	// handleServerRequest — lspclient.go:207.
	gostd::Error handleServerRequest(
	    const gostd::Context& ctx,
	    const std::shared_ptr<lsproto::RequestMessage>& req);
	// WriteMsg — lspclient.go:242.
	void WriteMsg(gostd::testing::T* t,
	              const std::shared_ptr<lsproto::Message>& msg);
	// SendRequestWorker — lspclient.go:282.
	std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool>
	SendRequestWorker(gostd::testing::T* t,
	                  const std::shared_ptr<lsproto::RequestMessage>& req,
	                  const std::shared_ptr<jsonrpc::ID>& reqID);
	// startRequestWorker — lspclient.go:287.
	std::shared_ptr<responseChan> startRequestWorker(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::RequestMessage>& req,
	    const std::shared_ptr<jsonrpc::ID>& reqID);
	// waitForResponse — lspclient.go:297.
	std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool>
	waitForResponse(gostd::testing::T* t,
	                const std::shared_ptr<jsonrpc::ID>& reqID,
	                const std::shared_ptr<responseChan>& respChan);
	// SendRequest — lspclient.go:250.
	template <class Params, class Resp>
	std::tuple<std::shared_ptr<lsproto::Message>, Resp, bool>
	SendRequest(gostd::testing::T* t,
	            const lsproto::RequestInfo<Params, Resp>& info,
	            const Params& params);
	// SendRequestAsync — lspclient.go:265.
	template <class Params, class Resp>
	std::function<std::tuple<std::shared_ptr<lsproto::Message>, Resp, bool>()>
	SendRequestAsync(gostd::testing::T* t,
	                 const lsproto::RequestInfo<Params, Resp>& info,
	                 const Params& params);
	// SendNotification — lspclient.go:317.
	template <class Params>
	void SendNotification(gostd::testing::T* t,
	                      const lsproto::NotificationInfo<Params>& info,
	                      const Params& params);
	// SetCompilerOptionsForInferredProjects — lspclient.go:324.
	void SetCompilerOptionsForInferredProjects(CompilerOptions* options);
};

// NewLSPClient — lspclient.go:91. Returns the client and a close function
// (Go's returned func() error becomes a std::function<gostd::Error()>).
std::pair<std::shared_ptr<LSPClient>, std::function<gostd::Error()>>
NewLSPClient(gostd::testing::T* t, lsp::ServerOptions serverOpts,
             ServerRequestHandler onServerRequest);

}  // namespace tsc::testutil::lsptestutil

#include "internal/testutil/lsptestutil/lspclient_inl.h"
