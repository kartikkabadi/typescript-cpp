// Port of tsc/internal/sourcemap/generator.go
#include "internal/sourcemap/sourcemap.h"

#include <cstdint>

namespace tsc::sourcemap {
namespace {

// base64FormatEncode — generator.go:372
char32_t base64FormatEncode(int64_t value) {
	if (value >= 0 && value < 26) {
		return U'A' + static_cast<char32_t>(value);
	}
	if (value >= 26 && value < 52) {
		return U'a' + static_cast<char32_t>(value) - 26;
	}
	if (value >= 52 && value < 62) {
		return U'0' + static_cast<char32_t>(value) - 52;
	}
	if (value == 62) {
		return U'+';
	}
	if (value == 63) {
		return U'/';
	}
	TSC_UNREACHABLE("not a base64 value");
}

// jsonWriteString — writes a JSON string literal exactly like Go's
// encoding/json/v2 (jsontext) with the package's default AllowInvalidUTF8
// option: only `"`, `\` and control characters < 0x20 are escaped (\b \f \n
// \r \t short forms, otherwise \u00xx with lowercase hex); all other bytes —
// including invalid UTF-8 — pass through verbatim.
void jsonWriteString(std::string& out, std::string_view s) {
	static constexpr char hexDigits[] = "0123456789abcdef";
	out += '"';
	for (char ch : s) {
		auto c = static_cast<unsigned char>(ch);
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				out += "\\u00";
				out += hexDigits[c >> 4];
				out += hexDigits[c & 0xF];
			} else {
				out += static_cast<char>(c);
			}
		}
	}
	out += '"';
}

void jsonWriteStringArray(std::string& out,
                          const std::vector<std::string>& values) {
	out += '[';
	for (size_t i = 0; i < values.size(); i++) {
		if (i != 0) {
			out += ',';
		}
		jsonWriteString(out, values[i]);
	}
	out += ']';
}

// marshalRawSourceMap — json.Marshal of RawSourceMap (compact, fields in
// struct-declaration order, sourcesContent omitted when not present).
std::string marshalRawSourceMap(const RawSourceMap& m) {
	std::string out;
	out += "{\"version\":";
	out += std::to_string(m.Version);
	out += ",\"file\":";
	jsonWriteString(out, m.File);
	// json:"sourceRoot,omitzero" — omitted when empty.
	if (!m.SourceRoot.empty()) {
		out += ",\"sourceRoot\":";
		jsonWriteString(out, m.SourceRoot);
	}
	out += ",\"sources\":";
	jsonWriteStringArray(out, m.Sources);
	out += ",\"names\":";
	jsonWriteStringArray(out, m.Names);
	out += ",\"mappings\":";
	jsonWriteString(out, m.Mappings);
	if (m.SourcesContent.has_value()) {
		out += ",\"sourcesContent\":[";
		const auto& contents = *m.SourcesContent;
		for (size_t i = 0; i < contents.size(); i++) {
			if (i != 0) {
				out += ',';
			}
			if (contents[i].has_value()) {
				jsonWriteString(out, *contents[i]);
			} else {
				out += "null";
			}
		}
		out += ']';
	}
	out += '}';
	return out;
}

// base64Encode — encoding/base64 StdEncoding.EncodeToString (with padding).
std::string base64Encode(std::string_view data) {
	static constexpr char enc[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 3 <= data.size(); i += 3) {
		uint32_t v = (static_cast<uint32_t>(
		                  static_cast<unsigned char>(data[i]))
		              << 16) |
		             (static_cast<uint32_t>(
		                  static_cast<unsigned char>(data[i + 1]))
		              << 8) |
		             static_cast<uint32_t>(
		                 static_cast<unsigned char>(data[i + 2]));
		out += enc[v >> 18];
		out += enc[(v >> 12) & 63];
		out += enc[(v >> 6) & 63];
		out += enc[v & 63];
	}
	size_t rem = data.size() - i;
	if (rem == 1) {
		uint32_t v = static_cast<uint32_t>(
		                 static_cast<unsigned char>(data[i]))
		             << 16;
		out += enc[v >> 18];
		out += enc[(v >> 12) & 63];
		out += '=';
		out += '=';
	} else if (rem == 2) {
		uint32_t v = (static_cast<uint32_t>(
		                  static_cast<unsigned char>(data[i]))
		              << 16) |
		             (static_cast<uint32_t>(
		                  static_cast<unsigned char>(data[i + 1]))
		              << 8);
		out += enc[v >> 18];
		out += enc[(v >> 12) & 63];
		out += enc[(v >> 6) & 63];
		out += '=';
	}
	return out;
}

} // namespace

