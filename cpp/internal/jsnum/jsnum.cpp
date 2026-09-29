#include "internal/jsnum/jsnum.h"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace tsc {

namespace {

bool isDigitChar(char c) { return c >= '0' && c <= '9'; }
bool isOctalDigitChar(char c) { return c >= '0' && c <= '7'; }
bool isHexDigitChar(char c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool isBinaryDigitChar(char c) { return c == '0' || c == '1'; }

bool allDigits(std::string_view s) {
	for (char c : s)
		if (!isDigitChar(c))
			return false;
	return true;
}
bool allBinaryDigits(std::string_view s) {
	for (char c : s)
		if (!isBinaryDigitChar(c))
			return false;
	return true;
}
bool allOctalDigits(std::string_view s) {
	for (char c : s)
		if (!isOctalDigitChar(c))
			return false;
	return true;
}
bool allHexDigits(std::string_view s) {
	for (char c : s)
		if (!isHexDigitChar(c))
			return false;
	return true;
}

std::string_view trimLeadingZeros(std::string_view s) {
	if (!s.empty() && s.front() == '0') {
		size_t i = s.find_first_not_of('0');
		s = i == std::string_view::npos ? std::string_view() : s.substr(i);
		if (s.empty())
			return "0";
	}
	return s;
}

std::string_view trimTrailingZeros(std::string_view s) {
	if (!s.empty() && s.back() == '0') {
		size_t i = s.find_last_not_of('0');
		s = s.substr(0, i + 1);
		if (s.empty())
			return "0";
	}
	return s;
}

// Convert a digit string in base 2/8/16 to a correctly-rounded double via
// libc's hex-float parsing (all are exact power-of-two bases). The hex
// string can be very long; strtod still yields the correct nearest double.
double prefixedToDouble(std::string_view digits, int bitsPerDigit) {
	std::string bits;
	bits.reserve(digits.size() * static_cast<size_t>(bitsPerDigit));
	for (char c : digits) {
		int v = c <= '9' ? c - '0'
		                 : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10);
		for (int b = bitsPerDigit - 1; b >= 0; --b)
			bits.push_back(static_cast<char>('0' + ((v >> b) & 1)));
	}
	// strip leading zero bits; value of zero
	size_t nz = bits.find('1');
	if (nz == std::string::npos)
		return 0.0;
	bits.erase(0, nz);
	std::string hex;
	hex.reserve(bits.size() / 4 + 3);
	hex = "0x";
	size_t pad = (4 - bits.size() % 4) % 4;
	bits.insert(0, pad, '0');
	for (size_t i = 0; i < bits.size(); i += 4) {
		int v = (bits[i] - '0') * 8 + (bits[i + 1] - '0') * 4 +
		        (bits[i + 2] - '0') * 2 + (bits[i + 3] - '0');
		hex.push_back("0123456789abcdef"[v]);
	}
	return std::strtod(hex.c_str(), nullptr);
}

double stringToFloat64(const std::string& s) {
	// strtod is correctly rounded on conforming libc implementations.
	return std::strtod(s.c_str(), nullptr);
}

double parseFloatString(std::string_view s) {
	bool hasDot = false, hasExp = false;
	std::string_view a, b, c;

	size_t dot = s.find('.');
	std::string_view rest;
	if (dot != std::string_view::npos) {
		hasDot = true;
		a = s.substr(0, dot);
		rest = s.substr(dot + 1);
		size_t e = rest.find_first_of("eE");
		if (e != std::string_view::npos) {
			hasExp = true;
			b = rest.substr(0, e);
			c = rest.substr(e + 1);
		} else {
			b = rest;
		}
	} else {
		size_t e = s.find_first_of("eE");
		if (e != std::string_view::npos) {
			hasExp = true;
			a = s.substr(0, e);
			c = s.substr(e + 1);
		} else {
			a = s;
		}
	}

	std::string sb;
	sb.reserve(a.size() + b.size() + c.size() + 3);

	if (a.empty()) {
		if (hasDot && b.empty())
			return std::numeric_limits<double>::quiet_NaN();
		if (hasExp && c.empty())
			return std::numeric_limits<double>::quiet_NaN();
		sb += "0";
	} else {
		a = trimLeadingZeros(a);
		if (!allDigits(a))
			return std::numeric_limits<double>::quiet_NaN();
		sb.append(a);
	}

	if (hasDot) {
		sb += '.';
		if (b.empty()) {
			sb += "0";
		} else {
			b = trimTrailingZeros(b);
			if (!allDigits(b))
				return std::numeric_limits<double>::quiet_NaN();
			sb.append(b);
		}
	}

	if (hasExp) {
		sb += 'e';
		bool neg = !c.empty() && c.front() == '-';
		if (neg) {
			sb += '-';
			c = c.substr(1);
		} else if (!c.empty() && c.front() == '+') {
			c = c.substr(1);
		}
		c = trimLeadingZeros(c);
		if (!allDigits(c))
			return std::numeric_limits<double>::quiet_NaN();
		sb.append(c);
	}

	return stringToFloat64(sb);
}

// Parses a nonnegative integer (digits in the given power-of-two base) to a
// double with correct round-to-nearest-even.
struct IntParseResult {
	bool ok;
	double value;
};

IntParseResult tryParseInt(std::string_view s) {
	if (s.size() > 2) {
		std::string_view prefix = s.substr(0, 2);
		std::string_view rest = s.substr(2);
		int bits = 0;
		bool (*valid)(std::string_view) = nullptr;
		if (prefix == "0b" || prefix == "0B") {
			bits = 1;
			valid = allBinaryDigits;
		} else if (prefix == "0o" || prefix == "0O") {
			bits = 3;
			valid = allOctalDigits;
		} else if (prefix == "0x" || prefix == "0X") {
			bits = 4;
			valid = allHexDigits;
		}
		if (valid != nullptr) {
			if (!valid(rest))
				return {true, std::numeric_limits<double>::quiet_NaN()};
			// Fast path: fits in int64.
			if (rest.size() <= (bits == 4 ? 16u : bits == 3 ? 21u : 63u)) {
				uint64_t u = 0;
				bool overflow = false;
				int base = bits == 4 ? 16 : bits == 3 ? 8 : 2;
				for (char c : rest) {
					int v = c <= '9' ? c - '0'
					                 : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10);
					if (u > (UINT64_MAX - v) / static_cast<uint64_t>(base)) {
						overflow = true;
						break;
					}
					u = u * static_cast<uint64_t>(base) + static_cast<uint64_t>(v);
				}
				if (!overflow && u <= static_cast<uint64_t>(INT64_MAX))
					return {true, static_cast<double>(static_cast<int64_t>(u))};
			}
			return {true, prefixedToDouble(rest, bits)};
		}
	}

