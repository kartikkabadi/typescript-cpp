// tests_string.cpp — port of tsc/internal/jsnum/string_test.go.
// FuzzStringJS/FuzzFromStringJS are not ported: Go fuzz tests do not run
// under `go test`.
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/jsnum/jsnum.h"
#include "internal/jsnum/tests/stringtest.h"
#include "internal/json/json.h"
#include "internal/testutil/jstest/jstest.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::Number;
using tsc::gostd::testing::T;
using jsnumtests::stringTest;
namespace assert = tsc::gotest::assert;

namespace {

// negativeZero — jsnum.go:127 (unexported package var).
constexpr Number negativeZero{-0.0};

Number numberFromBits(uint64_t b) {
	return Number(std::bit_cast<double>(b));
}

uint64_t numberToBits(Number n) {
	return std::bit_cast<uint64_t>(n.v);
}

std::array<uint32_t, 2> numberToUint32Array(Number n) {
	uint64_t bits = numberToBits(n);
	return {static_cast<uint32_t>(bits), static_cast<uint32_t>(bits >> 32)};
}

Number uint32ArrayToNumber(std::array<uint32_t, 2> a) {
	uint64_t bits = static_cast<uint64_t>(a[0]) |
	                static_cast<uint64_t>(a[1]) << 32;
	return numberFromBits(bits);
}

// goFloat formats like fmt.Sprintf("%v", float64(n)) — used for subtest names.
std::string goFloat(Number n) {
	if (n.isNaN()) return "NaN";
	if (std::isinf(n.v)) return n.v > 0 ? "+Inf" : "-Inf";
	char buf[64];
	auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), n.v,
	                               std::chars_format::general);
	return std::string(buf, ptr);
}

void assertEqualNumber(T* t, Number got, Number want) {
	t->Helper();

	if (got.isNaN() || want.isNaN()) {
		assert::Equal(t, got.isNaN(), want.isNaN(),
		              tsc::gostd::sprintf("got: %v, want: %v",
		                             {goFloat(got), goFloat(want)}));
	} else {
		assert::Equal(t, got, want);
	}
}

const std::vector<stringTest>& stringTests() {
	static const std::vector<stringTest> tests = [] {
		std::vector<stringTest> out = {
		    {Number::nan(), "NaN"},
		    {Number::inf(1), "Infinity"},
		    {Number::inf(-1), "-Infinity"},
		    {0, "0"},
		    {negativeZero, "0"},
		    {1, "1"},
		    {-1, "-1"},
		    {0.3, "0.3"},
		    {-0.3, "-0.3"},
		    {1.5, "1.5"},
		    {-1.5, "-1.5"},
		    {1e308, "1e+308"},
		    {-1e308, "-1e+308"},
		    {3.141592653589793, "3.141592653589793"},
		    {-3.141592653589793, "-3.141592653589793"},
		    {Number::maxSafeInteger(), "9007199254740991"},
		    {Number::minSafeInteger(), "-9007199254740991"},
		    {numberFromBits(0x000FFFFFFFFFFFFF), "2.225073858507201e-308"},
		    {numberFromBits(0x0010000000000000), "2.2250738585072014e-308"},
		    {1234567.8, "1234567.8"},
		    {19686109595169230000.0, "19686109595169230000"},
		    {123.456, "123.456"},
		    {-123.456, "-123.456"},
		    {444123, "444123"},
		    {-444123, "-444123"},
		    {444123.789123456789875436, "444123.7891234568"},
		    {-444123.78963636363636363636, "-444123.7896363636"},
		    {1e21, "1e+21"},
		    {1e20, "100000000000000000000"},
		};
		out.insert(out.end(), jsnumtests::ryuTests.begin(),
		           jsnumtests::ryuTests.end());
		return out;
	}();
	return tests;
}

