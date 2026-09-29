// pattern — port of tsc/internal/core/pattern.go
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace tsc {

struct Pattern {
	std::string text;
	int starIndex = -1;  // -1 for exact match

	bool isValid() const { return starIndex == -1 || starIndex < (int)text.size(); }

	bool matches(std::string_view candidate) const {
		if (starIndex == -1) {
			return text == candidate;
		}
		return candidate.size() >= text.size() - 1 &&
		       candidate.substr(0, starIndex) == text.substr(0, starIndex) &&
		       candidate.substr(candidate.size() - (text.size() - starIndex - 1)) ==
		           text.substr(starIndex + 1);
	}

	std::string matchedText(std::string_view candidate) const {
		// Callers must check matches first.
		if (starIndex == -1) {
			return "";
		}
		return std::string(
			candidate.substr(starIndex, candidate.size() - text.size() + starIndex + 1));
	}
};

inline Pattern tryParsePattern(std::string_view pattern) {
	size_t starIndex = pattern.find('*');
	if (starIndex == std::string_view::npos ||
	    pattern.find('*', starIndex + 1) == std::string_view::npos) {
		return Pattern{std::string(pattern), starIndex == std::string_view::npos
		                                     ? -1
		                                     : (int)starIndex};
	}
	return Pattern{};
}

// pattern.go: FindBestPatternMatch — most specific matching element, or
// nullptr when nothing matches.
template <class T>
const T* findBestPatternMatch(const std::vector<T>& values, Pattern (*getPattern)(const T&),
                              std::string_view candidate) {
	const T* bestPattern = nullptr;
	int longestMatchPrefixLength = -1;
	for (const T& value : values) {
		Pattern pattern = getPattern(value);
		if ((pattern.starIndex == -1 || pattern.starIndex > longestMatchPrefixLength) &&
		    pattern.matches(candidate)) {
			bestPattern = &value;
			longestMatchPrefixLength = pattern.starIndex;
		}
	}
	return bestPattern;
}

}  // namespace tsc
