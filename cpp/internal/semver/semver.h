// semver — port of tsc/internal/semver (version.go + version_range.go).
#pragma once

#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tsc::semver {

// version.go — regexes (ECMAScript syntax + icase mirrors Go `(?i)`).

inline const std::regex& versionRegexp() {
	static const std::regex re(
	    R"(^(0|[1-9]\d*)(?:\.(0|[1-9]\d*)(?:\.(0|[1-9]\d*)(?:-([a-z0-9-.]+))?(?:\+([a-z0-9-.]+))?)?)?$)",
	    std::regex_constants::icase);
	return re;
}
inline const std::regex& prereleaseRegexp() {
	static const std::regex re(
	    R"(^(?:0|[1-9]\d*|[a-z-][a-z0-9-]*)(?:\.(?:0|[1-9]\d*|[a-zA-Z-][a-zA-Z0-9-]*))*$)",
	    std::regex_constants::icase);
	return re;
}
inline const std::regex& prereleasePartRegexp() {
	static const std::regex re(R"(^(?:0|[1-9]\d*|[a-z-][a-z0-9-]*)$)",
	                           std::regex_constants::icase);
	return re;
}
inline const std::regex& buildRegExp() {
	static const std::regex re(R"(^[a-z0-9-]+(?:\.[a-z0-9-]+)*$)",
	                           std::regex_constants::icase);
	return re;
}
inline const std::regex& buildPartRegExp() {
	static const std::regex re(R"(^[a-z0-9-]+$)",
	                           std::regex_constants::icase);
	return re;
}
inline const std::regex& numericIdentifierRegExp() {
	static const std::regex re(R"(^(?:0|[1-9]\d*)$)");
	return re;
}

struct Version {
	uint32_t major{};
	uint32_t minor{};
	uint32_t patch{};
	std::vector<std::string> prerelease;
	std::vector<std::string> build;

	Version incrementMajor() const {
		Version r;
		r.major = major + 1;
		return r;
	}
	Version incrementMinor() const {
		Version r;
		r.major = major;
		r.minor = minor + 1;
		return r;
	}
	Version incrementPatch() const {
		Version r;
		r.major = major;
		r.minor = minor;
		r.patch = patch + 1;
		return r;
	}

	std::string String() const {
		std::string sb;
		sb += std::to_string(major);
		sb += '.';
		sb += std::to_string(minor);
		sb += '.';
		sb += std::to_string(patch);
		if (!prerelease.empty()) {
			sb += '-';
			for (size_t i = 0; i < prerelease.size(); i++) {
				if (i) sb += '.';
				sb += prerelease[i];
			}
		}
		if (!build.empty()) {
			sb += '+';
			for (size_t i = 0; i < build.size(); i++) {
				if (i) sb += '.';
				sb += build[i];
			}
		}
		return sb;
	}
};

inline const Version& versionZero() {
	static const Version v{0, 0, 0, {"0"}, {}};
	return v;
}

inline constexpr int comparisonLessThan = -1;
inline constexpr int comparisonEqualTo = 0;
inline constexpr int comparisonGreaterThan = 1;

inline int compareUint32(uint32_t a, uint32_t b) {
	return a < b ? -1 : a > b ? 1 : 0;
}

inline std::pair<uint32_t, bool> getUintComponent(std::string_view text) {
	// strconv.ParseUint(text, 10, 32)
	if (text.empty()) return {0, false};
	uint64_t r = 0;
	for (char c : text) {
		if (c < '0' || c > '9') return {0, false};
		r = r * 10 + static_cast<unsigned>(c - '0');
		if (r > 0xFFFFFFFFu) return {0, false};
	}
	return {static_cast<uint32_t>(r), true};
}

inline bool numericIdentifierIsNumeric(std::string_view s) {
	return std::regex_match(s.begin(), s.end(), numericIdentifierRegExp());
}

