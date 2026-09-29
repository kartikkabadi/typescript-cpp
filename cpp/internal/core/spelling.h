#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
namespace tsc {

// Deviation note: Go uses unicode.ToLower over all runes; keyword candidates are
// ASCII so ASCII-folding + Latin-1 letters covers every real comparison here.
// TODO(conformance): full Unicode case mapping.
namespace utf8detail {
inline std::vector<int32_t> decodeUtf8(std::string_view s) {
	std::vector<int32_t> out;
	for (size_t i = 0; i < s.size();) {
		uint8_t b0 = (uint8_t)s[i];
		if (b0 < 0x80) {
			out.push_back(b0);
			i += 1;
		} else if ((b0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
			out.push_back(((b0 & 0x1F) << 6) | ((uint8_t)s[i + 1] & 0x3F));
			i += 2;
		} else if ((b0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
			out.push_back(((b0 & 0x0F) << 12) |
			              (((uint8_t)s[i + 1] & 0x3F) << 6) |
			              ((uint8_t)s[i + 2] & 0x3F));
			i += 3;
		} else if ((b0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
			out.push_back(((b0 & 0x07) << 18) |
			              (((uint8_t)s[i + 1] & 0x3F) << 12) |
			              (((uint8_t)s[i + 2] & 0x3F) << 6) |
			              ((uint8_t)s[i + 3] & 0x3F));
			i += 4;
		} else {
			out.push_back(0xFFFD);
			i += 1;
		}
	}
	return out;
}
inline int32_t runeToLower(int32_t r) {
	if (r >= 'A' && r <= 'Z') {
		return r + 32;
	}
	// Latin-1 supplement letters (upper halves -> lower).
	if (r >= 0xC0 && r <= 0xD6 || r >= 0xD8 && r <= 0xDE) {
		return r + 32;
	}
	return r;
}
inline bool equalFold(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		char ca = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i];
		char cb = b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i];
		if (ca != cb) {
			return false;
		}
	}
	return true;
}
}  // namespace utf8detail

// getSpellingSuggestion: pick the closest candidate under a Levenshtein
// threshold. `name`/candidates are UTF-8; decoded to runes (code points).
inline double levenshteinWithMax(std::vector<double>& previous,
                                 std::vector<double>& current,
                                 const std::vector<int32_t>& s1,
                                 const std::vector<int32_t>& s2,
                                 double maxValue) {
	size_t bufferSize = s2.size() + 1;
	previous.clear();
	previous.resize(bufferSize);
	current.clear();
	current.resize(bufferSize);
	double big = maxValue + 0.01;
	for (size_t i = 0; i < previous.size(); i++) {
		previous[i] = (double)i;
	}
	for (size_t i = 1; i <= s1.size(); i++) {
		int32_t c1 = s1[i - 1];
		int minJ = std::max((int)std::ceil((double)i - maxValue), 1);
		int maxJ =
			std::min((int)std::floor(maxValue + (double)i), (int)s2.size());
		double colMin = (double)i;
		current[0] = colMin;
		for (int j = 1; j < minJ; j++) {
			current[j] = big;
		}
		for (int j = minJ; j <= maxJ; j++) {
			double substitutionDistance, dist;
			if (utf8detail::runeToLower(s1[i - 1]) ==
			    utf8detail::runeToLower(s2[j - 1])) {
				substitutionDistance = previous[j - 1] + 0.1;
			} else {
				substitutionDistance = previous[j - 1] + 2;
			}
			if (c1 == s2[j - 1]) {
				dist = previous[j - 1];
			} else {
				dist = std::min(previous[j] + 1,
				                std::min(current[j - 1] + 1,
				                         substitutionDistance));
			}
			current[j] = dist;
			colMin = std::min(colMin, dist);
		}
		for (size_t j = maxJ + 1; j <= s2.size(); j++) {
			current[j] = big;
		}
		if (colMin > maxValue) {
			// Give up -- everything in this column is > max and it can't get
			// better in future columns.
			return -1;
		}
		std::swap(previous, current);
	}
	double res = previous[s2.size()];
	if (res > maxValue) {
		return -1;
	}
	return res;
}

inline std::string getSpellingSuggestionForStrings(
	std::string_view name, const std::vector<std::string_view>& candidates) {
	std::vector<int32_t> runeName = utf8detail::decodeUtf8(name);
	int maximumLengthDifference =
		std::max(2, (int)((double)runeName.size() * 0.34));
	double bestDistance =
		std::floor((double)runeName.size() * 0.4) + 0.9;
	std::vector<double> previous, current;
	std::string_view bestCandidate;
	bool hasBest = false;
	for (const std::string_view& candidate : candidates) {
		std::vector<int32_t> runeCandidate = utf8detail::decodeUtf8(candidate);
		size_t maxLen = std::max(runeCandidate.size(), runeName.size());
		size_t minLen = std::min(runeCandidate.size(), runeName.size());
		if (!candidate.empty() &&
		    maxLen - minLen <= (size_t)maximumLengthDifference) {
			if (candidate == name) {
				continue;
			}
			if (candidate.size() < 3 &&
			    !utf8detail::equalFold(candidate, name)) {
				continue;
			}
			double distance = levenshteinWithMax(
				previous, current, runeName, runeCandidate, bestDistance);
			if (distance < 0) {
				continue;
			}
			if (distance < bestDistance) {
				bestDistance = distance;
				bestCandidate = candidate;
				hasBest = true;
			} else if (!hasBest || candidate < bestCandidate) {
				bestCandidate = candidate;
				hasBest = true;
			}
		}
	}
	return std::string(bestCandidate);
}

// Generic getSpellingSuggestion — port of core.GetSpellingSuggestion
// (core.go:606). Returns the default T when nothing is close enough.
template <typename T, typename GetName, typename Compare>
T getSpellingSuggestion(std::string_view name,
                        const std::vector<T>& candidates, GetName getName,
                        Compare compare, int maxCandidates = 0) {
	std::vector<int32_t> runeName = utf8detail::decodeUtf8(name);
	int maximumLengthDifference =
		std::max(2, (int)((double)runeName.size() * 0.34));
	double bestDistance =
		std::floor((double)runeName.size() * 0.4) +
		0.9;  // If the best result is worse than this, don't bother.
	std::vector<double> previous, current;
	T bestCandidate{};
	bool hasBest = false;
	int checkedCandidates = 0;
	for (const T& candidate : candidates) {
		checkedCandidates++;
		if (maxCandidates > 0 && checkedCandidates > maxCandidates) {
			return T{};
		}
		std::string candidateName = getName(candidate);
		std::vector<int32_t> runeCandidate =
			utf8detail::decodeUtf8(candidateName);
		size_t maxLen = std::max(runeCandidate.size(), runeName.size());
		size_t minLen = std::min(runeCandidate.size(), runeName.size());
		if (!candidateName.empty() &&
		    maxLen - minLen <= (size_t)maximumLengthDifference) {
			if (candidateName == name) {
				continue;
			}
			// Only consider candidates less than 3 characters long when they
			// differ by case. Otherwise, don't bother, since a user would
			// usually notice differences of a 2-character name.
			if (candidateName.size() < 3 &&
			    !utf8detail::equalFold(candidateName, name)) {
				continue;
			}
			double distance = levenshteinWithMax(
				previous, current, runeName, runeCandidate, bestDistance);
			if (distance < 0) {
				continue;
			}
			if (distance < bestDistance) {
				bestDistance = distance;
				bestCandidate = candidate;
				hasBest = true;
			} else if (!hasBest || compare(candidate, bestCandidate) < 0) {
				bestCandidate = candidate;
				hasBest = true;
			}
		}
	}
	return bestCandidate;
}

}  // namespace tsc