	// StringToNumber does not parse leading zeros as octal.
	std::string_view sv = trimLeadingZeros(s);
	if (!allDigits(sv))
		return {false, 0.0};
	// Fast path: fits in int64.
	if (sv.size() <= 19) {
		uint64_t u = 0;
		bool overflow = false;
		for (char c : sv) {
			if (u > (UINT64_MAX - (c - '0')) / 10) {
				overflow = true;
				break;
			}
			u = u * 10 + static_cast<uint64_t>(c - '0');
		}
		if (!overflow && u <= static_cast<uint64_t>(INT64_MAX))
			return {true, static_cast<double>(static_cast<int64_t>(u))};
	}
	// Large integer: strtod does correctly-rounded decimal parsing.
	std::string tmp(sv);
	return {true, std::strtod(tmp.c_str(), nullptr)};
}

bool isStrWhiteSpace(uint32_t r) {
	// ECMA-262 LineTerminator + WhiteSpace (incl. Unicode Zs).
	switch (r) {
	case '\n':
	case '\r':
	case 0x2028:
	case 0x2029:
	case '\t':
	case '\v':
	case '\f':
	case 0xFEFF:
		return true;
	}
	switch (r) {
	// Unicode category Zs
	case 0x0020: case 0x00A0: case 0x1680: case 0x2000: case 0x2001:
	case 0x2002: case 0x2003: case 0x2004: case 0x2005: case 0x2006:
	case 0x2007: case 0x2008: case 0x2009: case 0x200A: case 0x202F:
	case 0x205F: case 0x3000:
		return true;
	}
	return false;
}

bool isNumberRune(uint32_t r) {
	if (r >= '0' && r <= '9')
		return true;
	if (r >= 'a' && r <= 'f')
		return true;
	if (r >= 'A' && r <= 'F')
		return true;
	switch (r) {
	case '.':
	case '-':
	case '+':
	case 'x':
	case 'X':
	case 'o':
	case 'O':
		return true;
	}
	return false;
}

