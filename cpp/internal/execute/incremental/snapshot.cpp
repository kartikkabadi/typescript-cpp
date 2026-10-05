// Port of tsc/internal/execute/incremental/snapshot.go — FileInfo,
// FileEmitKind logic, emitSignature, the build-info diagnostic round-trip,
// the snapshot record itself, and ComputeHash (Go xxh3.HashString128 —
// scalar XXH3_128bits port at the bottom of the file).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "internal/checker/checker.h"
#include "internal/execute/incremental/incremental.h"

namespace tsc::execute::incremental {
namespace {

// ---------------------------------------------------------------------------
// XXH3-128 (default secret, seed 0) — scalar port of the reference algorithm
// used by Go's github.com/zeebo/xxh3 HashString128. Output is the canonical
// 128-bit hash (Hi, Lo).
// ---------------------------------------------------------------------------

constexpr uint64_t PRIME32_1 = 0x9E3779B1ULL;
constexpr uint64_t PRIME32_2 = 0x85EBCA77ULL;
constexpr uint64_t PRIME32_3 = 0xC2B2AE3DULL;
constexpr uint64_t PRIME64_1 = 0x9E3779B185EBCA87ULL;
constexpr uint64_t PRIME64_2 = 0xC2B2AE3D27D4EB4FULL;
constexpr uint64_t PRIME64_3 = 0x165667B19E3779F9ULL;
constexpr uint64_t PRIME64_4 = 0x85EBCA77C2B2AE63ULL;
constexpr uint64_t PRIME64_5 = 0x27D4EB2F165667C5ULL;
constexpr uint64_t PRIME_MX1 = 0x165667919E3779F9ULL;
constexpr uint64_t PRIME_MX2 = 0x9FB21C651E98DF25ULL;

// XXH3_kSecret — 192-byte default secret.
constexpr uint8_t kSecret[192] = {
	0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe,
	0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
	0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb,
	0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
	0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78,
	0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
	0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e,
	0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
	0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb,
	0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
	0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e,
	0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
	0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f,
	0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
	0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31,
	0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
	0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3,
	0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
	0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49,
	0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
	0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc,
	0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
	0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28,
	0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};

struct Hash128 {
	uint64_t lo = 0, hi = 0;
};

uint64_t readLE32(const uint8_t* p) {
	uint32_t v;
	std::memcpy(&v, p, 4);
	return v; // little-endian host assumed (matches xxhash fast path)
}

uint64_t readLE64(const uint8_t* p) {
	uint64_t v;
	std::memcpy(&v, p, 8);
	return v;
}

uint32_t rotl32(uint32_t x, int r) {
	return (x << r) | (x >> (32 - r));
}

uint32_t bswap32(uint32_t x) {
	return __builtin_bswap32(x);
}

uint64_t bswap64(uint64_t x) { return __builtin_bswap64(x); }

uint64_t xorshift64(uint64_t v, int s) { return v ^ (v >> s); }

// XXH64_avalanche
uint64_t avalanche64(uint64_t h) {
	h ^= h >> 33;
	h *= PRIME64_2;
	h ^= h >> 29;
	h *= PRIME64_3;
	h ^= h >> 32;
	return h;
}

// XXH3_avalanche
uint64_t avalanche3(uint64_t h) {
	h = xorshift64(h, 37);
	h *= PRIME_MX1;
	h = xorshift64(h, 32);
	return h;
}

// XXH_mult32to64
uint64_t mult32to64(uint32_t a, uint32_t b) {
	return static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
}

// XXH_mult64to128
Hash128 mult64to128(uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__)
	__uint128_t product = static_cast<__uint128_t>(a) * b;
	return {static_cast<uint64_t>(product),
	        static_cast<uint64_t>(product >> 64)};
#else
	uint64_t lo_lo, hi_hi, hi_lo, lo_hi, cross, upper, lower, middle;
	// 4x32 decomposition
	uint64_t a_lo = a & 0xFFFFFFFFULL, a_hi = a >> 32;
	uint64_t b_lo = b & 0xFFFFFFFFULL, b_hi = b >> 32;
	uint64_t p_lo_lo = a_lo * b_lo;
	uint64_t p_lo_hi = a_lo * b_hi;
	uint64_t p_hi_lo = a_hi * b_lo;
	uint64_t p_hi_hi = a_hi * b_hi;
	middle = p_lo_hi + (p_lo_lo >> 32);
	cross = middle + p_hi_lo;
	upper = p_hi_hi + (middle >> 32) + (cross < middle ? (1ULL << 32) : 0) +
	        (cross >> 32);
	lower = (cross << 32) | (p_lo_lo & 0xFFFFFFFFULL);
	return {lower, upper};
#endif
}

// XXH3_mul128_fold64
uint64_t mul128_fold64(uint64_t a, uint64_t b) {
	auto p = mult64to128(a, b);
	return p.lo ^ p.hi;
}

// XXH3_mix16B
uint64_t mix16B(const uint8_t* input, const uint8_t* secret, uint64_t seed) {
	uint64_t input_lo = readLE64(input);
	uint64_t input_hi = readLE64(input + 8);
	return mul128_fold64(input_lo ^ (readLE64(secret) + seed),
	                     input_hi ^ (readLE64(secret + 8) - seed));
}

// XXH128_mix32B
Hash128 mix32B(Hash128 acc, const uint8_t* in1, const uint8_t* in2,
               const uint8_t* secret, uint64_t seed) {
	acc.lo += mix16B(in1, secret, seed);
	acc.lo ^= readLE64(in2) + readLE64(in2 + 8);
	acc.hi += mix16B(in2, secret + 16, seed);
	acc.hi ^= readLE64(in1) + readLE64(in1 + 8);
	return acc;
}

// XXH3_len_1to3_128b
Hash128 len_1to3_128b(const uint8_t* input, size_t len,
                      const uint8_t* secret, uint64_t seed) {
	uint8_t c1 = input[0];
	uint8_t c2 = input[len >> 1];
	uint8_t c3 = input[len - 1];
	uint32_t combinedl = (static_cast<uint32_t>(c1) << 16) |
	                     (static_cast<uint32_t>(c2) << 24) |
	                     (static_cast<uint32_t>(c3)) |
	                     (static_cast<uint32_t>(len) << 8);
	uint32_t combinedh = rotl32(bswap32(combinedl), 13);
	uint64_t bitflipl =
	    (readLE32(secret) ^ readLE32(secret + 4)) + seed;
	uint64_t bitfliph =
	    (readLE32(secret + 8) ^ readLE32(secret + 12)) - seed;
	uint64_t keyed_lo = static_cast<uint64_t>(combinedl) ^ bitflipl;
	uint64_t keyed_hi = static_cast<uint64_t>(combinedh) ^ bitfliph;
	return {avalanche64(keyed_lo), avalanche64(keyed_hi)};
}

// XXH3_len_4to8_128b
Hash128 len_4to8_128b(const uint8_t* input, size_t len,
                      const uint8_t* secret, uint64_t seed) {
	seed ^= (static_cast<uint64_t>(readLE32(secret)) << 32) |
	        readLE32(secret + 4);
	uint32_t input_lo = readLE32(input);
	uint32_t input_hi = readLE32(input + len - 4);
	uint64_t input_64 =
	    input_lo + (static_cast<uint64_t>(input_hi) << 32);
	uint64_t bitflip =
	    (readLE64(secret + 16) ^ readLE64(secret + 24)) + seed;
	uint64_t keyed = input_64 ^ bitflip;
	Hash128 m128 =
	    mult64to128(keyed, PRIME64_1 + (static_cast<uint64_t>(len) << 2));
	m128.hi += (m128.lo << 1);
	m128.lo ^= (m128.hi >> 3);
	m128.lo = xorshift64(m128.lo, 35);
	m128.lo *= PRIME_MX2;
	m128.lo = xorshift64(m128.lo, 28);
	m128.hi = avalanche3(m128.hi);
	return m128;
}

// XXH3_len_9to16_128b
Hash128 len_9to16_128b(const uint8_t* input, size_t len,
                       const uint8_t* secret, uint64_t seed) {
	uint64_t bitflipl =
	    (readLE64(secret + 32) ^ readLE64(secret + 40)) - seed;
	uint64_t bitfliph =
	    (readLE64(secret + 48) ^ readLE64(secret + 56)) + seed;
	uint64_t input_lo = readLE64(input);
	uint64_t input_hi = readLE64(input + len - 8);
	Hash128 m128 =
	    mult64to128(input_lo ^ input_hi ^ bitflipl, PRIME64_1);
	m128.lo += static_cast<uint64_t>(len - 1) << 54;
	input_hi ^= bitfliph;
	// Reference code says "Add the high 32 bits of input_hi" but the
	// implementation (and Go xxh3) uses the low 32 bits — verified against
	// XXH3_128bits outputs.
	m128.hi += input_hi + mult32to64(static_cast<uint32_t>(input_hi),
	                                 PRIME32_2 - 1);
	m128.lo ^= bswap64(m128.hi);
	// 128x64 multiply: h128 = m128 * PRIME64_2.
	auto h128 = mult64to128(m128.lo, PRIME64_2);
	h128.hi += m128.hi * PRIME64_2;
	h128.lo = avalanche3(h128.lo);
	h128.hi = avalanche3(h128.hi);
	return h128;
}

// XXH3_len_0to16_128b
Hash128 len_0to16_128b(const uint8_t* input, size_t len,
                       const uint8_t* secret, uint64_t seed) {
	if (len > 8) return len_9to16_128b(input, len, secret, seed);
	if (len >= 4) return len_4to8_128b(input, len, secret, seed);
	if (len) return len_1to3_128b(input, len, secret, seed);
	return {avalanche64(seed ^ readLE64(secret + 64) ^
	                    readLE64(secret + 72)),
	        avalanche64(seed ^ readLE64(secret + 80) ^
	                    readLE64(secret + 88))};
}

// XXH3_len_17to128_128b
Hash128 len_17to128_128b(const uint8_t* input, size_t len,
                         const uint8_t* secret, uint64_t seed) {
	Hash128 acc{len * PRIME64_1, 0};
	if (len > 32) {
		if (len > 64) {
			if (len > 96) {
				acc = mix32B(acc, input + 48, input + len - 64,
				             secret + 96, seed);
			}
			acc = mix32B(acc, input + 32, input + len - 48,
			             secret + 64, seed);
		}
		acc = mix32B(acc, input + 16, input + len - 32, secret + 32,
		             seed);
	}
	acc = mix32B(acc, input, input + len - 16, secret, seed);
	Hash128 h128;
	h128.lo = acc.lo + acc.hi;
	h128.hi = acc.lo * PRIME64_1 + acc.hi * PRIME64_4 +
	          (static_cast<uint64_t>(len) - seed) * PRIME64_2;
	h128.lo = avalanche3(h128.lo);
	h128.hi = 0ULL - avalanche3(h128.hi);
	return h128;
}

constexpr size_t MIDSIZE_STARTOFFSET = 3;
constexpr size_t MIDSIZE_LASTOFFSET = 17;
constexpr size_t SECRET_SIZE_MIN = 136;

// XXH3_len_129to240_128b
Hash128 len_129to240_128b(const uint8_t* input, size_t len,
                          const uint8_t* secret, uint64_t seed) {
	Hash128 acc{len * PRIME64_1, 0};
	int nbRounds = static_cast<int>(len) / 32;
	for (int i = 0; i < 4; i++) {
		acc = mix32B(acc, input + 32 * i, input + 32 * i + 16,
		             secret + 32 * i, seed);
	}
	acc.lo = avalanche3(acc.lo);
	acc.hi = avalanche3(acc.hi);
	for (int i = 4; i < nbRounds; i++) {
		acc = mix32B(acc, input + 32 * i, input + 32 * i + 16,
		             secret + MIDSIZE_STARTOFFSET + 32 * (i - 4), seed);
	}
	acc = mix32B(acc, input + len - 16, input + len - 32,
	             secret + SECRET_SIZE_MIN - MIDSIZE_LASTOFFSET - 16,
	             0ULL - seed);
	Hash128 h128;
	h128.lo = acc.lo + acc.hi;
	h128.hi = acc.lo * PRIME64_1 + acc.hi * PRIME64_4 +
	          (static_cast<uint64_t>(len) - seed) * PRIME64_2;
	h128.lo = avalanche3(h128.lo);
	h128.hi = 0ULL - avalanche3(h128.hi);
	return h128;
}

constexpr size_t STRIPE_LEN = 64;
constexpr size_t SECRET_CONSUME_RATE = 8;
constexpr size_t SECRET_LASTACC_START = 7;
constexpr size_t SECRET_MERGEACCS_START = 11;

// XXH3_accumulate_512 (scalar)
void accumulate_512(uint64_t* acc, const uint8_t* input,
                    const uint8_t* secret) {
	for (int i = 0; i < 8; i++) {
		uint64_t data_val = readLE64(input + 8 * i);
		uint64_t data_key = data_val ^ readLE64(secret + 8 * i);
		acc[i ^ 1] += data_val; // swap adjacent lanes
		acc[i] += mult32to64(static_cast<uint32_t>(data_key & 0xFFFFFFFFULL),
		                     static_cast<uint32_t>(data_key >> 32));
	}
}

// XXH3_scrambleAcc (scalar)
void scrambleAcc(uint64_t* acc, const uint8_t* secret) {
	for (int i = 0; i < 8; i++) {
		uint64_t key64 = readLE64(secret + 8 * i);
		uint64_t acc64 = acc[i];
		acc64 = xorshift64(acc64, 47);
		acc64 ^= key64;
		acc64 *= PRIME32_1;
		acc[i] = acc64;
	}
}

// XXH3_accumulate
void accumulate(uint64_t* acc, const uint8_t* input,
                const uint8_t* secret, size_t nbStripes) {
	for (size_t n = 0; n < nbStripes; n++) {
		accumulate_512(acc, input + n * STRIPE_LEN,
		               secret + n * SECRET_CONSUME_RATE);
	}
}

// XXH3_hashLong_internal_loop
void hashLong_loop(uint64_t* acc, const uint8_t* input, size_t len,
                   const uint8_t* secret, size_t secretSize) {
	size_t nb_rounds = (secretSize - STRIPE_LEN) / SECRET_CONSUME_RATE;
	size_t block_len = STRIPE_LEN * nb_rounds;
	size_t nb_blocks = (len - 1) / block_len;
	for (size_t n = 0; n < nb_blocks; n++) {
		accumulate(acc, input + n * block_len, secret, nb_rounds);
		scrambleAcc(acc, secret + secretSize - STRIPE_LEN);
	}
	// last partial block
	size_t nbStripes =
	    ((len - 1) - (block_len * nb_blocks)) / STRIPE_LEN;
	accumulate(acc, input + nb_blocks * block_len, secret, nbStripes);
	// last stripe
	const uint8_t* p = input + len - STRIPE_LEN;
	accumulate_512(acc, p,
	               secret + secretSize - STRIPE_LEN - SECRET_LASTACC_START);
}

// XXH3_mix2Accs
uint64_t mix2Accs(const uint64_t* acc, const uint8_t* secret) {
	return mul128_fold64(acc[0] ^ readLE64(secret),
	                     acc[1] ^ readLE64(secret + 8));
}

// XXH3_mergeAccs
uint64_t mergeAccs(const uint64_t* acc, const uint8_t* secret,
                   uint64_t start) {
	uint64_t result64 = start;
	for (int i = 0; i < 4; i++) {
		result64 += mix2Accs(acc + 2 * i, secret + 16 * i);
	}
	return avalanche3(result64);
}

// XXH3_128bits (default secret, seed 0)
Hash128 xxh3_128bits(std::string_view input) {
	const uint8_t* in = reinterpret_cast<const uint8_t*>(input.data());
	size_t len = input.size();
	uint64_t seed = 0;
	if (len <= 16) return len_0to16_128b(in, len, kSecret, seed);
	if (len <= 128) return len_17to128_128b(in, len, kSecret, seed);
	if (len <= 240) return len_129to240_128b(in, len, kSecret, seed);
	// XXH3_hashLong_128b_default — accumulate with kSecret, merge.
	uint64_t acc[8] = {PRIME32_3, PRIME64_1, PRIME64_2, PRIME64_3,
	                   PRIME64_4, PRIME32_2, PRIME64_5, PRIME32_1};
	hashLong_loop(acc, in, len, kSecret, sizeof(kSecret));
	Hash128 h128;
	h128.lo = mergeAccs(acc, kSecret + SECRET_MERGEACCS_START,
	                    static_cast<uint64_t>(len) * PRIME64_1);
	h128.hi = mergeAccs(acc, kSecret + sizeof(kSecret) - STRIPE_LEN -
	                             SECRET_MERGEACCS_START,
	                    ~(static_cast<uint64_t>(len) * PRIME64_2));
	return h128;
}

}  // namespace

