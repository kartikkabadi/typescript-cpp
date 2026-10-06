// === dep decls — owned by lsp ===
// Decls the api slice needs from tsc/internal/lsp/lsproto (lsp.go +
// lsp_generated.go). Pure data types and the URI helpers the api requires at
// runtime are ported faithfully; everything else is stubbed in lsproto.cpp
// with TSC_UNREACHABLE. The lsp slice should replace this file when it lands.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "internal/bundled/bundled.h"
#include "internal/gostd/gostd.h"
#include "internal/tspath/tspath.h"

namespace tsc::lsproto {

// DocumentUri (lsp.go:17) — a URI identifying a document.
struct DocumentUri {
	std::string v;

	DocumentUri() = default;
	DocumentUri(const char* s) : v(s) {}
	DocumentUri(std::string s) : v(std::move(s)) {}

	const std::string& str() const { return v; }
	bool empty() const { return v.empty(); }
	operator const std::string&() const { return v; }
	bool operator==(const DocumentUri&) const = default;

	// FileName (lsp.go:19) — decodes the URI into a file name.
	std::string FileName() const;
	// Path (lsp.go:52) — FileName as a tspath.Path.
	tspath::Path Path(bool useCaseSensitiveFileNames) const;
};

// fixWindowsURIPath (lsp.go:57).
std::string fixWindowsURIPath(std::string_view path);

// Position (lsp_generated.go:3902).
struct Position {
	uint32_t Line{};
	uint32_t Character{};

	bool operator==(const Position&) const = default;
};

// Range (lsp_generated.go:3728).
struct Range {
	Position Start;
	Position End;

	bool operator==(const Range&) const = default;
};

// TextEdit (lsp_generated.go:1998).
struct TextEdit {
	Range range;
	std::string newText;

	bool operator==(const TextEdit&) const = default;
};

// PositionEncodingKind (lsp_generated.go:10241).
struct PositionEncodingKind {
	std::string v;

	PositionEncodingKind() = default;
	PositionEncodingKind(const char* s) : v(s) {}
	PositionEncodingKind(std::string s) : v(std::move(s)) {}

	const std::string& str() const { return v; }
	bool operator==(const PositionEncodingKind&) const = default;
};

inline const PositionEncodingKind PositionEncodingKindUTF8{"utf-8"};
inline const PositionEncodingKind PositionEncodingKindUTF16{"utf-16"};
inline const PositionEncodingKind PositionEncodingKindUTF32{"utf-32"};

// CompletionItemKind (lsp_generated.go:9874).
using CompletionItemKind = uint32_t;
inline constexpr CompletionItemKind CompletionItemKindText = 1;
inline constexpr CompletionItemKind CompletionItemKindMethod = 2;
inline constexpr CompletionItemKind CompletionItemKindFunction = 3;
inline constexpr CompletionItemKind CompletionItemKindConstructor = 4;
inline constexpr CompletionItemKind CompletionItemKindField = 5;
inline constexpr CompletionItemKind CompletionItemKindVariable = 6;
inline constexpr CompletionItemKind CompletionItemKindClass = 7;
inline constexpr CompletionItemKind CompletionItemKindInterface = 8;
inline constexpr CompletionItemKind CompletionItemKindModule = 9;
inline constexpr CompletionItemKind CompletionItemKindProperty = 10;
inline constexpr CompletionItemKind CompletionItemKindUnit = 11;
inline constexpr CompletionItemKind CompletionItemKindValue = 12;
inline constexpr CompletionItemKind CompletionItemKindEnum = 13;
inline constexpr CompletionItemKind CompletionItemKindKeyword = 14;
inline constexpr CompletionItemKind CompletionItemKindSnippet = 15;
inline constexpr CompletionItemKind CompletionItemKindColor = 16;
inline constexpr CompletionItemKind CompletionItemKindFile = 17;
inline constexpr CompletionItemKind CompletionItemKindReference = 18;
inline constexpr CompletionItemKind CompletionItemKindFolder = 19;
inline constexpr CompletionItemKind CompletionItemKindEnumMember = 20;
inline constexpr CompletionItemKind CompletionItemKindConstant = 21;
inline constexpr CompletionItemKind CompletionItemKindStruct = 22;
inline constexpr CompletionItemKind CompletionItemKindEvent = 23;
inline constexpr CompletionItemKind CompletionItemKindOperator = 24;
inline constexpr CompletionItemKind CompletionItemKindTypeParameter = 25;

// LanguageKind (lsp_generated.go:10144) — document language id.
struct LanguageKind {
	std::string v;

	LanguageKind() = default;
	LanguageKind(const char* s) : v(s) {}
	LanguageKind(std::string s) : v(std::move(s)) {}

	const std::string& str() const { return v; }
	bool empty() const { return v.empty(); }
	bool operator==(const LanguageKind&) const = default;
};

inline const LanguageKind LanguageKindTypeScript{"typescript"};
inline const LanguageKind LanguageKindJavaScript{"javascript"};
inline const LanguageKind LanguageKindTypeScriptReact{"typescriptreact"};
inline const LanguageKind LanguageKindJavaScriptReact{"javascriptreact"};
inline const LanguageKind LanguageKindJSON{"json"};
inline const LanguageKind LanguageKindPlaintext{"plaintext"};

// TextDocumentContentChangePartial (lsp_generated.go:6633).
struct TextDocumentContentChangePartial {
	Range range;
	std::optional<uint32_t> rangeLength; // Deprecated: use range instead.
	std::string text;
};

// TextDocumentContentChangeWholeDocument (lsp_generated.go:6653).
struct TextDocumentContentChangeWholeDocument {
	std::string text;
};

// TextDocumentContentChangePartialOrWholeDocument (lsp_generated.go:12178) —
// either a ranged change or a whole-document replacement.
struct TextDocumentContentChangePartialOrWholeDocument {
	std::optional<TextDocumentContentChangePartial> partial;
	std::optional<TextDocumentContentChangeWholeDocument> wholeDocument;
};

// FileEvent / FileChangeType (lsp_generated.go) — watched-file events.
using FileChangeType = int32_t;
inline constexpr FileChangeType FileChangeTypeCreated = 1;
inline constexpr FileChangeType FileChangeTypeChanged = 2;
inline constexpr FileChangeType FileChangeTypeDeleted = 3;

struct FileEvent {
	DocumentUri uri;
	FileChangeType type{};
};

} // namespace tsc::lsproto

template <>
struct std::hash<tsc::lsproto::DocumentUri> {
	size_t operator()(const tsc::lsproto::DocumentUri& u) const noexcept {
		return std::hash<std::string>()(u.v);
	}
};
