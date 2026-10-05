#pragma once

// json — json.go:1-99
//
// Thin wrapper over the JSON substrate (Go's encoding/json/v2 + jsontext).
// The C++ side implements the jsontext-level Token/Value/Encoder/Decoder
// streaming API plus a reflection-free marshal/unmarshal dispatch:
// Go's `MarshalerTo` maps to a member `marshalJSONTo(Encoder&) -> err`
// (or ADL `marshalJSONTo(enc, v)`), and `UnmarshalerFrom` maps to a member
// `unmarshalJSONFrom(Decoder&) -> err` (or ADL `unmarshalJSONFrom(dec, &v)`).
//
// Option-modeling note: Go `json.Options` values are composable flags; they
// are ported as Option = a function applied to an Options record, preserving
// jsontext's apply-order semantics (later options override earlier ones).

#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tsc::json {

// jsontext.Kind — the kind of the next token/value, identified by its first
// byte: '{', '}', '[', ']', 'n' (null), 'f' (false), 't' (true), '"' (string),
// '0' (number).
using Kind = char;

// jsontext.Value — the raw textual representation of a complete JSON value.
using Value = std::string;

// jsontext.Token.
struct Token {
    Kind k = 0;
    std::string raw; // literal/string/number raw text (strings include quotes)

    Kind kind() const { return k; }
    // Token.String: for '"' the unquoted content; otherwise the raw text.
    std::string string() const;
};

// jsontext token constructors.
namespace detail {
Token tokenFromRaw(Kind k, std::string_view raw);
}
Token tokenString(std::string_view s); // jsontext.String
Token tokenBool(bool b);               // jsontext.Bool
Token tokenInt(int64_t i);             // jsontext.Int
Token tokenUint(uint64_t u);           // jsontext.Uint
Token tokenFloat(double f);            // jsontext.Float

// Re-exported singleton tokens (json.go:93-99).
inline const Token Null = detail::tokenFromRaw('n', "null");
inline const Token BeginObject = detail::tokenFromRaw('{', "{");
inline const Token EndObject = detail::tokenFromRaw('}', "}");
inline const Token BeginArray = detail::tokenFromRaw('[', "[");
inline const Token EndArray = detail::tokenFromRaw(']', "]");

// --- options ---------------------------------------------------------------

struct Options {
    bool allowInvalidUTF8 = false;
    bool allowDuplicateNames = false;
    bool deterministic = false;
    bool multiline = false;
    std::string indent;
    std::string indentPrefix;
};

using Option = std::function<void(Options&)>;

// jsontext.AllowDuplicateNames
Option allowDuplicateNames(bool allow);
// json.Deterministic
Option deterministic(bool v);
// jsontext.WithIndent (implies multiline)
Option withIndent(std::string indent);
// jsontext.WithIndentPrefix (implies multiline)
Option withIndentPrefix(std::string prefix);

// --- Encoder / Decoder -----------------------------------------------------

// jsontext.Encoder — validates JSON grammar over a token/value stream and
// writes to an ostream. Errors are returned as non-empty strings (Go error).
class Encoder {
public:
    explicit Encoder(std::ostream& out, const Options& opts = {}) : out_(out), opts_(opts) {}

    // WriteToken writes the next token (name tokens inside objects included).
    std::string writeToken(const Token& t);
    // WriteValue writes the next complete value verbatim.
    std::string writeValue(std::string_view v);

    const Options& options() const { return opts_; }
    void setOptions(const Options& o) { opts_ = o; }

private:
    std::ostream& out_;
    Options opts_;
    struct Ctx { bool isObj; bool first = true; bool afterName = false; };
    std::vector<Ctx> stack_;
    bool wroteTop_ = false;

    std::string writeIndent(int depth);
};

// jsontext.Decoder — validates JSON grammar while streaming tokens/values
// from an in-memory buffer (jsontext buffers internally anyway; NewDecoder
// reads the io.Reader fully, like a buffered decode).
class Decoder {
public:
    explicit Decoder(std::string_view data, const Options& opts = {}) : data_(data), opts_(opts) {}

    // PeekKind reports the kind of the next token without consuming it
    // (or the member-name '"' inside objects).
    Kind peekKind() const;
    // ReadToken reads the next token.
    std::pair<Token, std::string> readToken();
    // ReadValue reads the next complete value (possibly nested).
    std::pair<Value, std::string> readValue();
    // SkipValue reads and discards the next complete value.
    std::string skipValue();
    // atEnd skips trailing whitespace and reports whether the buffer is done.
    bool atEnd();

