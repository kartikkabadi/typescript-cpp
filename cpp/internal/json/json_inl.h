#pragma once

// json — json.go template dispatch (included from json.h).
//
// Go's `in any` marshaling is reflection-based; the C++ port dispatches
// statically: MarshalerTo/UnmarshalerFrom interfaces map to
// marshalJSONTo(Encoder&)/unmarshalJSONFrom(Decoder&) members (or ADL
// functions), then built-in kinds, containers, optionals, pointers, variants.

#include <charconv>
#include <cstring>
#include <map>
#include <sstream>
#include <type_traits>
#include <variant>
#include <unordered_map>

namespace tsc::json {

namespace detail {

// --- trait helpers ----------------------------------------------------------

template <class T>
concept HasMarshalJSONTo = requires(const T& v, Encoder& e) {
    { v.marshalJSONTo(e) } -> std::convertible_to<std::string>;
};

template <class T>
concept HasADLMarshalJSONTo = requires(const T& v, Encoder& e) {
    { marshalJSONTo(e, v) } -> std::convertible_to<std::string>;
};

template <class T>
concept HasUnmarshalJSONFrom = requires(T& v, Decoder& d) {
    { v.unmarshalJSONFrom(d) } -> std::convertible_to<std::string>;
};

template <class T>
concept HasADLUnmarshalJSONFrom = requires(T& v, Decoder& d) {
    { unmarshalJSONFrom(d, &v) } -> std::convertible_to<std::string>;
};

template <class T>
struct isOptional : std::false_type {};
template <class T>
struct isOptional<std::optional<T>> : std::true_type {};

template <class T>
struct isSharedOrUniquePtr : std::false_type {};
template <class T, class D>
struct isSharedOrUniquePtr<std::unique_ptr<T, D>> : std::true_type {};
template <class T>
struct isSharedOrUniquePtr<std::shared_ptr<T>> : std::true_type {};
template <class T>
struct isSharedOrUniquePtr<std::weak_ptr<T>> : std::true_type {};

// collections::OrderedMap has `keys` + `mp` members.
template <class T>
concept IsOrderedMap = requires(const T& t) {
    t.keys;
    t.mp;
};

template <class T>
concept IsStdMap = requires {
    typename T::key_type;
    typename T::mapped_type;
} && (std::is_same_v<T, std::map<typename T::key_type, typename T::mapped_type>> ||
      std::is_same_v<T, std::unordered_map<typename T::key_type, typename T::mapped_type>>);

template <class T>
concept IsMapLike = IsOrderedMap<T> || IsStdMap<T>;

template <class T>
concept IsVector = requires {
    typename T::value_type;
    requires std::is_same_v<T, std::vector<typename T::value_type>>;
};

template <class T>
struct isVariant : std::false_type {};
template <class... Ts>
struct isVariant<std::variant<Ts...>> : std::true_type {};

template <class T>
concept IsIterable = requires(T& t) { std::begin(t); std::end(t); } &&
                     !std::is_convertible_v<T, std::string_view> &&
                     !IsMapLike<T> && !isVariant<T>::value;

// Map key → JSON object member name. Go marshals integer-keyed maps as
// strings; string keys pass through.
template <class K>
std::string mapKeyToString(const K& k) {
    if constexpr (std::is_convertible_v<K, std::string_view>) {
        return std::string(std::string_view(k));
    } else if constexpr (std::is_convertible_v<K, const std::string&>) {
        // === slice: api === — wrappers like project::ID convert via
        // `operator const std::string&`, not string_view.
        return std::string(k);
    } else {
        return std::to_string(k);
    }
}

// --- marshal dispatch ---------------------------------------------------------

// encode the next value on a live Encoder (Go's marshal into the stream).
template <class T>
std::string marshalInto(Encoder& enc, const T& v) {
    using U = std::decay_t<T>;
    if constexpr (HasMarshalJSONTo<U>) {
        return v.marshalJSONTo(enc);
    } else if constexpr (HasADLMarshalJSONTo<U>) {
        return marshalJSONTo(enc, v);
    } else if constexpr (std::is_same_v<U, Token>) {
        return enc.writeToken(v);
    } else if constexpr (std::is_same_v<U, Value>) {
        return enc.writeValue(v);
    } else if constexpr (std::is_same_v<U, bool>) {
        return enc.writeToken(tokenBool(v));
    } else if constexpr (std::is_same_v<U, std::monostate> ||
                         std::is_same_v<U, std::nullptr_t>) {
        return enc.writeToken(Null);
    } else if constexpr (std::is_enum_v<U>) {
        using B = std::underlying_type_t<U>;
        if constexpr (std::is_signed_v<B>) {
            return enc.writeToken(tokenInt(static_cast<int64_t>(v)));
        } else {
            return enc.writeToken(tokenUint(static_cast<uint64_t>(v)));
        }
    } else if constexpr (std::is_integral_v<U>) {
        if constexpr (std::is_signed_v<U>) {
            return enc.writeToken(tokenInt(static_cast<int64_t>(v)));
        } else {
            return enc.writeToken(tokenUint(static_cast<uint64_t>(v)));
        }
    } else if constexpr (std::is_floating_point_v<U>) {
        return enc.writeToken(tokenFloat(static_cast<double>(v)));
    } else if constexpr (std::is_convertible_v<U, std::string_view>) {
        return enc.writeToken(tokenString(std::string_view(v)));
    } else if constexpr (isOptional<U>::value) {
        if (!v.has_value()) {
            // encoding/json/v2 marshals a nil Go slice as [] and a nil Go
            // map as {} (v1's null needs FormatNilSliceAsNull/MapAsNull).
            if constexpr (IsVector<typename U::value_type>) {
                if (auto err = enc.writeToken(BeginArray); !err.empty()) return err;
                return enc.writeToken(EndArray);
            } else if constexpr (IsMapLike<typename U::value_type>) {
                if (auto err = enc.writeToken(BeginObject); !err.empty()) return err;
                return enc.writeToken(EndObject);
            } else {
                return enc.writeToken(Null);
            }
        }
        return marshalInto(enc, *v);
    } else if constexpr (isSharedOrUniquePtr<U>::value || std::is_pointer_v<U>) {
        if (!v) {
            return enc.writeToken(Null);
        }
        return marshalInto(enc, *v);
    } else if constexpr (std::is_bounded_array_v<U>) {
        std::string err = enc.writeToken(BeginArray);
        if (!err.empty()) return err;
        for (const auto& e : v) {
            if ((err = marshalInto(enc, e)), !err.empty()) return err;
        }
        return enc.writeToken(EndArray);
    } else if constexpr (isVariant<U>::value) {
        return std::visit([&](const auto& alt) { return marshalInto(enc, alt); }, v);
    } else if constexpr (IsOrderedMap<U>) {
        std::string err = enc.writeToken(BeginObject);
        if (!err.empty()) return err;
        for (const auto& k : v.keys) {
            if ((err = enc.writeToken(tokenString(mapKeyToString(k)))), !err.empty()) return err;
            if ((err = marshalInto(enc, v.mp.at(k))), !err.empty()) return err;
        }
        return enc.writeToken(EndObject);
    } else if constexpr (IsStdMap<U>) {
        std::string err = enc.writeToken(BeginObject);
        if (!err.empty()) return err;
        // Go maps marshal in random order; Deterministic sorts. std::map is
        // already sorted; unordered_map sorts under Deterministic.
        if constexpr (std::is_same_v<U, std::unordered_map<typename U::key_type, typename U::mapped_type>>) {
            if (enc.options().deterministic) {
                std::vector<typename U::key_type> keys;
                keys.reserve(v.size());
                for (const auto& [k, _] : v) keys.push_back(k);
                std::sort(keys.begin(), keys.end());
                for (const auto& k : keys) {
                    if ((err = enc.writeToken(tokenString(mapKeyToString(k)))), !err.empty()) return err;
                    if ((err = marshalInto(enc, v.at(k))), !err.empty()) return err;
                }
                return enc.writeToken(EndObject);
            }
        }
        for (const auto& [k, val] : v) {
            if ((err = enc.writeToken(tokenString(mapKeyToString(k)))), !err.empty()) return err;
            if ((err = marshalInto(enc, val)), !err.empty()) return err;
        }
        return enc.writeToken(EndObject);
    } else if constexpr (IsIterable<U>) {
        std::string err = enc.writeToken(BeginArray);
        if (!err.empty()) return err;
        for (const auto& e : v) {
            if ((err = marshalInto(enc, e)), !err.empty()) return err;
        }
        return enc.writeToken(EndArray);
    } else {
        static_assert(sizeof(U) == 0,
            "json.Marshal: unsupported type (needs marshalJSONTo(Encoder&) or a known JSON kind)");
    }
}

// --- unmarshal dispatch -------------------------------------------------------

template <class T>
std::string unmarshalDecodeImpl(Decoder& dec, T* out);

// decode elements of a JSON array (the '[' is already consumed).
template <class T>
struct isStdArray : std::false_type {};
template <class T, size_t N>
struct isStdArray<std::array<T, N>> : std::true_type {};

// decode elements of a JSON array (the '[' is already consumed).
template <class T>
std::string unmarshalArray(Decoder& dec, T* out) {
    using U = std::decay_t<T>;
    if constexpr (std::is_bounded_array_v<U> || isStdArray<U>::value) {
        constexpr size_t N = std::is_bounded_array_v<U> ? std::extent_v<U> : std::tuple_size_v<U>;
        size_t i = 0;
        while (dec.peekKind() != ']') {
            if (i >= N) {
                // Go drains the tail elements even when the destination is full.
                if (auto err = dec.skipValue(); !err.empty()) return err;
                continue;
            }
            std::string err = unmarshalDecodeImpl(dec, &(*out)[i]);
            if (!err.empty()) return err;
            i++;
        }
        auto [t, err] = dec.readToken();
        return err;
    } else {
        out->clear();
        while (dec.peekKind() != ']') {
            typename U::value_type elem{};
            std::string err = unmarshalDecodeImpl(dec, &elem);
            if (!err.empty()) return err;
            out->push_back(std::move(elem));
        }
        auto [t, err] = dec.readToken();
        return err;
    }
}

template <class T>
std::string unmarshalObjectInto(Decoder& dec, T* out) {
    using U = std::decay_t<T>;
    while (dec.peekKind() != '}') {
        auto [nameTok, err] = dec.readToken(); // '"' name token
        if (!err.empty()) return err;
        std::string name;
        name = nameTok.string();
        (void)err;
        if constexpr (IsOrderedMap<U>) {
            typename decltype(std::declval<U>().mp)::mapped_type val{};
            err = unmarshalDecodeImpl(dec, &val);
            if (!err.empty()) return err;
            out->Set(name, std::move(val));
        } else {
            typename U::mapped_type val{};
            err = unmarshalDecodeImpl(dec, &val);
            if (!err.empty()) return err;
            if constexpr (std::is_convertible_v<typename U::key_type, std::string_view>) {
                (*out)[name] = std::move(val);
            } else {
                (*out)[static_cast<typename U::key_type>(std::stoll(name))] = std::move(val);
            }
        }
    }
    auto [t, err] = dec.readToken();
    return err;
}

// parse a JSON number token raw text into an integer or floating point.
template <class T>
std::string unmarshalNumber(std::string_view raw, T* out) {
    using U = std::decay_t<T>;
    if constexpr (std::is_floating_point_v<U>) {
        std::string s(raw);
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (end != s.c_str() + s.size()) {
            return "json: cannot unmarshal number into Go value of type float64";
        }
        *out = static_cast<U>(d);
        return {};
    } else {
        if constexpr (std::is_enum_v<U>) {
            std::underlying_type_t<U> val{};
            auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), val);
            if (ec == std::errc::result_out_of_range) {
                return "json: cannot unmarshal number " + std::string(raw) + " into Go value (overflow)";
            }
            if (ec != std::errc() || ptr != raw.data() + raw.size()) {
                return "json: cannot unmarshal number into Go value of integer type";
            }
            *out = static_cast<U>(val);
            return {};
        } else {
            U val{};
            auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), val);
            if (ec == std::errc::result_out_of_range) {
                return "json: cannot unmarshal number " + std::string(raw) + " into Go value (overflow)";
            }
            if (ec != std::errc() || ptr != raw.data() + raw.size()) {
                return "json: cannot unmarshal number into Go value of integer type";
            }
            *out = val;
            return {};
        }
    }
}

