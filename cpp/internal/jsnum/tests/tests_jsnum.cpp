// tests_jsnum.cpp — port of tsc/internal/jsnum/jsnum_test.go.
// Benchmarks (BenchmarkToInt32, BenchmarkExponentiate) are not ported: they do
// not run under `go test`.
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/jsnum/jsnum.h"
#include "internal/json/json.h"
#include "internal/testutil/jstest/jstest.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::Number;
using tsc::gostd::testing::T;
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

// assertWithinOneULP checks that got and want are either equal or differ by
// at most 1 ULP (unit in the last place).
void assertWithinOneULP(T* t, Number got, Number want) {
	t->Helper();

	if (got.isNaN() || want.isNaN()) {
		assert::Equal(t, got.isNaN(), want.isNaN(),
		              tsc::gostd::sprintf("got: %v, want: %v",
		                             {goFloat(got), goFloat(want)}));
		return;
	}

	if (got == want) {
		return;
	}

	uint64_t gotBits = numberToBits(got);
	uint64_t wantBits = numberToBits(want);
	if (gotBits == wantBits) {
		return;
	}

	uint64_t ulpDist =
	    gotBits > wantBits ? gotBits - wantBits : wantBits - gotBits;

	if (ulpDist > 1) {
		// NOTE: tsc::gostd::sprintf has no width verbs; Go used %016x.
		t->Errorf(
		    "got %v (%x), want %v (%x) within 1 ULP (off by %d ULPs)",
		    {goFloat(got), tsc::gostd::fmtArg(static_cast<int64_t>(gotBits)),
		     goFloat(want), tsc::gostd::fmtArg(static_cast<int64_t>(wantBits)),
		     tsc::gostd::fmtArg(static_cast<int64_t>(ulpDist))});
	}
}

std::array<uint32_t, 2> numToUint32s(Number n) {
	uint64_t bits = numberToBits(n);
	return {static_cast<uint32_t>(bits), static_cast<uint32_t>(bits >> 32)};
}

Number uint32sToNum(std::array<uint32_t, 2> a) {
	uint64_t bits = static_cast<uint64_t>(a[0]) |
	                static_cast<uint64_t>(a[1]) << 32;
	return numberFromBits(bits);
}

// JSON shapes — Go marshals structs with `json:"x"` etc. tags; maps produce
// the same wire format.
using bitsPair = std::array<uint32_t, 2>;
using binaryInputMap = std::map<std::string, bitsPair>;    // {x, y}
using binaryResultMap = std::map<std::string, bitsPair>;   // {x, y, result}
using unaryInputMap = std::map<std::string, bitsPair>;     // {x}

void writeInputFile(T* t, const std::string& path,
                    const std::string& jsonInput) {
	t->Helper();
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) {
		t->Fatalf("failed to open %s for writing", {path});
		return;
	}
	f << jsonInput;
	f.close();
	if (!f) {
		t->Fatalf("failed to write %s", {path});
	}
}

// evalBinaryOp evaluates a binary JS expression on all cases using Node.js.
// Skips the calling test if Node.js is not available.
std::vector<Number> evalBinaryOp(T* t, const std::string& op,
                                 const std::vector<Number>& xs,
                                 const std::vector<Number>& ys) {
	t->Helper();
	tsc::testutil::jstest::SkipIfNoNodeJS(t);

	std::string tmpdir = t->TempDir();
	std::vector<binaryInputMap> inputs(xs.size());
	for (size_t i = 0; i < xs.size(); i++) {
		inputs[i] = {{"x", numToUint32s(xs[i])}, {"y", numToUint32s(ys[i])}};
	}

	auto [jsonInput, merr] = tsc::json::marshal(inputs);
	assert::Assert(t, merr.empty(), merr);

	std::string inputPath = tmpdir + "/input.json";
	writeInputFile(t, inputPath, jsonInput);

	std::string script = tsc::gostd::sprintf(
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
	    "\t\t\treturn input.map(({x, y}) => {\n"
	    "\t\t\t\tconst a = fromBits(x);\n"
	    "\t\t\t\tconst b = fromBits(y);\n"
	    "\t\t\t\treturn { x, y, result: toBits(%s) };\n"
	    "\t\t\t});\n"
	    "\t\t};\n"
	    "\t",
	    {op});

	auto [results, err] =
	    tsc::testutil::jstest::EvalNodeScript<std::vector<binaryResultMap>>(
	        t, script, tmpdir, {inputPath});
	assert::NilError(t, err);
	assert::Equal(t, results.size(), xs.size());

	std::vector<Number> out(results.size());
	for (size_t i = 0; i < results.size(); i++) {
		out[i] = uint32sToNum(results[i]["result"]);
	}
	return out;
}

