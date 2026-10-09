// conn_sync.cpp — Port of tsc/internal/ipc/conn_sync.go: SyncConn.
//
// Go's `c.mu sync.Mutex` serializes all protocol operations; the unlock/
// relock dance in Call lets a nested synchronous callback handle requests
// while a Call is in flight. recover() maps to try/catch, as in
// conn_async.cpp.

#include "internal/ipc/ipc.h"

namespace tsc::ipc {

// NewSyncConn — conn_sync.go:27.
std::shared_ptr<SyncConn> NewSyncConn(
    std::shared_ptr<gostd::io::ReadWriteCloser> rwc,
    std::shared_ptr<Protocol> protocol, std::shared_ptr<Handler> handler) {
	return std::make_shared<SyncConn>(std::move(rwc), std::move(protocol),
	                                  std::move(handler));
}

// SyncConn::Run — conn_sync.go:44. Starts processing messages on the
// connection; blocks until the context is cancelled or an error occurs.
gostd::Error SyncConn::Run(gostd::Context ctx) {
	for (;;) {
		if (gostd::Error e = gostd::ctxErr(ctx); e != nullptr) {
			return e;
		}

		std::unique_ptr<Message> msg;
		gostd::Error err;
		{
			std::lock_guard<std::mutex> lk(mu);
			auto [m, rerr] = protocol->ReadMessage();
			msg.reset(m);
			err = std::move(rerr);
		}

		if (err != nullptr) {
			if (gostd::errorIs(err, gostd::io::errEOF)) {
				return nullptr;
			}
			return err;
		}

		if (msg->IsRequest()) {
			if (gostd::Error herr = handleRequest(ctx, msg.get(), 0);
			    herr != nullptr) {
				return herr;
			}
		} else if (msg->IsNotification()) {
			handleNotification(ctx, msg.get());
		} else {
			// Responses are not expected in the main loop - they are read
			// inline by Call().
			return gostd::newError(
			    "ipc: unexpected response message in sync connection");
		}
	}
}

// SyncConn::lockTurn — conn_sync.go:91. Acquires mu once the nested call
// depth reaches `depth` (stack-ordered turns), then marks reading when
// nested (depth > 0). Returns with mu held.
void SyncConn::lockTurn(int depth) {
	mu.lock();
	std::unique_lock<std::mutex> lk(mu, std::adopt_lock);
	while (calls != depth) {
		turn.wait(lk);
	}
	lk.release();
	reading = depth > 0;
}

// SyncConn::handleRequest — conn_sync.go:81. Processes an incoming request.
gostd::Error SyncConn::handleRequest(gostd::Context ctx, Message* msg,
                                     int depth) {
	// Intercept the meta-requests for collected server timing before
	// dispatching to the handler, so they are answered directly and not
	// themselves recorded.
	if (msg->Method == MethodGetServerTiming) {
		gostd::Error writeErr;
		{
			lockTurn(depth);
			std::unique_lock<std::mutex> lk(mu, std::adopt_lock);
			writeErr = protocol->WriteResponse(
			    msgID(msg), serverTimingSnapshot(timing.get()));
		}
		if (writeErr != nullptr) {
			return gostd::errorf(
			    "ipc: failed to write server timing response: %w",
			    {writeErr});
		}
		return nullptr;
	}
	if (msg->Method == MethodResetServerTiming) {
		if (timing != nullptr) {
			timing->reset();
		}
		gostd::Error writeErr;
		{
			lockTurn(depth);
			std::unique_lock<std::mutex> lk(mu, std::adopt_lock);
			writeErr = protocol->WriteResponse(msgID(msg), json::Value{});
		}
		if (writeErr != nullptr) {
			return gostd::errorf(
			    "ipc: failed to write reset server timing response: %w",
			    {writeErr});
		}
		return nullptr;
	}

	gostd::Time start{};
	if (timing != nullptr) {
		start = gostd::now();
	}

	// Recover from panics and convert to error response with stack trace
	// (Go's deferred recover() covers everything below).
	try {
		auto [result, err] =
		    handler->HandleRequest(ctx, msg->Method, msg->Params);

		if (timing != nullptr) {
			timing->record(msg->Method, gostd::since(start));
		}

		gostd::Error writeErr;
		{
			lockTurn(depth);
			std::unique_lock<std::mutex> lk(mu, std::adopt_lock);
			if (err != nullptr) {
				jsonrpc::ResponseError re;
				re.Code = jsonrpc::CodeInternalError;
				re.Message = err->Error();
				writeErr = protocol->WriteError(msgID(msg), &re);
			} else {
				writeErr = protocol->WriteResponse(msgID(msg), result);
			}
		}

		if (writeErr != nullptr) {
			return gostd::errorf("ipc: failed to write response: %w",
			                     {writeErr});
		}
		return nullptr;
	} catch (...) {
		std::string r = panicText(std::current_exception());
		gostd::Error err =
		    gostd::errorf("panic: %v\n%s", {r, debugStack()});

		gostd::Error writeErr;
		{
			lockTurn(depth);
			std::unique_lock<std::mutex> lk(mu, std::adopt_lock);
			jsonrpc::ResponseError re;
			re.Code = jsonrpc::CodeInternalError;
			re.Message = err->Error();
			writeErr = protocol->WriteError(msgID(msg), &re);
		}

		if (writeErr != nullptr) {
			return gostd::errorf(
			    "ipc: failed to write panic error response: %w "
			    "(original panic: %v)",
			    {writeErr, r});
		}
		return nullptr;
	}
}

// SyncConn::handleNotification — conn_sync.go:161.
void SyncConn::handleNotification(gostd::Context ctx, Message* msg) {
	(void)handler->HandleNotification(ctx, msg->Method, msg->Params);
}

// SyncConn::Call — conn_sync.go:166. Sends a request to the client and waits
// for a response. Safe to call from multiple threads — calls are serialized.
std::pair<json::Value, gostd::Error>
SyncConn::Call(gostd::Context ctx, std::string_view method,
               const json::Value& params) {
	// Serialize all Call operations. This is critical because:
	// 1. The msgpack protocol uses method names as response IDs
	// 2. The handler code (project internals) may spawn goroutines that call
	//    filesystem callbacks concurrently
	// 3. We need to ensure write/read pairs are atomic
	std::unique_lock<std::mutex> lk(mu);
	while (reading) {
		turn.wait(lk);
	}
	calls++;
	reading = true;
	int depth = calls;
	struct CallGuard {
		SyncConn* c;
		~CallGuard() {
			c->calls--;
			c->reading = false;
			c->turn.notify_all();
		}
	} callGuard{this};

	jsonrpc::ID id = jsonrpc::NewIDString(method);

	if (gostd::Error err = protocol->WriteRequest(&id, method, params);
	    err != nullptr) {
		return {json::Value{}, err};
	}

	if (gostd::Error e = gostd::ctxErr(ctx); e != nullptr) {
		return {json::Value{}, e};
	}

	for (;;) {
		// Read the response inline.
		auto [m, err] = protocol->ReadMessage();
		if (err != nullptr) {
			return {json::Value{}, err};
		}
		std::unique_ptr<Message> msg(m);

		if (msg->IsResponse() && msg->Id.has_value() &&
		    msg->Id->String() == method) {
			if (msg->Error.has_value()) {
				return {json::Value{},
				        gostd::errorf(
				            "ipc: remote error [%d]: %s",
				            {msg->Error->Code, msg->Error->Message})};
			}
			return {msg->Result, nullptr};
		}
		if (msg->IsRequest()) {
			// A synchronous client callback may make a nested API request.
			// Release the protocol lock while handling it so nested
			// callbacks can proceed.
			reading = false;
			turn.notify_all();
			lk.unlock();
			gostd::Error herr = handleRequest(ctx, msg.get(), depth);
			lk.lock();
			if (herr != nullptr) {
				return {json::Value{}, herr};
			}
			continue;
		}
		if (msg->IsNotification()) {
			reading = false;
			turn.notify_all();
			lk.unlock();
			handleNotification(ctx, msg.get());
			lockTurn(depth);
			continue;
		}
		return {json::Value{},
		        gostd::errorf(
		            "ipc: unexpected message while waiting for %q response",
		            {method})};
	}
}

// SyncConn::Notify — conn_sync.go:222. Sends a notification to the client
// (no response expected).
gostd::Error SyncConn::Notify(gostd::Context, std::string_view method,
                              const json::Value& params) {
	std::lock_guard<std::mutex> lk(mu);
	return protocol->WriteNotification(method, params);
}

} // namespace tsc::ipc