template <class T>
std::string unmarshalDecodeImpl(Decoder& dec, T* out) {
    using U = std::decay_t<T>;
    if constexpr (HasUnmarshalJSONFrom<U>) {
        return out->unmarshalJSONFrom(dec);
    } else if constexpr (HasADLUnmarshalJSONFrom<U>) {
        return unmarshalJSONFrom(dec, out);
    } else if constexpr (std::is_same_v<U, Value>) {
        auto [v, err] = dec.readValue();
        if (!err.empty()) return err;
        *out = v;
        return {};
    } else {
        Kind k = dec.peekKind();
        if (k == 'n') {
            // Unmarshaling JSON null into a non-pointer Go value is a no-op;
            // for optional/pointer targets it clears them.
            auto [t, err] = dec.readToken();
            if (!err.empty()) return err;
            if constexpr (isOptional<U>::value) {
                out->reset();
            } else if constexpr (isSharedOrUniquePtr<U>::value) {
                out->reset();
            } else if constexpr (std::is_pointer_v<U>) {
                *out = nullptr;
            }
            return {};
        }
        if constexpr (std::is_same_v<U, bool>) {
            if (k != 't' && k != 'f') {
                return "json: cannot unmarshal non-bool into Go value of type bool";
            }
            auto [t, err] = dec.readToken();
            if (!err.empty()) return err;
            *out = (t.k == 't');
            return {};
        } else if constexpr (std::is_convertible_v<U, std::string_view> && !std::is_same_v<U, Value>) {
            if (k != '"') {
                return "json: cannot unmarshal non-string into Go value of type string";
            }
            auto [t, err] = dec.readToken();
            if (!err.empty()) return err;
            return detail::unquote(t.raw, reinterpret_cast<std::string*>(out), false);
        } else if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U>) {
            if (k != '0') {
                return "json: cannot unmarshal non-number into Go numeric value";
            }
            auto [t, err] = dec.readToken();
            if (!err.empty()) return err;
            return unmarshalNumber(t.raw, out);
        } else if constexpr (std::is_same_v<U, Token>) {
            auto [t, err] = dec.readToken();
            if (!err.empty()) return err;
            *out = t;
            return {};
        } else if constexpr (isOptional<U>::value || isSharedOrUniquePtr<U>::value) {
            if constexpr (isOptional<U>::value) {
                typename U::value_type inner{};
                std::string err = unmarshalDecodeImpl(dec, &inner);
                if (!err.empty()) return err;
                out->emplace(std::move(inner));
                return {};
            } else {
                typename U::element_type inner{};
                std::string err = unmarshalDecodeImpl(dec, &inner);
                if (!err.empty()) return err;
                *out = std::make_shared<typename U::element_type>(std::move(inner));
                return {};
            }
        } else if constexpr (std::is_pointer_v<U>) {
            std::remove_pointer_t<U> inner{};
            std::string err = unmarshalDecodeImpl(dec, &inner);
            if (!err.empty()) return err;
            *out = new std::remove_pointer_t<U>(std::move(inner));
            return {};
        } else if constexpr (isVariant<U>::value) {
            return "json: cannot unmarshal into variant without unmarshalJSONFrom";
        } else if constexpr (IsMapLike<U>) {
            if (k != '{') {
                return "json: cannot unmarshal non-object into Go map value";
            }
            auto [t, err] = dec.readToken();
            if (!err.empty()) return err;
            return unmarshalObjectInto(dec, out);
        } else if constexpr (IsIterable<U> || std::is_bounded_array_v<U> || isStdArray<U>::value) {
            if (k != '[') {
                return "json: cannot unmarshal non-array into Go array value";
            }
            auto [t, err] = dec.readToken();
            if (!err.empty()) return err;
            return unmarshalArray(dec, out);
        } else {
            static_assert(sizeof(U) == 0,
                "json.Unmarshal: unsupported type (needs unmarshalJSONFrom(Decoder&) or a known JSON kind)");
        }
    }
}

