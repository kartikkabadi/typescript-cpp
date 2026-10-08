// Port of tsc/internal/jsonrpc/jsonrpc.go + baseproto.go.
#include "internal/jsonrpc/jsonrpc.h"

#include <algorithm>
#include <charconv>
#include <cstring>

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

void putOmitZero(std::vector<std::pair<std::string, json::Value>>& members,
                 std::string_view name, const std::string& raw) {
	if (!raw.empty()) {
		members.emplace_back(name, raw);
	}
}

} // namespace

// marshalRequestMessage — json.Marshal(RequestMessage): field order jsonrpc,
// id, method, params with omitzero.
json::Value marshalRequestMessage(const RequestMessage& m) {
	std::vector<std::pair<std::string, json::Value>> members;
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
	std::vector<std::pair<std::string, json::Value>> members;
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
	std::vector<std::pair<std::string, json::Value>> members;
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
	// JSONRPCVersion.UnmarshalJSON — jsonrpc.go:27: any present "jsonrpc"
	// member other than exactly "2.0" is an error.
	if (const json::Dom* v = json::objGet(d, "jsonrpc")) {
		if (v->raw != "\"2.0\"") {
			return {Message{}, ErrInvalidJSONRPCVersion};
		}
	}
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

// === slice: ipc ===

// --- baseproto.go ------------------------------------------------------------

namespace {

// trimSpace — bytes.TrimSpace (ASCII whitespace at both ends).
std::string_view trimSpace(std::string_view s) {
	auto isSpace = [](char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\v' ||
		       c == '\f' || c == '\r';
	};
	while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
	while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
	return s;
}

// parseInt64 — strconv.ParseInt(s, 10, 64): optional sign + digits.
std::pair<int64_t, gostd::Error> parseInt64(std::string_view s) {
	std::string orig(s);
	if (!s.empty() && s[0] == '+') {
		s.remove_prefix(1); // strconv accepts a leading '+'
	}
	int64_t v = 0;
	auto [ptr, ec] =
	    std::from_chars(s.data(), s.data() + s.size(), v);
	// strconv.NumError: "strconv.ParseInt: parsing " + Quote(num) + ": " + err
	if (ec == std::errc::result_out_of_range) {
		return {0, gostd::newError("strconv.ParseInt: parsing " +
		                         gostd::detail::quoteGo(orig) +
		                         ": value out of range")};
	}
	if (s.empty() || ec != std::errc() || ptr != s.data() + s.size()) {
		return {0, gostd::newError("strconv.ParseInt: parsing " +
		                         gostd::detail::quoteGo(orig) +
		                         ": invalid syntax")};
	}
	return {v, nullptr};
}

// readUnderlying — bufio's fill loop: retry (0,nil) reads up to 100 times,
// then io.ErrNoProgress.
std::pair<int, gostd::Error> readUnderlying(gostd::io::Reader* r,
                                            std::span<char> buf) {
	for (int i = 100; i > 0; i--) {
		auto [n, err] = r->read(buf);
		if (n != 0 || err != nullptr) {
			return {n, err};
		}
	}
	return {0, gostd::newError("io: read made no progress")};
}

} // namespace

// Reader::Read — baseproto.go:39.
std::pair<std::string, gostd::Error> Reader::Read() {
	int64_t contentLength = 0;

	for (;;) {
		auto [line, err] = readBytes('\n');
		if (err != nullptr) {
			if (gostd::errorIs(err, gostd::io::errEOF)) {
				return {"", gostd::io::errEOF};
			}
			return {"", gostd::errorf("jsonrpc: read header: %w", {err})};
		}

		if (line == "\r\n") {
			break;
		}

		size_t colon = line.find(':');
		if (colon == std::string::npos) {
			return {"", gostd::errorf("%w: %q", {ErrInvalidHeader, line})};
		}
		std::string_view key(line.data(), colon);
		std::string_view value(line.data() + colon + 1,
		                       line.size() - colon - 1);
		if (key == "Content-Length") {
			auto [n, perr] = parseInt64(trimSpace(value));
			if (perr != nullptr) {
				return {"", gostd::errorf("%w: parse error: %w",
				                        {ErrInvalidContentLength, perr})};
			}
			if (n < 0) {
				return {"", gostd::errorf("%w: negative value %d",
				                        {ErrInvalidContentLength, n})};
			}
			contentLength = n;
		}
	}

	if (contentLength <= 0) {
		return {"", ErrNoContentLength};
	}

	std::string data(static_cast<size_t>(contentLength), '\0');
	if (auto err = readFull(
	        std::span<char>(data.data(), data.size()));
	    err != nullptr) {
		return {"", gostd::errorf("jsonrpc: read content: %w", {err})};
	}
	return {std::move(data), nullptr};
}

// Reader::readBytes — bufio.Reader.ReadBytes.
std::pair<std::string, gostd::Error> Reader::readBytes(char delim) {
	std::string line;
	for (;;) {
		size_t pos = buf_.find(delim);
		if (pos != std::string::npos) {
			line.append(buf_, 0, pos + 1);
			buf_.erase(0, pos + 1);
			return {std::move(line), nullptr};
		}
		line += buf_;
		buf_.clear();
		char tmp[4096];
		auto [n, err] = readUnderlying(
		    r_, std::span<char>(tmp, sizeof tmp));
		buf_.assign(tmp, static_cast<size_t>(n));
		if (err != nullptr) {
			// bufio returns the fragment read so far along with the error.
			line += buf_;
			buf_.clear();
			return {std::move(line), err};
		}
	}
}

// Reader::readFull — io.ReadFull over the buffered stream: drains buf_
// first, then reads the rest directly.
gostd::Error Reader::readFull(std::span<char> out) {
	size_t got = 0;
	while (got < out.size()) {
		if (!buf_.empty()) {
			size_t take = std::min(buf_.size(), out.size() - got);
			std::memcpy(out.data() + got, buf_.data(), take);
			buf_.erase(0, take);
			got += take;
			continue;
		}
		auto [n, err] = readUnderlying(r_, out.subspan(got));
		got += static_cast<size_t>(n);
		if (got >= out.size()) {
			return nullptr; // io.ReadAtLeast: n >= min clears the error
		}
		if (err != nullptr) {
			if (gostd::errorIs(err, gostd::io::errEOF)) {
				return got == 0 ? gostd::io::errEOF
				                : gostd::io::errUnexpectedEOF;
			}
			return err;
		}
	}
	return nullptr;
}

// Writer::Write — baseproto.go:93. Header + payload + single Flush.
gostd::Error Writer::Write(std::string_view data) {
	// fmt.Fprintf(w.w, "Content-Length: %d\r\n\r\n", len(data))
	buf_ += "Content-Length: ";
	buf_ += std::to_string(data.size());
	buf_ += "\r\n\r\n";
	buf_ += data;
	return flush();
}

// Writer::flush — bufio.Writer.Flush: ONE Write of the buffered bytes;
// a short write is io.ErrShortWrite.
gostd::Error Writer::flush() {
	if (buf_.empty()) {
		return nullptr;
	}
	size_t want = buf_.size();
	auto [n, err] = w_->write(std::string_view(buf_));
	if (n > 0) {
		buf_.erase(0, static_cast<size_t>(n));
	}
	if (err == nullptr && static_cast<size_t>(n) < want) {
		err = gostd::newError("short write");
	}
	return err;
}

// === end slice: ipc ===

} // namespace tsc::jsonrpc
