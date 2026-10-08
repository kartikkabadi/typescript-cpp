// displaypartswriter.go — EmitTextWriter that also collects classified text
// runs for VS colorized signature-help labels.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls {

// newDisplayPartsWriter — displaypartswriter.go:26.
displayPartsWriter* newDisplayPartsWriter(bool vsCapability) {
	auto* w = new displayPartsWriter();
	w->vsCapability = vsCapability;
	return w;
}

// addRun — displaypartswriter.go:30.
void displayPartsWriter::addRun(lsproto::ClassificationTypeName classification,
                                const std::string& text) {
	if (text == "") {
		return;
	}
	if (vsCapability) {
		auto run = std::make_shared<lsproto::VSClassifiedTextRun>();
		run->ClassificationTypeName = classification;
		run->Text = text;
		runs.push_back(std::move(run));
	}
	lastWritten = text;
	builder += text;
}

// WriteClassified — displaypartswriter.go:45. Writes text with an explicit
// classification type.
void displayPartsWriter::WriteClassified(
    const std::string& text,
    lsproto::ClassificationTypeName classification) {
	addRun(classification, text);
}

// WriteFrom — displaypartswriter.go:50. Copies the accumulated content from
// another displayPartsWriter.
void displayPartsWriter::WriteFrom(displayPartsWriter* other) {
	builder += other->String();
	if (vsCapability) {
		if (auto otherRuns = other->GetRuns(); otherRuns.has_value()) {
			for (auto& run : *otherRuns) {
				runs.push_back(run);
			}
		}
	}
	if (other->lastWritten != "") {
		lastWritten = other->lastWritten;
	}
}

// GetRuns — displaypartswriter.go:60.
lsproto::Slice<std::shared_ptr<lsproto::VSClassifiedTextRun>>
displayPartsWriter::GetRuns()
    const {
	return runs;
}

// String — displaypartswriter.go:64.
std::string displayPartsWriter::String() {
	return builder;
}

// Clear — displaypartswriter.go:68.
void displayPartsWriter::Clear() {
	lastWritten = "";
	builder.clear();
	runs.clear();
}

// DecreaseIndent — displaypartswriter.go:74.
void displayPartsWriter::DecreaseIndent() {}

// GetColumn — displaypartswriter.go:76.
TextPos displayPartsWriter::GetColumn() {
	return 0;
}

// GetIndent — displaypartswriter.go:78.
int displayPartsWriter::GetIndent() {
	return 0;
}

// GetLine — displaypartswriter.go:80.
int displayPartsWriter::GetLine() {
	return 0;
}

// GetTextPos — displaypartswriter.go:82.
int displayPartsWriter::GetTextPos() {
	return (int)builder.size();
}

// HasTrailingComment — displaypartswriter.go:86.
bool displayPartsWriter::HasTrailingComment() {
	return false;
}

// HasTrailingWhitespace — displaypartswriter.go:88.
bool displayPartsWriter::HasTrailingWhitespace() {
	if (builder.size() == 0) {
		return false;
	}
	// utf8.DecodeLastRuneInString(lastWritten): find the last rune boundary.
	std::string_view s = lastWritten;
	size_t start = s.size();
	while (start > 0 && (static_cast<unsigned char>(s[start - 1]) & 0xC0) == 0x80) {
		start--;
	}
	if (start == 0) {
		return false;
	}
	int w = 0;
	char32_t ch = decodeUtf8Rune(s.substr(start), &w);
	if (ch == 0xFFFD) { // utf8.RuneError
		return false;
	}
	return isWhiteSpaceLike(ch);
}

// IncreaseIndent — displaypartswriter.go:99.
void displayPartsWriter::IncreaseIndent() {}

// IsAtStartOfLine — displaypartswriter.go:101.
bool displayPartsWriter::IsAtStartOfLine() {
	return false;
}

// RawWrite — displaypartswriter.go:103.
void displayPartsWriter::RawWrite(const std::string& s) {
	addRun(lsproto::ClassificationTypeNameText, s);
}

// Write — displaypartswriter.go:107.
void displayPartsWriter::Write(const std::string& s) {
	addRun(lsproto::ClassificationTypeNameText, s);
}

// WriteComment — displaypartswriter.go:111. Strada's writeComment uses
// unknownWrite → SymbolDisplayPartKind.text → "text".
void displayPartsWriter::WriteComment(const std::string& text) {
	addRun(lsproto::ClassificationTypeNameText, text);
}

// WriteKeyword — displaypartswriter.go:116.
void displayPartsWriter::WriteKeyword(const std::string& text) {
	addRun(lsproto::ClassificationTypeNameKeyword, text);
}

// WriteLine — displaypartswriter.go:120.
void displayPartsWriter::WriteLine() {
	addRun(lsproto::ClassificationTypeNameWhiteSpace, " ");
}