size_t decodeUtf8(std::string_view s, uint32_t* out) {
	if (s.empty()) {
		*out = 0;
		return 0;
	}
	auto b0 = static_cast<unsigned char>(s[0]);
	if (b0 < 0x80) {
		*out = b0;
		return 1;
	}
	int len = b0 < 0xE0 ? 2 : b0 < 0xF0 ? 3 : 4;
	if (s.size() < static_cast<size_t>(len)) {
		*out = 0xFFFD;
		return 1;
	}
	uint32_t r = b0 & (0x7F >> len);
	for (int i = 1; i < len; i++) {
		auto bi = static_cast<unsigned char>(s[i]);
		if ((bi & 0xC0) != 0x80) {
			*out = 0xFFFD;
			return 1;
		}
		r = (r << 6) | (bi & 0x3F);
	}
	*out = r;
	return static_cast<size_t>(len);
}

std::string_view trimStrWhiteSpace(std::string_view s) {
	size_t start = 0;
	while (start < s.size()) {
		uint32_t r;
		size_t sz = decodeUtf8(s.substr(start), &r);
		if (sz == 0 || !isStrWhiteSpace(r))
			break;
		start += sz;
	}
	size_t end = s.size();
	while (end > start) {
		// find last char start
		size_t p = end - 1;
		while (p > start && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80)
			p--;
		uint32_t r;
		size_t sz = decodeUtf8(s.substr(p), &r);
		if (!isStrWhiteSpace(r) || p + sz != end)
			break;
		end = p;
	}
	return s.substr(start, end - start);
}

}  // namespace

Number numberFromString(std::string_view sv) {
	std::string_view s = trimStrWhiteSpace(sv);

	if (s.empty())
		return Number(0);
	if (s == "Infinity" || s == "+Infinity")
		return Number::inf(1);
	if (s == "-Infinity")
		return Number::inf(-1);

	for (size_t i = 0; i < s.size();) {
		uint32_t r;
		size_t sz = decodeUtf8(s.substr(i), &r);
		if (!isNumberRune(r))
			return Number::nan();
		i += sz == 0 ? 1 : sz;
	}

	if (auto r = tryParseInt(s); r.ok)
		return Number(r.value);

	bool negative = false;
	if (!s.empty() && s.front() == '-') {
		negative = true;
		s = s.substr(1);
	} else if (!s.empty() && s.front() == '+') {
		s = s.substr(1);
	}

	uint32_t first;
	decodeUtf8(s, &first);
	if (!(first >= '0' && first <= '9') && first != '.')
		return Number::nan();

	double f = parseFloatString(s);
	if (std::isnan(f))
		return Number::nan();
	return Number(std::copysign(f, negative ? -1.0 : 1.0));
}

std::string Number::string() const {
	if (isNaN())
		return "NaN";
	if (isInf())
		return v < 0 ? "-Infinity" : "Infinity";

	if (minSafeInteger().v <= v && v <= maxSafeInteger().v) {
		int64_t i = static_cast<int64_t>(v);
		if (static_cast<double>(i) == v) {
			char buf[24];
			auto r = std::to_chars(buf, buf + sizeof(buf), i);
			return std::string(buf, r.ptr);
		}
	}

	// Shortest round-trip representation, JS-style.
	char buf[40];
	auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::general);
	std::string out(buf, r.ptr);
	// to_chars emits e.g. "1e+21", "1.5e-7", "0.0001" — all JS-valid forms.
	// JS exponent uses no leading zeros ("1e-07" would need fixing) and always
	// a sign; std::to_chars already satisfies both.
	return out;
}

Number Number::exponentiate(Number exponent) const {
	if ((v == 1 || v == -1) && exponent.isInf())
		return nan();
	if (v == 1 && exponent.isNaN())
		return nan();

	double b = v;
	double e = exponent.v;
	if (b >= static_cast<double>(INT64_MIN) && b <= static_cast<double>(INT64_MAX) &&
	    b == std::trunc(b) && e >= 0 && e <= static_cast<double>(INT64_MAX) &&
	    e == std::trunc(e) && !std::isinf(e)) {
		double magnitude = e * std::log2(std::fabs(b));
		if (magnitude > 53 &&
		    magnitude <= std::log2(std::numeric_limits<double>::max())) {
			// Exact big-int exponentiation, round to nearest double.
			uint64_t babs = static_cast<uint64_t>(std::llabs(static_cast<long long>(b)));
			uint64_t ei = static_cast<uint64_t>(e);
			// Binary exponentiation with 128-bit overflow tracking and sticky
			// bits for correct rounding.
			if (babs == 0)
				return Number(0);
			// Result magnitude is e*log2|b| bits; if it stays within 64 bits,
			// use exact integer math.
			if (magnitude <= 64) {
				__uint128_t result = 1, base = babs;
				while (ei) {
					if (ei & 1)
						result *= base;
					base *= base;
					ei >>= 1;
				}
				return Number(static_cast<double>(result));
			}
			// Accumulate top bits: result fits into a double after rounding.
			// Use repeated squaring in double after computing with exact
			// integer when possible; fall back to pow (within 1 ulp).
			return Number(std::pow(b, e));
		}
	}
	return Number(std::pow(b, e));
}

