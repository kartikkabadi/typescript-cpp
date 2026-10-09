// tests_regexp.cpp — gostd::regexp behavioral tests against Go oracle
// values (generated with `go run` on Go 1.27's regexp), plus the ReDoS
// guard: catastrophic patterns that hang a backtracking engine must
// complete in milliseconds under RE2.
#include <chrono>
#include <string>
#include <vector>

#include "internal/gostd/regexp.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

gostd::regexp::Regexp compile(const std::string& p) {
	return gostd::regexp::Regexp(p);
}

void TestRegexpFindStringSubmatch(T* t) {
	t->Parallel();
	// Go: ["// @name:  value" "name" "value"]
	auto m = compile(R"((?m)^\/{2}\s*@(\w+)\s*:\s*([^\r\n]*))")
	             .FindStringSubmatch("x\n// @name:  value\ny");
	assert::DeepEqual(t, m, std::vector<std::string>(
	                          {"// @name:  value", "name", "value"}));

	// Non-participating group -> "". Go: ["ac" ""], ["abc" "b"]
	assert::DeepEqual(t, compile("a(b)?c").FindStringSubmatch("ac"),
	                  std::vector<std::string>({"ac", ""}));
	assert::DeepEqual(t, compile("a(b)?c").FindStringSubmatch("abc"),
	                  std::vector<std::string>({"abc", "b"}));
	assert::DeepEqual(t, compile("z+").FindStringSubmatch("abc"),
	                  std::vector<std::string>({}));
}

void TestRegexpFindAllEmptyMatches(T* t) {
	t->Parallel();
	// Go: a* on "abaabaccadaaae" ->
	// [["a"] ["aa"] ["a"] [""] ["a"] ["aaa"] [""]]
	auto all =
	    compile("a*").FindAllStringSubmatch("abaabaccadaaae", -1);
	assert::DeepEqual(t, all, std::vector<std::vector<std::string>>(
	                            {{"a"}, {"aa"}, {"a"}, {""}, {"a"}, {"aaa"},
	                             {""}}));
	auto lim =
	    compile("a*").FindAllStringSubmatch("abaabaccadaaae", 3);
	assert::DeepEqual(t, lim, std::vector<std::vector<std::string>>(
	                            {{"a"}, {"aa"}, {"a"}}));
	// Go: (x)?y on "yzy" -> [["y" ""] ["y" ""]]
	auto xy = compile("(x)?y").FindAllStringSubmatch("yzy", -1);
	assert::DeepEqual(t, xy, std::vector<std::vector<std::string>>(
	                           {{"y", ""}, {"y", ""}}));
}

void TestRegexpSplit(T* t) {
	t->Parallel();
	// The Split examples from Go's own doc comment.
	assert::DeepEqual(t, compile("a*").Split("abaabaccadaaae", 5),
	                  std::vector<std::string>({"", "b", "b", "c",
	                                            "cadaaae"}));
	assert::DeepEqual(t, compile("a*").Split("abaabaccadaaae", -1),
	                  std::vector<std::string>(
	                      {"", "b", "b", "c", "c", "d", "e"}));
	assert::DeepEqual(t, compile("a*").Split("abaabaccadaaae", 2),
	                  std::vector<std::string>({"", "baabaccadaaae"}));
	assert::DeepEqual(t, compile("x").Split("axbxc", -1),
	                  std::vector<std::string>({"a", "b", "c"}));
	assert::DeepEqual(t, compile("\\r?\\n").Split("a\r\nb\nc\n", -1),
	                  std::vector<std::string>({"a", "b", "c", ""}));
	// An empty match at position 0 contributes no leading piece; a lone
	// anchor matches only at 0 -> whole string back. (Go: ["abc"].)
	assert::DeepEqual(t, compile("^").Split("abc", -1),
	                  std::vector<std::string>({"abc"}));
	// Empty pattern splits between every rune. (Go: ["a" "b"].)
	assert::DeepEqual(t, compile("").Split("ab", -1),
	                  std::vector<std::string>({"a", "b"}));
	assert::DeepEqual(t, compile("x").Split("axbxc", 0),
	                  std::vector<std::string>({}));
}

