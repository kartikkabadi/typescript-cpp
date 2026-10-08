#pragma once

// Minimal stand-in for Go's context.Context — needed only for the
// WithValue/Value key-value propagation used by the locale package.
// Keys are opaque pointers: each package defines its own unexported key
// object and passes its address, matching Go's typed-key idiom.
//
// context/context.go — context.go

#include <any>
#include <memory>
#include <string>

#include "internal/gostd/gostd.h"

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

// toContextPtr — adapts a gostd::Context (cancellable, values) to the
// value-only tsc::ContextPtr the ls layer consumes. Unlike api/session.cpp's
// lossy toLSContext, this preserves ctx values (client capabilities, locale)
// so ls code that reads GetClientCapabilities(ctx) still sees them.
inline ContextPtr toContextPtr(const gostd::Context& ctx) {
	struct adapter final : Context {
		gostd::Context c;
		explicit adapter(gostd::Context c) : c(std::move(c)) {}
		const std::any* value(const void* key) const override {
			return c ? c->value(key) : nullptr;
		}
	};
	return std::static_pointer_cast<const Context>(
	    std::make_shared<adapter>(ctx));
}


} // namespace tsc

// === slice: project ===
// core/context.go — request ID + checker lifetime context keys over
// gostd::Context (the port's context.Context with cancel/deadline
// propagation). Go keys are an unexported `key` enum; opaque inline
// objects' addresses reproduce that here.
#include "internal/gostd/gostd.h"

namespace tsc::core {

namespace detail {
inline const char requestIDKey = 0;
inline const char checkerLifetimeKey = 0;
} // namespace detail

// WithRequestID — core/context.go:13.
inline gostd::Context WithRequestID(const gostd::Context& ctx,
                                    const std::string& id) {
    return gostd::contextWithValue(ctx, &detail::requestIDKey, id);
}

// GetRequestID — core/context.go:17. "" when unset or mistyped.
inline std::string GetRequestID(const gostd::Context& ctx) {
    if (const std::any* v = gostd::ctxValue(ctx, &detail::requestIDKey)) {
        if (const std::string* s = std::any_cast<std::string>(v)) {
            return *s;
        }
    }
    return "";
}

// CheckerLifetime — core/context.go:23.
enum class CheckerLifetime : int {
    Temporary = 0,
    Diagnostics,
    API,
};
inline constexpr CheckerLifetime CheckerLifetimeTemporary =
    CheckerLifetime::Temporary;
inline constexpr CheckerLifetime CheckerLifetimeDiagnostics =
    CheckerLifetime::Diagnostics;
inline constexpr CheckerLifetime CheckerLifetimeAPI =
    CheckerLifetime::API;

// WithCheckerLifetime — core/context.go:31.
inline gostd::Context WithCheckerLifetime(const gostd::Context& ctx,
                                          CheckerLifetime lifetime) {
    return gostd::contextWithValue(ctx, &detail::checkerLifetimeKey,
                                   lifetime);
}

// GetCheckerLifetime — core/context.go:35. Temporary when unset.
inline CheckerLifetime GetCheckerLifetime(const gostd::Context& ctx) {
    if (const std::any* v =
            gostd::ctxValue(ctx, &detail::checkerLifetimeKey)) {
        if (const CheckerLifetime* l =
                std::any_cast<CheckerLifetime>(v)) {
            return *l;
        }
    }
    return CheckerLifetimeTemporary;
}

// gostdContextAdapter — wraps a gostd::Context as a tsc::Context (the
// ls/compiler layer's context type) so value lookups (checker lifetime,
// request ID, client capabilities) see the same key chain. Contexts are
// read-only once built, so the shared_ptr needs no synchronization.
class gostdContextAdapter final : public Context {
public:
    explicit gostdContextAdapter(gostd::Context c) : c_(std::move(c)) {}
    const std::any* value(const void* key) const override {
        return gostd::ctxValue(c_, key);
    }

private:
    gostd::Context c_;
};

// gostdToContextPtr — carries a gostd::Context into the tsc::ContextPtr
// used by ls entry points. Go passes the request ctx straight through;
// our ls takes the value-only ContextPtr, so adapt.
inline ContextPtr gostdToContextPtr(const gostd::Context& ctx) {
    if (!ctx) {
        return backgroundContext();
    }
    return std::make_shared<gostdContextAdapter>(ctx);
}

// contextPtrToGostd — translates a tsc::ContextPtr back into a
// gostd::Context for checker-pool checkouts, preserving the values the
// pool reads (checker lifetime, request ID).
inline gostd::Context contextPtrToGostd(const ContextPtr& ctx) {
    gostd::Context c = gostd::contextBackground();
    if (!ctx) {
        return c;
    }
    if (const std::any* v = ctx->value(&detail::checkerLifetimeKey)) {
        c = gostd::contextWithValue(c, &detail::checkerLifetimeKey, *v);
    }
    if (const std::any* v = ctx->value(&detail::requestIDKey)) {
        c = gostd::contextWithValue(c, &detail::requestIDKey, *v);
    }
    return c;
}

} // namespace tsc::core
// === end slice: project ===
