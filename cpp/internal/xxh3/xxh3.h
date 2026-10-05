// xxh3.h — C++23 port of github.com/zeebo/xxh3 v1.1.0 Hash128 (scalar path;
// the Go SIMD accumulators compute the identical function — zeebo
// accum_generic.go:accumScalar is the reference semantics).
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace tsc::xxh3 {

// Uint128 is a 128 bit value — utils.go:9. The value is Hi<<64 | Lo.
struct Uint128 {
	uint64_t Hi = 0, Lo = 0;
	bool operator==(const Uint128&) const = default;

	// Bytes returns the uint128 as an array of bytes in canonical form
	// (big-endian encoded) — utils.go:16.
	std::array<uint8_t, 16> Bytes() const {
		return {static_cast<uint8_t>(Hi >> 0x38), static_cast<uint8_t>(Hi >> 0x30),
		        static_cast<uint8_t>(Hi >> 0x28), static_cast<uint8_t>(Hi >> 0x20),
		        static_cast<uint8_t>(Hi >> 0x18), static_cast<uint8_t>(Hi >> 0x10),
		        static_cast<uint8_t>(Hi >> 0x08), static_cast<uint8_t>(Hi),
		        static_cast<uint8_t>(Lo >> 0x38), static_cast<uint8_t>(Lo >> 0x30),
		        static_cast<uint8_t>(Lo >> 0x28), static_cast<uint8_t>(Lo >> 0x20),
		        static_cast<uint8_t>(Lo >> 0x18), static_cast<uint8_t>(Lo >> 0x10),
		        static_cast<uint8_t>(Lo >> 0x08), static_cast<uint8_t>(Lo)};
	}
};

namespace detail {

// consts.go
inline constexpr uint64_t prime32_1 = 2654435761;
inline constexpr uint64_t prime32_2 = 2246822519;
inline constexpr uint64_t prime32_3 = 3266489917;

inline constexpr uint64_t prime64_1 = 11400714785074694791ull;
inline constexpr uint64_t prime64_2 = 14029467366897019727ull;
inline constexpr uint64_t prime64_3 = 1609587929392839161ull;
inline constexpr uint64_t prime64_4 = 9650029242287828579ull;
inline constexpr uint64_t prime64_5 = 2870177450012600261ull;

inline constexpr size_t stripe = 64;
inline constexpr size_t block = 1024;

// key — the default 192-byte secret (consts.go:18).
inline constexpr uint8_t key[192] = {
	0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c,
	0xf7, 0x21, 0xad, 0x1c, 0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb,
	0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f, 0xcb, 0x79, 0xe6, 0x4e,
	0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
	0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6,
	0x81, 0x3a, 0x26, 0x4c, 0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb,
	0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3, 0x71, 0x64, 0x48, 0x97,
	0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
	0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7,
	0xc7, 0x0b, 0x4f, 0x1d, 0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31,
	0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64, 0xea, 0xc5, 0xac, 0x83,
	0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
	0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26,
	0x29, 0xd4, 0x68, 0x9e, 0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc,
	0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce, 0x45, 0xcb, 0x3a, 0x8f,
	0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};

inline constexpr uint64_t key64_000 = 0xbe4ba423396cfeb8ull;
inline constexpr uint64_t key64_008 = 0x1cad21f72c81017cull;
inline constexpr uint64_t key64_016 = 0xdb979083e96dd4deull;
inline constexpr uint64_t key64_024 = 0x1f67b3b7a4a44072ull;
inline constexpr uint64_t key64_032 = 0x78e5c0cc4ee679cbull;
inline constexpr uint64_t key64_040 = 0x2172ffcc7dd05a82ull;
inline constexpr uint64_t key64_048 = 0x8e2443f7744608b8ull;
inline constexpr uint64_t key64_056 = 0x4c263a81e69035e0ull;
inline constexpr uint64_t key64_064 = 0xcb00c391bb52283cull;
inline constexpr uint64_t key64_072 = 0xa32e531b8b65d088ull;
inline constexpr uint64_t key64_080 = 0x4ef90da297486471ull;
inline constexpr uint64_t key64_088 = 0xd8acdea946ef1938ull;
inline constexpr uint64_t key64_096 = 0x3f349ce33f76faa8ull;
inline constexpr uint64_t key64_104 = 0x1d4f0bc7c7bbdcf9ull;
inline constexpr uint64_t key64_112 = 0x3159b4cd4be0518aull;
inline constexpr uint64_t key64_120 = 0x647378d9c97e9fc8ull;
inline constexpr uint64_t key64_128 = 0xc3ebd33483acc5eaull;
inline constexpr uint64_t key64_136 = 0xeb6313faffa081c5ull;
inline constexpr uint64_t key64_144 = 0x49daf0b751dd0d17ull;
inline constexpr uint64_t key64_152 = 0x9e68d429265516d3ull;
inline constexpr uint64_t key64_160 = 0xfca1477d58be162bull;
inline constexpr uint64_t key64_168 = 0xce31d07ad1b8f88full;
inline constexpr uint64_t key64_176 = 0x280416958f3acb45ull;
inline constexpr uint64_t key64_184 = 0x7e404bbbcafbd7afull;

inline constexpr uint64_t key64_103 = 0x4f0bc7c7bbdcf93full;
inline constexpr uint64_t key64_111 = 0x59b4cd4be0518a1dull;
inline constexpr uint64_t key64_119 = 0x7378d9c97e9fc831ull;
inline constexpr uint64_t key64_127 = 0xebd33483acc5ea64ull;

inline constexpr uint64_t key64_121 = 0xea647378d9c97e9full;
inline constexpr uint64_t key64_129 = 0xc5c3ebd33483acc5ull;
inline constexpr uint64_t key64_137 = 0x17eb6313faffa081ull;
inline constexpr uint64_t key64_145 = 0xd349daf0b751dd0dull;
inline constexpr uint64_t key64_153 = 0x2b9e68d429265516ull;
inline constexpr uint64_t key64_161 = 0x8ffca1477d58be16ull;
inline constexpr uint64_t key64_169 = 0x45ce31d07ad1b8f8ull;
inline constexpr uint64_t key64_177 = 0xaf280416958f3acbull;

inline constexpr uint64_t key64_011 = 0x6dd4de1cad21f72cull;
inline constexpr uint64_t key64_019 = 0xa44072db979083e9ull;
inline constexpr uint64_t key64_027 = 0xe679cb1f67b3b7a4ull;
inline constexpr uint64_t key64_035 = 0xd05a8278e5c0cc4eull;
inline constexpr uint64_t key64_043 = 0x4608b82172ffcc7dull;
inline constexpr uint64_t key64_051 = 0x9035e08e2443f774ull;
inline constexpr uint64_t key64_059 = 0x52283c4c263a81e6ull;
inline constexpr uint64_t key64_067 = 0x65d088cb00c391bbull;

inline constexpr uint64_t key64_117 = 0xd9c97e9fc83159b4ull;
inline constexpr uint64_t key64_125 = 0x3483acc5ea647378ull;
inline constexpr uint64_t key64_133 = 0xfaffa081c5c3ebd3ull;
inline constexpr uint64_t key64_141 = 0xb751dd0d17eb6313ull;
inline constexpr uint64_t key64_149 = 0x29265516d349daf0ull;
inline constexpr uint64_t key64_157 = 0x7d58be162b9e68d4ull;
inline constexpr uint64_t key64_165 = 0x7ad1b8f88ffca147ull;
inline constexpr uint64_t key64_173 = 0x958f3acb45ce31d0ull;

inline constexpr uint32_t key32_000 = 0xbe4ba423;
inline constexpr uint32_t key32_004 = 0x396cfeb8;
inline constexpr uint32_t key32_008 = 0x1cad21f7;
inline constexpr uint32_t key32_012 = 0x2c81017c;

// utils.go — little-endian loads.
inline uint8_t readU8(const uint8_t* p, size_t o) { return p[o]; }
inline uint16_t readU16(const uint8_t* p, size_t o) {
	return uint16_t(p[o]) | uint16_t(p[o + 1]) << 8;
}
inline uint32_t readU32(const uint8_t* p, size_t o) {
	uint32_t v;
	std::memcpy(&v, p + o, 4);
	return v;
}
inline uint64_t readU64(const uint8_t* p, size_t o) {
	uint64_t v;
	std::memcpy(&v, p + o, 8);
	return v;
}

// bits.Mul64 — 64x64 -> hi,lo.
inline std::pair<uint64_t, uint64_t> mul64(uint64_t x, uint64_t y) {
	__uint128_t p = (__uint128_t)x * y;
	return {(uint64_t)(p >> 64), (uint64_t)p};
}

inline uint64_t rotl64(uint64_t x, int r) {
	return (x << r) | (x >> (64 - r));
}
inline uint32_t rotl32(uint32_t x, int r) {
	return (x << r) | (x >> (32 - r));
}
inline uint64_t bswap64(uint64_t x) {
	uint64_t r;
	uint8_t* d = reinterpret_cast<uint8_t*>(&r);
	const uint8_t* s = reinterpret_cast<const uint8_t*>(&x);
	for (int i = 0; i < 8; i++) d[i] = s[7 - i];
	return r;
}
inline uint32_t bswap32(uint32_t x) {
	uint32_t r;
	uint8_t* d = reinterpret_cast<uint8_t*>(&r);
	const uint8_t* s = reinterpret_cast<const uint8_t*>(&x);
	for (int i = 0; i < 4; i++) d[i] = s[3 - i];
	return r;
}

inline uint64_t xxh64AvalancheSmall(uint64_t x) {
	x *= prime64_2;
	x ^= x >> 29;
	x *= prime64_3;
	x ^= x >> 32;
	return x;
}

inline uint64_t xxh3Avalanche(uint64_t x) {
	x ^= x >> 37;
	x *= 0x165667919e3779f9ull;
	x ^= x >> 32;
	return x;
}

inline uint64_t mulFold64(uint64_t x, uint64_t y) {
	auto [hi, lo] = mul64(x, y);
	return hi ^ lo;
}

// accumScalar — accum_generic.go:7 (secret == key specialization).
inline void accumScalar(uint64_t (&accs)[8], const uint8_t* p, uint64_t l) {
	while (l > block) {
		const uint8_t* k = key;
		for (int i = 0; i < 16; i++) {
			for (int off = 0; off < 8; off += 2) {
				uint64_t dv0 = readU64(p, 8 * off);
				uint64_t dk0 = dv0 ^ readU64(k, 8 * off);
				uint64_t ac1 = dv0;
				uint64_t ac0 = uint64_t(uint32_t(dk0)) * (dk0 >> 32);

				uint64_t dv1 = readU64(p, 8 * off + 8);
				uint64_t dk1 = dv1 ^ readU64(k, 8 * off + 8);
				ac0 += dv1;
				ac1 += uint64_t(uint32_t(dk1)) * (dk1 >> 32);

				accs[off] += ac0;
				accs[off + 1] += ac1;
			}
			l -= stripe;
			if (l > 0) {
				p += stripe;
				k += 8;
			}
		}
		// scramble accs
		static constexpr uint64_t scrambleKey[8] = {
			key64_128, key64_136, key64_144, key64_152,
			key64_160, key64_168, key64_176, key64_184,
		};
		for (int i = 0; i < 8; i++) {
			accs[i] ^= accs[i] >> 47;
			accs[i] ^= scrambleKey[i];
			accs[i] *= prime32_1;
		}
	}

	if (l > 0) {
		uint64_t t = (l - 1) / stripe;
		const uint8_t* k = key;
		for (uint64_t i = 0; i < t; i++) {
			for (int off = 0; off < 8; off += 2) {
				uint64_t dv0 = readU64(p, 8 * off);
				uint64_t dk0 = dv0 ^ readU64(k, 8 * off);
				uint64_t ac1 = dv0;
				uint64_t ac0 = uint64_t(uint32_t(dk0)) * (dk0 >> 32);

				uint64_t dv1 = readU64(p, 8 * off + 8);
				uint64_t dk1 = dv1 ^ readU64(k, 8 * off + 8);
				ac0 += dv1;
				ac1 += uint64_t(uint32_t(dk1)) * (dk1 >> 32);

				accs[off] += ac0;
				accs[off + 1] += ac1;
			}
			l -= stripe;
			if (l > 0) {
				p += stripe;
				k += 8;
			}
		}

		if (l > 0) {
			p -= stripe - l;
			static constexpr uint64_t tailKey[8] = {
				key64_121, key64_129, key64_137, key64_145,
				key64_153, key64_161, key64_169, key64_177,
			};
			// dv feeds accs[off^1]; the keyed product feeds accs[off].
			for (int off = 0; off < 8; off++) {
				uint64_t dv = readU64(p, 8 * off);
				uint64_t dk = dv ^ tailKey[off];
				accs[off ^ 1] += dv;
				accs[off] += uint64_t(uint32_t(dk)) * (dk >> 32);
			}
		}
	}
}

} // namespace detail

// Hash128 returns the 128-bit hash of the byte slice — hash128.go:8.
inline Uint128 hash128(std::string_view sv) {
	using namespace detail;
	const uint8_t* p = reinterpret_cast<const uint8_t*>(sv.data());
	uint64_t l = sv.size();
	Uint128 acc;

	if (l <= 16) {
		if (l > 8) { // 9-16
			constexpr uint64_t bitflipl = key64_032 ^ key64_040;
			constexpr uint64_t bitfliph = key64_048 ^ key64_056;

			uint64_t input_lo = readU64(p, 0);
			uint64_t input_hi = readU64(p, l - 8);

			auto [m128_h0, m128_l] = mul64(input_lo ^ input_hi ^ bitflipl,
			                               prime64_1);
			uint64_t m128_h = m128_h0;

			m128_l += uint64_t(l - 1) << 54;
			input_hi ^= bitfliph;

			m128_h += input_hi + uint64_t(uint32_t(input_hi)) * (prime32_2 - 1);

			m128_l ^= bswap64(m128_h);

			auto [hi2, lo2] = mul64(m128_l, prime64_2);
			acc.Hi = hi2;
			acc.Lo = lo2;
			acc.Hi += m128_h * prime64_2;

			acc.Lo = xxh3Avalanche(acc.Lo);
			acc.Hi = xxh3Avalanche(acc.Hi);

			return acc;
		}
		if (l > 3) { // 4-8
			constexpr uint64_t bitflip = key64_016 ^ key64_024;

			uint64_t input_lo = readU32(p, 0);
			uint64_t input_hi = readU32(p, l - 4);
			uint64_t input_64 = input_lo + (input_hi << 32);
			uint64_t keyed = input_64 ^ bitflip;

			auto [hi, lo] = mul64(keyed, prime64_1 + (l << 2));
			acc.Hi = hi;
			acc.Lo = lo;

			acc.Hi += acc.Lo << 1;
			acc.Lo ^= acc.Hi >> 3;

			acc.Lo ^= acc.Lo >> 35;
			acc.Lo *= 0x9fb21c651e98df25ull;
			acc.Lo ^= acc.Lo >> 28;
			acc.Hi = xxh3Avalanche(acc.Hi);

			return acc;
		}
		if (l == 3) { // 3
			uint64_t c12 = readU16(p, 0);
			uint64_t c3 = readU8(p, 2);
			acc.Lo = (c12 << 16) + c3 + (3 << 8);
		} else if (l > 1) { // 2
			uint64_t c12 = readU16(p, 0);
			acc.Lo = (c12 * ((1 << 24) + 1) >> 8) + (2 << 8);
		} else if (l == 1) { // 1
			uint64_t c1 = readU8(p, 0);
			acc.Lo = c1 * ((1 << 24) + (1 << 16) + 1) + (1 << 8);
		} else { // 0
			return {0x99aa06d3014798d8ull, 0x6001c324468d497full};
		}

		acc.Hi = uint64_t(rotl32(bswap32(uint32_t(acc.Lo)), 13));
		acc.Lo ^= uint64_t(key32_000 ^ key32_004);
		acc.Hi ^= uint64_t(key32_008 ^ key32_012);

		acc.Lo = xxh64AvalancheSmall(acc.Lo);
		acc.Hi = xxh64AvalancheSmall(acc.Hi);

		return acc;
	}
	if (l <= 128) {
		acc.Lo = l * prime64_1;

		if (l > 32) {
			if (l > 64) {
				if (l > 96) {
					uint64_t in8 = readU64(p, l - 8 * 8);
					uint64_t in7 = readU64(p, l - 7 * 8);
					uint64_t i6 = readU64(p, 6 * 8);
					uint64_t i7 = readU64(p, 7 * 8);

					acc.Hi += mulFold64(in8 ^ key64_112, in7 ^ key64_120);
					acc.Hi ^= i6 + i7;
					acc.Lo += mulFold64(i6 ^ key64_096, i7 ^ key64_104);
					acc.Lo ^= in8 + in7;
				} // 96

				uint64_t in6 = readU64(p, l - 6 * 8);
				uint64_t in5 = readU64(p, l - 5 * 8);
				uint64_t i4 = readU64(p, 4 * 8);
				uint64_t i5 = readU64(p, 5 * 8);

				acc.Hi += mulFold64(in6 ^ key64_080, in5 ^ key64_088);
				acc.Hi ^= i4 + i5;
				acc.Lo += mulFold64(i4 ^ key64_064, i5 ^ key64_072);
				acc.Lo ^= in6 + in5;
			} // 64

			uint64_t in4 = readU64(p, l - 4 * 8);
			uint64_t in3 = readU64(p, l - 3 * 8);
			uint64_t i2 = readU64(p, 2 * 8);
			uint64_t i3 = readU64(p, 3 * 8);

			acc.Hi += mulFold64(in4 ^ key64_048, in3 ^ key64_056);
			acc.Hi ^= i2 + i3;
			acc.Lo += mulFold64(i2 ^ key64_032, i3 ^ key64_040);
			acc.Lo ^= in4 + in3;
		} // 32

		uint64_t in2 = readU64(p, l - 2 * 8);
		uint64_t in1 = readU64(p, l - 1 * 8);
		uint64_t i0 = readU64(p, 0 * 8);
		uint64_t i1 = readU64(p, 1 * 8);

		acc.Hi += mulFold64(in2 ^ key64_016, in1 ^ key64_024);
		acc.Hi ^= i0 + i1;
		acc.Lo += mulFold64(i0 ^ key64_000, i1 ^ key64_008);
		acc.Lo ^= in2 + in1;

		Uint128 r;
		r.Hi = (acc.Lo * prime64_1) + (acc.Hi * prime64_4) + (l * prime64_2);
		r.Lo = acc.Hi + acc.Lo;

		r.Hi = ~xxh3Avalanche(r.Hi) + 1; // -xxh3Avalanche
		r.Lo = xxh3Avalanche(r.Lo);

		return r;
	}
	if (l <= 240) {
		acc.Lo = l * prime64_1;

		{
			uint64_t i0 = readU64(p, 0 * 8), i1 = readU64(p, 1 * 8);
			uint64_t i2 = readU64(p, 2 * 8), i3 = readU64(p, 3 * 8);
			acc.Hi += mulFold64(i2 ^ key64_016, i3 ^ key64_024);
			acc.Hi ^= i0 + i1;
			acc.Lo += mulFold64(i0 ^ key64_000, i1 ^ key64_008);
			acc.Lo ^= i2 + i3;
		}
		{
			uint64_t i0 = readU64(p, 4 * 8), i1 = readU64(p, 5 * 8);
			uint64_t i2 = readU64(p, 6 * 8), i3 = readU64(p, 7 * 8);
			acc.Hi += mulFold64(i2 ^ key64_048, i3 ^ key64_056);
			acc.Hi ^= i0 + i1;
			acc.Lo += mulFold64(i0 ^ key64_032, i1 ^ key64_040);
			acc.Lo ^= i2 + i3;
		}
		{
			uint64_t i0 = readU64(p, 8 * 8), i1 = readU64(p, 9 * 8);
			uint64_t i2 = readU64(p, 10 * 8), i3 = readU64(p, 11 * 8);
			acc.Hi += mulFold64(i2 ^ key64_080, i3 ^ key64_088);
			acc.Hi ^= i0 + i1;
			acc.Lo += mulFold64(i0 ^ key64_064, i1 ^ key64_072);
			acc.Lo ^= i2 + i3;
		}
		{
			uint64_t i0 = readU64(p, 12 * 8), i1 = readU64(p, 13 * 8);
			uint64_t i2 = readU64(p, 14 * 8), i3 = readU64(p, 15 * 8);
			acc.Hi += mulFold64(i2 ^ key64_112, i3 ^ key64_120);
			acc.Hi ^= i0 + i1;
			acc.Lo += mulFold64(i0 ^ key64_096, i1 ^ key64_104);
			acc.Lo ^= i2 + i3;
		}

		// avalanche
		acc.Hi = xxh3Avalanche(acc.Hi);
		acc.Lo = xxh3Avalanche(acc.Lo);

		// trailing groups after 128
		size_t top = size_t(l) & ~size_t(31);
		for (size_t i = 4 * 32; i < top; i += 32) {
			uint64_t i0 = readU64(p, i + 0), i1 = readU64(p, i + 8);
			uint64_t i2 = readU64(p, i + 16), i3 = readU64(p, i + 24);
			uint64_t k0 = readU64(key, i - 125), k1 = readU64(key, i - 117);
			uint64_t k2 = readU64(key, i - 109), k3 = readU64(key, i - 101);

			acc.Hi += mulFold64(i2 ^ k2, i3 ^ k3);
			acc.Hi ^= i0 + i1;
			acc.Lo += mulFold64(i0 ^ k0, i1 ^ k1);
			acc.Lo ^= i2 + i3;
		}

		// last 32 bytes
		{
			uint64_t i0 = readU64(p, l - 32), i1 = readU64(p, l - 24);
			uint64_t i2 = readU64(p, l - 16), i3 = readU64(p, l - 8);

			acc.Hi += mulFold64(i0 ^ key64_119, i1 ^ key64_127);
			acc.Hi ^= i2 + i3;
			acc.Lo += mulFold64(i2 ^ key64_103, i3 ^ key64_111);
			acc.Lo ^= i0 + i1;
		}

		Uint128 r;
		r.Hi = (acc.Lo * prime64_1) + (acc.Hi * prime64_4) + (l * prime64_2);
		r.Lo = acc.Hi + acc.Lo;

		r.Hi = ~xxh3Avalanche(r.Hi) + 1; // -xxh3Avalanche
		r.Lo = xxh3Avalanche(r.Lo);

		return r;
	}

	// l > 240 — scalar accumulator.
	acc.Lo = l * prime64_1;
	acc.Hi = ~(l * prime64_2);

	uint64_t accs[8] = {
		prime32_3, prime64_1, prime64_2, prime64_3,
		prime64_4, prime32_2, prime64_5, prime32_1,
	};
	accumScalar(accs, p, l);

	// merge accs
	acc.Lo += mulFold64(accs[0] ^ key64_011, accs[1] ^ key64_019);
	acc.Hi += mulFold64(accs[0] ^ key64_117, accs[1] ^ key64_125);

	acc.Lo += mulFold64(accs[2] ^ key64_027, accs[3] ^ key64_035);
	acc.Hi += mulFold64(accs[2] ^ key64_133, accs[3] ^ key64_141);

	acc.Lo += mulFold64(accs[4] ^ key64_043, accs[5] ^ key64_051);
	acc.Hi += mulFold64(accs[4] ^ key64_149, accs[5] ^ key64_157);

	acc.Lo += mulFold64(accs[6] ^ key64_059, accs[7] ^ key64_067);
	acc.Hi += mulFold64(accs[6] ^ key64_165, accs[7] ^ key64_173);

	acc.Lo = xxh3Avalanche(acc.Lo);
	acc.Hi = xxh3Avalanche(acc.Hi);

	return acc;
}

} // namespace tsc::xxh3
