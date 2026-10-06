// tests/util/util.go — shared expected-value constants and helpers for
// generated fourslash tests.
// Port of tsc/internal/fourslash/tests/util/util.go (package
// fourslash_test, namespace `util`).
#include "internal/fourslash/tests/util/util.h"

#include <algorithm>
#include <unordered_set>

#include "internal/stringutil/stringutil.h"

namespace tsc::fourslash::tests::util {

// ===========================================================================
// util.go:14-26 — Ignored / DefaultCommitCharacters / InsertReplaceTextEdit
// ===========================================================================

// InsertReplaceTextEdit — util.go:18.
std::shared_ptr<lsproto::TextEditOrInsertReplaceEdit>
InsertReplaceTextEdit(const std::string& newText,
                      const lsproto::Range& editRange) {
	auto out = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>();
	out->InsertReplaceEdit =
	    std::make_shared<lsproto::InsertReplaceEdit>(
	        lsproto::InsertReplaceEdit{
	            .NewText = newText,
	            .Insert = editRange,
	            .Replace = editRange,
	        });
	return out;
}

// ===========================================================================
// util.go:28-253 — CompletionGlobalThisItem / CompletionUndefinedVarItem /
// CompletionGlobalVars
// ===========================================================================

const std::shared_ptr<lsproto::CompletionItem> CompletionGlobalThisItem =
    std::make_shared<lsproto::CompletionItem>(
        lsproto::CompletionItem{
            .Label = "globalThis",
            .Kind = std::make_shared<lsproto::CompletionItemKind>(
                lsproto::CompletionItemKindModule),
            .SortText = std::string(ls::SortTextGlobalsOrKeywords),
        });

const std::shared_ptr<lsproto::CompletionItem>
    CompletionUndefinedVarItem = std::make_shared<lsproto::CompletionItem>(
        lsproto::CompletionItem{
            .Label = "undefined",
            .Kind = std::make_shared<lsproto::CompletionItemKind>(
                lsproto::CompletionItemKindVariable),
            .SortText = std::string(ls::SortTextGlobalsOrKeywords),
        });

const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobalVars = {
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ArrayBuffer", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Boolean", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "DataView", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Date", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Error", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "EvalError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Float32Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Float64Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Function", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Infinity", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Int16Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Int32Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Int8Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Intl", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindModule), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "JSON", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Math", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "NaN", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Number", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Object", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "RangeError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ReferenceError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "RegExp", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "String", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "SyntaxError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "TypeError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "URIError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint16Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint32Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint8Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint8ClampedArray", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "decodeURI", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "decodeURIComponent", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "encodeURI", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "encodeURIComponent", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "eval", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "isFinite", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "isNaN", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "parseFloat", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "parseInt", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "escape", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .Tags = std::make_shared<lsproto::Slice<lsproto::CompletionItemTag>>(std::vector<lsproto::CompletionItemTag>{lsproto::CompletionItemTagDeprecated}), .SortText = std::string(ls::DeprecateSortText(ls::SortTextGlobalsOrKeywords))}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "unescape", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFunction), .Tags = std::make_shared<lsproto::Slice<lsproto::CompletionItemTag>>(std::vector<lsproto::CompletionItemTag>{lsproto::CompletionItemTagDeprecated}), .SortText = std::string(ls::DeprecateSortText(ls::SortTextGlobalsOrKeywords))}),
	};

const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobalKeywords = {
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "abstract", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "any", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "as", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "asserts", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "async", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "await", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "bigint", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "boolean", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "break", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "case", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "catch", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "class", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "const", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "continue", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "debugger", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "declare", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "default", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "delete", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "do", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "else", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "enum", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "export", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "extends", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "false", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "finally", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "for", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "function", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "if", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "implements", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "import", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "in", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "infer", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "instanceof", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "interface", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "keyof", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "let", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "module", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "namespace", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "never", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "new", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "null", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "number", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "object", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "package", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "readonly", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "return", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "satisfies", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "string", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "super", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "switch", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "symbol", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "this", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "throw", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "true", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "try", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "type", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "typeof", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "unique", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "unknown", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "using", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "var", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "void", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "while", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "with", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "yield", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	};

