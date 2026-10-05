// Port of tsc/internal/sourcemap/source_mapper.go
#include "internal/sourcemap/sourcemap.h"

#include "internal/ast/ast.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>

namespace tsc::sourcemap {
namespace {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// strings.CutPrefix
bool cutPrefix(std::string_view& s, std::string_view prefix) {
	if (!s.starts_with(prefix)) {
		return false;
	}
	s = s.substr(prefix.size());
	return true;
}

// core.DeduplicateSorted — keeps the first of each run of equal elements.
template <class T, class Eq>
std::vector<T> deduplicateSorted(std::vector<T> slice, Eq isEqual) {
	if (slice.empty()) {
		return slice;
	}
	std::vector<T> deduplicated;
	deduplicated.reserve(slice.size());
	T last = slice[0];
	deduplicated.push_back(slice[0]);
	for (size_t i = 1; i < slice.size(); i++) {
		T next = slice[i];
		if (isEqual(last, next)) {
			continue;
		}
		deduplicated.push_back(next);
		last = next;
	}
	return deduplicated;
}

// slices.BinarySearchFunc — first index i where cmp(x[i], target) >= 0.
template <class T, class V, class Cmp>
int binarySearchFunc(const std::vector<T>& xs, const V& target, Cmp cmp) {
	int lo = 0;
	int hi = static_cast<int>(xs.size());
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (cmp(xs[mid], target) < 0) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return lo;
}

// ---------------------------------------------------------------------------
// encoding/base64 StdEncoding.DecodeString — {bytes, ok}; \r\n ignored, `=`
// padding legal only at the very end.
// ---------------------------------------------------------------------------

int base64DecodeChar(unsigned char c) {
	if (c >= 'A' && c <= 'Z') {
		return c - 'A';
	}
	if (c >= 'a' && c <= 'z') {
		return c - 'a' + 26;
	}
	if (c >= '0' && c <= '9') {
		return c - '0' + 52;
	}
	if (c == '+') {
		return 62;
	}
	if (c == '/') {
		return 63;
	}
	return -1;
}

std::pair<std::string, bool> base64Decode(std::string_view in) {
	std::string s;
	s.reserve(in.size());
	for (char c : in) {
		// Go's decoder ignores \r and \n
		if (c == '\r' || c == '\n') {
			continue;
		}
		s += c;
	}
	if (s.size() % 4 != 0) {
		return {"", false}; // CorruptInputError
	}
	std::string out;
	out.reserve(s.size() / 4 * 3);
	for (size_t i = 0; i < s.size(); i += 4) {
		bool last = i + 4 == s.size();
		int pad = 0;
		if (s[i + 3] == '=') {
			pad++;
		}
		if (s[i + 2] == '=') {
			pad++;
		}
		if (pad != 0 && !last) {
			return {"", false}; // padding may only appear in the last quantum
		}
		// '=' cannot occupy position 0/1 of a quantum
		if (s[i] == '=' || s[i + 1] == '=') {
			return {"", false};
		}
		int a = base64DecodeChar(static_cast<unsigned char>(s[i]));
		int b = base64DecodeChar(static_cast<unsigned char>(s[i + 1]));
		if (a < 0 || b < 0) {
			return {"", false};
		}
		uint32_t v =
		    (static_cast<uint32_t>(a) << 18) | (static_cast<uint32_t>(b) << 12);
		if (pad == 0) {
			int c = base64DecodeChar(static_cast<unsigned char>(s[i + 2]));
			int d = base64DecodeChar(static_cast<unsigned char>(s[i + 3]));
			if (c < 0 || d < 0) {
				return {"", false};
			}
			v |= (static_cast<uint32_t>(c) << 6) | static_cast<uint32_t>(d);
			out += static_cast<char>((v >> 16) & 0xFF);
			out += static_cast<char>((v >> 8) & 0xFF);
			out += static_cast<char>(v & 0xFF);
		} else if (pad == 1) {
			int c = base64DecodeChar(static_cast<unsigned char>(s[i + 2]));
			if (c < 0) {
				return {"", false};
			}
			v |= static_cast<uint32_t>(c) << 6;
			out += static_cast<char>((v >> 16) & 0xFF);
			out += static_cast<char>((v >> 8) & 0xFF);
		} else {
			// pad == 2
			out += static_cast<char>((v >> 16) & 0xFF);
		}
	}
	return {out, true};
}

// ---------------------------------------------------------------------------
// Minimal strict JSON parse for RawSourceMap — mirrors encoding/json/v2
// (jsontext) defaults as used by json.Unmarshal: RFC 8259 syntax, single
// root value, no duplicate object member names, no invalid UTF-8.
// ---------------------------------------------------------------------------

struct JsonValue {
	enum Kind : char { Null, Bool, Number, String, Array, Object } kind = Null;
	std::string str;                                    // String text / raw Number literal
	std::vector<JsonValue> items;                       // Array
	std::vector<std::pair<std::string, JsonValue>> members; // Object
};

bool isUtf8Continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

// isValidUtf8 — Go utf8.ValidString (rejects overlongs, surrogates, > U+10FFFF)
bool isValidUtf8(std::string_view s) {
	for (size_t i = 0; i < s.size();) {
		auto b0 = static_cast<unsigned char>(s[i]);
		if (b0 < 0x80) {
			i++;
			continue;
		}
		if (b0 < 0xC2) { // 0x80..0xC1: stray continuation / overlong 2-byte
			return false;
		}
		int len = b0 < 0xE0 ? 2 : b0 < 0xF0 ? 3 : b0 < 0xF5 ? 4 : 0;
		if (len == 0 || i + static_cast<size_t>(len) > s.size()) {
			return false;
		}
		auto b1 = static_cast<unsigned char>(s[i + 1]);
		// per-lead-byte second-byte ranges (kill overlongs/surrogates)
		switch (b0) {
		case 0xE0:
			if (b1 < 0xA0 || b1 > 0xBF) return false;
			break;
		case 0xED:
			if (b1 < 0x80 || b1 > 0x9F) return false;
			break;
		case 0xF0:
			if (b1 < 0x90 || b1 > 0xBF) return false;
			break;
		case 0xF4:
			if (b1 < 0x80 || b1 > 0x8F) return false;
			break;
		default:
			if (!isUtf8Continuation(b1)) return false;
		}
		for (int k = 2; k < len; k++) {
			if (!isUtf8Continuation(static_cast<unsigned char>(s[i + k]))) {
				return false;
			}
		}
		i += static_cast<size_t>(len);
	}
	return true;
}

struct JsonParser {
	std::string_view s;
	size_t pos = 0;