// evalUnaryOp evaluates a unary JS expression on all cases using Node.js.
// Skips the calling test if Node.js is not available.
std::vector<Number> evalUnaryOp(T* t, const std::string& op,
                                const std::vector<Number>& xs) {
	t->Helper();
	tsc::testutil::jstest::SkipIfNoNodeJS(t);

	std::string tmpdir = t->TempDir();
	std::vector<unaryInputMap> inputs(xs.size());
	for (size_t i = 0; i < xs.size(); i++) {
		inputs[i] = {{"x", numToUint32s(xs[i])}};
	}

	auto [jsonInput, merr] = tsc::json::marshal(inputs);
	assert::Assert(t, merr.empty(), merr);

	std::string inputPath = tmpdir + "/input.json";
	writeInputFile(t, inputPath, jsonInput);

	std::string script = tsc::gostd::sprintf(
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
	    "\t\t\treturn input.map(({x}) => {\n"
	    "\t\t\t\tconst a = fromBits(x);\n"
	    "\t\t\t\treturn { x, result: toBits(%s) };\n"
	    "\t\t\t});\n"
	    "\t\t};\n"
	    "\t",
	    {op});

	auto [results, err] =
	    tsc::testutil::jstest::EvalNodeScript<std::vector<binaryResultMap>>(
	        t, script, tmpdir, {inputPath});
	assert::NilError(t, err);
	assert::Equal(t, results.size(), xs.size());

	std::vector<Number> out(results.size());
	for (size_t i = 0; i < results.size(); i++) {
		out[i] = uint32sToNum(results[i]["result"]);
	}
	return out;
}

struct toInt32Test {
	const char* name;
	Number input;
	int32_t want;
	bool bench;
};

constexpr double mathPi = 3.141592653589793;
constexpr double mathE = 2.718281828459045;
const double maxFloat64 = std::numeric_limits<double>::max();
const double smallestNonzeroFloat64 = std::numeric_limits<double>::denorm_min();

