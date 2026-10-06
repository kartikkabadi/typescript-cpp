// ============================================================================
// lsproto.cpp — hand-ported parts of the lsproto slice:
//   baseproto.go + jsonrpc.go + lsp.go + structcodec.go + util.go.
// (Everything except the generated tables in lsproto_generated.cpp.)
// ============================================================================

#include "internal/lsp/lsproto/lsproto.h"

#include <cstdlib>
#include <sstream>

#include "internal/bundled/bundled.h"

namespace tsc::lsp::lsproto {

namespace {

[[noreturn]] void panicImpl(const std::string& msg) {
	fprintf(stderr, "lsproto panic: %s\n", msg.c_str());
	abort();
}

} // namespace

[[noreturn]] void lsprotoPanic(std::string_view msg) {
	panicImpl(std::string(msg));
}

// ---------------------------------------------------------------------------
// lsp.go:87-109 — error constructors.
// ---------------------------------------------------------------------------

std::string errNotObject(json::Kind got) {
	return gostd::errorf("expected object start, but encountered %v",{ std::string(1, got)})
		->Error();
}

std::string errNull(const std::string& field) {
	return gostd::errorf("null value is not allowed for field \"%s\"",{ field})
		->Error();
}

std::string errMissing(const std::vector<std::string>& missing) {
	std::string joined;
	for (size_t i = 0; i < missing.size(); i++) {
		if (i) joined += ", ";
		joined += missing[i];
	}
	return gostd::errorf("missing required properties: %s",{ joined})->Error();
}

std::string errInvalidKind(std::string_view typeName, json::Kind got) {
	return gostd::errorf("invalid %s: got %v",{ std::string(typeName),
	                     std::string(1, got)})
		->Error();
}

std::string errInvalidValue(std::string_view typeName, const json::Value& data) {
	return gostd::errorf("invalid %s: %s",{ std::string(typeName),
	                     std::string_view(data)})
		->Error();
}

std::string errLiteralMismatch(std::string_view typeName, std::string_view expected,
                               const json::Value& got) {
	return gostd::errorf("expected %s value %s, got %s",{ std::string(typeName),
	                     std::string(expected), std::string_view(got)})
		->Error();
}

// ---------------------------------------------------------------------------
// StructDecoder::finish — structcodec.go:100.
// ---------------------------------------------------------------------------

std::string StructDecoder::finish() {
	uint64_t missing = requiredMask_ & ~seen_;
	if (missing != 0) {
		std::vector<std::string> missingProps;
		for (size_t id = 0; id < required_.size(); id++) {
			if (required_[id] && (missing & (1ull << id)) != 0) {
				missingProps.push_back(required_[id]->name);
			}
		}
		return errMissing(missingProps);
	}
	return {};
}

// ---------------------------------------------------------------------------
// unmarshalStruct — structcodec.go:116.
// ---------------------------------------------------------------------------

std::string unmarshalStructGo(json::Decoder& dec,
                              std::vector<StructFieldBinding> bindings) {
	StructDecoder d{std::move(bindings)};
	if (json::Kind k = dec.peekKind(); k != '{') {
		return errNotObject(k);
	}
	if (auto [tok, err] = dec.readToken(); !err.empty()) return err;
	while (dec.peekKind() != '}') {
		auto [name, err] = dec.readValue();
		if (!err.empty()) return err;
		if (auto err2 = d.field(name.substr(1, name.size() - 2), dec.peekKind(),
				[&](const StructFieldBinding* b, void* p) -> std::string {
					if (p == nullptr) {
						return dec.skipValue();
					}
					return b->decode(dec, p);
				});
			!err2.empty())
			return err2;
	}
	if (auto [tok, err] = dec.readToken(); !err.empty()) return err;
	return d.finish();
}

// unmarshalStructReflectGo — plain encoding/json semantics: members are
// assigned when present; required/rejectNull are reflect-invisible; null is
// a no-op for the struct value itself.
std::string unmarshalStructReflectGo(json::Decoder& dec,
                                     std::initializer_list<StructFieldBinding> bindings) {
	return unmarshalStructReflectGo(dec, std::vector<StructFieldBinding>(bindings));
}

std::string unmarshalStructReflectGo(json::Decoder& dec,
                                     std::vector<StructFieldBinding> bindings) {
	if (json::Kind k = dec.peekKind(); k == 'n') {
		if (auto [tok, err] = dec.readToken(); !err.empty()) return err;
		return {};
	}
	if (dec.peekKind() != '{') {
		if (auto err = dec.skipValue(); !err.empty()) return err;
		return "json: cannot unmarshal non-object into Go struct value";
	}
	if (auto [tok, err] = dec.readToken(); !err.empty()) return err;
	std::unordered_map<std::string, const StructFieldBinding*> byName;
	for (const auto& b : bindings) byName[b.name] = &b;
	while (dec.peekKind() != '}') {
		auto [name, err] = dec.readValue();
		if (!err.empty()) return err;
		auto it = byName.find(name.substr(1, name.size() - 2));
		if (it == byName.end()) {
			if (auto err2 = dec.skipValue(); !err2.empty()) return err2;
			continue;
		}
		if (auto err2 = it->second->decode(dec, const_cast<void*>(it->second->field));
			!err2.empty())
			return err2;
	}
	if (auto [tok, err] = dec.readToken(); !err.empty()) return err;
	return {};
}

// ---------------------------------------------------------------------------
// scanDiscriminatedStruct — structcodec.go:184.
// ---------------------------------------------------------------------------

std::pair<discriminatedStructDecoder, std::string>
scanDiscriminatedStruct(json::Decoder& dec, std::string_view typeName,
                        std::string_view discriminator) {
	if (json::Kind k = dec.peekKind(); k != '{') {
		return {discriminatedStructDecoder{}, errNotObject(k)};
	}
	if (auto [tok, err] = dec.readToken(); !err.empty()) {
		return {discriminatedStructDecoder{}, err};
	}
	discriminatedStructDecoder state;
	state.dec = &dec;
	state.typeName = typeName;
	state.discriminator = discriminator;
	while (dec.peekKind() != '}') {
		auto [rawName, err] = dec.readValue();
		if (!err.empty()) return {discriminatedStructDecoder{}, err};
		std::string name = rawName.substr(1, rawName.size() - 2);
		if (name == discriminator) {
			auto [value, err2] = dec.readValue();
			if (!err2.empty()) return {discriminatedStructDecoder{}, err2};
			state.discriminatorValue = std::move(value);
			state.hasDiscriminator = true;
			return {std::move(state), {}};
		}
		auto [value, err2] = dec.readValue();
		if (!err2.empty()) return {discriminatedStructDecoder{}, err2};
		state.deferred.push_back({std::move(name), std::move(value)});
	}
	if (auto [tok, err] = dec.readToken(); !err.empty()) {
		return {discriminatedStructDecoder{}, err};
	}
	state.closed = true;
	return {std::move(state), {}};
}

// invalidDiscriminator — structcodec.go:226.
std::string discriminatedStructDecoder::invalidDiscriminator() const {
	if (!hasDiscriminator) {
		return gostd::errorf("invalid %s: missing discriminator \"%s\"",{
		                     typeName, discriminator})
			->Error();
	}
	return gostd::errorf("invalid %s discriminator \"%s\": %s",{ typeName,
	                     discriminator, std::string_view(discriminatorValue)})
		->Error();
}

// ---------------------------------------------------------------------------
// marshalUnion — structcodec.go:290.
// ---------------------------------------------------------------------------

std::string marshalUnionGo(json::Encoder& enc, const char* name, bool nullable,
	const std::vector<std::pair<bool, std::function<std::string(json::Encoder&)>>>& members) {
	const std::function<std::string(json::Encoder&)>* set = nullptr;
	int count = 0;
	for (auto& [ok, m] : members) {
		if (ok) {
			count++;
			if (set == nullptr) set = &m;
		}
	}
	if (nullable) {
		assertAtMostOne(std::string("more than one element of ") + name + " is set", count);
		if (set == nullptr) {
			return enc.writeToken(json::Null);
		}
	} else {
		assertOnlyOne(std::string("exactly one element of ") + name + " should be set", count);
	}
	return (*set)(enc);
}

// ---------------------------------------------------------------------------
// lsp.go:124-185 — raw key scanning helpers.
// ---------------------------------------------------------------------------

bool jsonKeyCheck(const json::Value& name, std::string_view key) {
	return name.size() == key.size() + 2 && name.front() == '"' &&
	       name.substr(1, key.size()) == key;
}

json::Value jsonObjectRawField(const json::Value& data, std::string_view field) {
	json::Decoder dec(data);
	if (dec.peekKind() != '{') return {};
	if (auto [tok, err] = dec.readToken(); !err.empty()) return {};
	while (dec.peekKind() != '}') {
		auto [name, err] = dec.readValue();
		if (!err.empty()) return {};
		if (jsonKeyCheck(name, field)) {
			auto [value, err2] = dec.readValue();
			if (!err2.empty()) return {};
			return value;
		}
		if (auto err2 = dec.skipValue(); !err2.empty()) return {};
	}
	return {};
}

int jsonObjectHasKey(const json::Value& data, std::initializer_list<const char*> keys) {
	json::Decoder dec(data);
	if (dec.peekKind() != '{') return -1;
	if (auto [tok, err] = dec.readToken(); !err.empty()) return -1;
	while (dec.peekKind() != '}') {
		auto [name, err] = dec.readValue();
		if (!err.empty()) return -1;
		int i = 0;
		for (const char* key : keys) {
			if (jsonKeyCheck(name, key)) return i;
			i++;
		}
		if (auto err2 = dec.skipValue(); !err2.empty()) return -1;
	}
	return -1;
}

// ---------------------------------------------------------------------------
// LSPAny — Go `any` round trip.
// ---------------------------------------------------------------------------

std::string LSPAny::marshalJSONTo(json::Encoder& enc) const {
	switch (kind) {
	case K::Nil:
		return enc.writeToken(json::Null);
	case K::Bool:
		return enc.writeToken(json::tokenBool(b));
	case K::Int:
		return enc.writeToken(json::tokenInt(i));
	case K::Float:
		return enc.writeToken(json::tokenFloat(f));
	case K::String:
		return enc.writeToken(json::tokenString(s));
	case K::Array: {
		if (auto err = enc.writeToken(json::BeginArray); !err.empty()) return err;
		for (const LSPAny& e : arr) {
			if (auto err = e.marshalJSONTo(enc); !err.empty()) return err;
		}
		return enc.writeToken(json::EndArray);
	}
	case K::Object: {
		if (auto err = enc.writeToken(json::BeginObject); !err.empty()) return err;
		for (auto& [name, v] : obj) {
			if (auto err = enc.writeToken(json::tokenString(name)); !err.empty()) return err;
			if (auto err = v.marshalJSONTo(enc); !err.empty()) return err;
		}
		return enc.writeToken(json::EndObject);
	}
	}
	return {};
}

// unmarshalJSONFrom — decodes like Go's encoding/json into `any`.
std::string LSPAny::unmarshalJSONFrom(json::Decoder& dec) {
	switch (dec.peekKind()) {
	case 'n': {
		auto [tok, err] = dec.readToken();
		if (!err.empty()) return err;
		kind = K::Nil;
		return {};
	}
	case 't':
	case 'f': {
		auto [tok, err] = dec.readToken();
		if (!err.empty()) return err;
		kind = K::Bool;
		b = tok.string() == "true";
		return {};
	}
	case '0': {
		auto [tok, err] = dec.readToken();
		if (!err.empty()) return err;
		kind = K::Float;
		f = std::strtod(tok.string().c_str(), nullptr);
		return {};
	}
	case '"': {
		auto [tok, err] = dec.readToken();
		if (!err.empty()) return err;
		kind = K::String;
		s = tok.string();
		return {};
	}
	case '[': {
		if (auto [tok, err] = dec.readToken(); !err.empty()) return err;
		kind = K::Array;
		arr.clear();
		while (dec.peekKind() != ']') {
			LSPAny e;
			if (auto err = e.unmarshalJSONFrom(dec); !err.empty()) return err;
			arr.push_back(std::move(e));
		}
		auto [tok, err] = dec.readToken();
		return err;
	}
	case '{': {
		if (auto [tok, err] = dec.readToken(); !err.empty()) return err;
		kind = K::Object;
		obj.clear();
		while (dec.peekKind() != '}') {
			auto [nameTok, err] = dec.readToken();
			if (!err.empty()) return err;
			LSPAny v;
			if (auto err = v.unmarshalJSONFrom(dec); !err.empty()) return err;
			obj[nameTok.string()] = std::move(v);
		}
		auto [tok, err] = dec.readToken();
		return err;
	}
	}
	return "json: unexpected token kind";
}

// ---------------------------------------------------------------------------
// AnyValue — `any` field.
// ---------------------------------------------------------------------------

std::string AnyValue::marshalJSONTo(json::Encoder& enc) const {
	if (rawSet) {
		return enc.writeValue(raw.empty() ? json::Value("null") : raw);
	}
	if (marshal != nullptr) {
		return marshal(enc, hold.get());
	}
	return enc.writeToken(json::Null);
}

std::string AnyValue::unmarshalJSONFrom(json::Decoder& dec) {
	auto [v, err] = dec.readValue();
	if (!err.empty()) return err;
	raw = std::move(v);
	rawSet = true;
	marshal = nullptr;
	hold.reset();
	return {};
}

// ---------------------------------------------------------------------------
// Null / NoParams — lsp.go:266-285.
// ---------------------------------------------------------------------------

std::string Null::marshalJSONTo(json::Encoder& enc) const {
	return enc.writeToken(json::Null);
}

std::string Null::unmarshalJSONFrom(json::Decoder& dec) {
	auto [data, err] = dec.readValue();
	if (!err.empty()) return err;
	if (data != "null") {
		return gostd::errorf("expected null, got %s",{ std::string_view(data)})->Error();
	}
	return {};
}

std::string NoParams::marshalJSONTo(json::Encoder& enc) const {
	if (auto err = enc.writeToken(json::BeginObject); !err.empty()) return err;
	return enc.writeToken(json::EndObject);
}

std::string NoParams::unmarshalJSONFrom(json::Decoder& dec) {
	return unmarshalStructReflectGo(dec, {});
}

// `struct{}` — marshals as {}; unmarshals any object (members skipped, as
// encoding/json does for a struct with no fields).
std::string EmptyObject::marshalJSONTo(json::Encoder& enc) const {
	if (auto err = enc.writeToken(json::BeginObject); !err.empty()) return err;
	return enc.writeToken(json::EndObject);
}

std::string EmptyObject::unmarshalJSONFrom(json::Decoder& dec) {
	if (dec.peekKind() != '{') {
		return "json: cannot unmarshal non-object into Go value of type struct {}";
	}
	if (auto [tok, err] = dec.readToken(); !err.empty()) return err;
	while (dec.peekKind() != '}') {
		if (auto [tok, err] = dec.readToken(); !err.empty()) return err; // name
		if (auto err = dec.skipValue(); !err.empty()) return err;
	}
	auto [tok, err] = dec.readToken();
	return err;
}

// ---------------------------------------------------------------------------
// DocumentUri — lsp.go:19-57.
// ---------------------------------------------------------------------------

namespace {

std::string fixWindowsURIPath(const std::string& path) {
	if (path.starts_with('/')) {
		std::string_view rest = std::string_view(path).substr(1);
		if (rest.size() >= 2 && tspath::isVolumeCharacter(rest[0]) && rest[1] == ':') {
			return std::string(rest);
		}
	}
	return path;
}

// urlDecode — url.Parse's unescaping of the path component.
std::string urlDecode(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '%' && i + 2 < s.size()) {
			auto hex = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
			int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
			if (hi < 0 || lo < 0) {
				lsprotoPanic("invalid URI escape");
			}
			out += (char)((hi << 4) | lo);
			i += 2;
		} else {
			out += s[i];
		}
	}
	return out;
}

} // namespace

