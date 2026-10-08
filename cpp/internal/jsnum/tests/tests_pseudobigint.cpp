// tests_pseudobigint.cpp — port of tsc/internal/jsnum/pseudobigint_test.go.
#include <cstdint>
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/jsnum/jsnum.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::Number;
using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;

namespace {

void TestParsePseudoBigInt(T* t) {
	t->Parallel();

	std::vector<Number> testNumbers;
	for (int64_t i = 0; i < int64_t(1e3); i++) {
		testNumbers.push_back(Number(i));
	}
	for (int bits = 0; bits < 53; bits++) {
		testNumbers.push_back(Number(int64_t(1) << bits));
		testNumbers.push_back(Number((int64_t(1) << bits) - 1));
	}

	t->Run("strip base-10 strings", [&testNumbers](T* t) {
		t->Parallel();
		for (const auto& testNumber : testNumbers) {
			for (int leadingZeros = 0; leadingZeros < 10; leadingZeros++) {
				assert::Equal(
				    t,
				    tsc::parsePseudoBigInt(
				        std::string(leadingZeros, '0') +
				        testNumber.string() + "n"),
				    testNumber.string());
			}
		}
	});

	t->Run("parse non-decimal bases (small numbers)", [](T* t) {
		t->Parallel();

		struct tc {
			const char* lit;
			const char* out;
		};
		std::vector<tc> cases = {
		    // binary
		    {"0b0n", "0"},
		    {"0b1n", "1"},
		    {"0b1010n", "10"},
		    {"0b1010_0101n", "165"},
		    {"0B1101n", "13"},  // uppercase prefix

		    // octal
		    {"0o0n", "0"},
		    {"0o7n", "7"},
		    {"0o755n", "493"},
		    {"0o7_5_5n", "493"},
		    {"0O12n", "10"},  // uppercase prefix

		    // hex
		    {"0x0n", "0"},
		    {"0xFn", "15"},
		    {"0xFFn", "255"},
		    {"0xF_Fn", "255"},
		    {"0X1Fn", "31"},  // uppercase prefix
		};

		for (const auto& c : cases) {
			std::string got = tsc::parsePseudoBigInt(c.lit);
			assert::Equal(t, got, std::string(c.out),
			              tsc::gostd::sprintf("literal: %q", {c.lit}));
		}
	});

	t->Run("can parse large literals", [](T* t) {
		t->Parallel();
		assert::Equal(
		    t,
		    tsc::parsePseudoBigInt("123456789012345678901234567890n"),
		    std::string("123456789012345678901234567890"));
		assert::Equal(
		    t,
		    tsc::parsePseudoBigInt(
		        "0b1100011101110100100001111111101101100001101110011111000"
		        "001110111001001110001111110000101011010010n"),
		    std::string("123456789012345678901234567890"));
		assert::Equal(
		    t,
		    tsc::parsePseudoBigInt(
		        "0o143564417755415637016711617605322n"),
		    std::string("123456789012345678901234567890"));
		assert::Equal(
		    t,
		    tsc::parsePseudoBigInt("0x18ee90ff6c373e0ee4e3f0ad2n"),
		    std::string("123456789012345678901234567890"));
	});
}
REGISTER_UNIT_TEST("jsnum.TestParsePseudoBigInt", TestParsePseudoBigInt);

}  // namespace
