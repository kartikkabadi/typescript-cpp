// Minimal Go `regexp` package analog over RE2 (vendored third_party/re2,
// 2023-03-01) — only the subset used by the ported testrunner/test-infra
// code:
//
//   regexp.MustCompile(pattern)      — constructor; bad patterns panic
//                                      (throws std::runtime_error).
//   re.FindStringSubmatch(s)         — leftmost match + capture groups, or {}.
//   re.FindAllStringSubmatch(s, n)   — successive matches (n < 0 = all).
//   re.FindAllStringIndex(s, n)      — successive whole-match offsets.
//   re.MatchString(s)                — whether s contains a match.
//   re.Split(s, n)                   — split around matches (n < 0 = all).
//   re.ReplaceAllString(s, repl)     — Go template expansion ($1, $$, ${name}).
//
// RE2 is the same engine family Go's regexp package implements: linear-time
// matching (no backtracking, hence no catastrophic `(a+)+$`-style blowup),
// leftmost-first (non-POSIX) semantics, and identical syntax — `(?i)`, `(?m)`,
// `(?P<name>...)`, `\x{...}` are compiled inline exactly as Go compiles them.
// Patterns are therefore passed to RE2 verbatim; a pattern Go would reject
// (look-around, backrefs, unsupported escapes) fails RE2 compilation the
// same way, which callers observe as a MustCompile panic / nil Compile.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace re2 {
class RE2;
}

namespace tsc::gostd::regexp {

class Regexp {
public:
	// MustCompile — bad patterns panic. Throwing std::runtime_error (like
	// the previous std::regex_error) keeps Go's MustCompile-panic contract.
	explicit Regexp(std::string_view pattern);
	~Regexp();

	// Go's Regexp is used through a pointer and is not copied; the port is
	// move-only for the same reason (RE2 itself is not copyable).
	Regexp(Regexp&&) noexcept;
	Regexp& operator=(Regexp&&) noexcept;
	Regexp(const Regexp&) = delete;
	Regexp& operator=(const Regexp&) = delete;

	// FindStringSubmatch — Go: returns the match and its submatches, or nil.
	// The empty vector models nil; non-participating groups yield "".
	std::vector<std::string> FindStringSubmatch(const std::string& s) const;

	// FindAllStringSubmatch — Go: returns successive matches; n < 0 = all.
	std::vector<std::vector<std::string>> FindAllStringSubmatch(
	    const std::string& s, int n) const;

	// FindAllStringIndex — Go: successive whole-match [start,end) offsets;
	// n < 0 = all.
	std::vector<std::pair<int, int>> FindAllStringIndex(
	    const std::string& s, int n) const;

	bool MatchString(const std::string& s) const;

	// Split — Go: slices s around each match. n < 0 = all substrings.
	std::vector<std::string> Split(const std::string& s, int n) const;

	// ReplaceAllString — Go: replaces each match with repl expanded per Go
	// template rules ($1 digits, ${name} groups, $$ literal).
	std::string ReplaceAllString(const std::string& s,
	                             const std::string& repl) const;

private:
	std::string pattern_;
	std::unique_ptr<re2::RE2> re_;
	int ngroups_ = 0;

	// Unanchored find at pos (Go's Regexp.find): fills m with
	// [s0,e0,s1,e1,...] group offsets (-1,-1 = did not participate).
	bool find(const std::string& s, size_t pos, std::vector<int>& m) const;

	// Go's Regexp.matches iterator: successive non-overlapping matches,
	// skipping an empty match that abuts the previous match's end.
	void eachMatch(const std::string& s, int n,
	               const std::function<void(const std::vector<int>&)>& yield)
	    const;
};

}  // namespace tsc::gostd::regexp