// NewGenerator — generator.go:66
Generator* NewGenerator(std::string_view file, std::string_view sourceRoot,
                        std::string_view sourcesDirectoryPath,
                        const tspath::ComparePathsOptions& options) {
	auto* gen = new Generator();
	gen->file = file;
	gen->sourceRoot = sourceRoot;
	gen->sourcesDirectoryPath = sourcesDirectoryPath;
	gen->pathOptions = options;
	return gen;
}

// AddSource — generator.go:78
SourceIndex Generator::AddSource(std::string_view fileName) {
	std::string source = tspath::getRelativePathToDirectoryOrUrl(
	    sourcesDirectoryPath, fileName,
	    /*isAbsolutePathAnUrl*/ true, pathOptions);

	SourceIndex sourceIndex;
	auto it = sourceToSourceIndexMap.find(source);
	if (it == sourceToSourceIndexMap.end()) {
		sourceIndex = static_cast<SourceIndex>(sources.size());
		sources.push_back(source);
		rawSources.emplace_back(fileName);
		sourceToSourceIndexMap[source] = sourceIndex;
	} else {
		sourceIndex = it->second;
	}
	return sourceIndex;
}

// SetSourceContent — generator.go:101
gostd::Error Generator::SetSourceContent(SourceIndex sourceIndex,
                                std::string_view content) {
	if (sourceIndex < 0 ||
	    sourceIndex >= static_cast<SourceIndex>(sources.size())) {
		return gostd::newError("sourceIndex is out of range");
	}
	while (sourcesContent.size() <= static_cast<size_t>(sourceIndex)) {
		sourcesContent.emplace_back(std::nullopt);
	}
	sourcesContent[static_cast<size_t>(sourceIndex)] = std::string(content);
	return nullptr;
}

// AddName — generator.go:113
NameIndex Generator::AddName(std::string_view name) {
	NameIndex nameIndex;
	auto it = nameToNameIndexMap.find(std::string(name));
	if (it == nameToNameIndexMap.end()) {
		nameIndex = static_cast<NameIndex>(names.size());
		names.emplace_back(name);
		nameToNameIndexMap[std::string(name)] = nameIndex;
	} else {
		nameIndex = it->second;
	}
	return nameIndex;
}

// isNewGeneratedPosition — generator.go:126
bool Generator::isNewGeneratedPosition(int generatedLine,
                                       UTF16Offset generatedCharacter) const {
	return !hasPending || pendingGeneratedLine != generatedLine ||
	       pendingGeneratedCharacter != generatedCharacter;
}

// isBacktrackingSourcePosition — generator.go:132
bool Generator::isBacktrackingSourcePosition(
    SourceIndex sourceIndex, int sourceLine,
    UTF16Offset sourceCharacter) const {
	return sourceIndex != sourceIndexNotSet && sourceLine != notSet &&
	       sourceCharacter != notSetUTF16 &&
	       pendingSourceIndex == sourceIndex &&
	       (pendingSourceLine > sourceLine ||
	        (pendingSourceLine == sourceLine &&
	         pendingSourceCharacter > sourceCharacter));
}

// shouldCommitMapping — generator.go:141
bool Generator::shouldCommitMapping() const {
	return hasPending &&
	       (!hasLast || lastGeneratedLine != pendingGeneratedLine ||
	        lastGeneratedCharacter != pendingGeneratedCharacter ||
	        lastSourceIndex != pendingSourceIndex ||
	        lastSourceLine != pendingSourceLine ||
	        lastSourceCharacter != pendingSourceCharacter ||
	        lastNameIndex != pendingNameIndex);
}

// appendMappingCharCode — generator.go:151 (strings.Builder.WriteRune; every
// code point written here is ASCII so it appends a single byte)
void Generator::appendMappingCharCode(char32_t charCode) {
	mappings += static_cast<char>(charCode);
}

