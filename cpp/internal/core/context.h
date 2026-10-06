#pragma once

// Minimal stand-in for Go's context.Context — needed only for the
// WithValue/Value key-value propagation used by the locale package.
// Keys are opaque pointers: each package defines its own unexported key
// object and passes its address, matching Go's typed-key idiom.
//
// context/context.go — context.go

#include <any>
#include <memory>

#include "internal/gostd/gostd.h"

namespace tsc {

class Context {
public:
    virtual ~Context() = default;
    // Returns the value stored under `key`, or nullptr if absent.
    // Mirrors Go's `Context.Value(key) (any)` (nil when unset).
    virtual const std::any* value(const void* key) const { return nullptr; }
};

using ContextPtr = std::shared_ptr<const Context>;

// context.Background() — the root empty context.
inline ContextPtr backgroundContext() {
    static const std::shared_ptr<const Context> empty = std::make_shared<Context>();
    return empty;
}

namespace detail {
class ValueContext final : public Context {
public:
    ValueContext(ContextPtr parent, const void* key, std::any value)
        : parent_(std::move(parent)), key_(key), value_(std::move(value)) {}

    const std::any* value(const void* key) const override {
        if (key == key_) {
            return &value_;
        }
        return parent_ ? parent_->value(key) : nullptr;
    }

private:
    ContextPtr parent_;
    const void* key_;
    std::any value_;
};
} // namespace detail

// context.WithValue — returns a context carrying key=value, chaining to parent.
inline ContextPtr withContextValue(const ContextPtr& parent, const void* key, std::any value) {
    return std::make_shared<const detail::ValueContext>(parent, key, std::move(value));
}

// === slice: testutil-leaves ===
// core/context.go — request-ID and checker-lifetime context values over
// gostd::Context (the standard context.Context used by project/lsp code).
// Keys are opaque addresses of static objects, matching Go's typed-key idiom.

namespace detail {
// Unexported key objects (core/context.go: `type key int` const block).
inline const char requestIDKey = 0;
inline const char checkerLifetimeKey = 0;
} // namespace detail

// WithRequestID — core/context.go:14.
inline gostd::Context withRequestID(const gostd::Context& ctx, std::string id) {
    return gostd::contextWithValue(ctx, &detail::requestIDKey, std::move(id));
}

// GetRequestID — core/context.go:18.
inline std::string getRequestID(const gostd::Context& ctx) {
    if (ctx) {
        if (auto* v = ctx->value(&detail::requestIDKey)) {
            if (auto* s = std::any_cast<std::string>(v)) {
                return *s;
            }
        }
    }
    return "";
}

// CheckerLifetime — core/context.go:24.
enum class CheckerLifetime {
    Temporary = 0,
    Diagnostics = 1,
    API = 2,
};
inline constexpr CheckerLifetime CheckerLifetimeTemporary = CheckerLifetime::Temporary;
inline constexpr CheckerLifetime CheckerLifetimeDiagnostics = CheckerLifetime::Diagnostics;
inline constexpr CheckerLifetime CheckerLifetimeAPI = CheckerLifetime::API;

// WithCheckerLifetime — core/context.go:31.
inline gostd::Context withCheckerLifetime(const gostd::Context& ctx, CheckerLifetime lifetime) {
    return gostd::contextWithValue(ctx, &detail::checkerLifetimeKey, lifetime);
}

// GetCheckerLifetime — core/context.go:35.
inline CheckerLifetime getCheckerLifetime(const gostd::Context& ctx) {
    if (ctx) {
        if (auto* v = ctx->value(&detail::checkerLifetimeKey)) {
            if (auto* l = std::any_cast<CheckerLifetime>(v)) {
                return *l;
            }
        }
    }
    return CheckerLifetimeTemporary;
}
// === end slice: testutil-leaves ===

} // namespace tsc