inline int comparePreReleaseIdentifier(std::string_view left,
                                       std::string_view right) {
	// https://semver.org/#spec-item-11
	int compareResult = left < right   ? comparisonLessThan
	                    : left > right ? comparisonGreaterThan
	                                   : comparisonEqualTo;
	if (compareResult == 0) {
		return compareResult;
	}

	bool leftIsNumeric = numericIdentifierIsNumeric(left);
	bool rightIsNumeric = numericIdentifierIsNumeric(right);

	if (leftIsNumeric || rightIsNumeric) {
		// Numeric identifiers always have lower precedence than
		// non-numeric identifiers.
		if (!rightIsNumeric) return comparisonLessThan;
		if (!leftIsNumeric) return comparisonGreaterThan;

		// Identifiers consisting of only digits are compared numerically.
		auto [leftAsNumber, leftOk] = getUintComponent(left);
		auto [rightAsNumber, rightOk] = getUintComponent(right);
		if (!leftOk || !rightOk) {
			// This should only happen in the event of an overflow.
			// If so, use the lengths or fall back to string comparison.
			int lenCompare = compareUint32(
			    static_cast<uint32_t>(left.size()),
			    static_cast<uint32_t>(right.size()));
			if (lenCompare == 0) {
				return compareResult;
			}
			return lenCompare;
		}
		return compareUint32(leftAsNumber, rightAsNumber);
	}

	// Identifiers with letters or hyphens are compared lexically in ASCII
	// sort order.
	return compareResult;
}

inline int comparePreReleaseIdentifiers(
    const std::vector<std::string>& left,
    const std::vector<std::string>& right) {
	// When major, minor, and patch are equal, a pre-release version has
	// lower precedence than a normal version.
	if (left.empty()) {
		if (right.empty()) return comparisonEqualTo;
		return comparisonGreaterThan;
	} else if (right.empty()) {
		return comparisonLessThan;
	}

	// slices.CompareFunc(left, right, comparePreReleaseIdentifier)
	size_t n = std::min(left.size(), right.size());
	for (size_t i = 0; i < n; i++) {
		int r = comparePreReleaseIdentifier(left[i], right[i]);
		if (r != 0) return r;
	}
	return left.size() < right.size()   ? comparisonLessThan
	       : left.size() > right.size() ? comparisonGreaterThan
	                                   : comparisonEqualTo;
}

// Version::Compare — pointer form preserves Go's nil-receiver semantics.
inline int versionCompare(const Version* a, const Version* b) {
	if (a == b) return comparisonEqualTo;
	if (a == nullptr) return comparisonLessThan;
	if (b == nullptr) return comparisonGreaterThan;

	int r = compareUint32(a->major, b->major);
	if (r != 0) return r;

	r = compareUint32(a->minor, b->minor);
	if (r != 0) return r;

	r = compareUint32(a->patch, b->patch);
	if (r != 0) return r;

	return comparePreReleaseIdentifiers(a->prerelease, b->prerelease);
}

// TryParseVersion — Go's (Version, error); failure returns ok=false.
inline std::pair<Version, bool> TryParseVersion(std::string_view text) {
	Version result;
	std::cmatch match;
	std::string s{text};
	if (!std::regex_match(s.c_str(), match, versionRegexp())) {
		return {result, false};
	}

	std::string majorStr = match[1].str();
	std::string minorStr = match[2].str();
	std::string patchStr = match[3].str();
	std::string prereleaseStr = match[4].str();
	std::string buildStr = match[5].str();

	bool ok;
	std::tie(result.major, ok) = getUintComponent(majorStr);
	if (!ok) return {result, false};

	if (!minorStr.empty()) {
		std::tie(result.minor, ok) = getUintComponent(minorStr);
		if (!ok) return {result, false};
	}

	if (!patchStr.empty()) {
		std::tie(result.patch, ok) = getUintComponent(patchStr);
		if (!ok) return {result, false};
	}

	if (!prereleaseStr.empty()) {
		if (!std::regex_match(prereleaseStr, prereleaseRegexp())) {
			return {result, false};
		}
		// strings.Split(prereleaseStr, ".")
		size_t pos = 0;
		while (true) {
			auto idx = prereleaseStr.find('.', pos);
			if (idx == std::string::npos) {
				result.prerelease.push_back(prereleaseStr.substr(pos));
				break;
			}
			result.prerelease.push_back(
			    prereleaseStr.substr(pos, idx - pos));
			pos = idx + 1;
		}
	}
	if (!buildStr.empty()) {
		if (!std::regex_match(buildStr, buildRegExp())) {
			return {result, false};
		}
		size_t pos = 0;
		while (true) {
			auto idx = buildStr.find('.', pos);
			if (idx == std::string::npos) {
				result.build.push_back(buildStr.substr(pos));
				break;
			}
			result.build.push_back(buildStr.substr(pos, idx - pos));
			pos = idx + 1;
		}
	}

	return {result, true};
}

