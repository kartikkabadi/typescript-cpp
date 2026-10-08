// vfs.h — port of tsc/internal/vfs/vfs.go (FS interface + supporting types)
// plus the io/fs analogs the package relies on (FileMode, FileInfo, DirEntry,
// File, FS, error sentinels).
#pragma once

#include <any>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tsc::vfs {

using TimePoint = std::chrono::system_clock::time_point;
using Duration = std::chrono::milliseconds;

// Error — Go `error` analog. A nil-able shared pointer to an immutable Impl;
// nil means success. errors.Is semantics = impl-pointer identity along the
// Unwrap() chain (`wrapped`).
class Error {
public:
	struct Impl {
		std::string message;
		std::shared_ptr<Impl> wrapped;

		Impl(std::string msg, std::shared_ptr<Impl> w = nullptr)
		    : message(std::move(msg)), wrapped(std::move(w)) {}
		virtual ~Impl() = default;
	};

private:
	std::shared_ptr<Impl> impl_;

public:
	Error() = default;
	explicit Error(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

	// errors.New / fmt.Errorf without %w.
	static Error newError(std::string message) {
		return Error{std::make_shared<Impl>(std::move(message))};
	}

	// fmt.Errorf("...: %w", err) — a fresh Impl whose Unwrap() is err.
	static Error wrap(std::string message, const Error& err) {
		return Error{std::make_shared<Impl>(std::move(message),
		                                    err.impl_)};
	}

	explicit operator bool() const { return impl_ != nullptr; }
	bool operator==(const Error& o) const { return impl_ == o.impl_; }
	bool operator!=(const Error& o) const { return impl_ != o.impl_; }

	std::string str() const { return impl_ ? impl_->message : std::string{}; }
	const std::shared_ptr<Impl>& impl() const { return impl_; }
	Error unwrap() const {
		return impl_ ? Error{impl_->wrapped} : Error{};
	}

	// errors.Is — target must be a sentinel (identity on the impl pointer).
	bool is(const Error& target) const {
		for (auto p = impl_; p; p = p->wrapped) {
			if (p == target.impl_) return true;
		}
		return false;
	}

	// errors.AsType — dynamic_pointer_cast each link to the Impl subtype T.
	template <typename T>
	std::shared_ptr<T> as() const {
		for (auto p = impl_; p; p = p->wrapped) {
			if (auto cast = std::dynamic_pointer_cast<T>(p)) return cast;
		}
		return nullptr;
	}
};

// PathErrorImpl — fs.PathError analog (Unwrap() -> Err).
struct PathErrorImpl : Error::Impl {
	std::string op;
	std::string path;
	PathErrorImpl(std::string op, std::string path, const Error& err)
	    : Error::Impl{op + " " + path + ": " + err.str(), err.impl()},
	      op(std::move(op)), path(std::move(path)) {}
};

inline Error makePathError(std::string op, std::string path,
                           const Error& err) {
	return Error{std::make_shared<PathErrorImpl>(std::move(op),
	                                             std::move(path), err)};
}

// Sentinel errors — io/fs (vfs.go re-exports) and io/fs.WalkDir.
inline const Error ErrInvalid = Error::newError("invalid argument");
inline const Error ErrPermission = Error::newError("permission denied");
inline const Error ErrExist = Error::newError("file already exists");
inline const Error ErrNotExist = Error::newError("file does not exist");
inline const Error ErrClosed = Error::newError("file already closed");
inline const Error SkipDir = Error::newError("skip this directory");
inline const Error SkipAll = Error::newError("skip everything");
inline const Error ErrEOF = Error::newError("EOF"); // io.EOF

// FileMode — fs.FileMode: high bits are type flags, low 9 bits Unix perms.
struct FileMode {
	uint32_t v = 0;

	static constexpr uint32_t kDir = 1u << 31;
	static constexpr uint32_t kAppend = 1u << 30;
	static constexpr uint32_t kExclusive = 1u << 29;
	static constexpr uint32_t kTemporary = 1u << 28;
	static constexpr uint32_t kSymlink = 1u << 27;
	static constexpr uint32_t kDevice = 1u << 26;
	static constexpr uint32_t kNamedPipe = 1u << 25;
	static constexpr uint32_t kSocket = 1u << 24;
	static constexpr uint32_t kSetuid = 1u << 23;
	static constexpr uint32_t kSetgid = 1u << 22;
	static constexpr uint32_t kCharDevice = 1u << 21;
	static constexpr uint32_t kSticky = 1u << 20;
	static constexpr uint32_t kIrregular = 1u << 19;
	static constexpr uint32_t kType = kDir | kSymlink | kNamedPipe |
	                                  kSocket | kDevice | kCharDevice |
	                                  kIrregular;
	static constexpr uint32_t kPerm = 0777u;