const std::vector<stringTest> fromStringTests = {
    {Number::nan(), "    NaN"},
    {Number::inf(1), "Infinity    "},
    {Number::inf(-1), "    -Infinity"},
    {1, "1."},
    {1, "1.0   "},
    {1, "+1"},
    {1, "+1."},
    {1, "+1.0"},
    {Number::nan(), "whoops"},
    {0, ""},
    {0, "0"},
    {0, "0."},
    {0, "0.0"},
    {0, "0.0000"},
    {0, ".0000"},
    {negativeZero, "-0"},
    {negativeZero, "-0."},
    {negativeZero, "-0.0"},
    {negativeZero, "-.0"},
    {Number::nan(), "."},
    {Number::nan(), "e"},
    {Number::nan(), ".e"},
    {Number::nan(), "+"},
    {0, "0X0"},
    {Number::nan(), "e0"},
    {Number::nan(), "E0"},
    {Number::nan(), "1e"},
    {Number::nan(), "1e+"},
    {Number::nan(), "1e-"},
    {1, "1e+0"},
    {Number::nan(), "++0"},
    {Number::nan(), "0_0"},
    {Number::inf(1), "1e1000"},
    {Number::inf(-1), "-1e1000"},
    {0, ".0e0"},
    {Number::nan(), "0e++0"},
    {10, "0XA"},
    {0b1010, "0b1010"},
    {0b1010, "0B1010"},
    {10, "0o12"},  // Go literal 0o12
    {10, "0O12"},  // Go literal 0o12
    {double(0x123456789abcdef0LL), "0x123456789abcdef0"},
    {double(0x123456789abcdef0LL), "0X123456789ABCDEF0"},
    {18446744073709552000.0, "0X10000000000000000"},
    {18446744073709597000.0, "0X1000000000000A801"},
    {Number::nan(), "0B0.0"},
    {1.231235345083403e+91,
     "12312353450834030486384068034683603046834603806830644850340602384608"
     "368034634603680348603864"},
    {Number::nan(),
     "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
     "XXXXXXXXXXXXXXXXXXXXXXX8OOOOOOOOOOOOOOOOOOO"},
    {Number::inf(1), "+Infinity"},
    {1234.56, "  \t1234.56  "},
    {Number::nan(), "\u200b"},
    {0, " "},
    {0, "\n"},
    {0, "\r"},
    {0, "\r\n"},
    {0, "\u2028"},
    {0, "\u2029"},
    {0, "\t"},
    {0, "\v"},
    {0, "\f"},
    {0, "\uFEFF"},
    {0, "\u00A0"},
    {10000000000000000000.0, "010000000000000000000"},
    {Number::nan(),
     "0x1.fffffffffffffp1023"},  // Make sure Go's extended float syntax
                                // doesn't work.
    {Number::nan(), "0X_1FFFP-16"},
    {Number::nan(), "1_000"},  // NumberToString doesn't handle underscores.
    {0, "0x0"},
    {0, "0X0"},
    {Number::nan(), "0xOOPS"},
    {0xABCDEF, "0xABCDEF"},
    {0xABCDEF, "0xABCDEF"},
    {0, "0o0"},
    {0, "0O0"},
    {Number::nan(), "0o8"},
    {Number::nan(), "0O8"},
    {5349, "0o12345"},  // Go literal 0o12345
    {5349, "0O12345"},  // Go literal 0o12345
    {0, "0b0"},
    {0, "0B0"},
    {Number::nan(), "0b2"},
    {Number::nan(), "0b2"},
    {0b10101, "0b10101"},
    {0b10101, "0B10101"},
    {Number::nan(), "1.f"},
    {Number::nan(), "1.e"},
    {Number::nan(), "1.0ef"},
    {Number::nan(), "1.0e"},
    {Number::nan(), ".f"},
    {Number::nan(), ".e"},
    {Number::nan(), ".0ef"},
    {Number::nan(), ".0e"},
    {Number::nan(), "a.f"},
    {Number::nan(), "a.e"},
    {Number::nan(), "a.0ef"},
    {Number::nan(), "a.0e"},
};

void TestString(T* t) {
	t->Parallel();

	for (const auto& test : stringTests()) {
		t->Run(goFloat(test.number), [&test](T* t) {
			t->Parallel();
			assert::Equal(t, test.number.string(), test.str);
		});
	}
}
REGISTER_UNIT_TEST("jsnum.TestString", TestString);

void TestFromString(T* t) {
	t->Parallel();

	t->Run("stringTests", [](T* t) {
		t->Parallel();
		for (const auto& test : stringTests()) {
			t->Run(test.str, [&test](T* t) {
				t->Parallel();
				assertEqualNumber(t, tsc::numberFromString(test.str),
				                  test.number);
				assertEqualNumber(
				    t, tsc::numberFromString(test.str + " "), test.number);
				assertEqualNumber(
				    t, tsc::numberFromString(" " + test.str), test.number);
			});
		}
	});

	t->Run("fromStringTests", [](T* t) {
		t->Parallel();
		for (const auto& test : fromStringTests) {
			t->Run(test.str, [&test](T* t) {
				t->Parallel();
				assertEqualNumber(t, tsc::numberFromString(test.str),
				                  test.number);
			});
		}
	});
}
REGISTER_UNIT_TEST("jsnum.TestFromString", TestFromString);

void TestStringRoundtrip(T* t) {
	t->Parallel();

	for (const auto& test : stringTests()) {
		t->Run(test.str, [&test](T* t) {
			t->Parallel();
			assert::Equal(t, tsc::numberFromString(test.str).string(),
			              test.str);
		});
	}
}
REGISTER_UNIT_TEST("jsnum.TestStringRoundtrip", TestStringRoundtrip);