// appendBase64VLQ — generator.go:155
void Generator::appendBase64VLQ(int64_t inValue) {
	// Add a new least significant bit that has the sign of the value.
	// if negative number the least significant bit that gets added to the number has value 1
	// else least significant bit value that gets added is 0
	// eg. -1 changes to binary : 01 [1] => 3
	//     +1 changes to binary : 01 [0] => 2
	if (inValue < 0) {
		inValue = ((-inValue) << 1) + 1;
	} else {
		inValue = inValue << 1;
	}

	// Encode 5 bits at a time starting from least significant bits
	for (;;) {
		int64_t currentDigit = inValue & 31; // 11111
		inValue = inValue >> 5;
		if (inValue > 0) {
			// There are still more digits to decode, set the msb (6th bit)
			currentDigit = currentDigit | 32;
		}
		appendMappingCharCode(base64FormatEncode(currentDigit));
		if (inValue <= 0) {
			break;
		}
	}
}

// commitPendingMapping — generator.go:182
void Generator::commitPendingMapping() {
	if (!shouldCommitMapping()) {
		return;
	}

	// Line/Comma delimiters
	if (lastGeneratedLine < pendingGeneratedLine) {
		// Emit line delimiters
		for (;;) {
			appendMappingCharCode(';');
			lastGeneratedLine++;
			if (lastGeneratedLine >= pendingGeneratedLine) {
				break;
			}
		}
		// Only need to set this once
		lastGeneratedCharacter = 0;
	} else {
		if (lastGeneratedLine != pendingGeneratedLine) {
			// panic rather than error as an invariant has been violated
			TSC_UNREACHABLE("generatedLine cannot backtrack");
		}
		// Emit comma to separate the entry
		if (hasLast) {
			appendMappingCharCode(',');
		}
	}

	// 1. Relative generated character
	appendBase64VLQ(static_cast<int64_t>(pendingGeneratedCharacter) -
	                static_cast<int64_t>(lastGeneratedCharacter));
	lastGeneratedCharacter = pendingGeneratedCharacter;

	if (hasPendingSource) {
		// 2. Relative sourceIndex
		appendBase64VLQ(static_cast<int64_t>(pendingSourceIndex) -
		                static_cast<int64_t>(lastSourceIndex));
		lastSourceIndex = pendingSourceIndex;

		// 3. Relative source line
		appendBase64VLQ(static_cast<int64_t>(pendingSourceLine) -
		                static_cast<int64_t>(lastSourceLine));
		lastSourceLine = pendingSourceLine;

		// 4. Relative source character
		appendBase64VLQ(static_cast<int64_t>(pendingSourceCharacter) -
		                static_cast<int64_t>(lastSourceCharacter));
		lastSourceCharacter = pendingSourceCharacter;

		if (hasPendingName) {
			// 5. Relative nameIndex
			appendBase64VLQ(static_cast<int64_t>(pendingNameIndex) -
			                static_cast<int64_t>(lastNameIndex));
			lastNameIndex = pendingNameIndex;
		}
	}

	hasLast = true;
}

// addMapping — generator.go:237
void Generator::addMapping(int generatedLine, UTF16Offset generatedCharacter,
                           SourceIndex sourceIndex, int sourceLine,
                           UTF16Offset sourceCharacter, NameIndex nameIndex) {
	if (isNewGeneratedPosition(generatedLine, generatedCharacter) ||
	    isBacktrackingSourcePosition(sourceIndex, sourceLine,
	                                 sourceCharacter)) {
		commitPendingMapping();
		pendingGeneratedLine = generatedLine;
		pendingGeneratedCharacter = generatedCharacter;
		hasPendingSource = false;
		hasPendingName = false;
		hasPending = true;
	}

	if (sourceIndex != sourceIndexNotSet && sourceLine != notSet &&
	    sourceCharacter != notSetUTF16) {
		pendingSourceIndex = sourceIndex;
		pendingSourceLine = sourceLine;
		pendingSourceCharacter = sourceCharacter;
		hasPendingSource = true;
		if (nameIndex != nameIndexNotSet) {
			pendingNameIndex = nameIndex;
			hasPendingName = true;
		}
	}
}

// AddGeneratedMapping — generator.go:261
gostd::Error Generator::AddGeneratedMapping(int generatedLine,
                                   UTF16Offset generatedCharacter) {
	if (generatedLine < pendingGeneratedLine) {
		return gostd::newError("generatedLine cannot backtrack");
	}
	if (generatedCharacter < 0) {
		return gostd::newError("generatedCharacter cannot be negative");
	}
	addMapping(generatedLine, generatedCharacter, sourceIndexNotSet,
	           /*sourceLine*/ notSet, /*sourceCharacter*/ notSetUTF16,
	           nameIndexNotSet);
	hasPendingSource = false;
	hasPendingName = false;
	return nullptr;
}

