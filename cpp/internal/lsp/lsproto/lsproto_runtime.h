#pragma once

// ============================================================================
// lsproto_runtime.h — hand-ported runtime for the LSP protocol slice:
// baseproto.go + jsonrpc.go + lsp.go + structcodec.go + util.go (everything
// in tsc/internal/lsp/lsproto except the generated type tables in
// lsproto_generated.h).
// ============================================================================

#include <array>
#include <functional>
#include <type_traits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/json/json.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/tspath/tspath.h"

// ---------------------------------------------------------------------------
// ADL codec shims for jsonrpc types that only have Value-based codecs
// (jsonrpc.go gives them MarshalJSON/UnmarshalJSON; the streaming decoder
// needs the *From form).
// ---------------------------------------------------------------------------
namespace tsc::jsonrpc {

inline std::string marshalJSONTo(json::Encoder& enc, const JSONRPCVersion&) {
	return enc.writeValue(json::Value("\"2.0\""));
}
inline std::string unmarshalJSONFrom(json::Decoder& dec, JSONRPCVersion*) {
	auto [v, err] = dec.readValue();
	if (!err.empty()) return err;
	if (v != "\"2.0\"") return ErrInvalidJSONRPCVersion->Error();
	return {};
}
inline std::string marshalJSONTo(json::Encoder& enc, const ID& id) {
	return enc.writeValue(json::Value(id.marshalJSON()));
}
inline std::string unmarshalJSONFrom(json::Decoder& dec, ID* id) {
	auto [v, err] = dec.readValue();
	if (!err.empty()) return err;
	if (auto e = id->unmarshalJSON(v); e != nullptr) return e->Error();
	return {};
}
inline std::string marshalJSONTo(json::Encoder& enc, const ResponseError& e) {
	if (auto err = enc.writeToken(json::BeginObject); !err.empty()) return err;
	if (auto err = enc.writeValue("\"code\""); !err.empty()) return err;
	if (auto err = json::marshalEncode(enc, e.Code); !err.empty()) return err;
	if (auto err = enc.writeValue("\"message\""); !err.empty()) return err;
	if (auto err = json::marshalEncode(enc, e.Message); !err.empty()) return err;
	if (!e.Data.empty()) {
		if (auto err = enc.writeValue("\"data\""); !err.empty()) return err;
		if (auto err = enc.writeValue(e.Data); !err.empty()) return err;
	}
	return enc.writeToken(json::EndObject);
}
inline std::string unmarshalJSONFrom(json::Decoder& dec, ResponseError* e) {
	auto [v, err] = dec.readValue();
	if (!err.empty()) return err;
	auto [re, e2] = unmarshalResponseError(v);
	if (e2) return e2->Error();
	*e = std::move(re);
	return {};
}

} // namespace tsc::jsonrpc

namespace tsc::lsp::lsproto {

using gostd::Error;

// ---------------------------------------------------------------------------
// util.go:11 — Nil is the NUL byte rune used as a sentinel.
// ---------------------------------------------------------------------------
inline constexpr char Nil = '\x00';

// ---------------------------------------------------------------------------
// Go collection aliases for this slice.
// ---------------------------------------------------------------------------
template <class T> using Slice = std::optional<std::vector<T>>;
template <class K, class V> using Map = std::unordered_map<K, V>;

// ---------------------------------------------------------------------------
// lsp.go — LSPAny models Go `any` as decoded by encoding/json: nil, bool,
// float64, string, []any, map[string]any (plus an Int arm for programmatically
// constructed values).
// ---------------------------------------------------------------------------
struct LSPAny {
	enum class K { Nil, Bool, Int, Float, String, Array, Object };
	K kind = K::Nil;
	bool b = false;
	int64_t i = 0;
	double f = 0;
	std::string s;
	std::vector<LSPAny> arr;
	std::map<std::string, LSPAny> obj;

