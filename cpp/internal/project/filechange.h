#pragma once

// filechange.go — FileChangeKind / FileChange / FileChangeSummary and the
// summary merge helper.

#include <cstdint>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/ls/lsutil/lsutil.h" // lsproto dep decls (DocumentUri, LanguageKind, TextDocumentContentChangePartialOrWholeDocument)

namespace tsc::project {

inline constexpr int excessiveChangeThreshold = 1000;

struct FileChangeSummary;

// FileChangeExpander — filechange.go.
struct FileChangeExpander {
	virtual ~FileChangeExpander() = default;
	virtual FileChangeSummary ExpandFileChanges(
		FileChangeSummary summary) = 0;
};

// FileChangeKind — filechange.go.
enum class FileChangeKind : int32_t {
	Open,
	Close,
	Change,
	Save,
	WatchCreate,
	WatchChange,
	WatchDelete,
};
inline constexpr FileChangeKind FileChangeKindOpen = FileChangeKind::Open;
inline constexpr FileChangeKind FileChangeKindClose = FileChangeKind::Close;
inline constexpr FileChangeKind FileChangeKindChange = FileChangeKind::Change;
inline constexpr FileChangeKind FileChangeKindSave = FileChangeKind::Save;
inline constexpr FileChangeKind FileChangeKindWatchCreate =
	FileChangeKind::WatchCreate;
inline constexpr FileChangeKind FileChangeKindWatchChange =
	FileChangeKind::WatchChange;
inline constexpr FileChangeKind FileChangeKindWatchDelete =
	FileChangeKind::WatchDelete;

// IsWatchKind — filechange.go.
inline bool fileChangeKindIsWatchKind(FileChangeKind k) {
	return k == FileChangeKindWatchCreate || k == FileChangeKindWatchChange ||
	       k == FileChangeKindWatchDelete;
}

// FileChange — filechange.go.
struct FileChange {
	FileChangeKind Kind{};
	lsp::lsproto::DocumentUri URI;
	int32_t Version = 0;              // Only set for Open/Change
	std::string Content;              // Only set for Open
	lsp::lsproto::LanguageKind LanguageKind; // Only set for Open
	std::vector<lsp::lsproto::TextDocumentContentChangePartialOrWholeDocument>
	    Changes; // Only set for Change
};

// FileChangeSummary — filechange.go.
struct FileChangeSummary {
	// Only one file can be opened at a time per request
	lsp::lsproto::DocumentUri Opened;
	// Reopened is set if a close and open occurred for the same file in a
	// single batch of changes.
	lsp::lsproto::DocumentUri Reopened;
	collections::Set<lsp::lsproto::DocumentUri> Closed;
	collections::Set<lsp::lsproto::DocumentUri> Changed;
	// Only set when file watching is enabled
	collections::Set<lsp::lsproto::DocumentUri> Created;
	// Only set when file watching is enabled
	collections::Set<lsp::lsproto::DocumentUri> Deleted;

	// IncludesWatchChangeOutsideNodeModules is true if the summary includes a
	// create, change, or delete watch event of a file outside a node_modules
	// directory.
	bool IncludesWatchChangeOutsideNodeModules = false;
	// InvalidateAll indicates that all cached file state should be discarded.
	bool InvalidateAll = false;

	// Clone — filechange.go.
	FileChangeSummary Clone() const {
		FileChangeSummary f = *this;
		f.Closed = Closed.Clone();
		f.Changed = Changed.Clone();
		f.Created = Created.Clone();
		f.Deleted = Deleted.Clone();
		return f;
	}

	// IsEmpty — filechange.go.
	bool IsEmpty() const {
		return !InvalidateAll && Opened.empty() && Reopened.empty() &&
		       Closed.Len() == 0 && Changed.Len() == 0 && Created.Len() == 0 &&
		       Deleted.Len() == 0;
	}

	// HasExcessiveWatchEvents — filechange.go.
	bool HasExcessiveWatchEvents() const {
		return InvalidateAll ||
		       Created.Len() + Deleted.Len() + Changed.Len() >
		           excessiveChangeThreshold;
	}

	// HasExcessiveNonCreateWatchEvents — filechange.go.
	bool HasExcessiveNonCreateWatchEvents() const {
		return InvalidateAll ||
		       Deleted.Len() + Changed.Len() > excessiveChangeThreshold;
	}
};

// mergeFileChangeSummary merges src into dst, combining their change sets.
inline void mergeFileChangeSummary(FileChangeSummary* dst,
                                   const FileChangeSummary& src) {
	if (src.IsEmpty()) {
		return;
	}
	if (src.InvalidateAll) {
		dst->InvalidateAll = true;
	}
	for (const auto& uri : src.Changed.Keys()) {
		dst->Changed.Add(uri);
	}
	for (const auto& uri : src.Created.Keys()) {
		dst->Created.Add(uri);
	}
	for (const auto& uri : src.Deleted.Keys()) {
		dst->Deleted.Add(uri);
	}
	if (src.IncludesWatchChangeOutsideNodeModules) {
		dst->IncludesWatchChangeOutsideNodeModules = true;
	}
}

} // namespace tsc::project