// snapshot.go:32 ComputeHash — hex(hi BE, lo BE) of xxh3-128(text);
// hashWithText appends "-" + text.
std::string ComputeHash(std::string_view text, bool hashWithText) {
	auto h = xxh3_128bits(text);
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%016llx%016llx",
	              static_cast<unsigned long long>(h.hi),
	              static_cast<unsigned long long>(h.lo));
	std::string hash(buf, sizeof(buf));
	if (hashWithText) {
		hash += "-";
		hash += text;
	}
	return hash;
}

// snapshot.go:59 GetFileEmitKind.
FileEmitKind GetFileEmitKind(const CompilerOptions* options) {
	FileEmitKind result = FileEmitKindJs;
	if (options->SourceMap == Tristate::True) {
		result |= FileEmitKindJsMap;
	}
	if (options->InlineSourceMap == Tristate::True) {
		result |= FileEmitKindJsInlineMap;
	}
	if (options->GetEmitDeclarations()) {
		result |= FileEmitKindDts;
	}
	if (options->DeclarationMap == Tristate::True) {
		result |= FileEmitKindDtsMap;
	}
	if (options->EmitDeclarationOnly == Tristate::True) {
		result &= FileEmitKindAllDts;
	}
	return result;
}

// snapshot.go:79 getPendingEmitKindWithOptions.
FileEmitKind getPendingEmitKindWithOptions(
    const CompilerOptions* options, const CompilerOptions* oldOptions) {
	auto oldEmitKind = GetFileEmitKind(oldOptions);
	auto newEmitKind = GetFileEmitKind(options);
	return getPendingEmitKind(newEmitKind, oldEmitKind);
}

