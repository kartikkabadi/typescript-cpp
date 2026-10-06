// tests/util/util.go — shared expected-value constants and helpers for
// generated fourslash tests (Go package `fourslash_test`, dot-imported as
// `util`).
// Port of tsc/internal/fourslash/tests/util/util.go.
#pragma once

#include <any>
#include <memory>
#include <string>
#include <vector>

#include "internal/fourslash/fourslash.h"
#include "internal/ls/ls.h"
#include "internal/lsp/lsproto/lsproto.h"

namespace tsc::fourslash::tests::util {

// Ignored — util.go:14 (`struct{}{}` marker for "don't check").
struct IgnoredT {};
inline const IgnoredT Ignored{};

// ptr — Go `&T{...}` address-of-a-composite-literal → heap T* (test-only;
// intentionally leaked, mirrors Go's GC lifetime).
template <typename T>
T* ptr(T v) {
	return new T(std::move(v));
}

// DefaultCommitCharacters — util.go:16.
inline const std::vector<std::string> DefaultCommitCharacters{".", ",",
                                                              ";"};

// InsertReplaceTextEdit — util.go:18.
std::shared_ptr<lsproto::TextEditOrInsertReplaceEdit>
InsertReplaceTextEdit(const std::string& newText,
                      const lsproto::Range& editRange);

// Expected completion items — util.go:28-1351.
extern const std::shared_ptr<lsproto::CompletionItem>
    CompletionGlobalThisItem;
extern const std::shared_ptr<lsproto::CompletionItem>
    CompletionUndefinedVarItem;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobalVars;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobalKeywords;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobalTypeDecls;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionTypeKeywords;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionClassElementKeywords;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionClassElementInJSKeywords;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobals;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobalInJSKeywords;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionConstructorParameterKeywords;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionFunctionMembers;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionFunctionMembersWithPrototype;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobalTypes;
extern const std::vector<fourslash::CompletionsExpectedItem>
    CompletionTypeAssertionKeywords;

// sortCompletionItems — util.go:1364.
std::vector<fourslash::CompletionsExpectedItem> sortCompletionItems(
    const std::vector<fourslash::CompletionsExpectedItem>& items);

// CompletionGlobalsPlus — util.go:1410.
std::vector<fourslash::CompletionsExpectedItem> CompletionGlobalsPlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items,
    bool noLib);

// CompletionGlobalTypesPlus — util.go:1424.
std::vector<fourslash::CompletionsExpectedItem> CompletionGlobalTypesPlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items);

// getInJSKeywords — util.go:1435.
std::vector<fourslash::CompletionsExpectedItem> getInJSKeywords(
    const std::vector<fourslash::CompletionsExpectedItem>& keywords);

// CompletionGlobalsInJSPlus — util.go:1462.
std::vector<fourslash::CompletionsExpectedItem> CompletionGlobalsInJSPlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items,
    bool noLib);

// CompletionFunctionMembersPlus — util.go:1533.
std::vector<fourslash::CompletionsExpectedItem>
CompletionFunctionMembersPlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items);

// CompletionFunctionMembersWithPrototypePlus — util.go:1552.
std::vector<fourslash::CompletionsExpectedItem>
CompletionFunctionMembersWithPrototypePlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items);

// CompletionTypeKeywordsPlus — util.go:1561.
std::vector<fourslash::CompletionsExpectedItem> CompletionTypeKeywordsPlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items);

// ToAny — util.go:1578 (T -> any).
template <typename T>
std::vector<std::any> ToAny(const std::vector<T>& items) {
	return gostr::coreMap(items,
	                      [](const T& item) -> std::any {
		                      return item;
	                      });
}

} // namespace tsc::fourslash::tests::util