inline Version MustParse(std::string_view text) {
	auto [v, ok] = TryParseVersion(text);
	if (!ok) {
		fprintf(stderr, "tsc internal error: Could not parse version "
		                "string from \"%.*s\"\n",
		        static_cast<int>(text.size()), text.data());
		abort();
	}
	return v;
}

// ---------------------------------------------------------------------------
// version_range.go
// ---------------------------------------------------------------------------

enum class comparatorOperator { LessThan, LessThanEqual, Equal, GreaterThanEqual, GreaterThan, Tilde, Caret, None };

inline const std::regex& hyphenRegExp() {
	static const std::regex re(
	    R"(^\s*([a-z0-9-+.*]+)\s+-\s+([a-z0-9-+.*]+)\s*$)",
	    std::regex_constants::icase);
	return re;
}
inline const std::regex& partialRegExp() {
	static const std::regex re(
	    R"(^([x*0]|[1-9]\d*)(?:\.([x*0]|[1-9]\d*)(?:\.([x*0]|[1-9]\d*)(?:-([a-z0-9-.]+))?(?:\+([a-z0-9-.]+))?)?)?$)",
	    std::regex_constants::icase);
	return re;
}
inline const std::regex& rangeRegExp() {
	static const std::regex re(
	    R"(^([~^<>=]|<=|>=)?\s*([a-z0-9-+.*]+)$)", std::regex_constants::icase);
	return re;
}

struct versionComparator {
	comparatorOperator op;
	Version operand;
};

inline bool isWildcard(std::string_view text) {
	return text == "*" || text == "x" || text == "X";
}

inline std::vector<std::string> splitWhitespace(std::string_view s) {
	// Go's whitespaceRegExp.Split(s, -1): `\s` = [\t\n\f\r ].
	std::vector<std::string> out;
	auto isWs = [](char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
	};
	size_t pos = 0;
	size_t n = s.size();
	while (pos < n) {
		size_t start = pos;
		while (pos < n && !isWs(s[pos])) pos++;
		out.emplace_back(s.substr(start, pos - start));
		while (pos < n && isWs(s[pos])) pos++;
	}
	if (pos >= n && !s.empty() && isWs(s[n - 1])) {
		// Go's Split appends a trailing "" when the string ends in a
		// separator.
		out.emplace_back("");
	}
	return out;
}

inline std::vector<std::string> splitLogicalOr(std::string_view s) {
	// logicalOrRegExp.Split — literal '||'.
	std::vector<std::string> out;
	size_t pos = 0;
	while (true) {
		auto idx = s.find("||", pos);
		if (idx == std::string_view::npos) {
			out.emplace_back(s.substr(pos));
			break;
		}
		out.emplace_back(s.substr(pos, idx - pos));
		pos = idx + 2;
	}
	return out;
}

inline std::string_view trimSpaceView(std::string_view s) {
	auto isWs = [](char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
		       c == '\r';
	};
	while (!s.empty() && isWs(s.front())) s.remove_prefix(1);
	while (!s.empty() && isWs(s.back())) s.remove_suffix(1);
	return s;
}

struct partialVersion {
	Version version;
	std::string majorStr;
	std::string minorStr;
	std::string patchStr;
};

