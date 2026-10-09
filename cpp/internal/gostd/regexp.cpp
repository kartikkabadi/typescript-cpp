// Port of the Go `regexp` surface needed by testrunner — see regexp.h.
// Engine is vendored RE2 (third_party/re2, 2023-03-01): linear-time NFA
// simulation like Go's regexp, so catastrophic patterns such as (a+)+$
// complete in milliseconds instead of hanging (std::regex backtracks).
//
// All match loops below mirror Go 1.27's regexp.go verbatim:
//   eachMatch        = Regexp.matches (empty-match-abutting rule)
//   ReplaceAllString = Regexp.replaceAll (own loop: empty match abutting
//                      the previous end suppresses only the repl expansion)
//   Split            = Regexp.Split (driven by the FindAll index semantics)
//   expand           = Regexp.expand + extract ($-template rules)
#include "internal/gostd/regexp.h"

#include <stdexcept>

#include "re2/re2.h"

namespace tsc::gostd::regexp {

// runeWidthAt — utf8.DecodeRuneInString width of the first rune at s[i]:
// byte length on success, 1 on invalid/incomplete encoding, 0 at end.
static int runeWidthAt(const std::string& s, size_t i) {
	if (i >= s.size()) return 0;
	unsigned char b0 = (unsigned char)s[i];
	if (b0 < 0x80) return 1;
	int n;
	uint32_t min;
	uint32_t cp;
	if (b0 >= 0xF0) { n = 4; min = 0x10000; cp = b0 & 0x07; }
	else if (b0 >= 0xE0) { n = 3; min = 0x800; cp = b0 & 0x0F; }
	else if (b0 >= 0xC0) { n = 2; min = 0x80; cp = b0 & 0x1F; }
	else return 1;  // stray continuation byte
	if (i + (size_t)n > s.size()) return 1;
	for (int k = 1; k < n; k++) {
		unsigned char bk = (unsigned char)s[i + k];
		if ((bk & 0xC0) != 0x80) return 1;
		cp = (cp << 6) | (bk & 0x3F);
	}
	// Overlong encodings, UTF-16 surrogates, and >U+10FFFF are RuneError.
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 1;
	return n;
}

Regexp::Regexp(std::string_view pattern) : pattern_(pattern) {
	re2::RE2::Options opts;
	// Go's Compile reports errors through its return value, not stderr.
	opts.set_log_errors(false);
	re_ = std::make_unique<re2::RE2>(
	    re2::StringPiece(pattern_.data(), pattern_.size()), opts);
	if (!re_->ok()) {
		throw std::runtime_error("regexp: Compile(`" + pattern_ +
		                         "`): error parsing regexp: " + re_->error() +
		                         " (`" + re_->error_arg() + "`)");
	}
	ngroups_ = re_->NumberOfCapturingGroups();
}

Regexp::~Regexp() = default;
Regexp::Regexp(Regexp&&) noexcept = default;
Regexp& Regexp::operator=(Regexp&&) noexcept = default;

bool Regexp::find(const std::string& s, size_t pos,
                  std::vector<int>& m) const {
	const int n = ngroups_ + 1;
	std::vector<re2::StringPiece> pieces(n);
	re2::StringPiece text(s.data(), s.size());
	if (!re_->Match(text, pos, s.size(), re2::RE2::UNANCHORED, pieces.data(),
	                n)) {
		return false;
	}
	m.resize(2 * n);
	for (int i = 0; i < n; i++) {
		if (pieces[i].data() == nullptr) {
			m[2 * i] = m[2 * i + 1] = -1;
		} else {
			m[2 * i] = int(pieces[i].data() - text.data());
			m[2 * i + 1] = m[2 * i] + int(pieces[i].size());
		}
	}
	return true;
}

void Regexp::eachMatch(
    const std::string& s, int n,
    const std::function<void(const std::vector<int>&)>& yield) const {
	// Go regexp.matches: max<0 means unlimited; n==0 means no matches.
	if (n == 0) return;
	const int end = (int)s.size();
	std::vector<int> m;
	for (int pos = 0, prevMatchEnd = -1; pos <= end;) {
		if (!find(s, pos, m)) break;
		bool accept = true;
		if (m[1] == pos) {
			// We've found an empty match.
			if (m[0] == prevMatchEnd) {
				// We don't allow an empty match right
				// after a previous match, so ignore it.
				accept = false;
			}
			if (int width = runeWidthAt(s, pos); width > 0) {
				pos += width;
			} else {
				pos = end + 1;
			}
		} else {
			pos = m[1];
		}
		prevMatchEnd = m[1];
		if (accept) {
			yield(m);
			if (n > 0 && --n == 0) return;
		}
	}
}

std::vector<std::string> Regexp::FindStringSubmatch(
    const std::string& s) const {
	std::vector<int> m;
	if (!find(s, 0, m)) return {};
	std::vector<std::string> out(m.size() / 2);
	for (size_t i = 0; i < out.size(); i++) {
		if (m[2 * i] >= 0) out[i] = s.substr(m[2 * i], m[2 * i + 1] - m[2 * i]);
	}
	return out;
}

std::vector<std::vector<std::string>> Regexp::FindAllStringSubmatch(
    const std::string& s, int n) const {
	std::vector<std::vector<std::string>> out;
	eachMatch(s, n, [&](const std::vector<int>& m) {
		std::vector<std::string> sub(m.size() / 2);
		for (size_t i = 0; i < sub.size(); i++) {
			if (m[2 * i] >= 0)
				sub[i] = s.substr(m[2 * i], m[2 * i + 1] - m[2 * i]);
		}
		out.push_back(std::move(sub));
	});
	return out;
}

std::vector<std::pair<int, int>> Regexp::FindAllStringIndex(
    const std::string& s, int n) const {
	std::vector<std::pair<int, int>> out;
	eachMatch(s, n, [&](const std::vector<int>& m) {
		out.emplace_back(m[0], m[1]);
	});
	return out;
}

bool Regexp::MatchString(const std::string& s) const {
	return re_->Match(re2::StringPiece(s.data(), s.size()), 0, s.size(),
	                  re2::RE2::UNANCHORED, nullptr, 0);
}

std::vector<std::string> Regexp::Split(const std::string& s, int n) const {
	std::vector<std::string> out;
	if (n == 0) return out;
	if (!pattern_.empty() && s.empty()) return {""};

	std::vector<std::pair<int, int>> matches = FindAllStringIndex(s, n);
	out.reserve(matches.size());
	int beg = 0, end = 0;
	for (auto [m0, m1] : matches) {
		if (n > 0 && (int)out.size() >= n - 1) break;
		end = m0;
		if (m1 != 0) out.push_back(s.substr(beg, end - beg));
		beg = m1;
	}
	if (end != (int)s.size()) out.push_back(s.substr(beg));
	return out;
}

// extract — Go regexp.extract: reads "name" or "{name}" from repl at i
// (the '$' is already consumed). Returns {name, num(-1 if non-numeric),
// chars-consumed, ok}.
struct Extracted {
	std::string name;
	int num = -1;
	size_t rest = 0;
	bool ok = false;
};

static Extracted extract(const std::string& str, size_t i) {
	Extracted r;
	if (i >= str.size()) return r;
	bool brace = str[i] == '{';
	size_t k = i + (brace ? 1 : 0);
	size_t nameStart = k;
	// Letters, digits, and '_' — Go accepts full Unicode letters/digits;
	// callers' templates are ASCII, and every non-ASCII UTF-8 rune here is
	// treated as a name char (a superset divergence only for non-letter
	// multibyte punctuation, which no caller uses).
	while (k < str.size()) {
		unsigned char c = (unsigned char)str[k];
		if (c < 0x80) {
			if (!std::isalnum(c) && c != '_') break;
			k++;
		} else {
			k += runeWidthAt(str, k);
		}
	}
	if (k == nameStart) return r;  // empty name is not okay
	r.name = str.substr(nameStart, k - nameStart);
	if (brace) {
		if (k >= str.size() || str[k] != '}') return r;  // missing }
		k++;
	}
	// Parse number (Go: num = -1 when non-numeric, >=1e8, or leading zero).
	int num = 0;
	for (char ch : r.name) {
		if (ch < '0' || ch > '9' || num >= 100000000) {
			num = -1;
			break;
		}
		num = num * 10 + (ch - '0');
	}
	if (r.name[0] == '0' && r.name.size() > 1) num = -1;
	r.num = num;
	r.rest = k;
	r.ok = true;
	return r;
}

// expand — Go regexp.expand: appends repl to out with $-references resolved
// against match offsets m (flat s0,e0 pairs; -1 = non-participating).
static void expand(std::string& out, const std::string& s,
                   const std::vector<int>& m, const std::string& repl,
                   const std::map<std::string, int>& named) {
	size_t i = 0;
	while (i < repl.size()) {
		size_t dollar = repl.find('$', i);
		if (dollar == std::string::npos) break;
		out += repl.substr(i, dollar - i);
		size_t j = dollar + 1;
		if (j < repl.size() && repl[j] == '$') {
			out += '$';
			i = j + 1;
			continue;
		}
		Extracted e = extract(repl, j);
		if (!e.ok) {
			out += '$';
			i = j;
			continue;
		}
		i = e.rest;
		if (e.num >= 0) {
			if (2 * e.num + 1 < (int)m.size() && m[2 * e.num] >= 0) {
				out += s.substr(m[2 * e.num],
				                m[2 * e.num + 1] - m[2 * e.num]);
			}
		} else {
			auto it = named.find(e.name);
			if (it != named.end() && 2 * it->second + 1 < (int)m.size() &&
			    m[2 * it->second] >= 0) {
				out += s.substr(m[2 * it->second],
				                m[2 * it->second + 1] - m[2 * it->second]);
			}
		}
	}
	out += repl.substr(i);
}

std::string Regexp::ReplaceAllString(const std::string& s,
                                     const std::string& repl) const {
	// Go Regexp.replaceAll (string source, bsrc=nil). nmatch only sizes the
	// capture buffer — full captures are always collected here.
	std::string out;
	out.reserve(2 * s.size());
	int lastMatchEnd = 0;  // end position of the most recent match
	int searchPos = 0;     // position where we next look for a match
	const int endPos = (int)s.size();
	std::vector<int> a;
	while (searchPos <= endPos) {
		if (!find(s, searchPos, a)) break;

		// Copy the unmatched characters before this match.
		out += s.substr(lastMatchEnd, a[0] - lastMatchEnd);

		// Now insert a copy of the replacement string, but not for a
		// match of the empty string immediately after another match.
		if (a[1] > lastMatchEnd || a[0] == 0) {
			expand(out, s, a, repl, re_->NamedCapturingGroups());
		}
		lastMatchEnd = a[1];

		// Advance past this match; always advance at least one character.
		int width = runeWidthAt(s, searchPos);
		if (searchPos + width > a[1]) {
			searchPos += width;
		} else if (searchPos + 1 > a[1]) {
			// This clause is only needed at the end of the input
			// string. In that case, DecodeRuneInString returns width=0.
			searchPos++;
		} else {
			searchPos = a[1];
		}
	}
	// Copy the unmatched characters after the last match.
	out += s.substr(lastMatchEnd);
	return out;
}

}  // namespace tsc::gostd::regexp