// FileName — lsp.go:19.
std::string documentUriFileName(DocumentUri uri) {
	if (bundled::IsBundled(uri)) {
		return uri;
	}
	if (uri.starts_with("file://")) {
		// url.Parse(u).Host / .Path — minimal file-URI parse.
		std::string_view rest = std::string_view(uri).substr(7);
		std::string_view host, rawPath;
		auto slash = rest.find('/');
		if (slash == std::string_view::npos) {
			host = rest;
			rawPath = "";
		} else {
			host = rest.substr(0, slash);
			rawPath = rest.substr(slash);
		}
		if (!host.empty()) {
			return "//" + std::string(host) + urlDecode(rawPath);
		}
		return fixWindowsURIPath(urlDecode(rawPath));
	}

	// Leave all other URIs escaped so we can round-trip them.
	auto colon = uri.find(':');
	if (colon == std::string::npos) {
		lsprotoPanic(gostd::errorf("invalid URI: %s",{ uri})->Error());
	}
	std::string scheme = uri.substr(0, colon);
	std::string path = uri.substr(colon + 1);

	std::string authority = "ts-nul-authority";
	if (path.starts_with("//")) {
		std::string_view rest2 = std::string_view(path).substr(2);
		auto sl = rest2.find('/');
		if (sl == std::string_view::npos) {
			lsprotoPanic(gostd::errorf("invalid URI: %s",{ uri})->Error());
		}
		authority = std::string(rest2.substr(0, sl));
		path = std::string(rest2.substr(sl + 1));
	}
	return "^/" + scheme + "/" + authority + "/" + path;
}