	explicit JsonParser(std::string_view input) : s(input) {}

	void ws() {
		while (pos < s.size() &&
		       (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' ||
		        s[pos] == '\r')) {
			pos++;
		}
	}

	bool literal(std::string_view lit) {
		if (s.substr(pos, lit.size()) == lit) {
			pos += lit.size();
			return true;
		}
		return false;
	}

	bool string_(std::string& out) {
		// pos at '"'
		pos++;
		out.clear();
		while (true) {
			if (pos >= s.size()) {
				return false; // unterminated
			}
			auto c = static_cast<unsigned char>(s[pos]);
			if (c == '"') {
				pos++;
				return true;
			}
			if (c == '\\') {
				pos++;
				if (pos >= s.size()) {
					return false;
				}
				char e = s[pos++];
				switch (e) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					if (pos + 4 > s.size()) {
						return false;
					}
					auto hex = [&](size_t off) -> int {
						char h = s[pos + off];
						if (h >= '0' && h <= '9') return h - '0';
						if (h >= 'a' && h <= 'f') return h - 'a' + 10;
						if (h >= 'A' && h <= 'F') return h - 'A' + 10;
						return -1;
					};
					int h0 = hex(0), h1 = hex(1), h2 = hex(2), h3 = hex(3);
					if ((h0 | h1 | h2 | h3) < 0) {
						return false;
					}
					pos += 4;
					char32_t r = static_cast<char32_t>(
					    (h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
					if (isHighSurrogate(r)) {
						// must pair with \uXXXX low surrogate
						if (pos + 6 > s.size() || s[pos] != '\\' ||
						    s[pos + 1] != 'u') {
							return false;
						}
						pos += 2;
						int l0 = hex(0), l1 = hex(1), l2 = hex(2),
						    l3 = hex(3);
						if ((l0 | l1 | l2 | l3) < 0) {
							return false;
						}
						pos += 4;
						char32_t lo = static_cast<char32_t>(
						    (l0 << 12) | (l1 << 8) | (l2 << 4) | l3);
						if (!isLowSurrogate(lo)) {
							return false;
						}
						r = surrogatePairToCodePoint(r, lo);
					} else if (isLowSurrogate(r)) {
						return false; // unpaired low surrogate
					}
					char buf[4];
					int n = encodeUtf8Rune(r, buf);
					out.append(buf, static_cast<size_t>(n));
					break;
				}
				default:
					return false;
				}
				continue;
			}
			if (c < 0x20) {
				return false; // unescaped control character
			}
			out += static_cast<char>(c);
			pos++;
		}
	}

