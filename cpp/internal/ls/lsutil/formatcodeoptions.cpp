// === slice: ls-foundation ===
// lsutil/formatcodeoptions.go — formatting settings types and defaults.

#include "internal/ls/lsutil/lsutil.h"

#include "internal/printer/printer.h"
#include "internal/stringutil/stringutil.h"
#include "internal/ls/lsutil/unicode_tables.h"

namespace tsc::ls::lsutil {

namespace {
// core.BoolToTristate
inline Tristate boolToTristate(bool b) {
	return b ? Tristate::True : Tristate::False;
}

} // namespace

// parseIndentStyle — formatcodeoptions.go:39. IndentStyle is an int enum
// in Go; parsed from a lowercase name, or a number (float64 from JSON, int
// for programmatic callers).
IndentStyle parseIndentStyle(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) {
		std::string s = detail::goToLower(val.s);
		if (s == "none") {
			return IndentStyle::None;
		}
		if (s == "block") {
			return IndentStyle::Block;
		}
		if (s == "smart") {
			return IndentStyle::Smart;
		}
	}
	if (val.is(JsonAny::K::Float)) {
		return static_cast<IndentStyle>(static_cast<int>(val.f));
	}
	if (val.is(JsonAny::K::Int)) {
		return static_cast<IndentStyle>(val.i);
	}
	return IndentStyle::Smart;
}

// parseSemicolonPreference — formatcodeoptions.go:57. SemicolonPreference is a
// string enum in Go ("ignore" | "insert" | "remove"); anything else, including
// non-strings, maps to ignore.
SemicolonPreference parseSemicolonPreference(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) {
		std::string s = detail::goToLower(val.s);
		if (s == "ignore") {
			return SemicolonPreference::Ignore;
		}
		if (s == "insert") {
			return SemicolonPreference::Insert;
		}
		if (s == "remove") {
			return SemicolonPreference::Remove;
		}
	}
	return SemicolonPreference::Ignore;
}

// FromLSFormatOptions — formatcodeoptions.go:105
FormatCodeSettings FromLSFormatOptions(FormatCodeSettings f, const tsc::lsp::lsproto::FormattingOptions& opt) {
	FormatCodeSettings updatedSettings = f;
	updatedSettings.TabSize = static_cast<int>(opt.TabSize);
	updatedSettings.IndentSize = static_cast<int>(opt.TabSize);
	updatedSettings.ConvertTabsToSpaces = boolToTristate(opt.InsertSpaces);
	if (opt.TrimTrailingWhitespace.has_value()) {
		updatedSettings.TrimTrailingWhitespace = boolToTristate(*opt.TrimTrailingWhitespace);
	}
	return updatedSettings;
}

// ToLSFormatOptions — formatcodeoptions.go:116
std::unique_ptr<tsc::lsp::lsproto::FormattingOptions> FormatCodeSettings::ToLSFormatOptions() const {
	bool trimTrailingWhitespace = tristateIsTrue(TrimTrailingWhitespace);
	auto options = std::make_unique<tsc::lsp::lsproto::FormattingOptions>();
	options->TabSize = static_cast<uint32_t>(TabSize);
	options->InsertSpaces = tristateIsTrue(ConvertTabsToSpaces);
	options->TrimTrailingWhitespace = trimTrailingWhitespace;
	return options;
}

// GetDefaultFormatCodeSettings — formatcodeoptions.go:126
FormatCodeSettings GetDefaultFormatCodeSettings() {
	FormatCodeSettings settings;
	settings.IndentSize = tsc::printer::GetDefaultIndentSize();
	settings.TabSize = tsc::printer::GetDefaultIndentSize();
	settings.NewLineCharacter = "\n";
	settings.ConvertTabsToSpaces = Tristate::True;
	settings.IndentStyle = IndentStyleSmart;
	settings.TrimTrailingWhitespace = Tristate::True;
	settings.InsertSpaceAfterConstructor = Tristate::False;
	settings.InsertSpaceAfterCommaDelimiter = Tristate::True;
	settings.InsertSpaceAfterSemicolonInForStatements = Tristate::True;
	settings.InsertSpaceBeforeAndAfterBinaryOperators = Tristate::True;
	settings.InsertSpaceAfterKeywordsInControlFlowStatements = Tristate::True;
	settings.InsertSpaceAfterFunctionKeywordForAnonymousFunctions = Tristate::False;
	settings.InsertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis = Tristate::False;
	settings.InsertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets = Tristate::False;
	settings.InsertSpaceAfterOpeningAndBeforeClosingNonemptyBraces = Tristate::True;
	settings.InsertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces = Tristate::False;
	settings.InsertSpaceAfterOpeningAndBeforeClosingJsxExpressionBraces = Tristate::False;
	settings.InsertSpaceBeforeFunctionParenthesis = Tristate::False;
	settings.PlaceOpenBraceOnNewLineForFunctions = Tristate::False;
	settings.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::False;
	settings.Semicolons = SemicolonPreferenceIgnore;
	settings.IndentSwitchCase = Tristate::True;
	return settings;
}

} // namespace tsc::ls::lsutil

// === dep stubs — removed when owner slice lands ===
// lsproto/util.go — owned by the lsp slice. These are trivial real ports since
// nothing else in this package depends on them crashing.
namespace tsc::lsp::lsproto {

// ComparePositions — util.go:11
int ComparePositions(Position pos, Position other) {
	if (pos.Line != other.Line) {
		return pos.Line < other.Line ? -1 : 1;
	}
	if (pos.Character != other.Character) {
		return pos.Character < other.Character ? -1 : 1;
	}
	return 0;
}

// CompareRanges — util.go:22. Range.Start is compared before Range.End.
int CompareRanges(Range lsRange, Range other) {
	if (int startComp = ComparePositions(lsRange.Start, other.Start); startComp != 0) {
		return startComp;
	}
	return ComparePositions(lsRange.End, other.End);
}

} // namespace tsc::lsp::lsproto
