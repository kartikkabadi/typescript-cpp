// Port of tsc/internal/ls/lsutil/userpreferences_test.go.
// DeepEqual on UserPreferences is realized as marshal-equality: the fieldInfo
// table is the complete serialization surface, so two structs marshal
// identically iff all serialized fields match.
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/modulespecifiers/types.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;
using namespace tsc::ls::lsutil;

// NilError over the json package's string error return.
static void noErr(T* t, const std::string& err) {
	if (!err.empty()) t->Fatalf("assert.NilError failed: %s", {err});
}

static void prefsDeepEqual(T* t, const UserPreferences& expected,
                           const UserPreferences& parsed) {
	auto [eJSON, eErr] = tsc::json::marshal(expected);
	noErr(t, eErr);
	auto [pJSON, pErr] = tsc::json::marshal(parsed);
	noErr(t, pErr);
	gotest::assert::Equal(t, pJSON, eJSON);
}

// fillNonZeroValues is realized by AllFieldsNonZeroUserPreferences() in
// userpreferences.cpp (drives every fieldInfo::apply until the field
// serializes non-default; Go does it via reflection).

static void TestUserPreferencesRoundtrip(T* t) {
	t->Parallel();

	UserPreferences original = AllFieldsNonZeroUserPreferences();

	auto marshaled = tsc::json::marshal(&original);
	noErr(t, marshaled.second);
	std::string jsonBytes = marshaled.first;

	t->Run("UnmarshalJSONFrom", [jsonBytes, &original](T* t) {
		t->Parallel();
		UserPreferences parsed;
		auto err2 = tsc::json::unmarshal(jsonBytes, &parsed);
		noErr(t, err2);
		prefsDeepEqual(t, original, parsed);
	});

	t->Run("withConfig", [jsonBytes, &original](T* t) {
		t->Parallel();
		JsonObject config;
		auto err2 = tsc::json::unmarshal(jsonBytes, &config);
		noErr(t, err2);
		auto parsed = TestWithConfig(UserPreferences{}, config);
		prefsDeepEqual(t, original, parsed);
	});
}
REGISTER_UNIT_TEST("ls/lsutil.TestUserPreferencesRoundtrip",
                   TestUserPreferencesRoundtrip);

