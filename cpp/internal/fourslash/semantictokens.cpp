// semantictokens.go — semantic token verification for fourslash tests.
// Port of tsc/internal/fourslash/semantictokens.go (package fourslash).
#include "internal/fourslash/fourslash.h"

namespace tsc::fourslash {

// VerifySemanticTokens — semantictokens.go:17.
void FourslashTest::VerifySemanticTokens(
    gostd::testing::T* t,
    const std::vector<SemanticToken>& expected) {
	t->Helper();

	auto params = std::make_shared<lsproto::SemanticTokensParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);

	auto result = sendRequest(
	    t, lsproto::TextDocumentSemanticTokensFullInfo, params);

	if (result.SemanticTokens == nullptr) {
		if (expected.empty()) {
			return;
		}
		t->Fatal(
		    {std::string("Expected semantic tokens but got nil")});
	}

	// Decode the semantic tokens using token types/modifiers from the
	// test configuration
	auto actual = decodeSemanticTokens(
	    this, sliceOr(result.SemanticTokens->Data), semanticTokenTypes,
	    semanticTokenModifiers);

	// Compare with expected
	if (actual.size() != expected.size()) {
		t->Fatalf(
		    "Expected %d semantic tokens, got %d\n\nExpected:\n%s\n\n"
		    "Actual:\n%s",
		    {expected.size(), actual.size(),
		     formatSemanticTokens(expected),
		     formatSemanticTokens(actual)});
	}

	for (size_t i = 0; i < expected.size(); i++) {
		auto& exp = expected[i];
		auto& act = actual[i];
		if (exp.Type != act.Type || exp.Text != act.Text) {
			t->Errorf(
			    "Token %d mismatch:\n  Expected: {Type: %q, Text: "
			    "%q}\n  Actual:   {Type: %q, Text: %q}",
			    {i, exp.Type, exp.Text, act.Type, act.Text});
		}
	}
}

// decodeSemanticTokens — semantictokens.go:55.
std::vector<SemanticToken> decodeSemanticTokens(
    FourslashTest* f, const std::vector<uint32_t>& data,
    const std::vector<std::string>& tokenTypes,
    const std::vector<std::string>& tokenModifiers) {
	if (data.size() % 5 != 0) {
		TSC_UNREACHABLE(
		    gostd::sprintf(
		        "Invalid semantic tokens data length: %d",
		        {data.size()})
		        .c_str());
	}

	auto& scriptInfo = f->scriptInfos[f->activeFilename];
	auto converters = newTestConverters(
	    lsproto::PositionEncodingKindUTF8,
	    [scriptInfo](const std::string&) -> lsconv::LSPLineMap* {
		    return scriptInfo->lineMap;
	    });

	std::vector<SemanticToken> tokens;
	uint32_t prevLine = 0;
	uint32_t prevChar = 0;

	for (size_t i = 0; i < data.size(); i += 5) {
		uint32_t deltaLine = data[i];
		uint32_t deltaChar = data[i + 1];
		uint32_t length = data[i + 2];
		uint32_t tokenTypeIdx = data[i + 3];
		uint32_t tokenModifierMask = data[i + 4];

		// Calculate absolute position
		uint32_t line = prevLine + deltaLine;
		uint32_t char_;
		if (deltaLine == 0) {
			char_ = prevChar + deltaChar;
		} else {
			char_ = deltaChar;
		}

		// Get token type
		if (tokenTypeIdx >= tokenTypes.size()) {
			TSC_UNREACHABLE(
			    gostd::sprintf(
			        "Token type index out of range: %d",
			        {tokenTypeIdx})
			        .c_str());
		}
		auto& tokenType = tokenTypes[tokenTypeIdx];

		// Get modifiers
		std::vector<std::string> modifiers;
		for (size_t j = 0; j < tokenModifiers.size(); j++) {
			if (tokenModifierMask & (1u << j)) {
				modifiers.push_back(tokenModifiers[j]);
			}
		}

		// Build full type string (type.modifier1.modifier2)
		auto typeStr = tokenType;
		if (!modifiers.empty()) {
			typeStr = typeStr + "." + gostr::join(modifiers, ".");
		}

		// Get the text
		lsproto::Position startPos{line, char_};
		lsproto::Position endPos{line, char_ + length};
		auto startOffset =
		    converters->LineAndCharacterToPosition(
		        scriptInfo.get(), startPos);
		auto endOffset = converters->LineAndCharacterToPosition(
		    scriptInfo.get(), endPos);
		auto text = scriptInfo->content.substr(
		    (size_t)startOffset, (size_t)(endOffset - startOffset));

		tokens.push_back(SemanticToken{
		    .Type = typeStr,
		    .Text = text,
		});

		prevLine = line;
		prevChar = char_;
	}

	return tokens;
}

// formatSemanticTokens — semantictokens.go:124.
std::string formatSemanticTokens(
    const std::vector<SemanticToken>& tokens) {
	std::vector<std::string> lines;
	for (size_t i = 0; i < tokens.size(); i++) {
		auto& tok = tokens[i];
		lines.push_back(gostd::sprintf(
		    "  [%d] {Type: %q, Text: %q}",
		    {i, tok.Type, tok.Text}));
	}
	return gostr::join(lines, "\n");
}

} // namespace tsc::fourslash