	constexpr bool IsDir() const { return (v & kDir) != 0; }
	constexpr bool IsRegular() const { return (v & kType) == 0; }
	constexpr FileMode Perm() const { return FileMode{v & kPerm}; }
	constexpr FileMode Type() const { return FileMode{v & kType}; }

	constexpr bool operator==(const FileMode&) const = default;
	constexpr explicit operator bool() const { return v != 0; }
};

// fs.ModeXxx constants.
inline constexpr FileMode ModeDir{FileMode::kDir};
inline constexpr FileMode ModeAppend{FileMode::kAppend};
inline constexpr FileMode ModeExclusive{FileMode::kExclusive};
inline constexpr FileMode ModeTemporary{FileMode::kTemporary};
inline constexpr FileMode ModeSymlink{FileMode::kSymlink};
inline constexpr FileMode ModeDevice{FileMode::kDevice};
inline constexpr FileMode ModeNamedPipe{FileMode::kNamedPipe};
inline constexpr FileMode ModeSocket{FileMode::kSocket};
inline constexpr FileMode ModeSetuid{FileMode::kSetuid};
inline constexpr FileMode ModeSetgid{FileMode::kSetgid};
inline constexpr FileMode ModeCharDevice{FileMode::kCharDevice};
inline constexpr FileMode ModeSticky{FileMode::kSticky};
inline constexpr FileMode ModeIrregular{FileMode::kIrregular};
inline constexpr FileMode ModeType{FileMode::kType};
inline constexpr FileMode ModePerm{FileMode::kPerm};

constexpr FileMode operator|(FileMode a, FileMode b) {
	return FileMode{a.v | b.v};
}
constexpr FileMode operator&(FileMode a, FileMode b) {
	return FileMode{a.v & b.v};
}
constexpr FileMode operator~(FileMode a) { return FileMode{~a.v}; }

// FileInfo — fs.FileInfo interface.
struct FileInfo {
	virtual ~FileInfo() = default;
	virtual std::string Name() const = 0;
	virtual int64_t Size() const = 0;
	virtual FileMode Mode() const = 0;
	virtual TimePoint ModTime() const = 0;
	virtual bool IsDir() const = 0;
	virtual std::any Sys() const = 0;
};

// DirEntry — fs.DirEntry interface.
struct DirEntry {
	virtual ~DirEntry() = default;
	virtual std::string Name() const = 0;
	virtual bool IsDir() const = 0;
	virtual FileMode Type() const = 0;
	virtual std::pair<std::shared_ptr<FileInfo>, Error> Info() const = 0;
};

// File — fs.File interface.
struct File {
	virtual ~File() = default;
	virtual std::pair<std::shared_ptr<FileInfo>, Error> Stat() = 0;
	virtual std::pair<int, Error> Read(std::span<char> b) = 0;
	virtual Error Close() = 0;
};

// ReadDirFile — fs.ReadDirFile interface (File + ReadDir).
struct ReadDirFile : virtual File {
	// Up to n entries in directory order; n <= 0 returns all remaining.
	virtual std::pair<std::vector<std::shared_ptr<DirEntry>>, Error>
	ReadDir(int n) = 0;
};

// IoFS — fs.FS interface (named to distinguish it from vfs::FS).
struct IoFS {
	virtual ~IoFS() = default;
	virtual std::pair<std::shared_ptr<File>, Error>
	Open(const std::string& name) = 0;
};

struct StatFS : virtual IoFS {
	virtual std::pair<std::shared_ptr<FileInfo>, Error>
	Stat(const std::string& name) = 0;
};

struct ReadDirFS : virtual IoFS {
	virtual std::pair<std::vector<std::shared_ptr<DirEntry>>, Error>
	ReadDir(const std::string& name) = 0;
};

struct ReadFileFS : virtual IoFS {
	virtual std::pair<std::string, Error>
	ReadFile(const std::string& name) = 0;
};

struct SubFS : virtual IoFS {
	virtual std::pair<std::shared_ptr<IoFS>, Error>
	Sub(const std::string& dir) = 0;
};

struct GlobFS : virtual IoFS {
	virtual std::pair<std::vector<std::string>, Error>
	Glob(const std::string& pattern) = 0;
};

struct ReadLinkFS : virtual IoFS {
	virtual std::pair<std::string, Error>
	ReadLink(const std::string& name) = 0;
	// Lstat is part of Go's ReadLinkFS (LstatFS in older versions).
	virtual std::pair<std::shared_ptr<FileInfo>, Error>
	Lstat(const std::string& name) = 0;
};

// --- fs package generic helpers (fs.Stat / fs.ReadDir / fs.ReadFile /
// fs.Sub / fs.ValidPath / fs.FileInfoToDirEntry) -------------------------

std::pair<std::shared_ptr<FileInfo>, Error> fsStat(
    const std::shared_ptr<IoFS>& fsys, const std::string& name);

std::pair<std::vector<std::shared_ptr<DirEntry>>, Error> fsReadDir(
    const std::shared_ptr<IoFS>& fsys, const std::string& name);

std::pair<std::string, Error> fsReadFile(
    const std::shared_ptr<IoFS>& fsys, const std::string& name);

std::pair<std::shared_ptr<IoFS>, Error> fsSub(
    const std::shared_ptr<IoFS>& fsys, const std::string& dir);

// fs.ValidPath — unrooted, slash-separated, no empty/. /.. elements; "." ok.
bool validPath(const std::string& name);

// fs.FileInfoToDirEntry — wraps a FileInfo as a DirEntry (null in -> null out).
std::shared_ptr<DirEntry> fileInfoToDirEntry(
    const std::shared_ptr<FileInfo>& info);

// fs.Glob — io/fs/glob.go: pattern-matched file names, sorted.
std::pair<std::vector<std::string>, Error> fsGlob(
    const std::shared_ptr<IoFS>& fsys, const std::string& pattern);

// --- path package helpers (slash-separated, as in io/fs) ----------------

// path.Clean — lexical cleanup ("..", ".", repeated slashes); "" -> ".".
std::string pathClean(std::string_view path);
// path.Split — dir (incl. trailing slash) + file.
std::pair<std::string, std::string> pathSplit(std::string_view path);
// path.Join — joins with '/' then Clean.
std::string pathJoin(std::initializer_list<std::string_view> elems);
// path.Dir — all but the last element, cleaned; "" -> ".".
std::string pathDir(std::string_view path);
// path.Base — last element; "" -> ".", "/" -> "/".
std::string pathBase(std::string_view path);
// path.IsAbs — leading '/'.
bool pathIsAbs(std::string_view path);

// path.ErrBadPattern.
inline const Error ErrBadPattern =
    Error::newError("syntax error in pattern");
// path.Match — shell-style pattern match on a slash-separated name.
std::pair<bool, Error> pathMatch(const std::string& pattern,
                                 const std::string& name);

// --- vfs.FS -------------------------------------------------------------

// Entries — vfs.go. symlinks holds the names of entries in files or
// directories that were originally symbolic links (or reparse points) on
// disk — the link names, not targets. nullopt means symlink information is
// not available and entries may need to be re-checked for symlinks.
struct Entries {
	std::vector<std::string> files;
	std::vector<std::string> directories;
	std::optional<std::unordered_set<std::string>> symlinks;

