// packagejson — port of tsc/internal/packagejson (Parse + InfoCacheEntry).
// The JSON parsing mirrors encoding/json semantics for this package:
// duplicate names allowed (last wins), wrong-typed fields fail only that
// Expected<T> slot, malformed JSON fails the whole parse.

#include "internal/packagejson/packagejson.h"

#include "internal/json/json.h"

namespace tsc::packagejson {

namespace {

// jsonParser — minimal JSON value parser producing JSONValue trees.
struct jsonParser {
	std::string_view s;
	size_t pos = 0;
	bool failed = false;

	void skipWs() {
		while (pos < s.size() &&
		       (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' ||
		        s[pos] == '\r')) {
			pos++;
		}
	}

	char peek() {
		if (pos >= s.size()) {
			failed = true;
			return '\0';
		}
		return s[pos];
	}

	bool consume(char c) {
		skipWs();
		if (pos < s.size() && s[pos] == c) {
			pos++;
			return true;
		}
		failed = true;
		return false;
	}

	bool literal(std::string_view lit) {
		if (s.substr(pos, lit.size()) == lit) {
			pos += lit.size();
			return true;
		}
		failed = true;
		return false;
	}

	// appendUtf8 — encode a rune as UTF-8 (Go encoding/json semantics:
	// invalid sequences/U+FFFD handled by the caller).
	static void appendUtf8(std::string& out, char32_t r) {
		if (r < 0x80) {
			out += static_cast<char>(r);
		} else if (r < 0x800) {
			out += static_cast<char>(0xC0 | (r >> 6));
			out += static_cast<char>(0x80 | (r & 0x3F));
		} else if (r < 0x10000) {
			out += static_cast<char>(0xE0 | (r >> 12));
			out += static_cast<char>(0x80 | ((r >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (r & 0x3F));
		} else {
			out += static_cast<char>(0xF0 | (r >> 18));
			out += static_cast<char>(0x80 | ((r >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((r >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (r & 0x3F));
		}
	}

	char32_t hex4() {
		if (pos + 4 > s.size()) {
			failed = true;
			return 0;
		}
		uint32_t v = 0;
		for (int i = 0; i < 4; i++) {
			char c = s[pos + i];
			v <<= 4;
			if (c >= '0' && c <= '9') v |= c - '0';
			else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
			else {
				failed = true;
				return 0;
			}
		}
		pos += 4;
		return static_cast<char32_t>(v);
	}

	bool parseStringInto(std::string& out) {
		if (!consume('"')) return false;
		while (true) {
			if (pos >= s.size()) {
				failed = true;
				return false;
			}
			char c = s[pos];
			if (c == '"') {
				pos++;
				return true;
			}
			if (c == '\\') {
				pos++;
				if (pos >= s.size()) {
					failed = true;
					return false;
				}
				switch (s[pos]) {
				case '"': out += '"'; pos++; break;
				case '\\': out += '\\'; pos++; break;
				case '/': out += '/'; pos++; break;
				case 'b': out += '\b'; pos++; break;
				case 'f': out += '\f'; pos++; break;
				case 'n': out += '\n'; pos++; break;
				case 'r': out += '\r'; pos++; break;
				case 't': out += '\t'; pos++; break;
				case 'u': {
					pos++;
					char32_t r = hex4();
					if (failed) return false;
					// surrogate pair
					if (r >= 0xD800 && r <= 0xDBFF && pos + 6 <= s.size() &&
					    s[pos] == '\\' && s[pos + 1] == 'u') {
						pos += 2;
						char32_t lo = hex4();
						if (failed) return false;
						if (lo >= 0xDC00 && lo <= 0xDFFF) {
							r = 0x10000 + ((r - 0xD800) << 10) +
							    (lo - 0xDC00);
						} else {
							appendUtf8(out, 0xFFFD);
							appendUtf8(out, lo);
							continue;
						}
					} else if (r >= 0xD800 && r <= 0xDFFF) {
						r = 0xFFFD;
					}
					appendUtf8(out, r);
					break;
				}
				default:
					failed = true;
					return false;
				}
				continue;
			}
			if (static_cast<unsigned char>(c) < 0x20) {
				failed = true;
				return false;
			}
			out += c;
			pos++;
		}
	}

	bool parseNumberInto(double& out) {
		// Strict JSON number grammar: -? (0 | [1-9]\d*) (.\d+)? ([eE][+-]?\d+)?
		size_t start = pos;
		if (pos < s.size() && s[pos] == '-') pos++;
		if (pos < s.size() && s[pos] == '0') {
			pos++;
		} else if (pos < s.size() && s[pos] >= '1' && s[pos] <= '9') {
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
		} else {
			failed = true;
			return false;
		}
		if (pos < s.size() && s[pos] == '.') {
			pos++;
			if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
				failed = true;
				return false;
			}
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
		}
		if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
			pos++;
			if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) pos++;
			if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
				failed = true;
				return false;
			}
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
		}
		out = std::strtod(std::string(s.substr(start, pos - start)).c_str(),
		                  nullptr);
		return true;
	}

	bool parseValueInto(JSONValue& v) {
		skipWs();
		char c = peek();
		if (failed) return false;
		switch (c) {
		case 'n':
			if (!literal("null")) return false;
			v.type = JSONValueType::Null;
			return true;
		case 't':
			if (!literal("true")) return false;
			v.type = JSONValueType::Boolean;
			v.boolean = true;
			return true;
		case 'f':
			if (!literal("false")) return false;
			v.type = JSONValueType::Boolean;
			v.boolean = false;
			return true;
		case '"':
			v.type = JSONValueType::String;
			return parseStringInto(v.str);
		case '[': {
			if (!consume('[')) return false;
			auto arr = std::make_shared<std::vector<JSONValue>>();
			skipWs();
			if (pos < s.size() && s[pos] == ']') {
				pos++;
			} else {
				while (true) {
					JSONValue elem;
					if (!parseValueInto(elem)) return false;
					arr->push_back(std::move(elem));
					skipWs();
					if (pos >= s.size()) {
						failed = true;
						return false;
					}
					if (s[pos] == ']') {
						pos++;
						break;
					}
					if (s[pos] != ',') {
						failed = true;
						return false;
					}
					pos++;
				}
			}
			v.type = JSONValueType::Array;
			v.array = arr;
			return true;
		}
		case '{': {
			if (!consume('{')) return false;
			auto obj = std::make_shared<
			    collections::OrderedMap<std::string, JSONValue>>();
			skipWs();
			if (pos < s.size() && s[pos] == '}') {
				pos++;
			} else {
				while (true) {
					skipWs();
					std::string key;
					if (!parseStringInto(key)) return false;
					skipWs();
					if (pos >= s.size() || s[pos] != ':') {
						failed = true;
						return false;
					}
					pos++;
					JSONValue val;
					if (!parseValueInto(val)) return false;
					// AllowDuplicateNames: last wins.
					obj->Set(key, std::move(val));
					skipWs();
					if (pos >= s.size()) {
						failed = true;
						return false;
					}
					if (s[pos] == '}') {
						pos++;
						break;
					}
					if (s[pos] != ',') {
						failed = true;
						return false;
					}
					pos++;
				}
			}
			v.type = JSONValueType::Object;
			v.object = obj;
			return true;
		}
		default:
			v.type = JSONValueType::Number;
			return parseNumberInto(v.num);
		}
	}
};

// jsonTypeName — maps a node's type to the actualJSONType names Go's
// Expected.UnmarshalJSON derives from the leading byte.
const char* jsonTypeName(const JSONValue& v) {
	switch (v.type) {
	case JSONValueType::Null: return "null";
	case JSONValueType::String: return "string";
	case JSONValueType::Number: return "number";
	case JSONValueType::Boolean: return "boolean";
	case JSONValueType::Array: return "array";
	case JSONValueType::Object: return "object";
	default: return "";
	}
}

// setExpectedFrom — populates an Expected<T> from a JSON node following the
// shape of Expected.UnmarshalJSON.
template <typename T, typename Convert>
void setExpectedFrom(const JSONValue& node, Expected<T>& out,
                     const Convert& convert) {
	if (node.type == JSONValueType::Null) {
		out = Expected<T>{};
		out.Null = true;
		out.actualJSONType = "null";
		return;
	}
	out.actualJSONType = jsonTypeName(node);
	if (auto value = convert(node); value.has_value()) {
		out.Value = std::move(*value);
		out.Valid = true;
	}
}

std::optional<std::string> nodeAsString(const JSONValue& v) {
	if (v.type == JSONValueType::String) return v.str;
	return std::nullopt;
}

std::optional<bool> nodeAsBool(const JSONValue& v) {
	if (v.type == JSONValueType::Boolean) return v.boolean;
	return std::nullopt;
}

std::optional<std::vector<std::string>> nodeAsStringArray(
    const JSONValue& v) {
	if (v.type != JSONValueType::Array || v.array == nullptr) {
		return std::nullopt;
	}
	std::vector<std::string> out;
	for (auto& e : *v.array) {
		if (e.type != JSONValueType::String) return std::nullopt;
		out.push_back(e.str);
	}
	return out;
}

std::optional<DependencyMap> nodeAsDependencyMap(const JSONValue& v) {
	if (v.type != JSONValueType::Object || v.object == nullptr) {
		return std::nullopt;
	}
	DependencyMap out;
	for (auto& key : v.object->Keys()) {
		auto [val, ok] = v.object->Get(key);
		if (!ok || val->type != JSONValueType::String) {
			return std::nullopt;
		}
		out[key] = val->str;
	}
	return out;
}

ContentMapperFields parseContentMapper(const JSONValue& node) {
	ContentMapperFields out;
	if (node.type != JSONValueType::Object || node.object == nullptr) {
		return out;
	}
	for (auto& key : node.object->Keys()) {
		auto [val, ok] = node.object->Get(key);
		if (!ok) continue;
		if (key == "exec") {
			setExpectedFrom(*val, out.Exec, nodeAsStringArray);
		} else if (key == "compilerOptions") {
			setExpectedFrom(*val, out.CompilerOptions, nodeAsStringArray);
		} else if (key == "dynamicConfig") {
			setExpectedFrom(*val, out.DynamicConfig, nodeAsBool);
		}
	}
	return out;
}

// jsonValueToExportsOrImports — deep conversion JSONValue → ExportsOrImports.
ExportsOrImports jsonValueToExportsOrImports(const JSONValue& v) {
	ExportsOrImports out;
	out.type = v.type;
	out.str = v.str;
	out.num = v.num;
	out.boolean = v.boolean;
	if (v.type == JSONValueType::Array && v.array) {
		auto arr = std::make_shared<std::vector<ExportsOrImports>>();
		arr->reserve(v.array->size());
		for (auto& e : *v.array) {
			arr->push_back(jsonValueToExportsOrImports(e));
		}
		out.array = arr;
	} else if (v.type == JSONValueType::Object && v.object) {
		auto obj = std::make_shared<
		    collections::OrderedMap<std::string, ExportsOrImports>>();
		for (auto& key : v.object->Keys()) {
			auto [val, ok] = v.object->Get(key);
			if (ok) obj->Set(key, jsonValueToExportsOrImports(*val));
		}
		out.object = obj;
	}
	return out;
}

}  // namespace

// Parse — packagejson.go.
std::pair<Fields, bool> Parse(std::string_view data) {
	Fields fields;

	jsonParser parser{data};
	JSONValue root;
	if (!parser.parseValueInto(root)) {
		return {Fields{}, false};
	}
	parser.skipWs();
	if (parser.pos != parser.s.size() || root.type != JSONValueType::Object) {
		// json.Unmarshal requires a complete, top-level object to match a
		// struct.
		return {Fields{}, false};
	}

	auto* obj = root.object.get();
	for (auto& key : obj->Keys()) {
		auto [val, ok] = obj->Get(key);
		if (!ok) continue;
		const JSONValue& v = *val;
		if (key == "name") {
			setExpectedFrom(v, fields.Name, nodeAsString);
		} else if (key == "version") {
			setExpectedFrom(v, fields.Version, nodeAsString);
		} else if (key == "type") {
			setExpectedFrom(v, fields.Type, nodeAsString);
		} else if (key == "tsconfig") {
			setExpectedFrom(v, fields.TSConfig, nodeAsString);
		} else if (key == "main") {
			setExpectedFrom(v, fields.Main, nodeAsString);
		} else if (key == "types") {
			setExpectedFrom(v, fields.Types, nodeAsString);
		} else if (key == "typings") {
			setExpectedFrom(v, fields.Typings, nodeAsString);
		} else if (key == "typesVersions") {
			fields.TypesVersions = v;
		} else if (key == "imports") {
			fields.Imports = jsonValueToExportsOrImports(v);
		} else if (key == "exports") {
			fields.Exports = jsonValueToExportsOrImports(v);
		} else if (key == "dependencies") {
			setExpectedFrom(v, fields.Dependencies, nodeAsDependencyMap);
		} else if (key == "devDependencies") {
			setExpectedFrom(v, fields.DevDependencies, nodeAsDependencyMap);
		} else if (key == "peerDependencies") {
			setExpectedFrom(v, fields.PeerDependencies,
			                nodeAsDependencyMap);
		} else if (key == "optionalDependencies") {
			setExpectedFrom(v, fields.OptionalDependencies,
			                nodeAsDependencyMap);
		} else if (key == "typescript") {
			// Expected[typeScriptFields]: only a valid object yields a
			// value; anything else leaves ContentMapper absent (GetValue
			// returns the zero typeScriptFields).
			if (v.type == JSONValueType::Object && v.object) {
				Expected<ContentMapperFields> contentMapper;
				for (auto& k2 : v.object->Keys()) {
					auto [v2, ok2] = v.object->Get(k2);
					if (!ok2) continue;
					if (k2 == "contentMapper") {
						setExpectedFrom(*v2, contentMapper,
						                [](const JSONValue& n)
						                    -> std::optional<ContentMapperFields> {
							if (n.type == JSONValueType::Object) {
								return parseContentMapper(n);
							}
							return std::nullopt;
						});
					}
				}
				fields.ContentMapper = contentMapper;
			}
		}
	}

	return {fields, true};
}

// WithPackageDirectory — cache.go.
std::shared_ptr<InfoCacheEntry> InfoCacheEntry::WithPackageDirectory(
    const std::string& packageDirectory) const {
	if (PackageDirectory == packageDirectory) {
		// Go returns the same pointer; emulate with a shared alias.
		return std::shared_ptr<InfoCacheEntry>(
		    std::shared_ptr<InfoCacheEntry>{},
		    const_cast<InfoCacheEntry*>(this));
	}
	auto copy = std::make_shared<InfoCacheEntry>();
	copy->PackageDirectory = packageDirectory;
	copy->DirectoryExists = DirectoryExists;
	copy->Contents = Contents;
	return copy;
}

// JSONValue::unmarshalJSONFrom / unmarshalJSONValueFrom —
// jsonvalue.go:88-135. Go's `any` payload is modeled here as one slot per
// kind on JsonValueBase; `type` selects the live member. The T=JSONValue
// instantiation recurses through json::unmarshalDecode for array elements
// and object members, matching the Go generic's nested-value decode.
std::string JSONValue::unmarshalJSONFrom(json::Decoder& dec) {
	switch (dec.peekKind()) {
	case 'n': // json.Null.Kind()
		if (auto [t, e] = dec.readToken(); !e.empty()) {
			return e;
		}
		str.clear();
		array.reset();
		object.reset();
		type = JSONValueType::Null;
		return {};
	case '"':
		type = JSONValueType::String;
		if (auto e = json::unmarshalDecode(dec, &str); !e.empty()) {
			return e;
		}
		return {};
	case '[': {
		if (auto [t, e] = dec.readToken(); !e.empty()) {
			return e;
		}
		auto elements =
		    std::make_shared<std::vector<JSONValue>>();
		while (dec.peekKind() != json::EndArray.kind()) {
			JSONValue element;
			if (auto e = json::unmarshalDecode(dec, &element);
			    !e.empty()) {
				return e;
			}
			elements->push_back(std::move(element));
		}
		if (auto [t, e] = dec.readToken(); !e.empty()) {
			return e;
		}
		type = JSONValueType::Array;
		array = std::move(elements);
		return {};
	}
	case '{': {
		auto obj = std::make_shared<
		    collections::OrderedMap<std::string, JSONValue>>();
		if (auto e = json::unmarshalDecode(dec, obj.get());
		    !e.empty()) {
			return e;
		}
		type = JSONValueType::Object;
		object = std::move(obj);
		return {};
	}
	case 't':
	case 'f':
		type = JSONValueType::Boolean;
		if (auto e = json::unmarshalDecode(dec, &boolean); !e.empty()) {
			return e;
		}
		return {};
	default:
		type = JSONValueType::Number;
		if (auto e = json::unmarshalDecode(dec, &num); !e.empty()) {
			return e;
		}
		return {};
	}
}

}  // namespace tsc::packagejson