void TestToInt32(T* t) {
	t->Parallel();

	std::vector<toInt32Test> toInt32Tests = {
	    {"0.0", 0, 0, true},
	    {"-0.0", negativeZero, 0, false},
	    {"NaN", Number::nan(), 0, true},
	    {"+Inf", Number::inf(1), 0, true},
	    {"-Inf", Number::inf(-1), 0, true},
	    {"MaxInt32", Number(2147483647), INT32_MAX, false},
	    {"MaxInt32+1", Number(2147483648), INT32_MIN, true},
	    {"MinInt32", Number(-2147483648), INT32_MIN, false},
	    {"MinInt32-1", Number(-2147483649), INT32_MAX, true},
	    {"MIN_SAFE_INTEGER", Number::minSafeInteger(), 1, false},
	    {"MIN_SAFE_INTEGER-1", Number::minSafeInteger() - Number(1), 0, false},
	    {"MIN_SAFE_INTEGER+1", Number::minSafeInteger() + Number(1), 2, false},
	    {"MAX_SAFE_INTEGER", Number::maxSafeInteger(), -1, true},
	    {"MAX_SAFE_INTEGER-1", Number::maxSafeInteger() - Number(1), -2,
	     true},
	    {"MAX_SAFE_INTEGER+1", Number::maxSafeInteger() + Number(1), 0, true},
	    {"-8589934590", -8589934590, 2, false},
	    {"0xDEADBEEF", 0xDEADBEEF, -559038737, true},
	    {"4294967808", 4294967808, 512, false},
	    {"-0.4", -0.4, 0, false},
	    {"SmallestNonzeroFloat64", smallestNonzeroFloat64, 0, false},
	    {"-SmallestNonzeroFloat64", -smallestNonzeroFloat64, 0, false},
	    {"MaxFloat64", maxFloat64, 0, false},
	    {"-MaxFloat64", -maxFloat64, 0, false},
	    {"Largest subnormal number", numberFromBits(0x000FFFFFFFFFFFFF), 0,
	     false},
	    {"Smallest positive normal number", numberFromBits(0x0010000000000000),
	     0, false},
	    {"Largest normal number", maxFloat64, 0, false},
	    {"-Largest normal number", -maxFloat64, 0, false},
	    {"1.0", 1.0, 1, false},
	    {"-1.0", -1.0, -1, false},
	    {"1e308", 1e308, 0, false},
	    {"-1e308", -1e308, 0, false},
	    {"math.Pi", mathPi, 3, false},
	    {"-math.Pi", -mathPi, -3, false},
	    {"math.E", mathE, 2, false},
	    {"-math.E", -mathE, -2, false},
	    {"0.5", 0.5, 0, false},
	    {"-0.5", -0.5, 0, false},
	    {"0.49999999999999994", 0.49999999999999994, 0, false},
	    {"-0.49999999999999994", -0.49999999999999994, 0, false},
	    {"0.5000000000000001", 0.5000000000000001, 0, false},
	    {"-0.5000000000000001", -0.5000000000000001, 0, false},
	    {"2^31 + 0.5", 2147483648.5, -2147483648LL, false},
	    {"-2^31 - 0.5", -2147483648.5, -2147483648LL, false},
	    {"2^40", 1099511627776, 0, false},
	    {"-2^40", -1099511627776, 0, false},
	    {"TypeFlagsNarrowable", 536624127, 536624127, true},
	};

	std::vector<Number> inputs(toInt32Tests.size());
	std::vector<Number> zeros(toInt32Tests.size());
	for (size_t i = 0; i < toInt32Tests.size(); i++) {
		inputs[i] = toInt32Tests[i].input;
	}
	for (const auto& test : toInt32Tests) {
		t->Run(tsc::gostd::sprintf("%s (%v)", {test.name, goFloat(test.input)}),
		       [&test](T* t) {
			       t->Parallel();
			       int32_t got = test.input.toInt32();
			       assert::Equal(t, got, test.want);
		       });
	}

	t->Run("Node", [&inputs, &zeros, &toInt32Tests](T* t) {
		std::vector<Number> jsResults = evalBinaryOp(t, "a | b", inputs, zeros);
		for (size_t i = 0; i < toInt32Tests.size(); i++) {
			const auto& test = toInt32Tests[i];
			t->Run(
			    tsc::gostd::sprintf("%s (%v)",
			                   {test.name, goFloat(test.input)}),
			    [&test, &jsResults, i](T* t) {
				    t->Parallel();
				    assertEqualNumber(
				        t, Number(test.input.toInt32()), jsResults[i]);
			    });
		}
	});
}
REGISTER_UNIT_TEST("jsnum.TestToInt32", TestToInt32);

struct unaryTest {
	Number x, want;
};

struct binaryTest {
	Number x, y, want;
};

