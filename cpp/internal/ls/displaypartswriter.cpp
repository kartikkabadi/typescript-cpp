// Port of tsc/internal/ls/displaypartswriter.go — displayPartsWriter.
// === slice: ls-coreA ===

#include "internal/ls/ls.h"

#include "internal/stringutil/stringutil.h"

namespace tsc::ls {

// --- displaypartswriter.go:30 addRun ---

void displayPartsWriter::addRun(lsproto::ClassificationTypeName classification,
                                const std::string& text) {
	if (text.empty()) {
		return;
	}
	if (vsCapability_) {
		runs.push_back(new lsproto::VSClassifiedTextRun{
		    .ClassificationTypeName = classification, .Text = text});
	}
	lastWritten = text;
	builder += text;
}

// --- displaypartswriter.go:45 WriteClassified ---

void displayPartsWriter::WriteClassified(
    const std::string& text, lsproto::ClassificationTypeName classification) {
	addRun(classification, text);
}

// --- displaypartswriter.go:50 WriteFrom ---

void displayPartsWriter::WriteFrom(displayPartsWriter* other) {
	builder += other->String();
	if (vsCapability_) {
		auto& otherRuns = other->GetRuns();
		runs.insert(runs.end(), otherRuns.begin(), otherRuns.end());
	}
	if (!other->lastWritten.empty()) {
		lastWritten = other->lastWritten;
	}
}

// --- displaypartswriter.go:88 HasTrailingWhitespace ---

bool displayPartsWriter::HasTrailingWhitespace() {
	if (builder.empty()) {
		return false;
	}
	int width = 0;
	char32_t ch = decodeLastUtf8Rune(lastWritten, &width);
	if (ch == kRuneError) {
		return false;
	}
	return isWhiteSpaceLike(ch);
}

// --- displaypartswriter.go:111 WriteComment ---

void displayPartsWriter::WriteComment(const std::string& text) {
	// Strada's writeComment uses unknownWrite → SymbolDisplayPartKind.text →
	// "text"
	addRun(lsproto::ClassificationTypeNameText, text);
}

// --- displaypartswriter.go:157 WriteSymbol ---

namespace {

// displaypartswriter.go:211 isFirstDeclarationOfSymbolParameter — checks if
// the symbol's first declaration is a parameter.
bool isFirstDeclarationOfSymbolParameter(Symbol* symbol) {
	const std::vector<Node*>& declarations = symbol->declarations;
	if (declarations.empty()) {
		return false;
	}
	return declarations[0]->kind == Kind::Parameter;
}

// displaypartswriter.go:168 classificationForSymbol — determines the Roslyn
// classification type name based on a symbol's flags. Matches the Strada
// translation chain: displayPartKind() → GetClassificationName().
lsproto::ClassificationTypeName classificationForSymbol(Symbol* symbol) {
	if (symbol == nullptr) {
		return lsproto::ClassificationTypeNameText;
	}
	SymbolFlags flags = symbol->flags;
	if ((flags & SymbolFlagsVariable) != 0) {
		if (isFirstDeclarationOfSymbolParameter(symbol)) {
			return lsproto::ClassificationTypeNameParameterName;
		}
		return lsproto::ClassificationTypeNameLocalName;
	}
	if ((flags & SymbolFlagsProperty) != 0) {
		return lsproto::ClassificationTypeNamePropertyName;
	}
	if ((flags & SymbolFlagsGetAccessor) != 0) {
		return lsproto::ClassificationTypeNamePropertyName;
	}
	if ((flags & SymbolFlagsSetAccessor) != 0) {
		return lsproto::ClassificationTypeNamePropertyName;
	}
	if ((flags & SymbolFlagsEnumMember) != 0) {
		return lsproto::ClassificationTypeNameFieldName;
	}
	if ((flags & SymbolFlagsFunction) != 0) {
		return lsproto::ClassificationTypeNameMethodName;
	}
	if ((flags & SymbolFlagsClass) != 0) {
		return lsproto::ClassificationTypeNameClassName;
	}
	if ((flags & SymbolFlagsInterface) != 0) {
		return lsproto::ClassificationTypeNameInterfaceName;
	}
	if ((flags & SymbolFlagsEnum) != 0) {
		return lsproto::ClassificationTypeNameEnumName;
	}
	if ((flags & SymbolFlagsModule) != 0) {
		return lsproto::ClassificationTypeNameModuleName;
	}
	if ((flags & SymbolFlagsMethod) != 0) {
		return lsproto::ClassificationTypeNameMethodName;
	}
	if ((flags & SymbolFlagsTypeParameter) != 0) {
		return lsproto::ClassificationTypeNameTypeParameterName;
	}
	if ((flags & SymbolFlagsTypeAlias) != 0) {
		return lsproto::ClassificationTypeNameIdentifier;
	}
	if ((flags & SymbolFlagsAlias) != 0) {
		return lsproto::ClassificationTypeNameIdentifier;
	}
	return lsproto::ClassificationTypeNameText;
}

} // namespace

void displayPartsWriter::WriteSymbol(const std::string& text, Symbol* symbol) {
	lsproto::ClassificationTypeName classification =
	    classificationForSymbol(symbol);
	addRun(classification, text);
}

} // namespace tsc::ls
