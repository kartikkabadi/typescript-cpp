// Port of tsc/internal/vfs/walkdir_test.go.
#include <string>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"
#include "internal/vfs/wrapvfs/wrapvfs.h"

using tsc::vfs::Error;
using tsc::gostd::testing::T;
using tsc::vfs::Entries;
using tsc::vfs::ErrNotExist;
using tsc::vfs::FileMode;
using tsc::vfs::ModeDir;
using tsc::vfs::ModeSymlink;
using tsc::vfs::SkipAll;
using tsc::vfs::SkipDir;
using tsc::vfs::WalkDir;
namespace assert = tsc::gotest::assert;

namespace {

// nilError — assert.NilError for vfs::Error (nil means success).
void nilError(T* t, const tsc::vfs::Error& err) {
	t->Helper();
	if (err) {
		t->Fatalf("assert.NilError failed: %s", {err.str()});
	}
}


void TestWalkDir(T* t) {
	t->Parallel();

	auto base = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string,
	                       tsc::vfs::vfstest::MapFileInput>{
	        {"/root/a.ts", ""},
	        {"/root/dir/b.ts", ""},
	        {"/root/link/hidden.ts", ""},
	        {"/target/hidden.ts", ""},
	    },
	    true);
	tsc::vfs::wrapvfs::Replacements rep;
	rep.GetAccessibleEntries = [base](const std::string& path) {
		auto entries = base->GetAccessibleEntries(path);
		entries.symlinks.reset();
		if (path == "/root") {
			entries.files.push_back("C:/foreign.ts");
		}
		return entries;
	};
	rep.Realpath = [](const std::string& path) {
		if (path == "/root/link") {
			return std::string("/target");
		}
		if (path == "/root/link/hidden.ts") {
			return std::string("/target/hidden.ts");
		}
		return path;
	};
	auto fileSystem = tsc::vfs::wrapvfs::Wrap(base, rep);

	std::vector<std::string> paths;
	std::vector<FileMode> modes;
	auto err = WalkDir(
	    *fileSystem, "/root",
	    [t, &paths, &modes](const std::string& path,
	                        const std::shared_ptr<tsc::vfs::DirEntry>& entry,
	                        const Error& err) -> Error {
		    nilError(t, err);
		    paths.push_back(path);
		    modes.push_back(entry->Type());
		    if ((entry->Type().v & FileMode::kSymlink) != 0) {
			    auto [info, infoErr] = entry->Info();
			    nilError(t, infoErr);
			    assert::Equal(t, info->Mode(), ModeSymlink);
			    assert::Assert(t, !info->IsDir());
		    }
		    return Error{};
	    });
	nilError(t, err);
	assert::Equal(t, paths,
	              std::vector<std::string>{"/root", "/root/a.ts",
	                                       "/root/dir", "/root/dir/b.ts",
	                                       "/root/link"});
	assert::Equal(t, modes,
	              std::vector<FileMode>{ModeDir, FileMode{}, ModeDir,
	                                    FileMode{}, ModeSymlink});
}

void TestWalkDirDoesNotFollowRootSymlink(T* t) {
	t->Parallel();

	auto base = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string,
	                       tsc::vfs::vfstest::MapFileInput>{
	        {"/root/link/hidden.ts", ""},
	        {"/target/hidden.ts", ""},
	    },
	    true);
	tsc::vfs::wrapvfs::Replacements rep;
	rep.GetAccessibleEntries = [base](const std::string& path) {
		auto entries = base->GetAccessibleEntries(path);
		entries.symlinks.reset();
		return entries;
	};
	rep.Realpath = [](const std::string& path) {
		if (path == "/root/link") {
			return std::string("/target");
		}
		return path;
	};
	auto fileSystem = tsc::vfs::wrapvfs::Wrap(base, rep);

	std::vector<std::string> paths;
	auto err = WalkDir(
	    *fileSystem, "/root/link",
	    [t, &paths](const std::string& path,
	                const std::shared_ptr<tsc::vfs::DirEntry>& entry,
	                const Error& err) -> Error {
		    nilError(t, err);
		    paths.push_back(path);
		    assert::Equal(t, entry->Type(), ModeSymlink);
		    return Error{};
	    });
	nilError(t, err);
	assert::Equal(t, paths, std::vector<std::string>{"/root/link"});
}

void TestWalkDirReportsRootFileSymlink(T* t) {
	t->Parallel();

	auto base = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string,
	                       tsc::vfs::vfstest::MapFileInput>{
	        {"/target/file.ts", ""},
	    },
	    true);
	tsc::vfs::wrapvfs::Replacements rep;
	rep.Stat = [base](const std::string& path) {
		if (path == "/root/link.ts") {
			return base->Stat("/target/file.ts");
		}
		return base->Stat(path);
	};
	rep.Realpath = [](const std::string& path) {
		if (path == "/root/link.ts") {
			return std::string("/target/file.ts");
		}
		return path;
	};
	auto fileSystem = tsc::vfs::wrapvfs::Wrap(base, rep);

	auto err = WalkDir(
	    *fileSystem, "/root/link.ts",
	    [t](const std::string& path,
	        const std::shared_ptr<tsc::vfs::DirEntry>& entry,
	        const Error& err) -> Error {
		    nilError(t, err);
		    assert::Equal(t, path, "/root/link.ts");
		    assert::Equal(t, entry->Name(), "link.ts");
		    assert::Equal(t, entry->Type(), ModeSymlink);
		    return Error{};
	    });
	nilError(t, err);
}

