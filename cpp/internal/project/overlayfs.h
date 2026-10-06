#pragma once

// overlayfs.go — cachedFile / Overlay / overlayFS + LayeredFileSystem /
// RebasableFileSystem interfaces. Declarations + small bodies inline;
// processChanges / createOverlayDirectories / GetAccessibleEntries live in
// overlayfs.cpp.

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/core/textchange.h" // TextChange
#include "internal/debug/debug.h"
#include "internal/project/filechange.h"
#include "internal/project/files.h"
#include "internal/spanmap/spanmap.h"
#include "internal/xxh3/xxh3.h"

namespace tsc::project {

// cachedFile — overlayfs.go:76.
struct cachedFile : fileBase, FileHandle {
	bool needsReload = false;
	tspath::Path realpathPath;

	cachedFile(const std::string& fileName, const std::string& content) {
		this->fileName = fileName;
		this->content = content;
		this->hash = xxh3::hash128(content);
	}

	std::string Content() const override { return content; }
	xxh3::Uint128 Hash() const override { return hash; }
	std::string FileName() const override { return fileName; }
	int32_t Version() const override { return 0; }
	bool MatchesDiskText() const override { return !needsReload; }
	bool IsOverlay() const override { return false; }
	lsconv::LSPLineMap* LSPLineMap() override {
		return fileBase::LSPLineMap();
	}
	sourcemap::ECMALineInfo* ECMALineInfo() override {
		return fileBase::ECMALineInfo();
	}
	ScriptKind Kind() const override {
		return getScriptKindFromFileName(fileName);
	}

	cachedFile* Clone() {
		auto* f = new cachedFile(fileName, content);
		f->realpathPath = realpathPath;
		f->hash = hash;
		return f;
	}
};

// NewCachedFileHandle — overlayfs.go:90.
inline FileHandle* newCachedFileHandle(const std::string& fileName,
                                       const std::string& content) {
	return new cachedFile(fileName, content);
}

// Overlay — overlayfs.go:123.
struct Overlay : fileBase, FileHandle {
	int32_t version = 0;
	ScriptKind kind = ScriptKind::Unknown;
	bool matchesDiskText = false;

	Overlay(const std::string& fileName, const std::string& content,
	        int32_t version, ScriptKind kind)
	    : version(version), kind(kind) {
		this->fileName = fileName;
		this->content = content;
		this->hash = xxh3::hash128(content);
	}

	std::string Content() const override { return content; }
	xxh3::Uint128 Hash() const override { return hash; }
	std::string FileName() const override { return fileName; }
	int32_t Version() const override { return version; }
	// Script-concept accessors return a const ref like every other Script
	// impl — a by-value return makes `std::string_view sv = script->Text()`
	// dangle (the prvalue dies at the end of the full-expression; Go's
	// string return shares the backing bytes).
	const std::string& Text() const { return content; }
	std::string OriginalFileName() const { return FileName(); }

	// SpanMap and OriginalText satisfy lsconv.Script. An overlay holds the
	// editor's raw text (for a content-mapped file, that is the original
	// foreign text, not the transformed output), so it never carries a
	// span map and its original text is its own text.
	spanmap::SpanMap* SpanMap() const { return nullptr; }
	const std::string& OriginalText() const { return content; }

	// MatchesDiskText may return false negatives, but never false
	// positives.
	bool MatchesDiskText() const override { return matchesDiskText; }

	// !!! optimization: incorporate mtime
	std::pair<bool, bool> computeMatchesDiskText(vfs::FS* fs) {
		if (tspath::isDynamicFileName(fileName)) {
			return {false, false};
		}
		auto [diskContent, ok] = fs->ReadFile(fileName);
		if (!ok) {
			return {false, false};
		}
		return {xxh3::hash128(diskContent) == hash, true};
	}

	bool IsOverlay() const override { return true; }
	ScriptKind Kind() const override { return kind; }
	lsconv::LSPLineMap* LSPLineMap() override {
		return fileBase::LSPLineMap();
	}
	sourcemap::ECMALineInfo* ECMALineInfo() override {
		return fileBase::ECMALineInfo();
	}
};

inline Overlay* newOverlay(const std::string& fileName,
                           const std::string& content, int32_t version,
                           ScriptKind kind) {
	return new Overlay(fileName, content, version, kind);
}

// LayeredFileSystem — overlayfs.go:192.
struct LayeredFileSystem : virtual vfs::FS, FileHandleSource {
	virtual std::unordered_map<tspath::Path, Overlay*> Overlays() = 0;
};

// RebasableFileSystem — overlayfs.go:198.
struct RebasableFileSystem : virtual vfs::FS {
	virtual vfs::FS* BaseFileSystem() = 0;
	virtual LayeredFileSystem* WithBaseFileSystem(vfs::FS* base) = 0;
};

// overlayFS — overlayfs.go:182.
struct overlayFS : LayeredFileSystem {
	std::function<tspath::Path(const std::string&)> toPath;
	vfs::FS* host;
	lsp::lsproto::PositionEncodingKind positionEncoding;

	std::shared_mutex mu;
	std::unordered_map<tspath::Path, Overlay*> overlays;
	std::unordered_map<tspath::Path,
	                   std::unordered_map<tspath::Path, std::string>>
	    overlayDirectories;

