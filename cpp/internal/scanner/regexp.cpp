// Port of tsc/internal/scanner/regexp.go — regular-expression literal validator.
#include "internal/scanner/regexp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/diagnostics/messages_generated.h"
#include "internal/stringutil/stringutil.h"

namespace tsc {

// ---------------------------------------------------------------------------
// core::ScriptTarget -> lowercase name (matches Go's strings.ToLower(t.String()))
// ---------------------------------------------------------------------------

static std::string scriptTargetLowercase(ScriptTarget t) {
	static const char* names[] = {
		"none",   "es5",    "es2015", "es2016", "es2017", "es2018",
		"es2019", "es2020", "es2021", "es2022", "es2023", "es2024",
		"es2025",
	};
	if (t == ScriptTarget::ESNext)
		return "esnext";
	auto i = static_cast<int>(t);
	if (i >= 0 && i < static_cast<int>(std::size(names)))
		return names[i];
	return "esnext";
}

// ---------------------------------------------------------------------------
// flags
// ---------------------------------------------------------------------------

RegularExpressionFlags charCodeToRegExpFlag(char32_t ch) {
	switch (ch) {
	case 'd':
		return RegularExpressionFlagsHasIndices;
	case 'g':
		return RegularExpressionFlagsGlobal;
	case 'i':
		return RegularExpressionFlagsIgnoreCase;
	case 'm':
		return RegularExpressionFlagsMultiline;
	case 's':
		return RegularExpressionFlagsDotAll;
	case 'u':
		return RegularExpressionFlagsUnicode;
	case 'v':
		return RegularExpressionFlagsUnicodeSets;
	case 'y':
		return RegularExpressionFlagsSticky;
	}
	return 0;
}

static ScriptTarget regExpFlagToFirstAvailableLanguageVersion(
	RegularExpressionFlags flag) {
	switch (flag) {
	case RegularExpressionFlagsHasIndices:
		return ScriptTarget::ES2022;
	case RegularExpressionFlagsDotAll:
		return ScriptTarget::ES2018;
	case RegularExpressionFlagsUnicodeSets:
		return ScriptTarget::ES2024;
	}
	return ScriptTarget::None;
}

void checkRegularExpressionFlagAvailability(Scanner& s,
                                            RegularExpressionFlags flag,
                                            int pos, int size) {
	ScriptTarget availableFrom =
		regExpFlagToFirstAvailableLanguageVersion(flag);
	if (availableFrom != ScriptTarget::None &&
	    s.languageVersion() < availableFrom) {
		s.errorAt(
			This_regular_expression_flag_is_only_available_when_targeting_0_or_later,
			pos, size, {scriptTargetLowercase(availableFrom)});
	}
}

// ---------------------------------------------------------------------------
// core::GetSpellingSuggestionForStrings (Levenshtein-with-max port)
// ---------------------------------------------------------------------------

static double levenshteinWithMax(std::vector<double>& previous,
                                 std::vector<double>& current,
                                 const std::u32string& s1,
                                 const std::u32string& s2, double maxValue) {
	size_t bufferSize = s2.size() + 1;
	previous.assign(bufferSize, 0.0);
	current.assign(bufferSize, 0.0);

	double big = maxValue + 0.01;
	for (size_t i = 0; i < previous.size(); i++)
		previous[i] = static_cast<double>(i);
	for (size_t i = 1; i <= s1.size(); i++) {
		char32_t c1 = s1[i - 1];
		size_t minJ = std::max<size_t>(
			static_cast<size_t>(std::ceil(static_cast<double>(i) - maxValue)),
			1);
		size_t maxJ =
			std::min<size_t>(static_cast<size_t>(std::floor(
			                     maxValue + static_cast<double>(i))),
			                 s2.size());
		double colMin = static_cast<double>(i);
		current[0] = colMin;
		for (size_t j = 1; j < minJ; j++)
			current[j] = big;
		for (size_t j = minJ; j <= maxJ; j++) {
			double substitutionDistance, dist;
			char32_t lower1 = s1[i - 1];
			char32_t lower2 = s2[j - 1];
			if (lower1 >= 'a' && lower1 <= 'z')
				lower1 -= 'a' - 'A';
			if (lower2 >= 'a' && lower2 <= 'z')
				lower2 -= 'a' - 'A';
			if (lower1 == lower2)
				substitutionDistance = previous[j - 1] + 0.1;
			else
				substitutionDistance = previous[j - 1] + 2;
			if (c1 == s2[j - 1])
				dist = previous[j - 1];
			else
				dist = std::min(previous[j] + 1,
				                std::min(current[j - 1] + 1,
				                         substitutionDistance));
			current[j] = dist;
			colMin = std::min(colMin, dist);
		}
		for (size_t j = maxJ + 1; j <= s2.size(); j++)
			current[j] = big;
		if (colMin > maxValue)
			return -1;
		std::swap(previous, current);
	}
	double res = previous[s2.size()];
	if (res > maxValue)
		return -1;
	return res;
}

static std::u32string toU32(std::string_view s) {
	std::u32string out;
	for (size_t i = 0; i < s.size();) {
		int w;
		out.push_back(decodeUtf8Rune(s.substr(i), &w));
		i += w;
	}
	return out;
}

template <typename It>
static std::string getSpellingSuggestionForStrings(std::string_view name,
                                                   It begin, It end) {
	std::u32string runeName = toU32(name);
	size_t maximumLengthDifference =
		std::max<size_t>(2, static_cast<size_t>(runeName.size() * 0.34));
	double bestDistance =
		std::floor(runeName.size() * 0.4) + 0.9;
	std::vector<double> previous, current;
	std::string bestCandidate;
	bool hasBest = false;
	for (auto it = begin; it != end; ++it) {
		const std::string& candidateName = *it;
		size_t maxLen = std::max(candidateName.size(), runeName.size());
		size_t minLen = std::min(candidateName.size(), runeName.size());
		if (!candidateName.empty() && maxLen - minLen <= maximumLengthDifference) {
			if (candidateName == name)
				continue;
			if (candidateName.size() < 3) {
				bool equalFold = candidateName.size() == name.size();
				if (equalFold) {
					for (size_t i = 0; i < name.size(); i++) {
						char a = static_cast<char>(
							std::tolower(static_cast<unsigned char>(name[i])));
						char b = static_cast<char>(std::tolower(
							static_cast<unsigned char>(candidateName[i])));
						if (a != b) {
							equalFold = false;
							break;
						}
					}
				}
				if (!equalFold)
					continue;
			}
			double distance =
				levenshteinWithMax(previous, current, runeName,
				                   toU32(candidateName), bestDistance);
			if (distance < 0)
				continue;
			if (distance < bestDistance) {
				bestDistance = distance;
				bestCandidate = candidateName;
				hasBest = true;
			} else if (!hasBest || candidateName < bestCandidate) {
				bestCandidate = candidateName;
				hasBest = true;
			}
		}
	}
	return bestCandidate;
}

// ---------------------------------------------------------------------------
// unicodeproperties.go tables
// ---------------------------------------------------------------------------

static const std::unordered_map<std::string, std::string>
    nonBinaryUnicodeProperties = {
		{"General_Category", "General_Category"},
		{"gc", "General_Category"},
		{"Script", "Script"},
		{"sc", "Script"},
		{"Script_Extensions", "Script_Extensions"},
		{"scx", "Script_Extensions"},
};

static const std::unordered_set<std::string> binaryUnicodeProperties = {
	"ASCII", "ASCII_Hex_Digit", "AHex", "Alphabetic", "Alpha", "Any",
	"Assigned", "Bidi_Control", "Bidi_C", "Bidi_Mirrored", "Bidi_M",
	"Case_Ignorable", "CI", "Cased", "Changes_When_Casefolded", "CWCF",
	"Changes_When_Casemapped", "CWCM", "Changes_When_Lowercased", "CWL",
	"Changes_When_NFKC_Casefolded", "CWKCF", "Changes_When_Titlecased",
	"CWT", "Changes_When_Uppercased", "CWU", "Dash",
	"Default_Ignorable_Code_Point", "DI", "Deprecated", "Dep", "Diacritic",
	"Dia", "Emoji", "Emoji_Component", "EComp", "Emoji_Modifier", "EMod",
	"Emoji_Modifier_Base", "EBase", "Emoji_Presentation", "EPres",
	"Extended_Pictographic", "ExtPict", "Extender", "Ext", "Grapheme_Base",
	"Gr_Base", "Grapheme_Extend", "Gr_Ext", "Hex_Digit", "Hex",
	"IDS_Binary_Operator", "IDSB", "IDS_Trinary_Operator", "IDST",
	"ID_Continue", "IDC", "ID_Start", "IDS", "Ideographic", "Ideo",
	"Join_Control", "Join_C", "Logical_Order_Exception", "LOE", "Lowercase",
	"Lower", "Math", "Noncharacter_Code_Point", "NChar", "Pattern_Syntax",
	"Pat_Syn", "Pattern_White_Space", "Pat_WS", "Quotation_Mark", "QMark",
	"Radical", "Regional_Indicator", "RI", "Sentence_Terminal", "STerm",
	"Soft_Dotted", "SD", "Terminal_Punctuation", "Term",
	"Unified_Ideograph", "UIdeo", "Uppercase", "Upper",
	"Variation_Selector", "VS", "White_Space", "space", "XID_Continue",
	"XIDC", "XID_Start", "XIDS",
};

static const std::unordered_set<std::string> binaryUnicodePropertiesOfStrings =
	{"Basic_Emoji", "Emoji_Keycap_Sequence", "RGI_Emoji_Modifier_Sequence",
	 "RGI_Emoji_Flag_Sequence", "RGI_Emoji_Tag_Sequence",
	 "RGI_Emoji_ZWJ_Sequence", "RGI_Emoji"};

static const std::unordered_set<std::string> scriptValues = {
	"Adlm", "Adlam", "Aghb", "Caucasian_Albanian", "Ahom", "Arab", "Arabic",
	"Armi", "Imperial_Aramaic", "Armn", "Armenian", "Avst", "Avestan",
	"Bali", "Balinese", "Bamu", "Bamum", "Bass", "Bassa_Vah", "Batk",
	"Batak", "Beng", "Bengali", "Bhks", "Bhaiksuki", "Bopo", "Bopomofo",
	"Brah", "Brahmi", "Brai", "Braille", "Bugi", "Buginese", "Buhd",
	"Buhid", "Cakm", "Chakma", "Cans", "Canadian_Aboriginal", "Cari",
	"Carian", "Cham", "Cher", "Cherokee", "Chrs", "Chorasmian", "Copt",
	"Coptic", "Qaac", "Cpmn", "Cypro_Minoan", "Cprt", "Cypriot", "Cyrl",
	"Cyrillic", "Deva", "Devanagari", "Diak", "Dives_Akuru", "Dogr",
	"Dogra", "Dsrt", "Deseret", "Dupl", "Duployan", "Egyp",
	"Egyptian_Hieroglyphs", "Elba", "Elbasan", "Elym", "Elymaic", "Ethi",
	"Ethiopic", "Geor", "Georgian", "Glag", "Glagolitic", "Gong",
	"Gunjala_Gondi", "Gonm", "Masaram_Gondi", "Goth", "Gothic", "Gran",
	"Grantha", "Grek", "Greek", "Gujr", "Gujarati", "Guru", "Gurmukhi",
	"Hang", "Hangul", "Hani", "Han", "Hano", "Hanunoo", "Hatr", "Hatran",
	"Hebr", "Hebrew", "Hira", "Hiragana", "Hluw", "Anatolian_Hieroglyphs",
	"Hmng", "Pahawh_Hmong", "Hmnp", "Nyiakeng_Puachue_Hmong", "Hrkt",
	"Katakana_Or_Hiragana", "Hung", "Old_Hungarian", "Ital", "Old_Italic",
	"Java", "Javanese", "Kali", "Kayah_Li", "Kana", "Katakana", "Kawi",
	"Khar", "Kharoshthi", "Khmr", "Khmer", "Khoj", "Khojki", "Kits",
	"Khitan_Small_Script", "Knda", "Kannada", "Kthi", "Kaithi", "Lana",
	"Tai_Tham", "Laoo", "Lao", "Latn", "Latin", "Lepc", "Lepcha", "Limb",
	"Limbu", "Lina", "Linear_A", "Linb", "Linear_B", "Lisu", "Lyci",
	"Lycian", "Lydi", "Lydian", "Mahj", "Mahajani", "Maka", "Makasar",
	"Mand", "Mandaic", "Mani", "Manichaean", "Marc", "Marchen", "Medf",
	"Medefaidrin", "Mend", "Mende_Kikakui", "Merc", "Meroitic_Cursive",
	"Mero", "Meroitic_Hieroglyphs", "Mlym", "Malayalam", "Modi", "Mong",
	"Mongolian", "Mroo", "Mro", "Mtei", "Meetei_Mayek", "Mult", "Multani",
	"Mymr", "Myanmar", "Nagm", "Nag_Mundari", "Nand", "Nandinagari",
	"Narb", "Old_North_Arabian", "Nbat", "Nabataean", "Newa", "Nkoo",
	"Nko", "Nshu", "Nushu", "Ogam", "Ogham", "Olck", "Ol_Chiki", "Orkh",
	"Old_Turkic", "Orya", "Oriya", "Osge", "Osage", "Osma", "Osmanya",
	"Ougr", "Old_Uyghur", "Palm", "Palmyrene", "Pauc", "Pau_Cin_Hau",
	"Perm", "Old_Permic", "Phag", "Phags_Pa", "Phli",
	"Inscriptional_Pahlavi", "Phlp", "Psalter_Pahlavi", "Phnx",
	"Phoenician", "Plrd", "Miao", "Prti", "Inscriptional_Parthian",
	"Rjng", "Rejang", "Rohg", "Hanifi_Rohingya", "Runr", "Runic", "Samr",
	"Samaritan", "Sarb", "Old_South_Arabian", "Saur", "Saurashtra",
	"Sgnw", "SignWriting", "Shaw", "Shavian", "Shrd", "Sharada", "Sidd",
	"Siddham", "Sind", "Khudawadi", "Sinh", "Sinhala", "Sogd", "Sogdian",
	"Sogo", "Old_Sogdian", "Sora", "Sora_Sompeng", "Soyo", "Soyombo",
	"Sund", "Sundanese", "Sylo", "Syloti_Nagri", "Syrc", "Syriac",
	"Tagb", "Tagbanwa", "Takr", "Takri", "Tale", "Tai_Le", "Talu",
	"New_Tai_Lue", "Taml", "Tamil", "Tang", "Tangut", "Tavt", "Tai_Viet",
	"Telu", "Telugu", "Tfng", "Tifinagh", "Tglg", "Tagalog", "Thaa",
	"Thaana", "Thai", "Tibt", "Tibetan", "Tirh", "Tirhuta", "Tnsa",
	"Tangsa", "Toto", "Ugar", "Ugaritic", "Vaii", "Vai", "Vith",
	"Vithkuqi", "Wara", "Warang_Citi", "Wcho", "Wancho", "Xpeo",
	"Old_Persian", "Xsux", "Cuneiform", "Yezi", "Yezidi", "Yiii", "Yi",
	"Zanb", "Zanabazar_Square", "Zinh", "Inherited", "Qaai", "Zyyy",
	"Common", "Zzzz", "Unknown",
};

static const std::unordered_map<std::string,
                                const std::unordered_set<std::string>*>
valuesOfNonBinaryUnicodeProperties = [] {
	static const std::unordered_set<std::string> generalCategory = {
		"C", "Other", "Cc", "Control", "cntrl", "Cf", "Format", "Cn",
		"Unassigned", "Co", "Private_Use", "Cs", "Surrogate", "L",
		"Letter", "LC", "Cased_Letter", "Ll", "Lowercase_Letter", "Lm",
		"Modifier_Letter", "Lo", "Other_Letter", "Lt",
		"Titlecase_Letter", "Lu", "Uppercase_Letter", "M", "Mark",
		"Combining_Mark", "Mc", "Spacing_Mark", "Me", "Enclosing_Mark",
		"Mn", "Nonspacing_Mark", "N", "Number", "Nd", "Decimal_Number",
		"digit", "Nl", "Letter_Number", "No", "Other_Number", "P",
		"Punctuation", "punct", "Pc", "Connector_Punctuation", "Pd",
		"Dash_Punctuation", "Pe", "Close_Punctuation", "Pf",
		"Final_Punctuation", "Pi", "Initial_Punctuation", "Po",
		"Other_Punctuation", "Ps", "Open_Punctuation", "S", "Symbol",
		"Sc", "Currency_Symbol", "Sk", "Modifier_Symbol", "Sm",
		"Math_Symbol", "So", "Other_Symbol", "Z", "Separator", "Zl",
		"Line_Separator", "Zp", "Paragraph_Separator", "Zs",
		"Space_Separator",
	};
	return std::unordered_map<std::string,
	                          const std::unordered_set<std::string>*>{
		{"General_Category", &generalCategory},
		{"Script", &scriptValues},
		{"Script_Extensions", &scriptValues},
	};
}();

// ---------------------------------------------------------------------------
// regExpParser
// ---------------------------------------------------------------------------

namespace detail {

enum class ClassSetExpressionType {
	Unknown,
	ClassUnion,
	ClassIntersection,
	ClassSubtraction,
};

struct GroupNameReference {
	int pos;
	int end;
	std::string name;
};

struct DecimalEscapeValue {
	int pos;
	int end;
	int value;
};

struct RegExpParser {
	Scanner& s;
	int end;
	uint32_t regExpFlags;
	bool anyUnicodeMode;
	bool unicodeSetsMode;
	bool annexB;
	bool anyUnicodeModeOrNonAnnexB = false;
	bool namedCaptureGroups;
	bool mayContainStrings = false;
	int numberOfCapturingGroups = 0;
	std::unordered_map<std::string, bool> groupSpecifiers;
	std::vector<GroupNameReference> groupNameReferences;
	std::vector<DecimalEscapeValue> decimalEscapes;
	std::vector<std::unordered_map<std::string, bool>> namedCapturingGroups;
	char32_t pendingLowSurrogate = 0;

