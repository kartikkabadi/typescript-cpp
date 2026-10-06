// locale — locale.go:1-40
//
// `Parse` is a faithful port of golang.org/x/text v0.21.0 `language.Parse`:
//   internal/tag/tag.go         — Index, FixCase, cmp
//   internal/language/parse.go  — scanner, parse, parseTag, parseVariants,
//                                 parseExtensions, parseExtension
//   internal/language/lookup.go — getLangID/ISO2/ISO3, getRegionID/ISO2/ISO3/
//                                 M49, getScriptID, normLang, normRegion,
//                                 grandfathered, Language/Script/Region.String
//   language/language.go        — canonicalize (Default = Deprecated|Legacy),
//                                 Tag.String, Tag.RemakeString
// All table data lives in localetags.h (generated verbatim from x/text's
// generated tables; see the header comment there).

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/locale/locale.h"

namespace tsc::locale::detail {

namespace {

enum class ScanErr { none, value, dupKey, syntax };

// isAlpha returns true if the byte is not a digit.
// b must be an ASCII letter or digit.
bool isAlpha(char b) {
    return static_cast<unsigned char>(b) > '9';
}

// isAlphaNum returns true if the string contains only ASCII letters or digits.
bool isAlphaNum(std::string_view s) {
    for (char c : s) {
        if (!('a' <= c && c <= 'z' || 'A' <= c && c <= 'Z' || '0' <= c && c <= '9')) {
            return false;
        }
    }
    return true;
}

// cmp — tag.go: lexicographic comparison.
int cmp(std::string_view a, std::string_view b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    if (a.size() < b.size()) return -1;
    if (a.size() > b.size()) return 1;
    return 0;
}

// FixCase reformats b to the same pattern of cases as form ('Z'-or-below
// pattern chars mean uppercase, anything else means lowercase).
// Returns false if b is malformed. Writes into b like Go writes into the
// token's backing slice.
bool fixCase(std::string_view form, char* b, size_t n) {
    if (form.size() != n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = b[i];
        if (form[i] <= 'Z') {
            if (c >= 'a') {
                c -= 'z' - 'Z';
            }
            if (c < 'A' || 'Z' < c) {
                return false;
            }
        } else {
            if (c <= 'Z') {
                c += 'z' - 'Z';
            }
            if (c < 'a' || 'z' < c) {
                return false;
            }
        }
        b[i] = c;
    }
    return true;
}

// --- tag.Index — tag.go ----------------------------------------------------

struct Index {
    std::string_view s;

    // Elem returns the element data at the given index.
    std::string_view elem(int x) const { return s.substr(static_cast<size_t>(x) * 4, 4); }

    // Index reports the index of the first 4-byte entry whose first len(key)
    // bytes are equal to key, or -1.
    int index(std::string_view key) const {
        const int n = static_cast<int>(key.size());
        int count = static_cast<int>(s.size()) / 4;
        int lo = 0, hi = count;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (cmp(s.substr(static_cast<size_t>(mid) * 4, n), key) >= 0) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        int i = lo * 4;
        if (cmp(s.substr(i, key.size()), key) != 0) {
            return -1;
        }
        return lo;
    }

