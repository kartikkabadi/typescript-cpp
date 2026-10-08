// Port of tsc/internal/api/requestfilesystem/requestfilesystem_test.go
// (package requestfilesystem).
#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/api/requestfilesystem/requestfilesystem.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/project/filechange.h"
#include "internal/project/overlayfs.h"
#include "internal/project/session.h"
#include "internal/stringutil/stringutil.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/trackingvfs/trackingvfs.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::api::requestfilesystem {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace lsproto = tsc::lsp::lsproto;
namespace projecttestutil = tsc::testutil::projecttestutil;

// countingLayeredFileSystem — requestfilesystem_test.go:20.
struct countingLayeredFileSystem final : project::LayeredFileSystem {
	project::LayeredFileSystem* inner = nullptr;
	int getFileCalls = 0;

	project::FileHandle* GetFile(const std::string& fileName) override {
		getFileCalls++;
		return inner->GetFile(fileName);
	}
	project::FileHandle* GetFileByPath(const std::string& fileName,
	                                   const tspath::Path& path) override {
		getFileCalls++;
		return inner->GetFileByPath(fileName, path);
	}
	std::unordered_map<tspath::Path, project::Overlay*>
	Overlays() override {
		return inner->Overlays();
	}
	bool UseCaseSensitiveFileNames() override {
		return inner->UseCaseSensitiveFileNames();
	}
	std::pair<std::string, bool>
	ReadFile(const std::string& fileName) override {
		return inner->ReadFile(fileName);
	}
	bool FileExists(const std::string& fileName) override {
		return inner->FileExists(fileName);
	}
	bool DirectoryExists(const std::string& directoryName) override {
		return inner->DirectoryExists(directoryName);
	}
	vfs::Entries
	GetAccessibleEntries(const std::string& directoryName) override {
		return inner->GetAccessibleEntries(directoryName);
	}
	std::string Realpath(const std::string& path) override {
		return inner->Realpath(path);
	}
	vfs::Error WriteFile(const std::string& fileName,
	                     const std::string& data) override {
		return inner->WriteFile(fileName, data);
	}
	vfs::Error AppendFile(const std::string& fileName,
	                      const std::string& data) override {
		return inner->AppendFile(fileName, data);
	}
	vfs::Error Remove(const std::string& path) override {
		return inner->Remove(path);
	}
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint aTime,
	                   vfs::TimePoint mTime) override {
		return inner->Chtimes(path, aTime, mTime);
	}
	std::shared_ptr<vfs::FileInfo> Stat(const std::string& path) override {
		return inner->Stat(path);
	}
};

// newLayeredRequestFileSystem — requestfilesystem_test.go:39.
std::pair<std::shared_ptr<requestFileSystem>, gostd::Error>
newLayeredRequestFileSystem(RequestFileSystem* params,
                            const std::shared_ptr<vfs::FS>& base,
                            const std::string& currentDirectory) {
	project::FileChangeSummary fileChanges;
	auto [fileSystem, err] =
	    NewForUpdate(params, base, currentDirectory, &fileChanges);
	if (err != nullptr) {
		return {nullptr, err};
	}
	return {std::shared_ptr<requestFileSystem>(
	            fileSystem,
	            dynamic_cast<requestFileSystem*>(fileSystem.get())),
	        nullptr};
}

// newRequestFileSystem — requestfilesystem_test.go:35.
std::pair<std::shared_ptr<requestFileSystem>, gostd::Error>
newRequestFileSystem(RequestFileSystem* params,
                     const std::shared_ptr<vfs::FS>& base,
                     const std::string& currentDirectory) {
	return newLayeredRequestFileSystem(params, base, currentDirectory);
}

std::shared_ptr<vfs::FS> mkHost(
    std::unordered_map<std::string, vfs::vfstest::MapFileInput> files =
        {},
    bool caseSensitive = true) {
	return vfs::vfstest::FromMap(files, caseSensitive);
}

std::shared_ptr<vfs::trackingvfs::FS> mkTrackingHost(
    std::unordered_map<std::string, vfs::vfstest::MapFileInput> files = {},
    bool caseSensitive = true) {
	// trackingvfs::FS::Inner is a raw pointer — the MapFS owner must
	// outlive it, so the shared_ptr is leaked (Go's GC keeps it alive).
	static std::vector<std::shared_ptr<vfs::FS>> leakedHosts;
	leakedHosts.push_back(vfs::vfstest::FromMap(files, caseSensitive));
	return std::make_shared<vfs::trackingvfs::FS>(
	    leakedHosts.back().get());
}

void clearSeen(vfs::trackingvfs::FS* host) {
	for (const auto& seen : host->SeenFiles.ToSlice()) {
		host->SeenFiles.Delete(seen);
	}
}

// statProjection — the anonymous struct returned by verify("Stat").
struct statProjection {
	std::string Name;
	int64_t Size;
	uint32_t Mode;
	vfs::TimePoint ModTime;
	bool Directory;
	std::any Sys;
};

// deepEqualAny — reflect.DeepEqual over the `any` shapes verify() produces.
bool deepEqualAny(const std::any& a, const std::any& b) {
	if (a.type() != b.type()) {
		return false;
	}
	if (a.type() == typeid(std::pair<std::string, bool>)) {
		return std::any_cast<std::pair<std::string, bool>>(a) ==
		       std::any_cast<std::pair<std::string, bool>>(b);
	}
	if (a.type() == typeid(vfs::Entries)) {
		return std::any_cast<vfs::Entries>(a) ==
		       std::any_cast<vfs::Entries>(b);
	}
	if (a.type() == typeid(statProjection)) {
		const auto& x = std::any_cast<const statProjection&>(a);
		const auto& y = std::any_cast<const statProjection&>(b);
		return x.Name == y.Name && x.Size == y.Size && x.Mode == y.Mode &&
		       x.ModTime == y.ModTime && x.Directory == y.Directory &&
		       tsc::gotest::assert::detail::anyEqual(x.Sys, y.Sys);
	}
	return tsc::gotest::assert::detail::anyEqual(a, b);
}

// verifyCompactionWithoutHostReads — requestfilesystem_test.go:48.
void verifyCompactionWithoutHostReads(T* t, requestFileSystem* layer,
                                      vfs::trackingvfs::FS* host,
                                      const std::vector<std::string>& paths) {
	t->Helper();
	RequestFileSystem params{.kind = KindLayer};
	auto [compacted, err] =
	    newRequestFileSystem(&params,
	                         std::shared_ptr<vfs::FS>(
	                             layer, [](vfs::FS*) {}),
	                         layer->currentDirectory);
	assert::NilError(t, err);
	requestFileSystem* compactedPtr = compacted.get();
	auto verify = [&](const std::string& name,
	                  const std::function<std::any(vfs::FS*,
	                                               const std::string&)>&
	                      run) {
		t->Helper();
		for (const auto& path : paths) {
			clearSeen(host);
			std::any expected = run(layer, path);
			if (!host->SeenFiles.IsEmpty()) {
				continue;
			}
			std::any actual = run(compactedPtr, path);
			assert::Assert(t, host->SeenFiles.IsEmpty(),
			               name + " " + path);
			assert::Assert(t, deepEqualAny(actual, expected),
			               name + " " + path);
		}
	};
	verify("FileExists", [](vfs::FS* fs, const std::string& path) {
		return std::any(fs->FileExists(path));
	});
	verify("DirectoryExists", [](vfs::FS* fs, const std::string& path) {
		return std::any(fs->DirectoryExists(path));
	});
	verify("ReadFile", [](vfs::FS* fs, const std::string& path) {
		auto [content, ok] = fs->ReadFile(path);
		return std::any(std::pair<std::string, bool>(content, ok));
	});
	verify("Realpath", [](vfs::FS* fs, const std::string& path) {
		return std::any(fs->Realpath(path));
	});
	verify("GetAccessibleEntries",
	       [](vfs::FS* fs, const std::string& path) {
		       return std::any(fs->GetAccessibleEntries(path));
	       });
	verify("Stat", [](vfs::FS* fs, const std::string& path) {
		auto info = fs->Stat(path);
		if (info == nullptr) {
			return std::any();
		}
		return std::any(statProjection{
		    .Name = info->Name(),
		    .Size = info->Size(),
		    .Mode = info->Mode().v,
		    .ModTime = info->ModTime(),
		    .Directory = info->IsDir(),
		    .Sys = info->Sys(),
		});
	});
}

bool sliceContains(const std::vector<std::string>& v,
                   const std::string& s) {
	return std::find(v.begin(), v.end(), s) != v.end();
}

std::string upperStr(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (size_t p = 0; p < s.size();) {
		int w = 0;
		char32_t r = decodeUtf8Rune(
		    std::string_view(s).substr(p), &w);
		if (w <= 0) {
			w = 1;
		}
		p += w;
		char buf[4];
		out.append(buf, encodeUtf8Rune(
		                    stringutil::toUpperRune(r), buf));
	}
	return out;
}

bool hasSymlinkEntry(const vfs::Entries& e, const std::string& name) {
	return e.symlinks.has_value() && e.symlinks->count(name) != 0;
}

void TestInitializeForUpdate(T* t) {
	t->Parallel();

	t->Run("filesystem layers eagerly compact a request filesystem base",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost();
		project::FileChangeSummary fileChanges;
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {{"/base.ts", "base"}},
		};
		auto [base, err] =
		    NewForUpdate(&baseParams, host, "/", &fileChanges);
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {{"/layered.ts", "layered"}},
		};
		auto [layered, err2] =
		    NewForUpdate(&layerParams, base, "/", &fileChanges);
		assert::NilError(t, err2);
		auto* rfs = dynamic_cast<requestFileSystem*>(layered.get());
		assert::Assert(t, rfs != nullptr);
		assert::Assert(t, rfs->baseFileSystem() == host.get());
		assert::Equal(t, rfs->kind, KindFull);
		assert::Assert(t, layered->FileExists("/base.ts"));
		assert::Assert(t, layered->FileExists("/layered.ts"));
	});

	t->Run("filesystem layers over a host-backed snapshot", [](T* t) {
		t->Parallel();
		auto host = mkTrackingHost(
		    {{"/dir/host.ts", "host"}});
		project::FileChangeSummary fileChanges;
		RequestFileSystem params{
		    .kind = KindLayer,
		    .Files = {{"/dir/cached.ts", "cached"}},
		    .Directories =
		        std::map<std::string, RequestDirectoryEntries>{
		            {"/dir",
		             {.Files = {"cached.ts"},
		              .Directories = {}}}},
		};
		auto [fileSystem, err] =
		    NewForUpdate(&params,
		                 std::static_pointer_cast<vfs::FS>(host), "/",
		                 &fileChanges);
		assert::NilError(t, err);
		// Change generation may inspect the old directory; reading the
		// supplied complete listing itself must not fall back to the host.
		host->SeenFiles.Delete("/dir");
		assert::DeepEqual(
		    t, fileSystem->GetAccessibleEntries("/dir").files,
		    std::vector<std::string>{"cached.ts"});
		assert::Assert(t, !host->SeenFiles.Has("/dir"));
	});

	t->Run("memory starts a new chain", [](T* t) {
		t->Parallel();
		auto host = mkHost({{"/host.ts", "host"}});
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {{"/base.ts", "base"}},
		};
		auto [base, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		project::FileChangeSummary fileChanges;
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {{"/replacement.ts", "replacement"}},
		};
		auto [fileSystem, err2] =
		    NewForUpdate(&params,
		                 std::shared_ptr<vfs::FS>(
		                     base.get(), [](vfs::FS*) {}),
		                 "/", &fileChanges);
		assert::NilError(t, err2);
		auto* rfs = getRequestFileSystem(fileSystem);
		assert::Assert(t, rfs->baseFileSystem() == host.get());
		assert::Assert(t, getRequestFileSystem(
		                    rfs->baseFileSystem()) == nullptr);
	});
}
REGISTER_UNIT_TEST("requestfilesystem.TestInitializeForUpdate",
                   TestInitializeForUpdate);