// AddSourceMapping — generator.go:275
gostd::Error Generator::AddSourceMapping(int generatedLine,
                                UTF16Offset generatedCharacter,
                                SourceIndex sourceIndex, int sourceLine,
                                UTF16Offset sourceCharacter) {
	if (generatedLine < pendingGeneratedLine) {
		return gostd::newError("generatedLine cannot backtrack");
	}
	if (generatedCharacter < 0) {
		return gostd::newError("generatedCharacter cannot be negative");
	}
	if (sourceIndex < 0 ||
	    sourceIndex >= static_cast<SourceIndex>(sources.size())) {
		return gostd::newError("sourceIndex is out of range");
	}
	if (sourceLine < 0) {
		return gostd::newError("sourceLine cannot be negative");
	}
	if (sourceCharacter < 0) {
		return gostd::newError("sourceCharacter cannot be negative");
	}
	if (hasPending &&
	    !isNewGeneratedPosition(generatedLine, generatedCharacter) &&
	    !hasPendingSource) {
		return nullptr;
	}
	addMapping(generatedLine, generatedCharacter, sourceIndex, sourceLine,
	           sourceCharacter, nameIndexNotSet);
	return nullptr;
}

// AddNamedSourceMapping — generator.go:299
gostd::Error Generator::AddNamedSourceMapping(int generatedLine,
                                     UTF16Offset generatedCharacter,
                                     SourceIndex sourceIndex, int sourceLine,
                                     UTF16Offset sourceCharacter,
                                     NameIndex nameIndex) {
	if (generatedLine < pendingGeneratedLine) {
		return gostd::newError("generatedLine cannot backtrack");
	}
	if (generatedCharacter < 0) {
		return gostd::newError("generatedCharacter cannot be negative");
	}
	if (sourceIndex < 0 ||
	    sourceIndex >= static_cast<SourceIndex>(sources.size())) {
		return gostd::newError("sourceIndex is out of range");
	}
	if (sourceLine < 0) {
		return gostd::newError("sourceLine cannot be negative");
	}
	if (sourceCharacter < 0) {
		return gostd::newError("sourceCharacter cannot be negative");
	}
	if (nameIndex < 0 ||
	    nameIndex >= static_cast<NameIndex>(names.size())) {
		return gostd::newError("nameIndex is out of range");
	}
	if (hasPending &&
	    !isNewGeneratedPosition(generatedLine, generatedCharacter) &&
	    !hasPendingSource) {
		return nullptr;
	}
	addMapping(generatedLine, generatedCharacter, sourceIndex, sourceLine,
	           sourceCharacter, nameIndex);
	return nullptr;
}

// RawSourceMap — generator.go:326
struct RawSourceMap* Generator::RawSourceMap() {
	commitPendingMapping();
	// slices.Clone nil-or-empty normalization: sources/names marshal as []
	std::vector<std::string> sourcesClone = sources;
	std::vector<std::string> namesClone = names;
	auto* rsm = new ::tsc::sourcemap::RawSourceMap();
	rsm->Version = 3;
	rsm->File = file;
	rsm->SourceRoot = sourceRoot;
	rsm->Sources = std::move(sourcesClone);
	rsm->Names = std::move(namesClone);
	rsm->Mappings = mappings;
	// slices.Clone(gen.sourcesContent): nil (never appended) stays nil, and the
	// `omitzero` tag omits the field; Go's slice is only non-nil once appended.
	if (!sourcesContent.empty()) {
		rsm->SourcesContent = sourcesContent;
	}
	return rsm;
}

// bytes — generator.go:347 (json.Marshal of the RawSourceMap)
std::string Generator::bytes() {
	return marshalRawSourceMap(*RawSourceMap());
}

// String — generator.go:356
std::string Generator::String() { return bytes(); }

// Base64DataURL — generator.go:360
std::string Generator::Base64DataURL() {
	const std::string_view prefix = "data:application/json;base64,";
	std::string data = bytes();
	std::string sb;
	sb.reserve(prefix.size() + (data.size() + 2) / 3 * 4);
	sb.append(prefix);
	sb.append(base64Encode(data));
	return sb;
}

} // namespace tsc::sourcemap
