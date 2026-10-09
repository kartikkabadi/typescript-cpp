// tests_diagnostics.cpp — port of tsc/internal/diagnostics/diagnostics_test.go
// (package diagnostics — uses package-internal helpers).
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/diagnostics/diagnostics.h"
#include "internal/gostd/regexp.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/locale/locale.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

void TestLocalize(T* t) {
	t->Parallel();

	struct TestCase {
		std::string name;
		const DiagnosticMessage* message;
		locale::Locale locale;
		std::vector<std::string> args;
		std::string expected;
	};
	std::vector<TestCase> tests{
	    {"english default", Identifier_expected,
	     locale::parse("en").first, {}, "Identifier expected."},
	    {"undefined locale uses english", Identifier_expected,
	     locale::Default, {}, "Identifier expected."},
	    {"with single argument", X_0_expected, locale::parse("en").first,
	     {")"}, "')' expected."},
	    {"with multiple arguments",
	     The_parser_expected_to_find_a_1_to_match_the_0_token_here,
	     locale::parse("en").first, {"{", "}"},
	     "The parser expected to find a '}' to match the '{' token here."},
	    {"fallback to english for unknown locale", Identifier_expected,
	     locale::parse("af-ZA").first, {}, "Identifier expected."},
	    {"german", Identifier_expected, locale::parse("de-DE").first, {},
	     "Es wurde ein Bezeichner erwartet."},
	    {"french", Identifier_expected, locale::parse("fr-FR").first, {},
	     "Identificateur attendu."},
	    {"spanish", Identifier_expected, locale::parse("es-ES").first, {},
	     "Se esperaba un identificador."},
	    {"japanese", Identifier_expected, locale::parse("ja-JP").first, {},
	     "\xE8\xAD\x98\xE5\x88\xA5\xE5\xAD\x90\xE3\x81\x8C\xE5\xBF\x85\xE8\xA6"
	     "\x81\xE3\x81\xA7\xE3\x81\x99\xE3\x80\x82"},
	    {"chinese simplified", Identifier_expected,
	     locale::parse("zh-CN").first, {},
	     "\xE5\xBA\x94\xE4\xB8\xBA\xE6\xA0\x87\xE8\xAF\x86\xE7\xAC\xA6\xE3"
	     "\x80\x82"},
	    {"korean", Identifier_expected, locale::parse("ko-KR").first, {},
	     "\xEC\x8B\x9D\xEB\xB3\x84\xEC\x9E\x90\xEA\xB0\x80\x20\xED\x95\x84"
	     "\xEC\x9A\x94\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4\x2E"},
	    {"russian", Identifier_expected, locale::parse("ru-RU").first, {},
	     "\xD0\x9E\xD0\xB6\xD0\xB8\xD0\xB4\xD0\xB0\xD0\xBB\xD1\x81\xD1\x8F"
	     "\x20\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBD\xD1\x82\xD0\xB8\xD1\x84\xD0"
	     "\xB8\xD0\xBA\xD0\xB0\xD1\x82\xD0\xBE\xD1\x80\x2E"},
	    {"german with args", X_0_expected, locale::parse("de-DE").first,
	     {")"}, "\")\" wurde erwartet."},
	};

	for (const auto& tt : tests) {
		auto rec = tt;
		t->Run(tt.name, [rec](T* t) {
			t->Parallel();
			auto result = tsc::localize(rec.locale, rec.message,
			                            rec.message->key, rec.args);
			assert::Equal(t, result, rec.expected);
		});
	}
}

void TestLocalize_ByKey(T* t) {
	t->Parallel();

	struct TestCase {
		std::string name;
		Key key;
		locale::Locale locale;
		std::vector<std::string> args;
		std::string expected;
	};
	std::vector<TestCase> tests{
	    {"by key without args", "Identifier_expected_1003",
	     locale::parse("en").first, {}, "Identifier expected."},
	    {"by key with args", "_0_expected_1005",
	     locale::parse("en").first, {")"}, "')' expected."},
	};

	for (const auto& tt : tests) {
		auto rec = tt;
		t->Run(tt.name, [rec](T* t) {
			t->Parallel();
			auto result =
			    tsc::localize(rec.locale, nullptr, rec.key, rec.args);
			assert::Equal(t, result, rec.expected);
		});
	}
}