// TestUserPreferencesParsingEdgeCases — userpreferences_test.go:13.
static void TestUserPreferencesParsingEdgeCases(T* t) {
	t->Parallel();

	auto base = [] {
		UserPreferences p = NewDefaultUserPreferences();
		p.QuotePreference = QuotePreferenceDouble;
		p.MaximumHoverLength = 17;
		p.OrganizeImportsIgnoreCase = Tristate::True;
		return p;
	};
	auto runCase = [&](T* t, std::string name, JsonObject config,
	                   UserPreferences expected) {
		t->Run(name, [&, config, expected](T* t) {
			t->Parallel();
			prefsDeepEqual(t, expected,
			               TestWithConfig(base(), config));
		});
	};

	runCase(t, "null raw values leave preferences unchanged",
	        JsonObject{{"quotePreference", JsonAny{}},
	                   {"maximumHoverLength", JsonAny{}},
	                   {"includeCompletionsForModuleExports", JsonAny{}}},
	        base());

	{
		UserPreferences expected = base();
		expected.IncludeCompletionsForModuleExports = Tristate::Unknown;
		runCase(t, "invalid boolean becomes unknown",
		        JsonObject{{"includeCompletionsForModuleExports",
		                    JsonAny("invalid")}},
		        expected);
	}

	{
		UserPreferences expected = base();
		expected.QuotePreference = QuotePreferenceSingle;
		expected.JsxAttributeCompletionStyle =
		    JsxAttributeCompletionStyleBraces;
		expected.OrganizeImportsCaseFirst =
		    OrganizeImportsCaseFirstLower;
		expected.InlayHintsPreferences.IncludeInlayParameterNameHints =
		    IncludeInlayParameterNameHintsAll;
		expected.OrganizeImportsTypeOrder = OrganizeImportsTypeOrderFirst;
		expected.WorkspaceSymbolsScope =
		    WorkspaceSymbolsScopeCurrentProject;
		runCase(
		    t, "case-insensitive enums",
		    JsonObject{
		        {"quotePreference", JsonAny("SINGLE")},
		        {"jsxAttributeCompletionStyle", JsonAny("BRACES")},
		        {"organizeImportsCaseFirst", JsonAny("LOWER")},
		        {"includeInlayParameterNameHints", JsonAny("ALL")},
		        {"organizeImportsTypeOrder", JsonAny("FIRST")},
		        {"workspaceSymbolsScope",
		         JsonAny("CURRENTPROJECT")}},
		    expected);
	}

	runCase(
	    t, "present null primary path prevents fallback",
	    JsonObject{
	        {"suggest",
	         JsonAny(JsonObject{{"jsdoc",
	                             JsonAny(JsonObject{
	                                 {"enabled", JsonAny{}}})},
	                            {"completeJSDocs", JsonAny(false)}})}},
	    base());

	{
		UserPreferences expected = base();
		expected.OrganizeImportsIgnoreCase = Tristate::Unknown;
		runCase(
		    t, "null case sensitivity becomes unknown",
		    JsonObject{
		        {"preferences",
		         JsonAny(JsonObject{
		             {"organizeImports",
		              JsonAny(JsonObject{{"caseSensitivity",
		                                  JsonAny{}}})}})}},
		    expected);
	}

	{
		UserPreferences expected = base();
		expected.MaximumHoverLength = 9;
		expected.FormatCodeSettings.IndentStyle =
		    IndentStyleBlock;
		expected.AutoImportFileExcludePatterns = {"first", "second"};
		runCase(t, "numeric conversion and array filtering",
		        JsonObject{
		            {"maximumHoverLength", JsonAny(9.8)},
		            {"indentStyle", JsonAny(1.7)},
		            {"autoImportFileExcludePatterns",
		             JsonAny(std::vector<JsonAny>{
		                 JsonAny("first"), JsonAny(false), JsonAny(3),
		                 JsonAny("second")})}},
		        expected);
	}

	{
		UserPreferences expected = base();
		expected.AutoImportFileExcludePatterns = {};
		runCase(t, "empty array is not nil",
		        JsonObject{{"autoImportFileExcludePatterns",
		                    JsonAny(std::vector<JsonAny>{})}},
		        expected);
	}

	{
		UserPreferences expected = base();
		expected.ImportModuleSpecifierPreference =
		    tsc::modulespecifiers::
		        ImportModuleSpecifierPreferenceShortest;
		runCase(t,
		        "invalid module specifier preference uses its "
		        "default",
		        JsonObject{{"importModuleSpecifierPreference",
		                    JsonAny(true)}},
		        expected);
	}
}
REGISTER_UNIT_TEST("ls/lsutil.TestUserPreferencesParsingEdgeCases",
                   TestUserPreferencesParsingEdgeCases);

