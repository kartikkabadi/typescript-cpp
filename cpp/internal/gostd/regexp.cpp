// Port of the Go `regexp` surface needed by testrunner — see regexp.h.
#include "internal/gostd/regexp.h"

#include <algorithm> // std::all_of (not transitively included by libc++)

namespace tsc::gostd::regexp {

Regexp::Regexp(std::string_view pattern) {
	bool icase = false;
	while (pattern.starts_with("(?") ) {
		auto close = pattern.find(')');
		if (close == std::string_view::npos) break;
		auto flags = pattern.substr(2, close - 2);
		bool any = false;
		for (char f : flags) {
			if (f == 'm') multiline_ = true, any = true;
			else if (f == 'i') icase = true, any = true;
		}
		if (!any) break;
		pattern.remove_prefix(close + 1);
	}
	pattern_ = std::string(pattern);
	auto flags = std::regex::ECMAScript;
	if (icase) flags |= std::regex::icase;
	re_ = std::regex(pattern_, flags);
}

std::vector<std::string> Regexp::findIn(const std::string& s) const {
	std::smatch m;
	if (!std::regex_search(s, m, re_)) {
		return {};
	}
	std::vector<std::string> out;
	out.reserve(m.size());
	for (size_t i = 0; i < m.size(); i++) {
		out.push_back(m[i].str());
	}
	return out;
}

std::vector<std::string> Regexp::FindStringSubmatch(
    const std::string& s) const {
	if (!multiline_) {
		return findIn(s);
	}
	// (?m): `^`/`$` anchor at line boundaries — search line by line.
	size_t start = 0;
	while (start <= s.size()) {
		size_t nl = s.find('\n', start);
		std::string line = s.substr(
		    start, nl == std::string::npos ? nl : nl - start);
		if (auto m = findIn(line); !m.empty()) {
			return m;
		}
		if (nl == std::string::npos) break;
		start = nl + 1;
	}
	return {};
}

std::vector<std::vector<std::string>> Regexp::FindAllStringSubmatch(
    const std::string& s, int n) const {
	std::vector<std::vector<std::string>> out;
	if (n == 0) return out;
	if (multiline_) {
		size_t start = 0;
		while (start <= s.size() && (n < 0 || (int)out.size() < n)) {
			size_t nl = s.find('\n', start);
			std::string line = s.substr(
			    start, nl == std::string::npos ? nl : nl - start);
			if (auto m = findIn(line); !m.empty()) {
				out.push_back(std::move(m));
			}
			if (nl == std::string::npos) break;
			start = nl + 1;
		}
		return out;
	}
	for (auto it = std::sregex_iterator(s.begin(), s.end(), re_);
	     it != std::sregex_iterator() && (n < 0 || (int)out.size() < n);
	     ++it) {
		std::vector<std::string> m;
		m.reserve(it->size());
		for (size_t i = 0; i < it->size(); i++) {
			m.push_back((*it)[i].str());
		}
		out.push_back(std::move(m));
	}
	return out;
}

bool Regexp::MatchString(const std::string& s) const {
	if (!multiline_) {
		return std::regex_search(s, re_);
	}
	size_t start = 0;
	while (start <= s.size()) {
		size_t nl = s.find('\n', start);
		std::string line = s.substr(
		    start, nl == std::string::npos ? nl : nl - start);
		if (std::regex_search(line, re_)) {
			return true;
		}
		if (nl == std::string::npos) break;
		start = nl + 1;
	}
	return false;
}

std::vector<std::string> Regexp::Split(const std::string& s, int n) const {
	std::vector<std::string> out;
	if (n == 0) return out;
	size_t last = 0;
	size_t count = 0;
	for (auto it = std::sregex_iterator(s.begin(), s.end(), re_);
	     it != std::sregex_iterator() && (n < 0 || count < (size_t)n - 1);
	     ++it, ++count) {
		out.push_back(s.substr(last, it->position() - last));
		last = it->position() + it->length();
	}
	out.push_back(s.substr(last));
	return out;
}

// expandTemplate — Go's replacement-template rules: $$ literal $; $1/$99
// numbered groups (all digits consumed); ${name} named groups; a bare $ not
// followed by a valid name is emitted literally.
static std::string expandTemplate(const std::smatch& m,
                                  const std::string& repl) {
	std::string out;
	for (size_t i = 0; i < repl.size();) {
		char c = repl[i];
		if (c != '$' || i + 1 >= repl.size()) {
			out += c;
			i++;
			continue;
		}
		size_t j = i + 1;
		if (repl[j] == '$') {
			out += '$';
			i = j + 1;
			continue;
		}
		bool braces = repl[j] == '{';
		size_t nameStart = braces ? j + 1 : j;
		size_t k = nameStart;
		while (k < repl.size() &&
		       (std::isalnum((unsigned char)repl[k]) || repl[k] == '_'))
			k++;
		if (braces) {
			if (k == nameStart || k >= repl.size() || repl[k] != '}') {
				out += '$';
				i++;
				continue;
			}
		} else if (k == nameStart) {
			out += '$';
			i++;
			continue;
		}
		std::string name = repl.substr(nameStart, k - nameStart);
		bool allDigits = !name.empty() &&
		    std::all_of(name.begin(), name.end(), [](char ch) {
			    return std::isdigit((unsigned char)ch) != 0;
		    });
		if (allDigits) {
			int idx = std::atoi(name.c_str());
			if (idx >= 0 && idx < (int)m.size()) out += m[idx].str();
		}
		// Named groups are not used by ported callers; an unrecognized
		// (non-numeric) name expands to empty like Go's absent group.
		i = braces ? k + 1 : k;
	}
	return out;
}

std::string Regexp::replaceAllIn(const std::string& s,
                                 const std::string& repl) const {
	std::string out;
	size_t last = 0;
	for (auto it = std::sregex_iterator(s.begin(), s.end(), re_);
	     it != std::sregex_iterator(); ++it) {
		out += s.substr(last, it->position() - last);
		out += expandTemplate(*it, repl);
		last = it->position() + it->length();
	}
	out += s.substr(last);
	return out;
}

std::string Regexp::ReplaceAllString(const std::string& s,
                                     const std::string& repl) const {
	if (!multiline_) {
		return replaceAllIn(s, repl);
	}
	// (?m): per-line, mirroring MatchString/FindStringSubmatch.
	std::string out;
	size_t start = 0;
	while (start <= s.size()) {
		size_t nl = s.find('\n', start);
		std::string line = s.substr(
		    start, nl == std::string::npos ? nl : nl - start);
		out += replaceAllIn(line, repl);
		if (nl == std::string::npos) break;
		out += '\n';
		start = nl + 1;
	}
	return out;
}

}  // namespace tsc::gostd::regexp