// Path — lsp.go:52.
tspath::Path documentUriPath(DocumentUri uri, bool useCaseSensitiveFileNames) {
	return tspath::toPath(documentUriFileName(uri), "", useCaseSensitiveFileNames);
}

// ---------------------------------------------------------------------------
// jsonrpc.go — Message.
// ---------------------------------------------------------------------------

namespace {

// messageRaw — jsonrpc.go:33 (raw decode struct).
struct messageRaw {
	jsonrpc::JSONRPCVersion JSONRPC;
	Method Method;
	std::shared_ptr<jsonrpc::ID> ID;
	json::Value Params;
	json::Value Result;
	std::shared_ptr<jsonrpc::ResponseError> Error;

	std::string unmarshalJSONFrom(json::Decoder& dec) {
		return unmarshalStructReflectGo(dec, {
			{"jsonrpc", &JSONRPC, &fieldDecode<jsonrpc::JSONRPCVersion>, -1, false},
			{"method", &Method, &fieldDecode<std::string>, -1, false},
			{"id", &ID, &fieldDecode<std::shared_ptr<jsonrpc::ID>>, -1, false},
			{"params", &Params, &fieldDecode<json::Value>, -1, false},
			{"result", &Result, &fieldDecode<json::Value>, -1, false},
			{"error", &Error, &fieldDecode<std::shared_ptr<jsonrpc::ResponseError>>, -1, false},
		});
	}
};

// requestRaw — jsonrpc.go:99 (raw decode struct for RequestMessage).
struct requestRaw {
	jsonrpc::JSONRPCVersion JSONRPC;
	std::shared_ptr<jsonrpc::ID> ID;
	Method Method;
	json::Value Params;

