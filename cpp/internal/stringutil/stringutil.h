// Port of tsc/internal/stringutil/util.go — character classes, surrogate
// helpers, JS-string (CESU-8) rune encoding, BOM handling.
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

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
	if (i >= n) {
		// Go's DecodeRuneInString on an empty/short tail returns (RuneError, 0).
		*width = 0;
		return kRuneError;
	}
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

// ==== js_case.go: JS casing (Unicode 15.1.0, ICU root-locale semantics) ====

#include "internal/stringutil/js_case_generated.h"

namespace tsc::stringutil {

// DecodeJSStringRuneSize — width of one JS-string rune at s[i] (CESU-8 aware).
inline size_t DecodeJSStringRuneSize(std::string_view s) {
	int width = 0;
	decodeJSStringRune(s, 0, &width);
	return static_cast<size_t>(width);
}

inline char32_t DecodeJSStringRune(std::string_view s, int* width) {
	return decodeJSStringRune(s, 0, width);
}

inline bool IsHighSurrogate(char32_t r) { return isHighSurrogate(r); }
inline bool IsLowSurrogate(char32_t r) { return isLowSurrogate(r); }
inline bool IsSurrogate(char32_t r) { return isSurrogate(r); }
inline char32_t SurrogatePairToCodePoint(char32_t hi, char32_t lo) {
	return 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
}

// CombineSurrogatePairs canonicalizes a JS-string value produced by
// concatenation, merging any adjacent high+low surrogate sentinel pair into
// the single supplementary code point they represent.
inline std::string CombineSurrogatePairs(std::string_view s) {
	if (s.find('\xED') == std::string_view::npos) {
		return std::string(s);
	}
	std::string b;
	b.reserve(s.size());
	for (size_t i = 0; i < s.size();) {
		int size = 0;
		char32_t r = decodeJSStringRune(s, i, &size);
		if (IsHighSurrogate(r)) {
			int lowSize = 0;
			char32_t low = decodeJSStringRune(s, i + size, &lowSize);
			if (IsLowSurrogate(low)) {
				char buf[4];
				int n = encodeUtf8Rune(SurrogatePairToCodePoint(r, low), buf);
				b.append(buf, n);
				i += size + lowSize;
				continue;
			}
		}
		b.append(s.substr(i, size));
		i += size;
	}
	return b;
}



inline bool isUnicodeCased(char32_t r) {
	for (const Range32& rg : unicodeCasedRanges) {
		if (r < rg.lo) {
			break;
		}
		if (r <= rg.hi && ((r - rg.lo) % rg.stride) == 0) {
			return true;
		}
	}
	return false;
}

inline bool isUnicodeCaseIgnorable(char32_t r) {
	for (const Range32& rg : unicodeCaseIgnorableRanges) {
		if (r < rg.lo) {
			break;
		}
		if (r <= rg.hi && ((r - rg.lo) % rg.stride) == 0) {
			return true;
		}
	}
	return false;
}

inline bool isSigmaCased(char32_t r) { return isUnicodeCased(r); }

inline bool hasSigmaCasedAfter(std::string_view str, size_t start) {
	for (size_t i = start; i < str.size();) {
		int width = 0;
		char32_t r = decodeJSStringRune(str, i, &width);
		i += width;
		if (isUnicodeCaseIgnorable(r)) {
			continue;
		}
		return isSigmaCased(r);
	}
	return false;
}

// Final_Sigma context: preceded by a cased code point and not followed by one.
inline bool isFinalSigmaContext(bool casedBefore, std::string_view str, size_t afterOffset) {
	return casedBefore && !hasSigmaCasedAfter(str, afterOffset);
}

inline bool toLowerASCII(std::string_view str, std::string& out) {
	bool needsMapping = false;
	for (char ch : str) {
		if (static_cast<unsigned char>(ch) >= 0x80) {
			return false;
		}
		needsMapping = needsMapping || (ch >= 'A' && ch <= 'Z');
	}
	if (!needsMapping) {
		out = std::string(str);
		return true;
	}
	out = std::string(str);
	for (char& ch : out) {
		if (ch >= 'A' && ch <= 'Z') {
			ch = static_cast<char>(ch + ('a' - 'A'));
		}
	}
	return true;
}

inline bool toUpperASCII(std::string_view str, std::string& out) {
	bool needsMapping = false;
	for (char ch : str) {
		if (static_cast<unsigned char>(ch) >= 0x80) {
			return false;
		}
		needsMapping = needsMapping || (ch >= 'a' && ch <= 'z');
	}
	if (!needsMapping) {
		out = std::string(str);
		return true;
	}
	out = std::string(str);
	for (char& ch : out) {
		if (ch >= 'a' && ch <= 'z') {
			ch = static_cast<char>(ch - ('a' - 'A'));
		}
	}
	return true;
}

inline std::string ToLowerJS(std::string_view str) {
	std::string ascii;
	if (toLowerASCII(str, ascii)) {
		return ascii;
	}
	std::string builder;
	builder.reserve(str.size());
	// casedBefore tracks whether the most recent non-Case_Ignorable code point is
	// "cased", the backward half of the Final_Sigma context.
	bool casedBefore = false;
	for (size_t i = 0; i < str.size();) {
		int size = 0;
		char32_t r = decodeJSStringRune(str, i, &size);
		i += size;
		if (IsSurrogate(r)) {
			// A lone surrogate has no case mapping; preserve it verbatim.
			char buf[4];
			int n = encodeJSStringRune(r, buf);
			builder.append(buf, n);
		} else if (auto it = specialCasingMappings.find(r); it != specialCasingMappings.end()) {
			const SpecialCasingMapping& mapping = it->second;
			if (mapping.condition == SpecialCasingCondition::FinalSigma &&
				isFinalSigmaContext(casedBefore, str, i)) {
				builder += mapping.conditionalLower;
			} else {
				builder += mapping.lower;
			}
		} else {
			char buf[4];
			int n = encodeUtf8Rune(r, buf);
			builder.append(buf, n);
		}
		if (!isUnicodeCaseIgnorable(r)) {
			casedBefore = isSigmaCased(r);
		}
	}
	return builder;
}

inline std::string ToUpperJS(std::string_view str) {
	std::string ascii;
	if (toUpperASCII(str, ascii)) {
		return ascii;
	}
	std::string builder;
	builder.reserve(str.size());
	for (size_t i = 0; i < str.size();) {
		int size = 0;
		char32_t r = decodeJSStringRune(str, i, &size);
		if (IsSurrogate(r)) {
			// A lone surrogate has no case mapping; copy the sentinel bytes directly.
			builder.append(str.substr(i, size));
		} else if (auto it = specialCasingMappings.find(r); it != specialCasingMappings.end()) {
			builder += it->second.upper;
		} else {
			char buf[4];
			int n = encodeUtf8Rune(r, buf);
			builder.append(buf, n);
		}
		i += size;
	}
	return builder;
}

// ==== compare.go — case-insensitive comparison helpers ====

// toLowerRune — Go's unicode.ToLower: the simple (1:1) lowercase mapping of a
// rune. Multi-code-point special-casing mappings are left unchanged, matching
// Go's simple case mapping semantics.
inline char32_t toLowerRune(char32_t r) {
	if (auto it = specialCasingMappings.find(r); it != specialCasingMappings.end()) {
		const char* lower = it->second.lower;
		if (lower != nullptr) {
			int w = 0;
			char32_t lr = decodeUtf8Rune(std::string_view(lower), &w);
			if (static_cast<size_t>(w) == std::char_traits<char>::length(lower)) {
				return lr;
			}
		}
	}
	return r;
}

// toUpperRune — Go's unicode.ToUpper: simple uppercase mapping.
inline char32_t toUpperRune(char32_t r) {
	if (auto it = specialCasingMappings.find(r); it != specialCasingMappings.end()) {
		const char* upper = it->second.upper;
		if (upper != nullptr) {
			int w = 0;
			char32_t ur = decodeUtf8Rune(std::string_view(upper), &w);
			if (static_cast<size_t>(w) == std::char_traits<char>::length(upper)) {
				return ur;
			}
		}
	}
	return r;
}

// simpleFold — Go's unicode.SimpleFold: the next rune in the case-folding
// orbit of r. Implemented via the alternating upper/lower orbit, which is
// equivalent for all orbits whose members are simple (1:1) mappings.
inline char32_t simpleFold(char32_t r) {
	// Orbit through alternating upper/lower mappings: for fold orbits of
	// size >= 3 ({Σ, σ, ς}, {K, k, KELVIN}, {ſ, S, s}, ...) starting at any
	// member reaches every member. For size 2 it ping-pongs. Return the
	// smallest rune in the orbit as the canonical fold key — comparing fold
	// keys is equivalent to comparing orbits.
	char32_t best = r;
	char32_t cur = r;
	for (int i = 0; i < 8; i++) {
		cur = (i % 2 == 0) ? toUpperRune(cur) : toLowerRune(cur);
		if (cur < best) best = cur;
		if (cur == r && i > 0) break;
	}
	return best;
}

// EquateStringCaseInsensitive — Go's strings.EqualFold.
inline bool EquateStringCaseInsensitive(std::string_view a, std::string_view b) {
	if (a == b) return true;
	while (true) {
		int sa = 0, sb = 0;
		char32_t ca = decodeUtf8Rune(a, &sa);
		char32_t cb = decodeUtf8Rune(b, &sb);
		if (sa == 0 && sb == 0) return true;
		if (sa == 0) return false;
		if (sb == 0) return false;
		a = a.substr(sa);
		b = b.substr(sb);
		if (ca != cb && simpleFold(ca) != simpleFold(cb)) return false;
	}
}

inline bool EquateStringCaseSensitive(std::string_view a, std::string_view b) {
	return a == b;
}

inline std::function<bool(std::string_view, std::string_view)>
GetStringEqualityComparer(bool ignoreCase) {
	if (ignoreCase) {
		return [](std::string_view a, std::string_view b) {
			return EquateStringCaseInsensitive(a, b);
		};
	}
	return [](std::string_view a, std::string_view b) {
		return EquateStringCaseSensitive(a, b);
	};
}

using Comparison = int;
inline constexpr Comparison ComparisonLessThan = -1;
inline constexpr Comparison ComparisonEqual = 0;
inline constexpr Comparison ComparisonGreaterThan = 1;

// CompareStringsCaseInsensitive — Go's per-rune unicode.ToLower comparison.
inline Comparison CompareStringsCaseInsensitive(std::string_view a,
                                                std::string_view b) {
	if (a == b) return ComparisonEqual;
	while (true) {
		int sa = 0, sb = 0;
		char32_t ca = decodeUtf8Rune(a, &sa);
		char32_t cb = decodeUtf8Rune(b, &sb);
		if (sa == 0) {
			if (sb == 0) return ComparisonEqual;
			return ComparisonLessThan;
		}
		if (sb == 0) return ComparisonGreaterThan;
		char32_t lca = toLowerRune(ca);
		char32_t lcb = toLowerRune(cb);
		if (lca != lcb) {
			if (lca < lcb) return ComparisonLessThan;
			return ComparisonGreaterThan;
		}
		a = a.substr(sa);
		b = b.substr(sb);
	}
}

inline Comparison CompareStringsCaseSensitive(std::string_view a,
                                              std::string_view b) {
	if (a < b) return ComparisonLessThan;
	if (a > b) return ComparisonGreaterThan;
	return ComparisonEqual;
}

inline std::function<Comparison(std::string_view, std::string_view)>
GetStringComparer(bool ignoreCase) {
	if (ignoreCase) {
		return [](std::string_view a, std::string_view b) {
			return CompareStringsCaseInsensitive(a, b);
		};
	}
	return [](std::string_view a, std::string_view b) {
		return CompareStringsCaseSensitive(a, b);
	};
}

inline bool HasPrefix(std::string_view s, std::string_view prefix,
                      bool caseSensitive) {
	if (caseSensitive) {
		return s.starts_with(prefix);
	}
	if (prefix.size() > s.size()) return false;
	return EquateStringCaseInsensitive(s.substr(0, prefix.size()), prefix);
}

inline bool HasSuffix(std::string_view s, std::string_view suffix,
                      bool caseSensitive) {
	if (caseSensitive) {
		return s.ends_with(suffix);
	}
	if (suffix.size() > s.size()) return false;
	return EquateStringCaseInsensitive(s.substr(s.size() - suffix.size()),
	                                   suffix);
}

inline bool HasPrefixAndSuffixWithoutOverlap(std::string_view s,
                                             std::string_view prefix,
                                             std::string_view suffix,
                                             bool caseSensitive) {
	if (prefix.size() + suffix.size() > s.size()) return false;
	return HasPrefix(s, prefix, caseSensitive) &&
	       HasSuffix(s, suffix, caseSensitive);
}

inline Comparison CompareStringsCaseInsensitiveThenSensitive(
    std::string_view a, std::string_view b) {
	int cmp = CompareStringsCaseInsensitive(a, b);
	if (cmp != ComparisonEqual) return cmp;
	return CompareStringsCaseSensitive(a, b);
}

}  // namespace tsc::stringutil

