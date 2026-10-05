// glob — glob.go:1-349

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/glob/glob.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::glob {

namespace {

// Sentinel errors — glob.go:152
const std::string kErrBadRange = "'[' patterns must be of the form [x-y]";
const std::string kErrInvalidUTF8 = "invalid UTF-8 encoding";

// helper for decoding a rune in range elements, e.g. [a-z] — glob.go:137
struct RuneResult {
    char32_t r;
    int sz;
    std::string err; // empty = nil
};

RuneResult readRangeRune(std::string_view input) {
    int sz = 0;
    char32_t r = decodeUtf8Rune(input, &sz);
    std::string err;
    if (r == kRuneError) {
        // See the documentation for DecodeRuneInString.
        switch (sz) {
        case 0:
            err = kErrBadRange;
            break;
        case 1:
            err = kErrInvalidUTF8;
            break;
        }
    }
    return {r, sz, err};
}

// parseLiteral — glob.go:157. Appends a literal element covering all bytes up
// to the next special char; returns the remaining pattern.
std::string_view parseLiteral(Glob* g, std::string_view pattern, bool nested) {
    std::string_view specialChars = nested ? "*?{[/}," : "*?{[/";
    size_t end = pattern.find_first_of(specialChars);
    if (end == std::string_view::npos) {
        end = pattern.size();
    }
    g->elems.push_back(Literal{std::string(pattern.substr(0, end))});
    return pattern.substr(end);
}

struct ParseResult {
    std::shared_ptr<Glob> g; // nil on error
    std::string_view rest;
    std::string err;
};

// parse — glob.go:52
ParseResult parseInternal(std::string_view pattern, bool nested) {
    auto g = std::make_shared<Glob>();
    while (!pattern.empty()) {
        switch (pattern[0]) {
        case '/':
            pattern = pattern.substr(1);
            g->elems.push_back(Slash{});
            break;

        case '*':
            if (pattern.size() > 1 && pattern[1] == '*') {
                if ((!g->elems.empty() && !std::holds_alternative<Slash>(g->elems.back())) ||
                    (pattern.size() > 2 && pattern[2] != '/')) {
                    return {nullptr, {}, "** may only be adjacent to '/'"};
                }
                pattern = pattern.substr(2);
                g->elems.push_back(StarStar{});
                break;
            }
            pattern = pattern.substr(1);
            g->elems.push_back(Star{});
            break;

        case '?':
            pattern = pattern.substr(1);
            g->elems.push_back(AnyChar{});
            break;

        case '{': {
            Group gs;
            while (pattern[0] != '}') {
                pattern = pattern.substr(1);
                ParseResult pr = parseInternal(pattern, true);
                if (!pr.err.empty()) {
                    return {nullptr, {}, pr.err};
                }
                if (pr.rest.empty()) {
                    return {nullptr, {}, "unmatched '{'"};
                }
                pattern = pr.rest;
                gs.globs.push_back(pr.g);
            }
            pattern = pattern.substr(1);
            g->elems.push_back(std::move(gs));
            break;
        }

        case '}':
        case ',':
            if (nested) {
                return {g, pattern, {}};
            }
            pattern = parseLiteral(g.get(), pattern, false);
            break;

        case '[': {
            pattern = pattern.substr(1);
            if (pattern.empty()) {
                return {nullptr, {}, kErrBadRange};
            }
            bool negate = false;
            if (pattern[0] == '!') {
                pattern = pattern.substr(1);
                negate = true;
            }
            RuneResult low = readRangeRune(pattern);
            if (!low.err.empty()) {
                return {nullptr, {}, low.err};
            }
            pattern = pattern.substr(low.sz);
            if (pattern.empty() || pattern[0] != '-') {
                return {nullptr, {}, kErrBadRange};
            }
            pattern = pattern.substr(1);
            RuneResult high = readRangeRune(pattern);
            if (!high.err.empty()) {
                return {nullptr, {}, high.err};
            }
            pattern = pattern.substr(high.sz);
            if (pattern.empty() || pattern[0] != ']') {
                return {nullptr, {}, kErrBadRange};
            }
            pattern = pattern.substr(1);
            g->elems.push_back(CharRange{negate, low.r, high.r});
            break;
        }

        default:
            pattern = parseLiteral(g.get(), pattern, nested);
            break;
        }
    }
    return {g, {}, {}};
}

// split returns the portion before and after the first slash (or sequence of
// consecutive slashes). If there is no slash it returns (input, "").
// — glob.go:337
std::pair<std::string_view, std::string_view> split(std::string_view input) {
    size_t i = input.find('/');
    if (i == std::string_view::npos) {
        return {input, {}};
    }
    std::string_view first = input.substr(0, i);
    for (size_t j = i; j < input.size(); j++) {
        if (input[j] != '/') {
            return {first, input.substr(j)};
        }
    }
    return {first, {}};
}

// match — glob.go:219
bool match(std::span<const Element> elems, std::string_view input) {
    while (!elems.empty()) {
        const Element& elem = elems.front();
        elems = elems.subspan(1);
        switch (elem.index()) {
        case 0: { // slash
            if (input.empty() || input[0] != '/') {
                return false;
            }
            while (input[0] == '/') {
                input = input.substr(1);
            }
            break;
        }

        case 4: { // starStar
            // Special cases:
            //  - **/a matches "a"
            //  - **/ matches everything
            //
            // Note that if ** is followed by anything, it must be '/' (this is
            // enforced by Parse).
            if (!elems.empty()) {
                elems = elems.subspan(1);
            }

            // A trailing ** matches anything.
            if (elems.empty()) {
                return true;
            }

            // Backtracking: advance pattern segments until the remaining
            // pattern elements match.
            while (!input.empty()) {
                if (match(elems, input)) {
                    return true;
                }
                input = split(input).second;
            }
            return false;
        }

        case 1: { // literal
            const std::string& lit = std::get<Literal>(elem).text;
            if (input.substr(0, lit.size()) != lit) {
                return false;
            }
            input = input.substr(lit.size());
            break;
        }

        case 2: { // star
            auto [segInput, rest] = split(input);
            input = rest;

            size_t elemEnd = elems.size();
            for (size_t i = 0; i < elems.size(); i++) {
                if (std::holds_alternative<Slash>(elems[i])) {
                    elemEnd = i;
                    break;
                }
            }
            std::span<const Element> segElems = elems.subspan(0, elemEnd);
            elems = elems.subspan(elemEnd);

            // A trailing * matches the entire segment.
            if (segElems.empty()) {
                break;
            }

            // Backtracking: advance characters until remaining subpattern
            // elements match.
            bool matched = false;
            for (size_t i = 0; i < segInput.size(); i++) {
                if (match(segElems, segInput.substr(i))) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                return false;
            }
            break;
        }

        case 3: { // anyChar
            if (input.empty() || input[0] == '/') {
                return false;
            }
            input = input.substr(1);
            break;
        }

        case 5: { // group
            // Append remaining pattern elements to each group member looking
            // for a match.
            std::vector<Element> branch;
            for (const auto& m : std::get<Group>(elem).globs) {
                branch.clear();
                branch.insert(branch.end(), m->elems.begin(), m->elems.end());
                branch.insert(branch.end(), elems.begin(), elems.end());
                if (match(branch, input)) {
                    return true;
                }
            }
            return false;
        }

        case 6: { // charRange
            if (input.empty() || input[0] == '/') {
                return false;
            }
            const CharRange& r = std::get<CharRange>(elem);
            int sz = 0;
            char32_t c = decodeUtf8Rune(input, &sz);
            if (c < r.low || c > r.high) {
                return false;
            }
            input = input.substr(sz);
            break;
        }

        default:
            tscUnreachable("segment type not implemented");
        }
    }

    return input.empty();
}

// element Stringers — glob.go:197
void appendElementString(std::string& b, const Element& e) {
    switch (e.index()) {
    case 0: b += "/"; break;                                  // slash
    case 1: b += std::get<Literal>(e).text; break;            // literal
    case 2: b += "*"; break;                                  // star
    case 3: b += "?"; break;                                  // anyChar
    case 4: b += "**"; break;                                 // starStar
    case 5: {                                                 // group
        b += "{";
        bool first = true;
        for (const auto& m : std::get<Group>(e).globs) {
            if (!first) b += ",";
            first = false;
            b += m->string();
        }
        b += "}";
        break;
    }
    case 6: {                                                 // charRange
        const CharRange& r = std::get<CharRange>(e);
        b += "[" + utf8String(r.low) + "-" + utf8String(r.high) + "]";
        break;
    }
    }
}

} // namespace

// Parse — glob.go:47
std::pair<std::shared_ptr<Glob>, std::string> parse(std::string_view pattern) {
    ParseResult pr = parseInternal(pattern, false);
    return {pr.g, pr.err};
}

// Match — glob.go:215
bool Glob::match(std::string_view input) const {
    return glob::match(std::span<const Element>(elems), input);
}

// String — glob.go:172
std::string Glob::string() const {
    std::string b;
    for (const Element& e : elems) {
        appendElementString(b, e);
    }
    return b;
}

} // namespace tsc::glob