	LSPAny() = default;
	LSPAny(bool v) : kind(K::Bool), b(v) {}
	LSPAny(int v) : kind(K::Int), i(v) {}
	LSPAny(int64_t v) : kind(K::Int), i(v) {}
	LSPAny(double v) : kind(K::Float), f(v) {}
	LSPAny(const char* v) : kind(K::String), s(v) {}
	LSPAny(std::string_view v) : kind(K::String), s(v) {}
	LSPAny(std::string v) : kind(K::String), s(std::move(v)) {}
	LSPAny(std::vector<LSPAny> v) : kind(K::Array), arr(std::move(v)) {}
	LSPAny(std::map<std::string, LSPAny> v) : kind(K::Object), obj(std::move(v)) {}

	bool isNil() const { return kind == K::Nil; }
	std::string marshalJSONTo(json::Encoder& enc) const;
	std::string unmarshalJSONFrom(json::Decoder& dec);
};

using DocumentUri = std::string;  // lsp.go:17 (`type DocumentUri string`)
using URI = std::string;          // lsp.go:83 (`type URI string`)
using Method = std::string;       // lsp.go:85 (`type Method string`)

// lsp.go:19 — DocumentUri.FileName().
std::string documentUriFileName(DocumentUri uri);
// lsp.go:52 — DocumentUri.Path(useCaseSensitiveFileNames).
tspath::Path documentUriPath(DocumentUri uri, bool useCaseSensitiveFileNames);

// ---------------------------------------------------------------------------
// lsp.go — Null / NoParams / EmptyObject (`struct{}`).
// ---------------------------------------------------------------------------
struct Null {
	bool isZero() const { return false; }
	std::string marshalJSONTo(json::Encoder& enc) const;        // lsp.go:279
	std::string unmarshalJSONFrom(json::Decoder& dec);          // lsp.go:268
};

struct NoParams {
	bool isZero() const { return true; }                        // lsp.go:285
	std::string marshalJSONTo(json::Encoder& enc) const;
	std::string unmarshalJSONFrom(json::Decoder& dec);
};

// `struct{}` arms of generated unions.
struct EmptyObject {
	bool isZero() const { return true; }
	std::string marshalJSONTo(json::Encoder& enc) const;
	std::string unmarshalJSONFrom(json::Decoder& dec);
};

// ---------------------------------------------------------------------------
// AnyValue — Go `any` field holding either a raw json.Value (decode path) or
// a typed value with a marshal thunk (construction path).
// ---------------------------------------------------------------------------
struct AnyValue {
	json::Value raw;
	bool rawSet = false; // a decoded json.Value is set even when empty
	std::string (*marshal)(json::Encoder& enc, const void* p) = nullptr;
	std::shared_ptr<void> hold;
	std::string typeName;

	bool isSet() const { return rawSet || marshal != nullptr; }
	bool isZero() const { return !isSet(); }

	std::string marshalJSONTo(json::Encoder& enc) const;
	std::string unmarshalJSONFrom(json::Decoder& dec);