	int pos() const { return s.st.pos; }
	void incPos(int n) { s.st.pos += n; }
	char32_t char_() const { return s.char_(); }
	char32_t charAt(int p) const { return s.charAt(p - pos()); }
	void error(const DiagnosticMessage* d, int p, int length,
	           std::initializer_list<std::string> args = {}) {
		s.errorAt(d, p, length, args);
	}
	void error(const DiagnosticMessage* d, int p, int length,
	           const std::string& arg) {
		s.errorAt(d, p, length, {arg});
	}
	void error(const DiagnosticMessage* d, int p, int length, int arg) {
		s.errorAt(d, p, length, {std::to_string(arg)});
	}
	std::string_view text() const { return s.text; }

	void run();
	void scanDisjunction(bool isInGroup);
	void scanAlternative(bool isInGroup);
	uint32_t scanPatternModifiers(uint32_t currFlags);
	void scanAtomEscape();
	bool scanDecimalEscape();
	std::string scanCharacterEscape(bool atomEscape);
	void scanGroupName(bool isReference);
	bool namedCapturingGroupsContains(const std::string& name);
	bool isClassContentExit(char32_t ch) const {
		return ch == ']' || pos() >= end;
	}
	void scanClassRanges();
	void scanClassSetExpression();
	void scanClassSetSubExpression(ClassSetExpressionType expressionType);
	std::string scanClassSetOperand();
	void scanClassStringDisjunctionContents();
	std::string scanClassSetCharacter();
	std::string scanClassAtom();
	bool scanCharacterClassEscape();
	std::string getSpellingSuggestionForUnicodePropertyName(
		const std::string& name);
	std::string getSpellingSuggestionForUnicodePropertyValue(
		const std::string& propertyName, const std::string& value);
	std::string getSpellingSuggestionForUnicodePropertyNameOrValue(
		const std::string& name);
	std::string scanWordCharacters();
	std::string scanSourceCharacter();
	void scanExpectedChar(char32_t ch);
	void scanDigits();
};

static int compareDecimalStrings(std::string_view a, std::string_view b) {
	while (!a.empty() && a.front() == '0')
		a.remove_prefix(1);
	while (!b.empty() && b.front() == '0')
		b.remove_prefix(1);
	if (a.empty())
		a = "0";
	if (b.empty())
		b = "0";
	if (a.size() != b.size())
		return a.size() < b.size() ? -1 : 1;
	return a.compare(b);
}

void RegExpParser::scanDisjunction(bool isInGroup) {
	std::unordered_set<std::string> disjunctionNames;
	for (;;) {
		namedCapturingGroups.emplace_back();
		scanAlternative(isInGroup);
		auto alternativeNames = std::move(namedCapturingGroups.back());
		namedCapturingGroups.pop_back();
		for (auto& name : alternativeNames)
			disjunctionNames.insert(name.first);
		if (char_() != '|')
			break;
		incPos(1);
	}
	if (isInGroup && !namedCapturingGroups.empty()) {
		auto& parentScope = namedCapturingGroups.back();
		for (auto& name : disjunctionNames)
			parentScope[name] = true;
	}
}

void RegExpParser::scanAlternative(bool isInGroup) {
	bool isPreviousTermQuantifiable = false;
	while (pos() < end) {
		int start = pos();
		char32_t ch = char_();
		switch (ch) {
		case '^':
		case '$':
			incPos(1);
			isPreviousTermQuantifiable = false;
			break;
		case '\\':
			incPos(1);
			switch (char_()) {
			case 'b':
			case 'B':
				incPos(1);
				isPreviousTermQuantifiable = false;
				break;
			default:
				scanAtomEscape();
				isPreviousTermQuantifiable = true;
			}
			break;
		case '(':
			incPos(1);
			if (char_() == '?') {
				incPos(1);
				switch (char_()) {
				case '=':
				case '!':
					incPos(1);
					isPreviousTermQuantifiable =
						!anyUnicodeModeOrNonAnnexB;
					break;
				case '<': {
					int groupNameStart = pos();
					incPos(1);
					switch (char_()) {
					case '=':
					case '!':
						incPos(1);
						isPreviousTermQuantifiable = false;
						break;
					default:
						scanGroupName(false);
						scanExpectedChar('>');
						if (s.languageVersion() < ScriptTarget::ES2018) {
							error(
								Named_capturing_groups_are_only_available_when_targeting_ES2018_or_later,
								groupNameStart, pos() - groupNameStart);
						}
						numberOfCapturingGroups++;
						isPreviousTermQuantifiable = true;
					}
					break;
				}
				default: {
					int flagsStart = pos();
					uint32_t setFlags =
						scanPatternModifiers(RegularExpressionFlagsNone);
					if (char_() == '-') {
						incPos(1);
						scanPatternModifiers(setFlags);
						if (pos() == flagsStart + 1) {
							error(
								Subpattern_flags_must_be_present_when_there_is_a_minus_sign,
								flagsStart, pos() - flagsStart);
						}
					}
					if (pos() != flagsStart &&
					    s.languageVersion() < ScriptTarget::ES2025) {
						error(
							Regular_expression_pattern_modifiers_are_only_available_when_targeting_0_or_later,
							flagsStart, pos() - flagsStart,
							scriptTargetLowercase(ScriptTarget::ES2025));
					}
					scanExpectedChar(':');
					isPreviousTermQuantifiable = true;
				}
				}
			} else {
				numberOfCapturingGroups++;
				isPreviousTermQuantifiable = true;
			}
			scanDisjunction(true);
			scanExpectedChar(')');
			break;
		case '{': {
			incPos(1);
			int digitsStart = pos();
			scanDigits();
			std::string minStr = s.st.tokenValue;
			if (!anyUnicodeModeOrNonAnnexB && minStr.empty()) {
				isPreviousTermQuantifiable = true;
				continue;
			}
			if (char_() == ',') {
				incPos(1);
				scanDigits();
				std::string maxStr = s.st.tokenValue;
				if (minStr.empty()) {
					if (!maxStr.empty() || char_() == '}') {
						error(Incomplete_quantifier_Digit_expected,
						      digitsStart, 0);
					} else {
						error(
							Unexpected_0_Did_you_mean_to_escape_it_with_backslash,
							start, 1, utf8String(ch));
						isPreviousTermQuantifiable = true;
						continue;
					}
				} else if (!maxStr.empty()) {
					if (compareDecimalStrings(minStr, maxStr) > 0 &&
					    (anyUnicodeModeOrNonAnnexB || char_() == '}')) {
						error(Numbers_out_of_order_in_quantifier,
						      digitsStart, pos() - digitsStart);
					}
				}
			} else if (minStr.empty()) {
				if (anyUnicodeModeOrNonAnnexB) {
					error(
						Unexpected_0_Did_you_mean_to_escape_it_with_backslash,
						start, 1, utf8String(ch));
				}
				isPreviousTermQuantifiable = true;
				continue;
			}
			if (char_() != '}') {
				if (anyUnicodeModeOrNonAnnexB) {
					error(X_0_expected, pos(), 0, "}");
					incPos(-1);
				} else {
					isPreviousTermQuantifiable = true;
					continue;
				}
			}
			[[fallthrough]];
		}
		case '*':
		case '+':
		case '?':
			incPos(1);
			if (char_() == '?')
				incPos(1);
			if (!isPreviousTermQuantifiable) {
				error(There_is_nothing_available_for_repetition, start,
				      pos() - start);
			}
			isPreviousTermQuantifiable = false;
			break;
		case '.':
			incPos(1);
			isPreviousTermQuantifiable = true;
			break;
		case '[':
			incPos(1);
			if (unicodeSetsMode) {
				scanClassSetExpression();
			} else {
				scanClassRanges();
				pendingLowSurrogate = 0;
			}
			scanExpectedChar(']');
			isPreviousTermQuantifiable = true;
			break;
		case ')':
			if (isInGroup)
				return;
			[[fallthrough]];
		case ']':
		case '}':
			if (anyUnicodeModeOrNonAnnexB || ch == ')') {
				error(
					Unexpected_0_Did_you_mean_to_escape_it_with_backslash,
					pos(), 1, utf8String(ch));
			}
			incPos(1);
			isPreviousTermQuantifiable = true;
			break;
		case '/':
		case '|':
			return;
		default:
			scanSourceCharacter();
			isPreviousTermQuantifiable = true;
		}
	}
}

uint32_t RegExpParser::scanPatternModifiers(uint32_t currFlags) {
	while (pos() < end) {
		int size;
		char32_t ch = decodeUtf8Rune(text().substr(pos()), &size);
		if (ch == kRuneError || !isIdentifierPart(ch))
			break;
		uint32_t flag = charCodeToRegExpFlag(ch);
		if (flag == 0) {
			error(Unknown_regular_expression_flag, pos(), size);
		} else if ((currFlags & flag) != 0) {
			error(Duplicate_regular_expression_flag, pos(), size);
		} else if ((flag & RegularExpressionFlagsModifiers) == 0) {
			error(
				This_regular_expression_flag_cannot_be_toggled_within_a_subpattern,
				pos(), size);
		} else {
			currFlags |= flag;
		}
		incPos(size);
	}
	return currFlags;
}

void RegExpParser::scanAtomEscape() {
	switch (char_()) {
	case 'k':
		incPos(1);
		if (char_() == '<') {
			incPos(1);
			scanGroupName(true);
			scanExpectedChar('>');
		} else if (anyUnicodeModeOrNonAnnexB || namedCaptureGroups) {
			error(
				X_k_must_be_followed_by_a_capturing_group_name_enclosed_in_angle_brackets,
				pos() - 2, 2);
		}
		break;
	case 'q':
		if (unicodeSetsMode) {
			incPos(1);
			error(X_q_is_only_available_inside_character_class, pos() - 2,
			      2);
			return;
		}
		[[fallthrough]];
	default:
		if (!scanCharacterClassEscape() && !scanDecimalEscape()) {
			scanCharacterEscape(true);
		}
	}
}

bool RegExpParser::scanDecimalEscape() {
	char32_t ch = char_();
	if (ch >= '1' && ch <= '9') {
		int start = pos();
		scanDigits();
		int val = std::numeric_limits<int>::max();
		try {
			val = std::stoi(s.st.tokenValue);
		} catch (...) {
		}
		decimalEscapes.push_back({start, pos(), val});
		return true;
	}
	return false;
}

std::string RegExpParser::scanCharacterEscape(bool atomEscape) {
	char32_t ch = char_();
	switch (ch) {
	case static_cast<char32_t>(-1):
	case 0:
		if (pos() >= end) {
			error(Undetermined_character_escape, pos() - 1, 1);
			return "\\";
		}
		break;
	case 'c': {
		incPos(1);
		ch = char_();
		if (isASCIILetter(ch)) {
			incPos(1);
			return std::string(1, static_cast<char>(ch & 0x1f));
		}
		if (anyUnicodeModeOrNonAnnexB) {
			error(X_c_must_be_followed_by_an_ASCII_letter, pos() - 2, 2);
		} else if (atomEscape) {
			incPos(-1);
			return "\\";
		}
		return utf8String(ch);
	}
	case '^':
	case '$':
	case '/':
	case '\\':
	case '.':
	case '*':
	case '+':
	case '?':
	case '(':
	case ')':
	case '[':
	case ']':
	case '{':
	case '}':
	case '|':
		incPos(1);
		return utf8String(ch);
	}
	incPos(-1);  // back up to include the backslash for scanEscapeSequence
	EscapeSequenceScanningFlags flags =
		EscapeSequenceScanningFlags::RegularExpression;
	if (annexB)
		flags |= EscapeSequenceScanningFlags::AnnexB;
	if (anyUnicodeMode)
		flags |= EscapeSequenceScanningFlags::AnyUnicodeMode;
	if (atomEscape)
		flags |= EscapeSequenceScanningFlags::AtomEscape;
	return s.scanEscapeSequence(flags);
}

void RegExpParser::scanGroupName(bool isReference) {
	s.st.tokenStart = pos();
	if (!s.scanIdentifier(0, IdentifierVariant::RegExpGroupName)) {
		error(Expected_a_capturing_group_name, pos(), 0);
	} else if (isReference) {
		groupNameReferences.push_back(
			{s.st.tokenStart, pos(), s.st.tokenValue});
	} else if (namedCapturingGroupsContains(s.st.tokenValue)) {
		error(
			Named_capturing_groups_with_the_same_name_must_be_mutually_exclusive_to_each_other,
			s.st.tokenStart, pos() - s.st.tokenStart);
	} else {
		if (groupSpecifiers.count(s.st.tokenValue) &&
		    groupSpecifiers[s.st.tokenValue] &&
		    s.languageVersion() >= ScriptTarget::ES2018 &&
		    s.languageVersion() < ScriptTarget::ES2025) {
			error(
				Duplicate_named_capturing_groups_are_only_available_when_targeting_0_or_later,
				s.st.tokenStart, pos() - s.st.tokenStart,
				scriptTargetLowercase(ScriptTarget::ES2025));
		}
		if (!namedCapturingGroups.empty())
			namedCapturingGroups.back()[s.st.tokenValue] = true;
		groupSpecifiers[s.st.tokenValue] = true;
	}
}

bool RegExpParser::namedCapturingGroupsContains(const std::string& name) {
	for (auto& group : namedCapturingGroups) {
		if (group.count(name) && group[name])
			return true;
	}
	return false;
}

void RegExpParser::scanClassRanges() {
	pendingLowSurrogate = 0;
	if (char_() == '^')
		incPos(1);
	while (pos() < end) {
		char32_t ch = char_();
		if (isClassContentExit(ch))
			return;
		int minStart = pos();
		std::string minCharacter = scanClassAtom();
		if (char_() == '-') {
			incPos(1);
			ch = char_();
			if (isClassContentExit(ch))
				return;
			if (minCharacter.empty() && anyUnicodeModeOrNonAnnexB) {
				error(
					A_character_class_range_must_not_be_bounded_by_another_character_class,
					minStart, pos() - 1 - minStart);
			}
			int maxStart = pos();
			std::string maxCharacter = scanClassAtom();
			if (maxCharacter.empty() && anyUnicodeModeOrNonAnnexB) {
				error(
					A_character_class_range_must_not_be_bounded_by_another_character_class,
					maxStart, pos() - maxStart);
				continue;
			}
			if (minCharacter.empty())
				continue;
			int minSize, maxSize;
			char32_t minCharacterValue =
				decodeJSStringRune(minCharacter, 0, &minSize);
			char32_t maxCharacterValue =
				decodeJSStringRune(maxCharacter, 0, &maxSize);
			if (static_cast<int>(minCharacter.size()) == minSize &&
			    static_cast<int>(maxCharacter.size()) == maxSize &&
			    minCharacterValue > maxCharacterValue) {
				error(Range_out_of_order_in_character_class, minStart,
				      pos() - minStart);
			}
		}
	}
}

void RegExpParser::scanClassSetExpression() {
	bool isCharacterComplement = false;
	if (char_() == '^') {
		incPos(1);
		isCharacterComplement = true;
	}
	bool expressionMayContainStrings = false;
	char32_t ch = char_();
	if (isClassContentExit(ch))
		return;
	int start = pos();
	std::string operand;
	std::string_view twoChars;
	if (pos() + 1 < end)
		twoChars = text().substr(pos(), 2);
	if (twoChars == "--" || twoChars == "&&") {
		error(Expected_a_class_set_operand, pos(), 0);
		mayContainStrings = false;
	} else {
		operand = scanClassSetOperand();
	}
	switch (char_()) {
	case '-':
		if (pos() + 1 < end && charAt(pos() + 1) == '-') {
			if (isCharacterComplement && mayContainStrings) {
				error(
					Anything_that_would_possibly_match_more_than_a_single_character_is_invalid_inside_a_negated_character_class,
					start, pos() - start);
			}
			expressionMayContainStrings = mayContainStrings;
			scanClassSetSubExpression(
				ClassSetExpressionType::ClassSubtraction);
			mayContainStrings =
				!isCharacterComplement && expressionMayContainStrings;
			return;
		}
		break;
	case '&':
		if (pos() + 1 < end && charAt(pos() + 1) == '&') {
			scanClassSetSubExpression(
				ClassSetExpressionType::ClassIntersection);
			if (isCharacterComplement && mayContainStrings) {
				error(
					Anything_that_would_possibly_match_more_than_a_single_character_is_invalid_inside_a_negated_character_class,
					start, pos() - start);
			}
			expressionMayContainStrings = mayContainStrings;
			mayContainStrings =
				!isCharacterComplement && expressionMayContainStrings;
			return;
		}
		break;
	default:
		if (isCharacterComplement && mayContainStrings) {
			error(
				Anything_that_would_possibly_match_more_than_a_single_character_is_invalid_inside_a_negated_character_class,
				start, pos() - start);
		}
		expressionMayContainStrings = mayContainStrings;
	}
	while (pos() < end) {
		ch = char_();
		switch (ch) {
		case '-':
			incPos(1);
			ch = char_();
			if (isClassContentExit(ch)) {
				mayContainStrings = !isCharacterComplement &&
				                    expressionMayContainStrings;
				return;
			}
			if (ch == '-') {
				incPos(1);
				error(
					Operators_must_not_be_mixed_within_a_character_class_Wrap_it_in_a_nested_class_instead,
					pos() - 2, 2);
				start = pos() - 2;
				operand = std::string(
					text().substr(start, pos() - start));
				continue;
			} else {
				if (operand.empty()) {
					error(
						A_character_class_range_must_not_be_bounded_by_another_character_class,
						start, pos() - 1 - start);
				}
				int secondStart = pos();
				std::string secondOperand = scanClassSetOperand();
				if (isCharacterComplement && mayContainStrings) {
					error(
						Anything_that_would_possibly_match_more_than_a_single_character_is_invalid_inside_a_negated_character_class,
						secondStart, pos() - secondStart);
				}
				expressionMayContainStrings =
					expressionMayContainStrings || mayContainStrings;
				if (secondOperand.empty()) {
					error(
						A_character_class_range_must_not_be_bounded_by_another_character_class,
						secondStart, pos() - secondStart);
				} else if (!operand.empty()) {
					int minSize, maxSize;
					char32_t minCharacterValue =
						decodeJSStringRune(operand, 0, &minSize);
					char32_t maxCharacterValue =
						decodeJSStringRune(secondOperand, 0, &maxSize);
					if (static_cast<int>(operand.size()) == minSize &&
					    static_cast<int>(secondOperand.size()) ==
					        maxSize &&
					    minCharacterValue > maxCharacterValue) {
						error(Range_out_of_order_in_character_class,
						      start, pos() - start);
					}
				}
			}
			break;
		case '&':
			if (pos() + 1 < end && charAt(pos() + 1) == '&') {
				start = pos();
				incPos(2);
				error(
					Operators_must_not_be_mixed_within_a_character_class_Wrap_it_in_a_nested_class_instead,
					pos() - 2, 2);
				if (char_() == '&') {
					error(
						Unexpected_0_Did_you_mean_to_escape_it_with_backslash,
						pos(), 1, utf8String(ch));
					incPos(1);
				}
				operand = std::string(
					text().substr(start, pos() - start));
				continue;
			}
			break;
		}
		if (isClassContentExit(char_()))
			break;
		start = pos();
		twoChars = std::string_view();
		if (pos() + 1 < end)
			twoChars = text().substr(pos(), 2);
		if (twoChars == "--" || twoChars == "&&") {
			error(
				Operators_must_not_be_mixed_within_a_character_class_Wrap_it_in_a_nested_class_instead,
				pos(), 2);
			incPos(2);
			operand =
				std::string(text().substr(start, pos() - start));
		} else {
			operand = scanClassSetOperand();
			if (isCharacterComplement && mayContainStrings) {
				error(
					Anything_that_would_possibly_match_more_than_a_single_character_is_invalid_inside_a_negated_character_class,
					start, pos() - start);
			}
			expressionMayContainStrings =
				expressionMayContainStrings || mayContainStrings;
		}
	}
	mayContainStrings = !isCharacterComplement && expressionMayContainStrings;
}

void RegExpParser::scanClassSetSubExpression(
	ClassSetExpressionType expressionType) {
	bool expressionMayContainStrings = mayContainStrings;
	while (pos() < end) {
		char32_t ch = char_();
		if (isClassContentExit(ch))
			break;
		switch (ch) {
		case '-':
			incPos(1);
			if (char_() == '-') {
				incPos(1);
				if (expressionType !=
				    ClassSetExpressionType::ClassSubtraction) {
					error(
						Operators_must_not_be_mixed_within_a_character_class_Wrap_it_in_a_nested_class_instead,
						pos() - 2, 2);
				}
			} else {
				error(
					Operators_must_not_be_mixed_within_a_character_class_Wrap_it_in_a_nested_class_instead,
					pos() - 1, 1);
			}
			break;
		case '&':
			incPos(1);
			if (char_() == '&') {
				incPos(1);
				if (expressionType !=
				    ClassSetExpressionType::ClassIntersection) {
					error(
						Operators_must_not_be_mixed_within_a_character_class_Wrap_it_in_a_nested_class_instead,
						pos() - 2, 2);
				}
				if (char_() == '&') {
					error(
						Unexpected_0_Did_you_mean_to_escape_it_with_backslash,
						pos(), 1, utf8String(ch));
					incPos(1);
				}
			} else {
				error(
					Unexpected_0_Did_you_mean_to_escape_it_with_backslash,
					pos() - 1, 1, utf8String(ch));
			}
			break;
		default:
			switch (expressionType) {
			case ClassSetExpressionType::ClassSubtraction:
				error(X_0_expected, pos(), 0, "--");
				break;
			case ClassSetExpressionType::ClassIntersection:
				error(X_0_expected, pos(), 0, "&&");
				break;
			default:
				break;
			}
		}
		ch = char_();
		if (isClassContentExit(ch)) {
			error(Expected_a_class_set_operand, pos(), 0);
			break;
		}
		scanClassSetOperand();
		if (expressionType == ClassSetExpressionType::ClassIntersection) {
			expressionMayContainStrings =
				expressionMayContainStrings && mayContainStrings;
		}
	}
	mayContainStrings = expressionMayContainStrings;
}

std::string RegExpParser::scanClassSetOperand() {
	mayContainStrings = false;
	switch (char_()) {
	case '[':
		incPos(1);
		scanClassSetExpression();
		scanExpectedChar(']');
		return "";
	case '\\': {
		incPos(1);
		if (scanCharacterClassEscape()) {
			return "";
		} else if (char_() == 'q') {
			incPos(1);
			if (char_() == '{') {
				incPos(1);
				scanClassStringDisjunctionContents();
				scanExpectedChar('}');
				return "";
			} else {
				error(
					X_q_must_be_followed_by_string_alternatives_enclosed_in_braces,
					pos() - 2, 2);
				return "q";
			}
		}
		incPos(-1);
		break;
	}
	}
	return scanClassSetCharacter();
}

void RegExpParser::scanClassStringDisjunctionContents() {
	int characterCount = 0;
	while (pos() < end) {
		char32_t ch = char_();
		switch (ch) {
		case '}':
			if (characterCount != 1)
				mayContainStrings = true;
			return;
		case '|':
			if (characterCount != 1)
				mayContainStrings = true;
			incPos(1);
			characterCount = 0;
			break;
		default:
			scanClassSetCharacter();
			characterCount++;
		}
	}
}

std::string RegExpParser::scanClassSetCharacter() {
	char32_t ch = char_();
	if (ch == '\\') {
		incPos(1);
		char32_t innerCh = char_();
		switch (innerCh) {
		case 'b':
			incPos(1);
			return "\b";
		case '&':
		case '-':
		case '!':
		case '#':
		case '%':
		case ',':
		case ':':
		case ';':
		case '<':
		case '=':
		case '>':
		case '@':
		case '`':
		case '~':
			incPos(1);
			return utf8String(innerCh);
		default:
			return scanCharacterEscape(false);
		}
	} else if (pos() + 1 < end && ch == charAt(pos() + 1)) {
		switch (ch) {
		case '&':
		case '!':
		case '#':
		case '%':
		case '*':
		case '+':
		case ',':
		case '.':
		case ':':
		case ';':
		case '<':
		case '=':
		case '>':
		case '?':
		case '@':
		case '`':
		case '~': {
			error(
				A_character_class_must_not_contain_a_reserved_double_punctuator_Did_you_mean_to_escape_it_with_backslash,
				pos(), 2);
			incPos(2);
			return std::string(text().substr(pos() - 2, 2));
		}
		}
	}
	switch (ch) {
	case '/':
	case '(':
	case ')':
	case '[':
	case ']':
	case '{':
	case '}':
	case '-':
	case '|':
		error(Unexpected_0_Did_you_mean_to_escape_it_with_backslash, pos(),
		      1, utf8String(ch));
		incPos(1);
		return utf8String(ch);
	}
	return scanSourceCharacter();
}

std::string RegExpParser::scanClassAtom() {
	if (char_() == '\\') {
		incPos(1);
		char32_t ch = char_();
		switch (ch) {
		case 'b':
			incPos(1);
			return "\b";
		case '-':
			incPos(1);
			return utf8String(ch);
		default:
			if (scanCharacterClassEscape())
				return "";
			return scanCharacterEscape(false);
		}
	}
	return scanSourceCharacter();
}

bool RegExpParser::scanCharacterClassEscape() {
	bool isCharacterComplement = false;
	int start = pos() - 1;
	char32_t ch = char_();
	switch (ch) {
	case 'd':
	case 'D':
	case 's':
	case 'S':
	case 'w':
	case 'W':
		incPos(1);
		return true;
	case 'P':
		isCharacterComplement = true;
		[[fallthrough]];
	case 'p': {
		incPos(1);
		if (char_() == '{') {
			incPos(1);
			int propertyNameOrValueStart = pos();
			std::string propertyNameOrValue = scanWordCharacters();
			if (char_() == '=') {
				std::string propertyName;
				if (auto it =
				        nonBinaryUnicodeProperties.find(propertyNameOrValue);
				    it != nonBinaryUnicodeProperties.end())
					propertyName = it->second;
				if (pos() == propertyNameOrValueStart) {
					error(Expected_a_Unicode_property_name, pos(), 0);
				} else if (propertyName.empty()) {
					error(Unknown_Unicode_property_name,
					      propertyNameOrValueStart,
					      pos() - propertyNameOrValueStart);
					std::string suggestion =
						getSpellingSuggestionForUnicodePropertyName(
							propertyNameOrValue);
					if (!suggestion.empty()) {
						error(Did_you_mean_0, propertyNameOrValueStart,
						      pos() - propertyNameOrValueStart,
						      suggestion);
					}
				}
				incPos(1);
				int propertyValueStart = pos();
				std::string propertyValue = scanWordCharacters();
				if (pos() == propertyValueStart) {
					error(Expected_a_Unicode_property_value, pos(), 0);
				} else if (!propertyName.empty()) {
					if (auto vit =
					        valuesOfNonBinaryUnicodeProperties.find(
					            propertyName);
					    vit != valuesOfNonBinaryUnicodeProperties.end() &&
					    vit->second != nullptr &&
					    !vit->second->count(propertyValue)) {
						error(Unknown_Unicode_property_value,
						      propertyValueStart,
						      pos() - propertyValueStart);
						std::string suggestion =
							getSpellingSuggestionForUnicodePropertyValue(
								propertyName, propertyValue);
						if (!suggestion.empty()) {
							error(Did_you_mean_0, propertyValueStart,
							      pos() - propertyValueStart,
							      suggestion);
						}
					}
				}
			} else {
				if (pos() == propertyNameOrValueStart) {
					error(Expected_a_Unicode_property_name_or_value,
					      pos(), 0);
				} else if (binaryUnicodePropertiesOfStrings.count(
				               propertyNameOrValue)) {
					if (!unicodeSetsMode) {
						error(
							Any_Unicode_property_that_would_possibly_match_more_than_a_single_character_is_only_available_when_the_Unicode_Sets_v_flag_is_set,
							propertyNameOrValueStart,
							pos() - propertyNameOrValueStart);
					} else if (isCharacterComplement) {
						error(
							Anything_that_would_possibly_match_more_than_a_single_character_is_invalid_inside_a_negated_character_class,
							propertyNameOrValueStart,
							pos() - propertyNameOrValueStart);
					} else {
						mayContainStrings = true;
					}
				} else if (!valuesOfNonBinaryUnicodeProperties
				                .at("General_Category")
				                ->count(propertyNameOrValue) &&
				           !binaryUnicodeProperties.count(
				               propertyNameOrValue)) {
					error(Unknown_Unicode_property_name_or_value,
					      propertyNameOrValueStart,
					      pos() - propertyNameOrValueStart);
					std::string suggestion =
						getSpellingSuggestionForUnicodePropertyNameOrValue(
							propertyNameOrValue);
					if (!suggestion.empty()) {
						error(Did_you_mean_0, propertyNameOrValueStart,
						      pos() - propertyNameOrValueStart,
						      suggestion);
					}
				}
			}
			scanExpectedChar('}');
			if (!anyUnicodeMode) {
				error(
					Unicode_property_value_expressions_are_only_available_when_the_Unicode_u_flag_or_the_Unicode_Sets_v_flag_is_set,
					start, pos() - start);
			}
		} else if (anyUnicodeModeOrNonAnnexB) {
			error(
				X_0_must_be_followed_by_a_Unicode_property_value_expression_enclosed_in_braces,
				pos() - 2, 2, utf8String(ch));
		} else {
			incPos(-1);
			return false;
		}
		return true;
	}
	}
	return false;
}

std::string RegExpParser::getSpellingSuggestionForUnicodePropertyName(
	const std::string& name) {
	static const std::vector<std::string> keys = [] {
		std::vector<std::string> v;
		v.reserve(nonBinaryUnicodeProperties.size());
		for (auto& [k, _] : nonBinaryUnicodeProperties)
			v.push_back(k);
		return v;
	}();
	return getSpellingSuggestionForStrings(name, keys.begin(), keys.end());
}

std::string RegExpParser::getSpellingSuggestionForUnicodePropertyValue(
	const std::string& propertyName, const std::string& value) {
	auto it = valuesOfNonBinaryUnicodeProperties.find(propertyName);
	if (it == valuesOfNonBinaryUnicodeProperties.end() ||
	    it->second == nullptr)
		return "";
	return getSpellingSuggestionForStrings(value, it->second->begin(),
	                                     it->second->end());
}

std::string
RegExpParser::getSpellingSuggestionForUnicodePropertyNameOrValue(
	const std::string& name) {
	static const std::vector<std::string> keys = [] {
		std::vector<std::string> v;
		for (auto& k :
		     *valuesOfNonBinaryUnicodeProperties.at("General_Category"))
			v.push_back(k);
		for (auto& k : binaryUnicodeProperties)
			v.push_back(k);
		for (auto& k : binaryUnicodePropertiesOfStrings)
			v.push_back(k);
		return v;
	}();
	return getSpellingSuggestionForStrings(name, keys.begin(), keys.end());
}

std::string RegExpParser::scanWordCharacters() {
	int start = pos();
	while (pos() < end) {
		char32_t ch = char_();
		if (!isWordCharacter(ch))
			break;
		incPos(1);
	}
	return std::string(text().substr(start, pos() - start));
}

std::string RegExpParser::scanSourceCharacter() {
	if (pos() >= end)
		return "";
	if (!anyUnicodeMode) {
		if (pendingLowSurrogate != 0) {
			int size;
			decodeUtf8Rune(text().substr(pos()), &size);
			incPos(size);
			char32_t low = pendingLowSurrogate;
			pendingLowSurrogate = 0;
			char buf[4];
			int n = encodeJSStringRune(low, buf);
			return std::string(buf, n);
		}
		int size;
		char32_t ch = decodeUtf8Rune(text().substr(pos()), &size);
		if (ch == kRuneError || size == 0) {
			incPos(1);
			return std::string(1, text()[pos() - 1]);
		}
		if (ch > 0xFFFF) {
			char32_t high, low;
			codePointToSurrogatePair(ch, high, low);
			pendingLowSurrogate = low;
			char buf[4];
			int n = encodeJSStringRune(high, buf);
			return std::string(buf, n);
		}
		incPos(size);
		return utf8String(ch);
	}
	int size;
	char32_t ch = decodeUtf8Rune(text().substr(pos()), &size);
	if (size == 0)
		return "";
	if (ch == kRuneError) {
		incPos(size);
		return "";
	}
	incPos(size);
	return utf8String(ch);
}

void RegExpParser::scanExpectedChar(char32_t ch) {
	if (char_() == ch) {
		incPos(1);
	} else {
		error(X_0_expected, pos(), 0, utf8String(ch));
	}
}

void RegExpParser::scanDigits() {
	int start = pos();
	while (pos() < end && isDigit(char_()))
		incPos(1);
	s.st.tokenValue = std::string(text().substr(start, pos() - start));
}

void RegExpParser::run() {
	anyUnicodeModeOrNonAnnexB = anyUnicodeMode || !annexB;
	scanDisjunction(false);
	for (auto& reference : groupNameReferences) {
		if (!groupSpecifiers.count(reference.name) ||
		    !groupSpecifiers[reference.name]) {
			error(
				There_is_no_capturing_group_named_0_in_this_regular_expression,
				reference.pos, reference.end - reference.pos,
				reference.name);
			if (!groupSpecifiers.empty()) {
				std::vector<std::string> keys;
				keys.reserve(groupSpecifiers.size());
				for (auto& [k, _] : groupSpecifiers)
					keys.push_back(k);
				std::string suggestion = getSpellingSuggestionForStrings(
					reference.name, keys.begin(), keys.end());
				if (!suggestion.empty()) {
					error(Did_you_mean_0, reference.pos,
					      reference.end - reference.pos, suggestion);
				}
			}
		}
	}
	for (auto& escape : decimalEscapes) {
		if (escape.value > numberOfCapturingGroups) {
			if (numberOfCapturingGroups > 0) {
				error(
					This_backreference_refers_to_a_group_that_does_not_exist_There_are_only_0_capturing_groups_in_this_regular_expression,
					escape.pos, escape.end - escape.pos,
					numberOfCapturingGroups);
			} else {
				error(
					This_backreference_refers_to_a_group_that_does_not_exist_There_are_no_capturing_groups_in_this_regular_expression,
					escape.pos, escape.end - escape.pos);
			}
		}
	}
}

}  // namespace detail

void runRegExpValidator(Scanner& s, int /*startPos*/, int endPos,
                        uint32_t regExpFlags, bool namedCaptureGroups) {
	// Caller has already saved s.end and s.st.pos.
	s.end = endPos;
	detail::RegExpParser p{s,
	               endPos,
	               regExpFlags,
	               (regExpFlags & RegularExpressionFlagsAnyUnicodeMode) != 0,
	               (regExpFlags & RegularExpressionFlagsUnicodeSets) != 0,
	               /*annexB*/ true,
	               false,
	               namedCaptureGroups,
	               false,
	               0,
	               {},
	               {},
	               {},
	               {},
	               0};
	p.run();
}

}  // namespace tsc
