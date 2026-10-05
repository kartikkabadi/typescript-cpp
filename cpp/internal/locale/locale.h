#pragma once

// Locale — locale.go:1-40
//
// BCP 47 language tag used for diagnostic localization, plus helpers to
// attach it to a context. `Locale` wraps golang.org/x/text `language.Tag`;
// `Parse` is a faithful port of `language.Parse` (the BCP 47 well-formedness
// scanner and `Default` canonicalization are reimplemented over the same
// generated tables in localetags.h).

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "internal/core/context.h"
#include "internal/locale/localetags.h"

namespace tsc::locale {

namespace detail {
// A parsed tag, mirroring x/text internal/language.Tag.
struct Tag {
    LangID langID = 0; // 0 == "und"
    ScriptID scriptID = 0;
    RegionID regionID = 0;
    std::string str;
    uint8_t pVariant = 0;  // offset in str, includes preceding '-'
    uint16_t pExt = 0;     // offset of first extension, includes preceding '-'

    std::string String() const;
    bool operator==(const Tag&) const = default;
    bool equalTags(const Tag& o) const {
        return langID == o.langID && scriptID == o.scriptID && regionID == o.regionID;
    }
    void remakeString();
};
} // namespace detail

class Locale {
public:
    Locale() = default; // the zero value is `Default`
    std::string String() const;
    bool operator==(const Locale&) const = default;

    // Internal: the parsed x/text tag.
    detail::Tag tag;
};

// Default is the default Locale; it is equivalent to an unset locale.
inline const Locale Default = Locale();

// WithLocale returns a Context with `locale` attached.
ContextPtr withLocale(const ContextPtr& ctx, Locale locale);

// FromContext returns the Locale set in ctx, or Default if not present.
Locale fromContext(const ContextPtr& ctx);

// HasLocale reports whether ctx carries a Locale.
bool hasLocale(const ContextPtr& ctx);

// Parse parses a BCP 47 language tag into a Locale, e.g. "en-US".
// It returns ok=false if the tag is not well-formed or contains an
// unknown subtag.
std::pair<Locale, bool> parse(std::string_view localeStr);

} // namespace tsc::locale
