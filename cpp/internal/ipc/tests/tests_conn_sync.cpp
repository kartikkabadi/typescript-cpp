// tests_conn_sync.cpp — port of tsc/internal/ipc/conn_sync_test.go.
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

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

struct syncFailingResponseProtocol : Protocol {
	Message* message = nullptr;
	gostd::Error responseErr;

	std::pair<Message*, gostd::Error> ReadMessage() override {
		if (message == nullptr) {
			return {nullptr, gostd::io::errEOF};
		}
		auto* m = message;
		message = nullptr;
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

struct panicHandler : Handler {
	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		throw std::string("handler panic");
	}
	gostd::Error
	HandleNotification(gostd::Context ctx, std::string_view method,
	                   const json::Value& params) override {
		return gostd::Error();
	}
};

void TestSyncConnRunReturnsResponseWriteFailure(T* t) {
	t->Parallel();
	auto responseErr = gostd::newError("response write failed");
	auto* protocol = new syncFailingResponseProtocol();
	protocol->message = new Message{
	    .Id = jsonrpc::NewIDInt(1), .Method = "transform"};
	protocol->responseErr = responseErr;
	auto conn = NewSyncConn(
	    nullptr, std::shared_ptr<Protocol>(protocol),
	    std::shared_ptr<Handler>(std::make_shared<noOpHandler>()));

	auto err = conn->Run(t->Context());
	if (!gostd::errorIs(err, responseErr))
		t->Error({gostd::sprintf(
		    "expected response write error, got %v", {err->Error()})});
}
REGISTER_UNIT_TEST("ipc.TestSyncConnRunReturnsResponseWriteFailure",
                   TestSyncConnRunReturnsResponseWriteFailure);

void TestSyncConnRunReturnsPanicResponseWriteFailure(T* t) {
	t->Parallel();
	auto responseErr = gostd::newError("response write failed");
	auto* protocol = new syncFailingResponseProtocol();
	protocol->message = new Message{
	    .Id = jsonrpc::NewIDInt(1), .Method = "transform"};
	protocol->responseErr = responseErr;
	auto conn = NewSyncConn(
	    nullptr, std::shared_ptr<Protocol>(protocol),
	    std::shared_ptr<Handler>(std::make_shared<panicHandler>()));

	auto err = conn->Run(t->Context());
	if (!gostd::errorIs(err, responseErr))
		t->Error({gostd::sprintf(
		    "expected panic response write error, got %v",
		    {err->Error()})});
	if (err->Error().find("original panic: handler panic") ==
	    std::string::npos)
		t->Error({gostd::sprintf(
		    "expected error to mention original panic, got %v",
		    {err->Error()})});
}
REGISTER_UNIT_TEST(
    "ipc.TestSyncConnRunReturnsPanicResponseWriteFailure",
    TestSyncConnRunReturnsPanicResponseWriteFailure);

// chan_ — a tiny Go chan analog with an optional receive timeout (select +
// time.After). Same helper as tests_conn_async.cpp (file-local in Go too:
// both files share package ipc_test, but keeping it local mirrors the
// per-file C++ layout).
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

// gatedHandler — conn_sync_test.go:85. Blocks in HandleRequest until the
// per-params gate channel is closed, then echoes the params back. entered
// reports each invocation so the test can approximate synctest.Wait().
struct gatedHandler : Handler {
	std::unordered_map<std::string, std::shared_ptr<chan_<int>>> gates;
	std::shared_ptr<chan_<std::string>> entered =
	    std::make_shared<chan_<std::string>>();

	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		std::string key(params);
		entered->send(key);
		auto it = gates.find(key);
		if (it != gates.end()) {
			it->second->recv();
		}
		return {params, gostd::Error()};
	}
	gostd::Error
	HandleNotification(gostd::Context ctx, std::string_view method,
	                   const json::Value& params) override {
		return gostd::Error();
	}
};