// snapshot.go:85 getPendingEmitKind.
FileEmitKind getPendingEmitKind(FileEmitKind emitKind,
                                FileEmitKind oldEmitKind) {
	if (oldEmitKind == emitKind) {
		return FileEmitKindNone;
	}
	if (oldEmitKind == 0 || emitKind == 0) {
		return emitKind;
	}
	FileEmitKind diff = oldEmitKind ^ emitKind;
	FileEmitKind result = FileEmitKindNone;
	// If there is diff in Js emit, pending emit is js emit flags
	if ((diff & FileEmitKindAllJs) != 0) {
		result |= emitKind & FileEmitKindAllJs;
	}
	// If dts errors pending, add dts errors flag
	if ((diff & FileEmitKindDtsErrors) != 0) {
		result |= emitKind & FileEmitKindAllDts;
	}
	// If there is diff in Dts emit, pending emit is dts emit flags
	if ((diff & FileEmitKindAllDtsEmit) != 0) {
		result |= emitKind & FileEmitKindAllDtsEmit;
	}
	return result;
}

// snapshot.go:118 emitSignature.getNewEmitSignature — if the declaration
// map option differs, swap the stored format.
emitSignature* emitSignature::getNewEmitSignature(
    const CompilerOptions* oldOptions, const CompilerOptions* newOptions) {
	if ((oldOptions->DeclarationMap == Tristate::True) ==
	    (newOptions->DeclarationMap == Tristate::True)) {
		return this;
	}
	if (!signatureWithDifferentOptions.has_value()) {
		auto* e = new emitSignature();
		e->signatureWithDifferentOptions =
		    std::vector<std::string>{signature};
		return e;
	}
	auto* e = new emitSignature();
	e->signature = (*signatureWithDifferentOptions)[0];
	return e;
}