// Produces a "partial" version.
inline std::pair<partialVersion, bool> parsePartial(std::string_view text) {
	std::cmatch match;
	std::string s{text};
	if (!std::regex_match(s.c_str(), match, partialRegExp())) {
		return {{}, false};
	}

	std::string majorStr = match[1].str();
	std::string minorStr = match[2].str();
	std::string patchStr = match[3].str();
	std::string prereleaseStr = match[4].str();
	std::string buildStr = match[5].str();

	if (minorStr.empty()) minorStr = "*";
	if (patchStr.empty()) patchStr = "*";

	uint32_t majorNumeric = 0, minorNumeric = 0, patchNumeric = 0;
	bool ok;

	if (isWildcard(majorStr)) {
		majorNumeric = 0;
		minorNumeric = 0;
		patchNumeric = 0;
	} else {
		std::tie(majorNumeric, ok) = getUintComponent(majorStr);
		if (!ok) return {{}, false};

		if (isWildcard(minorStr)) {
			minorNumeric = 0;
			patchNumeric = 0;
		} else {
			std::tie(minorNumeric, ok) = getUintComponent(minorStr);
			if (!ok) return {{}, false};

			if (isWildcard(patchStr)) {
				patchNumeric = 0;
			} else {
				std::tie(patchNumeric, ok) = getUintComponent(patchStr);
				if (!ok) return {{}, false};
			}
		}
	}

	std::vector<std::string> prerelease;
	if (!prereleaseStr.empty()) {
		size_t pos = 0;
		while (true) {
			auto idx = prereleaseStr.find('.', pos);
			if (idx == std::string::npos) {
				prerelease.push_back(prereleaseStr.substr(pos));
				break;
			}
			prerelease.push_back(prereleaseStr.substr(pos, idx - pos));
			pos = idx + 1;
		}
	}

	std::vector<std::string> build;
	if (!buildStr.empty()) {
		size_t pos = 0;
		while (true) {
			auto idx = buildStr.find('.', pos);
			if (idx == std::string::npos) {
				build.push_back(buildStr.substr(pos));
				break;
			}
			build.push_back(buildStr.substr(pos, idx - pos));
			pos = idx + 1;
		}
	}

	partialVersion result;
	result.version.major = majorNumeric;
	result.version.minor = minorNumeric;
	result.version.patch = patchNumeric;
	result.version.prerelease = prerelease;
	result.version.build = build;
	result.majorStr = majorStr;
	result.minorStr = minorStr;
	result.patchStr = patchStr;

	return {result, true};
}

inline std::pair<std::vector<versionComparator>, bool> parseHyphen(
    std::string_view left, std::string_view right) {
	auto [leftResult, leftOk] = parsePartial(left);
	if (!leftOk) return {{}, false};

	auto [rightResult, rightOk] = parsePartial(right);
	if (!rightOk) return {{}, false};

	std::vector<versionComparator> comparators;
	if (!isWildcard(leftResult.majorStr)) {
		// `MAJOR.*.*-...` gives us `>=MAJOR.0.0 ...`
		comparators.push_back(
		    {comparatorOperator::GreaterThanEqual, leftResult.version});
	}

	if (!isWildcard(rightResult.majorStr)) {
		comparatorOperator op;
		Version operand = rightResult.version;

		if (isWildcard(rightResult.minorStr)) {
			// `...-MAJOR.*.*` gives us `... <(MAJOR+1).0.0`
			operand = operand.incrementMajor();
			op = comparatorOperator::LessThan;
		} else if (isWildcard(rightResult.patchStr)) {
			// `...-MAJOR.MINOR.*` gives us `... <MAJOR.(MINOR+1).0`
			operand = operand.incrementMinor();
			op = comparatorOperator::LessThan;
		} else {
			// `...-MAJOR.MINOR.PATCH` gives us `... <=MAJOR.MINOR.PATCH`
			op = comparatorOperator::LessThanEqual;
		}

		comparators.push_back({op, operand});
	}

	return {comparators, true};
}