static void TestUserPreferencesSerialize(T* t) {
	t->Parallel();

	t->Run("config path field serializes to nested path", [](T* t) {
		t->Parallel();
		UserPreferences prefs;
		prefs.QuotePreference = QuotePreferenceSingle;
		auto [jsonBytes, err] = tsc::json::marshal(&prefs);
		noErr(t, err);

		JsonAny actual;
		noErr(t, tsc::json::unmarshal(jsonBytes, &actual));

		auto& preferences = actual.obj["preferences"].obj;
		gotest::assert::Equal(t, preferences["quoteStyle"].s,
		                      std::string("single"));
	});

	t->Run("raw-only field serializes to unstable section", [](T* t) {
		t->Parallel();
		UserPreferences prefs;
		prefs.DisableSuggestions = Tristate::True;
		auto [jsonBytes, err] = tsc::json::marshal(&prefs);
		noErr(t, err);

		JsonAny actual;
		noErr(t, tsc::json::unmarshal(jsonBytes, &actual));

		auto& unstable = actual.obj["unstable"].obj;
		gotest::assert::Equal(t, unstable["disableSuggestions"].b, true);
	});

	t->Run("inlay hint inversion on serialize", [](T* t) {
		t->Parallel();
		UserPreferences prefs;
		prefs.InlayHintsPreferences.IncludeInlayParameterNameHints =
		    IncludeInlayParameterNameHintsAll;
		prefs.InlayHintsPreferences
		    .IncludeInlayParameterNameHintsWhenArgumentMatchesName =
		    Tristate::True;
		auto [jsonBytes, err] = tsc::json::marshal(&prefs);
		noErr(t, err);

		JsonAny actual;
		noErr(t, tsc::json::unmarshal(jsonBytes, &actual));

		auto& inlayHints = actual.obj["inlayHints"].obj;
		auto& parameterNames = inlayHints["parameterNames"].obj;
		gotest::assert::Equal(t, parameterNames["enabled"].s,
		                      std::string("all"));
		// inverted
		gotest::assert::Equal(
		    t, parameterNames["suppressWhenArgumentMatchesName"].b,
		    false);
	});

	t->Run("mixed config and unstable fields", [](T* t) {
		t->Parallel();
		UserPreferences prefs;
		prefs.QuotePreference = QuotePreferenceSingle;
		prefs.DisableSuggestions = Tristate::True;
		prefs.DisplayPartsForJSDoc = Tristate::True;
		auto [jsonBytes, err] = tsc::json::marshal(&prefs);
		noErr(t, err);

		JsonAny actual;
		noErr(t, tsc::json::unmarshal(jsonBytes, &actual));

		auto& preferences = actual.obj["preferences"].obj;
		gotest::assert::Equal(t, preferences["quoteStyle"].s,
		                      std::string("single"));

		auto& unstable = actual.obj["unstable"].obj;
		gotest::assert::Equal(t, unstable["disableSuggestions"].b, true);
		gotest::assert::Equal(t, unstable["displayPartsForJSDoc"].b, true);
	});
}
REGISTER_UNIT_TEST("ls/lsutil.TestUserPreferencesSerialize",
                   TestUserPreferencesSerialize);

