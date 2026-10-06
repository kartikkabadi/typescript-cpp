// lsproto — dep-decl for the ls-autoimport slice: the value types
// (lsp_generated.go + lsp.go) the autoimport package embeds or carries in
// signatures. The package's behavior (URI conversion, marshaling) is owned
// by the lsp slice; functions are dep-stubs.
#pragma once

#include <cstdint>
#include <string>

#include "internal/tspath/tspath.h"

namespace tsc::lsproto {

// === dep decls for ls-autoimport — owned by lsp ===

// DocumentUri — lsp.go:17 (Go `type DocumentUri string`).
using DocumentUri = std::string;

// DocumentUri.FileName — lsp.go:19. dep-stub — owned by lsp.
std::string documentUriFileName(const DocumentUri& uri);
// DocumentUri.Path — lsp.go:52. dep-stub — owned by lsp.
tspath::Path documentUriPath(const DocumentUri& uri,
                             bool useCaseSensitiveFileNames);

// Position — lsp_generated.go:3902.
struct Position {
	uint32_t Line{};
	uint32_t Character{};

	bool operator==(const Position&) const = default;
};

// Range — lsp_generated.go:3728.
struct Range {
	Position Start;
	Position End;

	bool operator==(const Range&) const = default;
};

// TextEdit — lsp_generated.go:1998.
struct TextEdit {
	Range range;
	std::string NewText;
};

// FileChangeType — lsp_generated.go:10260.
enum class FileChangeType : uint32_t {
	Created = 1,
	Changed = 2,
	Deleted = 3,
};
inline constexpr FileChangeType FileChangeTypeCreated = FileChangeType::Created;
inline constexpr FileChangeType FileChangeTypeChanged = FileChangeType::Changed;
inline constexpr FileChangeType FileChangeTypeDeleted = FileChangeType::Deleted;

// FileEvent — lsp_generated.go:5442.
struct FileEvent {
	DocumentUri Uri;
	FileChangeType Type{};
};

// LanguageKind — lsp_generated.go:10144.
using LanguageKind = std::string;
inline const LanguageKind LanguageKindTypeScript{"typescript"};
inline const LanguageKind LanguageKindJavaScript{"javascript"};

// PositionEncodingKind — lsp_generated.go:10241.
using PositionEncodingKind = std::string;
inline const PositionEncodingKind PositionEncodingKindUTF8{"utf-8"};
inline const PositionEncodingKind PositionEncodingKindUTF16{"utf-16"};
inline const PositionEncodingKind PositionEncodingKindUTF32{"utf-32"};

// TextDocumentContentChangePartial — lsp_generated.go:6633.
struct TextDocumentContentChangePartial {
	Range range;
	// RangeLength deprecated in Go; omitted (no ls-autoimport use).
	std::string Text;
};

// TextDocumentContentChangeWholeDocument — lsp_generated.go:6653.
struct TextDocumentContentChangeWholeDocument {
	std::string Text;
};

// TextDocumentContentChangePartialOrWholeDocument — lsp_generated.go:12178.
// Go: union {*Partial | *WholeDocument}; nullptr-nullptr == Go nil union.
struct TextDocumentContentChangePartialOrWholeDocument {
	TextDocumentContentChangePartial* Partial = nullptr;
	TextDocumentContentChangeWholeDocument* WholeDocument = nullptr;
};

// AutoImportFixKind — lsp_generated.go:10667.
enum class AutoImportFixKind : int32_t {
	UseNamespace = 0,
	JsdocTypeImport = 1,
	AddToExisting = 2,
	AddNew = 3,
	PromoteTypeOnly = 4,
};
inline constexpr AutoImportFixKind AutoImportFixKindUseNamespace =
	AutoImportFixKind::UseNamespace;
inline constexpr AutoImportFixKind AutoImportFixKindJsdocTypeImport =
	AutoImportFixKind::JsdocTypeImport;
inline constexpr AutoImportFixKind AutoImportFixKindAddToExisting =
	AutoImportFixKind::AddToExisting;
inline constexpr AutoImportFixKind AutoImportFixKindAddNew =
	AutoImportFixKind::AddNew;
inline constexpr AutoImportFixKind AutoImportFixKindPromoteTypeOnly =
	AutoImportFixKind::PromoteTypeOnly;

// ImportKind — lsp_generated.go:10694.
enum class ImportKind : int32_t {
	Named = 0,
	Default = 1,
	Namespace = 2,
	CommonJS = 3,
};
inline constexpr ImportKind ImportKindNamed = ImportKind::Named;
inline constexpr ImportKind ImportKindDefault = ImportKind::Default;
inline constexpr ImportKind ImportKindNamespace = ImportKind::Namespace;
inline constexpr ImportKind ImportKindCommonJS = ImportKind::CommonJS;

// AddAsTypeOnly — lsp_generated.go:10719. Go's values are a bitmask
// (Allowed=1, Required=2, NotAllowed=4).
enum class AddAsTypeOnly : int32_t {
	Allowed = 1,
	Required = 2,
	NotAllowed = 4,
};
inline constexpr AddAsTypeOnly AddAsTypeOnlyAllowed = AddAsTypeOnly::Allowed;
inline constexpr AddAsTypeOnly AddAsTypeOnlyRequired = AddAsTypeOnly::Required;
inline constexpr AddAsTypeOnly AddAsTypeOnlyNotAllowed =
	AddAsTypeOnly::NotAllowed;

// AutoImportFix — lsp_generated.go:8811.
struct AutoImportFix {
	AutoImportFixKind Kind{};
	std::string Name;
	ImportKind ImportKind{};
	bool UseRequire = false;
	AddAsTypeOnly AddAsTypeOnly{};
	std::string ModuleSpecifier;
	int32_t ImportIndex = 0;
	Position* UsagePosition = nullptr;
	std::string NamespacePrefix;
};

} // namespace tsc::lsproto
