// === slice: lsproto ===
// lsproto — handwritten helpers layered on top of the generated LSP types.
// `lsproto_generated.h` holds the spec-generated structs (include it for the
// types); this header is the one-stop include: generated types + helpers.
#pragma once

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/lsp/lsproto/lsproto_generated.h"

namespace tsc::lsp::lsproto {

// ---------------------------------------------------------------------------
// lsp.go:66-82 — capability constraints. The Go interfaces are satisfied
// structurally by the generated response types; in C++ they are concepts.
// ---------------------------------------------------------------------------
template <typename T>
concept HasTextDocumentURI = requires(const T& t) {
	{ t.TextDocumentURI() } -> std::convertible_to<DocumentUri>;
};
template <typename T>
concept HasTextDocumentPosition = HasTextDocumentURI<T> && requires(const T& t) {
	{ t.TextDocumentPosition() } -> std::convertible_to<Position>;
};
template <typename T>
concept HasLocations = requires(const T& t) {
	{ t.GetLocations() } -> std::convertible_to<std::shared_ptr<Slice<Location>>>;
};
template <typename T>
concept HasLocation = requires(const T& t) {
	{ t.GetLocation() } -> std::convertible_to<Location>;
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

// std::hash specializations — collections::Set<T> and unordered_map<K,...>
// over the generated value types.
// DocumentUri is a std::string alias — std::hash<std::string> covers it.
template <>
struct std::hash<tsc::lsp::lsproto::Position> {
	size_t operator()(const tsc::lsp::lsproto::Position& p) const {
		return (size_t)p.Line << 32 | p.Character;
	}
};
template <>
struct std::hash<tsc::lsp::lsproto::Range> {
	size_t operator()(const tsc::lsp::lsproto::Range& r) const {
		return std::hash<tsc::lsp::lsproto::Position>{}(r.Start) * 31 +
		       std::hash<tsc::lsp::lsproto::Position>{}(r.End);
	}
};
template <>
struct std::hash<tsc::lsp::lsproto::Location> {
	size_t operator()(const tsc::lsp::lsproto::Location& l) const {
		return std::hash<std::string>{}(l.Uri) * 31 +
		       std::hash<tsc::lsp::lsproto::Range>{}(l.Range);
	}
};

// === merge bridge ===
// Consumers ported against the interim flat tsc::lsproto union spell
// `lsproto::X` unqualified inside tsc::ls; route them to the canonical
// nested namespace. (namespace-alias coexists with the real nested name.)
namespace tsc {
namespace lsproto = tsc::lsp::lsproto;
}