	bool number(std::string& out) {
		size_t start = pos;
		if (pos < s.size() && s[pos] == '-') {
			pos++;
		}
		if (pos >= s.size()) {
			return false;
		}
		if (s[pos] == '0') {
			pos++;
		} else if (s[pos] >= '1' && s[pos] <= '9') {
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
				pos++;
			}
		} else {
			return false;
		}
		if (pos < s.size() && s[pos] == '.') {
			pos++;
			if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
				return false;
			}
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
				pos++;
			}
		}
		if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
			pos++;
			if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
				pos++;
			}
			if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
				return false;
			}
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
				pos++;
			}
		}
		out = std::string(s.substr(start, pos - start));
		return true;
	}

	bool value(JsonValue& out, int depth) {
		if (depth > 10000) {
			return false; // jsontext MaxNestingDepth
		}
		ws();
		if (pos >= s.size()) {
			return false;
		}
		switch (s[pos]) {
		case 'n':
			if (!literal("null")) return false;
			out.kind = JsonValue::Null;
			return true;
		case 't':
			if (!literal("true")) return false;
			out.kind = JsonValue::Bool;
			return true;
		case 'f':
			if (!literal("false")) return false;
			out.kind = JsonValue::Bool;
			return true;
		case '"':
			out.kind = JsonValue::String;
			return string_(out.str);
		case '[': {
			pos++;
			out.kind = JsonValue::Array;
			ws();
			if (pos < s.size() && s[pos] == ']') {
				pos++;
				return true;
			}
			for (;;) {
				out.items.emplace_back();
				if (!value(out.items.back(), depth + 1)) {
					return false;
				}
				ws();
				if (pos >= s.size()) {
					return false;
				}
				if (s[pos] == ',') {
					pos++;
					continue;
				}
				if (s[pos] == ']') {
					pos++;
					return true;
				}
				return false;
			}
		}
		case '{': {
			pos++;
			out.kind = JsonValue::Object;
			ws();
			if (pos < s.size() && s[pos] == '}') {
				pos++;
				return true;
			}
			for (;;) {
				ws();
				if (pos >= s.size() || s[pos] != '"') {
					return false;
				}
				std::string name;
				if (!string_(name)) {
					return false;
				}
				// jsontext default: duplicate member names are an error
				for (auto& m : out.members) {
					if (m.first == name) {
						return false;
					}
				}
				ws();
				if (pos >= s.size() || s[pos] != ':') {
					return false;
				}
				pos++;
				JsonValue member;
				if (!value(member, depth + 1)) {
					return false;
				}
				out.members.emplace_back(std::move(name), std::move(member));
				ws();
				if (pos >= s.size()) {
					return false;
				}
				if (s[pos] == ',') {
					pos++;
					continue;
				}
				if (s[pos] == '}') {
					pos++;
					return true;
				}
				return false;
			}
		}
		default:
			if (s[pos] == '-' || (s[pos] >= '0' && s[pos] <= '9')) {
				out.kind = JsonValue::Number;
				return number(out.str);
			}
			return false;
		}
	}

	// Parses a complete JSON document (leading/trailing whitespace allowed).
	bool parse(JsonValue& out) {
		if (!isValidUtf8(s)) {
			return false;
		}
		if (!value(out, 0)) {
			return false;
		}
		ws();
		return pos == s.size();
	}
};

