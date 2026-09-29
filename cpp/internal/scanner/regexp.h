// Port of tsc/internal/scanner/regexp.go — internal regular-expression
// literal validator used by Scanner::reScanSlashToken.
#pragma once

#include <cstdint>

#include "internal/core/types.h"
#include "internal/scanner/scanner.h"

namespace tsc {

using RegularExpressionFlags = int32_t;
inline constexpr RegularExpressionFlags RegularExpressionFlagsNone = 0;
inline constexpr RegularExpressionFlags RegularExpressionFlagsHasIndices =
	1 << 0;  // d
inline constexpr RegularExpressionFlags RegularExpressionFlagsGlobal =
	1 << 1;  // g
inline constexpr RegularExpressionFlags RegularExpressionFlagsIgnoreCase =
	1 << 2;  // i
inline constexpr RegularExpressionFlags RegularExpressionFlagsMultiline =
	1 << 3;  // m
inline constexpr RegularExpressionFlags RegularExpressionFlagsDotAll =
	1 << 4;  // s
inline constexpr RegularExpressionFlags RegularExpressionFlagsUnicode =
	1 << 5;  // u
inline constexpr RegularExpressionFlags RegularExpressionFlagsUnicodeSets =
	1 << 6;  // v
inline constexpr RegularExpressionFlags RegularExpressionFlagsSticky =
	1 << 7;  // y
inline constexpr RegularExpressionFlags
	RegularExpressionFlagsAnyUnicodeMode = RegularExpressionFlagsUnicode |
	                                       RegularExpressionFlagsUnicodeSets;
inline constexpr RegularExpressionFlags RegularExpressionFlagsModifiers =
	RegularExpressionFlagsIgnoreCase | RegularExpressionFlagsMultiline |
	RegularExpressionFlagsDotAll;

RegularExpressionFlags charCodeToRegExpFlag(char32_t ch);
void checkRegularExpressionFlagAvailability(Scanner& s,
                                            RegularExpressionFlags flag,
                                            int pos, int size);

void runRegExpValidator(Scanner& s, int startPos, int endPos,
                        uint32_t regExpFlags, bool namedCaptureGroups);

}  // namespace tsc
