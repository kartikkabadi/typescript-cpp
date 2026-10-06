#pragma once

// debug — debug.go:1-61
//
// Debug helpers: Fail/FailBadSyntaxKind/AssertNever/Assert.
// `panic` maps to TSC_UNREACHABLE (which prints and aborts), faithfully
// preserving the message text Go would raise.

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/ast/kind.h"

// <cassert> (pulled by ast.h) defines a function-like `assert` macro that
// would clobber debug::assert below.
#ifdef assert
#undef assert
#endif

namespace tsc::debug {

namespace detail {

// fmt.Sprint operand classification: spaces are inserted between operands
// only when neither adjacent operand is a string.
template <class T>
inline constexpr bool isStringLike =
    std::is_convertible_v<T, std::string_view> &&
    !std::is_same_v<std::decay_t<T>, bool>;

// %v of a single operand.
template <class T>
std::string sprintOne(const T& v) {
    if constexpr (std::is_convertible_v<T, std::string_view>) {
        return std::string(std::string_view(v));
    } else if constexpr (std::is_same_v<std::decay_t<T>, Kind>) {
        return std::string(kindToString(v));
    } else if constexpr (std::is_same_v<std::decay_t<T>, bool>) {
        return v ? "true" : "false";
    } else if constexpr (std::is_arithmetic_v<T>) {
        if constexpr (std::is_floating_point_v<T>) {
            std::ostringstream s;
            s << v;
            return s.str();
        } else {
            return std::to_string(v);
        }
    } else if constexpr (std::is_pointer_v<T>) {
        // *Node-like values print their String()/kind when available; Go's
        // %v of a pointer prints the pointed-to value for types with
        // String methods, which is what callers rely on here.
        if constexpr (requires { v->String(); }) {
            return v->String();
        } else {
            std::ostringstream s;
            s << static_cast<const void*>(v);
            return s.str();
        }
    } else if constexpr (requires { v.String(); }) {
        return v.String();
    } else {
        static_assert(sizeof(T) == 0, "sprintOne: no %v conversion for this type");
    }
}

template <class... Ts>
std::string sprintAny(const Ts&... vs) {
    std::string out;
    bool first = true;
    bool prevWasString = false;
    auto emit = [&](const auto& v) {
        using U = std::decay_t<decltype(v)>;
        constexpr bool cur = isStringLike<U>;
        if (!first && !prevWasString && !cur) {
            out += ' ';
        }
        out += sprintOne(v);
        first = false;
        prevWasString = cur;
    };
    (emit(vs), ...);
    return out;
}

// The KindString()/String()/%v chain of debug.AssertNever.
template <class T>
std::string assertNeverDetail(const T& member) {
    if constexpr (std::is_same_v<std::decay_t<T>, Kind>) {
        // ast.Kind has KindString() in Go via Kind.String().
        return std::string(kindToString(member));
    } else if constexpr (requires { member.KindString(); }) {
        return member.KindString();
    } else if constexpr (requires { member->KindString(); }) {
        return member->KindString();
    } else if constexpr (requires { member->kind; }) {
        // C++ Node*: Go's Node.KindString() == node.Kind.String().
        return std::string(kindToString(member->kind));
    } else if constexpr (requires { member.String(); }) {
        return member.String();
    } else {
        return sprintOne(member);
    }
}

} // namespace detail

// Fail — debug.go:7
[[noreturn]] inline void fail(std::string_view reason) {
    std::string msg;
    if (reason.empty()) {
        msg = "Debug failure.";
    } else {
        msg = "Debug failure. " + std::string(reason);
    }
    // runtime.Breakpoint()
    tscUnreachable(msg.c_str());
}

// FailBadSyntaxKind — debug.go:17. Callers pass *ast.Node.
template <class N, class... Msgs>
[[noreturn]] inline void failBadSyntaxKind(N node, Msgs&&... message) {
    std::string msg;
    if constexpr (sizeof...(Msgs) == 0) {
        msg = "Unexpected node.";
    } else {
        msg = detail::sprintAny(message...);
    }
    std::string kindStr;
    if constexpr (requires { node->KindString(); }) {
        kindStr = node->KindString();
    } else {
        kindStr = std::string(kindToString(node->kind));
    }
    fail(msg + "\nNode " + kindStr + " was unexpected.");
}

// AssertNever — debug.go:27
template <class M, class... Msgs>
[[noreturn]] inline void assertNever(M&& member, Msgs&&... message) {
    std::string msg;
    if constexpr (sizeof...(Msgs) == 0) {
        msg = "Illegal value:";
    } else {
        msg = detail::sprintAny(message...);
    }
    std::string detail = detail::assertNeverDetail(member);
    fail(msg + " " + detail);
}

// Assert — debug.go:45
template <class... Msgs>
inline void assert(bool value, Msgs&&... message) {
    if (value) {
        return;
    }
    // assertSlow — kept out of line of the common path like Go.
    std::string msg;
    if constexpr (sizeof...(Msgs) > 0) {
        msg = "False expression: " + detail::sprintAny(message...);
    } else {
        msg = "False expression.";
    }
    fail(msg);
}

} // namespace tsc::debug