inline std::pair<std::vector<versionComparator>, bool> parseComparator(
    std::string_view op, std::string_view text) {
	comparatorOperator opEnum = comparatorOperator::None;
	if (op == "<") opEnum = comparatorOperator::LessThan;
	else if (op == "<=") opEnum = comparatorOperator::LessThanEqual;
	else if (op == "=") opEnum = comparatorOperator::Equal;
	else if (op == ">=") opEnum = comparatorOperator::GreaterThanEqual;
	else if (op == ">") opEnum = comparatorOperator::GreaterThan;
	else if (op == "~") opEnum = comparatorOperator::Tilde;
	else if (op == "^") opEnum = comparatorOperator::Caret;

	auto [result, ok] = parsePartial(text);
	if (!ok) return {{}, false};

	std::vector<versionComparator> comparatorsResult;

	if (!isWildcard(result.majorStr)) {
		switch (opEnum) {
		case comparatorOperator::Tilde: {
			versionComparator first{comparatorOperator::GreaterThanEqual,
			                        result.version};

			Version secondVersion;
			if (isWildcard(result.minorStr)) {
				secondVersion = result.version.incrementMajor();
			} else {
				secondVersion = result.version.incrementMinor();
			}

			versionComparator second{comparatorOperator::LessThan,
			                         secondVersion};
			comparatorsResult = {first, second};
			break;
		}
		case comparatorOperator::Caret: {
			versionComparator first{comparatorOperator::GreaterThanEqual,
			                        result.version};

			Version secondVersion;
			if (result.version.major > 0 || isWildcard(result.minorStr)) {
				secondVersion = result.version.incrementMajor();
			} else if (result.version.minor > 0 ||
			           isWildcard(result.patchStr)) {
				secondVersion = result.version.incrementMinor();
			} else {
				secondVersion = result.version.incrementPatch();
			}
			versionComparator second{comparatorOperator::LessThan,
			                         secondVersion};
			comparatorsResult = {first, second};
			break;
		}
		case comparatorOperator::LessThan:
		case comparatorOperator::GreaterThanEqual: {
			Version version = result.version;
			if (isWildcard(result.minorStr) || isWildcard(result.patchStr)) {
				version.prerelease = {"0"};
			}
			comparatorsResult = {{opEnum, version}};
			break;
		}
		case comparatorOperator::LessThanEqual:
		case comparatorOperator::GreaterThan: {
			Version version = result.version;
			if (isWildcard(result.minorStr)) {
				opEnum = opEnum == comparatorOperator::LessThanEqual
				             ? comparatorOperator::LessThan
				             : comparatorOperator::GreaterThanEqual;

				version = version.incrementMajor();
				version.prerelease = {"0"};
			} else if (isWildcard(result.patchStr)) {
				opEnum = opEnum == comparatorOperator::LessThanEqual
				             ? comparatorOperator::LessThan
				             : comparatorOperator::GreaterThanEqual;

				version = version.incrementMinor();
				version.prerelease = {"0"};
			}

			comparatorsResult = {{opEnum, version}};
			break;
		}
		case comparatorOperator::Equal:
		case comparatorOperator::None: {
			// normalize empty string to `=`
			opEnum = comparatorOperator::Equal;

			if (isWildcard(result.minorStr) || isWildcard(result.patchStr)) {
				Version originalVersion = result.version;

				Version firstVersion = originalVersion;
				firstVersion.prerelease = {"0"};

				Version secondVersion;
				if (isWildcard(result.minorStr)) {
					secondVersion = originalVersion.incrementMajor();
				} else {
					secondVersion = originalVersion.incrementMinor();
				}
				secondVersion.prerelease = {"0"};

				comparatorsResult = {
				    {comparatorOperator::GreaterThanEqual, firstVersion},
				    {comparatorOperator::LessThan, secondVersion},
				};
			} else {
				comparatorsResult = {{opEnum, result.version}};
			}
			break;
		}
		default:
			fprintf(stderr, "tsc internal error: Unexpected operator\n");
			abort();
		}
	} else {
		if (opEnum == comparatorOperator::LessThan ||
		    opEnum == comparatorOperator::GreaterThan) {
			comparatorsResult = {
			    // < 0.0.0-0
			    {comparatorOperator::LessThan, versionZero()},
			};
		}
	}

	return {comparatorsResult, true};
}

