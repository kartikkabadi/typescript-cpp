// Port of tsc/internal/ls/lsutil/utilities_test.go.
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/parser/parser.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;
using namespace tsc::ls::lsutil;

static SourceFile* parseTS(T* t, const std::string& text) {
	t->Helper();
	SourceFileParseOptions opts;
	opts.FileName = "/test.ts";
	opts.Path = "/test.ts";
	return tsc::parseSourceFile(opts, text, ScriptKind::TS);
}

static void TestProbablyUsesSemicolons(T* t) {
	t->Parallel();

	struct {
		const char* name;
		const char* src;
		bool want;
	} tests[] = {
	    {
	        .name = "mixed semicolons and ASI favors semicolons when ratio exceeds one fifth",
	        // First five observations: 2 with semicolon, 3 without. Real ratio 2/3 > 1/5.
	        // Integer division bug compared against 1/5==0 and used with/without as ints,
	        // so the old check was effectively (with/without) > 0, which failed here.
	        .src = "let a = 1;\nlet b = 2;\nlet c = 3\nlet d = 4\nlet e = 5\n",
	        .want = true,
	    },
	    {
	        .name = "consistent ASI with no semicolons",
	        .src = "let a = 1\nlet b = 2\nlet c = 3\n",
	        .want = false,
	    },
	    {
	        .name = "consistent semicolons",
	        .src = "let a = 1;\nlet b = 2;\nlet c = 3;\n",
	        .want = true,
	    },
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [tt](T* t) {
			t->Parallel();
			SourceFile* file = parseTS(t, tt.src);
			if (bool got = ProbablyUsesSemicolons(file); got != tt.want) {
				t->Errorf("ProbablyUsesSemicolons() = %v, want %v",
				          {got, tt.want});
			}
		});
	}
}
REGISTER_UNIT_TEST("ls/lsutil.TestProbablyUsesSemicolons",
                   TestProbablyUsesSemicolons);

static void TestResolveOrganizeImportsSort(T* t) {
	t->Parallel();

	struct {
		const char* name;
		UserPreferences preferences;
		OrganizeImportsSort want;
	} tests[] = {
	    {
	        .name = "explicit sort wins",
	        .preferences = [] {
		        UserPreferences p;
		        p.OrganizeImportsSort = OrganizeImportsSortOrdinal;
		        p.OrganizeImportsCollation = OrganizeImportsCollationUnicode;
		        p.OrganizeImportsIgnoreCase = Tristate::True;
		        return p;
	        }(),
	        .want = OrganizeImportsSortOrdinal,
	    },
	    {
	        .name = "unicode case-sensitive maps to natural",
	        .preferences = [] {
		        UserPreferences p;
		        p.OrganizeImportsCollation = OrganizeImportsCollationUnicode;
		        p.OrganizeImportsIgnoreCase = Tristate::False;
		        return p;
	        }(),
	        .want = OrganizeImportsSortNatural,
	    },
	    {
	        .name = "unicode ignore case maps to natural ignore case",
	        .preferences = [] {
		        UserPreferences p;
		        p.OrganizeImportsCollation = OrganizeImportsCollationUnicode;
		        p.OrganizeImportsIgnoreCase = Tristate::True;
		        return p;
	        }(),
	        .want = OrganizeImportsSortNaturalIgnoreCase,
	    },
	    {
	        .name = "unicode unknown case sensitivity stays auto for detection",
	        .preferences = [] {
		        UserPreferences p;
		        p.OrganizeImportsCollation = OrganizeImportsCollationUnicode;
		        return p;
	        }(),
	        .want = OrganizeImportsSortAuto,
	    },
	    {
	        .name = "ordinal ignore case maps to ordinal ignore case",
	        .preferences = [] {
		        UserPreferences p;
		        p.OrganizeImportsIgnoreCase = Tristate::True;
		        return p;
	        }(),
	        .want = OrganizeImportsSortOrdinalIgnoreCase,
	    },
	    {
	        .name = "ordinal case sensitive maps to ordinal",
	        .preferences = [] {
		        UserPreferences p;
		        p.OrganizeImportsIgnoreCase = Tristate::False;
		        return p;
	        }(),
	        .want = OrganizeImportsSortOrdinal,
	    },
	    {
	        .name = "unknown ordinal stays auto",
	        .preferences = UserPreferences{},
	        .want = OrganizeImportsSortAuto,
	    },
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [tt](T* t) {
			t->Parallel();
			if (auto got = ResolveOrganizeImportsSort(tt.preferences);
			    got != tt.want) {
				t->Fatalf("ResolveOrganizeImportsSort() = %v, want %v",
				          {got, tt.want});
			}
		});
	}
}
REGISTER_UNIT_TEST("ls/lsutil.TestResolveOrganizeImportsSort",
                   TestResolveOrganizeImportsSort);

static int cmpSign(int value) {
	if (value < 0) return -1;
	if (value > 0) return 1;
	return 0;
}

static void TestCompareOrganizeImportsNaturalStrings(T* t) {
	t->Parallel();

	auto comparer = TestGetOrganizeImportsPresetStringComparer(
	    OrganizeImportsSortNaturalIgnoreCase);
	struct {
		const char* name;
		const char* a;
		const char* b;
		int want;
	} tests[] = {
	    {
	        .name = "numeric runs sort by numeric value",
	        .a = "a2",
	        .b = "a100",
	        .want = -1,
	    },
	    {
	        .name = "numeric runs with equal value use raw tie break",
	        .a = "a02",
	        .b = "a2",
	        .want = -1,
	    },
	    {
	        .name = "accents are folded for primary comparison",
	        .a = "À",
	        .b = "B",
	        .want = -1,
	    },
	    {
	        .name = "raw comparison breaks accent ties",
	        .a = "A",
	        .b = "À",
	        .want = -1,
	    },
	    {
	        .name = "hyphen sorts before slash like Intl.Collator fallback",
	        .a = "app-init",
	        .b = "app/app",
	        .want = -1,
	    },
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [tt, &comparer](T* t) {
			t->Parallel();
			if (int got = cmpSign(comparer(tt.a, tt.b)); got != tt.want) {
				t->Fatalf("comparer(%q, %q) = %v, want sign %v",
				          {std::string(tt.a), std::string(tt.b), got,
				           tt.want});
			}
		});
	}
}
REGISTER_UNIT_TEST("ls/lsutil.TestCompareOrganizeImportsNaturalStrings",
                   TestCompareOrganizeImportsNaturalStrings);
