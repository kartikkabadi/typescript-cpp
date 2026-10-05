// Port of tsc/internal/sourcemap/decoder.go
#include "internal/sourcemap/sourcemap.h"

namespace tsc::sourcemap {
namespace {

// base64FormatDecode — decoder.go:238
int base64FormatDecode(char ch) {
	if (ch >= 'A' && ch <= 'Z') {
		return static_cast<int>(ch - 'A');
	}
	if (ch >= 'a' && ch <= 'z') {
		return static_cast<int>(ch - 'a' + 26);
	}
	if (ch >= '0' && ch <= '9') {
		return static_cast<int>(ch - '0' + 52);
	}
	if (ch == '+') {
		return 62;
	}
	if (ch == '/') {
		return 63;
	}
	return -1;
}

} // namespace

// Mapping.Equals — decoder.go:19
bool Mapping::Equals(const Mapping* other) const {
	return this == other ||
	       (GeneratedLine == other->GeneratedLine &&
	        GeneratedCharacter == other->GeneratedCharacter &&
	        SourceIndex == other->SourceIndex &&
	        SourceLine == other->SourceLine &&
	        SourceCharacter == other->SourceCharacter &&
	        NameIndex == other->NameIndex);
}

// Mapping.IsSourceMapping — decoder.go:28
bool Mapping::IsSourceMapping() const {
	return SourceIndex != MissingSource && SourceLine != MissingLineOrColumn &&
	       SourceCharacter != MissingUTF16Column;
}

// DecodeMappings — decoder.go:55
MappingsDecoder* DecodeMappings(std::string_view mappings) {
	return new MappingsDecoder(mappings);
}

// State — decoder.go:71
Mapping* MappingsDecoder::State() {
	return captureMapping(/*hasSource*/ true, /*hasName*/ true);
}

// Next — decoder.go:85. Returns {value, done}.
std::pair<Mapping*, bool> MappingsDecoder::Next() {
	while (!done && pos < static_cast<int>(mappings.size())) {
		char ch = mappings[pos];
		if (ch == ';') {
			// new line
			generatedLine++;
			generatedCharacter = 0;
			pos++;
			continue;
		}

		if (ch == ',') {
			// Next entry is on same line - no action needed
			pos++;
			continue;
		}

		bool hasSource = false;
		bool hasName = false;
		// d.generatedCharacter += core.UTF16Offset(d.base64VLQFormatDecode())
		generatedCharacter += base64VLQFormatDecode();
		if (hasReportedError()) {
			return stopIterating();
		}
		if (generatedCharacter < 0) {
			return setErrorAndStopIterating("Invalid generatedCharacter found");
		}

		if (!isSourceMappingSegmentEnd()) {
			hasSource = true;

			sourceIndex += base64VLQFormatDecode();
			if (hasReportedError()) {
				return stopIterating();
			}
			if (sourceIndex < 0) {
				return setErrorAndStopIterating("Invalid sourceIndex found");
			}
			if (isSourceMappingSegmentEnd()) {
				return setErrorAndStopIterating(
				    "Unsupported Format: No entries after sourceIndex");
			}

			sourceLine += base64VLQFormatDecode();
			if (hasReportedError()) {
				return stopIterating();
			}
			if (sourceLine < 0) {
				return setErrorAndStopIterating("Invalid sourceLine found");
			}
			if (isSourceMappingSegmentEnd()) {
				return setErrorAndStopIterating(
				    "Unsupported Format: No entries after sourceLine");
			}

			sourceCharacter += base64VLQFormatDecode();
			if (hasReportedError()) {
				return stopIterating();
			}
			if (sourceCharacter < 0) {
				return setErrorAndStopIterating(
				    "Invalid sourceCharacter found");
			}

			if (!isSourceMappingSegmentEnd()) {
				hasName = true;
				nameIndex += base64VLQFormatDecode();
				if (hasReportedError()) {
					return stopIterating();
				}
				if (nameIndex < 0) {
					return setErrorAndStopIterating(
					    "Invalid nameIndex found");
				}

				if (!isSourceMappingSegmentEnd()) {
					return setErrorAndStopIterating(
					    "Unsupported Error Format: Entries after "
					    "nameIndex");
				}
			}
		}

		return {captureMapping(hasSource, hasName), false};
	}

	return stopIterating();
}

// captureMapping — decoder.go:167
Mapping* MappingsDecoder::captureMapping(bool hasSource, bool hasName) {
	Mapping* mapping = mappingArena.alloc<Mapping>();
	mapping->GeneratedLine = generatedLine;
	mapping->GeneratedCharacter = static_cast<UTF16Offset>(generatedCharacter);
	mapping->SourceIndex =
	    hasSource ? static_cast<SourceIndex>(sourceIndex) : MissingSource;
	mapping->SourceLine =
	    hasSource ? static_cast<int>(sourceLine) : MissingLineOrColumn;
	mapping->SourceCharacter =
	    hasSource ? static_cast<UTF16Offset>(sourceCharacter)
	              : MissingUTF16Column;
	mapping->NameIndex =
	    hasName ? static_cast<NameIndex>(nameIndex) : MissingName;
	return mapping;
}

// stopIterating — decoder.go:178
std::pair<Mapping*, bool> MappingsDecoder::stopIterating() {
	done = true;
	return {nullptr, true};
}

// setError — decoder.go:183
void MappingsDecoder::setError(std::string_view err) { error = err; }

// setErrorAndStopIterating — decoder.go:187
std::pair<Mapping*, bool>
MappingsDecoder::setErrorAndStopIterating(std::string_view err) {
	setError(err);
	return stopIterating();
}

// isSourceMappingSegmentEnd — decoder.go:196
bool MappingsDecoder::isSourceMappingSegmentEnd() const {
	return pos == static_cast<int>(mappings.size()) || mappings[pos] == ',' ||
	       mappings[pos] == ';';
}

// base64VLQFormatDecode — decoder.go:200
int64_t MappingsDecoder::base64VLQFormatDecode() {
	bool moreDigits = true;
	int shiftCount = 0;
	int64_t value = 0;
	for (; moreDigits; pos++) {
		if (pos >= static_cast<int>(mappings.size())) {
			setError(
			    "Error in decoding base64VLQFormatDecode, past the mapping "
			    "string");
			return -1;
		}

		// 6 digit number
		int currentByte = base64FormatDecode(mappings[pos]);
		if (currentByte == -1) {
			setError("Invalid character in VLQ");
			return -1;
		}

		// If msb is set, we still have more bits to continue
		moreDigits = (currentByte & 32) != 0;

		// least significant 5 bits are the next msbs in the final value.
		// (Go shifts an int by shiftCount unconditionally; a shiftCount >= 64
		// needs an input longer than any real mapping and yields 0 in Go —
		// mirrored here without UB.)
		value = value | (shiftCount < 64
		                     ? (static_cast<int64_t>(currentByte & 31)
		                        << shiftCount)
		                     : 0);
		shiftCount += 5;
	}

	// Least significant bit if 1 represents negative and rest of the msb is actual absolute value
	if ((value & 1) == 0) {
		// + number
		value = value >> 1;
	} else {
		// - number
		value = value >> 1;
		value = -value;
	}

	return value;
}

} // namespace tsc::sourcemap