static void TestUserPreferencesParseUnstable(T* t) {
	t->Parallel();

	struct {
		const char* name;
		const char* json;
		UserPreferences expected;
	} tests[] = {
	    {
	        .name = "unstable fields with correct casing",
	        .json = R"JSON({
				"unstable": {
					"disableSuggestions": true,
					"maximumHoverLength": 100,
					"allowRenameOfImportPath": true
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.DisableSuggestions = Tristate::True;
		        p.MaximumHoverLength = 100;
		        p.AllowRenameOfImportPath = Tristate::True;
		        return p;
	        }(),
	    },
	    {
	        .name = "nested preferences path",
	        .json = R"JSON({
				"preferences": {
					"quoteStyle": "single",
					"useAliasesForRenames": true
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.QuotePreference = QuotePreferenceSingle;
		        p.ProvidePrefixAndSuffixTextForRename = Tristate::True;
		        return p;
	        }(),
	    },
	    {
	        .name = "suggest section",
	        .json = R"JSON({
				"suggest": {
					"autoImports": false,
					"includeCompletionsForImportStatements": true
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.IncludeCompletionsForModuleExports = Tristate::False;
		        p.IncludeCompletionsForImportStatements = Tristate::True;
		        return p;
	        }(),
	    },
	    {
	        .name = "inlayHints with invert",
	        .json = R"JSON({
				"inlayHints": {
					"parameterNames": {
						"enabled": "all",
						"suppressWhenArgumentMatchesName": true
					}
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.InlayHintsPreferences.IncludeInlayParameterNameHints =
		            IncludeInlayParameterNameHintsAll;
		        p.InlayHintsPreferences
		            .IncludeInlayParameterNameHintsWhenArgumentMatchesName =
		            Tristate::False; // inverted
		        return p;
	        }(),
	    },
	    {
	        .name = "mixed config",
	        .json = R"JSON({
				"unstable": {
					"displayPartsForJSDoc": true
				},
				"preferences": {
					"importModuleSpecifier": "relative"
				},
				"workspaceSymbols": {
					"excludeLibrarySymbols": true,
					"scope": "currentProject"
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.DisplayPartsForJSDoc = Tristate::True;
		        p.ImportModuleSpecifierPreference = "relative";
		        p.ExcludeLibrarySymbolsInNavTo = Tristate::True;
		        p.WorkspaceSymbolsScope = WorkspaceSymbolsScopeCurrentProject;
		        return p;
	        }(),
	    },
	    {
	        .name = "stable config overrides unstable",
	        .json = R"JSON({
				"unstable": {
					"quotePreference": "double"
				},
				"preferences": {
					"quoteStyle": "single"
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.QuotePreference = QuotePreferenceSingle; // stable wins
		        return p;
	        }(),
	    },
	    {
	        .name = "unstable sets value when no stable config",
	        .json = R"JSON({
				"unstable": {
					"includeAutomaticOptionalChainCompletions": false
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.IncludeAutomaticOptionalChainCompletions = Tristate::False;
		        return p;
	        }(),
	    },
	    {
	        .name = "any field can be passed via unstable by its raw name",
	        .json = R"JSON({
				"unstable": {
					"quotePreference": "double",
					"includeCompletionsForModuleExports": true,
					"excludeLibrarySymbolsInNavTo": true
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.QuotePreference = QuotePreferenceDouble;
		        p.IncludeCompletionsForModuleExports = Tristate::True;
		        p.ExcludeLibrarySymbolsInNavTo = Tristate::True;
		        return p;
	        }(),
	    },
	    {
	        .name = "TypeScript raw names work in unstable section",
	        .json = R"JSON({
				"unstable": {
					"includeCompletionsForModuleExports": true,
					"quotePreference": "single",
					"providePrefixAndSuffixTextForRename": true,
					"includeInlayParameterNameHints": "all",
					"organizeImportsLocale": "en"
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.IncludeCompletionsForModuleExports = Tristate::True;
		        p.QuotePreference = QuotePreferenceSingle;
		        p.ProvidePrefixAndSuffixTextForRename = Tristate::True;
		        p.OrganizeImportsLocale = "en";
		        p.InlayHintsPreferences.IncludeInlayParameterNameHints =
		            IncludeInlayParameterNameHintsAll;
		        return p;
	        }(),
	    },
	    {
	        .name = "old raw organize imports unicode preferences load as raw state",
	        .json = R"JSON({
				"unstable": {
					"organizeImportsCollation": "unicode",
					"organizeImportsCaseFirst": "upper",
					"organizeImportsIgnoreCase": false,
					"organizeImportsNumericCollation": true
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.OrganizeImportsCollation = OrganizeImportsCollationUnicode;
		        p.OrganizeImportsCaseFirst = OrganizeImportsCaseFirstUpper;
		        p.OrganizeImportsIgnoreCase = Tristate::False;
		        p.OrganizeImportsNumericCollation = Tristate::True;
		        return p;
	        }(),
	    },
	    {
	        .name = "old top-level raw organize imports unicode preferences load as raw state",
	        .json = R"JSON({
				"organizeImportsCollation": "unicode",
				"organizeImportsIgnoreCase": true
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.OrganizeImportsCollation = OrganizeImportsCollationUnicode;
		        p.OrganizeImportsIgnoreCase = Tristate::True;
		        return p;
	        }(),
	    },
	    {
	        .name = "new top-level raw organize imports sort is accepted",
	        .json = R"JSON({
				"organizeImportsSort": "natural"
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.OrganizeImportsSort = OrganizeImportsSortNatural;
		        return p;
	        }(),
	    },
	    {
	        .name = "old raw organize imports ignore case loads as raw state",
	        .json = R"JSON({
				"unstable": {
					"organizeImportsIgnoreCase": true
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.OrganizeImportsIgnoreCase = Tristate::True;
		        return p;
	        }(),
	    },
	    {
	        .name = "new raw organize imports sort loads alongside old raw preferences",
	        .json = R"JSON({
				"unstable": {
					"organizeImportsSort": "ordinal",
					"organizeImportsCollation": "unicode",
					"organizeImportsIgnoreCase": true
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.OrganizeImportsSort = OrganizeImportsSortOrdinal;
		        p.OrganizeImportsCollation = OrganizeImportsCollationUnicode;
		        p.OrganizeImportsIgnoreCase = Tristate::True;
		        return p;
	        }(),
	    },
	    {
	        .name = "old nested organize imports unicode preferences load as raw state",
	        .json = R"JSON({
				"preferences": {
					"organizeImports": {
						"unicodeCollation": "unicode",
						"caseSensitivity": "caseSensitive",
						"numericCollation": true,
						"caseFirst": "upper"
					}
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.OrganizeImportsCollation = OrganizeImportsCollationUnicode;
		        p.OrganizeImportsIgnoreCase = Tristate::False;
		        p.OrganizeImportsNumericCollation = Tristate::True;
		        p.OrganizeImportsCaseFirst = OrganizeImportsCaseFirstUpper;
		        return p;
	        }(),
	    },
	    {
	        .name = "new nested organize imports sort loads alongside old nested preferences",
	        .json = R"JSON({
				"preferences": {
					"organizeImports": {
						"sort": "ordinalIgnoreCase",
						"unicodeCollation": "unicode",
						"caseSensitivity": "caseSensitive"
					}
				}
			})JSON",
	        .expected = [] {
		        UserPreferences p;
		        p.OrganizeImportsSort = OrganizeImportsSortOrdinalIgnoreCase;
		        p.OrganizeImportsCollation = OrganizeImportsCollationUnicode;
		        p.OrganizeImportsIgnoreCase = Tristate::False;
		        return p;
	        }(),
	    },
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [tt](T* t) {
			t->Parallel();
			JsonObject config;
			auto err = tsc::json::unmarshal(tt.json, &config);
			noErr(t, err);

			auto parsed = TestWithConfig(UserPreferences{}, config);

			prefsDeepEqual(t, tt.expected, parsed);
		});
	}
}
REGISTER_UNIT_TEST("ls/lsutil.TestUserPreferencesParseUnstable",
                   TestUserPreferencesParseUnstable);

