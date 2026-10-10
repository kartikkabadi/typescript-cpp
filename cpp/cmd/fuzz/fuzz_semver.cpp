// fuzz_semver — libFuzzer driver for the semver surface.
//
// Feeds raw bytes through the two untrusted-input entry points of
// tsc/internal/semver (package.json "typesVersions" keys, dependency
// ranges, and `tryParseVersion` on arbitrary text):
//   semver::TryParseVersion      — version.go
//   semver::TryParseVersionRange — version_range.go (hyphen ranges,
//     comparators, logical-or, wildcards, prerelease/build metadata)
// When both halves parse, the driver's second byte picks a Version to
// Test() against the range — exercising comparator evaluation, not just
// parsing.
//
// Layout: data[0] = split point selector, data[1] = op selector,
// data[2..split] = version text, data[split..] = range text.

#include <cstdint>
#include <string_view>

#include "internal/semver/semver.h"

#include "cmd/fuzz/fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size < 3) {
		return 0;
	}
	size_t split = 2 + (static_cast<size_t>(data[0]) * (size - 2)) / 256;
	uint8_t op = data[1];
	std::string_view verText(
	    reinterpret_cast<const char*>(data + 2), split - 2);
	std::string_view rangeText(
	    reinterpret_cast<const char*>(data + split), size - split);

	fuzz::runOnBigStack(
	    [verText, rangeText, op](const uint8_t*, size_t) {
		    // Whole-input forms: the same bytes as version and as range.
		    switch (op % 4) {
		    case 0: {
			    auto [v, ok] = tsc::semver::TryParseVersion(verText);
			    auto [r, rok] =
			        tsc::semver::TryParseVersionRange(rangeText);
			    if (ok && rok) {
				    (void)r.Test(&v);
			    }
			    break;
		    }
		    case 1: {
			    auto [v, ok] = tsc::semver::TryParseVersion(verText);
			    if (ok) {
				    // Compare against a second parse of the range
				    // text — exercises comparePreReleaseIdentifiers
				    // and numeric-component comparison.
				    auto [v2, ok2] =
				        tsc::semver::TryParseVersion(rangeText);
				    if (ok2) {
					    (void)tsc::semver::versionCompare(&v, &v2);
				    }
			    }
			    break;
		    }
		    case 2: {
			    auto [r, rok] =
			        tsc::semver::TryParseVersionRange(rangeText);
			    if (rok) {
				    // Test against the two boundary versions the
				    // package.json cache itself consults.
				    (void)r.Test(&tsc::semver::versionZero());
				    auto [v, ok] =
				        tsc::semver::TryParseVersion(verText);
				    if (ok) {
					    (void)r.Test(&v);
				    }
			    }
			    break;
		    }
		    default: {
			    // Both as ranges: alternative parsing +
			    // hyphen/comparator paths on both halves.
			    (void)tsc::semver::TryParseVersionRange(verText);
			    (void)tsc::semver::TryParseVersionRange(rangeText);
			    break;
		    }
		    }
	    },
	    data, 0);
	return 0;
}