// WriteLineForce — displaypartswriter.go:124.
void displayPartsWriter::WriteLineForce(bool force) {
	addRun(lsproto::ClassificationTypeNameWhiteSpace, " ");
}

// WriteLiteral — displaypartswriter.go:128. Strada's writeLiteral →
// SymbolDisplayPartKind.stringLiteral → "string".
void displayPartsWriter::WriteLiteral(const std::string& s) {
	addRun(lsproto::ClassificationTypeNameString, s);
}

// WriteOperator — displaypartswriter.go:133.
void displayPartsWriter::WriteOperator(const std::string& text) {
	addRun(lsproto::ClassificationTypeNameOperator, text);
}

// WriteParameter — displaypartswriter.go:137.
void displayPartsWriter::WriteParameter(const std::string& text) {
	addRun(lsproto::ClassificationTypeNameParameterName, text);
}

// WriteProperty — displaypartswriter.go:141.
void displayPartsWriter::WriteProperty(const std::string& text) {
	addRun(lsproto::ClassificationTypeNamePropertyName, text);
}

// WritePunctuation — displaypartswriter.go:145.
void displayPartsWriter::WritePunctuation(const std::string& text) {
	addRun(lsproto::ClassificationTypeNamePunctuation, text);
}

// WriteSpace — displaypartswriter.go:149.
void displayPartsWriter::WriteSpace(const std::string& text) {
	addRun(lsproto::ClassificationTypeNameWhiteSpace, text);
}

// WriteStringLiteral — displaypartswriter.go:153.
void displayPartsWriter::WriteStringLiteral(const std::string& text) {
	addRun(lsproto::ClassificationTypeNameString, text);
}

// WriteSymbol — displaypartswriter.go:157.
void displayPartsWriter::WriteSymbol(const std::string& text,
                                     Symbol* symbol) {
	auto classification = classificationForSymbol(symbol);
	addRun(classification, text);
}

// WriteTrailingSemicolon — displaypartswriter.go:162.
void displayPartsWriter::WriteTrailingSemicolon(const std::string& text) {
	addRun(lsproto::ClassificationTypeNamePunctuation, text);
}

// classificationForSymbol — displaypartswriter.go:168. Determines the Roslyn
// classification type name based on a symbol's flags. Matches the Strada
// translation chain: displayPartKind() → GetClassificationName().
lsproto::ClassificationTypeName classificationForSymbol(Symbol* symbol) {
	if (symbol == nullptr) {
		return lsproto::ClassificationTypeNameText;
	}
	auto flags = symbol->flags;
	if (flags & SymbolFlagsVariable) {
		if (isFirstDeclarationOfSymbolParameter(symbol)) {
			return lsproto::ClassificationTypeNameParameterName;
		}
		return lsproto::ClassificationTypeNameLocalName;
	}
	if (flags & SymbolFlagsProperty) {
		return lsproto::ClassificationTypeNamePropertyName;
	}
	if (flags & SymbolFlagsGetAccessor) {
		return lsproto::ClassificationTypeNamePropertyName;
	}
	if (flags & SymbolFlagsSetAccessor) {
		return lsproto::ClassificationTypeNamePropertyName;
	}
	if (flags & SymbolFlagsEnumMember) {
		return lsproto::ClassificationTypeNameFieldName;
	}
	if (flags & SymbolFlagsFunction) {
		return lsproto::ClassificationTypeNameMethodName;
	}
	if (flags & SymbolFlagsClass) {
		return lsproto::ClassificationTypeNameClassName;
	}
	if (flags & SymbolFlagsInterface) {
		return lsproto::ClassificationTypeNameInterfaceName;
	}
	if (flags & SymbolFlagsEnum) {
		return lsproto::ClassificationTypeNameEnumName;
	}
	if (flags & SymbolFlagsModule) {
		return lsproto::ClassificationTypeNameModuleName;
	}
	if (flags & SymbolFlagsMethod) {
		return lsproto::ClassificationTypeNameMethodName;
	}
	if (flags & SymbolFlagsTypeParameter) {
		return lsproto::ClassificationTypeNameTypeParameterName;
	}
	if (flags & SymbolFlagsTypeAlias) {
		return lsproto::ClassificationTypeNameIdentifier;
	}
	if (flags & SymbolFlagsAlias) {
		return lsproto::ClassificationTypeNameIdentifier;
	}
	return lsproto::ClassificationTypeNameText;
}

// isFirstDeclarationOfSymbolParameter — displaypartswriter.go:211. Checks if
// the symbol's first declaration is a parameter.
bool isFirstDeclarationOfSymbolParameter(Symbol* symbol) {
	auto& declarations = symbol->declarations;
	if (declarations.size() == 0) {
		return false;
	}
	return declarations[0]->kind == Kind::Parameter;
}

} // namespace tsc::ls