// json.Unmarshal into RawSourceMap — field semantics: case-sensitive names,
// `null` is a no-op, unknown members ignored, type mismatches are errors.
bool unmarshalInt(const JsonValue& v, int& out) {
	if (v.kind == JsonValue::Null) {
		return true;
	}
	if (v.kind != JsonValue::Number) {
		return false;
	}
	// int fields accept integer literals only (no fraction/exponent)
	for (char c : v.str) {
		if (c == '.' || c == 'e' || c == 'E') {
			return false;
		}
	}
	errno = 0;
	char* end = nullptr;
	long long n = std::strtoll(v.str.c_str(), &end, 10);
	if (errno != 0 || end == v.str.c_str() || *end != '\0') {
		return false;
	}
	out = static_cast<int>(n);
	return true;
}

bool unmarshalString(const JsonValue& v, std::string& out) {
	if (v.kind == JsonValue::Null) {
		return true;
	}
	if (v.kind != JsonValue::String) {
		return false;
	}
	out = v.str;
	return true;
}

bool unmarshalStringArray(const JsonValue& v, std::vector<std::string>& out) {
	if (v.kind == JsonValue::Null) {
		return true;
	}
	if (v.kind != JsonValue::Array) {
		return false;
	}
	out.clear();
	out.reserve(v.items.size());
	for (auto& item : v.items) {
		// elements are string; a null element unmarshals as the zero value ""
		if (item.kind == JsonValue::Null) {
			out.emplace_back();
		} else if (item.kind == JsonValue::String) {
			out.push_back(item.str);
		} else {
			return false;
		}
	}
	return true;
}

bool unmarshalOptionalStringArray(
    const JsonValue& v,
    std::optional<std::vector<std::optional<std::string>>>& out) {
	if (v.kind == JsonValue::Null) {
		return true;
	}
	if (v.kind != JsonValue::Array) {
		return false;
	}
	std::vector<std::optional<std::string>> list;
	list.reserve(v.items.size());
	for (auto& item : v.items) {
		if (item.kind == JsonValue::Null) {
			list.emplace_back(std::nullopt);
		} else if (item.kind == JsonValue::String) {
			list.emplace_back(item.str);
		} else {
			return false;
		}
	}
	out = std::move(list);
	return true;
}

bool unmarshalRawSourceMap(const JsonValue& v, RawSourceMap& out) {
	if (v.kind == JsonValue::Null) {
		return true; // unmarshaling null into a struct is a no-op
	}
	if (v.kind != JsonValue::Object) {
		return false;
	}
	for (auto& member : v.members) {
		const std::string& name = member.first;
		const JsonValue& val = member.second;
		if (name == "version") {
			if (!unmarshalInt(val, out.Version)) return false;
		} else if (name == "file") {
			if (!unmarshalString(val, out.File)) return false;
		} else if (name == "sourceRoot") {
			if (!unmarshalString(val, out.SourceRoot)) return false;
		} else if (name == "sources") {
			if (!unmarshalStringArray(val, out.Sources)) return false;
		} else if (name == "names") {
			if (!unmarshalStringArray(val, out.Names)) return false;
		} else if (name == "mappings") {
			if (!unmarshalString(val, out.Mappings)) return false;
		} else if (name == "sourcesContent") {
			if (!unmarshalOptionalStringArray(val, out.SourcesContent)) {
				return false;
			}
		}
		// unknown members are ignored (already syntax-validated)
	}
	return true;
}

// ---------------------------------------------------------------------------
// source_mapper.go
// ---------------------------------------------------------------------------

// tryParseRawSourceMap — source_mapper.go:272
RawSourceMap* tryParseRawSourceMap(std::string_view contents) {
	auto* sourceMap = new RawSourceMap();
	JsonParser p(contents);
	JsonValue root;
	if (!p.parse(root) || !unmarshalRawSourceMap(root, *sourceMap)) {
		// err != nil
		return nullptr;
	}
	if (sourceMap->Version != 3) {
		return nullptr;
	}
	return sourceMap;
}

// convertDocumentToSourceMapper — source_mapper.go:257
DocumentPositionMapper* convertDocumentToSourceMapper(
    Host* host, std::string_view contents, std::string_view mapFileName);

