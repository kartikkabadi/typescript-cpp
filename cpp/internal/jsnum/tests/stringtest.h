// stringtest.h — shared test table types for the jsnum string tests,
// mirroring tsc/internal/jsnum package-scope sharing between
// string_test.go and ryu_test.go.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "internal/jsnum/jsnum.h"

namespace jsnumtests {

struct stringTest {
	tsc::Number number;
	std::string str;
};

// ieeeParts2Double — ryu_test.go:28.
tsc::Number ieeeParts2Double(bool sign, uint32_t ieeeExponent,
                             uint64_t ieeeMantissa);

// maxMantissa — ryu_test.go:40.
inline constexpr uint64_t maxMantissa = (uint64_t(1) << 53) - 1;

// ryuTests — ryu_test.go:42.
extern const std::vector<stringTest> ryuTests;

}  // namespace jsnumtests