	std::string unmarshalJSONFrom(json::Decoder& dec) {
		return unmarshalStructReflectGo(dec, {
			{"jsonrpc", &JSONRPC, &fieldDecode<jsonrpc::JSONRPCVersion>, -1, false},
			{"id", &ID, &fieldDecode<std::shared_ptr<jsonrpc::ID>>, -1, false},
			{"method", &Method, &fieldDecode<std::string>, -1, false},
			{"params", &Params, &fieldDecode<json::Value>, -1, false},
		});
	}
};

} // namespace

// Message.UnmarshalJSON — jsonrpc.go:32.
std::string Message::unmarshalJSON(const json::Value& data) {
	messageRaw raw;
	if (auto err = json::unmarshal(data, &raw); !err.empty()) {
		return gostd::errorf("%w: %s",{ errorCodeErr(ErrorCode::InvalidRequest), err})
			->Error();
	}
	if (raw.ID != nullptr && raw.Method.empty()) {
		Kind = jsonrpc::MessageKind::Response;
		auto rm = std::make_shared<ResponseMessage>();
		rm->ID = std::move(raw.ID);
		rm->Result = AnyValue::ofRaw(std::move(raw.Result));
		rm->Error = std::move(raw.Error);
		msg = std::move(rm);
		return {};
	}

	AnyValue params;
	if (!raw.Params.empty()) {
		params = AnyValue::ofRaw(std::move(raw.Params));
	}

	if (raw.ID == nullptr) {
		Kind = jsonrpc::MessageKind::Notification;
	} else {
		Kind = jsonrpc::MessageKind::Request;
	}

	auto req = std::make_shared<RequestMessage>();
	req->ID = std::move(raw.ID);
	req->Method = std::move(raw.Method);
	req->Params = std::move(params);
	msg = std::move(req);
	return {};
}