// tryGetSourceMappingURL — source_mapper.go:284
std::string tryGetSourceMappingURL(Host* host, std::string_view fileName) {
	ECMALineInfo* lineInfo = host->GetECMALineInfo(fileName);
	return TryGetSourceMappingURL(lineInfo);
}

// tryParseBase64Url — source_mapper.go:290
// Equivalent to /^data:(?:application\/json;(?:charset=[uU][tT][fF]-8;)?base64,([A-Za-z0-9+/=]+)$)?/
std::pair<std::string_view, bool> tryParseBase64Url(std::string_view url) {
	if (!cutPrefix(url, "data:")) {
		return {"", false};
	}
	if (!cutPrefix(url, "application/json;")) {
		return {"", true};
	}
	if (cutPrefix(url, "charset=")) {
		// Go: url[:len(`utf-8;`)] — panics when fewer than 6 bytes remain
		if (url.size() < 6) {
			TSC_UNREACHABLE("slice bounds out of range");
		}
		if (!stringutil::EquateStringCaseInsensitive(url.substr(0, 6), "utf-8;")) {
			return {"", true};
		}
		url = url.substr(6);
	}
	if (!cutPrefix(url, "base64,")) {
		return {"", true};
	}
	// validate remaining runes
	for (size_t i = 0; i < url.size();) {
		int width;
		char32_t r = decodeUtf8Rune(url.substr(i), &width);
		if (!(isASCIILetter(r) || isDigit(r) || r == U'+' || r == U'/' ||
		      r == U'=')) {
			return {"", true};
		}
		i += static_cast<size_t>(width);
	}
	return {url, true};
}

