#pragma once

// files.h — the file abstraction shared by overlayfs.go (FileContent,
// FileHandle), snapshotfs.go (FileHandleSource, FileSource) and the caches.
// Split out so cache headers don't depend on either overlayfs.h or
// snapshotfs.h; the concrete implementations live in those files.

#include <cstdint>
#include <mutex>
#include <string>

#include "internal/core/types.h" // ScriptKind
#include "internal/ls/lsconv/lsconv.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"
#include "internal/xxh3/xxh3.h"

namespace tsc::project {

// FileContent — overlayfs.go:22.
struct FileContent {
	virtual ~FileContent() = default;
	virtual std::string Content() const = 0;
	virtual xxh3::Uint128 Hash() const = 0;
};

// FileHandle — overlayfs.go:27.
struct FileHandle : FileContent {
	virtual ~FileHandle() = default;
	virtual std::string FileName() const = 0;
	virtual int32_t Version() const = 0;
	virtual bool MatchesDiskText() const = 0;
	virtual bool IsOverlay() const = 0;
	virtual lsconv::LSPLineMap* LSPLineMap() = 0;
	virtual sourcemap::ECMALineInfo* ECMALineInfo() = 0;
	virtual ScriptKind Kind() const = 0;
};

// FileHandleSource — snapshotfs.go:21.
struct FileHandleSource {
	virtual ~FileHandleSource() = default;
	virtual FileHandle* GetFile(const std::string& fileName) = 0;
	virtual FileHandle* GetFileByPath(const std::string& fileName,
	                                  const tspath::Path& path) = 0;
};

// FileSource — snapshotfs.go:26.
struct FileSource : FileHandleSource {
	virtual ~FileSource() = default;
	virtual vfs::FS* FS() = 0;
	virtual bool FileExists(const std::string& fileName,
	                        const tspath::Path& path) = 0;
	virtual vfs::Entries GetAccessibleEntries(const std::string& path) = 0;
};

// fileBase — overlayfs.go:37. Lazily computes the line maps once.
struct fileBase {
	std::string fileName;
	std::string content;
	xxh3::Uint128 hash;

	std::once_flag lineMapOnce;
	lsconv::LSPLineMap* lineMap = nullptr;
	std::once_flag lineInfoOnce;
	sourcemap::ECMALineInfo* lineInfo = nullptr;

	std::string FileName() const { return fileName; }
	xxh3::Uint128 Hash() const { return hash; }
	std::string Content() const { return content; }

	lsconv::LSPLineMap* LSPLineMap() {
		std::call_once(lineMapOnce, [&] {
			lineMap = lsconv::ComputeLSPLineStarts(content);
		});
		return lineMap;
	}

	sourcemap::ECMALineInfo* ECMALineInfo() {
		std::call_once(lineInfoOnce, [&] {
			auto lineStarts = computeECMALineStarts(content);
			lineInfo = sourcemap::CreateECMALineInfo(content, lineStarts);
		});
		return lineInfo;
	}
};

} // namespace tsc::project
