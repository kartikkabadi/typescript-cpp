#pragma once

// ============================================================================
// lsproto.h — umbrella header for the LSP protocol slice: generated types
// (lsproto_generated.h) + hand-ported runtime (lsproto_runtime.h) + the
// decls that need complete generated types (interfaces, UnmarshalParams,
// util.go helpers).
// ============================================================================

#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/lsp/lsproto/lsproto_generated.h"

namespace tsc::lsp::lsproto {

// ---------------------------------------------------------------------------
// lsp.go:66-82 — capability interfaces (need the complete generated types).
// ---------------------------------------------------------------------------
struct HasTextDocumentURI {
	virtual ~HasTextDocumentURI() = default;
	virtual DocumentUri TextDocumentURI() const = 0;
};
struct HasTextDocumentPosition : HasTextDocumentURI {
	virtual Position TextDocumentPosition() const = 0;
};
struct HasLocations {
	virtual ~HasLocations() = default;
	virtual std::shared_ptr<Slice<Location>> GetLocations() const = 0;
};
struct HasLocation {
	virtual ~HasLocation() = default;
	virtual Location GetLocation() const = 0;
};

// ---------------------------------------------------------------------------
// lsp.go:235 — RequestMessage.UnmarshalParams.
// ---------------------------------------------------------------------------
template <class T>
std::pair<T, Error> RequestMessage::UnmarshalParams() const {
	T params{};
	json::Value raw;
	if (Params.isSet()) {
		if (!Params.rawSet) {
			return {params, gostd::errorf("%w: unexpected params type %s",
				{errorCodeErr(ErrorCode::InvalidParams), Params.typeName})};
		}
		raw = Params.raw;
	}
	// `params` is the zero value of T; this asserts on its type, i.e. whether
	// the method was declared with NoParams.
	if constexpr (std::is_same_v<T, NoParams>) {
		if (!raw.empty()) {
			return {params, gostd::errorf("%w: expected no params, got %s",
				{errorCodeErr(ErrorCode::InvalidParams), std::string_view(raw)})};
		}
		return {std::move(params), nullptr};
	}
	// The base protocol defines params as `array | object`; reject anything
	// else (absent, null, or a scalar).
	if (auto k = valueKind(raw); k != '{' && k != '[') {
		return {params, gostd::errorf("%w: params must be an object or array",
			{errorCodeErr(ErrorCode::InvalidParams)})};
	}
	if (auto err = json::unmarshal(raw, &params); !err.empty()) {
		return {params, gostd::errorf("%w: %s",
			{errorCodeErr(ErrorCode::InvalidParams), err})};
	}
	return {std::move(params), nullptr};
}

// ---------------------------------------------------------------------------
// util.go — comparisons + diagnostic helpers.
// ---------------------------------------------------------------------------
int ComparePositions(const Position& pos, const Position& other);
int CompareRanges(const Range& lsRange, const Range& other);

bool diagnosticsEqual(const Diagnostic* diag1, const Diagnostic* diag2);
std::pair<std::vector<std::shared_ptr<Diagnostic>>, std::vector<std::shared_ptr<Diagnostic>>>
CompareDiagnostics(const std::vector<std::shared_ptr<Diagnostic>>& list1,
                   const std::vector<std::shared_ptr<Diagnostic>>& list2);

// ---------------------------------------------------------------------------
// lsp.go:302 — PreferredMarkupKind.
// ---------------------------------------------------------------------------
MarkupKind PreferredMarkupKind(const Slice<MarkupKind>& formats);

// ---------------------------------------------------------------------------
// lsp.go:310-320 — CodeActionKind helpers (TS-derived constants).
// ---------------------------------------------------------------------------
extern const CodeActionKind CodeActionKindSourceFixAllTs;
extern const CodeActionKind CodeActionKindSourceOrganizeImportsTs;
extern const CodeActionKind CodeActionKindSourceRemoveUnusedImportsTs;
extern const CodeActionKind CodeActionKindSourceSortImportsTs;

bool codeActionKindContains(const CodeActionKind& kind, const CodeActionKind& other);

// ---------------------------------------------------------------------------
// lsp.go:19-55 — DocumentUri helpers. DocumentUri is a std::string alias, so
// the Go methods are free functions here.
// ---------------------------------------------------------------------------
std::string documentUriFileName(DocumentUri uri);
tspath::Path documentUriPath(DocumentUri uri, bool useCaseSensitiveFileNames);

} // namespace tsc::lsp::lsproto