    // Next finds the next occurrence of key after index x.
    int next(std::string_view key, int x) const {
        x++;
        if (x * 4 < static_cast<int>(s.size()) && cmp(s.substr(static_cast<size_t>(x) * 4, key.size()), key) == 0) {
            return x;
        }
        return -1;
    }
};

// makeIndex binds a 4-byte-entry table that contains embedded NUL bytes.
// Index{arr} would aggregate a string_view from `const char*` — strlen stops
// at the first NUL (every table's first entry is "---\0"/"----"), truncating
// the table to ~3 bytes and failing every lookup. Carry the array length.
template <size_t N>
constexpr Index makeIndex(const char (&arr)[N]) {
    return Index{std::string_view{arr, N - 1}};
}

// strToInt — lookup.go.
unsigned strToInt(std::string_view s) {
    unsigned v = 0;
    for (char c : s) {
        v *= 26;
        v += static_cast<unsigned>(c - 'a');
    }
    return v;
}

// intToStr converts the given integer to the original ASCII string passed to
// strToInt. Writes exactly 3 bytes.
void intToStr(unsigned v, char* s) {
    for (int i = 2; i >= 0; i--) {
        s[i] = static_cast<char>(v % 26) + 'a';
        v /= 26;
    }
}

// --- Language/Script/Region.String — lookup.go -----------------------------

std::string langToString(LangID id) {
    if (id >= langNoIndexOffset) {
        char buf[3];
        intToStr(static_cast<unsigned>(id) - langNoIndexOffset, buf);
        return std::string(buf, 3);
    }
    if (id == 0) {
        return "und";
    }
    const Index idx = makeIndex(kLangIndex);
    std::string_view e = idx.elem(id);
    if (e[3] == 0) {
        return std::string(e.substr(0, 3));
    }
    return std::string(e.substr(0, 2));
}

std::string scriptToString(ScriptID s) {
    if (s == 0) {
        return "Zzzz";
    }
    return std::string(makeIndex(kScriptIndex).elem(s));
}

std::string regionToString(RegionID r) {
    if (r < isoRegionOffset) {
        if (r == 0) {
            return "ZZ";
        }
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%03d", static_cast<int>(kM49[r]));
        return buf;
    }
    return std::string(makeIndex(kRegionISOIndex).elem(r - isoRegionOffset).substr(0, 2));
}

// --- scanner — internal/language/parse.go ----------------------------------

struct Scanner {
    std::string b;
    std::string token;
    int start = 0; // start position of the current token
    int end = 0;   // end position of the current token
    int next = 0;  // next point for scan
    ScanErr err = ScanErr::none;
    bool done = false;

    explicit Scanner(std::string_view s) : b(s) { init(); }

    void init() {
        for (auto& c : b) {
            if (c == '_') {
                c = '-';
            }
        }
        scan();
    }

    void toLower(int lo, int hi) {
        for (int i = lo; i < hi && i < static_cast<int>(b.size()); i++) {
            char& c = b[i];
            if ('A' <= c && c <= 'Z') {
                c += 'a' - 'A';
            }
        }
    }

    void setError(ScanErr e) {
        if (e == ScanErr::none) {
            return;
        }
        if (err == ScanErr::none || (e == ScanErr::syntax && err != ScanErr::syntax)) {
            err = e;
        }
    }

    // resizeRange shrinks or grows the array at position oldStart such that
    // a new string of size newSize can fit between oldStart and oldEnd.
    // Sets the scan point to after the resized range.
    void resizeRange(int oldStart, int oldEnd, int newSize) {
        start = oldStart;
        int endPos = oldStart + newSize;
        if (endPos != oldEnd) {
            b.replace(oldStart, oldEnd - oldStart, std::string(newSize, '\0'));
            next = endPos + (next - end);
            end = endPos;
        }
    }

    // replace replaces the current token with repl. (Go's s.token slice is
    // left pointing at the old bytes; nothing reads it before the next
    // scan(), so token is deliberately not refreshed.)
    void replace(std::string_view repl) {
        resizeRange(start, end, static_cast<int>(repl.size()));
        b.replace(start, repl.size(), repl);
    }

    // gobble removes the current token from the input.
    // Caller must call scan after calling gobble.
    void gobble(ScanErr e) {
        setError(e);
        if (start == 0) {
            b = b.substr(next);
            end = 0;
        } else {
            b = b.substr(0, start - 1) + b.substr(end);
            end = start - 1;
        }
        next = start;
    }

    // deleteRange removes the given range from b before the current token.
    void deleteRange(int s, int e) {
        b = b.substr(0, s) + b.substr(e);
        int diff = e - s;
        next -= diff;
        start -= diff;
        end -= diff;
    }

    // fixTokenCase applies FixCase to the current token inside b (Go's
    // FixCase writes into the token slice's shared storage), then refreshes
    // token. Returns false on malformed input.
    bool fixTokenCase(std::string_view form) {
        if (token.empty() || form.size() != token.size()) {
            return false;
        }
        bool ok = fixCase(form, b.data() + start, token.size());
        if (ok) {
            token = b.substr(start, end - start);
        }
        return ok;
    }

