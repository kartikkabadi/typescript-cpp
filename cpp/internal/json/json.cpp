// json — json.go:1-99
//
// jsontext substrate: Token/Value/Encoder/Decoder plus string/number
// formatting. Grammar validation mirrors jsontext's state machine (RFC 7493
// defaults: strict UTF-8 and unique object names unless relaxed by options).

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

#include "internal/json/json.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::json {

namespace {

bool isWS(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// jsontext kind classification of the first byte of a value.
Kind kindOf(char c) {
    switch (c) {
    case '{': case '}': case '[': case ']': case 'n': case 'f': case 't': case '"':
        return c;
    default:
        return '0'; // number
    }
}

// unescape one \uXXXX escape at raw[i+2..], handling surrogate pairs like Go.
// Returns the code point (or RuneError for lone surrogates) and consumed
// length measured from the '\\'.
char32_t decodeHexEscape(std::string_view raw, size_t i, size_t* len) {
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (i + 6 > raw.size() || raw[i + 1] != 'u') {
        return 0x110000; // invalid escape marker
    }
    char32_t r = 0;
    for (size_t j = i + 2; j < i + 6; j++) {
        int v = hexVal(raw[j]);
        if (v < 0) return 0x110000;
        r = (r << 4) | static_cast<char32_t>(v);
    }
    *len = 6;
    if (r >= 0xD800 && r <= 0xDBFF) {
        // high surrogate: look for \uXXXX low surrogate
        if (i + 12 <= raw.size() && raw[i + 6] == '\\' && raw[i + 7] == 'u') {
            char32_t lo = 0;
            for (size_t j = i + 8; j < i + 12; j++) {
                int v = hexVal(raw[j]);
                if (v < 0) {
                    return kRuneError;
                }
                lo = (lo << 4) | static_cast<char32_t>(v);
            }
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                *len = 12;
                return 0x10000 + ((r - 0xD800) << 10) + (lo - 0xDC00);
            }
        }
        return kRuneError;
    }
    if (r >= 0xDC00 && r <= 0xDFFF) {
        return kRuneError;
    }
    return r;
}

// utf8.Valid — strict RFC 3629 check.
bool validUtf8(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        int w;
        char32_t r = decodeUtf8RuneStrict(s.substr(i), &w);
        if (r == kRuneError && w <= 1) {
            return false;
        }
        i += static_cast<size_t>(w);
    }
    return true;
}

} // namespace

namespace detail {

Token tokenFromRaw(Kind k, std::string_view raw) {
    Token t;
    t.k = k;
    t.raw = std::string(raw);
    return t;
}

// jsontext string escaping: '"' '\\' short escapes \n \r \t \b? — jsontext
// uses \uXXXX for all <0x20 except \n \r \t? No: jsontext (v2) escapes
// '"' '\\' '\n' '\r' '\t' as short escapes and everything else <0x20 as
// \uXXXX, plus \u2028/\u2029 (JS safety). With AllowInvalidUTF8 (always set
// by this wrapper on marshal), invalid UTF-8 bytes pass through.
void appendQuotedString(std::string& out, std::string_view s) {
    out += '"';
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
            i++;
            continue;
        }
        if (c < 0x20) {
            switch (c) {
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            }
            i++;
            continue;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
            i++;
            continue;
        }
        // multibyte: emit the rune; escape U+2028/U+2029
        int w;
        char32_t r = decodeUtf8Rune(s.substr(i), &w);
        if (r == 0x2028) {
            out += "\\u2028";
        } else if (r == 0x2029) {
            out += "\\u2029";
        } else {
            out.append(s, i, static_cast<size_t>(w));
        }
        i += static_cast<size_t>(w);
    }
    out += '"';
}