// snapshot.go:158 buildInfoDiagnosticWithFileName.toDiagnostic.
Diagnostic* buildInfoDiagnosticWithFileName::toDiagnostic(
    compiler::SimpleProgram* p, SourceFile* file) {
	SourceFile* fileForDiagnostic = nullptr;
	if (!this->file.empty()) {
		fileForDiagnostic = p->GetSourceFileByPath(this->file);
	} else if (!noFile) {
		fileForDiagnostic = file;
	}

	if (repopulateInfo != nullptr) {
		return repopulateDiagnosticChain(this, p, fileForDiagnostic);
	}

	std::vector<Diagnostic*> messageChainDiags;
	messageChainDiags.reserve(messageChain.size());
	for (auto* msg : messageChain) {
		messageChainDiags.push_back(msg->toDiagnostic(p, fileForDiagnostic));
	}
	std::vector<Diagnostic*> relatedInformationDiags;
	relatedInformationDiags.reserve(relatedInformation.size());
	for (auto* info : relatedInformation) {
		relatedInformationDiags.push_back(
		    info->toDiagnostic(p, fileForDiagnostic));
	}
	Diagnostic* diagnostic = NewDiagnosticFromSerialized(
	    fileForDiagnostic, TextRange{pos, end}, code, category,
	    messageKey.c_str(), messageArgs, std::move(messageChainDiags),
	    std::move(relatedInformationDiags), reportsUnnecessary,
	    reportsDeprecated, skippedOnNoEmit);
	if (!source.empty() || !messageText.empty()) {
		diagnostic->SetExternalData(source, messageText);
	}
	return diagnostic;
}