// createDocumentPositionMapper — source_mapper.go:52
DocumentPositionMapper* createDocumentPositionMapper(
    Host* host, const RawSourceMap* sourceMap, std::string_view mapPath) {
	std::string mapDirectory = tspath::getDirectoryPath(mapPath);
	std::string sourceRoot;
	if (!sourceMap->SourceRoot.empty()) {
		sourceRoot =
		    tspath::getNormalizedAbsolutePath(sourceMap->SourceRoot,
		                                      mapDirectory);
	} else {
		sourceRoot = mapDirectory;
	}
	std::string generatedAbsoluteFilePath =
	    tspath::getNormalizedAbsolutePath(sourceMap->File, mapDirectory);
	// core.Map(sourceMap.Sources, ...)
	std::vector<std::string> sourceFileAbsolutePaths;
	sourceFileAbsolutePaths.reserve(sourceMap->Sources.size());
	for (const std::string& source : sourceMap->Sources) {
		sourceFileAbsolutePaths.push_back(
		    tspath::getNormalizedAbsolutePath(source, sourceRoot));
	}
	bool useCaseSensitiveFileNames = host->UseCaseSensitiveFileNames();
	std::unordered_map<std::string, SourceIndex> sourceToSourceIndexMap;
	for (size_t i = 0; i < sourceFileAbsolutePaths.size(); i++) {
		sourceToSourceIndexMap[tspath::getCanonicalFileName(
		    sourceFileAbsolutePaths[i],
		    useCaseSensitiveFileNames)] = static_cast<SourceIndex>(i);
	}

	std::vector<MappedPosition*> decodedMappings;
	std::vector<MappedPosition*> generatedMappings;
	std::unordered_map<SourceIndex, std::vector<SourceMappedPosition*>>
	    sourceMappings;

	// getDecodedMappings()
	MappingsDecoder* decoder = DecodeMappings(sourceMap->Mappings);
	decoder->Values([&](Mapping* mapping) {
		// processMapping()
		int generatedPosition = -1;
		if (ECMALineInfo* lineInfo =
		        host->GetECMALineInfo(generatedAbsoluteFilePath)) {
			generatedPosition = computePositionOfLineAndUTF16Character(
			    lineInfo->lineStarts, mapping->GeneratedLine,
			    mapping->GeneratedCharacter, lineInfo->text,
			    /*allowEdits*/ true);
		}

		int sourcePosition = -1;
		if (mapping->IsSourceMapping()) {
			if (mapping->SourceIndex < 0 ||
			    mapping->SourceIndex >= static_cast<SourceIndex>(
			                                sourceFileAbsolutePaths.size())) {
				TSC_UNREACHABLE("index out of range"); // Go slice-index panic
			}
			if (ECMALineInfo* lineInfo = host->GetECMALineInfo(
			        sourceFileAbsolutePaths[mapping->SourceIndex])) {
				int pos = computePositionOfLineAndUTF16Character(
				    lineInfo->lineStarts, mapping->SourceLine,
				    mapping->SourceCharacter, lineInfo->text,
				    /*allowEdits*/ true);
				sourcePosition = pos;
			}
		}

		decodedMappings.push_back(
		    new MappedPosition{generatedPosition, sourcePosition,
		                       mapping->SourceIndex, mapping->NameIndex});
		return true;
	});
	if (!decoder->Error().empty()) {
		decodedMappings.clear(); // decodedMappings = nil
	}

	// getSourceMappings()
	for (MappedPosition* mapping : decodedMappings) {
		if (!mapping->isSourceMappedPosition()) {
			continue;
		}
		SourceIndex sourceIndex = mapping->sourceIndex;
		sourceMappings[sourceIndex].push_back(
		    new SourceMappedPosition{mapping->generatedPosition,
		                             mapping->sourcePosition, sourceIndex,
		                             mapping->nameIndex});
	}
	for (auto& kv : sourceMappings) {
		std::vector<SourceMappedPosition*>& list = kv.second;
		std::sort(
		    list.begin(), list.end(),
		    [](SourceMappedPosition* a, SourceMappedPosition* b) {
			    TSC_ASSERT(a->sourceIndex == b->sourceIndex,
			               "All source mappings should have the same "
			               "source index");
			    return a->sourcePosition < b->sourcePosition;
		    });
		list = deduplicateSorted(
		    list, [](SourceMappedPosition* a, SourceMappedPosition* b) {
			    return a->generatedPosition == b->generatedPosition &&
			           a->sourceIndex == b->sourceIndex &&
			           a->sourcePosition == b->sourcePosition;
		    });
	}

	// getGeneratedMappings()
	generatedMappings = decodedMappings;
	std::sort(generatedMappings.begin(), generatedMappings.end(),
	          [](MappedPosition* a, MappedPosition* b) {
		          return a->generatedPosition < b->generatedPosition;
	          });
	generatedMappings = deduplicateSorted(
	    generatedMappings, [](MappedPosition* a, MappedPosition* b) {
		    return a->generatedPosition == b->generatedPosition &&
		           a->sourceIndex == b->sourceIndex &&
		           a->sourcePosition == b->sourcePosition;
	    });

	auto* d = new DocumentPositionMapper();
	d->useCaseSensitiveFileNames = useCaseSensitiveFileNames;
	d->sourceFileAbsolutePaths = std::move(sourceFileAbsolutePaths);
	d->sourceToSourceIndexMap = std::move(sourceToSourceIndexMap);
	d->generatedAbsoluteFilePath = std::move(generatedAbsoluteFilePath);
	d->generatedMappings = std::move(generatedMappings);
	d->sourceMappings = std::move(sourceMappings);
	return d;
}

// convertDocumentToSourceMapper — source_mapper.go:257
DocumentPositionMapper* convertDocumentToSourceMapper(
    Host* host, std::string_view contents, std::string_view mapFileName) {
	RawSourceMap* sourceMap = tryParseRawSourceMap(contents);
	if (sourceMap == nullptr || sourceMap->Sources.empty() ||
	    sourceMap->File.empty() || sourceMap->Mappings.empty()) {
		// invalid map
		return nullptr;
	}

	// Don't support source maps that contain inlined sources
	if (sourceMap->SourcesContent.has_value()) {
		for (const auto& s : *sourceMap->SourcesContent) {
			if (s.has_value()) {
				return nullptr;
			}
		}
	}

	return createDocumentPositionMapper(host, sourceMap, mapFileName);
}

} // namespace

