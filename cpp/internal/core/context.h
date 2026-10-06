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

// === slice: lsp-server — dep decls for core/context.go ===
// (The file's Go context.Context functions over gostd::Context; no slice owns
// them yet, so they are implemented here — they are pure value plumbing.)

// requestIDKey / checkerLifetimeKey — context.go:4-11 (`type key int`).
namespace detail {
inline const char requestIDKey_ = 0;
inline const char checkerLifetimeKey_ = 0;
} // namespace detail

// WithRequestID — context.go:14.
inline gostd::Context WithRequestID(const gostd::Context& ctx,
                                    std::string_view id) {
	return gostd::contextWithValue(ctx, &detail::requestIDKey_, std::string(id));
}

// GetRequestID — context.go:18.
inline std::string GetRequestID(const gostd::Context& ctx) {
	if (const std::any* v = ctx ? ctx->value(&detail::requestIDKey_) : nullptr) {
		if (const std::string* s = std::any_cast<std::string>(v)) {
			return *s;
		}
	}
	return "";
}

// CheckerLifetime — context.go:25.
enum class CheckerLifetime : int {
	CheckerLifetimeTemporary = 0,
	CheckerLifetimeDiagnostics = 1,
	CheckerLifetimeAPI = 2,
};

// WithCheckerLifetime — context.go:33.
inline gostd::Context WithCheckerLifetime(const gostd::Context& ctx,
                                          CheckerLifetime lifetime) {
	return gostd::contextWithValue(ctx, &detail::checkerLifetimeKey_, lifetime);
}

// GetCheckerLifetime — context.go:37.
inline CheckerLifetime GetCheckerLifetime(const gostd::Context& ctx) {
	if (const std::any* v =
	        ctx ? ctx->value(&detail::checkerLifetimeKey_) : nullptr) {
		if (const CheckerLifetime* l = std::any_cast<CheckerLifetime>(v)) {
			return *l;
		}
	}
	return CheckerLifetime::CheckerLifetimeTemporary;
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

// === end slice: lsp-server ===

} // namespace tsc