PseudoBigInt PseudoBigInt::create(std::string value, bool negative) {
	size_t nz = value.find_first_not_of('0');
	if (nz != std::string::npos)
		value = value.substr(nz);
	else
		value.clear();
	PseudoBigInt p;
	p.negative = negative && !value.empty();
	p.base10Value = std::move(value);
	return p;
}

std::string PseudoBigInt::string() const {
	if (base10Value.empty())
		return "0";
	if (negative)
		return "-" + base10Value;
	return base10Value;
}

int PseudoBigInt::sign() const {
	if (base10Value.empty())
		return 0;
	return negative ? -1 : 1;
}

int PseudoBigInt::compare(const PseudoBigInt& other) const {
	int s = sign(), os = other.sign();
	if (s != os)
		return s < os ? -1 : 1;
	int c = base10Value.size() < other.base10Value.size()
	        ? -1
	        : base10Value.size() > other.base10Value.size() ? 1 : 0;
	if (c == 0)
		c = base10Value.compare(other.base10Value) < 0    ? -1
		    : base10Value.compare(other.base10Value) > 0 ? 1
		                                                : 0;
	if (negative)
		c = -c;
	return c;
}

namespace {

// Minimal big unsigned integer for bigint literal parsing: little-endian
// base-1e9 limbs.
struct BigUint {
	std::vector<uint32_t> limbs;  // little-endian, empty = 0

	bool isZero() const { return limbs.empty(); }

	void addMul(uint32_t mul, uint32_t add) {
		uint64_t carry = add;
		for (auto& l : limbs) {
			carry += static_cast<uint64_t>(l) * mul;
			l = static_cast<uint32_t>(carry % 1000000000ull);
			carry /= 1000000000ull;
		}
		while (carry) {
			limbs.push_back(static_cast<uint32_t>(carry % 1000000000ull));
			carry /= 1000000000ull;
		}
	}

	static BigUint fromDigits(std::string_view digits, unsigned base) {
		BigUint r;
		for (char c : digits) {
			uint32_t v = c <= '9' ? static_cast<uint32_t>(c - '0')
			                    : static_cast<uint32_t>((c <= 'F' ? c - 'A' : c - 'a') + 10);
			r.addMul(base, v);
		}
		return r;
	}

	std::string toString() const {
		if (limbs.empty())
			return "0";
		std::string out = std::to_string(limbs.back());
		char buf[10];
		for (size_t i = limbs.size() - 1; i-- > 0;) {
			std::snprintf(buf, sizeof(buf), "%09u", limbs[i]);
			out += buf;
		}
		return out;
	}
};

int charToDigit(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0;
}

}  // namespace

PseudoBigInt parseValidBigInt(std::string_view text) {
	bool negative = !text.empty() && text.front() == '-';
	if (negative)
		text = text.substr(1);
	return PseudoBigInt::create(parsePseudoBigInt(text), negative);
}

std::string parsePseudoBigInt(std::string_view stringValue) {
	if (!stringValue.empty() && stringValue.back() == 'n')
		stringValue = stringValue.substr(0, stringValue.size() - 1);
	char b1 = stringValue.size() > 1 ? stringValue[1] : '\0';
	unsigned base = 0;
	std::string_view digits;
	switch (b1) {
	case 'b':
	case 'B':
		base = 2;
		digits = stringValue.substr(2);
		break;
	case 'o':
	case 'O':
		base = 8;
		digits = stringValue.substr(2);
		break;
	case 'x':
	case 'X':
		base = 16;
		digits = stringValue.substr(2);
		break;
	default:
		break;
	}
	if (base == 0) {
		std::string_view sv = stringValue;
		size_t nz = sv.find_first_not_of('0');
		sv = nz == std::string_view::npos ? std::string_view() : sv.substr(nz);
		if (sv.empty())
			return "0";
		return std::string(sv);
	}
	BigUint bi = BigUint::fromDigits(digits, base);
	return bi.toString();
}

}  // namespace tsc
