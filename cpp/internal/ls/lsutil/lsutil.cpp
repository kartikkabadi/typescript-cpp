// lsutil — dep-stubs for the ls-autoimport slice. Bodies panic like the
// unported sibling-slice functions they stand in for.
#include "internal/ls/lsutil/lsutil.h"

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/vfs/vfsmatch/vfsmatch.h" // === slice: ls-autoimport ===

namespace tsc::lsutil {

// === dep stubs — removed when owner slice lands ===

Node* getFirstToken(Node*, SourceFile*) {
	TSC_UNREACHABLE("getFirstToken — owned by ls/lsutil");
}

int compareImportsOrRequireStatements(
	Node*, Node*,
	const std::function<int(const std::string&, const std::string&)>&) {
	TSC_UNREACHABLE("compareImportsOrRequireStatements — owned by ls/lsutil");
}

int getImportSpecifierInsertionIndex(
	const std::vector<Node*>&, Node*,
	const std::function<int(Node*, Node*)>&) {
	TSC_UNREACHABLE("getImportSpecifierInsertionIndex — owned by ls/lsutil");
}

int getImportDeclarationInsertIndex(
	const std::vector<Node*>&, Node*,
	const std::function<int(Node*, Node*)>&) {
	TSC_UNREACHABLE("getImportDeclarationInsertIndex — owned by ls/lsutil");
}

std::pair<std::function<int(const std::string&, const std::string&)>, bool>
getOrganizeImportsStringComparerWithDetection(const std::vector<Node*>&,
                                              const UserPreferences&) {
	TSC_UNREACHABLE(
		"getOrganizeImportsStringComparerWithDetection — owned by ls/lsutil");
}

std::pair<std::function<int(Node*, Node*)>, Tristate>
getNamedImportSpecifierComparerWithDetection(Node*, SourceFile*,
                                             const UserPreferences&) {
	TSC_UNREACHABLE(
		"getNamedImportSpecifierComparerWithDetection — owned by ls/lsutil");
}

ScriptElementKind getSymbolKind(checker::Checker*, Symbol*, Node*) {
	TSC_UNREACHABLE("getSymbolKind — owned by ls/lsutil");
}

ScriptElementKindModifier getSymbolModifiers(checker::Checker*, Symbol*) {
	TSC_UNREACHABLE("getSymbolModifiers — owned by ls/lsutil");
}

Tristate shouldUseUriStyleNodeCoreModules(SourceFile*, compiler::SimpleProgram*) {
	TSC_UNREACHABLE("shouldUseUriStyleNodeCoreModules — owned by ls/lsutil");
}

QuotePreference getQuotePreference(SourceFile*, const UserPreferences&) {
	TSC_UNREACHABLE("getQuotePreference — owned by ls/lsutil");
}

std::string moduleSymbolToValidIdentifier(Symbol*, bool) {
	TSC_UNREACHABLE("moduleSymbolToValidIdentifier — owned by ls/lsutil");
}

std::string moduleSpecifierToValidIdentifier(std::string, bool) {
	TSC_UNREACHABLE("moduleSpecifierToValidIdentifier — owned by ls/lsutil");
}

// === slice: ls-autoimport ===
// ParsedAutoImportFileExcludePatterns — userpreferences.go:885.
std::unique_ptr<vfs::vfsmatch::SpecMatcher>
UserPreferences::ParsedAutoImportFileExcludePatterns(
    bool useCaseSensitiveFileNames) const {
	return vfs::vfsmatch::NewSpecMatcher(
	    AutoImportFileExcludePatterns, "", vfs::vfsmatch::Usage::Exclude,
	    useCaseSensitiveFileNames);
}

} // namespace tsc::lsutil