// DocumentPositionMapper.GetSourcePosition — source_mapper.go:169
// Free function (spanmap convention): Go callers may invoke on a nil mapper.
DocumentPosition*
GetSourcePosition(const DocumentPositionMapper* m, const DocumentPosition* loc) {
	if (m == nullptr) {
		return nullptr;
	}
	if (m->generatedMappings.empty()) {
		return nullptr;
	}

	int targetIndex = binarySearchFunc(
	    m->generatedMappings, loc->Pos,
	    [](const MappedPosition* mp, int pos) {
		    return static_cast<int64_t>(mp->generatedPosition) - pos;
	    });

	if (targetIndex < 0 ||
	    targetIndex >= static_cast<int>(m->generatedMappings.size())) {
		return nullptr;
	}

	MappedPosition* mapping = m->generatedMappings[targetIndex];
	if (!mapping->isSourceMappedPosition()) {
		return nullptr;
	}
	if (mapping->sourceIndex < 0 ||
	    mapping->sourceIndex >=
	        static_cast<SourceIndex>(m->sourceFileAbsolutePaths.size())) {
		TSC_UNREACHABLE("index out of range"); // Go slice-index panic
	}

	// Closest position
	return new DocumentPosition{m->sourceFileAbsolutePaths[mapping->sourceIndex],
	                            mapping->sourcePosition};
}

// DocumentPositionMapper.GetGeneratedPosition — source_mapper.go:197
DocumentPosition*
GetGeneratedPosition(const DocumentPositionMapper* m,
                     const DocumentPosition* loc) {
	if (m == nullptr) {
		return nullptr;
	}
	auto it = m->sourceToSourceIndexMap.find(tspath::getCanonicalFileName(
	    loc->FileName, m->useCaseSensitiveFileNames));
	if (it == m->sourceToSourceIndexMap.end()) {
		return nullptr;
	}
	SourceIndex sourceIndex = it->second;
	// Go checks against len(d.sourceMappings) — the MAP's key count
	if (sourceIndex < 0 ||
	    sourceIndex >= static_cast<SourceIndex>(m->sourceMappings.size())) {
		return nullptr;
	}
	// Go: sourceMappings := d.sourceMappings[sourceIndex] — absent key yields
	// a nil (empty) list; no insertion like operator[] would perform.
	static const std::vector<SourceMappedPosition*> emptyMappings;
	auto smIt = m->sourceMappings.find(sourceIndex);
	const std::vector<SourceMappedPosition*>& list =
	    smIt != m->sourceMappings.end() ? smIt->second : emptyMappings;
	int targetIndex = binarySearchFunc(
	    list, loc->Pos, [](const SourceMappedPosition* mp, int pos) {
		    return static_cast<int64_t>(mp->sourcePosition) - pos;
	    });

	if (targetIndex < 0 || targetIndex >= static_cast<int>(list.size())) {
		return nullptr;
	}

	MappedPosition* mapping = list[targetIndex];
	if (mapping->sourceIndex != sourceIndex) {
		return nullptr;
	}

	// Closest position
	return new DocumentPosition{m->generatedAbsoluteFilePath,
	                            mapping->generatedPosition};
}

// GetDocumentPositionMapper — source_mapper.go:229
DocumentPositionMapper*
GetDocumentPositionMapper(Host* host, std::string_view generatedFileName) {
	std::string mapFileName = tryGetSourceMappingURL(host, generatedFileName);
	if (!mapFileName.empty()) {
		auto [base64Object, matched] = tryParseBase64Url(mapFileName);
		if (matched) {
			if (!base64Object.empty()) {
				auto decoded = base64Decode(base64Object);
				if (decoded.second) {
					return convertDocumentToSourceMapper(host, decoded.first,
					                                     generatedFileName);
				}
			}
			// Not a data URL we can parse, skip it
			mapFileName.clear();
		}
	}

	std::vector<std::string> possibleMapLocations;
	if (!mapFileName.empty()) {
		possibleMapLocations.push_back(mapFileName);
	}
	possibleMapLocations.push_back(std::string(generatedFileName) + ".map");
	for (const std::string& location : possibleMapLocations) {
		std::string mapFileName2 = tspath::getNormalizedAbsolutePath(
		    location, tspath::getDirectoryPath(generatedFileName));
		auto fileResult = host->ReadFile(mapFileName2);
		if (fileResult.second) {
			return convertDocumentToSourceMapper(host, fileResult.first,
			                                     mapFileName2);
		}
	}
	return nullptr;
}

} // namespace tsc::sourcemap