// Message.MarshalJSON — jsonrpc.go:76.
std::string Message::marshalJSONTo(json::Encoder& enc) const {
	if (auto* req = std::get_if<std::shared_ptr<RequestMessage>>(&msg)) {
		return json::marshalEncode(enc, **req);
	}
	if (auto* resp = std::get_if<std::shared_ptr<ResponseMessage>>(&msg)) {
		return json::marshalEncode(enc, **resp);
	}
	return enc.writeToken(json::Null);
}

std::string Message::unmarshalJSONFrom(json::Decoder& dec) {
	auto [v, err] = dec.readValue();
	if (!err.empty()) return err;
	return unmarshalJSON(v);
}

// RequestMessage.UnmarshalJSON — jsonrpc.go:98.
std::string RequestMessage::unmarshalJSON(const json::Value& data) {
	requestRaw raw;
	if (auto err = json::unmarshal(data, &raw); !err.empty()) {
		return gostd::errorf("%w: %s",{ errorCodeErr(ErrorCode::InvalidRequest), err})
			->Error();
	}
	ID = std::move(raw.ID);
	Method = std::move(raw.Method);
	if (!raw.Params.empty()) {
		Params = AnyValue::ofRaw(std::move(raw.Params));
	}
	return {};
}

std::string RequestMessage::unmarshalJSONFrom(json::Decoder& dec) {
	auto [v, err] = dec.readValue();
	if (!err.empty()) return err;
	return unmarshalJSON(v);
}