    const Options& options() const { return opts_; }
    void setOptions(const Options& o) { opts_ = o; }

private:
    std::string_view data_;
    Options opts_;
    size_t pos_ = 0;
    struct Ctx {
        bool isObj;
        // 0: first-name/value-or-end, 1: ','-or-end, 2: name-after-comma,
        // 3: ':'+value (obj only)
        int state = 0;
        std::vector<std::string> names; // for duplicate-name rejection
    };
    std::vector<Ctx> stack_;

    void skipWS();
    std::string beforeElement(bool* isName); // consume separators/grammar
    std::pair<Token, std::string> readTokenRaw(bool isName); // token read after beforeElement ran
};

// --- marshal ---------------------------------------------------------------

// Marshal — json.go:13. Always applies AllowInvalidUTF8(true) first.
template <class T>
std::pair<std::string, std::string> marshal(const T& in, std::initializer_list<Option> opts = {});

// MarshalEncode — json.go:22.
template <class T>
std::string marshalEncode(Encoder& out, const T& in, std::initializer_list<Option> opts = {});

// MarshalWrite — json.go:31.
template <class T>
std::string marshalWrite(std::ostream& out, const T& in, std::initializer_list<Option> opts = {});

// MarshalIndent — json.go:40.
template <class T>
std::pair<std::string, std::string> marshalIndent(const T& in, std::string_view prefix, std::string_view indent);

// MarshalIndentWrite — json.go:48.
template <class T>
std::string marshalIndentWrite(std::ostream& out, const T& in, std::string_view prefix, std::string_view indent);

// --- unmarshal -------------------------------------------------------------

// Unmarshal — json.go:56. (No implicit AllowInvalidUTF8: strict UTF-8 and
// unique object names per RFC 7493 unless overridden.)
template <class T>
std::string unmarshal(std::string_view in, T* out, std::initializer_list<Option> opts = {});

// UnmarshalDecode — json.go:60. Calls out's unmarshalJSONFrom on the live
// decoder when implemented (Go's UnmarshalerFrom), else decodes the next
// complete value.
template <class T>
std::string unmarshalDecode(Decoder& in, T* out, std::initializer_list<Option> opts = {});

// UnmarshalRead — json.go:64.
template <class T>
std::string unmarshalRead(std::istream& in, T* out, std::initializer_list<Option> opts = {});

// NewDecoder — json.go:80. Reads r fully and buffers.
inline Decoder newDecoderFrom(std::string_view data) { return Decoder(data); }

// --- per-type marshaling (dispatch substrate) -------------------------------
// These are declared here and defined in the .cpp for the value-level
// encoding of strings/numbers; template dispatch lives in this header so it
// can see arbitrary caller types.

namespace detail {

// Write a JSON-escaped string literal (jsontext escaping: '"' '\\' and
// <0x20 as \uXXXX except \n \r \t shorthands; U+2028/U+2029 escaped;
// invalid UTF-8 preserved because the wrapper always sets AllowInvalidUTF8).
void appendQuotedString(std::string& out, std::string_view s);
// Go encoding/json/v2 float formatting: strconv shortest 'f' for
// 1e-6 <= |x| < 1e21, else 'e' with signed >=2-digit exponent.
std::string goFloat(double f);

// Unmarshal helpers operating on a raw Value.
std::string unquote(std::string_view raw, std::string* out, bool allowInvalidUTF8);

} // namespace detail

// MarshalerTo / UnmarshalerFrom (json.go:87-88) — Go interfaces mapped to
// C++ concepts; any type with marshalJSONTo(Encoder&) /
// unmarshalJSONFrom(Decoder&) (member or ADL) satisfies them.

template <class T>
concept MarshalerTo = requires(const T& v, Encoder& e) {
    { v.marshalJSONTo(e) } -> std::convertible_to<std::string>;
} || requires(const T& v, Encoder& e) {
    { marshalJSONTo(e, v) } -> std::convertible_to<std::string>;
};

template <class T>
concept UnmarshalerFrom = requires(T& v, Decoder& d) {
    { v.unmarshalJSONFrom(d) } -> std::convertible_to<std::string>;
} || requires(T& v, Decoder& d) {
    { unmarshalJSONFrom(d, &v) } -> std::convertible_to<std::string>;
};

} // namespace tsc::json

// Template definitions are in json_inl.h so this header stays readable.
#include "internal/json/json_inl.h"
