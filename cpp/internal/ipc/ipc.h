// ipc.h — declarations for tsc/internal/ipc (conn.go, conn_async.go,
// conn_sync.go, protocol.go, protocol_jsonrpc.go, timing.go, transport.go,
// transport_unix.go). The ipc slice owns the implementations; contentmapper
// declared the surface it uses ahead of the slice landing.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/json/json.h"
#include "internal/jsonrpc/jsonrpc.h"

namespace tsc::ipc {

inline const gostd::Error ErrConnClosed =
	gostd::newError("ipc: connection closed");
inline const gostd::Error ErrRequestTimeout =
	gostd::newError("ipc: request timeout");

using Message = jsonrpc::Message;

// Handler — conn.go:16. Processes incoming requests and notifications.
struct Handler {
	virtual ~Handler() = default;
	virtual std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) = 0;
	virtual gostd::Error HandleNotification(gostd::Context ctx,
	                                        std::string_view method,
	                                        const json::Value& params) = 0;
};

// Conn — conn.go:24. A bidirectional connection for API communication.
struct Conn {
	virtual ~Conn() = default;
	// Run starts processing messages, blocking until ctx cancels or an error.
	virtual gostd::Error Run(gostd::Context ctx) = 0;
	// Call sends a request and waits for the response.
	virtual std::pair<json::Value, gostd::Error>
	Call(gostd::Context ctx, std::string_view method,
	     const json::Value& params) = 0;
	// Notify sends a notification (no response expected).
	virtual gostd::Error Notify(gostd::Context ctx, std::string_view method,
	                            const json::Value& params) = 0;
};

// Protocol — protocol.go:11.
struct Protocol {
	virtual ~Protocol() = default;
	virtual std::pair<Message*, gostd::Error> ReadMessage() = 0;
	virtual gostd::Error WriteRequest(const jsonrpc::ID* id,
	                                  std::string_view method,
	                                  const json::Value& params) = 0;
	virtual gostd::Error WriteNotification(std::string_view method,
	                                       const json::Value& params) = 0;
	virtual gostd::Error WriteResponse(const jsonrpc::ID* id,
	                                   const json::Value& result) = 0;
	virtual gostd::Error WriteError(const jsonrpc::ID* id,
	                                const jsonrpc::ResponseError* err) = 0;
};

// === slice: ipc ===

// UnmarshalParams — conn.go:42. Helper to unmarshal params into a typed
// struct; returns nil (nullptr) for empty params.
template <typename T>
std::pair<std::unique_ptr<T>, gostd::Error>
UnmarshalParams(const json::Value& params) {
	if (params.empty()) {
		return {nullptr, nullptr};
	}
	auto v = std::make_unique<T>();
	if (std::string err = json::unmarshal(params, v.get()); !err.empty()) {
		return {nullptr, gostd::newError(err)};
	}
	return {std::move(v), nullptr};
}

// --- timing.go ---------------------------------------------------------------

// Method names for the generic connection-level timing feature, handled by
// the connection itself rather than the handler when timing collection is
// enabled.
inline constexpr std::string_view MethodGetServerTiming = "getServerTiming";
inline constexpr std::string_view MethodResetServerTiming = "resetServerTiming";

// serverRecentRequestCapacity — timing.go:16. Number of most-recent requests
// retained in the server-side timing ring buffer.
inline constexpr int serverRecentRequestCapacity = 5;

// serverRequestTiming — timing.go:20. A single server-side request's
// processing-time sample.
struct serverRequestTiming {
	std::string method;             // `json:"method"`
	double processingTimeMs = 0;    // `json:"processingTimeMs"`
	int64_t timestamp = 0;          // `json:"timestamp"`
};

// serverTimingTotals — timing.go:30. Running totals across handled requests.
struct serverTimingTotals {
	uint64_t requestCount = 0;      // `json:"requestCount"`
	double totalProcessingTimeMs = 0; // `json:"totalProcessingTimeMs"`
};

// serverTimingInfo — timing.go:38. Point-in-time snapshot of collected
// server timing, returned to clients for a getServerTiming request.
struct serverTimingInfo {
	bool enabled = false;                             // `json:"enabled"`
	serverTimingTotals totals;                        // `json:"totals"`
	std::vector<serverRequestTiming> recentRequests;  // `json:"recentRequests"`
};