	static AnyValue ofRaw(json::Value v) {
		AnyValue r;
		r.raw = std::move(v);
		r.rawSet = true;
		r.typeName = "json.Value";
		return r;
	}
	template <class T>
	static AnyValue of(T v) {
		AnyValue r;
		r.hold = std::make_shared<T>(std::move(v));
		r.typeName = typeid(T).name();
		r.marshal = [](json::Encoder& enc, const void* p) -> std::string {
			return json::marshalEncode(enc, *static_cast<const T*>(p));
		};
		return r;
	}
	template <class T>
	static AnyValue of(const std::shared_ptr<T>& v) {
		AnyValue r;
		r.hold = v;
		r.typeName = typeid(T).name();
		r.marshal = [](json::Encoder& enc, const void* p) -> std::string {
			return json::marshalEncode(enc, *static_cast<const T*>(p));
		};
		return r;
	}
};

// ---------------------------------------------------------------------------
// goIsZero / derefOr — used by generated marshalers and resolve bodies.
// ---------------------------------------------------------------------------
template <class T>
concept IsZeroable = requires(const T& t) { { t.isZero() } -> std::convertible_to<bool>; };

template <class T>
bool goIsZero(const T& v) {
	if constexpr (IsZeroable<T>) {
		return v.isZero();
	} else {
		return v == T{};
	}
}
template <class T>
bool goIsZero(const std::shared_ptr<T>& v) { return !v; }
template <class T>
bool goIsZero(const std::optional<T>& v) { return !v.has_value(); }
template <class T>
bool goIsZero(const std::vector<T>& v) { return v.empty(); }
template <class K, class V>
bool goIsZero(const Map<K, V>& v) { return v.empty(); }
inline bool goIsZero(const std::string& v) { return v.empty(); } // covers json::Value
inline bool goIsZero(const LSPAny& v) { return v.isNil(); }
inline bool goIsZero(const AnyValue& v) { return v.isZero(); }
template <class T, size_t N>
bool goIsZero(const std::array<T, N>& v) {
	for (const auto& e : v) if (!goIsZero(e)) return false;
	return true;
}

template <class T>
T derefOr(const std::optional<T>& v) { return v ? *v : T{}; }
template <class T>
T derefOr(const std::shared_ptr<T>& v) { return v ? *v : T{}; }
inline bool derefOr(bool v) { return v; }

// ---------------------------------------------------------------------------
// structcodec.go — bound-field struct decoding. StructFieldBinding replaces
// Go's reflect-driven structFieldSpec; every generated struct produces one
// via fieldBindings().
// ---------------------------------------------------------------------------
[[noreturn]] void lsprotoPanic(std::string_view msg);
std::string errNotObject(json::Kind got);
std::string errNull(const std::string& field);
std::string errMissing(const std::vector<std::string>& missing);
std::string errInvalidKind(std::string_view typeName, json::Kind got);
std::string errInvalidValue(std::string_view typeName, const json::Value& data);
std::string errLiteralMismatch(std::string_view typeName, std::string_view expected, const json::Value& got);

// lsp.go:111-121 — assertOnlyOne/assertAtMostOne take the already-computed
// non-nil count.
inline void assertOnlyOne(const std::string& message, int count) {
	if (count != 1) lsprotoPanic(message);
}
inline void assertAtMostOne(const std::string& message, int count) {
	if (count > 1) lsprotoPanic(message);
}

// countNonNil — structcodec.go:316.
inline int countNonNull() { return 0; }
template <class T, class... Rest>
int countNonNull(const std::shared_ptr<T>& v, const Rest&... rest) {
	return (v != nullptr ? 1 : 0) + countNonNull(rest...);
}
template <class T, class... Rest>
int countNonNull(const std::optional<T>& v, const Rest&... rest) {
	return (v ? 1 : 0) + countNonNull(rest...);
}
inline int countNonNull(bool v) { return v ? 1 : 0; }
template <class... Rest>
int countNonNull(bool v, const Rest&... rest) {
	return (v ? 1 : 0) + countNonNull(rest...);
}

// ---------------------------------------------------------------------------
// lsp.go:124-185 — raw object key scanning helpers.
// ---------------------------------------------------------------------------
bool jsonKeyCheck(const json::Value& name, std::string_view key);
json::Value jsonObjectRawField(const json::Value& data, std::string_view field);
int jsonObjectHasKey(const json::Value& data, std::initializer_list<const char*> keys);

// valueKind — jsontext.Value.Kind(): '{' '[' '"' 'n' 't' 'f', '0' for numbers.
inline json::Kind valueKind(const json::Value& v) {
	if (v.empty()) return '\0';
	char c = v.front();
	if (c == '-' || (c >= '0' && c <= '9')) return '0';
	return c;
}

// ---------------------------------------------------------------------------
// util.go — cmp.Compare helper (used by generated code too).
// ---------------------------------------------------------------------------
template <class T>
int compareScalars(const T& a, const T& b) {
	if (a < b) return -1;
	if (a > b) return 1;
	return 0;
}

struct StructFieldBinding {
	std::string name;   // json tag name
	const void* field;  // member address bound to the struct instance
	std::string (*decode)(json::Decoder& dec, void* p);
	int requiredID = -1;
	bool rejectNull = false;
};

template <class T>
std::string fieldDecode(json::Decoder& dec, void* p) {
	return json::unmarshalDecode(dec, static_cast<T*>(p));
}

// StructDecoder — structcodec.go:35 structDecoder.
class StructDecoder {
public:
	StructDecoder(std::vector<StructFieldBinding> bindings)
		: storage_(std::move(bindings)) {
		for (const auto& b : storage_) {
			if (b.requiredID >= 0) {
				requiredMask_ |= 1ull << b.requiredID;
				if ((int)required_.size() <= b.requiredID)
					required_.resize(b.requiredID + 1);
				required_[b.requiredID] = &b;
			}
			byName_[b.name] = &b;
		}
	}