    // scan parses the next token of a BCP 47 string. Tokens that are larger
    // than 8 characters or include non-alphanumeric characters result in an
    // error and are gobbled and removed from the output.
    // It returns the end position of the last token consumed.
    int scan() {
        int endPos = end;
        token.clear();
        for (start = next; next < static_cast<int>(b.size());) {
            auto dash = b.find('-', static_cast<size_t>(next));
            int i;
            if (dash == std::string::npos) {
                end = static_cast<int>(b.size());
                next = end;
                i = end - start;
            } else {
                // b.find is absolute; Go's i = bytes.IndexByte(s.b[next:])
                // is relative. end = next + i = dash; i stays the token
                // length (start == next at loop top).
                end = static_cast<int>(dash);
                next = end + 1;
                i = end - start;
            }
            std::string_view tok(b.data() + start, end - start);
            if (i < 1 || i > 8 || !isAlphaNum(tok)) {
                gobble(ScanErr::syntax);
                continue;
            }
            token.assign(tok);
            return endPos;
        }
        if (!b.empty() && b.back() == '-') {
            setError(ScanErr::syntax);
            b.pop_back();
        }
        done = true;
        return endPos;
    }

    // acceptMinSize parses multiple tokens of the given size or greater.
    // It returns the end position of the last token consumed.
    int acceptMinSize(int min) {
        int endPos = end;
        scan();
        for (; static_cast<int>(token.size()) >= min; scan()) {
            endPos = end;
        }
        return endPos;
    }
};

// --- lookups on the current token — lookup.go -------------------------------

// findIndex tries to find the (case-fixed) token in idx.
std::pair<int, ScanErr> findIndex(Scanner& scan, const Index& idx, std::string_view form) {
    if (!scan.fixTokenCase(form)) {
        return {0, ScanErr::syntax};
    }
    int i = idx.index(scan.token);
    if (i == -1) {
        return {0, ScanErr::value};
    }
    return {i, ScanErr::none};
}

std::pair<LangID, ScanErr> getLangISO2(Scanner& scan) {
    if (!scan.fixTokenCase("zz")) {
        return {0, ScanErr::syntax};
    }
    const Index idx = makeIndex(kLangIndex);
    int i = idx.index(scan.token);
    if (i != -1 && idx.elem(i)[3] != 0) {
        return {static_cast<LangID>(i), ScanErr::none};
    }
    return {0, ScanErr::value};
}

std::pair<LangID, ScanErr> getLangISO3(Scanner& scan) {
    if (!scan.fixTokenCase("und")) {
        return {0, ScanErr::syntax};
    }
    const Index idx = makeIndex(kLangIndex);
    // first try to match canonical 3-letter entries
    for (int i = idx.index(scan.token.substr(0, 2)); i != -1; i = idx.next(scan.token.substr(0, 2), i)) {
        std::string_view e = idx.elem(i);
        if (e[3] == 0 && e[2] == scan.token[2]) {
            LangID id = static_cast<LangID>(i);
            // We treat "und" as special and always translate it to "unspecified".
            if (id == nonCanonicalUnd) {
                return {0, ScanErr::none};
            }
            return {id, ScanErr::none};
        }
    }
    const Index alt = makeIndex(kAltLangISO3);
    if (int i = alt.index(scan.token); i != -1) {
        return {static_cast<LangID>(kAltLangIndex[static_cast<unsigned char>(alt.elem(i)[3])]), ScanErr::none};
    }
    unsigned n = strToInt(scan.token);
    if (kLangNoIndex[n / 8] & (1u << (n % 8))) {
        return {static_cast<LangID>(n + langNoIndexOffset), ScanErr::none};
    }
    // Check for non-canonical uses of ISO3.
    for (int i = idx.index(scan.token.substr(0, 1)); i != -1; i = idx.next(scan.token.substr(0, 1), i)) {
        std::string_view e = idx.elem(i);
        if (e[2] == scan.token[1] && e[3] == scan.token[2]) {
            return {static_cast<LangID>(i), ScanErr::none};
        }
    }
    return {0, ScanErr::value};
}

// getLangID returns the langID of s if s is a canonical subtag.
std::pair<LangID, ScanErr> getLangID(Scanner& scan) {
    if (scan.token.size() == 2) {
        return getLangISO2(scan);
    }
    return getLangISO3(scan);
}

std::pair<RegionID, ScanErr> getRegionISO2(Scanner& scan) {
    auto [i, err] = findIndex(scan, makeIndex(kRegionISOIndex), "ZZ");
    if (err != ScanErr::none) {
        return {0, err};
    }
    return {static_cast<RegionID>(i + isoRegionOffset), ScanErr::none};
}

std::pair<RegionID, ScanErr> getRegionISO3(Scanner& scan) {
    if (!scan.fixTokenCase("ZZZ")) {
        return {0, ScanErr::syntax};
    }
    const Index idx = makeIndex(kRegionISOIndex);
    for (int i = idx.index(scan.token.substr(0, 1)); i != -1; i = idx.next(scan.token.substr(0, 1), i)) {
        std::string_view e = idx.elem(i);
        if (e[2] == scan.token[1] && e[3] == scan.token[2]) {
            return {static_cast<RegionID>(i + isoRegionOffset), ScanErr::none};
        }
    }
    std::string_view alt3{kAltRegionISO3, sizeof(kAltRegionISO3) - 1};
    for (size_t i = 0; i < alt3.size(); i += 3) {
        if (cmp(alt3.substr(i, 3), scan.token) == 0) {
            return {static_cast<RegionID>(kAltRegionIDs[i / 3]), ScanErr::none};
        }
    }
    return {0, ScanErr::value};
}

std::pair<RegionID, ScanErr> getRegionM49(const Scanner& scan) {
    // strconv.ParseUint(token, 10, 10): all digits, 0 < n <= 999.
    unsigned n = 0;
    for (char c : scan.token) {
        if (c < '0' || c > '9') {
            // Parse failure falls through to getRegionISO2 by the caller.
            return {0, ScanErr::syntax};
        }
        n = n * 10 + static_cast<unsigned>(c - '0');
    }
    if (0 < n && n <= 999) {
        constexpr int searchBits = 7;
        constexpr int regionBits = 9;
        constexpr int regionMask = (1 << regionBits) - 1;
        int idx = n >> searchBits;
        const uint16_t* lo = kFromM49 + kM49Index[idx];
        const uint16_t* hi = kFromM49 + kM49Index[idx + 1];
        uint16_t val = static_cast<uint16_t>(n) << regionBits;
        const uint16_t* it = std::lower_bound(lo, hi, val);
        if (it != hi && (*it & ~regionMask) == val) {
            return {static_cast<RegionID>(*it & regionMask), ScanErr::none};
        }
    }
    return {0, ScanErr::value};
}

// getRegionID returns the region id for the current token if it is a valid
// 2-letter region code, 3-letter ISO code, or M49 numeric code.
std::pair<RegionID, ScanErr> getRegionID(Scanner& scan) {
    if (scan.token.size() == 3) {
        if (isAlpha(scan.token[0])) {
            return getRegionISO3(scan);
        }
        auto [r, err] = getRegionM49(scan);
        if (err == ScanErr::none) {
            return {r, err};
        }
        if (err == ScanErr::value) {
            return {0, ScanErr::value};
        }
        // ParseUint failed → fall through to getRegionISO2.
    }
    return getRegionISO2(scan);
}

std::pair<ScriptID, ScanErr> getScriptID(Scanner& scan, const Index& idx) {
    auto [i, err] = findIndex(scan, idx, "Zzzz");
    return {static_cast<ScriptID>(i), err};
}

// --- parseTag / parseVariants / parseExtensions — parse.go -------------------

std::pair<Tag, int> parseTag(Scanner& scan, bool doNorm);

// bytesSort: sort token strings by their first n bytes.
void sortBytes(std::vector<std::string>& v, int n) {
    std::sort(v.begin(), v.end(), [n](const std::string& a, const std::string& c) {
        return cmp(std::string_view(a).substr(0, n), std::string_view(c).substr(0, n)) < 0;
    });
}
void stableSortBytes(std::vector<std::string>& v, int n) {
    std::stable_sort(v.begin(), v.end(), [n](const std::string& a, const std::string& c) {
        return cmp(std::string_view(a).substr(0, n), std::string_view(c).substr(0, n)) < 0;
    });
}
std::string joinBytes(const std::vector<std::string>& v, char sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

// parseVariants scans tokens as long as each token is a valid variant string.
// Duplicate variants are removed.
int parseVariants(Scanner& scan, int end, Tag& t) {
    int start = scan.start;
    std::vector<uint8_t> varID;
    std::vector<std::string> variant;
    int last = -1;
    bool needSort = false;
    for (; scan.token.size() >= 4; scan.scan()) {
        // Look up variant in the registry index (Go: variantIndex map).
        uint8_t v = 0;
        bool ok = false;
        for (const auto& e : kVariantIndex) {
            if (scan.token == e.name) {
                v = e.id;
                ok = true;
                break;
            }
        }
        if (!ok) {
            // unknown variant
            scan.gobble(ScanErr::value);
            continue;
        }
        varID.push_back(v);
        variant.push_back(scan.token);
        if (!needSort) {
            if (last < static_cast<int>(v)) {
                last = static_cast<int>(v);
            } else {
                needSort = true;
                // There is no legal combination of more than 7 variants
                // (and this is by no means a useful sequence).
                constexpr int maxVariants = 8;
                if (varID.size() > maxVariants) {
                    break;
                }
            }
        }
        end = scan.end;
    }
    if (needSort) {
        // variantsSort{varID, variant}
        std::vector<size_t> order(varID.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t c) { return varID[a] < varID[c]; });
        std::vector<uint8_t> sortedID;
        std::vector<std::string> sortedVar;
        for (size_t i : order) {
            sortedID.push_back(varID[i]);
            sortedVar.push_back(std::move(variant[i]));
        }
        // Remove duplicates.
        std::vector<std::string> kept;
        int l = -1;
        for (size_t i = 0; i < sortedID.size(); i++) {
            int w = sortedID[i];
            if (l == w) {
                continue;
            }
            kept.push_back(std::move(sortedVar[i]));
            l = w;
        }
        std::string str = joinBytes(kept, '-');
        if (str.empty()) {
            end = start - 1;
        } else {
            scan.resizeRange(start, end, static_cast<int>(str.size()));
            scan.b.replace(scan.start, str.size(), str);
            end = scan.end;
        }
    }
    return end;
}

// parseExtension parses a single extension and returns the position of
// the extension end.
int parseExtension(Scanner& scan) {
    int start = scan.start, end = scan.end;
    switch (scan.token[0]) {
    case 'u': { // https://www.ietf.org/rfc/rfc6067.txt
        int attrStart = end;
        scan.scan();
        // Attribute keys may precede the key-type list. They are tokens of
        // more than 2 characters and may appear in any order.
        std::string lastAttr;
        while (scan.token.size() > 2) {
            if (cmp(scan.token, lastAttr) != -1) {
                // Attributes are unsorted. Start over from scratch.
                int p = attrStart + 1;
                scan.next = p;
                std::vector<std::string> attrs;
                for (scan.scan(); scan.token.size() > 2; scan.scan()) {
                    attrs.push_back(scan.token);
                    end = scan.end;
                }
                sortBytes(attrs, 3);
                // Go: copy(scan.b[p:], bytes.Join(attrs, separator))
                std::string joined = joinBytes(attrs, '-');
                scan.b.replace(static_cast<size_t>(p), joined.size(), joined);
                break;
            }
            lastAttr = scan.token;
            end = scan.end;
            scan.scan();
        }
        // Scan key-type sequences. A key is of length 2 and may be followed
        // by 0 or more "type" subtags from 3 to the maximum of 8 letters.
        std::string last, key;
        for (int attrEnd = end; scan.token.size() == 2; last = key) {
            key = scan.token;
            end = scan.end;
            for (scan.scan(); end < scan.end && scan.token.size() > 2; scan.scan()) {
                end = scan.end;
            }
            // TODO: check key value validity
            if (cmp(key, last) != 1 || scan.err != ScanErr::none) {
                // We have an invalid key or the keys are not sorted.
                // Start scanning keys from scratch and reorder.
                int p = attrEnd + 1;
                scan.next = p;
                std::vector<std::string> keys;
                for (scan.scan(); scan.token.size() == 2;) {
                    int keyStart = scan.start;
                    end = scan.end;
                    for (scan.scan(); end < scan.end && scan.token.size() > 2; scan.scan()) {
                        end = scan.end;
                    }
                    keys.push_back(scan.b.substr(static_cast<size_t>(keyStart), static_cast<size_t>(end - keyStart)));
                }
                stableSortBytes(keys, 2);
                if (!keys.empty()) {
                    size_t k = 0;
                    for (size_t i = 1; i < keys.size(); i++) {
                        if (cmp(keys[k].substr(0, 2), keys[i].substr(0, 2)) != 0) {
                            k++;
                            keys[k] = keys[i];
                        } else if (keys[k] != keys[i]) {
                            scan.setError(ScanErr::dupKey);
                        }
                    }
                    keys.resize(k + 1);
                }
                std::string reordered = joinBytes(keys, '-');
                if (int e = p + static_cast<int>(reordered.size()); e < end) {
                    scan.deleteRange(e, end);
                    end = e;
                }
                scan.b.replace(static_cast<size_t>(p), reordered.size(), reordered);
                break;
            }
        }
        break;
    }
    case 't': { // https://www.ietf.org/rfc/rfc6497.txt
        scan.scan();
        if (size_t n = scan.token.size(); n >= 2 && n <= 3 && isAlpha(scan.token[1])) {
            Tag unused;
            int newEnd = 0;
            std::tie(unused, newEnd) = parseTag(scan, false);
            end = newEnd;
            scan.toLower(start, end);
        }
        for (; scan.token.size() == 2 && !isAlpha(scan.token[1]);) {
            end = scan.acceptMinSize(3);
        }
        break;
    }
    case 'x':
        end = scan.acceptMinSize(1);
        break;
    default:
        end = scan.acceptMinSize(2);
        break;
    }
    return end;
}

// parseExtensions parses and normalizes the extensions in the buffer.
// It returns the last position of scan.b that is part of any extension.
// It also trims scan.b to remove excess parts accordingly.
int parseExtensions(Scanner& scan) {
    int start = scan.start;
    std::vector<std::string> exts;
    std::string privateExt;
    int end = scan.end;
    while (scan.token.size() == 1) {
        int extStart = scan.start;
        char ext = scan.token[0];
        end = parseExtension(scan);
        std::string extension = scan.b.substr(static_cast<size_t>(extStart), static_cast<size_t>(end - extStart));
        if (extension.size() < 3 || (ext != 'x' && extension.size() < 4)) {
            scan.setError(ScanErr::syntax);
            end = extStart;
            continue;
        }
        if (start == extStart && (ext == 'x' || scan.start == static_cast<int>(scan.b.size()))) {
            scan.b.resize(static_cast<size_t>(end));
            return end;
        }
        if (ext == 'x') {
            privateExt = extension;
            break;
        }
        exts.push_back(std::move(extension));
    }
    sortBytes(exts, 1);
    if (!privateExt.empty()) {
        exts.push_back(std::move(privateExt));
    }
    scan.b.resize(static_cast<size_t>(start));
    if (!exts.empty()) {
        scan.b += joinBytes(exts, '-');
    } else if (start > 0) {
        // Strip trailing '-'.
        scan.b.resize(static_cast<size_t>(start - 1));
    }
    return end;
}

// parseTag parses language, script, region and variants.
// It returns a Tag and the end position in the input that was parsed.
// If doNorm is true, then <lang>-<extlang> will be normalized to <extlang>.
std::pair<Tag, int> parseTag(Scanner& scan, bool doNorm) {
    Tag t;
    ScanErr e;
    std::tie(t.langID, e) = getLangID(scan);
    scan.setError(e);
    scan.replace(langToString(t.langID));
    int langStart = scan.start;
    int end = scan.scan();
    while (scan.token.size() == 3 && isAlpha(scan.token[0])) {
        // <lang>-<extlang> tags are equivalent to a tag of the form <extlang>.
        if (doNorm) {
            LangID lang;
            std::tie(lang, e) = getLangID(scan);
            if (lang != 0) {
                t.langID = lang;
                std::string langStr = langToString(lang);
                scan.b.replace(static_cast<size_t>(langStart), langStr.size(), langStr);
                scan.b[langStart + langStr.size()] = '-';
                scan.start = langStart + static_cast<int>(langStr.size()) + 1;
            }
            scan.gobble(e);
        }
        end = scan.scan();
    }
    if (scan.token.size() == 4 && isAlpha(scan.token[0])) {
        std::tie(t.scriptID, e) = getScriptID(scan, makeIndex(kScriptIndex));
        if (t.scriptID == 0) {
            scan.gobble(e);
        }
        end = scan.scan();
    }
    if (size_t n = scan.token.size(); n >= 2 && n <= 3) {
        std::tie(t.regionID, e) = getRegionID(scan);
        if (t.regionID == 0) {
            scan.gobble(e);
        } else {
            scan.replace(regionToString(t.regionID));
        }
        end = scan.scan();
    }
    scan.toLower(scan.start, static_cast<int>(scan.b.size()));
    t.pVariant = static_cast<uint8_t>(end);
    end = parseVariants(scan, end, t);
    t.pExt = static_cast<uint16_t>(end);
    return {t, end};
}

// genCoreBytes — language.go.
std::string genCoreBytes(const Tag& t) {
    std::string b = langToString(t.langID);
    if (t.scriptID != 0) {
        b += '-';
        b += scriptToString(t.scriptID);
    }
    if (t.regionID != 0) {
        b += '-';
        b += regionToString(t.regionID);
    }
    return b;
}

// --- canonicalize — language/language.go (Default = Deprecated | Legacy) ----

constexpr int8_t aliasDeprecated = 0;
constexpr int8_t aliasMacro = 1;
constexpr int8_t aliasLegacy = 2;

// normLang returns the mapped langID of id according to kAliasMap.
std::pair<LangID, int8_t> normLang(LangID id) {
    const FromTo* it = std::lower_bound(kAliasMap, kAliasMap + 193, id,
                                        [](const FromTo& f, uint16_t v) { return f.from < v; });
    if (it != kAliasMap + 193 && it->from == id) {
        return {it->to, static_cast<int8_t>(kAliasTypes[it - kAliasMap])};
    }
    return {id, static_cast<int8_t>(-1)};
}

// normRegion returns a region if r is deprecated or 0 otherwise.
RegionID normRegion(RegionID r) {
    const FromTo* it = std::lower_bound(kRegionOldMap, kRegionOldMap + 20, r,
                                        [](const FromTo& f, uint16_t v) { return f.from < v; });
    if (it != kRegionOldMap + 20 && it->from == r) {
        return it->to;
    }
    return 0;
}

// canonicalize with CanonType = Default (Deprecated | Legacy): replaces
// deprecated/legacy base languages, deprecated scripts and regions.
// SuppressScript/Macro/CLDR are not part of Default and are not applied.
std::pair<Tag, bool> canonicalizeDefault(Tag t) {
    bool changed = false;
    // canonLang = DeprecatedBase | Legacy | Macro; Default ⊂ canonLang.
    for (;;) {
        auto [l, aliasType] = normLang(t.langID);
        if (l != t.langID) {
            switch (aliasType) {
            case aliasLegacy:
                // (c&Legacy != 0)
                if (t.langID == _sh && t.scriptID == 0) {
                    t.scriptID = _Latn;
                }
                t.langID = l;
                changed = true;
                break;
            case aliasDeprecated:
                // (c&DeprecatedBase != 0)
                if (t.langID == _mo && t.regionID == 0) {
                    t.regionID = _MD;
                }
                t.langID = l;
                changed = true;
                // Other canonicalization types may still apply.
                continue;
            case aliasMacro:
            default:
                // (c&Macro == 0 under Default)
                break;
            }
        }
        break;
    }
    // DeprecatedScript
    if (t.scriptID == _Qaai) {
        changed = true;
        t.scriptID = _Zinh;
    }
    // DeprecatedRegion
    if (RegionID r = normRegion(t.regionID); r != 0) {
        changed = true;
        t.regionID = r;
    }
    return {t, changed};
}

// --- parse — internal/language/parse.go -------------------------------------

constexpr int maxAltTaglen = 11; // len("en-US-POSIX")

// Internal Parse: scanner + parse, no canonicalization.
std::pair<Tag, ScanErr> parseTag_(std::string_view s) {
    Scanner scan(s);
    Tag t;
    int end = 0;
    if (size_t n = scan.token.size(); n <= 1) {
        scan.toLower(0, static_cast<int>(scan.b.size()));
        if (n == 0 || scan.token[0] != 'x') {
            return {t, ScanErr::syntax};
        }
        end = parseExtensions(scan);
    } else if (n >= 4) {
        return {Tag{}, ScanErr::syntax};
    } else { // the usual case
        std::tie(t, end) = parseTag(scan, true);
        if (scan.token.size() == 1) {
            t.pExt = static_cast<uint16_t>(end);
            end = parseExtensions(scan);
        } else if (end < static_cast<int>(scan.b.size())) {
            scan.setError(ScanErr::syntax);
            scan.b.resize(static_cast<size_t>(end));
        }
    }
    if (static_cast<int>(t.pVariant) < static_cast<int>(scan.b.size())) {
        std::string_view orig = s;
        if (end < static_cast<int>(orig.size())) {
            orig = orig.substr(0, end);
        }
        if (!orig.empty() && cmp(orig, scan.b) == 0) {
            t.str = std::string(orig);
        } else {
            t.str = scan.b;
        }
    } else {
        t.pVariant = 0;
        t.pExt = 0;
    }
    return {t, scan.err};
}

// Full internal Parse with grandfathered handling.
std::pair<Tag, ScanErr> internalParse(std::string_view s) {
    if (s.empty()) {
        return {Tag{}, ScanErr::syntax};
    }
    if (s.size() <= maxAltTaglen) {
        std::string key(s);
        for (auto& c : key) {
            if ('A' <= c && c <= 'Z') {
                c += 'a' - 'A';
            } else if (c == '_') {
                c = '-';
            }
        }
        for (const auto& e : kGrandfathered) {
            if (key == e.tag) {
                if (e.langID < 0) {
                    // language.Make(alt): full parse pipeline, error ignored.
                    auto [t, e2] = internalParse(e.alt);
                    auto [t2, changed] = canonicalizeDefault(t);
                    if (changed) {
                        t2.remakeString();
                    }
                    return {t2, ScanErr::none};
                }
                Tag t;
                t.langID = static_cast<LangID>(e.langID);
                return {t, ScanErr::none};
            }
        }
    }
    return parseTag_(s);
}

} // namespace

// Tag::String — internal/language/language.go
std::string Tag::String() const {
    if (!str.empty()) {
        return str;
    }
    if (scriptID == 0 && regionID == 0) {
        return langToString(langID);
    }
    return genCoreBytes(*this);
}

// Tag::remakeString — RemakeString.
void Tag::remakeString() {
    if (str.empty()) {
        return;
    }
    std::string extra = str.substr(pVariant);
    if (pVariant > 0) {
        extra = extra.substr(1);
    }
    Tag und;
    if (equalTags(und) && extra.rfind("x-", 0) == 0) {
        str = extra;
        pVariant = 0;
        pExt = 0;
        return;
    }
    std::string b = genCoreBytes(*this);
    if (!extra.empty()) {
        int diff = static_cast<int>(b.size()) - static_cast<int>(pVariant);
        b += '-';
        b += extra;
        pVariant = static_cast<uint8_t>(static_cast<int>(pVariant) + diff);
        pExt = static_cast<uint16_t>(static_cast<int>(pExt) + diff);
    } else {
        pVariant = static_cast<uint8_t>(b.size());
        pExt = static_cast<uint16_t>(b.size());
    }
    str = b;
}

} // namespace tsc::locale::detail