// allowInvalidUTF8 apply + merge like json.go's slices.Clip/append.
inline Options optionsFor(std::initializer_list<Option> opts, bool withAllowInvalid) {
    Options o;
    if (withAllowInvalid) {
        o.allowInvalidUTF8 = true; // allowInvalid is always prepended
    }
    for (const auto& opt : opts) {
        opt(o);
    }
    return o;
}

} // namespace detail

// --- public API ------------------------------------------------------------

template <class T>
std::pair<std::string, std::string> marshal(const T& in, std::initializer_list<Option> opts) {
    std::ostringstream ss;
    std::string err = marshalWrite(ss, in, opts);
    if (!err.empty()) {
        return {"", err};
    }
    return {ss.str(), {}};
}

template <class T>
std::string marshalEncode(Encoder& out, const T& in, std::initializer_list<Option> opts) {
    // jsontext options apply at Encoder construction; per-call options in Go
    // adjust the encoder's behavior for this value — apply to the live
    // encoder's copy.
    Options merged = out.options();
    for (const auto& opt : opts) {
        opt(merged);
    }
    Options saved = out.options();
    merged.allowInvalidUTF8 = true; // allowInvalid is always applied
    out.setOptions(merged);
    std::string err = detail::marshalInto(out, in);
    out.setOptions(saved);
    return err;
}