	const StructFieldBinding* binding(const std::string& name) const {
		auto it = byName_.find(name);
		return it == byName_.end() ? nullptr : it->second;
	}

	// field — structcodec.go:86. decode receives (binding, fieldAddr) when the
	// name is known, (nullptr, nullptr) otherwise.
	std::string field(const std::string& name, json::Kind kind,
		const std::function<std::string(const StructFieldBinding*, void*)>& decode) {
		const StructFieldBinding* fs = binding(name);
		if (fs == nullptr) {
			return decode(nullptr, nullptr);
		}
		if (fs->requiredID >= 0) {
			seen_ |= 1ull << fs->requiredID;
		}
		if (fs->rejectNull && kind == 'n') {
			return errNull(name);
		}
		return decode(fs, const_cast<void*>(fs->field));
	}

	// finish — structcodec.go:100.
	std::string finish();

private:
	std::vector<StructFieldBinding> storage_;
	std::unordered_map<std::string, const StructFieldBinding*> byName_;
	std::vector<const StructFieldBinding*> required_;
	uint64_t seen_ = 0;
	uint64_t requiredMask_ = 0;
};

// unmarshalStructGo — unmarshalStruct, structcodec.go:116: object kind check,
// required-field tracking, rejectNull, unknown-key skip.
std::string unmarshalStructGo(json::Decoder& dec, std::vector<StructFieldBinding> bindings);
// unmarshalStructReflectGo — plain encoding/json semantics for structs that
// never declared UnmarshalJSONFrom (no required/rejectNull handling).
std::string unmarshalStructReflectGo(json::Decoder& dec, std::initializer_list<StructFieldBinding> bindings);
std::string unmarshalStructReflectGo(json::Decoder& dec, std::vector<StructFieldBinding> bindings);

// ---------------------------------------------------------------------------
// structcodec.go:167-287 — discriminated union decoding.
// ---------------------------------------------------------------------------
struct deferredStructField {
	std::string name;
	json::Value value;
};

// discriminatedStructDecoder — structcodec.go:172.
struct discriminatedStructDecoder {
	json::Decoder* dec = nullptr;
	std::string typeName;
	std::string discriminator;
	json::Value discriminatorValue;
	bool hasDiscriminator = false;
	std::vector<deferredStructField> deferred;
	bool closed = false;