// snapshot.go:199 repopulateDiagnosticChain — recomputes a diagnostic
// chain entry that depends on program state which may have changed
// between incremental builds.
Diagnostic* repopulateDiagnosticChain(
    buildInfoDiagnosticWithFileName* b, compiler::SimpleProgram* p,
    SourceFile* file) {
	auto* info = b->repopulateInfo;
	switch (info->kind) {
	case RepopulateDiagnosticKind::ModeMismatch:
		return repopulateModeMismatchChain(b, p, file);
	case RepopulateDiagnosticKind::ModuleNotFound:
		return repopulateModuleNotFoundChain(b, p, file, info);
	default:
		// Fall back to using the stored (possibly stale) data
		return b->toDiagnosticWithoutRepopulate(p, file);
	}
}

// snapshot.go:212 toDiagnosticWithoutRepopulate.
Diagnostic* buildInfoDiagnosticWithFileName::toDiagnosticWithoutRepopulate(
    compiler::SimpleProgram* p, SourceFile* file) {
	std::vector<Diagnostic*> messageChainDiags;
	messageChainDiags.reserve(messageChain.size());
	for (auto* msg : messageChain) {
		messageChainDiags.push_back(msg->toDiagnostic(p, file));
	}
	std::vector<Diagnostic*> relatedInformationDiags;
	relatedInformationDiags.reserve(relatedInformation.size());
	for (auto* info : relatedInformation) {
		relatedInformationDiags.push_back(info->toDiagnostic(p, file));
	}
	return NewDiagnosticFromSerialized(
	    file, TextRange{pos, end}, code, category, messageKey.c_str(),
	    messageArgs, std::move(messageChainDiags),
	    std::move(relatedInformationDiags), reportsUnnecessary,
	    reportsDeprecated, skippedOnNoEmit);
}

