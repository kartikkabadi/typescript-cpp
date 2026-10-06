// jsonrpc.h — port of tsc/internal/jsonrpc: JSON-RPC 2.0 message types.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "internal/gostd/gostd.h"
#include "internal/json/json.h"

namespace tsc::jsonrpc {

// JSONRPCVersion — jsonrpc.go:14. Marshals as "2.0".
struct JSONRPCVersion {
	bool operator==(const JSONRPCVersion&) const = default;
};

inline const gostd::Error ErrInvalidJSONRPCVersion =
	gostd::newError("invalid JSON-RPC version");

// ID — jsonrpc.go:32. A JSON-RPC message ID: string or int32.
struct ID {
	std::string str;
	int32_t int_ = 0;

	std::string String() const;
	std::string marshalJSON() const;
	gostd::Error unmarshalJSON(const json::Value& data);
	std::pair<int32_t, bool> TryInt() const;
	int32_t MustInt() const;
	bool isZero() const { return str.empty() && int_ == 0; }
};

// NewIDString / NewIDInt — jsonrpc.go:46,51.
inline ID NewIDString(std::string_view s) { return ID{std::string(s), 0}; }
inline ID NewIDInt(int32_t i) { return ID{"", i}; }

// IntegerOrString — jsonrpc.go:92.
struct IntegerOrString {
	std::optional<int32_t> Integer;
	std::optional<std::string> String;
};
inline ID NewID(const IntegerOrString& rawValue) {
	if (rawValue.String.has_value()) {
		return NewIDString(*rawValue.String);
	}
	return NewIDInt(rawValue.Integer.value_or(0));
}

// ResponseError — jsonrpc.go:98.
struct ResponseError {
	int32_t Code = 0;
	std::string Message;
	json::Value Data; // `json:"data,omitzero"` — raw JSON

	std::string String() const;
	std::string Error() const { return String(); }
};

// Standard JSON-RPC error codes — jsonrpc.go:120.
inline constexpr int32_t CodeParseError = -32700;
inline constexpr int32_t CodeInvalidRequest = -32600;
inline constexpr int32_t CodeMethodNotFound = -32601;
inline constexpr int32_t CodeInvalidParams = -32602;
inline constexpr int32_t CodeInternalError = -32603;

// MessageKind — jsonrpc.go:129.
enum class MessageKind : int32_t {
	Notification = 0,
	Request = 1,
	Response = 2,
};

// Message — jsonrpc.go:139. A raw JSON-RPC message (request, notification,
// or response) with params/result kept as raw JSON.
struct Message {
	JSONRPCVersion JSONRPC;
	std::optional<ID> Id;        // `json:"id,omitzero"`
	std::string Method;          // `json:"method,omitzero"`
	json::Value Params;          // `json:"params,omitzero"`
	json::Value Result;          // `json:"result,omitzero"`
	std::optional<ResponseError> Error; // `json:"error,omitzero"`

	MessageKind Kind() const;
	bool IsRequest() const { return Id.has_value() && !Method.empty(); }
	bool IsNotification() const { return !Id.has_value() && !Method.empty(); }
	bool IsResponse() const { return Id.has_value() && Method.empty(); }
};

// RequestMessage — jsonrpc.go:175. Convenience type for requests/notifications.
struct RequestMessage {
	JSONRPCVersion JSONRPC;
	const ID* Id = nullptr;   // `json:"id,omitzero"`
	std::string Method;       // `json:"method"`
	json::Value Params;       // `json:"params,omitzero"`
};

// ResponseMessage — jsonrpc.go:183.
struct ResponseMessage {
	JSONRPCVersion JSONRPC;
	const ID* Id = nullptr;   // `json:"id,omitzero"`
	json::Value Result;       // `json:"result,omitzero"`
	const ResponseError* Error = nullptr; // `json:"error,omitzero"`
};

// Marshal/unmarshal — json.Marshal/json.Unmarshal of the message types.
json::Value marshalRequestMessage(const RequestMessage& m);
json::Value marshalResponseMessage(const ResponseMessage& m);
json::Value marshalMessage(const Message& m);
std::pair<Message, gostd::Error> unmarshalMessage(const json::Value& data);
std::pair<ResponseError, gostd::Error> unmarshalResponseError(
    const json::Value& data);

// === slice: ipc ===

// ID equality + hash — Go map[jsonrpc.ID]chan *Message keys on the ID value.
inline bool operator==(const ID& a, const ID& b) {
	return a.str == b.str && a.int_ == b.int_;
}
struct IDHash {
	size_t operator()(const ID& id) const {
		return std::hash<std::string>{}(id.str) * 0x9e3779b97f4a7c15ULL +
		       std::hash<int32_t>{}(id.int_);
	}
};

// --- baseproto.go ------------------------------------------------------------
// LSP base protocol: Content-Length header framing over a byte stream.

inline const gostd::Error ErrInvalidHeader =
    gostd::newError("jsonrpc: invalid header");
inline const gostd::Error ErrInvalidContentLength =
    gostd::newError("jsonrpc: invalid content length");
inline const gostd::Error ErrNoContentLength =
    gostd::newError("jsonrpc: no content length");

// Reader — baseproto.go:25. Reads JSON-RPC messages with Content-Length
// framing (bufio.Reader equivalent internally).
class Reader {
public:
	explicit Reader(gostd::io::Reader* r) : r_(r) {}
	// Read reads the next message payload (bufio + io.ReadFull semantics).
	std::pair<std::string, gostd::Error> Read();

private:
	gostd::io::Reader* r_;
	std::string buf_;

	// readBytes — bufio.Reader.ReadBytes: collects through delim; on error
	// returns the accumulated bytes (incl. partial fragment) plus the error.
	std::pair<std::string, gostd::Error> readBytes(char delim);
	// readFull — io.ReadFull over the buffered reader.
	gostd::Error readFull(std::span<char> out);
};

// Writer — baseproto.go:79. Writes JSON-RPC messages with Content-Length
// framing (bufio.Writer equivalent internally).
class Writer {
public:
	explicit Writer(gostd::io::Writer* w) : w_(w) {}
	// Write writes a message payload with the Content-Length header, then
	// flushes (bufio: header + payload buffer, single Flush).
	gostd::Error Write(std::string_view data);

private:
	gostd::io::Writer* w_;
	std::string buf_;

	gostd::Error flush();
};

// NewReader / NewWriter — baseproto.go:32,86.
inline std::unique_ptr<Reader> NewReader(gostd::io::Reader* r) {
	return std::make_unique<Reader>(r);
}
inline std::unique_ptr<Writer> NewWriter(gostd::io::Writer* w) {
	return std::make_unique<Writer>(w);
}

// === end slice: ipc ===

} // namespace tsc::jsonrpc