inline std::pair<std::vector<std::vector<versionComparator>>, bool>
parseAlternatives(std::string_view text) {
	std::vector<std::vector<versionComparator>> alternatives;

	auto trimmed = trimSpaceView(text);
	for (auto& r0 : splitLogicalOr(trimmed)) {
		auto r = trimSpaceView(r0);
		if (r.empty()) {
			continue;
		}

		std::vector<versionComparator> comparators;

		std::cmatch hyphenMatch;
		std::string rs{r};
		if (std::regex_match(rs.c_str(), hyphenMatch, hyphenRegExp())) {
			auto [parsedComparators, ok] =
			    parseHyphen(hyphenMatch[1].str(), hyphenMatch[2].str());
			if (ok) {
				comparators.insert(comparators.end(),
				                   parsedComparators.begin(),
				                   parsedComparators.end());
			} else {
				return {{}, false};
			}
		} else {
			for (auto& simple0 : splitWhitespace(r)) {
				std::string simple{trimSpaceView(simple0)};
				std::cmatch match;
				if (!std::regex_match(simple.c_str(), match,
				                      rangeRegExp())) {
					return {{}, false};
				}

				auto [parsedComparators, ok] =
				    parseComparator(match[1].str(), match[2].str());
				if (ok) {
					comparators.insert(comparators.end(),
					                   parsedComparators.begin(),
					                   parsedComparators.end());
				} else {
					return {{}, false};
				}
			}
		}

		alternatives.push_back(comparators);
	}

	return {alternatives, true};
}

struct VersionRange {
	std::vector<std::vector<versionComparator>> alternatives;

	bool Test(const Version* version) const {
		return testDisjunction(alternatives, version);
	}

	std::string String() const {
		std::string sb;
		formatDisjunction(sb, alternatives);
		if (sb.empty()) return "*";
		return sb;
	}

private:
	static void formatDisjunction(
	    std::string& sb,
	    const std::vector<std::vector<versionComparator>>& alternatives) {
		for (size_t i = 0; i < alternatives.size(); i++) {
			if (i > 0) sb += " || ";
			formatAlternative(sb, alternatives[i]);
		}
	}

	static void formatAlternative(
	    std::string& sb, const std::vector<versionComparator>& comparators) {
		for (size_t i = 0; i < comparators.size(); i++) {
			if (i > 0) sb += ' ';
			formatComparator(sb, comparators[i]);
		}
	}

	static void formatComparator(std::string& sb,
	                             const versionComparator& comparator) {
		switch (comparator.op) {
		case comparatorOperator::LessThan: sb += "<"; break;
		case comparatorOperator::LessThanEqual: sb += "<="; break;
		case comparatorOperator::Equal: sb += "="; break;
		case comparatorOperator::GreaterThanEqual: sb += ">="; break;
		case comparatorOperator::GreaterThan: sb += ">"; break;
		default: break;
		}
		sb += comparator.operand.String();
	}

	static bool testDisjunction(
	    const std::vector<std::vector<versionComparator>>& alternatives,
	    const Version* version) {
		// an empty disjunction is treated as "*" (all versions)
		if (alternatives.empty()) {
			return true;
		}
		for (auto& alternative : alternatives) {
			if (testAlternative(alternative, version)) {
				return true;
			}
		}
		return false;
	}

	static bool testAlternative(
	    const std::vector<versionComparator>& alternative,
	    const Version* version) {
		for (auto& comparator : alternative) {
			if (!testComparator(comparator, version)) {
				return false;
			}
		}
		return true;
	}

	static bool testComparator(const versionComparator& comparator,
	                           const Version* version) {
		int cmp = versionCompare(version, &comparator.operand);
		switch (comparator.op) {
		case comparatorOperator::LessThan: return cmp < 0;
		case comparatorOperator::LessThanEqual: return cmp <= 0;
		case comparatorOperator::Equal: return cmp == 0;
		case comparatorOperator::GreaterThanEqual: return cmp >= 0;
		case comparatorOperator::GreaterThan: return cmp > 0;
		default:
			fprintf(stderr, "tsc internal error: Unexpected operator\n");
			abort();
		}
	}
};

inline std::pair<VersionRange, bool> TryParseVersionRange(
    std::string_view text) {
	auto [alternatives, ok] = parseAlternatives(text);
	VersionRange r;
	r.alternatives = alternatives;
	return {r, ok};
}

}  // namespace tsc::semver