// Go strconv.AppendFloat(f, 'f'|'e', -1, 64) selection: 'f' when
// 1e-6 <= |x| < 1e21, else 'e'. Shortest round-trip digits come from
// std::to_chars general; re-render in Go's notation.
std::string goFloat(double f) {
    if (std::isnan(f) || std::isinf(f)) {
        // encoding/json errors on non-finite; represent faithfully as the
        // error case via a sentinel that callers turn into an error.
        return "";
    }
    if (f == 0) {
        return std::signbit(f) ? "-0" : "0";
    }
    double abs = std::fabs(f);
    bool sci = abs < 1e-6 || abs >= 1e21;

    // shortest round-trip digits via to_chars general → split mantissa/exp
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), f, std::chars_format::general);
    std::string gen(buf, ptr - buf);

    // normalize: gen is like "-1.2345e-07" or "123.45" — decompose to digits+exp10
    bool neg = gen[0] == '-';
    std::string_view gv(gen);
    gv.remove_prefix(neg ? 1 : 0);
    size_t epos = gv.find('e');
    int exp10 = 0;
    std::string_view mant = gv;
    if (epos != std::string_view::npos) {
        mant = gv.substr(0, epos);
        exp10 = std::stoi(std::string(gv.substr(epos + 1)));
    }
    std::string digits;
    int dotPos = -1;
    for (char c : mant) {
        if (c == '.') {
            dotPos = static_cast<int>(digits.size());
        } else {
            digits += c;
        }
    }
    if (dotPos < 0) dotPos = static_cast<int>(digits.size());
    // strip leading zeros in digits adjusting dotPos
    int lz = 0;
    while (lz < static_cast<int>(digits.size()) - 1 && digits[lz] == '0') {
        lz++;
    }
    digits.erase(0, lz);
    dotPos -= lz;
    // effective decimal exponent of first significant digit
    int decExp = exp10 + dotPos - 1;

    std::string out;
    if (neg) out += '-';
    if (!sci) {
        // 'f' notation: digits with decimal point at decExp+1
        if (decExp >= 0) {
            int intDigits = decExp + 1;
            if (intDigits >= static_cast<int>(digits.size())) {
                out += digits;
                out.append(intDigits - static_cast<int>(digits.size()), '0');
            } else {
                out.append(digits, 0, intDigits);
                out += '.';
                out.append(digits, intDigits, std::string::npos);
            }
        } else {
            out += "0.";
            out.append(-decExp - 1, '0');
            out += digits;
        }
    } else {
        // 'e' notation: d.ddde±XX (2-digit minimum exponent)
        out += digits[0];
        if (digits.size() > 1) {
            out += '.';
            out.append(digits, 1, std::string::npos);
        }
        out += 'e';
        out += decExp < 0 ? '-' : '+';
        char ebuf[8];
        std::snprintf(ebuf, sizeof(ebuf), "%02d", std::abs(decExp));
        out += ebuf;
    }
    return out;
}

// unquote a '"' token's raw text into *out.
std::string unquote(std::string_view raw, std::string* out, bool allowInvalidUTF8) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
        return "json: invalid string token";
    }
    out->clear();
    std::string_view body = raw.substr(1, raw.size() - 2);
    size_t i = 0;
    while (i < body.size()) {
        char c = body[i];
        if (c == '\\' && i + 1 < body.size()) {
            char e = body[i + 1];
            switch (e) {
            case '"': out->push_back('"'); i += 2; continue;
            case '\\': out->push_back('\\'); i += 2; continue;
            case '/': out->push_back('/'); i += 2; continue;
            case 'b': out->push_back('\b'); i += 2; continue;
            case 'f': out->push_back('\f'); i += 2; continue;
            case 'n': out->push_back('\n'); i += 2; continue;
            case 'r': out->push_back('\r'); i += 2; continue;
            case 't': out->push_back('\t'); i += 2; continue;
            case 'u': {
                size_t len;
                char32_t r = decodeHexEscape(body, i, &len);
                if (r == 0x110000) {
                    return "json: invalid \\u escape";
                }
                char buf[4];
                int n = encodeUtf8Rune(r, buf);
                out->append(buf, n);
                i += len;
                continue;
            }
            default:
                return "json: invalid escape in string";
            }
        }
        out->push_back(c);
        i++;
    }
    if (!allowInvalidUTF8 && !validUtf8(*out)) {
        return "json: invalid UTF-8 in string";
    }
    return {};
}

} // namespace detail

// --- Token -------------------------------------------------------------------

std::string Token::string() const {
    if (k == '"') {
        std::string s;
        detail::unquote(raw, &s, true);
        return s;
    }
    return raw;
}

