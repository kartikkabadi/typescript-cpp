// C++23 port of tsc/internal/testrunner/compiler_test_filter.go — filter
// compiler test files before reading them. Go reads flag.Lookup("test.run");
// the port reads a process-global pattern set via setCompilerTestRunPattern
// (wired to -run in the runners that link the testrunner tests).

#include "internal/testrunner/testrunner.h"

#include <string>
#include <string_view>
#include <vector>

namespace tsc::testrunner {

namespace {

std::string g_compilerTestRunPattern;

// Minimal UTF-8 decoder: decodes one rune, advances i. Malformed input
// decodes the byte as-is (rune_error handled by callers as non-printable).
uint32_t decodeRune(std::string_view s, size_t* i) {
	unsigned char c = static_cast<unsigned char>(s[*i]);
	if (c < 0x80) {
		(*i)++;
		return c;
	}
	int len = 0;
	uint32_t r = 0;
	if ((c & 0xE0) == 0xC0) { len = 2; r = c & 0x1F; }
	else if ((c & 0xF0) == 0xE0) { len = 3; r = c & 0x0F; }
	else if ((c & 0xF8) == 0xF0) { len = 4; r = c & 0x07; }
	if (len == 0 || *i + len > s.size()) {
		(*i)++;
		return c;
	}
	for (int k = 1; k < len; k++) {
		unsigned char cc = static_cast<unsigned char>(s[*i + k]);
		if ((cc & 0xC0) != 0x80) {
			(*i)++;
			return c;
		}
		r = (r << 6) | (cc & 0x3F);
	}
	*i += len;
	return r;
}

// unicode.IsSpace
bool isSpaceRune(uint32_t r) {
	if (r <= 0x20) {
		return r == 0x20 || (r >= 0x09 && r <= 0x0D);
	}
	return r == 0x85 || r == 0xA0 || r == 0x1680 ||
	       (r >= 0x2000 && r <= 0x200A) || r == 0x2028 || r == 0x2029 ||
	       r == 0x202F || r == 0x205F || r == 0x3000;
}

// strconv.IsPrint — approximation of unicode.IsPrint (L|M|N|P|S + Zs space).
// Errs toward false, which sends the caller to the accept-all fallback.
bool isPrintRune(uint32_t r) {
	if (r < 0x20 || (r >= 0x7F && r <= 0x9F)) return false;
	if (r >= 0xD800 && r <= 0xDFFF) return false;
	if (r > 0x10FFFF) return false;
	if (r == 0x2028 || r == 0x2029) return false;
	if (r == 0xFEFF || r == 0x00AD || r == 0x061C) return false;
	if (r >= 0x200B && r <= 0x200F) return false;
	if (r >= 0x2060 && r <= 0x2064) return false;
	if (r >= 0xE000 && r <= 0xF8FF) return false;
	if ((r & 0xFFFE) == 0xFFFE) return false;  // 0xFFFE/0xFFFF per plane
	return true;
}

bool isUpperRune(uint32_t r) {
	if (r >= 'A' && r <= 'Z') return true;
	// Common non-ASCII upper ranges sufficient for test-name patterns.
	if (r >= 0xC0 && r <= 0xDE && r != 0xD7) return true;
	if (r >= 0x391 && r <= 0x3A9 && r != 0x3A2) return true;
	if (r >= 0x410 && r <= 0x42F) return true;
	return false;
}

// compilerTestNameNeedsRewrite — compiler_test_filter.go:48.
bool compilerTestNameNeedsRewrite(std::string_view name) {
	for (size_t i = 0; i < name.size();) {
		uint32_t r = decodeRune(name, &i);
		if (isSpaceRune(r) || !isPrintRune(r)) {
			return true;
		}
	}
	return false;
}

// splitCompilerTestFilter — compiler_test_filter.go:54.
std::vector<std::string> splitCompilerTestFilter(std::string_view pattern) {
	std::vector<std::string> parts;
	size_t start = 0;
	int depth = 0;
	bool inClass = false, escaped = false;
	for (size_t index = 0; index < pattern.size(); index++) {
		char character = pattern[index];
		if (escaped) {
			escaped = false;
			continue;
		}
		if (character == '\\') {
			escaped = true;
			continue;
		}
		if (inClass) {
			if (character == ']') {
				inClass = false;
			}
			continue;
		}
		switch (character) {
		case '[':
			inClass = true;
			break;
		case '(':
			depth++;
			break;
		case ')':
			depth--;
			break;
		case '|':
			if (depth == 0) {
				return {};
			}
			break;
		case '/':
			if (depth == 0) {
				parts.emplace_back(pattern.substr(start, index - start));
				start = index + 1;
			}
			break;
		}
	}
	parts.emplace_back(pattern.substr(start));
	return parts;
}

// literalPrefix — conservative regexp.LiteralPrefix equivalent: the longest
// literal string every match must start with. Anything the scanner cannot
// resolve (classes, alternation, anchors mid-pattern, fold flags, unbounded
// repeats) ends the prefix — it never over-extends. Sets *err on syntax
// errors so the caller can fall back to accept-all.
std::string literalPrefix(std::string_view pattern, bool* err) {
	std::string prefix;
	std::vector<size_t> groupStarts;
	size_t lastAtomStart = 0;
	*err = false;
	size_t i = 0;
	if (i < pattern.size() && pattern[i] == '^') {
		i++;  // ^ anchor: contributes nothing to the literal prefix
	}
	auto lastAtom = [&]() -> std::string_view {
		return std::string_view(prefix).substr(lastAtomStart);
	};
	while (i < pattern.size()) {
		char c = pattern[i];
		switch (c) {
		case '\\': {
			i++;
			if (i >= pattern.size()) {
				*err = true;
				return {};
			}
			char e = pattern[i];
			i++;
			if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' ||
			    e == 'S' || e == 'b' || e == 'B' || e == 'A' || e == 'z' ||
			    e == 'G') {
				return prefix;  // class/anchor escapes contribute nothing
			}
			uint32_t lit = 0;
			switch (e) {
			case 'n': lit = '\n'; break;
			case 't': lit = '\t'; break;
			case 'r': lit = '\r'; break;
			case 'f': lit = '\f'; break;
			case 'v': lit = '\v'; break;
			case 'a': lit = '\a'; break;
			case 'x': case 'u': case 'U': {
				// \xNN, \x{...}, \uNNNN, \UNNNNNNNN — decode best-effort
				int digits = e == 'x' ? 2 : e == 'u' ? 4 : 8;
				bool braced = false;
				if (i < pattern.size() && pattern[i] == '{') {
					braced = true;
					i++;
					digits = 6;
				}
				uint32_t v = 0;
				int got = 0;
				while (i < pattern.size() && got < digits) {
					char h = pattern[i];
					uint32_t d;
					if (h >= '0' && h <= '9') d = h - '0';
					else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
					else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
					else break;
					v = v * 16 + d;
					got++;
					i++;
				}
				if (got == 0 || (braced && (i >= pattern.size() || pattern[i] != '}'))) {
					*err = true;
					return {};
				}
				if (braced) i++;
				lit = v;
				break;
			}
			default:
				if ((e >= '0' && e <= '9') || (e >= 'A' && e <= 'Z') ||
				    (e >= 'a' && e <= 'z')) {
					*err = true;  // unknown escape
					return {};
				}
				lit = static_cast<unsigned char>(e);  // escaped punctuation
				break;
			}
			lastAtomStart = prefix.size();
			// encode lit as UTF-8
			if (lit < 0x80) {
				prefix += static_cast<char>(lit);
			} else if (lit < 0x800) {
				prefix += static_cast<char>(0xC0 | (lit >> 6));
				prefix += static_cast<char>(0x80 | (lit & 0x3F));
			} else if (lit < 0x10000) {
				prefix += static_cast<char>(0xE0 | (lit >> 12));
				prefix += static_cast<char>(0x80 | ((lit >> 6) & 0x3F));
				prefix += static_cast<char>(0x80 | (lit & 0x3F));
			} else {
				prefix += static_cast<char>(0xF0 | (lit >> 18));
				prefix += static_cast<char>(0x80 | ((lit >> 12) & 0x3F));
				prefix += static_cast<char>(0x80 | ((lit >> 6) & 0x3F));
				prefix += static_cast<char>(0x80 | (lit & 0x3F));
			}
			break;
		}
		case '^':
			return prefix;  // mid-pattern anchor: nothing further is literal
		case '$':
		case '.':
		case '[':
			return prefix;
		case '(': {
			if (i + 1 < pattern.size() && pattern[i + 1] == '?') {
				// (?:...) and (?P<name>...) are transparent groups
				size_t j = i + 2;
				if (j < pattern.size() && pattern[j] == ':') {
					groupStarts.push_back(prefix.size());
					i = j + 1;
					break;
				}
				if (j < pattern.size() && pattern[j] == 'P') {
					j++;
					if (j < pattern.size() && pattern[j] == '<') {
						while (j < pattern.size() && pattern[j] != '>') j++;
						if (j >= pattern.size()) {
							*err = true;
							return {};
						}
						groupStarts.push_back(prefix.size());
						i = j + 1;
						break;
					}
				}
				// (?i)-style flag groups / lookarounds: stop conservatively
				return prefix;
			}
			groupStarts.push_back(prefix.size());
			i++;
			break;
		}
		case ')': {
			if (groupStarts.empty()) {
				*err = true;
				return {};
			}
			lastAtomStart = groupStarts.back();
			groupStarts.pop_back();
			i++;
			break;
		}
		case '|': {
			// Alternation: the group's contribution is not literal.
			size_t gs = groupStarts.empty() ? 0 : groupStarts.back();
			prefix.resize(gs);
			return prefix;
		}
		case '*':
		case '?': {
			prefix.resize(lastAtomStart);
			return prefix;
		}
		case '+': {
			// atom contributes once; repeats end the prefix
			return prefix;
		}
		case '{': {
			// {m}, {m,}, {m,n}; a lone '{' is a literal in Go regexp
			size_t j = i + 1;
			long m = -1, n = -1;
			bool hasComma = false, closed = false;
			for (; j < pattern.size(); j++) {
				char cj = pattern[j];
				if (cj == '}') { closed = true; break; }
				if (cj == ',') { hasComma = true; if (n < 0) n = -2; continue; }
				if (cj < '0' || cj > '9') break;
				if (n == -2) n = 0;
				if (!hasComma) m = (m < 0 ? 0 : m) * 10 + (cj - '0');
				else n = n * 10 + (cj - '0');
			}
			if (!closed || (m < 0 && n < 0)) {
				lastAtomStart = prefix.size();
				prefix += '{';
				i++;
				break;
			}
			if (!hasComma) n = m;
			std::string_view atom = lastAtom();
			if (m == 0) {
				prefix.resize(lastAtomStart);
				if (n != m) {
					return prefix;
				}
				i = j + 1;
				break;
			}
			std::string extra;
			for (long k = 1; k < m; k++) {
				extra.append(atom.data(), atom.size());
			}
			prefix += extra;
			if (n != m) {
				return prefix;
			}
			i = j + 1;
			break;
		}
		default: {
			lastAtomStart = prefix.size();
			prefix += c;
			i++;
			break;
		}
		}
	}
	if (!groupStarts.empty()) {
		*err = true;
		return {};
	}
	return prefix;
}

}  // namespace