void testCompleteDirectoryListing(T* t, Kind kind, bool explicit_,
                                  const RequestDirectoryEntries& replacement) {
	t->Helper();
	auto host = mkTrackingHost({
	    {"/dir/host.ts", "host"},
	    {"/dir/host-dir/index.ts", "host child"},
	});
	auto params = std::make_unique<RequestFileSystem>();
	params->kind = kind;
	params->Files = {
	    {"/dir/base.ts", "base"},
	    {"/dir/base-dir/index.ts", "base child"},
	};
	if (explicit_) {
		params->Directories =
		    std::map<std::string, RequestDirectoryEntries>{
		        {"/dir",
		         {.Files = {"base.ts"},
		          .Directories = {"base-dir"}}},
		    };
	}
	auto [base, err] =
	    newRequestFileSystem(params.get(),
	                         std::static_pointer_cast<vfs::FS>(host), "/");
	assert::NilError(t, err);
	RequestFileSystem layerParams{
	    .kind = KindLayer,
	    .Directories = std::map<std::string, RequestDirectoryEntries>{
	        {"/dir", replacement}},
	};
	auto [layered, err2] = newLayeredRequestFileSystem(
	    &layerParams,
	    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err2);

	// Omitting a listing in a later update still merges its derived
	// entries with the complete listing, without reopening host fallback.
	RequestFileSystem nextParams{
	    .kind = KindLayer,
	    .Files = {
	        {"/dir/added.ts", "added"},
	        {"/dir/added-dir/index.ts", "added child"},
	    },
	};
	auto [next, err3] = newLayeredRequestFileSystem(
	    &nextParams,
	    std::shared_ptr<vfs::FS>(layered.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err3);
	clearSeen(host.get());

	requestFileSystem* layeredPtr = layered.get();
	requestFileSystem* nextPtr = next.get();
	auto verify = [&]() {
		t->Helper();
		auto entries = layeredPtr->GetAccessibleEntries("/dir");
		assert::Assert(t, host->SeenFiles.IsEmpty());
		assert::DeepEqual(t, entries.files, replacement.Files);
		assert::DeepEqual(t, entries.directories,
		                  replacement.Directories);
		entries = nextPtr->GetAccessibleEntries("/dir");
		assert::Assert(t, host->SeenFiles.IsEmpty());
		std::vector<std::string> expectedFiles{"added.ts"};
		expectedFiles.insert(expectedFiles.end(),
		                     replacement.Files.begin(),
		                     replacement.Files.end());
		std::vector<std::string> expectedDirs{"added-dir"};
		expectedDirs.insert(expectedDirs.end(),
		                    replacement.Directories.begin(),
		                    replacement.Directories.end());
		assert::DeepEqual(t, entries.files, expectedFiles);
		assert::DeepEqual(t, entries.directories, expectedDirs);
	};
	verify();
	assert::Assert(t, layeredPtr->baseFileSystem() == host.get());
	assert::Assert(t, nextPtr->baseFileSystem() == host.get());
	verify();
}

void TestRequestFileSystemCompleteDirectoryListingsFullExplicitReplacement(
    T* t) {
	t->Parallel();
	testCompleteDirectoryListing(
	    t, KindFull, true,
	    RequestDirectoryEntries{.Files = {"replacement.ts"},
	                            .Directories = {"replacement-dir"}});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemCompleteDirectoryListingsFullExplicitReplacement",
                   TestRequestFileSystemCompleteDirectoryListingsFullExplicitReplacement);

void TestRequestFileSystemPreservesExplicitDirectoryOrder(T* t) {
	t->Parallel();
	auto host = mkHost();
	RequestFileSystem params{
	    .kind = KindFull,
	    .Files = {
	        {"/src/index.ts", ""},
	        {"/src/foo.ts", ""},
	    },
	    .Directories = std::map<std::string, RequestDirectoryEntries>{
	        {"/src", {.Files = {"index.ts", "foo.ts"}}}},
	};
	auto [fileSystem, err] =
	    newRequestFileSystem(&params, host, "/");
	assert::NilError(t, err);
	assert::DeepEqual(t, fileSystem->GetAccessibleEntries("/src").files,
	              std::vector<std::string>({"index.ts", "foo.ts"}));
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemPreservesExplicitDirectoryOrder",
                   TestRequestFileSystemPreservesExplicitDirectoryOrder);

void TestRequestFileSystemDerivesDirectoryListingsWithHostCaseSensitivity(
    T* t) {
	t->Parallel();

	RequestFileSystem params{
	    .kind = KindFull,
	    .Files = {
	        {"C:/Repo/upper.ts", "upper"},
	        {"c:/repo/lower.ts", "lower"},
	    },
	};

	auto caseInsensitive = mkHost({}, false);
	auto [fileSystem, err] =
	    newRequestFileSystem(&params, caseInsensitive, "C:/Workspace");
	assert::NilError(t, err);
	assert::DeepEqual(
	    t, fileSystem->GetAccessibleEntries("C:/REPO").files,
	    std::vector<std::string>({"lower.ts", "upper.ts"}));
	assert::DeepEqual(
	    t, fileSystem->GetAccessibleEntries("C:/").directories,
	    std::vector<std::string>({"Repo", "Workspace"}));

	auto caseSensitive = mkHost({}, true);
	auto [fileSystem2, err2] =
	    newRequestFileSystem(&params, caseSensitive, "C:/Workspace");
	assert::NilError(t, err2);
	assert::DeepEqual(t,
	                  fileSystem2->GetAccessibleEntries("C:/Repo").files,
	                  std::vector<std::string>{"upper.ts"});
	assert::DeepEqual(t,
	                  fileSystem2->GetAccessibleEntries("c:/repo").files,
	                  std::vector<std::string>{"lower.ts"});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemDerivesDirectoryListingsWithHostCaseSensitivity",
                   TestRequestFileSystemDerivesDirectoryListingsWithHostCaseSensitivity);

void TestRequestFileSystemOverlaysDoesNotReadFileHandles(T* t) {
	t->Parallel();
	auto [session, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{{"/index.ts", "host"}});
	struct sessionCloser {
		project::Session* s;
		~sessionCloser() { s->Close(); }
	} closer{session};
	session->DidOpenFile(gostd::contextBackground(),
	                     lsproto::DocumentUri("file:///index.ts"), 1,
	                     "overlay", lsproto::LanguageKindTypeScript);

	auto* layeredFS =
	    dynamic_cast<project::LayeredFileSystem*>(session->FS());
	assert::Assert(t, layeredFS != nullptr,
	               "session FS is a LayeredFileSystem");
	auto baseOwner = std::make_shared<countingLayeredFileSystem>();
	baseOwner->inner = layeredFS;
	countingLayeredFileSystem* base = baseOwner.get();
	RequestFileSystem params{
	    .kind = KindLayer,
	    .RemovedPaths = std::vector<std::string>{"/index.ts"},
	};
	auto [fileSystem, err] = newRequestFileSystem(&params, baseOwner, "/");
	assert::NilError(t, err);
	base->getFileCalls = 0;

	assert::Equal(t, int(fileSystem->Overlays().size()), 0,
	              "overlays");
	assert::Equal(t, base->getFileCalls, 0, "getFileCalls");
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemOverlaysDoesNotReadFileHandles",
                   TestRequestFileSystemOverlaysDoesNotReadFileHandles);

void TestRequestFileSystemCompleteDirectoryListingsFullExplicitEmpty(T* t) {
	t->Parallel();
	testCompleteDirectoryListing(
	    t, KindFull, true,
	    RequestDirectoryEntries{.Files = {}, .Directories = {}});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemCompleteDirectoryListingsFullExplicitEmpty",
                   TestRequestFileSystemCompleteDirectoryListingsFullExplicitEmpty);

void TestRequestFileSystemCompleteDirectoryListingsFullDerivedReplacement(
    T* t) {
	t->Parallel();
	testCompleteDirectoryListing(
	    t, KindFull, false,
	    RequestDirectoryEntries{.Files = {"replacement.ts"},
	                            .Directories = {"replacement-dir"}});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemCompleteDirectoryListingsFullDerivedReplacement",
                   TestRequestFileSystemCompleteDirectoryListingsFullDerivedReplacement);

void TestRequestFileSystemCompleteDirectoryListingsFullDerivedEmpty(T* t) {
	t->Parallel();
	testCompleteDirectoryListing(
	    t, KindFull, false,
	    RequestDirectoryEntries{.Files = {}, .Directories = {}});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemCompleteDirectoryListingsFullDerivedEmpty",
                   TestRequestFileSystemCompleteDirectoryListingsFullDerivedEmpty);

void TestRequestFileSystemCompleteDirectoryListingsLayerExplicitReplacement(
    T* t) {
	t->Parallel();
	testCompleteDirectoryListing(
	    t, KindLayer, true,
	    RequestDirectoryEntries{.Files = {"replacement.ts"},
	                            .Directories = {"replacement-dir"}});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemCompleteDirectoryListingsLayerExplicitReplacement",
                   TestRequestFileSystemCompleteDirectoryListingsLayerExplicitReplacement);

void TestRequestFileSystemCompleteDirectoryListingsLayerExplicitEmpty(T* t) {
	t->Parallel();
	testCompleteDirectoryListing(
	    t, KindLayer, true,
	    RequestDirectoryEntries{.Files = {}, .Directories = {}});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemCompleteDirectoryListingsLayerExplicitEmpty",
                   TestRequestFileSystemCompleteDirectoryListingsLayerExplicitEmpty);

void TestRequestFileSystemCompleteDirectoryListingsLayerDerivedReplacement(
    T* t) {
	t->Parallel();
	testCompleteDirectoryListing(
	    t, KindLayer, false,
	    RequestDirectoryEntries{.Files = {"replacement.ts"},
	                            .Directories = {"replacement-dir"}});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemCompleteDirectoryListingsLayerDerivedReplacement",
                   TestRequestFileSystemCompleteDirectoryListingsLayerDerivedReplacement);

void TestRequestFileSystemCompleteDirectoryListingsLayerDerivedEmpty(T* t) {
	t->Parallel();
	testCompleteDirectoryListing(
	    t, KindLayer, false,
	    RequestDirectoryEntries{.Files = {}, .Directories = {}});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemCompleteDirectoryListingsLayerDerivedEmpty",
                   TestRequestFileSystemCompleteDirectoryListingsLayerDerivedEmpty);

struct symlinkReplacementOptions {
	Kind kind;
	bool hostTarget = false;
	bool caseSensitive = true;
	bool remove = false;
	std::string linkPath;
};

void testSymlinkReplacesDirectory(T* t,
                                  const symlinkReplacementOptions& options) {
	t->Helper();
	std::string linkPath = options.linkPath;
	bool remove = options.remove;
	auto host =
	    mkHost({
	        {"/dir/removed/old.ts", "old host"},
	        {"/dir/removed/child/old.ts", "old host child"},
	        {"/dir/removed/sibling.ts", "old sibling"},
	        {"/dir/removed-other/old.ts", "unrelated"},
	        {"/target/new.ts", "host target"},
	        {"/target/removed/new.ts", "host target"},
	    },
	    options.caseSensitive);
	auto params = std::make_unique<RequestFileSystem>();
	params->kind = options.kind;
	params->Files = {
	    {"/dir/removed/cached.ts", "cached"},
	    {"/dir/removed/child/cached.ts", "cached child"},
	    {"/dir/removed-other/old.ts", "unrelated"},
	};
	params->Directories = std::map<std::string, RequestDirectoryEntries>{
	    {"/dir", {.Directories = {"removed", "removed-other"}}},
	    {"/dir/removed",
	     {.Files = {"cached.ts"}, .Directories = {"child"}}},
	    {"/dir/removed/child", {.Files = {"cached.ts"}}},
	};
	std::string expectedContent = "host target";
	if (options.kind == KindFull) {
		params->Files["/target/new.ts"] = "request target";
		params->Files["/target/removed/new.ts"] = "request target";
		if (!options.hostTarget) {
			expectedContent = "request target";
		}
	}
	auto [base, err] = newRequestFileSystem(params.get(), host, "/");
	assert::NilError(t, err);
	std::shared_ptr<requestFileSystem> previous = base;
	if (remove) {
		std::string removedPath = "/dir/removed";
		if (!options.caseSensitive) {
			removedPath = upperStr(removedPath);
		}
		RequestFileSystem rm{
		    .kind = KindLayer,
		    .RemovedPaths = std::vector<std::string>{removedPath},
		};
		auto [p, e] = newLayeredRequestFileSystem(
		    &rm,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		assert::NilError(t, e);
		previous = p;
	}
	auto verifyPrevious = [&]() {
		t->Helper();
		assert::Equal(t, previous->DirectoryExists("/dir/removed"),
		              !remove);
		assert::Equal(t, previous->FileExists("/dir/removed/cached.ts"),
		              !remove);
		if (remove) {
			assert::Assert(
			    t, !previous->FileExists("/dir/removed/old.ts"));
			assert::Equal(t,
			              int(previous->GetAccessibleEntries(
			                      "/dir/removed")
			                      .files.size()),
			              0);
			assert::Equal(t,
			              int(previous->GetAccessibleEntries(
			                      "/dir/removed")
			                      .directories.size()),
			              0);
		} else {
			assert::DeepEqual(t,
			                  previous->GetAccessibleEntries(
			                      "/dir/removed")
			                      .files,
			                  std::vector<std::string>{"cached.ts"});
			assert::DeepEqual(t,
			                  previous->GetAccessibleEntries(
			                      "/dir/removed")
			                      .directories,
			                  std::vector<std::string>{"child"});
		}
	};
	verifyPrevious();
	RequestFileSystem linkParams{
	    .kind = KindLayer,
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {linkPath,
	         {.Target = "/target", .Host = options.hostTarget}}},
	};
	auto [linked, err2] = newLayeredRequestFileSystem(
	    &linkParams,
	    std::shared_ptr<vfs::FS>(previous.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err2);
	auto verifyLinked = [&](requestFileSystem* fileSystem) {
		t->Helper();
		assert::Assert(t, fileSystem->baseFileSystem() == host.get());
		assert::Equal(t, fileSystem->kind, options.kind);
		assert::Assert(t, fileSystem->DirectoryExists(linkPath));
		assert::Assert(t, !fileSystem->FileExists(linkPath));
		for (const auto& suffix : {"/new.ts", "/removed/new.ts"}) {
			std::string fileName = linkPath + suffix;
			assert::Assert(t, fileSystem->FileExists(fileName),
			               fileName);
			auto [content, ok] = fileSystem->ReadFile(fileName);
			assert::Assert(t, ok);
			assert::Equal(t, content, expectedContent);
			auto info = fileSystem->Stat(fileName);
			assert::Assert(t, info != nullptr);
			assert::Assert(t, !info->IsDir());
			assert::Equal(t, info->Size(),
			              int64_t(expectedContent.size()));
			assert::Equal(t, fileSystem->Realpath(fileName),
			              std::string("/target") + suffix);
		}
		auto info = fileSystem->Stat(linkPath);
		assert::Assert(t, info != nullptr);
		assert::Assert(t, info->IsDir());
		assert::Equal(t, fileSystem->Realpath(linkPath),
		              std::string("/target"));
		assert::DeepEqual(
		    t, fileSystem->GetAccessibleEntries(linkPath).files,
		    std::vector<std::string>{"new.ts"});
		assert::DeepEqual(
		    t,
		    fileSystem->GetAccessibleEntries(linkPath).directories,
		    std::vector<std::string>{"removed"});
		auto parentEntries = fileSystem->GetAccessibleEntries(
		    tspath::getDirectoryPath(linkPath));
		std::string linkName =
		    std::string(tspath::getBaseFileName(linkPath));
		assert::Assert(
		    t, sliceContains(parentEntries.directories, linkName));
		assert::Assert(t, hasSymlinkEntry(parentEntries, linkName));
		assert::Assert(t,
		               !fileSystem->FileExists(linkPath + "/old.ts"));
		assert::Assert(
		    t, !fileSystem->FileExists(linkPath + "/cached.ts"));
		if (linkPath != "/dir") {
			assert::Assert(t, fileSystem->FileExists(
			                      "/dir/removed-other/old.ts"));
		}
		if (remove) {
			assert::Assert(t, !fileSystem->FileExists(
			                      "/dir/removed/sibling.ts"));
		}
		assert::DeepEqual(t,
		                  fileSystem
		                      ->GetAccessibleEntries(linkPath +
		                                             "/removed")
		                      .files,
		                  std::vector<std::string>{"new.ts"});
	};
	verifyLinked(linked.get());
	RequestFileSystem emptyLayer{.kind = KindLayer};
	auto [next, err3] = newLayeredRequestFileSystem(
	    &emptyLayer,
	    std::shared_ptr<vfs::FS>(linked.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err3);
	verifyLinked(next.get());
	RequestFileSystem delParams{
	    .kind = KindLayer,
	    .RemovedPaths =
	        std::vector<std::string>{linkPath + "/new.ts"},
	};
	auto [deleted, err4] = newLayeredRequestFileSystem(
	    &delParams,
	    std::shared_ptr<vfs::FS>(next.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err4);
	assert::Assert(t, !deleted->FileExists(linkPath + "/new.ts"));
	assert::Assert(t,
	               deleted->FileExists(linkPath + "/removed/new.ts"));
	assert::Equal(
	    t,
	    int(deleted->GetAccessibleEntries(linkPath).files.size()), 0);
	verifyLinked(linked.get());
	assert::Assert(t, base->FileExists("/dir/removed/cached.ts"));
	verifyPrevious();
}

#define SYMLINK_TEST(name, ...)                                             \
	void name(T* t) { t->Parallel(); testSymlinkReplacesDirectory(t, __VA_ARGS__); } \
	REGISTER_UNIT_TEST("requestfilesystem." #name, name)

SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullRequestSealedSame,
             symlinkReplacementOptions{.kind = KindFull, .linkPath = "/dir/removed"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullRequestSealedParent,
             symlinkReplacementOptions{.kind = KindFull, .linkPath = "/dir"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullRequestSealedChild,
             symlinkReplacementOptions{.kind = KindFull, .linkPath = "/dir/removed/child"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullRequestRemovedSame,
             symlinkReplacementOptions{.kind = KindFull, .remove = true, .linkPath = "/dir/removed"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullRequestRemovedParent,
             symlinkReplacementOptions{.kind = KindFull, .remove = true, .linkPath = "/dir"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullRequestRemovedChild,
             symlinkReplacementOptions{.kind = KindFull, .remove = true, .linkPath = "/dir/removed/child"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullHostSealedSame,
             symlinkReplacementOptions{.kind = KindFull, .hostTarget = true, .linkPath = "/dir/removed"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullHostSealedParent,
             symlinkReplacementOptions{.kind = KindFull, .hostTarget = true, .linkPath = "/dir"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullHostSealedChild,
             symlinkReplacementOptions{.kind = KindFull, .hostTarget = true, .linkPath = "/dir/removed/child"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullHostRemovedSame,
             symlinkReplacementOptions{.kind = KindFull, .hostTarget = true, .remove = true, .linkPath = "/dir/removed"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullHostRemovedParent,
             symlinkReplacementOptions{.kind = KindFull, .hostTarget = true, .remove = true, .linkPath = "/dir"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryFullHostRemovedChild,
             symlinkReplacementOptions{.kind = KindFull, .hostTarget = true, .remove = true, .linkPath = "/dir/removed/child"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerFallbackSealedSame,
             symlinkReplacementOptions{.kind = KindLayer, .linkPath = "/dir/removed"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerFallbackSealedParent,
             symlinkReplacementOptions{.kind = KindLayer, .linkPath = "/dir"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerFallbackSealedChild,
             symlinkReplacementOptions{.kind = KindLayer, .linkPath = "/dir/removed/child"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerFallbackRemovedSame,
             symlinkReplacementOptions{.kind = KindLayer, .remove = true, .linkPath = "/dir/removed"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerFallbackRemovedParent,
             symlinkReplacementOptions{.kind = KindLayer, .remove = true, .linkPath = "/dir"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerFallbackRemovedChild,
             symlinkReplacementOptions{.kind = KindLayer, .remove = true, .linkPath = "/dir/removed/child"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerHostInsensitiveSealedSame,
             symlinkReplacementOptions{.kind = KindLayer, .hostTarget = true, .caseSensitive = false, .linkPath = "/dir/removed"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerHostInsensitiveSealedParent,
             symlinkReplacementOptions{.kind = KindLayer, .hostTarget = true, .caseSensitive = false, .linkPath = "/dir"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerHostInsensitiveSealedChild,
             symlinkReplacementOptions{.kind = KindLayer, .hostTarget = true, .caseSensitive = false, .linkPath = "/dir/removed/child"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerHostInsensitiveRemovedSame,
             symlinkReplacementOptions{.kind = KindLayer, .hostTarget = true, .caseSensitive = false, .remove = true, .linkPath = "/dir/removed"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerHostInsensitiveRemovedParent,
             symlinkReplacementOptions{.kind = KindLayer, .hostTarget = true, .caseSensitive = false, .remove = true, .linkPath = "/dir"});
SYMLINK_TEST(TestRequestFileSystemSymlinkReplacesDirectoryLayerHostInsensitiveRemovedChild,
             symlinkReplacementOptions{.kind = KindLayer, .hostTarget = true, .caseSensitive = false, .remove = true, .linkPath = "/dir/removed/child"});

void testObjectOverridesTombstone(T* t, const std::string& object,
                                  bool inheritedLink,
                                  const std::string& path) {
	t->Helper();
	auto host = mkTrackingHost({
	    {"/dir/removed/old.ts", "old"},
	    {"/dir/removed/child.ts", "old child"},
	    {"/old/removed/old.ts", "old target"},
	    {"/target/file.ts", "target"},
	});
	auto baseParams = std::make_unique<RequestFileSystem>();
	baseParams->kind = KindLayer;
	if (inheritedLink) {
		baseParams->Symlinks = std::map<std::string, RequestSymlink>{
		    {"/dir", {.Target = "/old"}}};
	}
	auto [base, err] =
	    newRequestFileSystem(baseParams.get(),
	                         std::static_pointer_cast<vfs::FS>(host),
	                         "/");
	assert::NilError(t, err);
	RequestFileSystem rm{
	    .kind = KindLayer,
	    .RemovedPaths = std::vector<std::string>{"/dir/removed"},
	};
	auto [removed, err2] = newLayeredRequestFileSystem(
	    &rm, std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err2);
	auto params = std::make_unique<RequestFileSystem>();
	params->kind = KindLayer;
	if (object == "file") {
		params->Files = {{path, "new"}};
	} else if (object == "directory") {
		params->Directories =
		    std::map<std::string, RequestDirectoryEntries>{{path, {}}};
	} else if (object == "symlink") {
		params->Symlinks = std::map<std::string, RequestSymlink>{
		    {path, {.Target = "/target"}}};
	} else if (object == "file-symlink") {
		params->Symlinks = std::map<std::string, RequestSymlink>{
		    {path, {.Target = "/target/file.ts"}}};
	}
	auto [replaced, err3] = newLayeredRequestFileSystem(
	    params.get(),
	    std::shared_ptr<vfs::FS>(removed.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err3);
	bool isFile = object == "file" || object == "file-symlink";
	assert::Equal(t, replaced->FileExists(path), isFile);
	assert::Equal(t, replaced->DirectoryExists(path), !isFile);
	assert::Assert(t, replaced->Stat(path) != nullptr);
	auto entries = replaced->GetAccessibleEntries(
	    tspath::getDirectoryPath(path));
	std::string entryName =
	    std::string(tspath::getBaseFileName(path));
	assert::Equal(t, sliceContains(entries.files, entryName), isFile);
	assert::Equal(t, sliceContains(entries.directories, entryName),
	              !isFile);
	assert::Equal(t, hasSymlinkEntry(entries, entryName),
	              object == "symlink" || object == "file-symlink");
	assert::Assert(t, !replaced->FileExists("/dir/removed/old.ts"));
	auto [c, ok] = replaced->ReadFile("/dir/removed/old.ts");
	assert::Assert(t, !ok);
	assert::Assert(t, replaced->Stat("/dir/removed/old.ts") == nullptr);
	if (isFile) {
		assert::Equal(t,
		              int(replaced->GetAccessibleEntries(path +
		                                                 "/removed")
		                      .files.size()),
		              0);
		assert::Equal(t,
		              int(replaced->GetAccessibleEntries(path)
		                      .directories.size()),
		              0);
	}
	assert::Assert(t, !removed->DirectoryExists("/dir/removed"));
	verifyCompactionWithoutHostReads(t, replaced.get(), host.get(),
	                               {path, path + "/file.ts", "/dir",
	                                "/dir/removed",
	                                "/dir/removed/old.ts",
	                                "/target/file.ts"});
}

#define TOMBSTONE_TEST(name, object, inheritedLink, path)                   \
	void name(T* t) {                                                       \
		t->Parallel();                                                      \
		testObjectOverridesTombstone(t, object, inheritedLink, path);       \
	}                                                                       \
	REGISTER_UNIT_TEST("requestfilesystem." #name, name)

TOMBSTONE_TEST(TestRequestFileSystemFileOverridesParentTombstone,
               "file", false, "/dir");
TOMBSTONE_TEST(TestRequestFileSystemFileOverridesSameTombstone,
               "file", false, "/dir/removed");
TOMBSTONE_TEST(TestRequestFileSystemFileOverridesChildTombstone,
               "file", false, "/dir/removed/child");
TOMBSTONE_TEST(TestRequestFileSystemFileOverridesParentTombstoneInheritedLink,
               "file", true, "/dir");
TOMBSTONE_TEST(TestRequestFileSystemFileOverridesSameTombstoneInheritedLink,
               "file", true, "/dir/removed");
TOMBSTONE_TEST(TestRequestFileSystemFileOverridesChildTombstoneInheritedLink,
               "file", true, "/dir/removed/child");
TOMBSTONE_TEST(TestRequestFileSystemDirectoryOverridesParentTombstone,
               "directory", false, "/dir");
TOMBSTONE_TEST(TestRequestFileSystemDirectoryOverridesSameTombstone,
               "directory", false, "/dir/removed");
TOMBSTONE_TEST(TestRequestFileSystemDirectoryOverridesChildTombstone,
               "directory", false, "/dir/removed/child");
TOMBSTONE_TEST(TestRequestFileSystemDirectoryOverridesParentTombstoneInheritedLink,
               "directory", true, "/dir");
TOMBSTONE_TEST(TestRequestFileSystemDirectoryOverridesSameTombstoneInheritedLink,
               "directory", true, "/dir/removed");
TOMBSTONE_TEST(TestRequestFileSystemDirectoryOverridesChildTombstoneInheritedLink,
               "directory", true, "/dir/removed/child");
TOMBSTONE_TEST(TestRequestFileSystemSymlinkOverridesParentTombstone,
               "symlink", false, "/dir");
TOMBSTONE_TEST(TestRequestFileSystemSymlinkOverridesSameTombstone,
               "symlink", false, "/dir/removed");
TOMBSTONE_TEST(TestRequestFileSystemSymlinkOverridesChildTombstone,
               "symlink", false, "/dir/removed/child");
TOMBSTONE_TEST(TestRequestFileSystemSymlinkOverridesParentTombstoneInheritedLink,
               "symlink", true, "/dir");
TOMBSTONE_TEST(TestRequestFileSystemSymlinkOverridesSameTombstoneInheritedLink,
               "symlink", true, "/dir/removed");
TOMBSTONE_TEST(TestRequestFileSystemSymlinkOverridesChildTombstoneInheritedLink,
               "symlink", true, "/dir/removed/child");
TOMBSTONE_TEST(TestRequestFileSystemFileSymlinkOverridesParentTombstone,
               "file-symlink", false, "/dir");
TOMBSTONE_TEST(TestRequestFileSystemFileSymlinkOverridesSameTombstone,
               "file-symlink", false, "/dir/removed");
TOMBSTONE_TEST(TestRequestFileSystemFileSymlinkOverridesChildTombstone,
               "file-symlink", false, "/dir/removed/child");
TOMBSTONE_TEST(TestRequestFileSystemFileSymlinkOverridesParentTombstoneInheritedLink,
               "file-symlink", true, "/dir");
TOMBSTONE_TEST(TestRequestFileSystemFileSymlinkOverridesSameTombstoneInheritedLink,
               "file-symlink", true, "/dir/removed");
TOMBSTONE_TEST(TestRequestFileSystemFileSymlinkOverridesChildTombstoneInheritedLink,
               "file-symlink", true, "/dir/removed/child");

void testReplacementPreservesCurrentRemoval(T* t, Kind kind,
                                            const std::string& removedPath) {
	t->Helper();
	std::map<std::string, std::string> files{
	    {"/dir/old.ts", "old host"},
	    {"/target/keep.ts", "keep"},
	    {"/target/blocked/gone.ts", "gone"},
	};
	std::unordered_map<std::string, vfs::vfstest::MapFileInput> hostFiles;
	for (auto& [k, v] : files) {
		hostFiles[k] = v;
	}
	auto host = mkHost(hostFiles);
	auto params = std::make_unique<RequestFileSystem>();
	params->kind = kind;
	if (kind == KindFull) {
		for (auto& [k, v] : files) {
			params->Files[k] = v;
		}
	}
	auto [base, err] = newRequestFileSystem(params.get(), host, "/");
	assert::NilError(t, err);
	RequestFileSystem rm{
	    .kind = KindLayer,
	    .RemovedPaths =
	        std::vector<std::string>{"/dir", "/dir/blocked"},
	};
	auto [removed, err2] = newLayeredRequestFileSystem(
	    &rm, std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err2);
	RequestFileSystem linkParams{
	    .kind = KindLayer,
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/dir", {.Target = "/target", .Host = kind == KindLayer}}},
	    .RemovedPaths = std::vector<std::string>{removedPath},
	};
	auto [linked, err3] = newLayeredRequestFileSystem(
	    &linkParams,
	    std::shared_ptr<vfs::FS>(removed.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err3);
	assert::Assert(t, linked->FileExists("/dir/keep.ts"));
	assert::Assert(t, !linked->FileExists("/dir/old.ts"));
	assert::Assert(t, !linked->FileExists("/dir/blocked/gone.ts"));
	auto [c, ok] = linked->ReadFile("/dir/blocked/gone.ts");
	assert::Assert(t, !ok);
	assert::Assert(t, linked->Stat("/dir/blocked/gone.ts") == nullptr);
	assert::Equal(
	    t,
	    int(linked->GetAccessibleEntries("/dir/blocked").files.size()),
	    0);
	RequestFileSystem delParams{
	    .kind = KindLayer,
	    .RemovedPaths = std::vector<std::string>{"/dir"},
	};
	auto [deleted, err4] = newLayeredRequestFileSystem(
	    &delParams,
	    std::shared_ptr<vfs::FS>(linked.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err4);
	assert::Assert(t, !deleted->DirectoryExists("/dir"));
	assert::Assert(t, !deleted->FileExists("/dir/keep.ts"));
	assert::Equal(
	    t, int(deleted->GetAccessibleEntries("/dir").files.size()), 0);
	assert::Assert(t, deleted->FileExists("/target/keep.ts"));
	assert::Assert(t, linked->FileExists("/dir/keep.ts"));
}

#define REMOVAL_TEST(name, kind, removedPath)                               \
	void name(T* t) {                                                       \
		t->Parallel();                                                      \
		testReplacementPreservesCurrentRemoval(t, kind, removedPath);       \
	}                                                                       \
	REGISTER_UNIT_TEST("requestfilesystem." #name, name)

REMOVAL_TEST(TestRequestFileSystemReplacementPreservesCurrentFullAliasDirectoryRemoval,
             KindFull, "/dir/blocked");
REMOVAL_TEST(TestRequestFileSystemReplacementPreservesCurrentFullAliasFileRemoval,
             KindFull, "/dir/blocked/gone.ts");
REMOVAL_TEST(TestRequestFileSystemReplacementPreservesCurrentFullTargetDirectoryRemoval,
             KindFull, "/target/blocked");
REMOVAL_TEST(TestRequestFileSystemReplacementPreservesCurrentFullTargetFileRemoval,
             KindFull, "/target/blocked/gone.ts");
REMOVAL_TEST(TestRequestFileSystemReplacementPreservesCurrentLayerAliasDirectoryRemoval,
             KindLayer, "/dir/blocked");
REMOVAL_TEST(TestRequestFileSystemReplacementPreservesCurrentLayerAliasFileRemoval,
             KindLayer, "/dir/blocked/gone.ts");
REMOVAL_TEST(TestRequestFileSystemReplacementPreservesCurrentLayerTargetDirectoryRemoval,
             KindLayer, "/target/blocked");
REMOVAL_TEST(TestRequestFileSystemReplacementPreservesCurrentLayerTargetFileRemoval,
             KindLayer, "/target/blocked/gone.ts");

void testSameLayerRemoval(T* t, bool hostTarget,
                          const std::string& removedPath,
                          const std::string& form) {
	t->Helper();
	auto host = mkTrackingHost({{"/target/file.ts", "host"}});
	RequestFileSystem baseParams{.kind = KindFull};
	auto [base, err] =
	    newRequestFileSystem(&baseParams,
	                         std::static_pointer_cast<vfs::FS>(host),
	                         "/");
	assert::NilError(t, err);
	RequestFileSystem params{
	    .kind = KindLayer,
	    .Files = {{"/target/file.ts", "request"}},
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/links/pkg", {.Target = "/target", .Host = hostTarget}}},
	    .RemovedPaths = std::vector<std::string>{removedPath},
	};
	gostd::Error err2;
	std::shared_ptr<requestFileSystem> fileSystem;
	if (form == "standalone") {
		auto r = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(host), "/");
		fileSystem = r.first;
		err2 = r.second;
	} else if (form == "layered") {
		auto r = newRequestFileSystem(
		    &params,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		fileSystem = r.first;
		err2 = r.second;
	} else {
		auto r = newLayeredRequestFileSystem(
		    &params,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		fileSystem = r.first;
		err2 = r.second;
		if (err2 == nullptr && form == "recompacted") {
			RequestFileSystem emptyLayer{.kind = KindLayer};
			auto r2 = newLayeredRequestFileSystem(
			    &emptyLayer,
			    std::shared_ptr<vfs::FS>(fileSystem.get(),
			                             [](vfs::FS*) {}),
			    "/");
			fileSystem = r2.first;
			err2 = r2.second;
		} else if (err2 == nullptr && form == "compacted-input") {
			auto r2 = newRequestFileSystem(
			    &params,
			    std::shared_ptr<vfs::FS>(fileSystem.get(),
			                             [](vfs::FS*) {}),
			    "/");
			fileSystem = r2.first;
			err2 = r2.second;
		}
	}
	assert::NilError(t, err2);
	assert::Assert(t, !fileSystem->FileExists("/links/pkg/file.ts"));
	auto [c, ok] = fileSystem->ReadFile("/links/pkg/file.ts");
	assert::Assert(t, !ok);
	assert::Assert(t, fileSystem->Stat("/links/pkg/file.ts") == nullptr);
	assert::Equal(t, fileSystem->Realpath("/links/pkg/file.ts"),
	              std::string("/links/pkg/file.ts"));
	assert::Equal(
	    t,
	    int(fileSystem->GetAccessibleEntries("/links/pkg").files.size()),
	    0);
	bool linkExists = removedPath == "/links/pkg/file.ts";
	assert::Equal(t, fileSystem->DirectoryExists("/links/pkg"),
	              linkExists);
	auto entries = fileSystem->GetAccessibleEntries("/links");
	assert::Equal(t, sliceContains(entries.directories, "pkg"),
	              linkExists);
	assert::Equal(t, hasSymlinkEntry(entries, "pkg"), linkExists);
	assert::Assert(t, fileSystem->FileExists("/target/file.ts"));
	verifyCompactionWithoutHostReads(
	    t, fileSystem.get(), host.get(),
	    {"/", "/links", "/links/pkg", "/links/pkg/file.ts", "/target",
	     "/target/file.ts", "/missing"});
}

#define SLELAYER_TEST(name, hostTarget, removedPath, form)                  \
	void name(T* t) {                                                       \
		t->Parallel();                                                      \
		testSameLayerRemoval(t, hostTarget, removedPath, form);             \
	}                                                                       \
	REGISTER_UNIT_TEST("requestfilesystem." #name, name)

SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestAncestorStandalone, false, "/links", "standalone");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestAncestorLayered, false, "/links", "layered");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestAncestorCompacted, false, "/links", "compacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestAncestorRecompacted, false, "/links", "recompacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestAncestorCompactedInput, false, "/links", "compacted-input");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestLinkStandalone, false, "/links/pkg", "standalone");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestLinkLayered, false, "/links/pkg", "layered");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestLinkCompacted, false, "/links/pkg", "compacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestLinkRecompacted, false, "/links/pkg", "recompacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestLinkCompactedInput, false, "/links/pkg", "compacted-input");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestDescendantStandalone, false, "/links/pkg/file.ts", "standalone");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestDescendantLayered, false, "/links/pkg/file.ts", "layered");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestDescendantCompacted, false, "/links/pkg/file.ts", "compacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestDescendantRecompacted, false, "/links/pkg/file.ts", "recompacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalRequestDescendantCompactedInput, false, "/links/pkg/file.ts", "compacted-input");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostAncestorStandalone, true, "/links", "standalone");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostAncestorLayered, true, "/links", "layered");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostAncestorCompacted, true, "/links", "compacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostAncestorRecompacted, true, "/links", "recompacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostAncestorCompactedInput, true, "/links", "compacted-input");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostLinkStandalone, true, "/links/pkg", "standalone");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostLinkLayered, true, "/links/pkg", "layered");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostLinkCompacted, true, "/links/pkg", "compacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostLinkRecompacted, true, "/links/pkg", "recompacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostLinkCompactedInput, true, "/links/pkg", "compacted-input");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostDescendantStandalone, true, "/links/pkg/file.ts", "standalone");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostDescendantLayered, true, "/links/pkg/file.ts", "layered");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostDescendantCompacted, true, "/links/pkg/file.ts", "compacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostDescendantRecompacted, true, "/links/pkg/file.ts", "recompacted");
SLELAYER_TEST(TestRequestFileSystemSameLayerRemovalHostDescendantCompactedInput, true, "/links/pkg/file.ts", "compacted-input");

void testRemovalExceptions(T* t, bool hostTarget, bool removeAgain) {
	t->Helper();
	auto host = mkHost({
	    {"/dir/old.ts", "old"},
	    {"/target/a.ts", "a"},
	    {"/target/b.ts", "b"},
	    {"/target/sub/c.ts", "c"},
	});
	RequestFileSystem baseParams{.kind = KindLayer};
	auto [base, err] =
	    newRequestFileSystem(&baseParams, host, "/");
	assert::NilError(t, err);
	RequestFileSystem rm{
	    .kind = KindLayer,
	    .RemovedPaths = std::vector<std::string>{"/dir"},
	};
	auto [removed, err2] = newLayeredRequestFileSystem(
	    &rm, std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err2);
	RequestFileSystem params{
	    .kind = KindLayer,
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/dir/pkg", {.Target = "/target", .Host = hostTarget}}},
	    .RemovedPaths = std::vector<std::string>{"/dir/pkg/b.ts"},
	};
	auto [layered, err3] = newRequestFileSystem(
	    &params,
	    std::shared_ptr<vfs::FS>(removed.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err3);
	RequestFileSystem emptyLayer{.kind = KindLayer};
	auto [compacted, err4] = newRequestFileSystem(
	    &emptyLayer,
	    std::shared_ptr<vfs::FS>(layered.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err4);
	auto [input, err5] = newRequestFileSystem(
	    &emptyLayer,
	    std::shared_ptr<vfs::FS>(compacted.get(), [](vfs::FS*) {}), "/");
	assert::NilError(t, err5);
	auto verify = [&](std::shared_ptr<requestFileSystem> fileSystem) {
		t->Helper();
		if (removeAgain) {
			RequestFileSystem rm2{
			    .kind = KindLayer,
			    .RemovedPaths =
			        std::vector<std::string>{"/dir"},
			};
			auto r = newLayeredRequestFileSystem(
			    &rm2,
			    std::shared_ptr<vfs::FS>(fileSystem.get(),
			                             [](vfs::FS*) {}),
			    "/");
			assert::NilError(t, r.second);
			fileSystem = r.first;
		}
		assert::Equal(t, fileSystem->FileExists("/dir/pkg/a.ts"),
		              !removeAgain);
		assert::Assert(t, !fileSystem->FileExists("/dir/pkg/b.ts"));
		assert::Assert(t, !fileSystem->FileExists("/dir/old.ts"));
		auto entries = fileSystem->GetAccessibleEntries("/dir/pkg");
		if (removeAgain) {
			assert::Equal(t, int(entries.files.size()), 0);
			assert::Equal(t, int(entries.directories.size()), 0);
		} else {
			assert::DeepEqual(t, entries.files,
			                  std::vector<std::string>{"a.ts"});
			assert::DeepEqual(t, entries.directories,
			                  std::vector<std::string>{"sub"});
		}
	};
	verify(layered);
	verify(compacted);
	verify(input);
	assert::Assert(t, !removed->DirectoryExists("/dir/pkg"));
	assert::Assert(t, compacted->FileExists("/dir/pkg/a.ts"));
}

void TestRequestFileSystemRemovalExceptionsRequest(T* t) {
	t->Parallel();
	testRemovalExceptions(t, false, false);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemRemovalExceptionsRequest",
                   TestRequestFileSystemRemovalExceptionsRequest);

void TestRequestFileSystemRemovalExceptionsRequestRemovedAgain(T* t) {
	t->Parallel();
	testRemovalExceptions(t, false, true);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemRemovalExceptionsRequestRemovedAgain",
                   TestRequestFileSystemRemovalExceptionsRequestRemovedAgain);

void TestRequestFileSystemRemovalExceptionsHost(T* t) {
	t->Parallel();
	testRemovalExceptions(t, true, false);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemRemovalExceptionsHost",
                   TestRequestFileSystemRemovalExceptionsHost);

void TestRequestFileSystemRemovalExceptionsHostRemovedAgain(T* t) {
	t->Parallel();
	testRemovalExceptions(t, true, true);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystemRemovalExceptionsHostRemovedAgain",
                   TestRequestFileSystemRemovalExceptionsHostRemovedAgain);

// TestRequestFileSystem — requestfilesystem_test.go:1108.
void TestRequestFileSystem(T* t) {
	t->Parallel();

	t->Run("compaction preserves host fallback", [](T* t) {
		t->Parallel();
		auto host = mkHost({{"/host.ts", "host"}});
		RequestFileSystem baseParams{.kind = KindLayer};
		auto [baseFS, err] = newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);
		auto* base = baseFS.get();
		assert::Assert(t, base != nullptr);
		assert::Assert(t, base->baseFileSystem() == host.get());
		assert::Assert(t, !base->FileExists("/created-after-base.ts"));
		assert::Assert(t, host->WriteFile("/created-after-base.ts", "created").impl() == nullptr);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {{"/layered.ts", "layered"}},
		};
		auto [layeredFS, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(base, [](vfs::FS*) {}), "/");
		assert::NilError(t, err2);
		auto* layered = layeredFS.get();
		assert::Assert(t, layered != nullptr);
		assert::Assert(t, layered->baseFileSystem() == host.get());
		assert::Assert(t, layered->FileExists("/created-after-base.ts"));
		assert::Assert(t, host->Remove("/created-after-base.ts").impl() == nullptr);

		assert::Assert(t, layered->baseFileSystem() == host.get());
		assert::Assert(t, !layered->FileExists("/created-after-base.ts"));
		auto [contents, ok] = layered->ReadFile("/host.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("host"));
	});

	t->Run("memory is total and never falls back", [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost({{"/host.ts", "host"}});
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {{"/src/index.ts", "memory"}},
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base), "/");
		assert::NilError(t, err);

		auto [contents, ok] = fileSystem->ReadFile("/src/index.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("memory"));
		assert::Assert(t, fileSystem->FileExists("/src/index.ts"));
		assert::Assert(t, fileSystem->DirectoryExists("/src"));
		assert::DeepEqual(t,
		                  fileSystem->GetAccessibleEntries("/src").files,
		                  std::vector<std::string>{"index.ts"});

		auto [c2, ok2] = fileSystem->ReadFile("/host.ts");
		assert::Assert(t, !ok2);
		assert::Assert(t, !fileSystem->FileExists("/host.ts"));
		assert::Assert(t, !base->SeenFiles.Has("/host.ts"));
	});

	t->Run("cache hits bypass the host and misses fall back", [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost({{"/fallback.ts", "fallback"}});
		RequestFileSystem params{
		    .kind = KindLayer,
		    .Files = {{"/cached/index.ts", "cached"}},
		    .Directories = std::map<std::string, RequestDirectoryEntries>{
		        {"/cached",
		         {.Files = {"index.ts"}, .Directories = {}}}},
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base), "/");
		assert::NilError(t, err);
		clearSeen(base.get());

		auto [contents, ok] = fileSystem->ReadFile("/cached/index.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("cached"));
		assert::Assert(t, fileSystem->FileExists("/cached/index.ts"));
		assert::Assert(t, fileSystem->DirectoryExists("/cached"));
		assert::DeepEqual(
		    t, fileSystem->GetAccessibleEntries("/cached").files,
		    std::vector<std::string>{"index.ts"});
		assert::Assert(t, !base->SeenFiles.Has("/cached/index.ts"));
		assert::Assert(t, !base->SeenFiles.Has("/cached"));

		auto [contents2, ok2] = fileSystem->ReadFile("/fallback.ts");
		assert::Assert(t, ok2);
		assert::Equal(t, contents2, std::string("fallback"));
		assert::Assert(t, base->SeenFiles.Has("/fallback.ts"));
	});

	t->Run("layered memory is a total replacement", [](T* t) {
		t->Parallel();
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {{"/memory.ts", "memory"}},
		};
		auto [fileSystem, err] = newLayeredRequestFileSystem(
		    &params, mkHost({{"/host.ts", "host"}}), "/");
		assert::NilError(t, err);
		auto [contents, ok] = fileSystem->ReadFile("/memory.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("memory"));
		auto [c2, ok2] = fileSystem->ReadFile("/host.ts");
		assert::Assert(t, !ok2);
	});

	t->Run("memory resolves internal file and directory symlinks",
	       [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost({{"/host.ts", "host"}});
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {
		        {"/packages/pkg/index.d.ts",
		         "export declare const value: number;"},
		    },
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/project/node_modules/pkg",
		         {.Target = "../../../packages/pkg"}},
		        {"/project/pkg.d.ts",
		         {.Target = "../packages/pkg/index.d.ts"}},
		    },
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base), "/");
		assert::NilError(t, err);

		auto [contents, ok] = fileSystem->ReadFile(
		    "/project/node_modules/pkg/index.d.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents,
		              std::string("export declare const value: number;"));
		auto [contents2, ok2] =
		    fileSystem->ReadFile("/project/pkg.d.ts");
		assert::Assert(t, ok2);
		assert::Equal(t, contents2,
		              std::string("export declare const value: number;"));
		assert::Equal(t,
		              fileSystem->Realpath(
		                  "/project/node_modules/pkg/index.d.ts"),
		              std::string("/packages/pkg/index.d.ts"));

		auto entries = fileSystem->GetAccessibleEntries(
		    "/project/node_modules");
		assert::DeepEqual(t, entries.directories,
		                  std::vector<std::string>{"pkg"});
		assert::Assert(t, hasSymlinkEntry(entries, "pkg"));
		entries = fileSystem->GetAccessibleEntries("/project");
		assert::DeepEqual(t, entries.files,
		                  std::vector<std::string>{"pkg.d.ts"});
		assert::Assert(t, hasSymlinkEntry(entries, "pkg.d.ts"));
		assert::Assert(t, base->SeenFiles.IsEmpty());
	});

	t->Run("cache resolves internal symlinks before the host", [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost(
		    {{"/packages/pkg/index.d.ts", "host content"}});
		RequestFileSystem params{
		    .kind = KindLayer,
		    .Files = {
		        {"/packages/pkg/index.d.ts", "cached content"},
		    },
		    .Directories = std::map<std::string, RequestDirectoryEntries>{
		        {"/project/node_modules",
		         {.Files = {}, .Directories = {}}}},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/project/node_modules/pkg",
		         {.Target = "/packages/pkg"}},
		    },
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base), "/");
		assert::NilError(t, err);
		clearSeen(base.get());

		auto [contents, ok] = fileSystem->ReadFile(
		    "/project/node_modules/pkg/index.d.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("cached content"));
		assert::Equal(t,
		              fileSystem->Realpath(
		                  "/project/node_modules/pkg/index.d.ts"),
		              std::string("/packages/pkg/index.d.ts"));
		auto entries = fileSystem->GetAccessibleEntries(
		    "/project/node_modules");
		assert::DeepEqual(t, entries.directories,
		                  std::vector<std::string>{"pkg"});
		assert::Assert(t, hasSymlinkEntry(entries, "pkg"));
		assert::Assert(t, base->SeenFiles.IsEmpty());
	});

	t->Run("cache file shadows underlying symlink realpath", [](T* t) {
		t->Parallel();
		auto base = mkHost(
		    {{"/project/node_modules/pkg",
		      vfs::vfstest::Symlink("/host/pkg")},
		     {"/host/pkg/index.d.ts", "host content"}});
		RequestFileSystem params{
		    .kind = KindLayer,
		    .Files = {
		        {"/project/node_modules/pkg/index.d.ts",
		         "cached content"},
		    },
		};
		auto [fileSystem, err] =
		    newRequestFileSystem(&params, base, "/");
		assert::NilError(t, err);

		auto [contents, ok] = fileSystem->ReadFile(
		    "/project/node_modules/pkg/index.d.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("cached content"));
		assert::Equal(
		    t,
		    fileSystem->Realpath(
		        "/project/node_modules/pkg/index.d.ts"),
		    std::string("/project/node_modules/pkg/index.d.ts"));
	});

	t->Run("layered cache adds changes and blocks removed entries",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost();
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {
		        {"/keep.ts", "keep"},
		        {"/change.ts", "old"},
		        {"/remove.ts", "remove"},
		        {"/removed-dir/gone.ts", "gone"},
		        {"/becomes-file/child.ts", "child"},
		        {"/becomes-directory.ts", "file"},
		    },
		};
		auto [base, err] = newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {
		        {"/change.ts", "new"},
		        {"/added.ts", "added"},
		        {"/remove.ts", "replacement"},
		        {"/removed-dir/replacement.ts", "replacement"},
		        {"/becomes-file", "file"},
		        {"/becomes-directory.ts/child.ts", "child"},
		    },
		    .Directories = std::map<std::string, RequestDirectoryEntries>{
		        {"/",
		         {.Files = {"added.ts", "becomes-file", "change.ts",
		                    "remove.ts"},
		          .Directories = {"becomes-directory.ts",
		                          "removed-dir"}}}},
		    .RemovedPaths = std::vector<std::string>{"/remove.ts",
		                                            "/removed-dir"},
		};
		auto [layered, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		assert::NilError(t, err2);

		for (const auto& [path, expected] :
		     std::map<std::string, std::string>{
		         {"/keep.ts", "keep"},
		         {"/change.ts", "new"},
		         {"/added.ts", "added"},
		         {"/remove.ts", "replacement"},
		         {"/removed-dir/replacement.ts", "replacement"},
		         {"/becomes-file", "file"},
		         {"/becomes-directory.ts/child.ts", "child"},
		     }) {
			auto [contents, ok] = layered->ReadFile(path);
			assert::Assert(t, ok, path);
			assert::Equal(t, contents, expected);
		}
		assert::Assert(t, layered->FileExists("/remove.ts"));
		assert::Assert(t, layered->DirectoryExists("/removed-dir"));
		assert::Assert(t, !layered->FileExists("/removed-dir/gone.ts"));
		assert::Assert(t, layered->Stat("/remove.ts") != nullptr);
		assert::Assert(
		    t, layered->Stat("/removed-dir/replacement.ts") != nullptr);
		assert::Equal(
		    t, layered->Realpath("/removed-dir/replacement.ts"),
		    std::string("/removed-dir/replacement.ts"));
		assert::Assert(t, layered->FileExists("/becomes-file"));
		assert::Assert(t, !layered->DirectoryExists("/becomes-file"));
		assert::Assert(
		    t, !layered->FileExists("/becomes-directory.ts"));
		assert::Assert(
		    t, layered->DirectoryExists("/becomes-directory.ts"));
		assert::DeepEqual(
		    t, layered->GetAccessibleEntries("/").files,
		    std::vector<std::string>(
		        {"added.ts", "becomes-file", "change.ts",
		         "remove.ts"}));
		assert::DeepEqual(
		    t, layered->GetAccessibleEntries("/").directories,
		    std::vector<std::string>(
		        {"becomes-directory.ts", "removed-dir"}));
	});

	t->Run("new layers override targets of inherited symlinks",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost();
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {
		        {"/target/change.ts", "old"},
		        {"/target/keep.ts", "keep"},
		        {"/target/remove.ts", "remove"},
		    },
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/link", {.Target = "/target"}},
		    },
		};
		auto [base, err] = newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {
		        {"/target/change.ts", "new"},
		        {"/target/added.ts", "added"},
		    },
		    .RemovedPaths =
		        std::vector<std::string>{"/target/remove.ts"},
		};
		auto [layered, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		assert::NilError(t, err2);

		auto [contents, ok] = layered->ReadFile("/link/change.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("new"));
		auto [contents2, ok2] = layered->ReadFile("/link/added.ts");
		assert::Assert(t, ok2);
		assert::Equal(t, contents2, std::string("added"));
		auto [c3, ok3] = layered->ReadFile("/link/remove.ts");
		assert::Assert(t, !ok3);
		assert::DeepEqual(
		    t, layered->GetAccessibleEntries("/link").files,
		    std::vector<std::string>(
		        {"added.ts", "change.ts", "keep.ts"}));
	});

	t->Run("alias tombstones take precedence over inherited symlink "
	       "targets",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost();
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {{"/target/file.ts", "old"}},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/link", {.Target = "/target"}},
		    },
		};
		auto [base, err] = newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {{"/target/file.ts", "new"}},
		    .RemovedPaths = std::vector<std::string>{"/link"},
		};
		auto [layered, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		assert::NilError(t, err2);

		auto [c, ok] = layered->ReadFile("/link/file.ts");
		assert::Assert(t, !ok);
		assert::Assert(t, !layered->FileExists("/link/file.ts"));
		assert::Assert(t, !layered->DirectoryExists("/link"));
		assert::Assert(t, layered->Stat("/link/file.ts") == nullptr);
		assert::Equal(
		    t, int(layered->GetAccessibleEntries("/link").files.size()),
		    0);
	});

	t->Run("alias tombstones take precedence over same-layer symlink "
	       "targets",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost({{"/host-target/file.ts", "host"}});
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {{"/target/file.ts", "memory"}},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/link", {.Target = "/target"}},
		        {"/host-link",
		         {.Target = "/host-target", .Host = true}},
		    },
		    .RemovedPaths = std::vector<std::string>{"/link/file.ts",
		                                            "/host-link/file.ts"},
		};
		auto [fileSystem, err] =
		    newRequestFileSystem(&params, host, "/");
		assert::NilError(t, err);

		for (const char* path : {"/link/file.ts", "/host-link/file.ts"}) {
			auto [c, ok] = fileSystem->ReadFile(path);
			assert::Assert(t, !ok, path);
			assert::Assert(t, !fileSystem->FileExists(path), path);
			assert::Assert(t, fileSystem->Stat(path) == nullptr,
			               path);
		}
		assert::Equal(
		    t,
		    int(fileSystem->GetAccessibleEntries("/link").files.size()),
		    0);
		assert::Equal(
		    t, int(fileSystem->GetAccessibleEntries("/host-link")
		               .files.size()),
		    0);
	});

	t->Run("compaction preserves overlays addressed through inherited "
	       "symlinks",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost();
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {{"/target/remove.ts", "remove"}},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/link", {.Target = "/target"}},
		    },
		};
		auto [baseFS, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {},
		    .RemovedPaths =
		        std::vector<std::string>{"/link/remove.ts"},
		};
		auto [layeredFS, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(baseFS.get(), [](vfs::FS*) {}),
		    "/");
		assert::NilError(t, err2);
		auto* layered = layeredFS.get();

		auto [c, ok] = layered->ReadFile("/link/remove.ts");
		assert::Assert(t, !ok);
	});

	t->Run("compaction removes tombstones from explicit listings",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost();
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {{"/dir/remove.ts", "remove"}},
		    .Directories = std::map<std::string, RequestDirectoryEntries>{
		        {"/dir",
		         {.Files = {"remove.ts"}, .Directories = {}}}},
		};
		auto [baseFS, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {},
		    .RemovedPaths =
		        std::vector<std::string>{"/dir/remove.ts"},
		};
		auto [layeredFS, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(baseFS.get(), [](vfs::FS*) {}),
		    "/");
		assert::NilError(t, err2);
		auto* layered = layeredFS.get();
		assert::Equal(
		    t, int(layered->GetAccessibleEntries("/dir").files.size()),
		    0);
	});

	t->Run("compaction allows recreating a path removed through an "
	       "inherited symlink",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost();
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {{"/target/recreated.ts", "base"}},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/link", {.Target = "/target"}},
		    },
		};
		auto [baseFS, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem rmParams{
		    .kind = KindLayer,
		    .RemovedPaths =
		        std::vector<std::string>{"/link/recreated.ts"},
		};
		auto [removedFS, err2] = newLayeredRequestFileSystem(
		    &rmParams,
		    std::shared_ptr<vfs::FS>(baseFS.get(), [](vfs::FS*) {}),
		    "/");
		assert::NilError(t, err2);

		RequestFileSystem recreateParams{
		    .kind = KindLayer,
		    .Files = {{"/link/recreated.ts", "recreated"}},
		};
		auto [recreatedFS, err3] = newLayeredRequestFileSystem(
		    &recreateParams,
		    std::shared_ptr<vfs::FS>(removedFS.get(),
		                             [](vfs::FS*) {}),
		    "/");
		assert::NilError(t, err3);
		auto* recreated = recreatedFS.get();
		auto [contents, ok] =
		    recreated->ReadFile("/link/recreated.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("recreated"));
	});

	t->Run("compaction allows recreating a descendant of a path removed "
	       "through an inherited symlink",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost();
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {{"/target/dir/existing.ts", "existing"}},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/link", {.Target = "/target"}},
		    },
		};
		auto [base, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem rmParams{
		    .kind = KindLayer,
		    .RemovedPaths = std::vector<std::string>{"/link/dir"},
		};
		auto [removed, err2] = newLayeredRequestFileSystem(
		    &rmParams,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		assert::NilError(t, err2);

		RequestFileSystem recreateParams{
		    .kind = KindLayer,
		    .Files = {{"/link/dir/recreated.ts", "recreated"}},
		};
		auto [recreated, err3] = newLayeredRequestFileSystem(
		    &recreateParams,
		    std::shared_ptr<vfs::FS>(removed.get(), [](vfs::FS*) {}),
		    "/");
		assert::NilError(t, err3);
		auto [contents, ok] =
		    recreated->ReadFile("/link/dir/recreated.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("recreated"));
		assert::Assert(
		    t, !recreated->FileExists("/link/dir/existing.ts"));
		assert::DeepEqual(
		    t, recreated->GetAccessibleEntries("/link/dir").files,
		    std::vector<std::string>{"recreated.ts"});
		assert::DeepEqual(
		    t, recreated->GetAccessibleEntries("/link").directories,
		    std::vector<std::string>{"dir"});
	});

	t->Run("files replacing inherited symlink target directories have "
	       "empty listings",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost();
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {{"/target/item/child.ts", "child"}},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/link", {.Target = "/target"}},
		    },
		};
		auto [base, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {{"/target/item", "file"}},
		};
		auto [layered, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		assert::NilError(t, err2);

		assert::Assert(t, layered->FileExists("/link/item"));
		assert::Assert(t, !layered->DirectoryExists("/link/item"));
		assert::Equal(
		    t,
		    int(layered->GetAccessibleEntries("/link/item").files.size()),
		    0);
		assert::Equal(
		    t, int(layered->GetAccessibleEntries("/link/item")
		               .directories.size()),
		    0);
	});

	t->Run("cache tombstones block host hits", [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost({
		    {"/remove.ts", "host"},
		    {"/removed-dir/gone.ts", "host"},
		});
		RequestFileSystem params{
		    .kind = KindLayer,
		    .Files = {},
		    .RemovedPaths = std::vector<std::string>{"/remove.ts",
		                                            "/removed-dir"},
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base), "/");
		assert::NilError(t, err);
		clearSeen(base.get());

		assert::Assert(t, !fileSystem->FileExists("/remove.ts"));
		assert::Assert(t, !fileSystem->DirectoryExists("/removed-dir"));
		assert::Assert(
		    t, !fileSystem->FileExists("/removed-dir/gone.ts"));
		assert::Assert(t, base->SeenFiles.IsEmpty());
	});

	t->Run("compacted filesystem layers retain host fallback", [](T* t) {
		t->Parallel();
		auto host = mkHost({
		    {"/host.ts", "host"},
		    {"/removed.ts", "host removed"},
		    {"/sealed/host.ts", "hidden from listing"},
		    {"/open/host.ts", "host listing"},
		    {"/open/layer-listed.ts", "host listed"},
		});
		RequestFileSystem baseParams{
		    .kind = KindLayer,
		    .Files = {
		        {"/inherited.ts", "inherited"},
		        {"/sealed/inherited.ts", "sealed inherited"},
		    },
		    .Directories = std::map<std::string, RequestDirectoryEntries>{
		        {"/sealed",
		         {.Files = {"inherited.ts"}, .Directories = {}}}},
		    .RemovedPaths = std::vector<std::string>{"/removed.ts"},
		};
		auto [baseFS, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {
		        {"/added.ts", "added"},
		        {"/sealed/added.ts", "sealed added"},
		    },
		    .Directories = std::map<std::string, RequestDirectoryEntries>{
		        {"/open",
		         {.Files = {"layer-listed.ts"}, .Directories = {}}}},
		};
		auto [layeredFS, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(baseFS.get(), [](vfs::FS*) {}),
		    "/");
		assert::NilError(t, err2);
		auto* layered = layeredFS.get();
		assert::Assert(t, layered->baseFileSystem() == host.get());
		assert::Equal(t, layered->kind, KindLayer);

		for (const auto& [path, expected] :
		     std::map<std::string, std::string>{
		         {"/host.ts", "host"},
		         {"/inherited.ts", "inherited"},
		         {"/added.ts", "added"},
		     }) {
			auto [contents, ok] = layered->ReadFile(path);
			assert::Assert(t, ok, path);
			assert::Equal(t, contents, expected);
		}
		auto [c, ok] = layered->ReadFile("/removed.ts");
		assert::Assert(t, !ok);
		assert::DeepEqual(
		    t, layered->GetAccessibleEntries("/sealed").files,
		    std::vector<std::string>({"added.ts", "inherited.ts"}));
		assert::DeepEqual(
		    t, layered->GetAccessibleEntries("/open").files,
		    std::vector<std::string>({"layer-listed.ts"}));
	});

	t->Run("compacting a filesystem layer over a full filesystem "
	       "produces a full filesystem",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost({{"/host.ts", "host"}});
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {{"/target/inherited.ts", "inherited"}},
		    .Directories = std::map<std::string, RequestDirectoryEntries>{
		        {"/target",
		         {.Files = {"inherited.ts"}, .Directories = {}}}},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/link", {.Target = "/target"}},
		    },
		};
		auto [baseFS, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {{"/target/added.ts", "added"}},
		};
		auto [layeredFS, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(baseFS.get(), [](vfs::FS*) {}),
		    "/");
		assert::NilError(t, err2);
		auto* layered = layeredFS.get();
		assert::Equal(t, layered->kind, KindFull);
		assert::Assert(t, layered->baseFileSystem() == host.get());

		for (const auto& [path, expected] :
		     std::map<std::string, std::string>{
		         {"/link/inherited.ts", "inherited"},
		         {"/link/added.ts", "added"},
		     }) {
			auto [contents, ok] = layered->ReadFile(path);
			assert::Assert(t, ok, path);
			assert::Equal(t, contents, expected);
		}
		auto [c, ok] = layered->ReadFile("/host.ts");
		assert::Assert(t, !ok);
	});

	t->Run("memory routes explicit host symlinks to the host only "
	       "through the link",
	       [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost({
		    {"/host/node_modules/pkg/index.d.ts",
		     "export declare const hostValue: string;"},
		    {"/host/outside.ts", "outside"},
		});
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {
		        {"/project/index.ts",
		         "import { hostValue } from \"pkg\";"},
		    },
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/project/node_modules",
		         {.Target = "/host/node_modules", .Host = true}},
		    },
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base), "/");
		assert::NilError(t, err);

		auto [c, ok] = fileSystem->ReadFile("/host/outside.ts");
		assert::Assert(t, !ok);
		assert::Assert(t, !base->SeenFiles.Has("/host/outside.ts"));

		auto [contents, ok2] = fileSystem->ReadFile(
		    "/project/node_modules/pkg/index.d.ts");
		assert::Assert(t, ok2);
		assert::Equal(
		    t, contents,
		    std::string("export declare const hostValue: string;"));
		assert::Assert(
		    t,
		    base->SeenFiles.Has("/host/node_modules/pkg/index.d.ts"));
		assert::Equal(t,
		              fileSystem->Realpath(
		                  "/project/node_modules/pkg/index.d.ts"),
		              std::string("/host/node_modules/pkg/index.d.ts"));

		auto entries = fileSystem->GetAccessibleEntries("/project");
		assert::DeepEqual(t, entries.directories,
		                  std::vector<std::string>{"node_modules"});
		assert::Assert(t, hasSymlinkEntry(entries, "node_modules"));
	});

	t->Run("layered host symlinks bypass snapshot bases", [](T* t) {
		t->Parallel();
		auto host = mkTrackingHost({{"/host/pkg/index.d.ts", "host"}});
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {{"/memory.ts", "memory"}},
		};
		auto [base, err] = newRequestFileSystem(
		    &baseParams, std::static_pointer_cast<vfs::FS>(host), "/");
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/project/pkg",
		         {.Target = "/host/pkg", .Host = true}},
		    },
		};
		auto [layered, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		assert::NilError(t, err2);

		auto [contents, ok] =
		    layered->ReadFile("/project/pkg/index.d.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("host"));
		assert::Assert(t, layered->FileExists("/project/pkg/index.d.ts"));
		assert::Assert(t, layered->DirectoryExists("/project/pkg"));
		assert::DeepEqual(
		    t, layered->GetAccessibleEntries("/project/pkg").files,
		    std::vector<std::string>{"index.d.ts"});
		assert::Equal(
		    t, layered->Realpath("/project/pkg/index.d.ts"),
		    std::string("/host/pkg/index.d.ts"));
		auto info = layered->Stat("/project/pkg/index.d.ts");
		assert::Assert(t, info != nullptr);
		assert::Equal(t, info->Name(), std::string("index.d.ts"));
		assert::Assert(
		    t, host->SeenFiles.Has("/host/pkg/index.d.ts"));
	});

	t->Run("inherited host symlinks bypass newer cache entries at the "
	       "target",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost({
		    {"/host/pkg/host.ts", "host"},
		    {"/host/pkg/removed.ts", "removed"},
		});
		RequestFileSystem baseParams{
		    .kind = KindFull,
		    .Files = {},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/link", {.Target = "/host/pkg", .Host = true}},
		    },
		};
		auto [base, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);

		RequestFileSystem layerParams{
		    .kind = KindLayer,
		    .Files = {
		        {"/host/pkg/host.ts", "cache"},
		        {"/host/pkg/cache-only.ts", "cache only"},
		    },
		    .RemovedPaths =
		        std::vector<std::string>{"/link/removed.ts"},
		};
		auto [layered, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		assert::NilError(t, err2);

		auto [contents, ok] = layered->ReadFile("/link/host.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("host"));
		assert::Assert(t, !layered->FileExists("/link/cache-only.ts"));
		assert::Assert(t, !layered->FileExists("/link/removed.ts"));
		assert::Equal(
		    t, layered->Stat("/link/host.ts")->Size(),
		    int64_t(4));
		assert::DeepEqual(
		    t, layered->GetAccessibleEntries("/link").files,
		    std::vector<std::string>{"host.ts"});
	});

	t->Run("canonical path collisions are rejected", [](T* t) {
		t->Parallel();
		auto base = mkHost({}, false);

		RequestFileSystem p1{
		    .kind = KindFull,
		    .Files = {
		        {"C:\\Repo\\file.ts", "first"},
		        {"c:/repo/file.ts", "second"},
		    },
		};
		auto [r1, err] =
		    newRequestFileSystem(&p1, base, "C:\\Workspace");
		assert::ErrorContains(
		    t, err, "duplicate request filesystem file path");

		RequestFileSystem p2{
		    .kind = KindFull,
		    .Files = {},
		    .Directories = std::map<std::string, RequestDirectoryEntries>{
		        {"C:\\Repo", {}},
		        {"c:/repo/.", {}},
		    },
		};
		auto [r2, err2] =
		    newRequestFileSystem(&p2, base, "C:\\Workspace");
		assert::ErrorContains(
		    t, err2, "duplicate request filesystem directory path");

		RequestFileSystem p3{
		    .kind = KindFull,
		    .Files = {},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"C:\\Repo\\link", {.Target = "C:\\Target"}},
		        {"c:/repo/link", {.Target = "C:\\Other"}},
		    },
		};
		auto [r3, err3] =
		    newRequestFileSystem(&p3, base, "C:\\Workspace");
		assert::ErrorContains(
		    t, err3, "duplicate request filesystem symlink path");
	});

	t->Run("symlink cycles are treated as missing", [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost({{"/host.ts", "host"}});
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/a", {.Target = "/b"}},
		        {"/b", {.Target = "/a"}},
		    },
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base), "/");
		assert::NilError(t, err);

		auto [c, ok] = fileSystem->ReadFile("/a/file.ts");
		assert::Assert(t, !ok);
		assert::Assert(t, !fileSystem->DirectoryExists("/a"));
		assert::Equal(t, fileSystem->Realpath("/a"), std::string("/a"));
		assert::Assert(t, base->SeenFiles.IsEmpty());
	});

	t->Run("posix relative symlink targets resolve from the link "
	       "directory",
	       [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost();
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {
		        {"/packages/pkg/index.d.ts",
		         "export declare const value: number;"},
		    },
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/project/pkg", {.Target = "../packages/pkg"}},
		    },
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base),
		    "C:\\Workspace");
		assert::NilError(t, err);

		auto [contents, ok] =
		    fileSystem->ReadFile("/project/pkg/index.d.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents,
		              std::string("export declare const value: number;"));
		assert::Equal(t,
		              fileSystem->Realpath("/project/pkg/index.d.ts"),
		              std::string("/packages/pkg/index.d.ts"));
		assert::Assert(t, base->SeenFiles.IsEmpty());
	});

	t->Run("vscode document URI paths support listings symlinks and "
	       "tombstones",
	       [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost();
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {
		        {"vscode-remote://ssh-remote+host/workspace/src/index.ts",
		         "index"},
		        {"vscode-remote://ssh-remote+host/workspace/packages/pkg/a.ts",
		         "package"},
		    },
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"vscode-remote://ssh-remote+host/workspace/src/pkg",
		         {.Target = "../packages/pkg"}},
		    },
		    .RemovedPaths = std::vector<std::string>{
		        "vscode-remote://ssh-remote+host/workspace/packages/pkg/removed.ts"},
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base), "/");
		assert::NilError(t, err);

		auto [contents, ok] = fileSystem->ReadFile(
		    "vscode-remote://ssh-remote+host/workspace/src/index.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("index"));
		auto [contents2, ok2] = fileSystem->ReadFile(
		    "vscode-remote://ssh-remote+host/workspace/src/pkg/a.ts");
		assert::Assert(t, ok2);
		assert::Equal(t, contents2, std::string("package"));
		assert::Equal(
		    t,
		    fileSystem->Realpath(
		        "vscode-remote://ssh-remote+host/workspace/src/pkg/a.ts"),
		    std::string("vscode-remote://ssh-remote+host/workspace/"
		                "packages/pkg/a.ts"));
		assert::DeepEqual(
		    t,
		    fileSystem
		        ->GetAccessibleEntries(
		            "vscode-remote://ssh-remote+host/workspace/src")
		        .files,
		    std::vector<std::string>{"index.ts"});
		assert::DeepEqual(
		    t,
		    fileSystem
		        ->GetAccessibleEntries(
		            "vscode-remote://ssh-remote+host/workspace/src")
		        .directories,
		    std::vector<std::string>{"pkg"});
		assert::Assert(t, !fileSystem->FileExists(
		                     "vscode-remote://ssh-remote+host/workspace/"
		                     "src/pkg/removed.ts"));
		assert::Assert(t, base->SeenFiles.IsEmpty());
	});

	t->Run("windows paths resolve symlinks case insensitively",
	       [](T* t) {
		t->Parallel();
		auto base =
		    mkTrackingHost({{"C:/Host/outside.ts", "outside"}}, false);
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {
		        {"C:\\Repo\\Packages\\Pkg\\Index.d.ts",
		         "export declare const windowsValue: number;"},
		    },
		    .Directories = std::map<std::string, RequestDirectoryEntries>{
		        {"C:\\Repo\\Project\\node_modules",
		         {.Files = {}, .Directories = {"pkg"}}}},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"C:\\Repo\\Project\\node_modules\\PKG",
		         {.Target = "..\\..\\Packages\\Pkg"}},
		        {"C:\\Repo\\Project\\Current.d.ts",
		         {.Target = "..\\Packages\\Pkg\\Index.d.ts"}},
		    },
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base),
		    "C:\\Workspace");
		assert::NilError(t, err);

		auto [contents, ok] = fileSystem->ReadFile(
		    "c:\\repo\\project\\NODE_MODULES\\pkg\\INDEX.D.TS");
		assert::Assert(t, ok);
		assert::Equal(
		    t, contents,
		    std::string("export declare const windowsValue: number;"));
		auto [contents2, ok2] =
		    fileSystem->ReadFile("C:\\REPO\\PROJECT\\current.d.ts");
		assert::Assert(t, ok2);
		assert::Equal(
		    t, contents2,
		    std::string("export declare const windowsValue: number;"));
		assert::Equal(
		    t,
		    fileSystem->Realpath(
		        "c:\\repo\\project\\node_modules\\pkg\\index.d.ts"),
		    std::string("C:/Repo/Packages/Pkg/index.d.ts"));

		auto entries = fileSystem->GetAccessibleEntries(
		    "c:\\REPO\\project\\NODE_MODULES");
		assert::DeepEqual(t, entries.directories,
		                  std::vector<std::string>{"PKG"});
		assert::Assert(t, hasSymlinkEntry(entries, "PKG"));
		assert::Assert(t, base->SeenFiles.IsEmpty());
	});

	t->Run("case insensitive symlink matching handles unicode byte "
	       "length changes",
	       [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost({}, false);
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {
		        {"C:/Repo/target.ts", "target"},
		    },
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"C:/Repo/\xE2\x84\xAA",  // K (KELVIN SIGN U+212A)
		         {.Target = "C:/Repo/target.ts"}},
		    },
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base),
		    "C:/Repo");
		assert::NilError(t, err);

		auto [contents, ok] = fileSystem->ReadFile("c:/repo/k");
		assert::Assert(t, ok, "kelvin read");
		assert::Equal(t, contents, std::string("target"),
		              "kelvin content");
	});

	t->Run("full request filesystems are immutable after eager "
	       "compaction",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost({{"/host.ts", "host"}});
		RequestFileSystem memParams{
		    .kind = KindFull,
		    .Files = {{"/src/a.ts", "a"}},
		};
		auto [memory, err] =
		    newRequestFileSystem(&memParams, host, "/");
		assert::NilError(t, err);
		assert::Assert(
		    t, memory->WriteFile("/src/b.ts", "b").is(vfs::ErrInvalid));
		assert::Assert(
		    t, memory->AppendFile("/src/a.ts", "b").is(vfs::ErrInvalid));
		assert::Assert(
		    t, memory->Remove("/src").is(vfs::ErrInvalid));
		auto [contents, ok] = memory->ReadFile("/src/a.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("a"));

		RequestFileSystem layerParams{.kind = KindLayer, .Files = {}};
		auto [cache, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(memory.get(), [](vfs::FS*) {}),
		    "/");
		assert::NilError(t, err2);
		assert::Equal(t, cache->kind, KindFull);
		assert::Assert(
		    t,
		    cache->WriteFile("/written.ts", "written")
		        .is(vfs::ErrInvalid));
	});

	t->Run("layer request filesystems write through after eager "
	       "compaction",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost();
		RequestFileSystem baseParams{.kind = KindLayer, .Files = {}};
		auto [base, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);
		RequestFileSystem layerParams{.kind = KindLayer, .Files = {}};
		auto [cache, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		assert::NilError(t, err2);
		assert::Equal(t, cache->kind, KindLayer);
		assert::Assert(
		    t,
		    cache->WriteFile("/written.ts", "written").impl() ==
		        nullptr);
		assert::Assert(t, cache->AppendFile("/written.ts", " appended")
		                      .impl() == nullptr);
		auto [contents, ok] = host->ReadFile("/written.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("written appended"));
		assert::Assert(
		    t, cache->Remove("/written.ts").impl() == nullptr);
		assert::Assert(t, !host->FileExists("/written.ts"));
	});

	t->Run("cache mutations follow inherited request symlinks",
	       [](T* t) {
		t->Parallel();
		auto host = mkHost({
		    {"/target/write.ts", "target"},
		    {"/target/append.ts", "target"},
		    {"/target/remove.ts", "target"},
		    {"/target/times.ts", "target"},
		    {"/link/write.ts", "alias"},
		    {"/link/append.ts", "alias"},
		    {"/link/remove.ts", "alias"},
		    {"/link/times.ts", "alias"},
		});
		RequestFileSystem baseParams{
		    .kind = KindLayer,
		    .Files = {},
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        {"/link", {.Target = "/target"}},
		    },
		};
		auto [base, err] =
		    newRequestFileSystem(&baseParams, host, "/");
		assert::NilError(t, err);
		RequestFileSystem layerParams{.kind = KindLayer};
		auto [cache, err2] = newLayeredRequestFileSystem(
		    &layerParams,
		    std::shared_ptr<vfs::FS>(base.get(), [](vfs::FS*) {}), "/");
		assert::NilError(t, err2);

		assert::Assert(t, cache->WriteFile("/link/write.ts", "written")
		                      .impl() == nullptr);
		auto [contents, ok] = host->ReadFile("/target/write.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::string("written"));

		assert::Assert(
		    t, cache->AppendFile("/link/append.ts", " appended")
		           .impl() == nullptr);
		auto [contents2, ok2] = host->ReadFile("/target/append.ts");
		assert::Assert(t, ok2);
		assert::Equal(t, contents2, std::string("target appended"));

		assert::Assert(t, cache->Remove("/link/remove.ts").impl() ==
		                      nullptr);
		assert::Assert(t, !host->FileExists("/target/remove.ts"));

		vfs::TimePoint modified =
		    std::chrono::system_clock::from_time_t(123);
		assert::Assert(t, cache->Chtimes("/link/times.ts", modified,
		                                 modified)
		                      .impl() == nullptr);
		assert::Equal(t, host->Stat("/target/times.ts")->ModTime(),
		              modified);
	});

	t->Run("mixed windows and posix roots support cross-root and "
	       "relative symlinks",
	       [](T* t) {
		t->Parallel();
		auto base = mkTrackingHost(
		    {{"C:/Host/node_modules/host-pkg/index.d.ts",
		      "export declare const hostValue: boolean;"}},
		    false);
		RequestFileSystem params{
		    .kind = KindFull,
		    .Files = {
		        {"C:\\Repo\\Packages\\windows-pkg\\index.d.ts",
		         "export declare const windowsValue: number;"},
		        {"/repo/packages/posix-pkg/index.d.ts",
		         "export declare const posixValue: string;"},
		    },
		    .Symlinks = std::map<std::string, RequestSymlink>{
		        // Cross between drive-letter and POSIX roots in both
		        // directions.
		        {"C:\\Repo\\Project\\node_modules\\posix-pkg",
		         {.Target = "/repo/packages/posix-pkg"}},
		        {"/repo/project/node_modules/windows-pkg",
		         {.Target = "C:\\Repo\\Packages\\windows-pkg"}},
		        // Windows symlink targets read from disk may be relative
		        // to the link's directory.
		        {"C:\\Repo\\Project\\windows-pkg.d.ts",
		         {.Target = "..\\Packages\\windows-pkg\\index.d.ts"}},
		        {"C:\\Repo\\Project\\node_modules\\host-pkg",
		         {.Target = "..\\..\\..\\Host\\node_modules\\host-pkg",
		          .Host = true}},
		    },
		};
		auto [fileSystem, err] = newRequestFileSystem(
		    &params, std::static_pointer_cast<vfs::FS>(base),
		    "C:\\Workspace");
		assert::NilError(t, err);

		auto [contents, ok] = fileSystem->ReadFile(
		    "c:\\REPO\\project\\NODE_MODULES\\POSIX-PKG\\INDEX.D.TS");
		assert::Assert(t, ok);
		assert::Equal(
		    t, contents,
		    std::string("export declare const posixValue: string;"));
		auto [contents2, ok2] = fileSystem->ReadFile(
		    "/REPO/PROJECT/NODE_MODULES/WINDOWS-PKG/INDEX.D.TS");
		assert::Assert(t, ok2);
		assert::Equal(
		    t, contents2,
		    std::string("export declare const windowsValue: number;"));
		auto [contents3, ok3] = fileSystem->ReadFile(
		    "c:\\repo\\project\\WINDOWS-PKG.D.TS");
		assert::Assert(t, ok3);
		assert::Equal(
		    t, contents3,
		    std::string("export declare const windowsValue: number;"));
		auto [contents4, ok4] = fileSystem->ReadFile(
		    "C:\\Repo\\Project\\node_modules\\HOST-PKG\\index.d.ts");
		assert::Assert(t, ok4);
		assert::Equal(
		    t, contents4,
		    std::string("export declare const hostValue: boolean;"));

		assert::Equal(
		    t,
		    fileSystem->Realpath(
		        "c:\\repo\\project\\node_modules\\posix-pkg\\index.d.ts"),
		    std::string("/repo/packages/posix-pkg/index.d.ts"));
		assert::Equal(
		    t,
		    fileSystem->Realpath(
		        "/repo/project/node_modules/windows-pkg/index.d.ts"),
		    std::string("C:/Repo/Packages/windows-pkg/index.d.ts"));
		assert::Assert(
		    t,
		    base->SeenFiles.Has(
		        "C:/Host/node_modules/host-pkg/index.d.ts"));
	});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestFileSystem",
                   TestRequestFileSystem);

}  // namespace
}  // namespace tsc::api::requestfilesystem
