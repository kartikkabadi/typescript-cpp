// Port of tsc/internal/stringutil/util.go — character classes, surrogate
// helpers, JS-string (CESU-8) rune encoding, BOM handling.
#pragma once

#include <cstdint>
#include <string_view>

namespace tsc {

inline bool isWhiteSpaceSingleLine(char32_t ch) {
	switch (ch) {
	case U' ': case U'\t': case U'\v': case U'\f':
	case 0x85:   // nextLine (Zs, whitespace but not an ECMAScript line break)
	case 0xA0:   // nonBreakingSpace
	case 0x1680: // ogham
	case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
	case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009:
	case 0x200A: // enQuad .. hairSpace
	case 0x200B: // zeroWidthSpace
	case 0x202F: // narrowNoBreakSpace
	case 0x205F: // mathematicalSpace
	case 0x3000: // ideographicSpace
	case 0xFEFF: // byteOrderMark
		return true;
	}
	return false;
}

inline bool isLineBreak(char32_t ch) {
	return ch == U'\n' || ch == U'\r' || ch == 0x2028 || ch == 0x2029;
}

inline bool isWhiteSpaceLike(char32_t ch) {
	return isWhiteSpaceSingleLine(ch) || isLineBreak(ch);
}

inline bool isDigit(char32_t ch) { return ch >= U'0' && ch <= U'9'; }
inline bool isOctalDigit(char32_t ch) { return ch >= U'0' && ch <= U'7'; }
inline bool isHexDigit(char32_t ch) {
	return (ch >= U'0' && ch <= U'9') || (ch >= U'a' && ch <= U'f') ||
	       (ch >= U'A' && ch <= U'F');
}
inline bool isASCIILetter(char32_t ch) {
	return (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z');
}

inline bool isHighSurrogate(char32_t ch) { return ch >= 0xD800 && ch <= 0xDBFF; }
inline bool isLowSurrogate(char32_t ch) { return ch >= 0xDC00 && ch <= 0xDFFF; }
inline bool isSurrogate(char32_t ch) { return ch >= 0xD800 && ch <= 0xDFFF; }

inline char32_t surrogatePairToCodePoint(char32_t hi, char32_t lo) {
	return 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
}

inline void codePointToSurrogatePair(char32_t cp, char32_t& hi, char32_t& lo) {
	cp -= 0x10000;
	hi = static_cast<char32_t>(0xD800) + (cp >> 10);
	lo = static_cast<char32_t>(0xDC00) + (cp & 0x3FF);
}

// EncodeJSStringRune: JS strings can contain lone surrogates; we store text as
// UTF-8, so lone surrogates encode as CESU-8 (3-byte 0xED..).
inline int encodeJSStringRune(char32_t r, char* out) {
	if (r < 0x80) {
		out[0] = static_cast<char>(r);
		return 1;
	}
	if (r < 0x800) {
		out[0] = static_cast<char>(0xC0 | (r >> 6));
		out[1] = static_cast<char>(0x80 | (r & 0x3F));
		return 2;
	}
	if (r >= 0xD800 && r <= 0xDFFF) {
		out[0] = static_cast<char>(0xE0 | (r >> 12));
		out[1] = static_cast<char>(0x80 | ((r >> 6) & 0x3F));
		out[2] = static_cast<char>(0x80 | (r & 0x3F));
		return 3;
	}
	if (r < 0x10000) {
		out[0] = static_cast<char>(0xE0 | (r >> 12));
		out[1] = static_cast<char>(0x80 | ((r >> 6) & 0x3F));
		out[2] = static_cast<char>(0x80 | (r & 0x3F));
		return 3;
	}
	out[0] = static_cast<char>(0xF0 | (r >> 18));
	out[1] = static_cast<char>(0x80 | ((r >> 12) & 0x3F));
	out[2] = static_cast<char>(0x80 | ((r >> 6) & 0x3F));
	out[3] = static_cast<char>(0x80 | (r & 0x3F));
	return 4;
}

inline int encodeRune(char32_t r, char* out) { return encodeJSStringRune(r, out); }

// DecodeJSStringRune decodes one rune from UTF-8/CESU-8 at s[i]; returns the
// code point and byte length. Mirrors Go's decode where invalid sequences
// yield RuneError (0xFFFD) width 1; CESU-8 surrogate halves decode as the
// surrogate code point.
inline constexpr char32_t kRuneError = 0xFFFD;
inline constexpr char32_t kRuneSelf = 0x80;

// Standard UTF-8 decode (matches Go's utf8.DecodeRuneInString semantics):
// returns the decoded rune and writes its byte width to *width. Invalid
// sequences decode to kRuneError with width 1.
inline char32_t decodeUtf8Rune(std::string_view s, int* width) {
	if (s.empty()) {
		*width = 0;
		return kRuneError;
	}
	auto b0 = static_cast<unsigned char>(s[0]);
	if (b0 < 0x80) {
		*width = 1;
		return b0;
	}
	int len = b0 < 0xC0 ? 1 : b0 < 0xE0 ? 2 : b0 < 0xF0 ? 3 : 4;
	if (len == 1) {
		*width = 1;
		return kRuneError;
	}
	if (s.size() < static_cast<size_t>(len)) {
		*width = 1;
		return kRuneError;
	}
	char32_t r = b0 & (0x7F >> len);
	for (int i = 1; i < len; i++) {
		auto bi = static_cast<unsigned char>(s[i]);
		if ((bi & 0xC0) != 0x80) {
			*width = 1;
			return kRuneError;
		}
		r = (r << 6) | (bi & 0x3F);
	}
	*width = len;
	return r;
}

inline char32_t decodeLastUtf8Rune(std::string_view s, int* width) {
	if (s.empty()) {
		*width = 0;
		return kRuneError;
	}
	size_t p = s.size() - 1;
	while (p > 0 && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80)
		p--;
	int w;
	char32_t r = decodeUtf8Rune(s.substr(p), &w);
	if (p + static_cast<size_t>(w) != s.size()) {
		*width = 1;
		return kRuneError;
	}
	*width = w;
	return r;
}

// Standard UTF-8 encode (Go's utf8.EncodeRune: surrogates -> U+FFFD).
inline int encodeUtf8Rune(char32_t r, char* out) {
	if (isSurrogate(r))
		r = kRuneError;
	auto u = static_cast<uint32_t>(r);
	if (u < 0x80) {
		out[0] = static_cast<char>(u);
		return 1;
	}
	if (u < 0x800) {
		out[0] = static_cast<char>(0xC0 | (u >> 6));
		out[1] = static_cast<char>(0x80 | (u & 0x3F));
		return 2;
	}
	if (u < 0x10000) {
		out[0] = static_cast<char>(0xE0 | (u >> 12));
		out[1] = static_cast<char>(0x80 | ((u >> 6) & 0x3F));
		out[2] = static_cast<char>(0x80 | (u & 0x3F));
		return 3;
	}
	out[0] = static_cast<char>(0xF0 | (u >> 18));
	out[1] = static_cast<char>(0x80 | ((u >> 12) & 0x3F));
	out[2] = static_cast<char>(0x80 | ((u >> 6) & 0x3F));
	out[3] = static_cast<char>(0x80 | (u & 0x3F));
	return 4;
}

inline std::string utf8String(char32_t r) {
	char buf[4];
	int n = encodeUtf8Rune(r, buf);
	return std::string(buf, n);
}


inline char32_t decodeJSStringRune(std::string_view s, size_t i, int* width) {
	const auto* p = reinterpret_cast<const unsigned char*>(s.data());
	const size_t n = s.size();
	unsigned char b0 = p[i];
	if (b0 < 0x80) {
		*width = 1;
		return b0;
	}
	if (b0 < 0xC0) {
		*width = 1;
		return kRuneError;
	}
	if (b0 < 0xE0) {
		if (i + 1 >= n || (p[i + 1] & 0xC0) != 0x80) {
			*width = 1;
			return kRuneError;
		}
		*width = 2;
		return ((b0 & 0x1F) << 6) | (p[i + 1] & 0x3F);
	}
	if (b0 < 0xF0) {
		if (i + 2 >= n || (p[i + 1] & 0xC0) != 0x80 || (p[i + 2] & 0xC0) != 0x80) {
			*width = 1;
			return kRuneError;
		}
		*width = 3;
		return ((b0 & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
	}
	if (b0 < 0xF8) {
		if (i + 3 >= n || (p[i + 1] & 0xC0) != 0x80 || (p[i + 2] & 0xC0) != 0x80 ||
		    (p[i + 3] & 0xC0) != 0x80) {
			*width = 1;
			return kRuneError;
		}
		*width = 4;
		return ((b0 & 0x07) << 18) | ((p[i + 1] & 0x3F) << 12) |
		       ((p[i + 2] & 0x3F) << 6) | (p[i + 3] & 0x3F);
	}
	*width = 1;
	return kRuneError;
}

// Removes a leading BOM; returns (textWithoutBOM, wasRemoved handled by caller).
inline std::string_view removeByteOrderMark(std::string_view text) {
	if (text.size() >= 3 &&
	    (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
	    (unsigned char)text[2] == 0xBF) {
		return text.substr(3);
	}
	if (text.size() >= 2 &&
	    (((unsigned char)text[0] == 0xFF && (unsigned char)text[1] == 0xFE) ||
	     ((unsigned char)text[0] == 0xFE && (unsigned char)text[1] == 0xFF))) {
		return text.substr(2);
	}
	return text;
}

}  // namespace tsc