// setCompilerTestRunPattern — Go reads flag.Lookup("test.run") inside
// RunTests; the port stores the -run value passed to the test binary.
void setCompilerTestRunPattern(std::string pattern) {
	g_compilerTestRunPattern = std::move(pattern);
}

std::string_view compilerTestRunPattern() {
	return g_compilerTestRunPattern;
}

// compilerTestFileFilter — compiler_test_filter.go:10.
std::function<bool(std::string_view)> compilerTestFileFilter(
    const std::string& pattern) {
	auto acceptAll = [](std::string_view) { return true; };
	if (compilerTestNameNeedsRewrite(pattern)) {
		return acceptAll;
	}
	auto parts = splitCompilerTestFilter(pattern);
	if (parts.size() < 2) {
		return acceptAll;
	}
	std::string component = parts[1];
	bool anchored = !component.empty() && component.front() == '^';
	std::string_view expression = std::string_view(component).substr(
	    anchored ? 1 : 0);
	bool err = false;
	std::string prefix = literalPrefix(expression, &err);
	if (err || prefix.empty() || compilerTestNameNeedsRewrite(prefix)) {
		return acceptAll;
	}
	bool hasUpper = false;
	for (size_t i = 0; i < prefix.size();) {
		uint32_t r = decodeRune(prefix, &i);
		if (isUpperRune(r)) {
			hasUpper = true;
			break;
		}
	}
	if (!anchored && !hasUpper) {
		return acceptAll;
	}
	return [prefix, anchored](std::string_view filename) -> bool {
		if (compilerTestNameNeedsRewrite(filename)) {
			return true;
		}
		if (anchored) {
			if (filename.size() >= prefix.size() &&
			    filename.substr(0, prefix.size()) == prefix) {
				return true;
			}
			return filename.size() < prefix.size() &&
			       prefix.substr(0, filename.size()) == filename &&
			       prefix[filename.size()] == '_';
		}
		if (filename.find(prefix) != std::string_view::npos) {
			return true;
		}
		for (size_t index = 0; index < filename.size(); index++) {
			auto suffix = filename.substr(index);
			if (prefix.size() >= suffix.size() + 1 &&
			    prefix.substr(0, suffix.size()) == suffix &&
			    prefix[suffix.size()] == '_') {
				return true;
			}
		}
		return false;
	};
}

}  // namespace tsc::testrunner