// RequestMessage marshal — reflection order: jsonrpc, id (omitzero), method,
// params (omitzero).
std::string RequestMessage::marshalJSONTo(json::Encoder& enc) const {
	if (auto err = enc.writeToken(json::BeginObject); !err.empty()) return err;
	if (auto err = enc.writeValue("\"jsonrpc\""); !err.empty()) return err;
	if (auto err = json::marshalEncode(enc, JSONRPC); !err.empty()) return err;
	if (!goIsZero(ID)) {
		if (auto err = enc.writeValue("\"id\""); !err.empty()) return err;
		if (auto err = json::marshalEncode(enc, ID); !err.empty()) return err;
	}
	if (auto err = enc.writeValue("\"method\""); !err.empty()) return err;
	if (auto err = json::marshalEncode(enc, Method); !err.empty()) return err;
	if (!goIsZero(Params)) {
		if (auto err = enc.writeValue("\"params\""); !err.empty()) return err;
		if (auto err = json::marshalEncode(enc, Params); !err.empty()) return err;
	}
	return enc.writeToken(json::EndObject);
}

bool RequestMessage::isZero() const {
	return goIsZero(JSONRPC) && goIsZero(ID) && goIsZero(Method) && goIsZero(Params);
}

// ResponseMessage marshal — reflection order: jsonrpc, id, result (omitzero),
// error (omitzero).
std::string ResponseMessage::marshalJSONTo(json::Encoder& enc) const {
	if (auto err = enc.writeToken(json::BeginObject); !err.empty()) return err;
	if (auto err = enc.writeValue("\"jsonrpc\""); !err.empty()) return err;
	if (auto err = json::marshalEncode(enc, JSONRPC); !err.empty()) return err;
	if (auto err = enc.writeValue("\"id\""); !err.empty()) return err;
	if (auto err = json::marshalEncode(enc, ID); !err.empty()) return err;
	if (!goIsZero(Result)) {
		if (auto err = enc.writeValue("\"result\""); !err.empty()) return err;
		if (auto err = json::marshalEncode(enc, Result); !err.empty()) return err;
	}
	if (!goIsZero(Error)) {
		if (auto err = enc.writeValue("\"error\""); !err.empty()) return err;
		if (auto err = json::marshalEncode(enc, Error); !err.empty()) return err;
	}
	return enc.writeToken(json::EndObject);
}

