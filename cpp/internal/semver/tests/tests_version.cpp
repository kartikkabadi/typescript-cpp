// tests_version.cpp — port of tsc/internal/semver/version_test.go.
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/semver/semver.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using tsc::semver::Version;
using tsc::semver::versionCompare;

namespace {

void assertVersion(T* t, const Version& a, const Version& b) {
	assert::Equal(t, a.major, b.major);
	assert::Equal(t, a.minor, b.minor);
	assert::Equal(t, a.patch, b.patch);
	assert::Assert(t, a.prerelease == b.prerelease);
	assert::Assert(t, a.build == b.build);
}

void TestTryParseSemver(T* t) {
	t->Parallel();
	struct {
		const char* in;
		Version out;
	} tests[] = {
	    {"1.2.3-pre.4+build.5",
	     Version{1, 2, 3, {"pre", "4"}, {"build", "5"}}},
	    {"1.2.3-pre.4", Version{1, 2, 3, {"pre", "4"}, {}}},
	    {"1.2.3+build.4", Version{1, 2, 3, {}, {"build", "4"}}},
	    {"1.2.3", Version{1, 2, 3, {}, {}}},
	};

	for (auto& test : tests) {
		t->Run(test.in, [test](T* t) {
			t->Parallel();
			auto [v, ok] = tsc::semver::TryParseVersion(test.in);
			assert::Assert(t, ok);
			assertVersion(t, v, test.out);
		});
	}
}

void TestVersionString(T* t) {
	t->Parallel();
	struct {
		Version in;
		const char* out;
	} tests[] = {
	    {Version{1, 2, 3, {"pre", "4"}, {"build", "5"}},
	     "1.2.3-pre.4+build.5"},
	    {Version{1, 2, 3, {"pre", "4"}, {"build"}},
	     "1.2.3-pre.4+build"},
	    {Version{1, 2, 3, {}, {"build"}}, "1.2.3+build"},
	    {Version{1, 2, 3, {"pre", "4"}, {}}, "1.2.3-pre.4"},
	    {Version{1, 2, 3, {}, {"build", "4"}}, "1.2.3+build.4"},
	    {Version{1, 2, 3, {}, {}}, "1.2.3"},
	};

	for (auto& test : tests) {
		t->Run(test.out, [test](T* t) {
			t->Parallel();
			assert::Equal(t, test.in.String(),
			              std::string(test.out));
		});
	}
}

void TestVersionCompare(T* t) {
	t->Parallel();
	constexpr int comparisonLessThan = tsc::semver::comparisonLessThan;
	constexpr int comparisonGreaterThan =
	    tsc::semver::comparisonGreaterThan;
	constexpr int comparisonEqualTo = tsc::semver::comparisonEqualTo;

	struct {
		const char* v1;
		const char* v2;
		int want;
	} tests[] = {
	    // https://semver.org/#spec-item-11
	    // > Precedence is determined by the first difference when
	    // > comparing each of these identifiers from left to right as
	    // > follows: Major, minor, and patch versions are always
	    // > compared numerically.
	    {"1.0.0", "2.0.0", comparisonLessThan},
	    {"1.0.0", "1.1.0", comparisonLessThan},
	    {"1.0.0", "1.0.1", comparisonLessThan},
	    {"2.0.0", "1.0.0", comparisonGreaterThan},
	    {"1.1.0", "1.0.0", comparisonGreaterThan},
	    {"1.0.1", "1.0.0", comparisonGreaterThan},
	    {"1.0.0", "1.0.0", comparisonEqualTo},

	    // > When major, minor, and patch are equal, a pre-release
	    // > version has lower precedence than a normal version.
	    {"1.0.0", "1.0.0-pre", comparisonGreaterThan},
	    {"1.0.1-pre", "1.0.0", comparisonGreaterThan},
	    {"1.0.0-pre", "1.0.0", comparisonLessThan},

	    // > identifiers consisting of only digits are compared
	    // > numerically
	    {"1.0.0-0", "1.0.0-1", comparisonLessThan},
	    {"1.0.0-1", "1.0.0-0", comparisonGreaterThan},
	    {"1.0.0-2", "1.0.0-10", comparisonLessThan},
	    {"1.0.0-10", "1.0.0-2", comparisonGreaterThan},
	    {"1.0.0-0", "1.0.0-0", comparisonEqualTo},

	    // > identifiers with letters or hyphens are compared lexically
	    // > in ASCII sort order.
	    {"1.0.0-a", "1.0.0-b", comparisonLessThan},
	    {"1.0.0-a-2", "1.0.0-a-10", comparisonGreaterThan},
	    {"1.0.0-b", "1.0.0-a", comparisonGreaterThan},
	    {"1.0.0-a", "1.0.0-a", comparisonEqualTo},
	    {"1.0.0-A", "1.0.0-a", comparisonLessThan},

	    // > Numeric identifiers always have lower precedence than
	    // > non-numeric identifiers.
	    {"1.0.0-0", "1.0.0-alpha", comparisonLessThan},
	    {"1.0.0-alpha", "1.0.0-0", comparisonGreaterThan},
	    {"1.0.0-0", "1.0.0-0", comparisonEqualTo},
	    {"1.0.0-alpha", "1.0.0-alpha", comparisonEqualTo},

	    // > A larger set of pre-release fields has a higher precedence
	    // > than a smaller set, if all of the preceding identifiers are
	    // > equal.
	    {"1.0.0-alpha", "1.0.0-alpha.0", comparisonLessThan},
	    {"1.0.0-alpha.0", "1.0.0-alpha", comparisonGreaterThan},

	    // > Precedence for two pre-release versions with the same
	    // > major, minor, and patch version MUST be determined by
	    // > comparing each dot separated identifier from left to right
	    // > until a difference is found [...]
	    {"1.0.0-a.0.b.1", "1.0.0-a.0.b.2", comparisonLessThan},
	    {"1.0.0-a.0.b.1", "1.0.0-b.0.a.1", comparisonLessThan},
	    {"1.0.0-a.0.b.2", "1.0.0-a.0.b.1", comparisonGreaterThan},
	    {"1.0.0-b.0.a.1", "1.0.0-a.0.b.1", comparisonGreaterThan},

	    // > Build metadata does not figure into precedence
	    {"1.0.0+build", "1.0.0", comparisonEqualTo},
	    {"1.0.0+build.stuff", "1.0.0", comparisonEqualTo},
	    {"1.0.0", "1.0.0+build", comparisonEqualTo},
	    {"1.0.0+build", "1.0.0+stuff", comparisonEqualTo},

	    // Edge cases for numeric and lexical comparison of prerelease
	    // identifiers.
	    {"1.0.0-alpha.99999", "1.0.0-alpha.100000",
	     comparisonLessThan},
	    {"1.0.0-alpha.beta", "1.0.0-alpha.alpha",
	     comparisonGreaterThan},
	};

	for (auto& test : tests) {
		std::string name = std::string(test.v1) + " <=> " + test.v2;
		t->Run(name, [test](T* t) {
			t->Parallel();
			auto [v1, ok1] = tsc::semver::TryParseVersion(test.v1);
			assert::Assert(t, ok1, test.v1);
			auto [v2, ok2] = tsc::semver::TryParseVersion(test.v2);
			assert::Assert(t, ok2, test.v2);
			assert::Equal(t, versionCompare(&v1, &v2), test.want);
		});
	}
}

} // namespace

REGISTER_UNIT_TEST("semver.TestTryParseSemver", TestTryParseSemver);
REGISTER_UNIT_TEST("semver.TestVersionString", TestVersionString);
REGISTER_UNIT_TEST("semver.TestVersionCompare", TestVersionCompare);
