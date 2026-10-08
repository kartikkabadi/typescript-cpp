// tests_conn_async.cpp — port of tsc/internal/ipc/conn_async_test.go.
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "internal/fourslash/goutil.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::ipc {
namespace {

using gostd::testing::T;

// chan_ — a tiny Go chan analog with an optional receive timeout
// (select + time.After).
template <typename T> struct chan_ {
	std::mutex mu;
	std::condition_variable cv;
	std::deque<T> q;
	bool closed = false;

	bool send(T v) {
		{
			std::lock_guard<std::mutex> lk(mu);
			if (closed) {
				return false;
			}
			q.push_back(std::move(v));
		}
		cv.notify_one();
		return true;
	}
	void close() {
		{
			std::lock_guard<std::mutex> lk(mu);
			closed = true;
		}
		cv.notify_all();
	}
	// recv — timeoutMs < 0 blocks forever (plain <-ch); otherwise a
	// select against time.After.
	std::pair<T, bool> recv(int64_t timeoutMs = -1) {
		std::unique_lock<std::mutex> lk(mu);
		auto ready = [&] { return !q.empty() || closed; };
		if (timeoutMs < 0) {
			cv.wait(lk, ready);
		} else if (!cv.wait_for(
		               lk, std::chrono::milliseconds(timeoutMs),
		               ready)) {
			return {T{}, false};
		}
		if (q.empty()) {
			return {T{}, false};
		}
		T v = std::move(q.front());
		q.pop_front();
		return {std::move(v), true};
	}
};

struct noOpHandler : Handler {
	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		return {json::Value{}, gostd::Error()};
	}
	gostd::Error
	HandleNotification(gostd::Context ctx, std::string_view method,
	                   const json::Value& params) override {
		return gostd::Error();
	}
};

struct queuedProtocol : Protocol {
	std::deque<Message*> messages;
	gostd::Error responseErr;

	std::pair<Message*, gostd::Error> ReadMessage() override {
		if (messages.empty()) {
			return {nullptr, gostd::io::errEOF};
		}
		auto* m = messages.front();
		messages.pop_front();
		return {m, gostd::Error()};
	}
	gostd::Error WriteRequest(const jsonrpc::ID* id, std::string_view method,
	                          const json::Value& params) override {
		return gostd::Error();
	}
	gostd::Error
	WriteNotification(std::string_view method,
	                  const json::Value& params) override {
		return gostd::Error();
	}
	gostd::Error WriteResponse(const jsonrpc::ID* id,
	                           const json::Value& result) override {
		return responseErr;
	}
	gostd::Error
	WriteError(const jsonrpc::ID* id,
	           const jsonrpc::ResponseError* err) override {
		return responseErr;
	}
};

struct blockingHandler : Handler {
	chan_<int> started; // capacity >= number of expected signals
	chan_<int> release;

	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		started.send(0);
		release.recv();
		return {json::Value{}, gostd::Error()};
	}
	gostd::Error
	HandleNotification(gostd::Context ctx, std::string_view method,
	                   const json::Value& params) override {
		started.send(0);
		release.recv();
		return gostd::Error();
	}
};

struct contextHandler : Handler {
	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		gostd::ctxWaitDone(ctx);
		return {json::Value{}, gostd::ctxErr(ctx)};
	}
	gostd::Error
	HandleNotification(gostd::Context ctx, std::string_view method,
	                   const json::Value& params) override {
		gostd::ctxWaitDone(ctx);
		return gostd::ctxErr(ctx);
	}
};

void TestAsyncConnRunWaitsForHandlers(T* t) {
	t->Parallel();

	auto id = jsonrpc::NewIDString("1");
	auto* protocol = new queuedProtocol();
	protocol->messages.push_back(new Message{.Id = id, .Method = "request"});
	protocol->messages.push_back(new Message{.Method = "notification"});
	auto* handler = new blockingHandler();
	auto conn = NewAsyncConnWithProtocol(
	    nullptr, std::shared_ptr<Protocol>(protocol),
	    std::shared_ptr<Handler>(handler));

	// Channels are heap-pinned: Go's chan is GC-owned, so a detached sender
	// may finish send()'s notify after the test frame is gone.
	auto runDone = std::make_shared<chan_<gostd::Error>>();
	auto ctx = t->Context();
	std::thread([runDone, conn, ctx] {
		runDone->send(conn->Run(ctx));
	}).detach();

	handler->started.recv();
	handler->started.recv();
	auto [_, runReturned] = runDone->recv(0);
	if (runReturned)
		t->Error({"Run returned while handlers were active"});

	handler->release.close();
	auto [err, _2] = runDone->recv(1000);
	if (err)
		t->Error({err->Error()});
}
REGISTER_UNIT_TEST("ipc.TestAsyncConnRunWaitsForHandlers",
                   TestAsyncConnRunWaitsForHandlers);

