// Port of tsc/internal/core/text.go
#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "internal/stringutil/stringutil.h"

namespace tsc {

using TextPos = int32_t;
// === slice: sourcemap ===
// core.UTF16Offset — Go `type UTF16Offset int`; kept 32-bit like TextPos.
using UTF16Offset = int32_t;
// === end slice: sourcemap ===

struct TextRange {
	TextPos pos_{-1};
	TextPos end_{-1};

	constexpr TextPos pos() const { return pos_; }
	constexpr TextPos end() const { return end_; }
	constexpr TextPos len() const { return end_ - pos_; }
	constexpr bool contains(int pos) const { return pos >= pos_ && pos < end_; }
	constexpr bool containsInclusive(int pos) const { return pos >= pos_ && pos <= end_; }
	constexpr TextRange withPos(TextPos p) const { return {p, end_}; }
	constexpr TextRange withEnd(TextPos e) const { return {pos_, e}; }
	constexpr bool overlaps(TextRange o) const {
		return (pos_ < o.end() && end() > o.pos()) || (pos_ <= o.pos() && end() >= o.end());
	}
	constexpr bool intersects(TextRange o) const { return o.pos() < end() && o.end() > pos_; }

	constexpr bool containedBy(TextRange t2) const { return t2.pos() <= pos_ && t2.end() >= end_; }

	constexpr bool operator==(const TextRange&) const = default;

	static constexpr TextRange undefined() { return {-1, -1}; }
};

inline constexpr int compareTextRanges(TextRange a, TextRange b) {
	if (a.pos() != b.pos())
		return a.pos() - b.pos();
	return a.end() - b.end();
}

// Number of UTF-16 code units in the UTF-8 string up to byte offset `pos`.
// Mirrors core.UTF16Offset: code points above U+FFFF count as 2 units.
inline TextPos utf16Offset(std::string_view s, TextPos pos) {
	TextPos count = 0;
	const auto* p = reinterpret_cast<const unsigned char*>(s.data());
	for (TextPos i = 0; i < pos; ++count) {
		unsigned char c = p[i];
		if (c < 0x80) {
			i += 1;
		} else if (c < 0xE0) {
			i += 2;
		} else if (c < 0xF0) {
			i += 3;
		} else {
			i += 4;
			count += 1;  // astral character counts double
		}
	}
	return count;
}

inline TextPos utf16Len(std::string_view s) { return utf16Offset(s, static_cast<TextPos>(s.size())); }

using ECMALineStarts = std::vector<TextPos>;

// core.ComputeECMALineStarts — ECMAScript line starts (LF, CR, CRLF, LS, PS).
inline ECMALineStarts computeECMALineStarts(std::string_view text) {
	ECMALineStarts result;
	TextPos textLen = static_cast<TextPos>(text.size());
	TextPos pos = 0, lineStart = 0;
	while (pos < textLen) {
		auto b = static_cast<unsigned char>(text[pos]);
		if (b < 0x80) {
			pos++;
			if (b == '\r') {
				if (pos < textLen && text[pos] == '\n')
					pos++;
				result.push_back(lineStart);
				lineStart = pos;
			} else if (b == '\n') {
				result.push_back(lineStart);
				lineStart = pos;
			}
		} else {
			int size;
			char32_t ch = decodeUtf8Rune(text.substr(pos), &size);
			pos += static_cast<TextPos>(size);
			if (isLineBreak(ch)) {
				result.push_back(lineStart);
				lineStart = pos;
			}
		}
	}
	result.push_back(lineStart);
	return result;
}

}  // namespace tsc