void TestBitwiseNOT(T* t) {
	t->Parallel();

	std::vector<unaryTest> tests = {
	    // Original pairs: ~(-2147483649) == ~(2147483647)
	    {Number(-2147483649), -2147483648},
	    {Number(2147483647), -2147483648},
	    // Original pairs: ~(-4294967296) == ~(0)
	    {Number(-4294967296), -1},
	    {0, -1},
	    // Original pairs: ~(2147483648) == ~(-2147483648)
	    {Number(2147483648), 2147483647},
	    {Number(-2147483648), 2147483647},
	    // Original pairs: ~(4294967296) == ~(0)
	    {Number(4294967296), -1},
	};

	std::vector<Number> xs(tests.size());
	for (size_t i = 0; i < tests.size(); i++) {
		xs[i] = tests[i].x;
	}
	for (const auto& test : tests) {
		t->Run(tsc::gostd::sprintf("~%v", {goFloat(test.x)}), [&test](T* t) {
			t->Parallel();
			Number got = test.x.bitwiseNOT();
			assertEqualNumber(t, got, test.want);
		});
	}

	t->Run("Node", [&xs, &tests](T* t) {
		std::vector<Number> jsResults = evalUnaryOp(t, "~a", xs);
		for (size_t i = 0; i < tests.size(); i++) {
			const auto& test = tests[i];
			t->Run(tsc::gostd::sprintf("~%v", {goFloat(test.x)}),
			       [&test, &jsResults, i](T* t) {
				       t->Parallel();
				       assertEqualNumber(t, test.x.bitwiseNOT(),
				                         jsResults[i]);
			       });
		}
	});
}
REGISTER_UNIT_TEST("jsnum.TestBitwiseNOT", TestBitwiseNOT);

// binOpTest generates TestBitwiseAND/OR/XOR-style tests.
template <class F>
void binaryOpTest(T* t, const std::string& opSym, const std::string& jsOp,
                  const std::vector<binaryTest>& tests, F&& op) {
	std::vector<Number> xs(tests.size()), ys(tests.size());
	for (size_t i = 0; i < tests.size(); i++) {
		xs[i] = tests[i].x;
		ys[i] = tests[i].y;
	}
	for (const auto& test : tests) {
		t->Run(tsc::gostd::sprintf("%v %s %v",
		                      {goFloat(test.x), opSym, goFloat(test.y)}),
		       [&test, &op](T* t) {
			       t->Parallel();
			       Number got = op(test.x, test.y);
			       assertEqualNumber(t, got, test.want);
		       });
	}

	t->Run("Node", [&xs, &ys, &tests, &jsOp, &opSym, &op](T* t) {
		std::vector<Number> jsResults = evalBinaryOp(t, jsOp, xs, ys);
		for (size_t i = 0; i < tests.size(); i++) {
			const auto& test = tests[i];
			t->Run(tsc::gostd::sprintf("%v %s %v",
			                      {goFloat(test.x), opSym,
			                       goFloat(test.y)}),
			       [&test, &jsResults, i, &op](T* t) {
				       t->Parallel();
				       assertEqualNumber(t, op(test.x, test.y),
				                         jsResults[i]);
			       });
		}
	});
}

void TestBitwiseAND(T* t) {
	t->Parallel();
	std::vector<binaryTest> tests = {
	    {0, 0, 0},
	    {0, 1, 0},
	    {1, 0, 0},
	    {1, 1, 1},
	};
	binaryOpTest(t, "&", "a & b", tests,
	             [](Number x, Number y) { return x.bitwiseAND(y); });
}
REGISTER_UNIT_TEST("jsnum.TestBitwiseAND", TestBitwiseAND);

void TestBitwiseOR(T* t) {
	t->Parallel();
	std::vector<binaryTest> tests = {
	    {0, 0, 0},
	    {0, 1, 1},
	    {1, 0, 1},
	    {1, 1, 1},
	};
	binaryOpTest(t, "|", "a | b", tests,
	             [](Number x, Number y) { return x.bitwiseOR(y); });
}
REGISTER_UNIT_TEST("jsnum.TestBitwiseOR", TestBitwiseOR);

