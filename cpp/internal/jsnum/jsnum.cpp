#include "internal/jsnum/jsnum.h"

#include "internal/json/json.h"

#include <bit>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>
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
	// strconv.ParseFloat: the entire string must parse (a trailing segment is
	// a syntax error → NaN); ErrRange still returns the rounded value.
	if (s.empty())
		return std::numeric_limits<double>::quiet_NaN();
	char* end = nullptr;
	double f = std::strtod(s.c_str(), &end);
	if (end != s.c_str() + s.size())
		return std::numeric_limits<double>::quiet_NaN();
	return f;
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

// utf8.DecodeRuneInString: invalid encodings (bad lead, bad continuation,
// overlong, surrogate, >U+10FFFF, truncated) yield (RuneError, 1).
size_t decodeUtf8(std::string_view s, uint32_t* out) {
	if (s.empty()) {
		*out = 0;
		return 0;
	}
	auto fail = [&]() -> size_t {
		*out = 0xFFFD;
		return 1;
	};
	auto b0 = static_cast<unsigned char>(s[0]);
	if (b0 < 0x80) {
		*out = b0;
		return 1;
	}
	int len;
	uint32_t min;
	if (b0 < 0xC2) {
		return fail();  // 0x80..0xC1: stray continuation or overlong lead
	} else if (b0 < 0xE0) {
		len = 2;
		min = 0x80;
	} else if (b0 < 0xF0) {
		len = 3;
		min = 0x800;
	} else if (b0 < 0xF5) {
		len = 4;
		min = 0x10000;
	} else {
		return fail();  // 0xF5..0xFF: above U+10FFFF
	}
	if (s.size() < static_cast<size_t>(len))
		return fail();
	uint32_t r = b0 & (0x7F >> len);
	for (int i = 1; i < len; i++) {
		auto bi = static_cast<unsigned char>(s[i]);
		if ((bi & 0xC0) != 0x80)
			return fail();
		r = (r << 6) | (bi & 0x3F);
	}
	if (r < min || (r >= 0xD800 && r <= 0xDFFF))
		return fail();  // overlong / surrogate
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

	// jsnum string.go:36 — Go delegates to json.Marshal(float64), i.e.
	// strconv 'f' notation when 1e-6 <= |x| < 1e21 else 'e' notation with
	// shortest round-trip digits.
	return json::detail::goFloat(v);
}

// jsnum.go:147 — big.Int equivalent: arbitrary-precision unsigned integer,
// little-endian 64-bit limbs.
struct PowUint {
	std::vector<uint64_t> limbs;  // no trailing zero limbs; empty == 0

	bool isZero() const { return limbs.empty(); }
	uint64_t bit(int i) const {
		return (limbs[i / 64] >> (i % 64)) & 1;
	}
	int bitLen() const {
		if (limbs.empty())
			return 0;
		return static_cast<int>(limbs.size() * 64 -
		                        std::countl_zero(limbs.back()));
	}
	void trim() {
		while (!limbs.empty() && limbs.back() == 0)
			limbs.pop_back();
	}

	static PowUint fromU64(uint64_t x) {
		PowUint r;
		if (x)
			r.limbs.push_back(x);
		return r;
	}

	static PowUint mul(const PowUint& a, const PowUint& b) {
		PowUint r;
		if (a.isZero() || b.isZero())
			return r;
		r.limbs.assign(a.limbs.size() + b.limbs.size(), 0);
		for (size_t i = 0; i < a.limbs.size(); ++i) {
			uint64_t carry = 0;
			for (size_t j = 0; j < b.limbs.size(); ++j) {
				__uint128_t cur = static_cast<__uint128_t>(a.limbs[i]) *
				                      b.limbs[j] +
				                  r.limbs[i + j] + carry;
				r.limbs[i + j] = static_cast<uint64_t>(cur);
				carry = static_cast<uint64_t>(cur >> 64);
			}
			for (size_t k = i + b.limbs.size(); carry; ++k) {
				__uint128_t cur = static_cast<__uint128_t>(r.limbs[k]) + carry;
				r.limbs[k] = static_cast<uint64_t>(cur);
				carry = static_cast<uint64_t>(cur >> 64);
			}
		}
		r.trim();
		return r;
	}