void TestAsyncConnRunCancelsHandlersOnEOF(T* t) {
	t->Parallel();

	auto id = jsonrpc::NewIDString("1");
	auto* protocol = new queuedProtocol();
	protocol->messages.push_back(new Message{.Id = id, .Method = "request"});
	auto conn = NewAsyncConnWithProtocol(
	    nullptr, std::shared_ptr<Protocol>(protocol),
	    std::shared_ptr<Handler>(std::make_shared<contextHandler>()));

	// Channels are heap-pinned: Go's chan is GC-owned, so a detached sender
	// may finish send()'s notify after the test frame is gone.
	auto runDone = std::make_shared<chan_<gostd::Error>>();
	auto ctx = t->Context();
	std::thread([runDone, conn, ctx] {
		runDone->send(conn->Run(ctx));
	}).detach();

	auto [err, ok] = runDone->recv(1000);
	if (!ok) {
		t->Fatal({"Run did not cancel active handlers after EOF"});
	} else if (err) {
		t->Fatal({err->Error()});
	}
}
REGISTER_UNIT_TEST("ipc.TestAsyncConnRunCancelsHandlersOnEOF",
                   TestAsyncConnRunCancelsHandlersOnEOF);

void TestAsyncConnResponseWriteFailureWithNilTransport(T* t) {
	t->Parallel();

	auto responseErr = gostd::newError("response write failed");
	auto id = jsonrpc::NewIDString("1");
	auto* protocol = new queuedProtocol();
	protocol->messages.push_back(new Message{.Id = id, .Method = "request"});
	protocol->responseErr = responseErr;
	auto conn = NewAsyncConnWithProtocol(
	    nullptr, std::shared_ptr<Protocol>(protocol),
	    std::shared_ptr<Handler>(std::make_shared<noOpHandler>()));

	auto err = conn->Run(t->Context());
	if (!gostd::errorIs(err, responseErr))
		t->Error({gostd::sprintf(
		    "expected response write error, got %v", {err->Error()})});
}
REGISTER_UNIT_TEST("ipc.TestAsyncConnResponseWriteFailureWithNilTransport",
                   TestAsyncConnResponseWriteFailureWithNilTransport);

// pipeEnd — one endpoint of a net.Pipe-style synchronous duplex connection.
struct pipeEnd : gostd::io::ReadWriteCloser {
	std::shared_ptr<gostd::pipeReader> r;
	std::shared_ptr<gostd::pipeWriter> w;

	pipeEnd(std::shared_ptr<gostd::pipeReader> r,
	        std::shared_ptr<gostd::pipeWriter> w)
	    : r(std::move(r)), w(std::move(w)) {}
	std::pair<int, gostd::Error> read(std::span<char> buf) override {
		return r->read(buf);
	}
	std::pair<int, gostd::Error> write(std::string_view d) override {
		return w->write(d);
	}
	gostd::Error close() override {
		r->close();
		return w->close();
	}
};

// netPipe — net.Pipe(): two synchronous in-memory full-duplex endpoints.
inline std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
                 std::shared_ptr<gostd::io::ReadWriteCloser>>
netPipe() {
	auto [ar, aw] = gostd::ioPipe();
	auto [br, bw] = gostd::ioPipe();
	return {std::make_shared<pipeEnd>(ar, bw),
	        std::make_shared<pipeEnd>(br, aw)};
}

void TestAsyncConnCallReturnsWhenPeerCloses(T* t) {
	t->Parallel();
	auto np = netPipe();
	auto client = np.first;
	auto server = np.second;
	auto conn = NewAsyncConn(
	    client, std::shared_ptr<Handler>(std::make_shared<noOpHandler>()));
	// Channels are heap-pinned: Go's chan is GC-owned, so a detached sender
	// may finish send()'s notify after the test frame is gone.
	auto runDone = std::make_shared<chan_<gostd::Error>>();
	auto ctx = t->Context();
	std::thread([runDone, conn, ctx] {
		runDone->send(conn->Run(ctx));
	}).detach();

	auto callDone = std::make_shared<chan_<gostd::Error>>();
	std::thread([callDone, conn, t] {
		auto [_, err] = conn->Call(t->Context(), "transform", json::Value{});
		callDone->send(err);
	}).detach();

	std::array<char, 1024> buffer;
	auto [n, rerr] = server->read(buffer);
	if (rerr)
		t->Fatal({rerr->Error()});
	if (auto cerr = server->close(); cerr)
		t->Fatal({cerr->Error()});
	auto [err, _2] = runDone->recv(1000);
	if (err)
		t->Error({err->Error()});
	auto [callErr, _3] = callDone->recv(1000);
	if (!gostd::errorIs(callErr, ErrConnClosed))
		t->Error({gostd::sprintf("expected ErrConnClosed, got %v",
		                         {callErr->Error()})});
	if (auto cerr = client->close(); cerr)
		t->Error({cerr->Error()});
}
REGISTER_UNIT_TEST("ipc.TestAsyncConnCallReturnsWhenPeerCloses",
                   TestAsyncConnCallReturnsWhenPeerCloses);