const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobalTypeDecls = {
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Symbol", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "PropertyKey", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "PropertyDescriptor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "PropertyDescriptorMap", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Object", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ObjectConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Function", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "FunctionConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ThisParameterType", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "OmitThisParameter", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "CallableFunction", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "NewableFunction", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "IArguments", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "String", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "StringConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Boolean", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "BooleanConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Number", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "NumberConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "TemplateStringsArray", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ImportMeta", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ImportCallOptions", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ImportAssertions", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .Tags = std::make_shared<lsproto::Slice<lsproto::CompletionItemTag>>(std::vector<lsproto::CompletionItemTag>{lsproto::CompletionItemTagDeprecated}), .SortText = std::string(ls::DeprecateSortText(ls::SortTextGlobalsOrKeywords))}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ImportAttributes", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Math", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Date", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "DateConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "RegExpMatchArray", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "RegExpExecArray", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "RegExp", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "RegExpConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Error", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ErrorConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "EvalError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "EvalErrorConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "RangeError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "RangeErrorConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ReferenceError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ReferenceErrorConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "SyntaxError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "SyntaxErrorConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "TypeError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "TypeErrorConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "URIError", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "URIErrorConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "JSON", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ReadonlyArray", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ConcatArray", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ArrayConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "TypedPropertyDescriptor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ClassDecorator", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "PropertyDecorator", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "MethodDecorator", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ParameterDecorator", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ClassMemberDecoratorContext", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "DecoratorContext", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "DecoratorMetadata", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "DecoratorMetadataObject", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ClassDecoratorContext", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ClassMethodDecoratorContext", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ClassGetterDecoratorContext", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ClassSetterDecoratorContext", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ClassAccessorDecoratorContext", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ClassAccessorDecoratorTarget", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ClassAccessorDecoratorResult", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ClassFieldDecoratorContext", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "PromiseConstructorLike", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "PromiseLike", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Promise", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Awaited", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ArrayLike", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Partial", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Required", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Readonly", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Pick", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Record", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Exclude", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Extract", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Omit", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "NonNullable", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Parameters", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ConstructorParameters", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ReturnType", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "InstanceType", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uppercase", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Lowercase", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Capitalize", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uncapitalize", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "NoInfer", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ThisType", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ArrayBuffer", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ArrayBufferTypes", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ArrayBufferLike", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ArrayBufferConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "ArrayBufferView", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "DataView", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "DataViewConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Int8Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Int8ArrayConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint8Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint8ArrayConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint8ClampedArray", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint8ClampedArrayConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Int16Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Int16ArrayConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint16Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint16ArrayConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Int32Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Int32ArrayConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint32Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Uint32ArrayConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Float32Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Float32ArrayConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Float64Array", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Float64ArrayConstructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "Intl", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindModule), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "WeakKey", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "WeakKeyTypes", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	};

const std::vector<fourslash::CompletionsExpectedItem>
    CompletionTypeKeywords = {
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "any", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "asserts", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "bigint", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "boolean", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "false", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "infer", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "keyof", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "never", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "null", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "number", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "object", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "readonly", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "string", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "symbol", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "true", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "typeof", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "undefined", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "unique", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "unknown", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "void", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	};

const std::vector<fourslash::CompletionsExpectedItem>
    CompletionClassElementKeywords = {
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "abstract", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "accessor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "async", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "constructor", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "declare", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "get", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "override", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "private", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "protected", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "public", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "readonly", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "set", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "static", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	};

const std::vector<fourslash::CompletionsExpectedItem>
    CompletionConstructorParameterKeywords = {
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "override", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "private", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "protected", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "public", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "readonly", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword), .SortText = std::string(ls::SortTextGlobalsOrKeywords)}),
	};

const std::vector<fourslash::CompletionsExpectedItem>
    CompletionFunctionMembers = {
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "apply", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "arguments", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "bind", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "call", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "caller", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "length", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField)}),
	std::make_shared<lsproto::CompletionItem>(
			lsproto::CompletionItem{.Label = "toString", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)}),
	};

// ===========================================================================
// util.go:1353-1580 — computed globals + helpers
// ===========================================================================

// CompletionClassElementInJSKeywords — util.go:1353.
const std::vector<fourslash::CompletionsExpectedItem>
    CompletionClassElementInJSKeywords =
        getInJSKeywords(CompletionClassElementKeywords);

// CompletionGlobals — util.go:1355.
const std::vector<fourslash::CompletionsExpectedItem> CompletionGlobals =
    [] {
	    std::vector<fourslash::CompletionsExpectedItem> all;
	    all.insert(all.end(), CompletionGlobalVars.begin(),
	               CompletionGlobalVars.end());
	    all.insert(all.end(), CompletionGlobalKeywords.begin(),
	               CompletionGlobalKeywords.end());
	    all.push_back(CompletionGlobalThisItem);
	    all.push_back(CompletionUndefinedVarItem);
	    return sortCompletionItems(all);
    }();

