// ipc.h — declarations for tsc/internal/ipc (conn.go, protocol.go). The ipc
// slice owns the implementations; contentmapper declares the surface it uses
// and dep-stubs the constructors.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>

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

// NewJSONRPCProtocol — protocol_jsonrpc.go:20. Dep-stubbed; ipc slice owns.
std::shared_ptr<Protocol> NewJSONRPCProtocol(
    std::shared_ptr<gostd::io::ReadWriteCloser> rw);

// NewAsyncConnWithProtocol — conn_async.go:46. Dep-stubbed; ipc slice owns.
std::shared_ptr<Conn> NewAsyncConnWithProtocol(
    std::shared_ptr<gostd::io::ReadWriteCloser> rwc,
    std::shared_ptr<Protocol> protocol, std::shared_ptr<Handler> handler);

} // namespace tsc::ipc