// splitLines (stringutil/util.go:87).
inline std::vector<std::string_view> splitLines(std::string_view text) {
	std::vector<std::string_view> lines;
	size_t start = 0;
	size_t pos = 0;
	while (pos < text.size()) {
		char c = text[pos];
		if (c == '\r') {
			if (pos + 1 < text.size() && text[pos + 1] == '\n') {
				lines.push_back(text.substr(start, pos - start));
				pos += 2;
				start = pos;
				continue;
			}
		}
		if (c == '\n' || c == '\r') {
			lines.push_back(text.substr(start, pos - start));
			pos++;
			start = pos;
			continue;
		}
		pos++;
	}
	if (start < text.size()) {
		lines.push_back(text.substr(start));
	}
	return lines;
}

// guessIndentation (stringutil/util.go:115).
inline int guessIndentation(const std::vector<std::string_view>& lines) {
	constexpr int kMaxSmiX86 = 0x3fffffff;
	int indentation = kMaxSmiX86;
	for (auto line : lines) {
		if (line.empty()) {
			continue;
		}
		size_t i = 0;
		while (i < line.size() && (int)i < indentation) {
			int width = 0;
			char32_t ch = tsc::decodeUtf8Rune(line.substr(i), &width);
			if (!tsc::isWhiteSpaceLike(ch)) {
				break;
			}
			i += width;
		}
		if ((int)i < indentation) {
			indentation = (int)i;
		}
		if (indentation == 0) {
			return 0;
		}
	}
	return indentation == kMaxSmiX86 ? 0 : indentation;
}

// trimSpace — strings.TrimSpace equivalent for ASCII whitespace.
inline std::string_view trimSpace(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
	                      s.front() == '\n' || s.front() == '\r' ||
	                      s.front() == '\v' || s.front() == '\f')) {
		s.remove_prefix(1);
	}
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
	                      s.back() == '\n' || s.back() == '\r' ||
	                      s.back() == '\v' || s.back() == '\f')) {
		s.remove_suffix(1);
	}
	return s;
}