void TestBitwiseXOR(T* t) {
	t->Parallel();
	std::vector<binaryTest> tests = {
	    {0, 0, 0},
	    {0, 1, 1},
	    {1, 0, 1},
	    {1, 1, 0},
	};
	binaryOpTest(t, "^", "a ^ b", tests,
	             [](Number x, Number y) { return x.bitwiseXOR(y); });
}
REGISTER_UNIT_TEST("jsnum.TestBitwiseXOR", TestBitwiseXOR);

void TestSignedRightShift(T* t) {
	t->Parallel();
	std::vector<binaryTest> tests = {
	    {1, 0, 1},   {1, 1, 0},  {1, 2, 0},   {1, 31, 0},   {1, 32, 1},
	    {-4, 0, -4}, {-4, 1, -2}, {-4, 2, -1}, {-4, 3, -1}, {-4, 4, -1},
	    {-4, 31, -1}, {-4, 32, -4}, {-4, 33, -2},
	};
	binaryOpTest(t, ">>", "a >> b", tests,
	             [](Number x, Number y) { return x.signedRightShift(y); });
}
REGISTER_UNIT_TEST("jsnum.TestSignedRightShift", TestSignedRightShift);

void TestUnsignedRightShift(T* t) {
	t->Parallel();
	std::vector<binaryTest> tests = {
	    {1, 0, 1},
	    {1, 1, 0},
	    {1, 2, 0},
	    {1, 31, 0},
	    {1, 32, 1},
	    {-4, 0, 4294967292},
	    {-4, 1, 2147483646},
	    {-4, 2, 1073741823},
	    {-4, 3, 536870911},
	    {-4, 4, 268435455},
	    {-4, 31, 1},
	    {-4, 32, 4294967292},
	    {-4, 33, 2147483646},
	};
	binaryOpTest(t, ">>>", "a >>> b", tests,
	             [](Number x, Number y) { return x.unsignedRightShift(y); });
}
REGISTER_UNIT_TEST("jsnum.TestUnsignedRightShift", TestUnsignedRightShift);

void TestLeftShift(T* t) {
	t->Parallel();
	std::vector<binaryTest> tests = {
	    {1, 0, 1},
	    {1, 1, 2},
	    {1, 2, 4},
	    {1, 31, -2147483648},
	    {1, 32, 1},
	    {-4, 0, -4},
	    {-4, 1, -8},
	    {-4, 2, -16},
	    {-4, 3, -32},
	    {-4, 31, 0},
	    {-4, 32, -4},
	};
	binaryOpTest(t, "<<", "a << b", tests,
	             [](Number x, Number y) { return x.leftShift(y); });
}
REGISTER_UNIT_TEST("jsnum.TestLeftShift", TestLeftShift);

void TestRemainder(T* t) {
	t->Parallel();
	std::vector<binaryTest> tests = {
	    {Number::nan(), 1, Number::nan()},
	    {1, Number::nan(), Number::nan()},
	    {Number::inf(1), 1, Number::nan()},
	    {Number::inf(-1), 1, Number::nan()},
	    {123, Number::inf(1), 123},
	    {123, Number::inf(-1), 123},
	    {123, 0, Number::nan()},
	    {123, negativeZero, Number::nan()},
	    {0, 123, 0},
	    {negativeZero, 123, negativeZero},
	    // Normal cases
	    {10, 3, 1},
	    {-10, 3, -1},
	    {10, -3, 1},
	    {-10, -3, -1},
	    {5.5, 2, 1.5},
	    {-5.5, 2, -1.5},
	    {1, 0.5, 0},
	    {-1, 0.5, negativeZero},
	    {1.5, 1, 0.5},
	    {-1.5, 1, -0.5},
	    // Edge cases that prove the bug in the manual formula:
	    // The manual formula n - d*(n/d).trunc() accumulates floating-point
	    // rounding errors that IEEE 754 fmod (math.Mod) avoids.
	    {7, 0.1, Number(std::fmod(7, 0.1))},
	    {7, 0.2, Number(std::fmod(7, 0.2))},
	    {7, 0.3, Number(std::fmod(7, 0.3))},
	    {100, 0.3, Number(std::fmod(100, 0.3))},
	};
	binaryOpTest(t, "%", "a % b", tests,
	             [](Number x, Number y) { return x.remainder(y); });
}
REGISTER_UNIT_TEST("jsnum.TestRemainder", TestRemainder);