// snapshot.go:236 repopulateModeMismatchChain.
Diagnostic* repopulateModeMismatchChain(
    buildInfoDiagnosticWithFileName* b, compiler::SimpleProgram* p,
    SourceFile* file) {
	if (file == nullptr) {
		return b->toDiagnosticWithoutRepopulate(p, file);
	}

	auto details = checker::CreateModeMismatchDetails(p, file);

	std::vector<Diagnostic*> nextChain;
	nextChain.reserve(b->messageChain.size());
	for (auto* msg : b->messageChain) {
		nextChain.push_back(msg->toDiagnostic(p, file));
	}

	return NewDiagnosticFromSerialized(
	    file, TextRange{b->pos, b->end}, details.message->code,
	    details.message->category, details.message->key, details.args,
	    std::move(nextChain), {}, false, false, false);
}

// snapshot.go:263 repopulateModuleNotFoundChain.
Diagnostic* repopulateModuleNotFoundChain(
    buildInfoDiagnosticWithFileName* b, compiler::SimpleProgram* p,
    SourceFile* file, RepopulateDiagnosticInfo* info) {
	if (file == nullptr) {
		return b->toDiagnosticWithoutRepopulate(p, file);
	}

	std::string packageName = info->packageName;
	if (packageName.empty()) {
		packageName = info->moduleReference;
	}

	auto details = checker::CreateModuleNotFoundChain(
	    p, file, info->moduleReference, info->mode, packageName);

	std::vector<Diagnostic*> nextChain;
	nextChain.reserve(b->messageChain.size());
	for (auto* msg : b->messageChain) {
		nextChain.push_back(msg->toDiagnostic(p, file));
	}

	return NewDiagnosticFromSerialized(
	    file, TextRange{b->pos, b->end}, details.message->code,
	    details.message->category, details.message->key, details.args,
	    std::move(nextChain), {}, false, false, false);
}

