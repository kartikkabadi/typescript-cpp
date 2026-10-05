// Port of tsc/internal/jsonrpc/jsonrpc.go.
#include "internal/jsonrpc/jsonrpc.h"

namespace tsc::jsonrpc {

// ID.String — jsonrpc.go:55.
std::string ID::String() const {
	if (!str.empty()) {
		return str;
	}
	return std::to_string(int_);
}

// ID.MarshalJSON — jsonrpc.go:62.
std::string ID::marshalJSON() const {
	if (!str.empty()) {
		return json::marshalString(str);
	}
	return json::marshalInt64(int_);
}

// ID.UnmarshalJSON — jsonrpc.go:69.
gostd::Error ID::unmarshalJSON(const json::Value& data) {
	*this = ID{};
	if (!data.empty() && data[0] == '"') {
		auto [d, perr] = json::parse(data);
		if (perr) return perr;
		auto [s, serr] = json::asString(d, "string");
		if (serr) return serr;
		str = std::move(s);
		return nullptr;
	}
	auto [d, perr] = json::parse(data);
	if (perr) return perr;
	auto [n, nerr] = json::asInt32(d, "int32");
	if (nerr) return nerr;
	int_ = n;
	return nullptr;
}

// ID.TryInt — jsonrpc.go:77.
std::pair<int32_t, bool> ID::TryInt() const {
	if (!str.empty()) {
		return {0, false};
	}
	return {int_, true};
}

// ID.MustInt — jsonrpc.go:84.
int32_t ID::MustInt() const {
	if (!str.empty()) {
		TSC_UNREACHABLE("ID is not an integer");
	}
	return int_;
}

// ResponseError.String — jsonrpc.go:104. Go marshals Data and prints the
// partial output only when marshaling fails.
std::string ResponseError::String() const {
	// json.Marshal(nil) == "null"; an empty Data marshals fine.
	auto [data, err] = Data.empty() ? std::pair<std::string, gostd::Error>{"null", nullptr}
	                                : json::marshalRaw(Data);
	if (err != nullptr) {
		return gostd::sprintf("[%d]: %s\n%s", {Code, Message, data});
	}
	return gostd::sprintf("[%d]: %s", {Code, Message});
}

namespace {

void putOmitZero(std::vector<std::pair<std::string, std::string>>& members,
                 std::string_view name, const std::string& raw) {
	if (!raw.empty()) {
		members.emplace_back(name, raw);
	}
}

} // namespace

// marshalRequestMessage — json.Marshal(RequestMessage): field order jsonrpc,
// id, method, params with omitzero.
json::Value marshalRequestMessage(const RequestMessage& m) {
	std::vector<std::pair<std::string, std::string>> members;
	members.emplace_back("jsonrpc", "\"2.0\"");
	if (m.Id != nullptr) {
		members.emplace_back("id", m.Id->marshalJSON());
	}
	members.emplace_back("method", json::marshalString(m.Method));
	putOmitZero(members, "params", m.Params);
	return json::marshalObject(members);
}

// marshalResponseMessage — json.Marshal(ResponseMessage).
json::Value marshalResponseMessage(const ResponseMessage& m) {
	std::vector<std::pair<std::string, std::string>> members;
	members.emplace_back("jsonrpc", "\"2.0\"");
	if (m.Id != nullptr) {
		members.emplace_back("id", m.Id->marshalJSON());
	}
	putOmitZero(members, "result", m.Result);
	if (m.Error != nullptr) {
		members.emplace_back(
		    "error",
		    json::marshalObject({{"code", json::marshalInt64(m.Error->Code)},
		                         {"message", json::marshalString(m.Error->Message)}}));
	}
	return json::marshalObject(members);
}

// marshalMessage — json.Marshal(Message).
json::Value marshalMessage(const Message& m) {
	std::vector<std::pair<std::string, std::string>> members;
	members.emplace_back("jsonrpc", "\"2.0\"");
	if (m.Id.has_value()) {
		members.emplace_back("id", m.Id->marshalJSON());
	}
	if (!m.Method.empty()) {
		members.emplace_back("method", json::marshalString(m.Method));
	}
	putOmitZero(members, "params", m.Params);
	putOmitZero(members, "result", m.Result);
	if (m.Error.has_value()) {
		members.emplace_back(
		    "error",
		    json::marshalObject({{"code", json::marshalInt64(m.Error->Code)},
		                         {"message", json::marshalString(m.Error->Message)}}));
	}
	return json::marshalObject(members);
}

// unmarshalMessage — json.Unmarshal(Message).
std::pair<Message, gostd::Error> unmarshalMessage(const json::Value& data) {
	auto [d, err] = json::parse(data);
	if (err) {
		return {Message{}, err};
	}
	if (d.kind != json::Dom::K::Object) {
		return {Message{}, gostd::newError(
		                   "json: cannot unmarshal non-object into Message")};
	}
	Message m;
	if (const json::Dom* v = json::objGet(d, "id")) {
		ID id;
		if (auto e = id.unmarshalJSON(json::Value(v->raw))) {
			return {Message{}, e};
		}
		m.Id = id;
	}
	if (const json::Dom* v = json::objGet(d, "method")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return {Message{}, e};
		m.Method = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "params")) {
		m.Params = json::Value(v->raw);
	}
	if (const json::Dom* v = json::objGet(d, "result")) {
		m.Result = json::Value(v->raw);
	}
	if (const json::Dom* v = json::objGet(d, "error")) {
		auto [re, e] = unmarshalResponseError(json::Value(v->raw));
		if (e) return {Message{}, e};
		m.Error = std::move(re);
	}
	return {m, nullptr};
}

// unmarshalResponseError — json.Unmarshal(ResponseError).
std::pair<ResponseError, gostd::Error> unmarshalResponseError(
    const json::Value& data) {
	auto [d, err] = json::parse(data);
	if (err) {
		return {ResponseError{}, err};
	}
	ResponseError re;
	if (const json::Dom* v = json::objGet(d, "code")) {
		auto [n, e] = json::asInt32(*v, "int32");
		if (e) return {ResponseError{}, e};
		re.Code = n;
	}
	if (const json::Dom* v = json::objGet(d, "message")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return {ResponseError{}, e};
		re.Message = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "data")) {
		re.Data = json::Value(v->raw);
	}
	return {re, nullptr};
}

} // namespace tsc::jsonrpc