void TestAsyncConnCallAfterReadLoopFailureReturnsImmediately(T* t) {
	t->Parallel();
	auto np = netPipe();
	auto client = np.first;
	auto server = np.second;
	// Cleanup captures by value: Go closures close over the variable (GC
	// lifetime) but C++ [&] refs dangle once the test frame is gone.
	t->Cleanup([client] { client->close(); });
	t->Cleanup([server] { server->close(); });
	auto conn = NewAsyncConn(
	    client, std::shared_ptr<Handler>(std::make_shared<noOpHandler>()));
	// Channels are heap-pinned: Go's chan is GC-owned, so a detached sender
	// may finish send()'s notify after the test frame is gone.
	auto runDone = std::make_shared<chan_<gostd::Error>>();
	auto runCtx = t->Context();
	std::thread([runDone, conn, runCtx] {
		runDone->send(conn->Run(runCtx));
	}).detach();

	auto [_, werr] = server->write("oops\n");
	if (werr)
		t->Fatal({werr->Error()});
	auto [err, _2] = runDone->recv(1000);
	if (!err || err->Error().find("invalid header") == std::string::npos)
		t->Error({gostd::sprintf("expected invalid header, got %v",
		                         {err ? err->Error() : std::string("nil")})});
	// io.Copy(io.Discard, server) — drain whatever the conn writes.
	std::thread([srv = server] {
		std::array<char, 256> buf;
		while (true) {
			auto [n, e] = srv->read(buf);
			if (e) {
				return;
			}
		}
	}).detach();

	auto cct = gostd::contextWithTimeout(t->Context(),
	                                     std::chrono::seconds(1));
	auto ctx = cct.first;
	auto cancel = cct.second;
	t->Cleanup([cancel] { cancel(); });
	auto [rv, err2] = conn->Call(ctx, "transform", json::Value{});
	if (!gostd::errorIs(err2, ErrConnClosed))
		t->Error({gostd::sprintf("expected ErrConnClosed, got %v",
		                         {err2 ? err2->Error()
		                               : std::string("nil")})});
	if (gostd::errorIs(err2, gostd::errDeadlineExceeded))
		t->Error({gostd::sprintf(
		    "call waited for its context deadline: %v",
		    {err2->Error()})});
	err2 = conn->Notify(ctx, "changed", json::Value{});
	if (!gostd::errorIs(err2, ErrConnClosed))
		t->Error({gostd::sprintf("expected ErrConnClosed, got %v",
		                         {err2 ? err2->Error()
		                               : std::string("nil")})});
}
REGISTER_UNIT_TEST(
    "ipc.TestAsyncConnCallAfterReadLoopFailureReturnsImmediately",
    TestAsyncConnCallAfterReadLoopFailureReturnsImmediately);

struct closeSignal : gostd::io::ReadWriteCloser {
	chan_<int>* closed = new chan_<int>();
	std::once_flag once;

	std::pair<int, gostd::Error> read(std::span<char> buf) override {
		return {0, gostd::io::errEOF};
	}
	std::pair<int, gostd::Error> write(std::string_view d) override {
		return {(int)d.size(), gostd::Error()};
	}
	gostd::Error close() override {
		std::call_once(once, [&] { closed->close(); });
		return gostd::Error();
	}
};

struct failingResponseProtocol : Protocol {
	chan_<int>* closed;
	bool requestRead = false;
	gostd::Error responseErr;

	std::pair<Message*, gostd::Error> ReadMessage() override {
		if (!requestRead) {
			requestRead = true;
			return {new Message{.Id = jsonrpc::NewIDInt(1),
			                    .Method = "transform"},
			        gostd::Error()};
		}
		closed->recv();
		return {nullptr,
		        gostd::newError("io: read/write on closed pipe")};
	}
	gostd::Error WriteRequest(const jsonrpc::ID* id, std::string_view method,
	                          const json::Value& params) override {
		return gostd::Error();
	}
	gostd::Error
	WriteNotification(std::string_view method,
	                  const json::Value& params) override {
		return gostd::Error();
	}
	gostd::Error WriteResponse(const jsonrpc::ID* id,
	                           const json::Value& result) override {
		return responseErr;
	}
	gostd::Error
	WriteError(const jsonrpc::ID* id,
	           const jsonrpc::ResponseError* err) override {
		return responseErr;
	}
};