// sortCompletionItems — util.go:1364.
std::vector<fourslash::CompletionsExpectedItem> sortCompletionItems(
    const std::vector<fourslash::CompletionsExpectedItem>& items) {
	auto compareStrings =
	    stringutil::CompareStringsCaseInsensitiveThenSensitive;
	auto sorted = items; // slices.Clone
	std::stable_sort(
	    sorted.begin(), sorted.end(),
	    [compareStrings](const fourslash::CompletionsExpectedItem& a,
	                     const fourslash::CompletionsExpectedItem& b) {
		    std::string defaultSortText =
		        std::string(ls::SortTextLocationPriority);
		    std::string aSortText, bSortText;
		    if (auto* item =
		            std::get_if<
		                std::shared_ptr<lsproto::CompletionItem>>(
		                &a);
		        item != nullptr && *item != nullptr) {
			    if ((*item)->SortText.has_value()) {
				    aSortText = *(*item)->SortText;
			    }
		    }
		    if (auto* item =
		            std::get_if<
		                std::shared_ptr<lsproto::CompletionItem>>(
		                &b);
		        item != nullptr && *item != nullptr) {
			    if ((*item)->SortText.has_value()) {
				    bSortText = *(*item)->SortText;
			    }
		    }
		    if (aSortText.empty()) aSortText = defaultSortText;
		    if (bSortText.empty()) bSortText = defaultSortText;
		    auto bySortText = compareStrings(aSortText, bSortText);
		    if (bySortText != 0) {
			    return bySortText < 0;
		    }
		    std::string aLabel, bLabel;
		    if (auto* item =
		            std::get_if<
		                std::shared_ptr<lsproto::CompletionItem>>(
		                &a);
		        item != nullptr && *item != nullptr) {
			    aLabel = (*item)->Label;
		    } else if (auto* s = std::get_if<std::string>(&a)) {
			    aLabel = *s;
		    } else {
			    TSC_UNREACHABLE(
			        gostd::sprintf(
			            "unexpected completion item type: %T",
			            {gostr::variantTypeName(a)})
			            .c_str());
		    }
		    if (auto* item =
		            std::get_if<
		                std::shared_ptr<lsproto::CompletionItem>>(
		                &b);
		        item != nullptr && *item != nullptr) {
			    bLabel = (*item)->Label;
		    } else if (auto* s = std::get_if<std::string>(&b)) {
			    bLabel = *s;
		    } else {
			    TSC_UNREACHABLE(
			        gostd::sprintf(
			            "unexpected completion item type: %T",
			            {gostr::variantTypeName(b)})
			            .c_str());
		    }
		    return compareStrings(aLabel, bLabel) < 0;
	    });
	return sorted;
}

// CompletionGlobalsPlus — util.go:1410.
std::vector<fourslash::CompletionsExpectedItem> CompletionGlobalsPlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items,
    bool noLib) {
	std::vector<fourslash::CompletionsExpectedItem> all;
	if (noLib) {
		all.insert(all.end(), items.begin(), items.end());
		all.push_back(CompletionGlobalThisItem);
		all.push_back(CompletionUndefinedVarItem);
		all.insert(all.end(), CompletionGlobalKeywords.begin(),
		           CompletionGlobalKeywords.end());
	} else {
		all.insert(all.end(), items.begin(), items.end());
		all.insert(all.end(), CompletionGlobals.begin(),
		           CompletionGlobals.end());
	}
	return sortCompletionItems(all);
}

// CompletionGlobalTypesPlus — util.go:1424.
std::vector<fourslash::CompletionsExpectedItem> CompletionGlobalTypesPlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items) {
	std::vector<fourslash::CompletionsExpectedItem> all;
	all.insert(all.end(), CompletionGlobalTypeDecls.begin(),
	           CompletionGlobalTypeDecls.end());
	all.push_back(CompletionGlobalThisItem);
	all.insert(all.end(), CompletionTypeKeywords.begin(),
	           CompletionTypeKeywords.end());
	all.insert(all.end(), items.begin(), items.end());
	return sortCompletionItems(all);
}

// CompletionGlobalTypes — util.go:1433.
const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobalTypes = CompletionGlobalTypesPlus({});