std::string ResponseMessage::unmarshalJSONFrom(json::Decoder& dec) {
	return unmarshalStructReflectGo(dec, {
		{"jsonrpc", &JSONRPC, &fieldDecode<jsonrpc::JSONRPCVersion>, -1, false},
		{"id", &ID, &fieldDecode<std::shared_ptr<jsonrpc::ID>>, -1, false},
		{"result", &Result, &fieldDecode<AnyValue>, -1, false},
		{"error", &Error, &fieldDecode<std::shared_ptr<jsonrpc::ResponseError>>, -1, false},
	});
}

bool ResponseMessage::isZero() const {
	return goIsZero(JSONRPC) && goIsZero(ID) && goIsZero(Result) && goIsZero(Error);
}

// RequestMessage.Message / ResponseMessage.Message — jsonrpc.go:87,125.
std::shared_ptr<Message> RequestMessage::toMessage() const {
	auto kind = jsonrpc::MessageKind::Request;
	if (ID == nullptr) {
		kind = jsonrpc::MessageKind::Notification;
	}
	auto m = std::make_shared<tsc::lsp::lsproto::Message>();
	m->Kind = kind;
	m->msg = std::make_shared<RequestMessage>(*this);
	return m;
}

std::shared_ptr<Message> ResponseMessage::toMessage() const {
	auto m = std::make_shared<tsc::lsp::lsproto::Message>();
	m->Kind = jsonrpc::MessageKind::Response;
	m->msg = std::make_shared<ResponseMessage>(*this);
	return m;
}

// NewID — jsonrpc.go:12.
std::shared_ptr<jsonrpc::ID> NewID(const IntegerOrString& rawValue) {
	if (rawValue.String != nullptr) {
		return std::make_shared<jsonrpc::ID>(jsonrpc::NewIDString(*rawValue.String));
	}
	return std::make_shared<jsonrpc::ID>(jsonrpc::NewIDInt(*rawValue.Integer));
}

// ---------------------------------------------------------------------------
// lsp.go:287-299 — client capabilities context plumbing.
// ---------------------------------------------------------------------------

namespace {
const std::string kClientCapabilitiesKey = "lsproto.clientCapabilities";
} // namespace

gostd::Context withClientCapabilities(gostd::Context ctx,
	const std::shared_ptr<ResolvedClientCapabilities>& caps) {
	return gostd::contextWithValue(std::move(ctx), kClientCapabilitiesKey, caps);
}

std::shared_ptr<ResolvedClientCapabilities> getClientCapabilities(gostd::Context ctx) {
	if (auto v = gostd::ctxValue(ctx, kClientCapabilitiesKey)) {
		if (auto caps = std::static_pointer_cast<ResolvedClientCapabilities>(v);
			caps != nullptr) {
			return caps;
		}
	}
	return std::make_shared<ResolvedClientCapabilities>();
}

// ---------------------------------------------------------------------------
// lsp.go:302 — PreferredMarkupKind.
// ---------------------------------------------------------------------------

MarkupKind PreferredMarkupKind(const Slice<MarkupKind>& formats) {
	if (formats && !formats->empty()) {
		return formats->front();
	}
	return MarkupKindPlainText;
}

// ---------------------------------------------------------------------------
// lsp.go:310-320 — CodeActionKind helpers (TS-derived constants).
// ---------------------------------------------------------------------------

const CodeActionKind CodeActionKindSourceFixAllTs =
	CodeActionKindSourceFixAll + ".ts";
const CodeActionKind CodeActionKindSourceOrganizeImportsTs =
	CodeActionKindSourceOrganizeImports + ".ts";
const CodeActionKind CodeActionKindSourceRemoveUnusedImportsTs =
	CodeActionKindSource + ".removeUnusedImports.ts";
const CodeActionKind CodeActionKindSourceSortImportsTs =
	CodeActionKindSource + ".sortImports.ts";

// Contains — lsp.go:310.
bool codeActionKindContains(const CodeActionKind& kind, const CodeActionKind& other) {
	return kind == other || kind == CodeActionKindEmpty ||
	       other.starts_with(kind + ".");
}

// ---------------------------------------------------------------------------
// util.go — comparisons + diagnostic helpers.
// ---------------------------------------------------------------------------

