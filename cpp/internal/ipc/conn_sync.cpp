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
			if (gostd::Error herr = handleRequest(ctx, msg.get());
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

// SyncConn::handleRequest — conn_sync.go:81. Processes an incoming request.
gostd::Error SyncConn::handleRequest(gostd::Context ctx, Message* msg) {
	// Intercept the meta-requests for collected server timing before
	// dispatching to the handler, so they are answered directly and not
	// themselves recorded.
	if (msg->Method == MethodGetServerTiming) {
		gostd::Error writeErr;
		{
			std::lock_guard<std::mutex> lk(mu);
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
			std::lock_guard<std::mutex> lk(mu);
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
			std::lock_guard<std::mutex> lk(mu);
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
			std::lock_guard<std::mutex> lk(mu);
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
			lk.unlock();
			gostd::Error herr = handleRequest(ctx, msg.get());
			lk.lock();
			if (herr != nullptr) {
				return {json::Value{}, herr};
			}
			continue;
		}
		if (msg->IsNotification()) {
			lk.unlock();
			handleNotification(ctx, msg.get());
			lk.lock();
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