	std::unordered_map<tspath::Path, Overlay*> Overlays() override {
		std::shared_lock<std::shared_mutex> lk(mu);
		return overlays;
	}

	FileHandle* GetFile(const std::string& fileName) override {
		return GetFileByPath(fileName, toPath(fileName));
	}

	FileHandle* GetFileByPath(const std::string& fileName,
	                          const tspath::Path& path) override;

	bool UseCaseSensitiveFileNames() override {
		return host->UseCaseSensitiveFileNames();
	}

	bool FileExists(const std::string& fileName) override {
		std::shared_lock<std::shared_mutex> lk(mu);
		auto path = toPath(fileName);
		bool file = overlays.count(path) != 0;
		bool directory = overlayDirectories.count(path) != 0;
		lk.unlock();
		return file || (!directory && host->FileExists(fileName));
	}

	std::pair<std::string, bool>
	ReadFile(const std::string& fileName) override {
		if (auto* file = GetFile(fileName)) {
			return {file->Content(), true};
		}
		return {"", false};
	}

	vfs::Error WriteFile(const std::string& path,
	                     const std::string& data) override {
		return host->WriteFile(path, data);
	}
	vfs::Error AppendFile(const std::string& path,
	                      const std::string& data) override {
		return host->AppendFile(path, data);
	}
	vfs::Error Remove(const std::string& path) override {
		return host->Remove(path);
	}
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint aTime,
	                   vfs::TimePoint mTime) override {
		return host->Chtimes(path, aTime, mTime);
	}

	bool DirectoryExists(const std::string& directoryName) override {
		std::shared_lock<std::shared_mutex> lk(mu);
		auto path = toPath(directoryName);
		bool file = overlays.count(path) != 0;
		bool directory = overlayDirectories.count(path) != 0;
		lk.unlock();
		return directory || (!file && host->DirectoryExists(directoryName));
	}

	vfs::Entries
	GetAccessibleEntries(const std::string& directoryName) override;

	std::shared_ptr<vfs::FileInfo> Stat(const std::string& path) override;

	std::string Realpath(const std::string& path) override {
		return host->Realpath(path);
	}

	std::pair<FileChangeSummary,
	          std::unordered_map<tspath::Path, Overlay*>>
	processChanges(const std::vector<FileChange>& changes);
};

// overlayFileInfo — overlayfs.go:362.
struct overlayFileInfo : vfs::FileInfo {
	Overlay* overlay;

	explicit overlayFileInfo(Overlay* overlay) : overlay(overlay) {}

	std::string Name() const override {
		return std::string(tspath::getBaseFileName(overlay->FileName()));
	}
	int64_t Size() const override {
		return static_cast<int64_t>(overlay->Content().size());
	}
	vfs::FileMode Mode() const override { return vfs::FileMode{0444}; }
	vfs::TimePoint ModTime() const override { return vfs::TimePoint{}; }
	bool IsDir() const override { return false; }
	std::any Sys() const override { return std::any{}; }
};

// overlayDirectoryInfo — overlayfs.go:373.
struct overlayDirectoryInfo : vfs::FileInfo {
	std::string name;

	explicit overlayDirectoryInfo(std::string name) : name(std::move(name)) {}

	std::string Name() const override { return name; }
	int64_t Size() const override { return 0; }
	vfs::FileMode Mode() const override {
		return vfs::ModeDir | vfs::FileMode{0555};
	}
	vfs::TimePoint ModTime() const override { return vfs::TimePoint{}; }
	bool IsDir() const override { return true; }
	std::any Sys() const override { return std::any{}; }
};

// createOverlayDirectories — overlayfs.go:384.
std::unordered_map<tspath::Path,
                   std::unordered_map<tspath::Path, std::string>>
createOverlayDirectories(
	const std::unordered_map<tspath::Path, Overlay*>& overlays);

// newOverlayFS — overlayfs.go:204.
inline overlayFS* newOverlayFS(
	vfs::FS* fs,
	std::unordered_map<tspath::Path, Overlay*> overlays,
	lsp::lsproto::PositionEncodingKind positionEncoding,
	std::function<tspath::Path(const std::string&)> toPath) {
	auto* result = new overlayFS();
	result->host = fs;
	result->positionEncoding = std::move(positionEncoding);
	result->overlays = std::move(overlays);
	result->overlayDirectories = createOverlayDirectories(result->overlays);
	result->toPath = std::move(toPath);
	return result;
}

// layerOverlayFileSystem — overlayfs.go:226.
inline LayeredFileSystem* layerOverlayFileSystem(
	vfs::FS* fileSystem,
	std::unordered_map<tspath::Path, Overlay*> overlays,
	lsp::lsproto::PositionEncodingKind positionEncoding,
	std::function<tspath::Path(const std::string&)> toPath) {
	vfs::FS* base = fileSystem;
	RebasableFileSystem* layer =
	    dynamic_cast<RebasableFileSystem*>(fileSystem);
	if (layer != nullptr) {
		base = layer->BaseFileSystem();
	}
	if (auto* previous = dynamic_cast<overlayFS*>(base)) {
		base = previous->host;
	}
	auto* overlay = newOverlayFS(base, std::move(overlays), positionEncoding,
	                             std::move(toPath));
	if (layer == nullptr) {
		return overlay;
	}
	return layer->WithBaseFileSystem(overlay);
}

} // namespace tsc::project