void TestSyncConnAnswersNestedRequestsInStackOrder(T* t) {
	t->Parallel();
	// Go: synctest.Test(t, ...) — there is no bubble-time runtime in C++;
	// the handler's entered signal plus short sleeps stand in for
	// synctest.Wait(). Heap-pinned shared_ptrs let detached threads outlive
	// the test frame.
	auto np = netPipe();
	auto server = np.first, client = np.second;
	t->Cleanup([client] { client->close(); });
	t->Cleanup([server] { server->close(); });
	auto handler = std::make_shared<gatedHandler>();
	handler->gates.emplace("\"a\"", std::make_shared<chan_<int>>());
	handler->gates.emplace("\"b\"", std::make_shared<chan_<int>>());
	auto conn = NewSyncConn(server,
	                        std::shared_ptr<Protocol>(
	                            NewJSONRPCProtocol(server)),
	                        std::shared_ptr<Handler>(handler));
	auto peer = NewJSONRPCProtocol(client);

	auto answers = std::make_shared<std::map<std::string, std::string>>();
	// serve — conn_sync_test.go:101. For each request the peer writes a
	// nested "resolve" request back, records the nested response, then
	// answers the original request with its params.
	// The detached serve thread outlives this frame (Go's closure pins
	// peer/answers via GC); a heap-held std::function plus by-value
	// captures gives the same lifetime.
	auto serve = std::make_shared<std::function<json::Value()>>();
	*serve = [serve, peer, answers]() -> json::Value {
		for (;;) {
			auto [m, rerr] = peer->ReadMessage();
			if (rerr != nullptr) {
				return json::Value{};
			}
			std::unique_ptr<Message> msg(m);
			if (msg->IsResponse()) {
				return msg->Result;
			}
			auto rid = jsonrpc::NewIDString("resolve");
			peer->WriteRequest(&rid, "resolve", msg->Params);
			(*answers)[std::string(msg->Params)] =
			    std::string((*serve)());
			peer->WriteResponse(msgID(msg.get()), msg->Params);
		}
	};
	std::thread([serve] { (*serve)(); }).detach();

	auto ctx = t->Context();
	auto a = std::make_shared<json::Value>();
	auto b = std::make_shared<json::Value>();
	auto doneA = std::make_shared<chan_<int>>();
	auto doneB = std::make_shared<chan_<int>>();
	std::thread([conn, ctx, a, doneA] {
		auto [r, _e] = conn->Call(ctx, "callback", json::Value("\"a\""));
		*a = r;
		doneA->send(1);
	}).detach();
	// synctest.Wait(): a's request must be inside the gated handler.
	auto [k1, ok1] = handler->entered->recv(5000);
	if (!ok1) {
		t->Fatal({gostd::newError("timed out waiting for handler 'a'")
		              ->Error()});
	}
	std::thread([conn, ctx, b, doneB] {
		auto [r, _e] = conn->Call(ctx, "callback", json::Value("\"b\""));
		*b = r;
		doneB->send(1);
	}).detach();
	// synctest.Wait(): give b's Call time to queue behind a's turn.
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	handler->gates["\"a\""]->close();
	// synctest.Wait(): b reaches the handler only after a's call unwinds.
	auto [k2, ok2] = handler->entered->recv(5000);
	if (!ok2) {
		t->Fatal({gostd::newError("timed out waiting for handler 'b'")
		              ->Error()});
	}
	handler->gates["\"b\""]->close();
	doneA->recv(5000);
	doneB->recv(5000);

	if ((*answers)["\"a\""] != "\"a\"" ||
	    (*answers)["\"b\""] != "\"b\"") {
		t->Error({gostd::sprintf("expected answers {a: a, b: b}, got %v",
		                         {(*answers)["\"a\""] + " " +
		                          (*answers)["\"b\""]})});
	}
	if (std::string(*a) != "\"a\"") {
		t->Error({gostd::sprintf("expected a == \"a\", got %v", {*a})});
	}
	if (std::string(*b) != "\"b\"") {
		t->Error({gostd::sprintf("expected b == \"b\", got %v", {*b})});
	}
}
REGISTER_UNIT_TEST("ipc.TestSyncConnAnswersNestedRequestsInStackOrder",
                   TestSyncConnAnswersNestedRequestsInStackOrder);

// callingNotificationHandler — conn_sync_test.go:151.
struct callingNotificationHandler : noOpHandler {
	std::shared_ptr<SyncConn> conn;

	gostd::Error
	HandleNotification(gostd::Context ctx, std::string_view method,
	                   const json::Value& params) override {
		auto [_r, err] = conn->Call(ctx, "inner", json::Value{});
		return err;
	}
};

void TestSyncConnNotificationHandlerCanCall(T* t) {
	t->Parallel();
	auto np = netPipe();
	auto server = np.first, client = np.second;
	t->Cleanup([client] { client->close(); });
	t->Cleanup([server] { server->close(); });
	auto handler = std::make_shared<callingNotificationHandler>();
	auto conn = NewSyncConn(server,
	                        std::shared_ptr<Protocol>(
	                            NewJSONRPCProtocol(server)),
	                        std::shared_ptr<Handler>(handler));
	handler->conn = conn;
	auto peer = NewJSONRPCProtocol(client);
	std::thread([peer] {
		auto [o, _e1] = peer->ReadMessage();
		std::unique_ptr<Message> outer(o);
		peer->WriteNotification("notify", json::Value{});
		auto [i, _e2] = peer->ReadMessage();
		std::unique_ptr<Message> inner(i);
		peer->WriteResponse(msgID(inner.get()),
		                    json::Value("\"" + inner->Method + "\""));
		peer->WriteResponse(msgID(outer.get()),
		                    json::Value("\"" + outer->Method + "\""));
	}).detach();

	auto [result, err] = conn->Call(t->Context(), "outer", json::Value{});
	if (err != nullptr) {
		t->Error({gostd::sprintf("unexpected call error: %v", {err->Error()})});
	}
	if (std::string(result) != "\"outer\"") {
		t->Error({gostd::sprintf("expected result == \"outer\", got %v",
		                         {result})});
	}
}
REGISTER_UNIT_TEST("ipc.TestSyncConnNotificationHandlerCanCall",
                   TestSyncConnNotificationHandlerCanCall);

} // namespace
} // namespace tsc::ipc
