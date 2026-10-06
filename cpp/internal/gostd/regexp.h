// Minimal Go `regexp` package analog over std::regex — only the RE2 subset
// used by the ported testrunner/test-infra code:
//
//   regexp.MustCompile(pattern)     — constructor; bad patterns panic.
//   re.FindStringSubmatch(s)        — leftmost match + capture groups, or {}.
//   re.FindAllStringSubmatch(s, n)  — successive matches (n < 0 = all).
//   re.MatchString(s)               — whether s contains a match.
//   re.Split(s, n)                  — split around matches (n < 0 = all).
//
// Go's `(?m)` flag (multiline `^`/`$`) is supported: the flag is stripped at
// compile time and Find*/Split iterate per line so `^` anchors at line
// starts exactly as RE2 does under (?m). All callers' patterns are bounded
// within a line ([^\r\n]* style), so per-line iteration is faithful.
#pragma once

#include <regex>
#include <string>
#include <vector>

namespace tsc::gostd::regexp {

class Regexp {
public:
	// MustCompile — panics (TSC_UNREACHABLE is not usable before ast.h; a
	// bad pattern throws std::regex_error like Go's MustCompile panic).
	explicit Regexp(std::string_view pattern);

	// FindStringSubmatch — Go: returns the match and its submatches, or nil.
	// The empty vector models nil.
	std::vector<std::string> FindStringSubmatch(const std::string& s) const;

	// FindAllStringSubmatch — Go: returns successive matches; n < 0 = all.
	std::vector<std::vector<std::string>> FindAllStringSubmatch(
	    const std::string& s, int n) const;

	bool MatchString(const std::string& s) const;

	// Split — Go: slices s around each match. n < 0 = all substrings.
	std::vector<std::string> Split(const std::string& s, int n) const;

private:
	std::string pattern_;
	std::regex re_;
	bool multiline_ = false;

	std::vector<std::string> findIn(const std::string& s) const;
};

}  // namespace tsc::gostd::regexp