// getInJSKeywords — util.go:1435.
std::vector<fourslash::CompletionsExpectedItem> getInJSKeywords(
    const std::vector<fourslash::CompletionsExpectedItem>& keywords) {
	return gostr::coreFilter(
	    keywords, [](const fourslash::CompletionsExpectedItem& item) {
		    std::string label;
		    if (auto* ci =
		            std::get_if<
		                std::shared_ptr<lsproto::CompletionItem>>(
		                &item);
		        ci != nullptr && *ci != nullptr) {
			    label = (*ci)->Label;
		    } else if (auto* s = std::get_if<std::string>(&item)) {
			    label = *s;
		    } else {
			    TSC_UNREACHABLE(
			        gostd::sprintf(
			            "unexpected completion item type: %T",
			            {gostr::variantTypeName(item)})
			            .c_str());
		    }
		    static const std::unordered_set<std::string> excluded{
		        "enum",      "interface", "implements", "private",
		        "protected", "public",    "abstract",   "any",
		        "boolean",   "declare",   "infer",      "is",
		        "keyof",     "module",    "namespace",  "never",
		        "readonly",  "number",    "object",     "string",
		        "symbol",    "type",      "unique",     "override",
		        "unknown",   "global",    "bigint",
		    };
		    return excluded.count(label) == 0;
	    });
}

// CompletionGlobalInJSKeywords — util.go:1460.
const std::vector<fourslash::CompletionsExpectedItem>
    CompletionGlobalInJSKeywords =
        getInJSKeywords(CompletionGlobalKeywords);

// CompletionGlobalsInJSPlus — util.go:1462.
std::vector<fourslash::CompletionsExpectedItem> CompletionGlobalsInJSPlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items,
    bool noLib) {
	std::vector<fourslash::CompletionsExpectedItem> all;
	all.insert(all.end(), items.begin(), items.end());
	all.push_back(CompletionGlobalThisItem);
	all.push_back(CompletionUndefinedVarItem);
	all.insert(all.end(), CompletionGlobalInJSKeywords.begin(),
	           CompletionGlobalInJSKeywords.end());
	if (!noLib) {
		all.insert(all.end(), CompletionGlobalVars.begin(),
		           CompletionGlobalVars.end());
	}
	return sortCompletionItems(all);
}

// CompletionFunctionMembersPlus — util.go:1533.
std::vector<fourslash::CompletionsExpectedItem>
CompletionFunctionMembersPlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items) {
	std::vector<fourslash::CompletionsExpectedItem> all;
	all.insert(all.end(), CompletionFunctionMembers.begin(),
	           CompletionFunctionMembers.end());
	all.insert(all.end(), items.begin(), items.end());
	return sortCompletionItems(all);
}

// CompletionFunctionMembersWithPrototype — util.go:1542.
const std::vector<fourslash::CompletionsExpectedItem>
    CompletionFunctionMembersWithPrototype = [] {
	    std::vector<fourslash::CompletionsExpectedItem> all;
	    all.insert(all.end(), CompletionFunctionMembers.begin(),
	               CompletionFunctionMembers.end());
	    all.push_back(std::make_shared<lsproto::CompletionItem>(
	        lsproto::CompletionItem{
	            .Label = "prototype",
	            .Kind = std::make_shared<lsproto::CompletionItemKind>(
	                lsproto::CompletionItemKindField),
	        }));
	    return sortCompletionItems(all);
    }();

// CompletionFunctionMembersWithPrototypePlus — util.go:1552.
std::vector<fourslash::CompletionsExpectedItem>
CompletionFunctionMembersWithPrototypePlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items) {
	std::vector<fourslash::CompletionsExpectedItem> all;
	all.insert(all.end(), CompletionFunctionMembersWithPrototype.begin(),
	           CompletionFunctionMembersWithPrototype.end());
	all.insert(all.end(), items.begin(), items.end());
	return sortCompletionItems(all);
}

// CompletionTypeKeywordsPlus — util.go:1561.
std::vector<fourslash::CompletionsExpectedItem> CompletionTypeKeywordsPlus(
    const std::vector<fourslash::CompletionsExpectedItem>& items) {
	std::vector<fourslash::CompletionsExpectedItem> all;
	all.insert(all.end(), CompletionTypeKeywords.begin(),
	           CompletionTypeKeywords.end());
	all.insert(all.end(), items.begin(), items.end());
	return sortCompletionItems(all);
}

// CompletionTypeAssertionKeywords — util.go:1570.
const std::vector<fourslash::CompletionsExpectedItem>
    CompletionTypeAssertionKeywords =
        CompletionGlobalTypesPlus(std::vector<
                                  fourslash::CompletionsExpectedItem>{
            std::make_shared<lsproto::CompletionItem>(
                lsproto::CompletionItem{
                    .Label = "const",
                    .Kind =
                        std::make_shared<lsproto::CompletionItemKind>(
                            lsproto::CompletionItemKindKeyword),
                    .SortText =
                        std::string(ls::SortTextGlobalsOrKeywords),
                }),
        });

} // namespace tsc::fourslash::tests::util