static void TestUserPreferencesLocale(T* t) {
	t->Parallel();

	auto prefs = ParseUserPreferences(JsonObject{
	    {"typescript", JsonObject{{"locale", JsonAny("de")}}},
	    {"js/ts", JsonObject{{"locale", JsonAny("fr")}}},
	});

	gotest::assert::Equal(t, prefs.Locale, std::string("fr"));
}
REGISTER_UNIT_TEST("ls/lsutil.TestUserPreferencesLocale",
                   TestUserPreferencesLocale);

static void TestUserPreferencesReportStyleChecksAsWarnings(T* t) {
	t->Parallel();

	t->Run("reportStyleChecksAsWarnings via config path", [](T* t) {
		t->Parallel();
		auto prefs = ParseUserPreferences(JsonObject{
		    {"js/ts",
		     JsonObject{{"reportStyleChecksAsWarnings", JsonAny(false)}}},
		});
		gotest::assert::Equal(t, prefs.ReportStyleChecksAsWarnings,
		                      Tristate::False);
	});

	t->Run("reportStyleChecksAsWarnings defaults to true", [](T* t) {
		t->Parallel();
		auto prefs = NewDefaultUserPreferences();
		gotest::assert::Equal(t, prefs.ReportStyleChecksAsWarnings,
		                      Tristate::True);
	});

	t->Run("reportStyleChecksAsWarnings via unstable section", [](T* t) {
		t->Parallel();
		auto prefs = ParseUserPreferences(JsonObject{
		    {"js/ts",
		     JsonObject{{"unstable",
		                 JsonObject{{"reportStyleChecksAsWarnings",
		                             JsonAny(false)}}}}},
		});
		gotest::assert::Equal(t, prefs.ReportStyleChecksAsWarnings,
		                      Tristate::False);
	});
}
REGISTER_UNIT_TEST("ls/lsutil.TestUserPreferencesReportStyleChecksAsWarnings",
                   TestUserPreferencesReportStyleChecksAsWarnings);