// snapshot.go:295 getDiagnostics — converts and caches like Go.
std::vector<Diagnostic*>
DiagnosticsOrBuildInfoDiagnosticsWithFileName::getDiagnostics(
    compiler::SimpleProgram* p, SourceFile* file) {
	if (!diagnostics.empty()) {
		return diagnostics;
	}
	// Convert and cache the diagnostics
	diagnostics.reserve(buildInfoDiagnostics.size());
	for (auto* diag : buildInfoDiagnostics) {
		diagnostics.push_back(diag->toDiagnostic(p, file));
	}
	return diagnostics;
}

// snapshot.go:354 addFileToChangeSet.
void snapshot::addFileToChangeSet(const tspath::Path& filePath) {
	changedFilesSet.Add(filePath);
	buildInfoEmitPending.store(true);
}

// snapshot.go:359 addFileToAffectedFilesPendingEmit.
void snapshot::addFileToAffectedFilesPendingEmit(
    const tspath::Path& filePath, FileEmitKind emitKind) {
	auto [existingKind, _] = affectedFilesPendingEmit.Load(filePath);
	affectedFilesPendingEmit.Store(filePath, existingKind | emitKind);
	if ((emitKind & FileEmitKindDtsErrors) != 0) {
		emitDiagnosticsPerFile.Delete(filePath);
	}
	buildInfoEmitPending.store(true);
}