Token tokenString(std::string_view s) {
    std::string raw;
    detail::appendQuotedString(raw, s);
    return detail::tokenFromRaw('"', raw);
}
Token tokenBool(bool b) { return detail::tokenFromRaw(b ? 't' : 'f', b ? "true" : "false"); }
Token tokenInt(int64_t i) { return detail::tokenFromRaw('0', std::to_string(i)); }
Token tokenUint(uint64_t u) { return detail::tokenFromRaw('0', std::to_string(u)); }
Token tokenFloat(double f) { return detail::tokenFromRaw('0', detail::goFloat(f)); }

// --- options -------------------------------------------------------------------

Option allowDuplicateNames(bool allow) {
    return [allow](Options& o) { o.allowDuplicateNames = allow; };
}
Option deterministic(bool v) {
    return [v](Options& o) { o.deterministic = v; };
}
Option withIndent(std::string indent) {
    return [indent = std::move(indent)](Options& o) {
        o.multiline = true;
        o.indent = indent;
    };
}
Option withIndentPrefix(std::string prefix) {
    return [prefix = std::move(prefix)](Options& o) {
        o.multiline = true;
        o.indentPrefix = prefix;
    };
}

// --- Encoder -------------------------------------------------------------------

std::string Encoder::writeIndent(int depth) {
    if (!opts_.multiline) return {};
    std::string s = "\n" + opts_.indentPrefix;
    for (int i = 0; i < depth; i++) s += opts_.indent;
    return s;
}

std::string Encoder::writeToken(const Token& t) {
    if (stack_.empty()) {
        if (wroteTop_) {
            return "jsontext: cannot write token after top-level value";
        }
        if (t.k == '}' || t.k == ']') {
            return "jsontext: unexpected end token";
        }
        out_ << t.raw;
        if (t.k == '{' || t.k == '[') {
            stack_.push_back({t.k == '{', true, false});
        } else {
            wroteTop_ = true;
        }
        return {};
    }

    Ctx& c = stack_.back();

    // name position inside an object
    if (c.isObj && !c.afterName) {
        if (t.k == '}') {
            bool hadMembers = !c.first;
            stack_.pop_back();
            if (hadMembers) {
                out_ << writeIndent(static_cast<int>(stack_.size()));
            }
            out_ << "}";
            if (!stack_.empty()) {
                Ctx& p = stack_.back();
                p.afterName = false;
                p.first = false;
            } else {
                wroteTop_ = true;
            }
            return {};
        }
        if (t.k != '"') {
            return "jsontext: object member name must be a string";
        }
        if (!c.first) out_ << ',';
        if (opts_.multiline) out_ << writeIndent(static_cast<int>(stack_.size()));
        out_ << t.raw << (opts_.multiline ? ": " : ":");
        c.afterName = true;
        c.first = false;
        return {};
    }

    // value position
    if (t.k == '}' || t.k == ']') {
        if (c.isObj || t.k == '}') {
            return "jsontext: unexpected '" + std::string(1, t.k) + "' where a value was expected";
        }
        bool hadElems = !c.first;
        stack_.pop_back();
        if (hadElems) {
            out_ << writeIndent(static_cast<int>(stack_.size()));
        }
        out_ << "]";
        if (!stack_.empty()) {
            Ctx& p = stack_.back();
            p.afterName = false;
            p.first = false;
        } else {
            wroteTop_ = true;
        }
        return {};
    }

    if (!c.isObj) {
        if (!c.first) out_ << ',';
        if (opts_.multiline) out_ << writeIndent(static_cast<int>(stack_.size()));
        c.first = false;
    } else {
        c.afterName = false;
    }
    out_ << t.raw;
    if (t.k == '{' || t.k == '[') {
        stack_.push_back({t.k == '{', true, false});
    }
    return {};
}

std::string Encoder::writeValue(std::string_view v) {
    // Validate well-formedness like jsontext WriteValue and re-emit through
    // writeToken so separators and indentation are applied.
    Decoder d(v, opts_);
    for (;;) {
        auto [tok, err] = d.readToken();
        if (!err.empty()) return err;
        err = writeToken(tok);
        if (!err.empty()) return err;
        if (d.atEnd()) return {};
    }
}

// --- Decoder -------------------------------------------------------------------

void Decoder::skipWS() {
    while (pos_ < data_.size() && isWS(data_[pos_])) pos_++;
}

bool Decoder::atEnd() {
    skipWS();
    return pos_ == data_.size();
}

