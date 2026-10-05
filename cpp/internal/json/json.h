// json.h — minimal port of tsc/internal/json (the jsontext/v2 wrapper):
// raw Value, a DOM parser matching encoding/json unmarshal semantics, and
// Go-compatible (HTML-escaping) writers for the scalar/struct shapes the
// content mapper protocol needs.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/gostd/gostd.h"

namespace tsc::json {

// Value — jsontext.Value: raw JSON bytes.
using Value = std::string;

// Kind — jsontext.Value.Kind(): classifies on the first byte. Returns 0 for an
// empty value.
inline char Kind(const Value& v) {
	if (v.empty()) {
		return 0;
	}
	char c = v[0];
	switch (c) {
	case '"': return '"';
	case 'n': return 'n';
	case 't': case 'f': return 'f';
	case '[': return '[';
	case '{': return '{';
	default: return '0'; // number: digit or '-'
	}
}

// Dom is one parsed JSON node; raw keeps the node's exact source bytes so
// `json.Value` fields decode as raw passthroughs.
struct Dom {
	enum class K { Null, Bool, Number, String, Array, Object };
	K kind = K::Null;
	std::string_view raw; // valid only while the parsed Value outlives the Dom
	bool boolVal = false;
	std::string strVal;   // decoded (String), or number text (Number)
	std::vector<Dom> arr;
	std::vector<std::pair<std::string, Dom>> obj;
};

// parse — jsontext.Unmarshal of a whole value: strict JSON, rejects trailing
// data, rejects duplicate object member names, rejects invalid UTF-8/lone
// surrogates (Unmarshal is called with no options in json.go).
std::pair<Dom, gostd::Error> parse(std::string_view data);

// objGet — encoding/json member lookup: exact match first, then a
// case-insensitive scan, later duplicates impossible (parse rejects them).
const Dom* objGet(const Dom& obj, std::string_view name);

// Scalar decoders — Go json.Unmarshal into the matching Go type. Numbers into
// ints require integral literals (no '.', 'e') and range-check.
std::pair<int64_t, gostd::Error> asInt(const Dom& v, const char* goType);
std::pair<int32_t, gostd::Error> asInt32(const Dom& v, const char* goType);
std::pair<double, gostd::Error> asNumber(const Dom& v, const char* goType);
std::pair<bool, gostd::Error> asBool(const Dom& v, const char* goType);
// asString decodes a String kind; asStringNull is for `*string`/`omitzero`
// fields where null decodes to empty.
std::pair<std::string, gostd::Error> asString(const Dom& v, const char* goType);

// Writers — encoding/json output: HTML-escaping (< > & → < > &),
// control chars → \u00XX, invalid UTF-8 → (json.Marshal passes
// jsontext.AllowInvalidUTF8(true)).
std::string marshalString(std::string_view s);
std::string marshalInt64(int64_t v);
std::string marshalBool(bool v);
// marshalObject/marshalArray take pre-marshaled member values.
std::string marshalObject(const std::vector<std::pair<std::string, std::string>>& members);
std::string marshalArray(const std::vector<std::string>& elements);
// marshalRaw emits v verbatim when it is a valid JSON value; empty -> error.
std::pair<std::string, gostd::Error> marshalRaw(const Value& v);

} // namespace tsc::json