template <class T>
std::string marshalWrite(std::ostream& out, const T& in, std::initializer_list<Option> opts) {
    Options o = detail::optionsFor(opts, /*withAllowInvalid=*/true);
    Encoder enc(out, o);
    return detail::marshalInto(enc, in);
}

template <class T>
std::pair<std::string, std::string> marshalIndent(const T& in, std::string_view prefix, std::string_view indent) {
    if (prefix.empty() && indent.empty()) {
        // WithIndentPrefix and WithIndent imply multiline output, so skip them.
        return marshal(in);
    }
    return marshal(in, {withIndentPrefix(std::string(prefix)), withIndent(std::string(indent))});
}

template <class T>
std::string marshalIndentWrite(std::ostream& out, const T& in, std::string_view prefix, std::string_view indent) {
    if (prefix.empty() && indent.empty()) {
        return marshalWrite(out, in);
    }
    return marshalWrite(out, in, {withIndentPrefix(std::string(prefix)), withIndent(std::string(indent))});
}

template <class T>
std::string unmarshal(std::string_view in, T* out, std::initializer_list<Option> opts) {
    Options o = detail::optionsFor(opts, /*withAllowInvalid=*/false);
    Decoder dec(in, o);
    std::string err = detail::unmarshalDecodeImpl(dec, out);
    if (!err.empty()) return err;
    // trailing non-ws bytes are an error like Go's Unmarshal
    if (!dec.atEnd()) {
        return "json: unexpected bytes after top-level value";
    }
    return {};
}

template <class T>
std::string unmarshalDecode(Decoder& in, T* out, std::initializer_list<Option> opts) {
    Options saved = in.options();
    Options merged = saved;
    for (const auto& opt : opts) {
        opt(merged);
    }
    in.setOptions(merged);
    std::string err = detail::unmarshalDecodeImpl(in, out);
    in.setOptions(saved);
    return err;
}

template <class T>
std::string unmarshalRead(std::istream& in, T* out, std::initializer_list<Option> opts) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return unmarshal(ss.str(), out, opts);
}

} // namespace tsc::json