	static PowUint pow(uint64_t base, uint64_t e) {
		PowUint result = fromU64(1), b = fromU64(base);
		while (e) {
			if (e & 1)
				result = mul(result, b);
			e >>= 1;
			if (e)
				b = mul(b, b);
		}
		return result;
	}
};

// Round-to-nearest-even the top `keep` significant bits of `x` (like
// big.Float.SetInt at precision `keep`). Fills out[0..3] with the kept
// mantissa (normalized: bit keep-1 set unless zero) and returns `shift`
// such that the rounded value == out * 2^shift.
static int roundTopBits(const PowUint& x, int keep, uint64_t out[4]) {
	int L = x.bitLen();
	uint64_t mant[5] = {};
	if (L <= keep) {
		// no rounding — normalize so the top bit sits at position keep-1
		int back = keep - L;
		for (int i = 0; i < L; ++i)
			if (x.bit(i))
				mant[(i + back) / 64] |= uint64_t(1) << ((i + back) % 64);
		for (int i = 0; i < 4; ++i)
			out[i] = mant[i];
		return L - keep;
	}
	int shift = L - keep;
	for (int i = 0; i < keep; ++i)
		if (x.bit(shift + i))
			mant[i / 64] |= uint64_t(1) << (i % 64);
	uint64_t roundBit = x.bit(shift - 1);
	bool sticky = false;
	for (int i = 0; i < shift - 1; ++i)
		if (x.bit(i)) {
			sticky = true;
			break;
		}
	if (roundBit && (sticky || (mant[0] & 1))) {
		for (int i = 0, nl = keep / 64 + 1; i < nl; ++i)
			if (++mant[i])
				break;
		if (mant[keep / 64] & (uint64_t(1) << (keep % 64))) {
			// carry out past bit keep-1 → rounded value = 2^keep
			mant[0] = mant[1] = mant[2] = mant[3] = mant[4] = 0;
			mant[(keep - 1) / 64] = uint64_t(1) << ((keep - 1) % 64);
			++shift;
		}
	}
	for (int i = 0; i < 4; ++i)
		out[i] = mant[i];
	return shift;
}

// big.Float SetPrec(256).SetInt(ri).Float64(): RNE to 256 significant bits,
// then RNE to a double's 53-bit mantissa; sign applied by caller.
static double bigIntToDoubleRounded(const PowUint& x) {
	uint64_t m256[4];
	int s1 = roundTopBits(x, 256, m256);
	PowUint m;
	m.limbs.assign(m256, m256 + 4);
	m.trim();
	uint64_t m53[4];
	int s2 = roundTopBits(m, 53, m53);
	int exp = s1 + s2;  // value == m53 * 2^exp, m53 normalized to 53 bits
	// m53's bit 52 is the implicit leading 1.
	uint64_t mant53 = m53[0];  // 53 bits fit in limb 0
	int e2 = exp + 52;         // unbiased binary exponent
	if (e2 >= 1024)
		return std::numeric_limits<double>::infinity();
	uint64_t fieldExp = static_cast<uint64_t>(e2 + 1023);
	uint64_t bits = (fieldExp << 52) | (mant53 & ((uint64_t(1) << 52) - 1));
	double d;
	std::memcpy(&d, &bits, 8);
	return d;
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
			// Go uses big.Int.Exp (exact, signed) then big.Float(256) →
			// Float64: round-to-nearest-even at 256 bits, then at 53.
			int64_t bi = static_cast<int64_t>(b);
			uint64_t ei = static_cast<uint64_t>(e);
			uint64_t babs = bi < 0 ? uint64_t(0) - static_cast<uint64_t>(bi)
			                       : static_cast<uint64_t>(bi);
			bool neg = bi < 0 && (ei & 1);
			PowUint ri = PowUint::pow(babs, ei);
			double result = bigIntToDoubleRounded(ri);
			return Number(neg ? -result : result);
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
		// Go big.Int.SetString(s, 0) accepts '_' digit separators.
		BigUint r;
		for (char c : digits) {
			if (c == '_')
				continue;
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