void TestWalkDirSkipDir(T* t) {
	t->Parallel();

	auto fileSystem = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string,
	                       tsc::vfs::vfstest::MapFileInput>{
	        {"/root/a/hidden.ts", ""},
	        {"/root/b.ts", ""},
	    },
	    true);
	std::vector<std::string> paths;
	auto err = WalkDir(
	    *fileSystem, "/root",
	    [t, &paths](const std::string& path,
	                const std::shared_ptr<tsc::vfs::DirEntry>& entry,
	                const Error& err) -> Error {
		    nilError(t, err);
		    paths.push_back(path);
		    if (path == "/root/a") {
			    return SkipDir;
		    }
		    return Error{};
	    });
	nilError(t, err);
	assert::Equal(t, paths,
	              std::vector<std::string>{"/root", "/root/a",
	                                       "/root/b.ts"});
}

void TestWalkDirSkipAll(T* t) {
	t->Parallel();

	auto fileSystem = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string,
	                       tsc::vfs::vfstest::MapFileInput>{
	        {"/root/a.ts", ""},
	        {"/root/b.ts", ""},
	    },
	    true);
	std::vector<std::string> paths;
	auto err = WalkDir(
	    *fileSystem, "/root",
	    [t, &paths](const std::string& path,
	                const std::shared_ptr<tsc::vfs::DirEntry>& entry,
	                const Error& err) -> Error {
		    nilError(t, err);
		    paths.push_back(path);
		    if (path == "/root/a.ts") {
			    return SkipAll;
		    }
		    return Error{};
	    });
	nilError(t, err);
	assert::Equal(t, paths,
	              std::vector<std::string>{"/root", "/root/a.ts"});
}

void TestWalkDirConsumesSkipDirForRootFile(T* t) {
	t->Parallel();

	auto fileSystem = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string,
	                       tsc::vfs::vfstest::MapFileInput>{
	        {"/root.ts", ""},
	    },
	    true);
	auto err = WalkDir(
	    *fileSystem, "/root.ts",
	    [t](const std::string& path,
	        const std::shared_ptr<tsc::vfs::DirEntry>& entry,
	        const Error& err) -> Error {
		    nilError(t, err);
		    return SkipDir;
	    });
	nilError(t, err);
}

void TestWalkDirConsumesSkipForMissingRoot(T* t) {
	t->Parallel();

	auto fileSystem = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string,
	                       tsc::vfs::vfstest::MapFileInput>{},
	    true);
	for (auto sentinel : {SkipDir, SkipAll}) {
		auto err = WalkDir(
		    *fileSystem, "/missing",
		    [t, sentinel](const std::string& path,
		                  const std::shared_ptr<tsc::vfs::DirEntry>& entry,
		                  const Error& err) -> Error {
			    assert::Assert(t, err.is(ErrNotExist));
			    return sentinel;
		    });
		nilError(t, err);
	}
}

void TestWalkDirUsesSymlinkMetadataWithoutRealpathCalls(T* t) {
	t->Parallel();

	auto base = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string,
	                       tsc::vfs::vfstest::MapFileInput>{
	        {"/root/dir/file.ts", ""},
	    },
	    true);
	std::vector<std::string> realpathCalls;
	tsc::vfs::wrapvfs::Replacements rep;
	rep.Realpath = [&realpathCalls](const std::string& path) {
		realpathCalls.push_back(path);
		return path;
	};
	auto fileSystem = tsc::vfs::wrapvfs::Wrap(base, rep);

	auto err = WalkDir(
	    *fileSystem, "/root",
	    [](const std::string& path,
	       const std::shared_ptr<tsc::vfs::DirEntry>& entry,
	       const Error& err) -> Error { return err; });
	nilError(t, err);
	assert::Assert(t,
	               std::find(realpathCalls.begin(), realpathCalls.end(),
	                         "/root/dir") == realpathCalls.end());
}

} // namespace

REGISTER_UNIT_TEST("vfs.TestWalkDir", TestWalkDir);
REGISTER_UNIT_TEST("vfs.TestWalkDirDoesNotFollowRootSymlink",
                   TestWalkDirDoesNotFollowRootSymlink);
REGISTER_UNIT_TEST("vfs.TestWalkDirReportsRootFileSymlink",
                   TestWalkDirReportsRootFileSymlink);
REGISTER_UNIT_TEST("vfs.TestWalkDirSkipDir", TestWalkDirSkipDir);
REGISTER_UNIT_TEST("vfs.TestWalkDirSkipAll", TestWalkDirSkipAll);
REGISTER_UNIT_TEST("vfs.TestWalkDirConsumesSkipDirForRootFile",
                   TestWalkDirConsumesSkipDirForRootFile);
REGISTER_UNIT_TEST("vfs.TestWalkDirConsumesSkipForMissingRoot",
                   TestWalkDirConsumesSkipForMissingRoot);
REGISTER_UNIT_TEST("vfs.TestWalkDirUsesSymlinkMetadataWithoutRealpathCalls",
                   TestWalkDirUsesSymlinkMetadataWithoutRealpathCalls);
