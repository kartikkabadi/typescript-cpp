// Port of the tsc/internal/json wrapper semantics over jsontext:
// strict DOM parse + encoding/json-compatible scalar marshal/unmarshal.
#include "internal/json/json.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>

namespace tsc::json {

namespace {

// utf8RuneLen — decode the next rune's byte width; 1 for ASCII/invalid, 0 at
// end. Mirrors utf8.DecodeRuneInString's width (invalid bytes decode as
// RuneError with width 1).
int utf8RuneLen(std::string_view s, size_t i) {
	if (i >= s.size()) return 0;
	unsigned char b = (unsigned char)s[i];
	if (b < 0x80) return 1;
	if (b < 0xc2) return 1; // stray continuation/invalid
	if (b < 0xe0) return i + 1 < s.size() ? 2 : 1;
	if (b < 0xf0) return i + 2 < s.size() ? 3 : 1;
	if (b < 0xf5) return i + 3 < s.size() ? 4 : 1;
	return 1;
}

struct parser {
	std::string_view s;
	size_t i = 0;
	gostd::Error err;

	void skipWS() {
		while (i < s.size() &&
		       (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
			i++;
		}
	}
	gostd::Error fail(std::string_view msg, size_t at) {
		return gostd::errorf("jsontext: %s (offset %d)",
		                     {std::string(msg), (int64_t)at});
	}
	bool value(Dom& out) {
		if (err) return false;
		skipWS();
		size_t start = i;
		if (i >= s.size()) {
			err = fail("unexpected end of input", i);
			return false;
		}
		char c = s[i];
		bool ok = false;
		switch (c) {
		case 'n': ok = lit("null", Dom::K::Null, out); break;
		case 't': ok = lit("true", Dom::K::Bool, out, true); break;
		case 'f': ok = lit("false", Dom::K::Bool, out, false); break;
		case '"': ok = str(out); break;
		case '[': ok = array(out); break;
		case '{': ok = object(out); break;
		default: ok = number(out); break;
		}
		out.raw = s.substr(start, i - start);
		return ok;
	}
	bool lit(std::string_view word, Dom::K k, Dom& out, bool b = false) {
		if (s.substr(i, word.size()) != word) {
			err = fail("invalid literal", i);
			return false;
		}
		i += word.size();
		out.kind = k;
		out.boolVal = b;
		return true;
	}
	bool str(Dom& out) {
		out.kind = Dom::K::String;
		out.strVal.clear();
		i++; // '"'
		while (i < s.size()) {
			unsigned char c = (unsigned char)s[i];
			if (c == '"') {
				i++;
				return true;
			}
			if (c == '\\') {
				if (i + 1 >= s.size()) {
					err = fail("truncated escape", i);
					return false;
				}
				char e = s[i + 1];
				i += 2;
				switch (e) {
				case '"': out.strVal += '"'; break;
				case '\\': out.strVal += '\\'; break;
				case '/': out.strVal += '/'; break;
				case 'b': out.strVal += '\b'; break;
				case 'f': out.strVal += '\f'; break;
				case 'n': out.strVal += '\n'; break;
				case 'r': out.strVal += '\r'; break;
				case 't': out.strVal += '\t'; break;
				case 'u': {
					if (i + 4 > s.size()) {
						err = fail("truncated \\u escape", i);
						return false;
					}
					uint32_t cp = hex4(i);
					if (err) return false;
					if (cp >= 0xD800 && cp < 0xDC00) {
						// surrogate pair required
						if (i + 6 > s.size() || s[i] != '\\' ||
						    s[i + 1] != 'u') {
							err = fail("unpaired surrogate", i);
							return false;
						}
						i += 2;
						uint32_t lo = hex4(i);
						i -= 2;
						if (err) return false;
						if (lo < 0xDC00 || lo >= 0xE000) {
							err = fail("unpaired surrogate", i);
							return false;
						}
						i += 6;
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					} else if (cp >= 0xDC00 && cp < 0xE000) {
						err = fail("unpaired surrogate", i);
						return false;
					}
					encodeUTF8(out.strVal, cp);
					break;
				}
				default:
					err = fail("invalid escape", i - 2);
					return false;
				}
				continue;
			}
			if (c < 0x20) {
				err = fail("unescaped control byte in string", i);
				return false;
			}
			int w = utf8RuneLen(s, i);
			if (w == 1 && c >= 0x80) {
				// invalid UTF-8 byte — strict Unmarshal rejects it
				err = fail("invalid UTF-8 in string", i);
				return false;
			}
			out.strVal += s.substr(i, w);
			i += w;
		}
		err = fail("unterminated string", i);
		return false;
	}
	uint32_t hex4(size_t& at) {
		uint32_t v = 0;
		for (int j = 0; j < 4; j++) {
			char h = s[at + j];
			v <<= 4;
			if (h >= '0' && h <= '9') v |= h - '0';
			else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
			else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
			else {
				err = fail("invalid \\u escape", at);
				return 0;
			}
		}
		at += 4;
		return v;
	}
	void encodeUTF8(std::string& out, uint32_t cp) {
		if (cp < 0x80) {
			out += (char)cp;
		} else if (cp < 0x800) {
			out += (char)(0xC0 | cp >> 6);
			out += (char)(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += (char)(0xE0 | cp >> 12);
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		} else {
			out += (char)(0xF0 | cp >> 18);
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}
	bool number(Dom& out) {
		out.kind = Dom::K::Number;
		size_t start = i;
		if (i < s.size() && s[i] == '-') i++;
		// int part
		if (i < s.size() && s[i] == '0') {
			i++;
		} else {
			if (i >= s.size() || s[i] < '1' || s[i] > '9') {
				err = fail("invalid number", start);
				return false;
			}
			while (i < s.size() && isdigit((unsigned char)s[i])) i++;
		}
		if (i < s.size() && s[i] == '.') {
			i++;
			if (i >= s.size() || !isdigit((unsigned char)s[i])) {
				err = fail("invalid number", start);
				return false;
			}
			while (i < s.size() && isdigit((unsigned char)s[i])) i++;
		}
		if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
			i++;
			if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
			if (i >= s.size() || !isdigit((unsigned char)s[i])) {
				err = fail("invalid number", start);
				return false;
			}
			while (i < s.size() && isdigit((unsigned char)s[i])) i++;
		}
		out.strVal = s.substr(start, i - start);
		return true;
	}
	bool array(Dom& out) {
		out.kind = Dom::K::Array;
		i++; // '['
		skipWS();
		if (i < s.size() && s[i] == ']') {
			i++;
			return true;
		}
		for (;;) {
			Dom el;
			if (!value(el)) return false;
			out.arr.push_back(std::move(el));
			skipWS();
			if (i >= s.size()) break;
			if (s[i] == ',') {
				i++;
				continue;
			}
			if (s[i] == ']') {
				i++;
				return true;
			}
			break;
		}
		err = fail("unterminated array", i);
		return false;
	}
	bool object(Dom& out) {
		out.kind = Dom::K::Object;
		i++; // '{'
		skipWS();
		if (i < s.size() && s[i] == '}') {
			i++;
			return true;
		}
		for (;;) {
			skipWS();
			if (i >= s.size() || s[i] != '"') {
				err = fail("expected object member name", i);
				return false;
			}
			Dom name;
			if (!str(name)) return false;
			skipWS();
			if (i >= s.size() || s[i] != ':') {
				err = fail("expected ':'", i);
				return false;
			}
			i++;
			Dom val;
			if (!value(val)) return false;
			// jsontext default: duplicate names are an error.
			if (objGet(out, name.strVal) != nullptr) {
				err = fail("duplicate object member name", i);
				return false;
			}
			out.obj.emplace_back(std::move(name.strVal), std::move(val));
			skipWS();
			if (i >= s.size()) break;
			if (s[i] == ',') {
				i++;
				continue;
			}
			if (s[i] == '}') {
				i++;
				return true;
			}
			break;
		}
		err = fail("unterminated object", i);
		return false;
	}
};

gostd::Error unmarshalError(std::string_view kind, const Dom& v,
                            const char* goType) {
	return gostd::errorf(
	    "json: cannot unmarshal %s into Go value of type %s",
	    {std::string(kind), goType});
}

const char* domKindName(const Dom& v) {
	switch (v.kind) {
	case Dom::K::Null: return "null";
	case Dom::K::Bool: return "bool";
	case Dom::K::Number: return "number";
	case Dom::K::String: return "string";
	case Dom::K::Array: return "array";
	case Dom::K::Object: return "object";
	}
	return "?";
}

} // namespace

std::pair<Dom, gostd::Error> parse(std::string_view data) {
	parser p{data};
	Dom root;
	if (!p.value(root)) {
		return {Dom{}, p.err};
	}
	p.skipWS();
	if (p.i != data.size()) {
		return {Dom{}, p.fail("unexpected trailing data", p.i)};
	}
	return {std::move(root), nullptr};
}

const Dom* objGet(const Dom& obj, std::string_view name) {
	if (obj.kind != Dom::K::Object) {
		return nullptr;
	}
	for (const auto& [k, v] : obj.obj) {
		if (k == name) {
			return &v;
		}
	}
	// encoding/json: fall back to a case-insensitive match.
	for (const auto& [k, v] : obj.obj) {
		if (k.size() == name.size()) {
			bool eq = true;
			for (size_t j = 0; j < k.size(); j++) {
				char a = k[j], b = name[j];
				if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
				if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
				if (a != b) {
					eq = false;
					break;
				}
			}
			if (eq) {
				return &v;
			}
		}
	}
	return nullptr;
}

std::pair<int64_t, gostd::Error> asInt(const Dom& v, const char* goType) {
	if (v.kind == Dom::K::Null) {
		return {0, nullptr}; // null unmarshals as zero
	}
	if (v.kind != Dom::K::Number) {
		return {0, unmarshalError(domKindName(v), v, goType)};
	}
	std::string_view t = v.strVal;
	// Go ParseInt: optional sign + digits only.
	size_t j = 0;
	if (j < t.size() && t[j] == '-') j++;
	bool digits = false;
	int64_t n = 0;
	bool overflow = false;
	for (; j < t.size(); j++) {
		char c = t[j];
		if (c < '0' || c > '9') {
			return {0, unmarshalError("number", v, goType)};
		}
		digits = true;
		int d = c - '0';
		if (n > (INT64_MAX - d) / 10) overflow = true;
		if (!overflow) n = n * 10 + d;
	}
	if (!digits || overflow) {
		return {0, unmarshalError("number", v, goType)};
	}
	if (!t.empty() && t[0] == '-') n = -n;
	return {n, nullptr};
}

std::pair<int32_t, gostd::Error> asInt32(const Dom& v, const char* goType) {
	auto [n, err] = asInt(v, goType);
	if (err) return {0, err};
	if (n < INT32_MIN || n > INT32_MAX) {
		return {0, unmarshalError("number", v, goType)};
	}
	return {(int32_t)n, nullptr};
}

std::pair<double, gostd::Error> asNumber(const Dom& v, const char* goType) {
	if (v.kind == Dom::K::Null) {
		return {0, nullptr};
	}
	if (v.kind != Dom::K::Number) {
		return {0, unmarshalError(domKindName(v), v, goType)};
	}
	double d;
	auto r = std::from_chars(v.strVal.data(),
	                         v.strVal.data() + v.strVal.size(), d);
	if (r.ec != std::errc()) {
		return {0, unmarshalError("number", v, goType)};
	}
	return {d, nullptr};
}

std::pair<bool, gostd::Error> asBool(const Dom& v, const char* goType) {
	if (v.kind == Dom::K::Null) {
		return {false, nullptr};
	}
	if (v.kind != Dom::K::Bool) {
		return {false, unmarshalError(domKindName(v), v, goType)};
	}
	return {v.boolVal, nullptr};
}

std::pair<std::string, gostd::Error> asString(const Dom& v, const char* goType) {
	if (v.kind == Dom::K::Null) {
		return {"", nullptr};
	}
	if (v.kind != Dom::K::String) {
		return {"", unmarshalError(domKindName(v), v, goType)};
	}
	return {v.strVal, nullptr};
}

// ---------------------------------------------------------------------------
// writers
// ---------------------------------------------------------------------------

std::string marshalString(std::string_view s) {
	std::string out;
	out.reserve(s.size() + 2);
	out += '"';
	static const char* hex = "0123456789abcdef";
	for (size_t i = 0; i < s.size();) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
		case '"': out += "\\\""; i++; continue;
		case '\\': out += "\\\\"; i++; continue;
		case '\n': out += "\\n"; i++; continue;
		case '\r': out += "\\r"; i++; continue;
		case '\t': out += "\\t"; i++; continue;
		}
		if (c < 0x20) {
			out += "\\u00";
			out += hex[c >> 4];
			out += hex[c & 0xf];
			i++;
			continue;
		}
		if (c < 0x80) {
			out += (char)c;
			i++;
			continue;
		}
		int w = utf8RuneLen(s, i);
		if (w == 1) {
			out += "\xef\xbf\xbd"; // 
			i++;
			continue;
		}
		//
		out += s.substr(i, w);
		i += w;
	}
	out += '"';
	return out;
}

std::string marshalInt64(int64_t v) { return std::to_string(v); }
std::string marshalBool(bool v) { return v ? "true" : "false"; }

std::string marshalObject(
    const std::vector<std::pair<std::string, std::string>>& members) {
	std::string out = "{";
	bool first = true;
	for (const auto& [k, v] : members) {
		if (!first) out += ',';
		first = false;
		out += marshalString(k);
		out += ':';
		out += v;
	}
	out += '}';
	return out;
}

std::string marshalArray(const std::vector<std::string>& elements) {
	std::string out = "[";
	bool first = true;
	for (const auto& e : elements) {
		if (!first) out += ',';
		first = false;
		out += e;
	}
	out += ']';
	return out;
}

std::pair<std::string, gostd::Error> marshalRaw(const Value& v) {
	auto [dom, err] = parse(v);
	if (err != nullptr) {
		return {"", err};
	}
	return {v, nullptr};
}

} // namespace tsc::json
