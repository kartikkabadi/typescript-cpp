// === slice: ls-foundation ===
// Unicode lookup helpers for lsutil, generated from the exact oracle
// toolchain (Go x/text v0.42.0 norm + go1.27 unicode tables / UCD 17) so
// norm.NFD, unicode.Is(unicode.Mn), unicode.IsUpper, unicode.ToLower and
// unicode.ToUpper behave bit-identically.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "internal/stringutil/stringutil.h"

namespace tsc::ls::lsutil::detail::tables {
#include "internal/ls/lsutil/unicode_tables.inc"
} // namespace tsc::ls::lsutil::detail::tables

namespace tsc::ls::lsutil::detail {

using tables::canonicalCombiningClassTable;
using tables::canonicalDecompPool;
using tables::canonicalDecompTable;
using tables::unicodeMnTable;
using tables::unicodeSimpleLowerTable;
using tables::unicodeSimpleUpperTable;
using tables::unicodeUpperTable;

// Sorted-table lookups (all generated tables are sorted by first column).
inline const uint32_t* decompEntry(char32_t r) {
	size_t lo = 0, hi = std::size(canonicalDecompTable);
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (canonicalDecompTable[mid][0] < static_cast<uint32_t>(r)) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (lo < std::size(canonicalDecompTable) &&
		canonicalDecompTable[lo][0] == static_cast<uint32_t>(r)) {
		return canonicalDecompTable[lo];
	}
	return nullptr;
}

inline char32_t simpleCaseOf(const uint32_t (*table)[2], size_t n, char32_t r) {
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (table[mid][0] < static_cast<uint32_t>(r)) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (lo < n && table[lo][0] == static_cast<uint32_t>(r)) {
		return static_cast<char32_t>(table[lo][1]);
	}
	return r;
}

// unicode.ToLower — simple (1:1) lowercase mapping.
inline char32_t goSimpleLower(char32_t r) {
	return simpleCaseOf(unicodeSimpleLowerTable, std::size(unicodeSimpleLowerTable), r);
}

// unicode.ToUpper — simple (1:1) uppercase mapping.
inline char32_t goSimpleUpper(char32_t r) {
	return simpleCaseOf(unicodeSimpleUpperTable, std::size(unicodeSimpleUpperTable), r);
}

// unicode.Is(<rangle table>, r) — range membership.
inline bool inUnicodeRange(const uint32_t (*table)[2], size_t n, char32_t r) {
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (table[mid][0] <= static_cast<uint32_t>(r)) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return lo > 0 && static_cast<uint32_t>(r) <= table[lo - 1][1];
}

// unicode.Is(unicode.Mn, r)
inline bool isMnRune(char32_t r) {
	return inUnicodeRange(unicodeMnTable, std::size(unicodeMnTable), r);
}

// unicode.IsUpper(r)
inline bool isUpperRune(char32_t r) {
	return inUnicodeRange(unicodeUpperTable, std::size(unicodeUpperTable), r);
}

// norm.RuneCCC(r) — canonical combining class.
inline uint32_t cccOf(char32_t r) {
	size_t lo = 0, hi = std::size(canonicalCombiningClassTable);
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (canonicalCombiningClassTable[mid][0] < static_cast<uint32_t>(r)) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (lo < std::size(canonicalCombiningClassTable) &&
		canonicalCombiningClassTable[lo][0] == static_cast<uint32_t>(r)) {
		return canonicalCombiningClassTable[lo][1];
	}
	return 0;
}

// norm.NFD.String(s): canonical decomposition of each rune, then canonical
// ordering — stable sort of each maximal run of non-zero combining-class
// runes by CCC (never crossing a starter).
inline std::u32string nfd(std::u32string_view runes) {
	std::u32string out;
	for (char32_t r : runes) {
		if (const uint32_t* e = decompEntry(r)) {
			out.append(canonicalDecompPool + e[1], canonicalDecompPool + e[1] + e[2]);
		} else {
			out.push_back(r);
		}
	}
	for (size_t i = 1; i < out.size(); i++) {
		uint32_t ccc = cccOf(out[i]);
		if (ccc == 0) continue;
		size_t j = i;
		while (j > 0 && cccOf(out[j - 1]) > ccc) {
			std::swap(out[j], out[j - 1]);
			j--;
		}
	}
	return out;
}

// --- shared Go string helpers used across the lsutil translation units ---

// []rune(s) — stringutil's decodeUtf8Rune mirrors Go's
// utf8.DecodeRuneInString (U+FFFD + width 1 on invalid input).
inline std::u32string toRunes(std::string_view s) {
	std::u32string out;
	out.reserve(s.size());
	size_t p = 0;
	while (p < s.size()) {
		int w = 0;
		out.push_back(tsc::decodeUtf8Rune(s.substr(p), &w));
		p += static_cast<size_t>(w);
	}
	return out;
}

// string([]rune)
inline std::string fromRunes(std::u32string_view rs) {
	std::string out;
	char buf[8];
	for (char32_t r : rs) {
		out.append(buf, static_cast<size_t>(tsc::encodeUtf8Rune(r, buf)));
	}
	return out;
}

// strings.ToLower — Go's simple (1:1) lowercase mapping.
inline std::string goToLower(std::string_view s) {
	std::u32string runes = toRunes(s);
	for (auto& r : runes) {
		r = goSimpleLower(r);
	}
	return fromRunes(runes);
}

// unicode.IsSpace — Go's White_Space table.
inline bool isGoSpaceRune(char32_t r) {
	if (r >= 0x09 && r <= 0x0d) return true;
	if (r == 0x20 || r == 0x85 || r == 0xa0) return true;
	if (r == 0x1680) return true;
	if (r >= 0x2000 && r <= 0x200a) return true;
	if (r == 0x2028 || r == 0x2029 || r == 0x202f || r == 0x205f || r == 0x3000) return true;
	return false;
}

// strings.TrimSpace — trims Go unicode.IsSpace runes from both ends.
inline std::string goTrimSpace(std::string_view s) {
	size_t start = 0, end = s.size();
	while (start < end) {
		int w = 0;
		char32_t r = tsc::decodeUtf8Rune(s.substr(start), &w);
		if (!isGoSpaceRune(r)) break;
		start += static_cast<size_t>(w);
	}
	while (end > start) {
		// Find last rune start.
		size_t back = end - 1;
		while (back > start && (static_cast<unsigned char>(s[back]) & 0xc0) == 0x80) --back;
		int w = 0;
		char32_t r = tsc::decodeUtf8Rune(s.substr(back), &w);
		if (!isGoSpaceRune(r) || static_cast<size_t>(w) != end - back) break;
		end = back;
	}
	return std::string(s.substr(start, end - start));
}

// strings.TrimLeftFunc with unicode.IsSpace — TrimLeftSpace.
inline std::string_view trimSpaceLeft(std::string_view s) {
	size_t start = 0;
	while (start < s.size()) {
		int w = 0;
		char32_t r = tsc::decodeUtf8Rune(s.substr(start), &w);
		if (!isGoSpaceRune(r)) break;
		start += static_cast<size_t>(w);
	}
	return s.substr(start);
}

} // namespace tsc::ls::lsutil::detail