void TestRegexpReplaceAll(T* t) {
	t->Parallel();
	// stack_sanitizer.go's actual expansion form.
	assert::Equal(t,
	              compile("(?i)(key|token)([(\\[.|])")
	                  .ReplaceAllString("aKEY(b", "${1}X_X${2}"),
	              std::string("aKEYX_X(b"));
	// Go: "[a]b[a]" — empty match at end contributes one more expansion.
	assert::Equal(t, compile("a*").ReplaceAllString("aba", "[$0]"),
	              std::string("[a]b[a]"));
	assert::Equal(t, compile("(a*)(b*)").ReplaceAllString("aab", "[$1|$2]"),
	              std::string("[aa|b]"));
	// Named groups — Go: "two+one".
	assert::Equal(t, compile("(?P<first>\\w+)-(?P<second>\\w+)")
	                     .ReplaceAllString("one-two", "${second}+$first"),
	              std::string("two+one"));
	// $$ -> $, out-of-range $9 -> "", unterminated ${bad -> literal.
	// Go: "a$x${badb".
	assert::Equal(t, compile("x").ReplaceAllString("axb", "$$x$9${bad"),
	              std::string("a$x${badb"));
	// (?m) multiline anchor in replace. Go: "Y\nY\nbx".
	assert::Equal(t, compile("(?m)^x").ReplaceAllString("x\nx\nbx", "Y"),
	              std::string("Y\nY\nbx"));
	// Non-participating group expands to "". Go: "<>".
	assert::Equal(t, compile("(a)?b").ReplaceAllString("b", "<$1>"),
	              std::string("<>"));
}

void TestRegexpAnchors(T* t) {
	t->Parallel();
	// ^/$ are text anchors (Go defaults), (?m) makes them line anchors.
	assert::Equal(t, compile("^x$").MatchString("x\nx"), false);
	assert::Equal(t, compile("(?m)^x$").MatchString("x\nx"), true);
	assert::Equal(t, compile("\\bx\\b").MatchString("a x b"), true);
	// $ does not match before a trailing newline (unlike JS's $).
	assert::Equal(t, compile("x$").MatchString("x\n"), false);
	assert::Equal(t, compile("x$").MatchString("x\ny"), false);
}

void TestRegexpFindAllStringIndex(T* t) {
	t->Parallel();
	// Go: [[0 1] [2 3]] — empty matches abutting a match end are skipped.
	auto idx = compile("a*").FindAllStringIndex("aba", -1);
	assert::DeepEqual(t, idx, std::vector<std::pair<int, int>>(
	                            {{0, 1}, {2, 3}}));
}

void TestRegexpCompileErrorsPanic(T* t) {
	t->Parallel();
	// MustCompile panics (throw here) on patterns Go rejects — including
	// backrefs and look-around that std::regex ECMAScript accepted.
	for (const char* bad : {"(a+", "a\\1", "a(?=b)", "[\\1]", "a{2,1}"}) {
		bool threw = false;
		try {
			compile(bad);
		} catch (const std::exception&) {
			threw = true;
		}
		if (!threw) t->Errorf("pattern %s did not panic", {bad});
	}
}

void TestRegexpCatastrophicCompletes(T* t) {
	t->Parallel();
	// ReDoS guard (SECURITY_REVIEW F1): (a+)+$ is RE2-valid, and Go's
	// linear engine answers 'no match' instantly while a backtracking
	// engine explores exponentially many groupings of the 'a' run.
	// 32 a's + 'b' is the classic non-matching worst case — ~2^32
	// groupings under backtracking; trivial under RE2.
	auto re = compile("(a+)+$");
	std::string input(32, 'a');
	input += 'b';
	auto start = std::chrono::steady_clock::now();
	bool matched = re.MatchString(input);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	              std::chrono::steady_clock::now() - start)
	              .count();
	assert::Equal(t, matched, false);
	if (ms > 500) {
		t->Errorf("(a+)+$ x32 took %dms — backtracking engine?", {(int)ms});
	}
}

}  // namespace

REGISTER_UNIT_TEST("gostd.TestRegexpFindStringSubmatch",
                   TestRegexpFindStringSubmatch);
REGISTER_UNIT_TEST("gostd.TestRegexpFindAllEmptyMatches",
                   TestRegexpFindAllEmptyMatches);
REGISTER_UNIT_TEST("gostd.TestRegexpSplit", TestRegexpSplit);
REGISTER_UNIT_TEST("gostd.TestRegexpReplaceAll", TestRegexpReplaceAll);
REGISTER_UNIT_TEST("gostd.TestRegexpAnchors", TestRegexpAnchors);
REGISTER_UNIT_TEST("gostd.TestRegexpFindAllStringIndex",
                   TestRegexpFindAllStringIndex);
REGISTER_UNIT_TEST("gostd.TestRegexpCompileErrorsPanic",
                   TestRegexpCompileErrorsPanic);
REGISTER_UNIT_TEST("gostd.TestRegexpCatastrophicCompletes",
                   TestRegexpCatastrophicCompletes);