// getStringResultsFromJS — string_test.go:303. Output rows decode into raw
// json::Value members, then each member unmarshals further.
std::vector<stringTest> getStringResultsFromJS(
    T* t, const std::vector<stringTest>& tests) {
	t->Helper();
	std::string tmpdir = t->TempDir();

	using dataMap = std::map<std::string, tsc::json::Value>;

	std::vector<dataMap> inputData(tests.size());
	for (size_t i = 0; i < tests.size(); i++) {
		auto bits = numberToUint32Array(tests[i].number);
		inputData[i] = {
		    {"bits", tsc::json::marshalArray(
		                 {tsc::json::marshalInt64(bits[0]),
		                  tsc::json::marshalInt64(bits[1])})},
		    {"str", tsc::json::marshalString(tests[i].str)},
		};
	}

	auto [jsonInput, merr] = tsc::json::marshal(inputData);
	assert::Assert(t, merr.empty(), merr);

	std::string jsonInputPath = tmpdir + "/input.json";
	{
		std::ofstream f(jsonInputPath,
		                std::ios::binary | std::ios::trunc);
		f << jsonInput;
		f.close();
		if (!f) {
			t->Fatalf("failed to write %s", {jsonInputPath});
		}
	}

	static const std::string script =
	    "\n"
	    "\t\timport fs from 'fs';\n"
	    "\n"
	    "\t\tfunction fromBits(bits) {\n"
	    "\t\t\tconst buffer = new ArrayBuffer(8);\n"
	    "\t\t\t(new Uint32Array(buffer))[0] = bits[0];\n"
	    "\t\t\t(new Uint32Array(buffer))[1] = bits[1];\n"
	    "\t\t\treturn new Float64Array(buffer)[0];\n"
	    "\t\t}\n"
	    "\n"
	    "\t\tfunction toBits(number) {\n"
	    "\t\t\tconst buffer = new ArrayBuffer(8);\n"
	    "\t\t\t(new Float64Array(buffer))[0] = number;\n"
	    "\t\t\treturn [(new Uint32Array(buffer))[0], (new "
	    "Uint32Array(buffer))[1]];\n"
	    "\t\t}\n"
	    "\n"
	    "\t\texport default function(inputFile) {\n"
	    "\t\t\tconst input = JSON.parse(fs.readFileSync(inputFile, "
	    "'utf8'));\n"
	    "\n"
	    "\t\t\tconst output = input.map((input) => ({\n"
	    "\t\t\t\tstr: \"\"+fromBits(input.bits),\n"
	    "\t\t\t\tbits: toBits(+input.str),\n"
	    "\t\t\t}));\n"
	    "\n"
	    "\t\t\treturn output;\n"
	    "\t\t};\n"
	    "\t";

	auto [outputData, err] =
	    tsc::testutil::jstest::EvalNodeScript<std::vector<dataMap>>(
	        t, script, tmpdir, {jsonInputPath});
	assert::NilError(t, err);
	assert::Equal(t, outputData.size(), tests.size());

	std::vector<stringTest> output(outputData.size());
	for (size_t i = 0; i < outputData.size(); i++) {
		std::array<uint32_t, 2> bits{};
		assert::Assert(
		    t,
		    tsc::json::unmarshal(outputData[i]["bits"], &bits).empty());
		std::string str;
		assert::Assert(
		    t,
		    tsc::json::unmarshal(outputData[i]["str"], &str).empty());
		output[i] = stringTest{uint32ArrayToNumber(bits), str};
	}

	return output;
}

void TestStringJS(T* t) {
	t->Parallel();
	tsc::testutil::jstest::SkipIfNoNodeJS(t);

	t->Run("stringTests", [](T* t) {
		t->Parallel();

		// These tests should roundtrip both ways.
		std::vector<stringTest> stringTestsResults =
		    getStringResultsFromJS(t, stringTests());
		for (size_t i = 0; i < stringTests().size(); i++) {
			const auto& test = stringTests()[i];
			t->Run(goFloat(test.number),
			       [&test, &stringTestsResults, i](T* t) {
				       t->Parallel();
				       assertEqualNumber(t, stringTestsResults[i].number,
				                         test.number);
				       assert::Equal(t, stringTestsResults[i].str, test.str);
			       });
		}
	});

	t->Run("fromStringTests", [](T* t) {
		t->Parallel();

		// These tests should convert the string to the same number.
		std::vector<stringTest> fromStringTestsResults =
		    getStringResultsFromJS(t, fromStringTests);
		for (size_t i = 0; i < fromStringTests.size(); i++) {
			const auto& test = fromStringTests[i];
			t->Run(tsc::gostd::sprintf("fromString %q", {test.str}),
			       [&test, &fromStringTestsResults, i](T* t) {
				       t->Parallel();
				       assertEqualNumber(
				           t, fromStringTestsResults[i].number, test.number);
			       });
		}
	});
}
REGISTER_UNIT_TEST("jsnum.TestStringJS", TestStringJS);

}  // namespace