namespace tsc::locale {

std::string Locale::String() const {
    if (*this == Default) {
        return "";
    }
    return tag.String();
}

namespace {
// contextKey(0) — package-private context key identity.
constexpr char kLocaleContextKey = 0;
}

ContextPtr withLocale(const ContextPtr& ctx, Locale locale) {
    return withContextValue(ctx, &kLocaleContextKey, std::any(std::move(locale)));
}

Locale fromContext(const ContextPtr& ctx) {
    if (ctx) {
        if (const std::any* v = ctx->value(&kLocaleContextKey)) {
            if (const Locale* l = std::any_cast<Locale>(v)) {
                return *l;
            }
        }
    }
    return Default;
}

bool hasLocale(const ContextPtr& ctx) {
    if (!ctx) {
        return false;
    }
    const std::any* v = ctx->value(&kLocaleContextKey);
    return v != nullptr && std::any_cast<Locale>(v) != nullptr;
}

// Parse — outer language.Parse: internal parse + canonicalize(Default) +
// RemakeString. Parse gracefully fails: the returned Locale carries whatever
// could be parsed.
std::pair<Locale, bool> parse(std::string_view localeStr) {
    Locale l;
    if (localeStr.empty()) {
        return {l, false}; // ErrSyntax
    }
    // Internal parse (mirrors the Go recover: any internal exception →
    // Tag{}, ErrSyntax).
    detail::Tag t;
    detail::ScanErr err;
    try {
        auto [tt, ee] = detail::internalParse(localeStr);
        t = std::move(tt);
        err = ee;
    } catch (...) {
        l.tag = detail::Tag{};
        return {l, false};
    }
    l.tag = t;
    if (err != detail::ScanErr::none) {
        return {l, false};
    }
    auto [t2, changed] = detail::canonicalizeDefault(t);
    if (changed) {
        t2.remakeString();
    }
    l.tag = std::move(t2);
    return {l, true};
}

} // namespace tsc::locale
