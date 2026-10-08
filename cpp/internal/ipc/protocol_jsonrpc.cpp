// protocol_jsonrpc.cpp — Port of tsc/internal/ipc/protocol_jsonrpc.go:
// JSONRPCProtocol (JSON-RPC 2.0 over LSP Content-Length framing).
//
// Go marshals message structs via encoding/json; the port uses the
// jsonrpc::marshal*/unmarshal* helpers. Params/Result are pre-marshaled
// json::Value (raw JSON passthrough), matching the Conn/Handler surface.

#include "internal/ipc/ipc.h"

namespace tsc::ipc {

// NewJSONRPCProtocol — protocol_jsonrpc.go:20.
std::shared_ptr<Protocol> NewJSONRPCProtocol(
    std::shared_ptr<gostd::io::ReadWriteCloser> rw) {
	return std::make_shared<JSONRPCProtocol>(std::move(rw));
}

JSONRPCProtocol::JSONRPCProtocol(
    std::shared_ptr<gostd::io::ReadWriteCloser> rw_)
    : rw(std::move(rw_)), reader(jsonrpc::NewReader(rw.get())),
      writer(jsonrpc::NewWriter(rw.get())) {}

// JSONRPCProtocol::ReadMessage — protocol_jsonrpc.go:30.
std::pair<Message*, gostd::Error> JSONRPCProtocol::ReadMessage() {
	auto [data, err] = reader->Read();
	if (err != nullptr) {
		return {nullptr, err};
	}

	auto [msg, uerr] = jsonrpc::unmarshalMessage(data);
	if (uerr != nullptr) {
		return {nullptr, uerr};
	}

	return {new Message(std::move(msg)), nullptr};
}

// JSONRPCProtocol::WriteRequest — protocol_jsonrpc.go:43.
gostd::Error JSONRPCProtocol::WriteRequest(const jsonrpc::ID* id,
                                           std::string_view method,
                                           const json::Value& params) {
	jsonrpc::RequestMessage msg;
	msg.Id = id;
	msg.Method = std::string(method);
	msg.Params = params;
	json::Value data = jsonrpc::marshalRequestMessage(msg);
	return writer->Write(data);
}

// JSONRPCProtocol::WriteNotification — protocol_jsonrpc.go:56.
gostd::Error
JSONRPCProtocol::WriteNotification(std::string_view method,
                                   const json::Value& params) {
	jsonrpc::RequestMessage msg;
	msg.Method = std::string(method);
	msg.Params = params;
	json::Value data = jsonrpc::marshalRequestMessage(msg);
	return writer->Write(data);
}

// JSONRPCProtocol::WriteResponse — protocol_jsonrpc.go:68.
gostd::Error JSONRPCProtocol::WriteResponse(const jsonrpc::ID* id,
                                            const json::Value& result) {
	json::Value r = result;
	if (r.empty()) {
		r = "null"; // Go: result == nil → json.Value("null")
	}
	jsonrpc::ResponseMessage msg;
	msg.Id = id;
	msg.Result = std::move(r);
	json::Value data = jsonrpc::marshalResponseMessage(msg);
	return writer->Write(data);
}

// JSONRPCProtocol::WriteError — protocol_jsonrpc.go:82.
gostd::Error JSONRPCProtocol::WriteError(
    const jsonrpc::ID* id, const jsonrpc::ResponseError* respErr) {
	jsonrpc::ResponseMessage msg;
	msg.Id = id;
	msg.Error = respErr;
	json::Value data = jsonrpc::marshalResponseMessage(msg);
	return writer->Write(data);
}

} // namespace tsc::ipc