void TestAsyncConnTerminalErrorIncludesResponseWriteFailure(T* t) {
	t->Parallel();
	auto responseErr = gostd::newError("response write failed");
	auto rwc = std::make_shared<closeSignal>();
	auto* protocol = new failingResponseProtocol();
	protocol->closed = rwc->closed;
	protocol->responseErr = responseErr;
	auto conn = NewAsyncConnWithProtocol(
	    rwc, std::shared_ptr<Protocol>(protocol),
	    std::shared_ptr<Handler>(std::make_shared<noOpHandler>()));

	auto err = conn->Run(t->Context());
	if (!gostd::errorIs(err, responseErr))
		t->Error({gostd::sprintf(
		    "expected response write error, got %v", {err->Error()})});
	auto [_, err2] =
	    conn->Call(t->Context(), "transform", json::Value{});
	if (!gostd::errorIs(err2, responseErr))
		t->Error({gostd::sprintf(
		    "expected terminal response write error, got %v",
		    {err2->Error()})});
	auto es = err2->Error();
	size_t count = 0;
	for (size_t p = 0;
	     (p = es.find("response write failed", p)) != std::string::npos;
	     p += 19) {
		count++;
	}
	if (count != 1)
		t->Error({gostd::sprintf(
		    "expected error to contain response failure once, got %v",
		    {es})});
}
REGISTER_UNIT_TEST(
    "ipc.TestAsyncConnTerminalErrorIncludesResponseWriteFailure",
    TestAsyncConnTerminalErrorIncludesResponseWriteFailure);

void TestAsyncConnRunWaitsForRequestAfterPeerCloses(T* t) {
	t->Parallel();
	auto np = netPipe();
	auto client = np.first;
	auto server = np.second;
	t->Cleanup([server] { server->close(); });
	// shared_ptr: the cleanup below must keep the handler alive past conn's
	// destruction — Go's GC does this implicitly; a raw pointer here left
	// handler->release.mu_ destroyed when the cleanup ran.
	auto handler = std::make_shared<blockingHandler>();
	// Go defer: `select { case <-handler.release: return; default:
	// close(handler.release) }` — close() is idempotent here, so closing
	// unconditionally is equivalent.
	t->Cleanup([handler] { handler->release.close(); });
	auto conn = NewAsyncConn(server, std::shared_ptr<Handler>(handler));
	// Channels are heap-pinned: Go's chan is GC-owned, so a detached sender
	// may finish send()'s notify after the test frame is gone.
	auto runDone = std::make_shared<chan_<gostd::Error>>();
	auto ctx = t->Context();
	std::thread([runDone, conn, ctx] {
		runDone->send(conn->Run(ctx));
	}).detach();

	auto clientProtocol = NewJSONRPCProtocol(client);
	auto reqId = jsonrpc::NewIDInt(1);
	if (auto err = clientProtocol->WriteRequest(&reqId, "transform",
	                                            json::Value{});
	    err)
		t->Fatal({err->Error()});
	if (!handler->started.recv(1000).second) {
		t->Fatal({"request handler did not start"});
	}
	if (auto err = client->close(); err)
		t->Fatal({err->Error()});

	auto [err2, blocked] = runDone->recv(100);
	if (!blocked) {
		// handlerBlocked: Run must not return while the handler is
		// blocked.
	} else {
		t->Fatalf(
		    "connection stopped while request handler was blocked: %v",
		    {err2->Error()});
	}

	handler->release.close();
	auto [err3, ok] = runDone->recv(1000);
	if (!ok) {
		t->Fatal({"connection did not stop after request handler "
		          "completed"});
	}
	if (!err3 ||
	    err3->Error().find("ipc: failed to write response") ==
	        std::string::npos)
		t->Error({gostd::sprintf(
		    "expected failed-to-write-response error, got %v",
		    {err3 ? err3->Error() : std::string("nil")})});
	auto [_, cerr] =
	    conn->Call(t->Context(), "transform", json::Value{});
	if (!cerr ||
	    cerr->Error().find("ipc: failed to write response") ==
	        std::string::npos)
		t->Error({gostd::sprintf(
		    "expected failed-to-write-response error, got %v",
		    {cerr ? cerr->Error() : std::string("nil")})});
	cerr = conn->Notify(t->Context(), "changed", json::Value{});
	if (!cerr ||
	    cerr->Error().find("ipc: failed to write response") ==
	        std::string::npos)
		t->Error({gostd::sprintf(
		    "expected failed-to-write-response error, got %v",
		    {cerr ? cerr->Error() : std::string("nil")})});
}
REGISTER_UNIT_TEST("ipc.TestAsyncConnRunWaitsForRequestAfterPeerCloses",
                   TestAsyncConnRunWaitsForRequestAfterPeerCloses);

} // namespace
} // namespace tsc::ipc