	std::string invalidDiscriminator() const;
};

std::pair<discriminatedStructDecoder, std::string>
scanDiscriminatedStruct(json::Decoder& dec, std::string_view typeName, std::string_view discriminator);

// unmarshalDiscriminatedArm — structcodec.go:235. `out` receives the decoded
// arm only after the full object parses successfully.
template <class T>
std::string unmarshalDiscriminatedArm(discriminatedStructDecoder& state, std::shared_ptr<T>* out) {
	T target{};
	StructDecoder targetDecoder{target.fieldBindings()};

	if (state.hasDiscriminator) {
		if (auto err = targetDecoder.field(state.discriminator,
				valueKind(state.discriminatorValue),
				[&](const StructFieldBinding* b, void* p) -> std::string {
					if (p == nullptr) return {};
					json::Decoder d(state.discriminatorValue);
					if (auto err = b->decode(d, p); !err.empty()) return err;
					if (!d.atEnd())
						return "json: unexpected bytes after top-level value";
					return {};
				}); !err.empty()) return err;
	}
	for (const auto& df : state.deferred) {
		if (auto err = targetDecoder.field(df.name, valueKind(df.value),
				[&](const StructFieldBinding* b, void* p) -> std::string {
					if (p == nullptr) return {};
					json::Decoder d(df.value);
					if (auto err = b->decode(d, p); !err.empty()) return err;
					if (!d.atEnd())
						return "json: unexpected bytes after top-level value";
					return {};
				}); !err.empty()) return err;
	}

	if (!state.closed) {
		while (state.dec->peekKind() != '}') {
			auto [rawName, err] = state.dec->readValue();
			if (!err.empty()) return err;
			std::string name = rawName.substr(1, rawName.size() - 2);
			if (auto err2 = targetDecoder.field(name, state.dec->peekKind(),
					[&](const StructFieldBinding* b, void* p) -> std::string {
						if (p == nullptr) return state.dec->skipValue();
						return b->decode(*state.dec, p);
					}); !err2.empty()) return err2;
		}
		if (auto [tok, err] = state.dec->readToken(); !err.empty()) return err;
	}
	if (auto err = targetDecoder.finish(); !err.empty()) return err;
	*out = std::make_shared<T>(std::move(target));
	return {};
}

// marshalUnion — structcodec.go:290.
std::string marshalUnionGo(json::Encoder& enc, const char* name, bool nullable,
	const std::vector<std::pair<bool, std::function<std::string(json::Encoder&)>>>& members);

template <class T>
std::pair<bool, std::function<std::string(json::Encoder&)>>
tryField(const std::shared_ptr<T>& p) {
	return {p != nullptr, [p](json::Encoder& enc) -> std::string {
		return json::marshalEncode(enc, p);
	}};
}

// ---------------------------------------------------------------------------
// lsp.go:87-122 — errors and asserts.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// jsonrpc.go — Message / RequestMessage / ResponseMessage.
// ---------------------------------------------------------------------------
struct RequestMessage;
struct ResponseMessage;

// Message — jsonrpc.go:19. Kind + the decoded request/response payload.
struct Message {
	jsonrpc::MessageKind Kind = jsonrpc::MessageKind::Notification;
	std::variant<std::monostate, std::shared_ptr<RequestMessage>,
	             std::shared_ptr<ResponseMessage>> msg;

	std::shared_ptr<RequestMessage> AsRequest() const {
		return std::get<std::shared_ptr<RequestMessage>>(msg);
	}
	std::shared_ptr<ResponseMessage> AsResponse() const {
		return std::get<std::shared_ptr<ResponseMessage>>(msg);
	}

	std::string unmarshalJSON(const json::Value& data);   // jsonrpc.go:32
	std::string marshalJSONTo(json::Encoder& enc) const;  // jsonrpc.go:76
	std::string unmarshalJSONFrom(json::Decoder& dec);    // stream-side shim
	bool isZero() const { return std::holds_alternative<std::monostate>(msg); }
};

// RequestMessage — jsonrpc.go:80.
struct RequestMessage {
	jsonrpc::JSONRPCVersion JSONRPC;
	std::shared_ptr<jsonrpc::ID> ID;   // `json:"id,omitzero"`
	Method Method;                     // `json:"method"`
	AnyValue Params;                   // `json:"params,omitzero"`

	// Message() — jsonrpc.go:87 (named `Message` in Go; `toMessage` here to
	// avoid colliding with the Message type name).
	std::shared_ptr<tsc::lsp::lsproto::Message> toMessage() const;
	template <class T>
	std::pair<T, Error> UnmarshalParams() const;                  // lsp.go:235
	std::string unmarshalJSON(const json::Value& data);          // jsonrpc.go:98
	std::string unmarshalJSONFrom(json::Decoder& dec);
	std::string marshalJSONTo(json::Encoder& enc) const;
	bool isZero() const;
};

// ResponseMessage — jsonrpc.go:118.
struct ResponseMessage {
	jsonrpc::JSONRPCVersion JSONRPC;
	std::shared_ptr<jsonrpc::ID> ID;               // `json:"id"`
	AnyValue Result;                               // `json:"result,omitzero"`
	std::shared_ptr<jsonrpc::ResponseError> Error; // `json:"error,omitzero"`