// timingCollector — timing.go:60. Accumulates per-request processing times
// into running totals and a fixed-size ring buffer; safe for concurrent use.
class timingCollector {
	std::mutex mu;
	serverTimingTotals totals;
	// ring holds up to serverRecentRequestCapacity entries; once full, head
	// marks the oldest entry.
	std::vector<serverRequestTiming> ring;
	int head = 0;

public:
	void record(std::string_view method, gostd::Duration d);
	serverTimingInfo snapshot();
	void reset();
};

// newTimingCollector — timing.go:69.
std::unique_ptr<timingCollector> newTimingCollector();

// serverTimingSnapshot — timing.go:131: the collector's snapshot (or a
// disabled one), marshaled for a getServerTiming response. (Go marshals the
// serverTimingInfo inside WriteResponse; the port takes pre-marshaled
// json::Value there, so the marshaling happens here.)
json::Value serverTimingSnapshot(timingCollector* c);

// durationToMillis — timing.go:142. Fractional milliseconds, clamped
// non-negative, sub-microsecond precision preserved.
double durationToMillis(gostd::Duration d);

// --- package-internal machinery ----------------------------------------------

// msgID — Go's `msg.ID` (*jsonrpc.ID): the Id member as a pointer, nullptr
// when absent.
inline const jsonrpc::ID* msgID(const Message* m) {
	return m->Id.has_value() ? &*m->Id : nullptr;
}

// pendingCall — the `chan *Message` (capacity 1) registered in
// AsyncConn::pending for each outgoing Call. `deliver` is Go's
// `ch <- msg; close(ch)`; `close` is Go's `close(ch)` (closed empty →
// `resp, ok := <-ch` yields ok=false).
struct pendingCall {
	std::mutex mu;
	std::condition_variable cv;
	std::unique_ptr<Message> msg;
	bool closed = false;

	void deliver(std::unique_ptr<Message> m) {
		{
			std::lock_guard<std::mutex> lk(mu);
			msg = std::move(m);
			closed = true;
		}
		cv.notify_all();
	}
	void close() {
		{
			std::lock_guard<std::mutex> lk(mu);
			closed = true;
		}
		cv.notify_all();
	}
};

// requestErrors — the buffered `chan error` (capacity 1) created in
// AsyncConn::Run; only the first recordRequestError lands a value.
struct requestErrors {
	gostd::Error err;
	bool has = false;
};

// waitGroup — sync.WaitGroup: go() spawns a detached thread, wait() blocks
// until the count reaches zero.
struct waitGroup {
	// Shared state — Go's GC keeps the WaitGroup alive for as long as any
	// spawned goroutine can touch it. A detached worker's final
	// decrement/notify can run after wait() returned and the group owner
	// was destroyed, so the state must be heap-shared.
	struct shared {
		std::mutex mu;
		std::condition_variable cv;
		int count = 0;
	};
	std::shared_ptr<shared> st = std::make_shared<shared>();

	void go(std::function<void()> f) {
		{
			std::lock_guard<std::mutex> lk(st->mu);
			++st->count;
		}
		std::thread([st = st, f = std::move(f)] {
			f();
			std::unique_lock<std::mutex> lk(st->mu);
			--st->count;
			lk.unlock();
			st->cv.notify_all();
		}).detach();
	}
	void wait() {
		std::unique_lock<std::mutex> lk(st->mu);
		st->cv.wait(lk, [st = st] { return st->count == 0; });
	}
};

// debugStack — runtime/debug.Stack(): best-effort C++ backtrace text for the
// "panic:" error responses in handleRequest.
std::string debugStack();

// panicText — `%v` of a recovered panic: what() for std::exception, the
// value for common scalar throw types, "unknown" otherwise.
std::string panicText(std::exception_ptr ep);

// --- conn_async.go -----------------------------------------------------------

// AsyncConn — conn_async.go:19. Bidirectional JSON-RPC communication with
// async request handling: each incoming request is handled in its own
// thread, allowing concurrent processing. The standard implementation for
// LSP-style JSON-RPC protocols.
class AsyncConn : public Conn {
public:
	AsyncConn(std::shared_ptr<gostd::io::ReadWriteCloser> rwc,
	          std::shared_ptr<Protocol> protocol,
	          std::shared_ptr<Handler> handler)
	    : rwc(std::move(rwc)), protocol(std::move(protocol)),
	      handler(std::move(handler)) {}

	// SetCollectTiming enables or disables per-request server
	// processing-time measurement (conn_async.go:56).
	void SetCollectTiming(bool enabled) {
		timing = enabled ? newTimingCollector() : nullptr;
	}