static void TestUserPreferencesParseServerFeaturePreferences(T* t) {
	t->Parallel();

	t->Run("preferred server feature settings", [](T* t) {
		t->Parallel();
		auto prefs = ParseUserPreferences(JsonObject{
		    {"js/ts",
		     JsonObject{
		         {"validate", JsonObject{{"enabled", JsonAny(false)}}},
		         {"format", JsonObject{{"enabled", JsonAny(false)}}},
		         {"autoClosingTags",
		          JsonObject{{"enabled", JsonAny(false)}}},
		     }},
		});
		gotest::assert::Equal(t, prefs.ValidateEnabled, Tristate::False);
		gotest::assert::Equal(t, prefs.FormatEnabled, Tristate::False);
		gotest::assert::Equal(t, prefs.AutoClosingTags,
		                      Tristate::False);
	});

	t->Run("legacy server feature fallbacks", [](T* t) {
		t->Parallel();
		auto prefs = ParseUserPreferences(JsonObject{
		    {"typescript",
		     JsonObject{
		         {"validate", JsonObject{{"enable", JsonAny(false)}}},
		         {"format", JsonObject{{"enable", JsonAny(false)}}},
		         {"autoClosingTags", JsonAny(false)},
		     }},
		});
		gotest::assert::Equal(t, prefs.ValidateEnabled, Tristate::False);
		gotest::assert::Equal(t, prefs.FormatEnabled, Tristate::False);
		gotest::assert::Equal(t, prefs.AutoClosingTags,
		                      Tristate::False);
	});

	t->Run("preferred settings take precedence over fallbacks", [](T* t) {
		t->Parallel();
		auto prefs = ParseUserPreferences(JsonObject{
		    {"typescript",
		     JsonObject{
		         {"validate", JsonObject{{"enable", JsonAny(false)}}},
		         {"format", JsonObject{{"enable", JsonAny(false)}}},
		         {"autoClosingTags", JsonAny(false)},
		     }},
		    {"js/ts",
		     JsonObject{
		         {"validate", JsonObject{{"enabled", JsonAny(true)}}},
		         {"format", JsonObject{{"enabled", JsonAny(true)}}},
		         {"autoClosingTags",
		          JsonObject{{"enabled", JsonAny(true)}}},
		     }},
		});
		gotest::assert::Equal(t, prefs.ValidateEnabled, Tristate::True);
		gotest::assert::Equal(t, prefs.FormatEnabled, Tristate::True);
		gotest::assert::Equal(t, prefs.AutoClosingTags,
		                      Tristate::True);
	});
}
REGISTER_UNIT_TEST(
    "ls/lsutil.TestUserPreferencesParseServerFeaturePreferences",
    TestUserPreferencesParseServerFeaturePreferences);

static void TestParseUserPreferencesEditorFormatting(T* t) {
	t->Parallel();

	auto prefs = ParseUserPreferences(JsonObject{
	    {"editor",
	     JsonObject{
	         {"tabSize", JsonAny(2)},
	         {"insertSpaces", JsonAny(false)},
	     }},
	});

	gotest::assert::Equal(t, prefs.FormatCodeSettings.TabSize, 2);
	gotest::assert::Equal(t, prefs.FormatCodeSettings.IndentSize, 2);
	gotest::assert::Equal(t, prefs.FormatCodeSettings.ConvertTabsToSpaces,
	                      Tristate::False);
}
REGISTER_UNIT_TEST("ls/lsutil.TestParseUserPreferencesEditorFormatting",
                   TestParseUserPreferencesEditorFormatting);

