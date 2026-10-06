// Port of the Go `regexp` surface needed by testrunner — see regexp.h.
#include "internal/gostd/regexp.h"

namespace tsc::gostd::regexp {

Regexp::Regexp(std::string_view pattern) {
	if (pattern.starts_with("(?m)")) {
		multiline_ = true;
		pattern.remove_prefix(4);
	}
	pattern_ = std::string(pattern);
	re_ = std::regex(pattern_, std::regex::ECMAScript);
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

}  // namespace tsc::gostd::regexp