void TestExponentiate(T* t) {
	t->Parallel();

	std::vector<binaryTest> tests = {
	    {2, 3, 8},
	    {Number::inf(1), 3, Number::inf(1)},
	    {Number::inf(1), -5, 0},
	    {Number::inf(-1), 3, Number::inf(-1)},
	    {Number::inf(-1), 4, Number::inf(1)},
	    {Number::inf(-1), -3, negativeZero},
	    {Number::inf(-1), -4, 0},
	    {0, 3, 0},
	    {0, -10, Number::inf(1)},
	    {negativeZero, 3, negativeZero},
	    {negativeZero, 4, 0},
	    {negativeZero, -3, Number::inf(-1)},
	    {negativeZero, -4, Number::inf(1)},
	    {3, Number::inf(1), Number::inf(1)},
	    {-3, Number::inf(1), Number::inf(1)},
	    {3, Number::inf(-1), 0},
	    {-3, Number::inf(-1), 0},
	    {Number::nan(), 3, Number::nan()},
	    {1, Number::inf(1), Number::nan()},
	    {1, Number::inf(-1), Number::nan()},
	    {-1, Number::inf(1), Number::nan()},
	    {-1, Number::inf(-1), Number::nan()},
	    {1, Number::nan(), Number::nan()},
	    // Cases where math.Pow diverges from V8 by >1 ULP.
	    // Expected values are the correctly-rounded IEEE 754 results
	    // computed via exact integer arithmetic (big.Int).
	    // Cross-engine testing (V8, SpiderMonkey, QuickJS, XS via jsvu)
	    // confirmed these match the majority of JS engines.
	    {10, 308, numberFromBits(0x7fe1ccf385ebc8a0)},
	    {5, 210, numberFromBits(0x5e68557f31326bbb)},
	    {10, 200, numberFromBits(0x6974e718d7d7625a)},
	};

	std::vector<Number> xs(tests.size()), ys(tests.size());
	for (size_t i = 0; i < tests.size(); i++) {
		xs[i] = tests[i].x;
		ys[i] = tests[i].y;
	}
	for (const auto& test : tests) {
		t->Run(tsc::gostd::sprintf("%v ** %v",
		                      {goFloat(test.x), goFloat(test.y)}),
		       [&test](T* t) {
			       t->Parallel();
			       Number got = test.x.exponentiate(test.y);
			       assertEqualNumber(t, got, test.want);
		       });
	}

	// The ES spec says exponentiate is "implementation-approximated".
	// Different JS engines (V8, SpiderMonkey, JSC) use different pow
	// implementations that can differ by 1 ULP. Allow that tolerance.
	t->Run("Node", [&xs, &ys, &tests](T* t) {
		std::vector<Number> jsResults = evalBinaryOp(t, "a ** b", xs, ys);
		for (size_t i = 0; i < tests.size(); i++) {
			const auto& test = tests[i];
			t->Run(tsc::gostd::sprintf("%v ** %v",
			                      {goFloat(test.x), goFloat(test.y)}),
			       [&test, &jsResults, i](T* t) {
				       t->Parallel();
				       assertWithinOneULP(
				           t, test.x.exponentiate(test.y), jsResults[i]);
			       });
		}
	});
}
REGISTER_UNIT_TEST("jsnum.TestExponentiate", TestExponentiate);

}  // namespace
