// === slice: ls-foundation ===
// lsutil/utilities.go — misc helpers: semicolon/style detection, node core
// module URI detection, quote preferences, identifier sanitization.

#include "internal/ls/lsutil/lsutil.h"

#include <vector>

#include "internal/astnav/tokens.h"
#include "internal/compiler/program.h"
#include "internal/core/nodemodules.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"
#include "internal/ls/lsutil/unicode_tables.h"

namespace tsc::ls::lsutil {

namespace {
// core.Find
template <class T, class Pred>
T* find(const std::vector<T*>& v, Pred pred) {
	for (T* e : v) {
		if (pred(e)) return e;
	}
	return nullptr;
}

// strings.TrimSuffix
std::string_view removeSuffix(std::string_view s, std::string_view suffix) {
	if (s.ends_with(suffix)) {
		return s.substr(0, s.size() - suffix.size());
	}
	return s;
}

} // namespace

// ProbablyUsesSemicolons — utilities.go:14
bool ProbablyUsesSemicolons(SourceFile* file) {
	int withSemicolon = 0;
	int withoutSemicolon = 0;
	const int nStatementsToObserve = 5;

	std::function<bool(Node*)> visit = [&](Node* node) -> bool {
		if ((node->flags & NodeFlagsReparsed) != 0) {
			return false;
		}
		if (SyntaxRequiresTrailingSemicolonOrASI(node->kind)) {
			Node* lastToken = GetLastToken(node, file);
			if (lastToken != nullptr && lastToken->kind == Kind::SemicolonToken) {
				withSemicolon++;
			} else {
				withoutSemicolon++;
			}
		} else if (SyntaxRequiresTrailingCommaOrSemicolonOrASI(node->kind)) {
			Node* lastToken = GetLastToken(node, file);
			if (lastToken != nullptr && lastToken->kind == Kind::SemicolonToken) {
				withSemicolon++;
			} else if (lastToken != nullptr && lastToken->kind != Kind::CommaToken) {
				TextPos lastTokenLine = getECMALineOfPosition(
					file,
					astnav::getStartOfNode(lastToken, file, false /*includeJSDoc*/));
				TextPos nextTokenLine = getECMALineOfPosition(
					file,
					TextPos(skipTrivia(file->text, lastToken->end())));
				// Avoid counting missing semicolon in single-line objects:
				// `function f(p: { x: string /*no semicolon here is insignificant*/ }) {`
				if (lastTokenLine != nextTokenLine) {
					withoutSemicolon++;
				}
			}
		}

		if (withSemicolon + withoutSemicolon >= nStatementsToObserve) {
			return true;
		}

		return node->forEachChild(visit);
	};

	file->forEachChild(visit);

	// One statement missing a semicolon isn't sufficient evidence to say the user
	// doesn't want semicolons, because they may not even be done writing that statement.
	if (withSemicolon == 0 && withoutSemicolon <= 1) {
		return true;
	}

	// When both kinds of observation exist, treat the file as using semicolons when the
	// ratio withSemicolon/withoutSemicolon exceeds 1/nStatementsToObserve (real arithmetic),
	// implemented as an integer inequality to avoid truncation.
	if (withoutSemicolon == 0) {
		return true;
	}
	return withSemicolon * nStatementsToObserve > withoutSemicolon;
}

// ShouldUseUriStyleNodeCoreModules — utilities.go:71
Tristate ShouldUseUriStyleNodeCoreModules(SourceFile* file, compiler::SimpleProgram* program) {
	for (Node* node : file->imports) {
		if (nodeCoreModules().contains(node->text()) &&
			!ExclusivelyPrefixedNodeCoreModules.contains(node->text())) {
			if (node->text().starts_with("node:")) {
				return Tristate::True;
			} else {
				return Tristate::False;
			}
		}
	}

	return program->UsesUriStyleNodeCoreModules();
}

// QuotePreferenceFromString — utilities.go:87
QuotePreference QuotePreferenceFromString(StringLiteral* str) {
	if ((str->TokenFlags & TokenFlagsSingleQuote) != 0) {
		return QuotePreferenceSingle;
	}
	return QuotePreferenceDouble;
}

// GetQuotePreference — utilities.go:94
QuotePreference GetQuotePreference(SourceFile* sourceFile, const UserPreferences& preferences) {
	if (preferences.QuotePreference != "" && preferences.QuotePreference != "auto") {
		if (preferences.QuotePreference == "single") {
			return QuotePreferenceSingle;
		}
		return QuotePreferenceDouble;
	}
	// ignore synthetic import added when importHelpers: true
	Node* firstModuleSpecifier = find(sourceFile->imports, [](Node* n) {
		return isStringLiteral(n) && !nodeIsSynthesized(n->parent);
	});
	if (firstModuleSpecifier != nullptr) {
		return QuotePreferenceFromString(firstModuleSpecifier->as<StringLiteral>());
	}
	return QuotePreferenceDouble;
}

// ModuleSymbolToValidIdentifier — utilities.go:107
std::string ModuleSymbolToValidIdentifier(Symbol* moduleSymbol, bool forceCapitalize) {
	std::string moduleName = moduleSymbol->data->name;
	if (auto [ambientModuleName, ok] = detail::tryGetAmbientModuleNameFromSymbolName(moduleName); ok) {
		moduleName = ambientModuleName;
	}
	return ModuleSpecifierToValidIdentifier(moduleName, forceCapitalize);
}

// ModuleSpecifierToValidIdentifier — utilities.go:115
std::string ModuleSpecifierToValidIdentifier(std::string_view moduleSpecifier, bool forceCapitalize) {
	std::string_view baseName = tspath::getBaseFileName(
		removeSuffix(tspath::removeAnyFileExtension(moduleSpecifier), "/index"));
	std::u32string res;
	bool lastCharWasValid = true;
	std::u32string baseNameRunes = detail::toRunes(baseName);
	if (!baseNameRunes.empty() && isIdentifierStart(baseNameRunes[0])) {
		if (forceCapitalize) {
			res.push_back(detail::goSimpleUpper(baseNameRunes[0]));
		} else {
			res.push_back(baseNameRunes[0]);
		}
	} else {
		lastCharWasValid = false;
	}

	for (size_t i = 1; i < baseNameRunes.size(); i++) {
		bool isValid = isIdentifierPart(baseNameRunes[i]);
		if (isValid) {
			if (!lastCharWasValid) {
				res.push_back(detail::goSimpleUpper(baseNameRunes[i]));
			} else {
				res.push_back(baseNameRunes[i]);
			}
		}
		lastCharWasValid = isValid;
	}

	// Need `"_"` to ensure result isn't empty.
	std::string resString = detail::fromRunes(res);
	if (!resString.empty() && !IsNonContextualKeyword(stringToToken(resString))) {
		return resString;
	}
	return "_" + resString;
}

// IsNonContextualKeyword — utilities.go:150
bool IsNonContextualKeyword(Kind token) {
	return isKeywordKind(token) && !detail::isContextualKeyword(token);
}

} // namespace tsc::ls::lsutil
