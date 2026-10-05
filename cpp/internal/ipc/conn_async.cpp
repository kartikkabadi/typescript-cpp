// conn_async.cpp — Port of tsc/internal/ipc/conn_async.go: AsyncConn.
//
// Concurrency mapping:
// - sync.WaitGroup.Go per-request goroutines → waitGroup::go (detached
//   std::thread + counter/condition_variable for Wait).
// - atomic.Int64 seq → std::atomic<int64_t>.
// - pending map[ID]chan *Message + pendingMu → unordered_map<ID,
//   shared_ptr<pendingCall>> + pendingMu; pendingCall (ipc.h) is mutex +
//   condvar + a single message slot + closed flag — callers block in Call
//   until the response is delivered or a terminal error closes it.
// - context.Context → gostd::Context; Call's `select` on ctx.Done +
//   the response channel is a condvar wait bridged by contextAfterFunc.
// - buffered `chan error` requestErrors(1) → requestErrors slot + flag
//   (only the first recordRequestError lands).
// - runtime/debug.Stack → execinfo backtrace (debugStack).
// - recover() → try/catch around the handler call + response write; the
//   panic value's %v is recovered via panicText(std::current_exception()).

#include "internal/ipc/ipc.h"

#include <cstdlib>
#include <execinfo.h>

