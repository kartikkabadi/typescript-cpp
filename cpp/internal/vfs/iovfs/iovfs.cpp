// iovfs.cpp — port of tsc/internal/vfs/iovfs/iofs.go
#include "internal/vfs/iovfs/iovfs.h"

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/tspath/tspath.h"
#include "internal/vfs/internal/internal.h"

namespace tsc::vfs::iovfs {

namespace {

struct ioFS final : FsWithSys {
	vfs::internal::Common common;

	bool useCaseSensitiveFileNames;
	std::function<std::pair<std::string, Error>(const std::string&)>
	    realpath;
	std::function<Error(const std::string&, const std::string&)>
	    writeFile;
	std::function<Error(const std::string&, const std::string&)>
	    appendFile;
	std::function<Error(const std::string&)> mkdirAll;
	std::function<Error(const std::string&)> remove;
	std::function<Error(const std::string&, TimePoint, TimePoint)>
	    chtimes;
	std::shared_ptr<IoFS> fsys;

	bool UseCaseSensitiveFileNames() override {
		return useCaseSensitiveFileNames;
	}

	bool DirectoryExists(const std::string& path) override {
		return common.DirectoryExists(path);
	}

	bool FileExists(const std::string& path) override {
		return common.FileExists(path);
	}

	Entries GetAccessibleEntries(const std::string& path) override {
		return common.GetAccessibleEntries(path);
	}

	std::shared_ptr<FileInfo> Stat(const std::string& path) override {
		internal::rootLength(path); // Assert path is rooted
		return common.Stat(path);
	}

	std::pair<std::string, bool>
	ReadFile(const std::string& path) override {
		return common.ReadFile(path);
	}

	Error Remove(const std::string& path) override {
		internal::rootLength(path); // Assert path is rooted
		return remove(path);
	}

	Error Chtimes(const std::string& path, TimePoint aTime,
	              TimePoint mTime) override {
		internal::rootLength(path); // Assert path is rooted
		return chtimes(path, aTime, mTime);
	}

	std::string Realpath(const std::string& path) override {
		auto [root, rest] = internal::splitPath(path);
		// splitPath normalizes the path into parts (e.g. "c:/foo/bar" ->
		// "c:/", "foo/bar"). Put them back together to call realpath.
		auto [real, err] = realpath(root + rest);
		if (err) {
			return path;
		}
		return real;
	}

	Error writeFileEnsuringDir(
	    const std::string& path, const std::string& content,
	    const std::function<Error(const std::string&,
	                              const std::string&)>& write) {
		internal::rootLength(path); // Assert path is rooted
		if (auto err = write(path, content); !err) {
			return Error{};
		}
		if (auto err = mkdirAll(tspath::getDirectoryPath(
		        tspath::normalizePath(path)));
		    err) {
			return err;
		}
		return write(path, content);
	}

	Error WriteFile(const std::string& path,
	                const std::string& content) override {
		return writeFileEnsuringDir(path, content, writeFile);
	}

	Error AppendFile(const std::string& path,
	                 const std::string& content) override {
		return writeFileEnsuringDir(path, content, appendFile);
	}

	std::shared_ptr<IoFS> FSys() override { return fsys; }
};

// strings.CutPrefix(path, "/").
inline std::pair<std::string, bool> cutSlashPrefix(
    const std::string& path) {
	if (!path.empty() && path[0] == '/') {
		return {path.substr(1), true};
	}
	return {path, false};
}

} // namespace

std::shared_ptr<FsWithSys> From(std::shared_ptr<IoFS> fsys,
                                bool useCaseSensitiveFileNames) {
	std::function<std::pair<std::string, Error>(const std::string&)>
	    realpath;
	if (auto rfs = std::dynamic_pointer_cast<RealpathFS>(fsys)) {
		realpath = [rfs](const std::string& path)
		    -> std::pair<std::string, Error> {
			auto [rest, hadSlash] = cutSlashPrefix(path);
			auto [rp, err] = rfs->Realpath(rest);
			if (err) {
				return {"", err};
			}
			if (hadSlash) {
				return {"/" + rp, Error{}};
			}
			return {rp, Error{}};
		};
	} else {
		realpath = [](const std::string& path)
		    -> std::pair<std::string, Error> {
			return {path, Error{}};
		};
	}

	std::function<Error(const std::string&, const std::string&)>
	    writeFile, appendFile;
	std::function<Error(const std::string&)> mkdirAll, remove;
	std::function<Error(const std::string&, TimePoint, TimePoint)>
	    chtimes;
	if (auto wfs = std::dynamic_pointer_cast<WritableFS>(fsys)) {
		writeFile = [wfs](const std::string& path,
		                  const std::string& content) {
			return wfs->WriteFile(cutSlashPrefix(path).first, content,
			                      FileMode{0666});
		};
		appendFile = [wfs](const std::string& path,
		                   const std::string& content) {
			return wfs->AppendFile(cutSlashPrefix(path).first, content,
			                       FileMode{0666});
		};
		mkdirAll = [wfs](const std::string& path) {
			return wfs->MkdirAll(cutSlashPrefix(path).first,
			                     FileMode{0777});
		};
		remove = [wfs](const std::string& path) {
			return wfs->Remove(cutSlashPrefix(path).first);
		};
		chtimes = [wfs](const std::string& path, TimePoint aTime,
		                TimePoint mTime) {
			return wfs->Chtimes(cutSlashPrefix(path).first, aTime,
			                    mTime);
		};
	} else {
		writeFile = [](const std::string&, const std::string&) -> Error {
			TSC_UNREACHABLE("writeFile not supported");
		};
		appendFile = [](const std::string&,
		                const std::string&) -> Error {
			TSC_UNREACHABLE("appendFile not supported");
		};
		mkdirAll = [](const std::string&) -> Error {
			TSC_UNREACHABLE("mkdirAll not supported");
		};
		remove = [](const std::string&) -> Error {
			TSC_UNREACHABLE("remove not supported");
		};
		chtimes = [](const std::string&, TimePoint, TimePoint) -> Error {
			TSC_UNREACHABLE("chtimes not supported");
		};
	}

	auto result = std::make_shared<ioFS>();
	result->useCaseSensitiveFileNames = useCaseSensitiveFileNames;
	result->realpath = std::move(realpath);
	result->writeFile = std::move(writeFile);
	result->appendFile = std::move(appendFile);
	result->mkdirAll = std::move(mkdirAll);
	result->remove = std::move(remove);
	result->chtimes = std::move(chtimes);
	result->fsys = fsys;
	result->common.RootFor =
	    [fsys](const std::string& root) -> std::shared_ptr<IoFS> {
		if (root == "/") {
			return fsys;
		}

		auto p = tspath::removeTrailingDirectorySeparator(root);
		auto [sub, err] = fsSub(fsys, std::string{p});
		if (err) {
			if (tspath::isUrl(root)) {
				return nullptr;
			}
			TSC_UNREACHABLE(("vfs: failed to create sub file system "
			                 "for \"" +
			                 std::string{p} + "\": " + err.str())
			                    .c_str());
		}
		return sub;
	};
	return result;
}

} // namespace tsc::vfs::iovfs