// ComparePositions — util.go:11.
int ComparePositions(const Position& pos, const Position& other) {
	if (int lineComp = compareScalars(pos.Line, other.Line); lineComp != 0) {
		return lineComp;
	}
	return compareScalars(pos.Character, other.Character);
}

// CompareRanges — util.go:17.
int CompareRanges(const Range& lsRange, const Range& other) {
	if (int startComp = ComparePositions(lsRange.Start, other.Start); startComp != 0) {
		return startComp;
	}
	return ComparePositions(lsRange.End, other.End);
}

// StringOrMarkupContent.AsString — util.go:30.
std::string StringOrMarkupContent::AsString() const {
	if (String != nullptr) {
		return *String;
	}
	if (MarkupContent != nullptr) {
		return MarkupContent->Value;
	}
	return "";
}

// IntegerOrString.AsString — util.go:40.
std::string IntegerOrString::AsString() const {
	if (String != nullptr) {
		return *String;
	}
	if (Integer != nullptr) {
		return std::to_string(*Integer);
	}
	return "-1";
}

// diagnostic helpers — util.go:52-96.
namespace {

bool diagnosticExistsInSlice(const Diagnostic* elem,
                             const std::vector<std::shared_ptr<Diagnostic>>& diags) {
	for (const auto& diag : diags) {
		if (diagnosticsEqual(elem, diag.get())) return true;
	}
	return false;
}

bool diagnosticCodesEqual(const IntegerOrString* code1, const IntegerOrString* code2) {
	if (code1->String != nullptr && code2->String != nullptr) {
		return *code1->String == *code2->String;
	}
	if (code1->Integer != nullptr && code2->Integer != nullptr) {
		return *code1->Integer == *code2->Integer;
	}
	return false;
}

bool diagnosticMessagesEqual(const StringOrMarkupContent& m1,
                             const StringOrMarkupContent& m2) {
	if (m1.String != nullptr && m2.String != nullptr) {
		return *m1.String == *m2.String;
	}
	if (m1.MarkupContent != nullptr && m2.MarkupContent != nullptr) {
		return m1.MarkupContent->Kind == m2.MarkupContent->Kind &&
		       m1.MarkupContent->Value == m2.MarkupContent->Value;
	}
	return false;
}

} // namespace

bool diagnosticsEqual(const Diagnostic* diag1, const Diagnostic* diag2) {
	if (diagnosticCodesEqual(diag1->Code.get(), diag2->Code.get()) &&
	    diagnosticMessagesEqual(diag1->Message, diag2->Message) &&
	    CompareRanges(diag1->Range, diag2->Range) == 0) {
		return true;
	}
	return false;
}

// CompareDiagnostics — util.go:82.
std::pair<std::vector<std::shared_ptr<Diagnostic>>, std::vector<std::shared_ptr<Diagnostic>>>
CompareDiagnostics(const std::vector<std::shared_ptr<Diagnostic>>& list1,
                   const std::vector<std::shared_ptr<Diagnostic>>& list2) {
	std::vector<std::shared_ptr<Diagnostic>> missingFromList1;
	std::vector<std::shared_ptr<Diagnostic>> missingFromList2;
	for (const auto& elem : list1) {
		if (!diagnosticExistsInSlice(elem.get(), list2)) {
			missingFromList2.push_back(elem);
		}
	}
	for (const auto& elem : list2) {
		if (!diagnosticExistsInSlice(elem.get(), list1)) {
			missingFromList1.push_back(elem);
		}
	}
	return {std::move(missingFromList1), std::move(missingFromList2)};
}

// Diagnostic.AsString — util.go:98.
std::string Diagnostic::AsString() const {
	return gostd::sprintf("%s (%v:%v-%v:%v): %s", {Code->AsString(),
	                      static_cast<int64_t>(Range.Start.Line),
	                      static_cast<int64_t>(Range.Start.Character),
	                      static_cast<int64_t>(Range.End.Line),
	                      static_cast<int64_t>(Range.End.Character),
	                      Message.AsString()});
}

// Diagnostic.CodeAsString — util.go:102.
std::string Diagnostic::CodeAsString() const {
	return gostd::sprintf("Code(%s)", {Code->AsString()});
}

} // namespace tsc::lsp::lsproto