	gostd::Error Run(gostd::Context ctx) override;
	std::pair<json::Value, gostd::Error>
	Call(gostd::Context ctx, std::string_view method,
	     const json::Value& params) override;
	gostd::Error Notify(gostd::Context ctx, std::string_view method,
	                    const json::Value& params) override;

private:
	std::shared_ptr<gostd::io::ReadWriteCloser> rwc;
	std::shared_ptr<Protocol> protocol;
	std::shared_ptr<Handler> handler;

	// timing, when non-null, accumulates the wall-clock time spent handling
	// each request. Clients retrieve the collected data via a
	// getServerTiming request.
	std::unique_ptr<timingCollector> timing;

	// For server→client requests
	std::atomic<int64_t> seq{0};
	std::unordered_map<jsonrpc::ID, std::shared_ptr<pendingCall>,
	                   jsonrpc::IDHash>
	    pending;
	std::mutex pendingMu;
	gostd::Error terminal;
	bool hasCause = false;
	std::mutex writeMu;
	waitGroup handlers;

	// runLoop — the read/dispatch loop of Run (Run adds the deferred
	// cleanup: close pending calls, cancel handlers, wait, join the first
	// request error).
	gostd::Error runLoop(gostd::Context ctx, const gostd::Context& handlerCtx,
	                     requestErrors* requestErrs);
	void closePendingCalls(const gostd::Error& runErr);
	bool recordRequestError(const gostd::Error& requestErr,
	                        requestErrors* errs);
	// Callers hold pendingMu.
	bool recordTerminalErrorLocked(const gostd::Error& terminalErr);
	// Callers hold pendingMu.
	void closePendingCallsLocked();
	void handleResponse(std::unique_ptr<Message> msg);
	gostd::Error handleRequest(gostd::Context ctx, Message* msg);
	void handleNotification(gostd::Context ctx, Message* msg);
	// callAwait — the tail of Call after the pending registration: write the
	// request, then wait for the response or ctx cancellation.
	std::pair<json::Value, gostd::Error>
	callAwait(gostd::Context ctx, const jsonrpc::ID& id,
	          const std::shared_ptr<pendingCall>& responseChan,
	          std::string_view method, const json::Value& params);
	// removePendingCall — Call's deferred cleanup: close+delete the call's
	// channel if still registered.
	void removePendingCall(const jsonrpc::ID& id);
};

// NewAsyncConn — conn_async.go:42. Uses JSONRPCProtocol (LSP-style
// Content-Length framing) by default.
std::shared_ptr<AsyncConn> NewAsyncConn(
    std::shared_ptr<gostd::io::ReadWriteCloser> rwc,
    std::shared_ptr<Handler> handler);

// NewAsyncConnWithProtocol — conn_async.go:46. A new async connection with a
// custom protocol.
std::shared_ptr<Conn> NewAsyncConnWithProtocol(
    std::shared_ptr<gostd::io::ReadWriteCloser> rwc,
    std::shared_ptr<Protocol> protocol, std::shared_ptr<Handler> handler);

// --- conn_sync.go ------------------------------------------------------------

// SyncConn — conn_sync.go:16. Bidirectional communication with synchronous
// request handling: requests are handled one at a time inline, and outgoing
// calls are serialized.
class SyncConn : public Conn {
public:
	SyncConn(std::shared_ptr<gostd::io::ReadWriteCloser> rwc,
	         std::shared_ptr<Protocol> protocol,
	         std::shared_ptr<Handler> handler)
	    : rwc(std::move(rwc)), protocol(std::move(protocol)),
	      handler(std::move(handler)) {}

	// SetCollectTiming — conn_sync.go:36.
	void SetCollectTiming(bool enabled) {
		timing = enabled ? newTimingCollector() : nullptr;
	}

	gostd::Error Run(gostd::Context ctx) override;
	std::pair<json::Value, gostd::Error>
	Call(gostd::Context ctx, std::string_view method,
	     const json::Value& params) override;
	gostd::Error Notify(gostd::Context ctx, std::string_view method,
	                    const json::Value& params) override;

private:
	std::shared_ptr<gostd::io::ReadWriteCloser> rwc;
	std::shared_ptr<Protocol> protocol;
	std::shared_ptr<Handler> handler;

	// timing, when non-null, accumulates the wall-clock time spent handling
	// each request. Clients retrieve the collected data via a
	// getServerTiming request.
	std::unique_ptr<timingCollector> timing;