	std::shared_ptr<tsc::lsp::lsproto::Message> toMessage() const; // jsonrpc.go:125
	std::string marshalJSONTo(json::Encoder& enc) const;
	std::string unmarshalJSONFrom(json::Decoder& dec);
	bool isZero() const;
};

struct IntegerOrString; // generated union; fwd for NewID.

// NewID — jsonrpc.go:12.
std::shared_ptr<jsonrpc::ID> NewID(const IntegerOrString& rawValue);

// ---------------------------------------------------------------------------
// lsp.go:188-232 — RequestInfo / NotificationInfo service tables.
// ---------------------------------------------------------------------------
template <class Params, class Resp>
struct RequestInfo {
	Method Method;

	// UnmarshalResult — lsp.go:194.
	std::pair<Resp, Error> UnmarshalResult(const AnyValue& result) const {
		if (!result.rawSet) {
			return {Resp{}, gostd::errorf("expected json.Value, got %s",{
				result.isSet() ? result.typeName : "<nil>"})};
		}
		Resp r;
		if (auto err = json::unmarshal(result.raw, &r); !err.empty()) {
			return {Resp{}, gostd::newError(err)};
		}
		return {std::move(r), nullptr};
	}

	// NewRequestMessage — lsp.go:207.
	std::shared_ptr<RequestMessage> NewRequestMessage(
		std::shared_ptr<jsonrpc::ID> id, const Params& params) const {
		auto m = std::make_shared<RequestMessage>();
		m->ID = std::move(id);
		m->Method = Method;
		m->Params = AnyValue::of(params);
		return m;
	}
};

template <class Params>
struct NotificationInfo {
	Method Method;

	// NewNotificationMessage — lsp.go:220.
	std::shared_ptr<RequestMessage> NewNotificationMessage(const Params& params) const {
		auto m = std::make_shared<RequestMessage>();
		m->Method = Method;
		m->Params = AnyValue::of(params);
		return m;
	}
};

// ---------------------------------------------------------------------------
// lsp.go:287-299 — client-capabilities context plumbing.
// ---------------------------------------------------------------------------
struct ResolvedClientCapabilities;
gostd::Context withClientCapabilities(gostd::Context ctx,
	const std::shared_ptr<ResolvedClientCapabilities>& caps);
std::shared_ptr<ResolvedClientCapabilities> getClientCapabilities(gostd::Context ctx);

// ---------------------------------------------------------------------------
// baseproto.go — BaseReader / BaseWriter wrap the jsonrpc frame codecs.
// ---------------------------------------------------------------------------
struct BaseReader {
	std::unique_ptr<jsonrpc::Reader> Reader;
	std::pair<std::string, Error> Read() { return Reader->Read(); }
};
inline BaseReader NewBaseReader(gostd::io::Reader* r) {
	return {jsonrpc::NewReader(r)};
}
struct BaseWriter {
	std::unique_ptr<jsonrpc::Writer> Writer;
	Error Write(std::string_view data) { return Writer->Write(data); }
};
inline BaseWriter NewBaseWriter(gostd::io::Writer* w) {
	return {jsonrpc::NewWriter(w)};
}

// Bitwise ops for LSP flag enums (e.g. WatchKind). Defined generically over
// this namespace's enum types so ADL picks them up (Go enums are uint32-typed
// and freely combinable).
template <class E>
constexpr std::enable_if_t<std::is_enum_v<E>, E> operator|(E a, E b) {
	return static_cast<E>(static_cast<std::underlying_type_t<E>>(a) |
	                      static_cast<std::underlying_type_t<E>>(b));
}
template <class E>
constexpr std::enable_if_t<std::is_enum_v<E>, E> operator&(E a, E b) {
	return static_cast<E>(static_cast<std::underlying_type_t<E>>(a) &
	                      static_cast<std::underlying_type_t<E>>(b));
}
template <class E>
constexpr std::enable_if_t<std::is_enum_v<E>, E> operator~(E a) {
	return static_cast<E>(~static_cast<std::underlying_type_t<E>>(a));
}
template <class E>
constexpr std::enable_if_t<std::is_enum_v<E>, E&> operator|=(E& a, E b) {
	return a = a | b;
}
template <class E>
constexpr std::enable_if_t<std::is_enum_v<E>, E&> operator&=(E& a, E b) {
	return a = a & b;
}

} // namespace tsc::lsp::lsproto
