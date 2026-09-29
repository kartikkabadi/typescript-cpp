// Port of Go's unicode.Is over the generated ES-next identifier tables.
#pragma once

#include <cstdint>

#include "internal/stringutil/unicode_tables.h"

namespace tsc {

template <class T>
inline bool inRangeTable(const T (*ranges)[3], int count, uint32_t r) {
	int lo = 0, hi = count;
	while (lo < hi) {
		int m = lo + (hi - lo) / 2;
		T lo_ = ranges[m][0], hi_ = ranges[m][1], stride = ranges[m][2];
		if (lo_ <= r && r <= hi_)
			return (r - lo_) % stride == 0;
		if (r < lo_)
			hi = m;
		else
			lo = m + 1;
	}
	return false;
}

inline bool isUnicodeIdentifierStart(char32_t ch) {
	uint32_t r = static_cast<uint32_t>(ch);
	constexpr int n16 = sizeof(kIdentifierStartR16) / sizeof(kIdentifierStartR16[0]);
	constexpr int n32 = sizeof(kIdentifierStartR32) / sizeof(kIdentifierStartR32[0]);
	if (n16 > 0 && r <= kIdentifierStartR16[n16 - 1][1])
		return inRangeTable(kIdentifierStartR16, n16, r);
	if (n32 > 0 && r >= kIdentifierStartR32[0][0])
		return inRangeTable(kIdentifierStartR32, n32, r);
	return false;
}

inline bool isUnicodeIdentifierPart(char32_t ch) {
	uint32_t r = static_cast<uint32_t>(ch);
	constexpr int n16 = sizeof(kIdentifierPartR16) / sizeof(kIdentifierPartR16[0]);
	constexpr int n32 = sizeof(kIdentifierPartR32) / sizeof(kIdentifierPartR32[0]);
	if (n16 > 0 && r <= kIdentifierPartR16[n16 - 1][1])
		return inRangeTable(kIdentifierPartR16, n16, r);
	if (n32 > 0 && r >= kIdentifierPartR32[0][0])
		return inRangeTable(kIdentifierPartR32, n32, r);
	return false;
}

}  // namespace tsc