	// mu serializes all protocol operations (reads and writes). This
	// ensures that concurrent calls from handler threads (e.g., project
	// code spawning goroutines that invoke filesystem callbacks) don't
	// corrupt the stream.
	std::mutex mu;

	gostd::Error handleRequest(gostd::Context ctx, Message* msg);
	void handleNotification(gostd::Context ctx, Message* msg);
};

// NewSyncConn — conn_sync.go:27.
std::shared_ptr<SyncConn> NewSyncConn(
    std::shared_ptr<gostd::io::ReadWriteCloser> rwc,
    std::shared_ptr<Protocol> protocol, std::shared_ptr<Handler> handler);

// --- protocol_jsonrpc.go -----------------------------------------------------

// JSONRPCProtocol — protocol_jsonrpc.go:14. JSON-RPC 2.0 with the LSP base
// protocol framing (Content-Length headers).
class JSONRPCProtocol : public Protocol {
public:
	explicit JSONRPCProtocol(
	    std::shared_ptr<gostd::io::ReadWriteCloser> rw);

	std::pair<Message*, gostd::Error> ReadMessage() override;
	gostd::Error WriteRequest(const jsonrpc::ID* id, std::string_view method,
	                          const json::Value& params) override;
	gostd::Error WriteNotification(std::string_view method,
	                               const json::Value& params) override;
	gostd::Error WriteResponse(const jsonrpc::ID* id,
	                           const json::Value& result) override;
	gostd::Error WriteError(const jsonrpc::ID* id,
	                        const jsonrpc::ResponseError* err) override;

private:
	std::shared_ptr<gostd::io::ReadWriteCloser> rw;
	std::unique_ptr<jsonrpc::Reader> reader;
	std::unique_ptr<jsonrpc::Writer> writer;
};

// NewJSONRPCProtocol — protocol_jsonrpc.go:20.
std::shared_ptr<Protocol> NewJSONRPCProtocol(
    std::shared_ptr<gostd::io::ReadWriteCloser> rw);

// --- transport.go / transport_unix.go ----------------------------------------

// Transport — transport.go:9. Accepts connections from API clients.
struct Transport {
	virtual ~Transport() = default;
	// Accept waits for and returns the next connection.
	virtual std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
	                  gostd::Error>
	Accept() = 0;
	// Close stops the transport from accepting new connections.
	virtual gostd::Error Close() = 0;
};

// PipeTransport — transport.go:17. Accepts connections on a Unix domain
// socket (or Windows named pipe — only unix is ported).
class PipeTransport : public Transport {
public:
	PipeTransport(int listenerFd, std::string path)
	    : listenerFd(listenerFd), path(std::move(path)) {}
	~PipeTransport() override { (void)Close(); }

	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Accept() override;
	gostd::Error Close() override;
	// Path returns the path of the pipe/socket.
	std::string Path() const { return path; }

private:
	int listenerFd;
	std::string path;
};

// NewPipeTransport — transport.go:23. On Unix, creates a Unix domain socket.
std::pair<std::shared_ptr<PipeTransport>, gostd::Error>
NewPipeTransport(std::string_view path);

// StdioTransport — transport.go:49. Wraps stdin/stdout as a single
// connection transport; only accepts one connection.
class StdioTransport : public Transport {
public:
	// Names stdin_/stdout_ dodge the MSVC CRT macros stdin/stdout.
	StdioTransport(std::shared_ptr<gostd::io::ReadCloser> stdin_,
	               std::shared_ptr<gostd::io::WriteCloser> stdout_)
	    : stdin_(std::move(stdin_)), stdout_(std::move(stdout_)) {}

	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Accept() override;
	gostd::Error Close() override;

private:
	std::shared_ptr<gostd::io::ReadCloser> stdin_;
	std::shared_ptr<gostd::io::WriteCloser> stdout_;
	bool used = false;
};

// NewStdioTransport — transport.go:55.
std::shared_ptr<StdioTransport> NewStdioTransport(
    std::shared_ptr<gostd::io::ReadCloser> stdin_,
    std::shared_ptr<gostd::io::WriteCloser> stdout_);

// GeneratePipePath — transport_unix.go:23. A platform-appropriate pipe path
// for the given name (os.TempDir() + name).
std::string GeneratePipePath(std::string_view name);

// newPipeListener — transport_unix.go:15 (defined in transport_unix.cpp).
// Returns the listening socket fd.
std::pair<int, gostd::Error> newPipeListener(std::string_view path);

// === end slice: ipc ===

} // namespace tsc::ipc