Kind Decoder::peekKind() const {
    size_t p = pos_;
    auto ws = [&] { while (p < data_.size() && isWS(data_[p])) p++; };
    ws();
    if (!stack_.empty()) {
        const Ctx& c = stack_.back();
        if (c.state == 1 && p < data_.size() && data_[p] == ',') {
            p++;
            ws();
        } else if (c.isObj && c.state == 3 && p < data_.size() && data_[p] == ':') {
            p++;
            ws();
        }
    }
    if (p >= data_.size()) return 0;
    return kindOf(data_[p]);
}

// Consume structural separators per grammar state; sets *isName when the next
// token is an object member name.
std::string Decoder::beforeElement(bool* isName) {
    *isName = false;
    if (stack_.empty()) {
        return {};
    }
    Ctx& c = stack_.back();
    if (c.isObj) {
        switch (c.state) {
        case 0: // first member: '"' or '}'
            *isName = true;
            return {};
        case 1: { // ',' or '}'
            skipWS();
            if (pos_ < data_.size() && data_[pos_] == '}') {
                *isName = true;
                return {};
            }
            if (pos_ >= data_.size() || data_[pos_] != ',') {
                return "jsontext: expected ',' or '}' in object";
            }
            pos_++;
            c.state = 2;
            *isName = true;
            return {};
        }
        case 2: // after ',' — must be '"' name
            *isName = true;
            return {};
        case 3: // after name: expect ':' then a value
            skipWS();
            if (pos_ >= data_.size() || data_[pos_] != ':') {
                return "jsontext: expected ':' after object member name";
            }
            pos_++;
            return {};
        }
    } else {
        switch (c.state) {
        case 0:
            return {};
        case 1: {
            skipWS();
            if (pos_ < data_.size() && data_[pos_] == ']') {
                return {};
            }
            if (pos_ >= data_.size() || data_[pos_] != ',') {
                return "jsontext: expected ',' or ']' in array";
            }
            pos_++;
            c.state = 2;
            return {};
        }
        case 2:
            return {};
        }
    }
    return {};
}

std::pair<Token, std::string> Decoder::readToken() {
    bool isName;
    std::string err = beforeElement(&isName);
    if (!err.empty()) return {{}, err};
    return readTokenRaw(isName);
}