static void TestUserPreferencesParseJSDocCompletionPreferences(T* t) {
	t->Parallel();

	t->Run("unified jsdoc enabled setting", [](T* t) {
		t->Parallel();
		auto prefs = ParseUserPreferences(JsonObject{
		    {"js/ts",
		     JsonObject{
		         {"suggest",
		          JsonObject{{"jsdoc",
		                      JsonObject{{"enabled", JsonAny(false)}}}}},
		     }},
		});
		gotest::assert::Equal(t, prefs.CompleteJSDocs,
		                      Tristate::False);
	});

	t->Run("language fallback completeJSDocs setting", [](T* t) {
		t->Parallel();
		auto prefs = ParseUserPreferences(JsonObject{
		    {"typescript",
		     JsonObject{
		         {"suggest",
		          JsonObject{{"completeJSDocs", JsonAny(false)}}},
		     }},
		});
		gotest::assert::Equal(t, prefs.CompleteJSDocs,
		                      Tristate::False);
	});

	t->Run("unified jsdoc enabled takes precedence over language fallback",
	       [](T* t) {
		       t->Parallel();
		       auto prefs = ParseUserPreferences(JsonObject{
		           {"typescript",
		            JsonObject{
		                {"suggest",
		                 JsonObject{{"completeJSDocs", JsonAny(false)}}},
		            }},
		           {"js/ts",
		            JsonObject{
		                {"suggest",
		                 JsonObject{{"jsdoc",
		                             JsonObject{{"enabled",
		                                         JsonAny(true)}}}}},
		            }},
		       });
		       gotest::assert::Equal(t, prefs.CompleteJSDocs,
		                             Tristate::True);
	       });

	t->Run("unified jsdoc generateReturns setting", [](T* t) {
		t->Parallel();
		auto prefs = ParseUserPreferences(JsonObject{
		    {"js/ts",
		     JsonObject{
		         {"suggest",
		          JsonObject{{"jsdoc",
		                      JsonObject{{"generateReturns",
		                                  JsonAny(false)}}}}},
		     }},
		});
		gotest::assert::Equal(t, prefs.GenerateReturnInDocTemplate,
		                      Tristate::False);
	});

	t->Run("language jsdoc generateReturns setting", [](T* t) {
		t->Parallel();
		auto prefs = ParseUserPreferences(JsonObject{
		    {"typescript",
		     JsonObject{
		         {"suggest",
		          JsonObject{{"jsdoc",
		                      JsonObject{{"generateReturns",
		                                  JsonAny(false)}}}}},
		     }},
		});
		gotest::assert::Equal(t, prefs.GenerateReturnInDocTemplate,
		                      Tristate::False);
	});
}
REGISTER_UNIT_TEST(
    "ls/lsutil.TestUserPreferencesParseJSDocCompletionPreferences",
    TestUserPreferencesParseJSDocCompletionPreferences);

static void TestUserPreferencesParseATA(T* t) {
	t->Parallel();

	t->Run(
	    "ParseUserPreferences with unified ATA setting in js/ts section",
	    [](T* t) {
		    t->Parallel();
		    auto prefs = ParseUserPreferences(JsonObject{
		        {"js/ts",
		         JsonObject{
		             {"tsserver",
		              JsonObject{{"automaticTypeAcquisition",
		                          JsonObject{{"enabled",
		                                      JsonAny(false)}}}}},
		         }},
		    });
		    gotest::assert::Assert(t, prefs.IsATADisabled());
		    gotest::assert::Equal(t, prefs.AutomaticTypeAcquisitionEnabled,
		                          Tristate::False);
	    });

	t->Run(
	    "ParseUserPreferences with deprecated disableAutomaticTypeAcquisition in typescript section",
	    [](T* t) {
		    t->Parallel();
		    auto prefs = ParseUserPreferences(JsonObject{
		        {"typescript",
		         JsonObject{{"disableAutomaticTypeAcquisition",
		                     JsonAny(true)}}},
		    });
		    gotest::assert::Assert(t, prefs.IsATADisabled());
		    gotest::assert::Equal(t, prefs.DisableAutomaticTypeAcquisition,
		                          Tristate::True);
	    });

	t->Run(
	    "unified setting takes precedence over deprecated setting",
	    [](T* t) {
		    t->Parallel();
		    // Both settings set: unified (js/ts) should take precedence
		    auto prefs = ParseUserPreferences(JsonObject{
		        {"typescript",
		         JsonObject{{"disableAutomaticTypeAcquisition",
		                     JsonAny(true)}}},
		        {"js/ts",
		         JsonObject{
		             {"tsserver",
		              JsonObject{{"automaticTypeAcquisition",
		                          JsonObject{{"enabled",
		                                      JsonAny(true)}}}}},
		         }},
		    });
		    gotest::assert::Assert(t, !prefs.IsATADisabled());
		    gotest::assert::Equal(t, prefs.AutomaticTypeAcquisitionEnabled,
		                          Tristate::True);
	    });

	t->Run(
	    "IsATADisabled returns false when neither setting is configured",
	    [](T* t) {
		    t->Parallel();
		    auto prefs = NewDefaultUserPreferences();
		    gotest::assert::Assert(t, !prefs.IsATADisabled());
	    });
}
REGISTER_UNIT_TEST("ls/lsutil.TestUserPreferencesParseATA",
                   TestUserPreferencesParseATA);