namespace tsc::ipc {

// debugStack — runtime/debug.Stack().
std::string debugStack() {
	void* frames[64];
	int n = ::backtrace(frames, 64);
	char** syms = ::backtrace_symbols(frames, n);
	std::string out;
	if (syms != nullptr) {
		for (int i = 0; i < n; i++) {
			out += syms[i];
			out += '\n';
		}
		std::free(syms);
	}
	return out;
}

// panicText — the `%v` text of a recovered panic value.
std::string panicText(std::exception_ptr ep) {
	try {
		if (ep != nullptr) {
			std::rethrow_exception(ep);
		}
	} catch (const std::exception& e) {
		return e.what();
	} catch (const std::string& s) {
		return s;
	} catch (const char* s) {
		return s != nullptr ? s : "unknown";
	} catch (int v) {
		return std::to_string(v);
	} catch (...) {
	}
	return "unknown";
}

// NewAsyncConn — conn_async.go:41.
std::shared_ptr<AsyncConn> NewAsyncConn(
    std::shared_ptr<gostd::io::ReadWriteCloser> rwc,
    std::shared_ptr<Handler> handler) {
	auto protocol = NewJSONRPCProtocol(rwc);
	return std::make_shared<AsyncConn>(rwc, std::move(protocol),
	                                   std::move(handler));
}

// NewAsyncConnWithProtocol — conn_async.go:46.
std::shared_ptr<Conn> NewAsyncConnWithProtocol(
    std::shared_ptr<gostd::io::ReadWriteCloser> rwc,
    std::shared_ptr<Protocol> protocol, std::shared_ptr<Handler> handler) {
	return std::make_shared<AsyncConn>(std::move(rwc), std::move(protocol),
	                                   std::move(handler));
}

// AsyncConn::Run — conn_async.go:64.
gostd::Error AsyncConn::Run(gostd::Context ctx) {
	auto [handlerCtx, cancelHandlers] = gostd::contextWithCancel(ctx);
	requestErrors requestErrs;

	gostd::Error err = runLoop(ctx, handlerCtx, &requestErrs);

	// Go's deferred cleanup.
	closePendingCalls(err);
	cancelHandlers();
	handlers.wait();
	// select { case requestErr := <-requestErrors: ...; default: }
	if (requestErrs.has) {
		err = gostd::joinError({err, requestErrs.err});
	}
	return err;
}

// runLoop — the read/dispatch loop of Run (conn_async.go:69-100).
gostd::Error AsyncConn::runLoop(gostd::Context ctx,
                                const gostd::Context& handlerCtx,
                                requestErrors* requestErrs) {
	for (;;) {
		if (gostd::Error e = gostd::ctxErr(ctx); e != nullptr) {
			return e;
		}

		auto [m, err] = protocol->ReadMessage();
		if (err != nullptr) {
			if (gostd::errorIs(err, gostd::io::errEOF)) {
				return nullptr;
			}
			return err;
		}
		std::unique_ptr<Message> msg(m);

		if (msg->IsResponse()) {
			handleResponse(std::move(msg));
		} else if (msg->IsRequest()) {
			handlers.go([this, handlerCtx, m = msg.release(),
			             requestErrs]() mutable {
				std::unique_ptr<Message> msg(m);
				m = nullptr;
				if (gostd::Error requestErr =
				        handleRequest(handlerCtx, msg.get());
				    requestErr != nullptr) {
					if (recordRequestError(requestErr, requestErrs)) {
						if (rwc != nullptr) {
							(void)rwc->close();
						}
					}
				}
			});
		} else if (msg->IsNotification()) {
			handlers.go([this, handlerCtx, m = msg.release()]() mutable {
				std::unique_ptr<Message> msg(m);
				m = nullptr;
				handleNotification(handlerCtx, msg.get());
			});
		}
	}
}

// AsyncConn::closePendingCalls — conn_async.go:112. Records that the read
// loop has exited and unblocks requests waiting for a response.
void AsyncConn::closePendingCalls(const gostd::Error& runErr) {
	std::lock_guard<std::mutex> lk(pendingMu);
	recordTerminalErrorLocked(runErr);
	closePendingCallsLocked();
}

// AsyncConn::recordRequestError — conn_async.go:119.
bool AsyncConn::recordRequestError(const gostd::Error& requestErr,
                                   requestErrors* errs) {
	std::lock_guard<std::mutex> lk(pendingMu);
	if (!recordTerminalErrorLocked(requestErr)) {
		return false;
	}
	errs->err = requestErr;
	errs->has = true;
	closePendingCallsLocked();
	return true;
}

// AsyncConn::recordTerminalErrorLocked — conn_async.go:130. Caller holds
// pendingMu.
bool AsyncConn::recordTerminalErrorLocked(const gostd::Error& terminalErr) {
	if (terminal == nullptr) {
		terminal = ErrConnClosed;
		if (terminalErr != nullptr) {
			terminal = gostd::joinError({terminal, terminalErr});
			hasCause = true;
			return true;
		}
	} else if (!hasCause && terminalErr != nullptr) {
		terminal = gostd::joinError({terminal, terminalErr});
		hasCause = true;
		return true;
	}
	return false;
}

// AsyncConn::closePendingCallsLocked — conn_async.go:145. Caller holds
// pendingMu.
void AsyncConn::closePendingCallsLocked() {
	for (auto& [id, ch] : pending) {
		ch->close();
	}
	pending.clear();
}

// AsyncConn::handleResponse — conn_async.go:154. Matches a response to a
// pending request.
void AsyncConn::handleResponse(std::unique_ptr<Message> msg) {
	std::shared_ptr<pendingCall> ch;
	{
		std::lock_guard<std::mutex> lk(pendingMu);
		auto it = pending.find(*msg->Id);
		if (it != pending.end()) {
			ch = it->second;
			pending.erase(it);
		}
	}

	if (ch != nullptr) {
		ch->deliver(std::move(msg)); // ch <- msg; close(ch)
	}
}

// AsyncConn::handleRequest — conn_async.go:167. Processes an incoming
// request.
gostd::Error AsyncConn::handleRequest(gostd::Context ctx, Message* msg) {
	// Intercept the meta-requests for collected server timing before
	// dispatching to the handler, so they are answered directly and not
	// themselves recorded.
	if (msg->Method == MethodGetServerTiming) {
		gostd::Error writeErr;
		{
			std::lock_guard<std::mutex> lk(writeMu);
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
			std::lock_guard<std::mutex> lk(writeMu);
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
			std::lock_guard<std::mutex> lk(writeMu);
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
			std::lock_guard<std::mutex> lk(writeMu);
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

// AsyncConn::handleNotification — conn_async.go:236.
void AsyncConn::handleNotification(gostd::Context ctx, Message* msg) {
	(void)handler->HandleNotification(ctx, msg->Method, msg->Params);
}

// AsyncConn::Call — conn_async.go:241. Sends a request to the client and
// waits for a response.
std::pair<json::Value, gostd::Error>
AsyncConn::Call(gostd::Context ctx, std::string_view method,
                const json::Value& params) {
	// Create unique request ID
	jsonrpc::ID id = jsonrpc::NewIDString(
	    gostd::sprintf("api%d", {seq.fetch_add(1) + 1}));

	// Register response channel BEFORE sending request to avoid race
	auto responseChan = std::make_shared<pendingCall>();
	{
		std::lock_guard<std::mutex> lk(pendingMu);
		if (terminal != nullptr) {
			gostd::Error err = terminal;
			return {json::Value{}, err};
		}
		pending[id] = responseChan;
	}

	// Go's defer: close+delete the channel if still registered.
	auto out = callAwait(ctx, id, responseChan, method, params);
	removePendingCall(id);
	return out;
}

// AsyncConn::callAwait — conn_async.go:264-290. The tail of Call after the
// pending registration.
std::pair<json::Value, gostd::Error>
AsyncConn::callAwait(gostd::Context ctx, const jsonrpc::ID& id,
                     const std::shared_ptr<pendingCall>& responseChan,
                     std::string_view method, const json::Value& params) {
	// Send the request
	gostd::Error err;
	{
		std::lock_guard<std::mutex> lk(writeMu);
		err = protocol->WriteRequest(&id, method, params);
	}
	if (err != nullptr) {
		return {json::Value{}, err};
	}

	// select { case <-ctx.Done(): ...; case resp, ok := <-responseChan: }
	// One condvar wait covers both: contextAfterFunc notifies responseChan's
	// cv when ctx finishes.
	auto stop = gostd::contextAfterFunc(ctx, [responseChan] {
		responseChan->cv.notify_all();
	});
	std::unique_ptr<Message> resp;
	{
		std::unique_lock<std::mutex> lk(responseChan->mu);
		responseChan->cv.wait(lk, [&] {
			return responseChan->closed ||
			       gostd::ctxErr(ctx) != nullptr;
		});
		if (gostd::Error ctxE = gostd::ctxErr(ctx); ctxE != nullptr) {
			stop();
			return {json::Value{}, ctxE};
		}
		resp = std::move(responseChan->msg);
	}
	stop();

	if (resp == nullptr) {
		// Channel closed without a value: the connection reached a
		// terminal state.
		std::lock_guard<std::mutex> lk(pendingMu);
		return {json::Value{}, terminal};
	}
	if (resp->Error.has_value()) {
		return {json::Value{},
		        gostd::errorf("ipc: remote error [%d]: %s",
		                      {resp->Error->Code, resp->Error->Message})};
	}
	return {resp->Result, nullptr};
}

// AsyncConn::removePendingCall — the deferred channel cleanup in Call
// (conn_async.go:254-262).
void AsyncConn::removePendingCall(const jsonrpc::ID& id) {
	std::lock_guard<std::mutex> lk(pendingMu);
	auto it = pending.find(id);
	if (it != pending.end()) {
		it->second->close();
		pending.erase(it);
	}
}

// AsyncConn::Notify — conn_async.go:295. Sends a notification to the client
// (no response expected).
gostd::Error AsyncConn::Notify(gostd::Context, std::string_view method,
                               const json::Value& params) {
	{
		std::lock_guard<std::mutex> lk(pendingMu);
		if (terminal != nullptr) {
			return terminal;
		}
	}
	std::lock_guard<std::mutex> lk(writeMu);
	return protocol->WriteNotification(method, params);
}

} // namespace tsc::ipc