// readTokenRaw reads the token at pos_ after separators were consumed.
std::pair<Token, std::string> Decoder::readTokenRaw(bool isName) {
    skipWS();
    if (pos_ >= data_.size()) {
        return {{}, "jsontext: unexpected end of input"};
    }
    char c = data_[pos_];
    Token t;

    auto endOfElement = [&]() {
        if (!stack_.empty()) {
            Ctx& top = stack_.back();
            if (top.isObj) {
                top.state = (top.state == 3) ? 1 : 3;
            } else {
                top.state = 1;
            }
        }
    };

    if (isName && c != '"' && c != '}') {
        return {{}, "jsontext: object member name must be a string"};
    }

    switch (c) {
    case '{': case '[': {
        t.k = c;
        t.raw = std::string(1, c);
        pos_++;
        stack_.push_back({c == '{', 0, {}});
        return {t, {}};
    }
    case '}': case ']': {
        if (stack_.empty()) {
            return {{}, "jsontext: unexpected '" + std::string(1, c) + "'"};
        }
        Ctx& top = stack_.back();
        bool wantClose = c == '}' ? top.isObj : !top.isObj;
        if (!wantClose) {
            return {{}, "jsontext: mismatched end delimiter '" + std::string(1, c) + "'"};
        }
        if (top.state == 2) {
            return {{}, "jsontext: trailing ',' not allowed"};
        }
        if (top.state == 3) {
            return {{}, "jsontext: unexpected '" + std::string(1, c) + "' after member name"};
        }
        t.k = c;
        t.raw = std::string(1, c);
        pos_++;
        stack_.pop_back();
        endOfElement();
        return {t, {}};
    }
    default:
        break;
    }

    size_t start = pos_;
    if (c == '"') {
        // scan the string token
        size_t i = pos_ + 1;
        for (;;) {
            if (i >= data_.size()) {
                return {{}, "jsontext: unterminated string"};
            }
            char ch = data_[i];
            if (ch == '"') {
                i++;
                break;
            }
            if (ch == '\\') {
                if (i + 1 >= data_.size()) {
                    return {{}, "jsontext: unterminated string"};
                }
                i += 2;
                continue;
            }
            i++;
        }
        t.k = '"';
        t.raw = std::string(data_.substr(start, i - start));
        pos_ = i;
        {
            // Escape syntax is validated at scan time regardless of
            // AllowInvalidUTF8 (which only relaxes the UTF-8 check inside
            // unquote).
            std::string dummy;
            if (!detail::unquote(t.raw, &dummy, opts_.allowInvalidUTF8)
                     .empty()) {
                return {{}, "jsontext: invalid UTF-8 or escape in string"};
            }
        }
        if (isName) {
            // duplicate-name check (RFC 7493 default)
            std::string name = t.string();
            Ctx& top = stack_.back();
            if (!opts_.allowDuplicateNames) {
                if (std::find(top.names.begin(), top.names.end(), name) != top.names.end()) {
                    return {{}, "jsontext: duplicate object member name"};
                }
                top.names.push_back(name);
            }
            top.state = 3;
        } else {
            endOfElement();
        }
        return {t, {}};
    }

    // literal or number: read a token span then validate
    {
        size_t i = pos_;
        while (i < data_.size() && !isWS(data_[i]) && data_[i] != ',' &&
               data_[i] != '}' && data_[i] != ']') {
            i++;
        }
        std::string_view word = data_.substr(start, i - start);
        pos_ = i;
        if (word == "null") {
            t.k = 'n';
        } else if (word == "true") {
            t.k = 't';
        } else if (word == "false") {
            t.k = 'f';
        } else {
            // validate JSON number grammar: -?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?
            auto badNum = [&]() -> std::pair<Token, std::string> {
                return {{}, "jsontext: invalid number " + std::string(word)};
            };
            size_t j = 0;
            if (j < word.size() && word[j] == '-') j++;
            if (j >= word.size()) return badNum();
            if (word[j] == '0') {
                j++;
            } else if (isDigit(word[j])) {
                while (j < word.size() && isDigit(word[j])) j++;
            } else {
                return badNum();
            }
            if (j < word.size() && word[j] == '.') {
                j++;
                if (j >= word.size() || !isDigit(word[j])) return badNum();
                while (j < word.size() && isDigit(word[j])) j++;
            }
            if (j < word.size() && (word[j] == 'e' || word[j] == 'E')) {
                j++;
                if (j < word.size() && (word[j] == '+' || word[j] == '-')) j++;
                if (j >= word.size() || !isDigit(word[j])) return badNum();
                while (j < word.size() && isDigit(word[j])) j++;
            }
            if (j != word.size()) return badNum();
            t.k = '0';
        }
        t.raw = std::string(word);
        endOfElement();
        return {t, {}};
    }
}

// scanValueEnd finds the end offset of the complete value starting at `start`
// (assumes beforeElement already ran and pos_ == start, or raw scan). Used to
// implement ReadValue/SkipValue over nested content with full validation.
std::pair<Value, std::string> Decoder::readValue() {
    bool isName;
    std::string err = beforeElement(&isName);
    if (!err.empty()) return {"", err};
    skipWS();
    if (pos_ >= data_.size()) {
        return {"", "jsontext: unexpected end of input"};
    }
    if (isName && data_[pos_] != '"') {
        return {"", "jsontext: object member name must be a string"};
    }
    size_t start = pos_;
    char c = data_[pos_];
    if (c == '{' || c == '[') {
        // consume the nested value token-by-token on this decoder's own
        // state machine; the first token's beforeElement ran above, so read
        // it through a private raw-reader path.
        auto [t0, e0] = readTokenRaw(isName);
        if (!e0.empty()) return {"", e0};
        int depth = 1;
        while (depth > 0) {
            auto [t, e] = readToken();
            if (!e.empty()) return {"", e};
            if (t.k == '{' || t.k == '[') depth++;
            if (t.k == '}' || t.k == ']') depth--;
        }
        return {std::string(data_.substr(start, pos_ - start)), {}};
    }
    auto [t, e2] = readTokenRaw(isName);
    if (!e2.empty()) return {"", e2};
    (void)t;
    return {std::string(data_.substr(start, pos_ - start)), {}};
}

std::string Decoder::skipValue() {
    auto [v, err] = readValue();
    (void)v;
    return err;
}

} // namespace tsc::json