	bool operator==(const Entries&) const = default;
};

// FS is a file system abstraction — vfs.go.
struct FS {
	virtual ~FS() = default;

	// UseCaseSensitiveFileNames returns true if the file system is
	// case-sensitive.
	virtual bool UseCaseSensitiveFileNames() = 0;

	// FileExists returns true if the file exists.
	virtual bool FileExists(const std::string& path) = 0;

	// ReadFile reads the file specified by path; ok=false on failure.
	virtual std::pair<std::string, bool>
	ReadFile(const std::string& path) = 0;

	virtual Error WriteFile(const std::string& path,
	                        const std::string& data) = 0;

	// AppendFile appends data to the file at path, creating it if it does
	// not exist.
	virtual Error AppendFile(const std::string& path,
	                         const std::string& data) = 0;

	// Removes `path` and all its contents; the first error encountered.
	virtual Error Remove(const std::string& path) = 0;

	// Chtimes changes the access and modification times of the named file.
	virtual Error Chtimes(const std::string& path, TimePoint aTime,
	                      TimePoint mTime) = 0;

	// DirectoryExists returns true if the path is a directory.
	virtual bool DirectoryExists(const std::string& path) = 0;

	// GetAccessibleEntries returns the files/directories in the specified
	// directory. If any entry is a symlink, it will be followed.
	virtual Entries GetAccessibleEntries(const std::string& path) = 0;

	virtual std::shared_ptr<FileInfo> Stat(const std::string& path) = 0;

	// Realpath returns the "real path" of the specified path, following
	// symlinks and correcting filename casing.
	virtual std::string Realpath(const std::string& path) = 0;
};

// WalkDirFunc — fs.WalkDirFunc: (path, entry, err) -> error. entry is null
// when err is set.
using WalkDirFunc = std::function<Error(
    const std::string& path, const std::shared_ptr<DirEntry>& entry,
    const Error& err)>;

// WalkDir calls walkFn for root and each accessible descendant in lexical
// order. Symbolic links are reported but not followed. Directory read
// failures are indistinguishable from empty directories because
// FS.GetAccessibleEntries does not return errors. The FileInfo returned by
// DirEntry.Info for a symbolic link contains only its name and mode because
// FS does not provide lstat. Using DirEntry.Info() requires the FS to
// implement Stat().
Error WalkDir(FS& fileSystem, const std::string& root,
              const WalkDirFunc& walkFn);

} // namespace tsc::vfs
