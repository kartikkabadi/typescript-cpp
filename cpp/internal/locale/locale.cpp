#include "internal/locale/locale.h"

namespace tsc::locale {

namespace {

bool isAlpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlnum(char c) { return isAlpha(c) || isDigit(c); }

// Well-formed BCP-47 check matching golang.org/x/text/language.Parse's
// accepted shapes (regular grandfathered tags excluded — tsc never ships
// those):
//   lang      = 2-3 alpha | 4 alpha | 5-8 alpha
//   script    = 4 alpha
//   region    = 2 alpha | 3 digit
//   variant   = 5-8 alnum | 4 alnum starting with digit
//   extension = singleton [0-9a-wy-zA-WY-Z] + (2-8 alnum)+
//   private   = x + (1-8 alnum)+
bool wellFormed(std::string_view s) {
	if (s.empty()) {
		return false;
	}
	// language.Parse rejects tags over 128 chars as ErrSyntax-ish too.
	if (s.size() > 128) {
		return false;
	}
	size_t i = 0, n = s.size();
	// lang
	size_t j = i;
	while (j < n && isAlpha(s[j])) j++;
	size_t langLen = j - i;
	if (langLen < 2 || langLen > 8) {
		return false;
	}
	i = j;
	bool sawPrivate = false;
	enum { Script, Region, Variant } next = Script;
	while (i < n) {
		if (s[i] != '-') {
			return false;
		}
		i++;
		j = i;
		while (j < n && isAlnum(s[j])) j++;
		std::string_view sub = s.substr(i, j - i);
		if (sub.empty()) {
			return false;
		}
		// private-use: x-...
		if (sub.size() == 1 && (s[i] == 'x' || s[i] == 'X')) {
			sawPrivate = true;
			i = j;
			// remaining subtags must be 1-8 alnum, each after '-'
			while (i < n) {
				if (s[i] != '-') return false;
				i++;
				j = i;
				while (j < n && isAlnum(s[j])) j++;
				size_t len = j - i;
				if (len < 1 || len > 8) return false;
				i = j;
			}
			return true;
		}
		// extension: singleton 0-9a-wy-zA-WY-Z + one-or-more 2-8 alnum subtags
		if (sub.size() == 1 && isAlnum(s[i]) && s[i] != 'x' && s[i] != 'X') {
			// extension singleton; consume 2-8 alnum subtags
			i = j;
			if (i >= n || s[i] != '-') return false;
			for (;;) {
				i++;
				j = i;
				while (j < n && isAlnum(s[j])) j++;
				size_t len = j - i;
				if (len < 2 || len > 8) return false;
				i = j;
				if (i >= n) break;
				if (s[i] != '-') return false;
				// peek: is next subtag another extension singleton or
				// a private-use x? then done with this extension.
				size_t k = i + 1, e = k;
				while (e < n && isAlnum(s[e])) e++;
				if (e - k == 1) {
					break;  // next '-x' starts a new extension/private
				}
			}
			return i == n;
		}
		// positional: script(4a) then region(2a|3d) then variants
		if (next == Script && sub.size() == 4 &&
		    std::all_of(sub.begin(), sub.end(), isAlpha)) {
			next = Region;
			i = j;
			continue;
		}
		if (next <= Region) {
			if ((sub.size() == 2 &&
			     std::all_of(sub.begin(), sub.end(), isAlpha)) ||
			    (sub.size() == 3 &&
			     std::all_of(sub.begin(), sub.end(), isDigit))) {
				next = Variant;
				i = j;
				continue;
			}
			// skipped script — region may still follow; a 4-alpha here
			// is also invalid (script must precede region).
		}
		// variant: 5-8 alnum, or 4 alnum starting with digit (repeatable)
		if ((sub.size() >= 5 && sub.size() <= 8) ||
		    (sub.size() == 4 && isDigit(sub[0]))) {
			if (std::all_of(sub.begin(), sub.end(), isAlnum)) {
				i = j;
				continue;
			}
			return false;
		}
		return false;
	}
	(void)sawPrivate;
	return true;
}

}  // namespace

// locale.Parse — locale.go:36.
std::pair<Locale, bool> Parse(std::string_view localeStr) {
	if (!wellFormed(localeStr)) {
		return {Locale{}, false};
	}
	return {Locale{std::string(localeStr)}, true};
}

}  // namespace tsc::locale