// snapshot.go:368 getAllFilesExcludingDefaultLibraryFile.
std::vector<SourceFile*> snapshot::getAllFilesExcludingDefaultLibraryFile(
    compiler::SimpleProgram* program, SourceFile* firstSourceFile) {
	allFilesExcludingDefaultLibraryFileOnce.run([&]() {
		auto files = program->GetSourceFiles();
		allFilesExcludingDefaultLibraryFile.reserve(files.size());
		auto addSourceFile = [&](SourceFile* file) {
			if (!program->IsSourceFileDefaultLibrary(file->Path())) {
				allFilesExcludingDefaultLibraryFile.push_back(file);
			}
		};
		if (firstSourceFile != nullptr) {
			addSourceFile(firstSourceFile);
		}
		for (auto* file : files) {
			if (file != firstSourceFile) {
				addSourceFile(file);
			}
		}
	});
	return allFilesExcludingDefaultLibraryFile;
}

// snapshot.go:389 getTextHandlingSourceMapForSignature.
std::string_view getTextHandlingSourceMapForSignature(
    std::string_view text, const compiler::WriteFileData* data) {
	if (data->SourceMapUrlPos != -1) {
		return text.substr(0, data->SourceMapUrlPos);
	}
	return text;
}

// snapshot.go:422 diagnosticToStringBuilder — file-local (Go order kept
// below); fwd decl for computeSignatureWithDiagnostics.
void diagnosticToStringBuilder(Diagnostic* diagnostic, SourceFile* file,
                               std::string& builder);

// snapshot.go:396 computeSignatureWithDiagnostics.
std::string snapshot::computeSignatureWithDiagnostics(
    SourceFile* file, std::string_view text,
    const compiler::WriteFileData* data) {
	std::string builder;
	builder.append(
	    getTextHandlingSourceMapForSignature(text, data));
	for (auto* diag : data->Diagnostics) {
		diagnosticToStringBuilder(diag, file, builder);
	}
	return computeHash(builder);
}

// Category.Name — uses tsc::categoryName from diagnostics.h (the
// file-local duplicate was removed when diagnostics.h grew the real one).

// snapshot.go:405 diagnosticToStringBuilder — writes into a std::string.
void diagnosticToStringBuilder(Diagnostic* diagnostic, SourceFile* file,
                               std::string& builder) {
	if (diagnostic == nullptr) {
		return;
	}
	builder += "\n";
	if (diagnostic->File() != file) {
		builder += tspath::ensurePathIsNonModuleName(
		    tspath::getRelativePathFromDirectory(
		        tspath::getDirectoryPath(file->Path()),
		        diagnostic->File()->Path(),
		        tspath::ComparePathsOptions{}));
	}
	if (diagnostic->File() != nullptr) {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "(%d,%d): ", diagnostic->Pos(),
		              diagnostic->Len());
		builder += buf;
	}
	builder += categoryName(diagnostic->Category());
	char codeBuf[24];
	std::snprintf(codeBuf, sizeof(codeBuf), "%d: ", diagnostic->Code());
	builder += codeBuf;
	builder += diagnostic->MessageKey();
	builder += "\n";
	for (const auto& arg : diagnostic->MessageArgs()) {
		builder += arg;
		builder += "\n";
	}
	for (auto* chain : diagnostic->MessageChain()) {
		diagnosticToStringBuilder(chain, file, builder);
	}
	for (auto* info : diagnostic->RelatedInformation()) {
		diagnosticToStringBuilder(info, file, builder);
	}
}

// snapshot.go:436 computeHash.
std::string snapshot::computeHash(std::string_view text) {
	return ComputeHash(text, hashWithText);
}

// snapshot.go:440 canUseIncrementalState.
bool snapshot::canUseIncrementalState() const {
	if (!options->IsIncremental() &&
	    options->Build == Tristate::True) {
		// If not incremental build (with tsc -b), we don't need to track
		// state except diagnostics per file so we can use it
		return false;
	}
	return true;
}

}  // namespace tsc::execute::incremental