std::set<std::string> placeholderSet(const std::string& text) {
	static const gostd::regexp::Regexp re(R"(\{(\d+)\})");
	std::set<std::string> result;
	for (auto [start, end] : re.FindAllStringIndex(text, -1)) {
		result.insert(text.substr(start, end - start));
	}
	return result;
}

void validateLocalizedMessages(
    T* t, const std::unordered_map<Key, std::string>& localizedMessages) {
	t->Helper();
	for (const auto& [key, localizedText] : localizedMessages) {
		const auto* message = keyToMessage(key);
		if (message == nullptr) {
			continue;
		}
		auto localizedPlaceholders = placeholderSet(localizedText);
		auto englishPlaceholders = placeholderSet(message->text);
		assert::Equal(t, (int)localizedPlaceholders.size(),
		              (int)englishPlaceholders.size(),
		              "placeholder mismatch for " + key);
		for (const auto& placeholder : englishPlaceholders) {
			assert::Assert(t,
			               localizedPlaceholders.count(placeholder) > 0,
			               "localized diagnostic " + key +
			                   " is missing placeholder " + placeholder);
		}
	}
}

void TestLocaleFiles(T* t) {
	t->Parallel();

	// filepath.Glob("loc/*.generated.json") — resolved against the Go
	// diagnostics package directory, derived from this file's location.
	std::filesystem::path self(__FILE__);
	auto locDir = self.parent_path().parent_path().parent_path()
	                  .parent_path()
	                  .parent_path() /
	              "tsc" / "internal" / "diagnostics" / "loc";

	std::vector<std::string> files;
	for (const auto& entry : std::filesystem::directory_iterator(locDir)) {
		if (entry.path().extension() == ".json" &&
		    entry.path().filename().string().find(".generated.json") !=
		        std::string::npos) {
			files.push_back(entry.path().string());
		}
	}
	assert::Assert(t, files.size() > 0);

	for (const auto& path : files) {
		std::string base = std::filesystem::path(path).filename().string();
		auto localeName =
		    base.substr(0, base.size() - std::string(".generated.json").size());
		t->Run(localeName, [path, localeName](T* t) {
			t->Parallel();

			std::ifstream file(path);
			assert::Assert(t, file.good(), "open " + path);
			std::stringstream ss;
			ss << file.rdbuf();
			std::string text = ss.str();

			auto [dom, err] = json::parse(text);
			assert::NilError(t, err);
			std::unordered_map<Key, std::string> handback;
			for (const auto& [k, v] : dom.obj) {
				handback[k] = v.strVal;
			}
			validateLocalizedMessages(t, handback);

			std::unordered_map<Key, std::string> activeMessages;
			for (const auto& [key, text] : handback) {
				if (keyToMessage(key) != nullptr) {
					activeMessages[key] = text;
				}
			}

			const auto* actual =
			    getLocalizedMessages(locale::parse(localeName).first);
			assert::Assert(t, actual != nullptr);
			assert::Assert(t, *actual == activeMessages);
		});
	}
}

void TestLocaleFilesIgnoreStaleDiagnostics(T* t) {
	t->Parallel();
	validateLocalizedMessages(
	    t, {{"Removed_diagnostic_99999", "Stale translation."}});
}

} // namespace

REGISTER_UNIT_TEST("diagnostics.TestLocalize", TestLocalize);
REGISTER_UNIT_TEST("diagnostics.TestLocalize_ByKey", TestLocalize_ByKey);
REGISTER_UNIT_TEST("diagnostics.TestLocaleFiles", TestLocaleFiles);
REGISTER_UNIT_TEST("diagnostics.TestLocaleFilesIgnoreStaleDiagnostics",
                   TestLocaleFilesIgnoreStaleDiagnostics);
