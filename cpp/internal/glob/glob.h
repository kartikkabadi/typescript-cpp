#pragma once

// glob — glob.go:1-349
//
// LSP-compliant glob patterns (lsp 3.17 spec, #documentFilter):
//   *   one or more characters in a path segment
//   ?   one character in a path segment
//   **  any number of path segments, including none
//   {}  group of sub patterns as an OR expression
//   []  a range of characters; [!...] negates
//   '/' matches one or more literal slashes; other chars match literally.

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace tsc::glob {

struct Glob;

// element types — glob.go:184
struct Slash {};                    // One or more '/' separators
struct Literal { std::string text; }; // string literal, not containing /, *, ?, {}, or []
struct Star {};                     // *
struct AnyChar {};                  // ?
struct StarStar {};                 // **
struct Group { std::vector<std::shared_ptr<Glob>> globs; }; // {foo, bar, ...} grouping
struct CharRange {                  // [a-z] character range
    bool negate;
    char32_t low, high;
};

// element holds a glob pattern element (element fmt.Stringer).
using Element =
    std::variant<Slash, Literal, Star, AnyChar, StarStar, Group, CharRange>;

// A Glob is an LSP-compliant glob pattern.
struct Glob {
    std::vector<Element> elems; // pattern elements

    // Match reports whether the input string matches the glob pattern.
    bool match(std::string_view input) const;

    std::string string() const;
};

// Parse builds a Glob for the given pattern. The error string is empty on
// success (one of the sentinel messages from glob.go otherwise).
std::pair<std::shared_ptr<Glob>, std::string> parse(std::string_view pattern);

} // namespace tsc::glob
