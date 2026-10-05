// locale.h — dep-stub decls for tsc/internal/locale — owned by the locale
// slice. A BCP-47 language tag; zero value is Default. (Shape lifted
// verbatim from tsoptions.h's former minimal block.)
#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace tsc::locale {

// locale.go — a BCP-47 language tag; zero value is Default.
struct Locale {
	std::string tag;
	std::string String() const { return tag; }
	bool operator==(const Locale&) const = default;
};
inline Locale Default{};

// locale.Parse — locale.go:36. dep-stubbed in tsconfigparsing' TU.
std::pair<Locale, bool> Parse(std::string_view localeStr);

} // namespace tsc::locale
