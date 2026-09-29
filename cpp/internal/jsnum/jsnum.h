#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace tsc {

// JS-like number (ECMA-262 Number semantics).
struct Number {
	double v;

	constexpr Number() : v(0) {}
	constexpr Number(double d) : v(d) {}

	static Number nan() { return Number(std::numeric_limits<double>::quiet_NaN()); }
	static Number inf(int sign) {
		return Number(std::numeric_limits<double>::infinity() * sign);
	}
	static constexpr Number maxSafeInteger() { return Number(9007199254740991.0); }
	static constexpr Number minSafeInteger() { return Number(-9007199254740991.0); }

	bool isNaN() const { return std::isnan(v); }
	bool isInf() const { return std::isinf(v); }

	// ECMA-262 ToInt32
	int32_t toInt32() const {
		double x = v;
		int32_t smi = static_cast<int32_t>(x);
		if (static_cast<double>(smi) == x)
			return smi;
		if (std::isnan(x) || std::isinf(x))
			return 0;
		x = std::trunc(x);
		x = std::fmod(x, 4294967296.0);
		return static_cast<int32_t>(static_cast<int64_t>(x));
	}
	uint32_t toUint32() const { return static_cast<uint32_t>(toInt32()); }
	uint32_t toShiftCount() const { return toUint32() & 31; }

	Number signedRightShift(Number y) const { return Number(toInt32() >> y.toShiftCount()); }
	Number unsignedRightShift(Number y) const {
		return Number(static_cast<int32_t>(toUint32() >> y.toShiftCount()));
	}
	Number leftShift(Number y) const { return Number(toInt32() << y.toShiftCount()); }
	Number bitwiseNOT() const { return Number(~toInt32()); }
	Number bitwiseOR(Number y) const { return Number(toInt32() | y.toInt32()); }
	Number bitwiseAND(Number y) const { return Number(toInt32() & y.toInt32()); }
	Number bitwiseXOR(Number y) const { return Number(toInt32() ^ y.toInt32()); }

	Number trunc() const { return Number(std::trunc(v)); }
	Number floor() const { return Number(std::floor(v)); }
	Number abs() const { return Number(std::fabs(v)); }

	Number remainder(Number d) const {
		if (isNaN() || d.isNaN() || isInf())
			return nan();
		if (d.isInf())
			return *this;
		if (d.v == 0)
			return nan();
		if (v == 0)
			return *this;
		return Number(std::fmod(v, d.v));
	}

	Number exponentiate(Number exponent) const;

	// ECMA-262 Number::toString — NaN / Infinity / int / shortest repr.
	std::string string() const;

	bool operator==(Number o) const { return v == o.v; }
	bool operator!=(Number o) const { return v != o.v; }
	bool operator<(Number o) const { return v < o.v; }

	Number operator-() const { return Number(-v); }
	Number operator+(Number o) const { return Number(v + o.v); }
	Number operator-(Number o) const { return Number(v - o.v); }
	Number operator*(Number o) const { return Number(v * o.v); }
	Number operator/(Number o) const { return Number(v / o.v); }
};

// ECMA-262 StringToNumber.
Number numberFromString(std::string_view s);

// JS-like bigint (decimal string magnitude; zero = empty Base10Value).
struct PseudoBigInt {
	bool negative = false;
	std::string base10Value;

	static PseudoBigInt create(std::string value, bool negative);
	std::string string() const;
	int sign() const;
	int compare(const PseudoBigInt& other) const;
	bool operator==(const PseudoBigInt& o) const {
		return negative == o.negative && base10Value == o.base10Value;
	}
};

PseudoBigInt parseValidBigInt(std::string_view text);
// Returns the absolute decimal value string of a bigint literal (no 'n' suffix).
std::string parsePseudoBigInt(std::string_view stringValue);

}  // namespace tsc
