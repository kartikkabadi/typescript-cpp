#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace tsc::locale {

// locale.go — a BCP-47 language tag; zero value is Default.
struct Locale {
	std::string tag;
	// Go: `language.Tag(l).String()` — the Default (empty) tag renders ""
	// and every other tag renders itself, i.e. `tag` verbatim.
	std::string String() const { return tag; }
	bool operator==(const Locale&) const = default;
};
inline Locale Default{};

// locale.Parse — locale.go:36. Go uses golang.org/x/text/language.Parse
// (strict BCP-47 well-formedness with graceful failure, no canonicalization
// needed for our uses: --locale flag validation + tag storage).
std::pair<Locale, bool> Parse(std::string_view localeStr);

}  // namespace tsc::locale
