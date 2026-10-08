// Port of tsc/internal/api/requestfilesystem/pathtree_test.go (package
// requestfilesystem).
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

#include "internal/api/requestfilesystem/requestfilesystem.h"
#include "internal/collections/collections.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/project/project.h"
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

// newRequestFileSystem — test helper (requestfilesystem_test.go:35).
std::pair<std::shared_ptr<requestFileSystem>, gostd::Error>
newRequestFileSystem(RequestFileSystem* params,
                     const std::shared_ptr<vfs::FS>& base,
                     const std::string& currentDirectory) {
	project::FileChangeSummary fileChanges;
	auto [fileSystem, err] =
	    NewForUpdate(params, base, currentDirectory, &fileChanges);
	if (err != nullptr) {
		return {nullptr, err};
	}
	// Aliasing shared_ptr keeps `fileSystem`'s owner while exposing the
	// requestFileSystem view (vfs::FS is a virtual base, so no
	// static_pointer_cast).
	return {std::shared_ptr<requestFileSystem>(
	            fileSystem,
	            dynamic_cast<requestFileSystem*>(fileSystem.get())),
	        nullptr};
}

// newLayeredRequestFileSystem — requestfilesystem_test.go:39.
std::pair<std::shared_ptr<requestFileSystem>, gostd::Error>
newLayeredRequestFileSystem(RequestFileSystem* params,
                            const std::shared_ptr<vfs::FS>& base,
                            const std::string& currentDirectory) {
	return newRequestFileSystem(params, base, currentDirectory);
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

// deepEqualAny — reflect.DeepEqual over the `any` shapes this test's verify
// lambdas produce (bools, strings, {content,ok} pairs, Entries,
// statProjection, or nil).
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
		return x.Name == y.Name && x.Size == y.Size &&
		       x.Mode == y.Mode && x.ModTime == y.ModTime &&
		       x.Directory == y.Directory &&
		       tsc::gotest::assert::detail::anyEqual(x.Sys, y.Sys);
	}
	return tsc::gotest::assert::detail::anyEqual(a, b);
}

// Node-entry constructors — requestFile/requestDirectory/requestSymlink are
// non-aggregate (virtual member functions), so build them explicitly.
requestFile* mkRequestFile(std::string fileName, std::string content) {
	auto* f = new requestFile;
	f->fileName = std::move(fileName);
	f->content = std::move(content);
	return f;
}
requestDirectory* mkRequestDirectory(std::string directoryName,
                                     vfs::Entries* listing = nullptr) {
	auto* d = new requestDirectory;
	d->directoryName = std::move(directoryName);
	d->listing = listing;
	return d;
}
requestSymlink* mkRequestSymlink(std::string linkName, std::string target) {
	auto* s = new requestSymlink;
	s->linkName = std::move(linkName);
	s->target = std::move(target);
	return s;
}

// sharedRequestFS — shared_ptr aliasing a requestFileSystem owned by the
// test (the composed layers are leaked, matching GC semantics).
std::shared_ptr<vfs::FS> sharedRequestFS(requestFileSystem* fs) {
	return std::shared_ptr<vfs::FS>(fs, [](vfs::FS*) {});
}

// verifyCompactionWithoutHostReads — requestfilesystem_test.go:48.
void verifyCompactionWithoutHostReads(T* t, requestFileSystem* layer,
                                      vfs::trackingvfs::FS* host,
                                      const std::vector<std::string>& paths) {
	t->Helper();
	RequestFileSystem params{.kind = KindLayer};
	auto [compacted, err] = newRequestFileSystem(
	    &params, sharedRequestFS(layer), layer->currentDirectory);
	assert::NilError(t, err);
	requestFileSystem* compactedPtr = compacted.get();
	auto verify = [&](const std::string& name,
	                  const std::function<std::any(vfs::FS*,
	                                               const std::string&)>& run) {
		t->Helper();
		for (const auto& path : paths) {
			for (const auto& seen : host->SeenFiles.ToSlice()) {
				host->SeenFiles.Delete(seen);
			}
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
	verify("GetAccessibleEntries", [](vfs::FS* fs, const std::string& path) {
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

void TestRequestPathTreeChildOverridesInheritedMissing(T* t) {
	t->Parallel();
	auto base = std::make_unique<requestPathNode>();
	base->ensure("/dir")->fallback = requestFallback::Missing;
	auto layer = std::make_unique<requestPathNode>();
	layer->ensure("/dir/pkg")->entry =
	    mkRequestSymlink("/dir/pkg", "/target");
	requestPathNode* compacted = composeRequestPaths(
	    base.get(), layer.get(), requestFallback::Allowed, true);
	auto [n1, fallback] = compacted->lookup("/dir/pkg/file.ts");
	assert::Equal(t, fallback, requestFallback::Allowed);
	auto [n2, fallback2] = compacted->lookup("/dir/other.ts");
	assert::Equal(t, fallback2, requestFallback::Missing);
	auto [n3, fallback3] = base->lookup("/dir/pkg/file.ts");
	assert::Equal(t, fallback3, requestFallback::Missing);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeChildOverridesInheritedMissing",
                   TestRequestPathTreeChildOverridesInheritedMissing);

void TestRequestPathTreeSameLayerMissingBlocksSymlink(T* t) {
	t->Parallel();
	auto layer = std::make_unique<requestPathNode>();
	layer->ensure("/dir")->fallback = requestFallback::Missing;
	layer->ensure("/dir/pkg")->entry =
	    mkRequestSymlink("/dir/pkg", "/target");
	auto emptyBase = std::make_unique<requestPathNode>();
	requestPathNode* compacted = composeRequestPaths(
	    emptyBase.get(), layer.get(), requestFallback::Allowed, true);
	auto [n, fallback] = compacted->lookup("/dir/pkg/file.ts");
	assert::Equal(t, fallback, requestFallback::Missing);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeSameLayerMissingBlocksSymlink",
                   TestRequestPathTreeSameLayerMissingBlocksSymlink);

void TestRequestPathTreeDirectoryPreservesInheritedMissing(T* t) {
	t->Parallel();
	auto base = std::make_unique<requestPathNode>();
	base->ensure("/dir")->fallback = requestFallback::Missing;
	auto layer = std::make_unique<requestPathNode>();
	layer->ensure("/dir/new")->entry = mkRequestDirectory("/dir/new");
	requestPathNode* compacted = composeRequestPaths(
	    base.get(), layer.get(), requestFallback::Allowed, true);
	auto [node, fallback] = compacted->lookup("/dir/new");
	auto* directory = dynamic_cast<requestDirectory*>(node->entry);
	assert::Assert(t, directory != nullptr);
	assert::Equal(t, directory->directoryName, std::string("/dir/new"));
	assert::Equal(t, fallback, requestFallback::Missing);
	auto [n2, fallback2] = compacted->lookup("/dir/new/old.ts");
	assert::Equal(t, fallback2, requestFallback::Missing);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeDirectoryPreservesInheritedMissing",
                   TestRequestPathTreeDirectoryPreservesInheritedMissing);

void TestRequestPathTreeFileReplacesSubtree(T* t) {
	t->Parallel();
	auto base = std::make_unique<requestPathNode>();
	base->ensure("/dir")->entry = mkRequestDirectory(
	    "/dir", new vfs::Entries{
	                .files = std::vector<std::string>{"old.ts"}});
	base->ensure("/dir/old.ts")->entry =
	    mkRequestFile("/dir/old.ts", "old");
	auto layer = std::make_unique<requestPathNode>();
	layer->ensure("/dir")->entry = mkRequestFile("/dir", "new");
	requestPathNode* compacted = composeRequestPaths(
	    base.get(), layer.get(), requestFallback::Allowed, true);
	auto [node, f1] = compacted->lookup("/dir");
	auto* file = dynamic_cast<requestFile*>(node->entry);
	assert::Assert(t, file != nullptr);
	assert::Equal(t, file->content, std::string("new"));
	assert::Equal(t, int(node->children.size()), 0);
	assert::Assert(t, compacted->containsFileAncestor("/dir/old.ts"));
	auto [previous, f2] = base->lookup("/dir/old.ts");
	auto* previousFile = dynamic_cast<requestFile*>(previous->entry);
	assert::Assert(t, previousFile != nullptr);
	assert::Equal(t, previousFile->content, std::string("old"));
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeFileReplacesSubtree",
                   TestRequestPathTreeFileReplacesSubtree);

void TestRequestPathTreeListingReplacementDoesNotRemoveFiles(T* t) {
	t->Parallel();
	auto inner = vfs::vfstest::FromMap(
	    std::unordered_map<std::string, vfs::vfstest::MapFileInput>{}, true);
	auto hostOwner = std::make_unique<vfs::trackingvfs::FS>(inner.get());
	vfs::trackingvfs::FS* host = hostOwner.get();
	RequestFileSystem baseParams{
	    .kind = KindLayer,
	    .Files = {{"/dir/retained.ts", "retained"}},
	    .Directories = std::map<std::string, RequestDirectoryEntries>{
	        {"/dir", {.Files = {"retained.ts"}}}},
	};
	auto [base, err] =
	    newRequestFileSystem(&baseParams,
	                         std::shared_ptr<vfs::FS>(host, [](vfs::FS*) {}),
	                         "/");
	assert::NilError(t, err);
	RequestFileSystem layerParams{
	    .kind = KindLayer,
	    .Directories = std::map<std::string, RequestDirectoryEntries>{
	        {"/dir",
	         {.Files = {}, .Directories = {}}}},
	};
	auto [compacted, err2] =
	    newLayeredRequestFileSystem(&layerParams,
	                                sharedRequestFS(base.get()), "/");
	assert::NilError(t, err2);
	auto [content, ok] = compacted->ReadFile("/dir/retained.ts");
	assert::Assert(t, ok);
	assert::Equal(t, content, std::string("retained"));
	assert::DeepEqual(
	    t, compacted->GetAccessibleEntries("/dir").files,
	    std::vector<std::string>{});
	assert::DeepEqual(t, base->GetAccessibleEntries("/dir").files,
	              std::vector<std::string>{"retained.ts"});
	verifyCompactionWithoutHostReads(
	    t, compacted.get(), host, {"/dir", "/dir/retained.ts"});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeListingReplacementDoesNotRemoveFiles",
                   TestRequestPathTreeListingReplacementDoesNotRemoveFiles);

void TestRequestPathTreeCompositionPreservesListingSnapshots(T* t) {
	t->Parallel();
	auto base = std::make_unique<requestPathNode>();
	base->ensure("/dir")->entry = mkRequestDirectory(
	    "/dir", new vfs::Entries{
	                .files = std::vector<std::string>{"OLD.ts"}});
	base->ensure("/dir/old.ts")->entry =
	    mkRequestFile("/dir/OLD.ts", "");
	auto layer = std::make_unique<requestPathNode>();
	layer->ensure("/dir/old.ts")->fallback = requestFallback::Missing;
	layer->ensure("/dir/new.ts")->entry =
	    mkRequestFile("/dir/new.ts", "");
	requestPathNode* compacted = composeRequestPaths(
	    base.get(), layer.get(), requestFallback::Allowed, false);
	auto [node, f1] = compacted->lookup("/dir");
	auto* directory = dynamic_cast<requestDirectory*>(node->entry);
	assert::Assert(t, directory != nullptr);
	assert::DeepEqual(t, directory->listing->files,
	              std::vector<std::string>{"new.ts"});
	auto [previous, f2] = base->lookup("/dir");
	auto* previousDirectory =
	    dynamic_cast<requestDirectory*>(previous->entry);
	assert::Assert(t, previousDirectory != nullptr);
	assert::DeepEqual(t, previousDirectory->listing->files,
	              std::vector<std::string>{"OLD.ts"});
	auto emptyLayer = std::make_unique<requestPathNode>();
	requestPathNode* next = composeRequestPaths(
	    compacted, emptyLayer.get(), requestFallback::Allowed, false);
	auto [nextNode, f3] = next->lookup("/dir");
	assert::Assert(t, nextNode == node);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeCompositionPreservesListingSnapshots",
                   TestRequestPathTreeCompositionPreservesListingSnapshots);

void TestRequestPathTreeFileTakesPrecedenceOverSameLayerSymlink(T* t) {
	t->Parallel();
	RequestFileSystem params{
	    .kind = KindFull,
	    .Files = {
	        {"/item", "file"},
	        {"/target/file.ts", "target"},
	    },
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/item", {.Target = "/target"}}},
	};
	auto [fileSystem, err] =
	    newRequestFileSystem(&params,
	                         vfs::vfstest::FromMap(
	                             std::unordered_map<std::string,
	                                                vfs::vfstest::MapFileInput>{},
	                             true),
	                         "/");
	assert::NilError(t, err);
	auto [content, ok] = fileSystem->ReadFile("/item");
	assert::Assert(t, ok);
	assert::Equal(t, content, std::string("file"));
	assert::Assert(t, !fileSystem->DirectoryExists("/item"));
	assert::Equal(t, fileSystem->Realpath("/item"), std::string("/item"));
	auto [node, f1] = fileSystem->paths->lookup("/item");
	assert::Assert(t, dynamic_cast<requestFile*>(node->entry) != nullptr);
	assert::Assert(t, !fileSystem->paths->hasSymlinks);
	assert::DeepEqual(t, fileSystem->GetAccessibleEntries("/").files,
	              std::vector<std::string>{"item"});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeFileTakesPrecedenceOverSameLayerSymlink",
                   TestRequestPathTreeFileTakesPrecedenceOverSameLayerSymlink);

void TestRequestPathTreeDirectoryTakesPrecedenceOverSameLayerSymlink(T* t) {
	t->Parallel();
	RequestFileSystem params{
	    .kind = KindFull,
	    .Files = {
	        {"/item/child.ts", "child"},
	        {"/target.ts", "target"},
	    },
	    .Directories = std::map<std::string, RequestDirectoryEntries>{
	        {"/item", {.Files = {"child.ts"}}}},
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/item", {.Target = "/target.ts"}}},
	};
	auto [fileSystem, err] =
	    newRequestFileSystem(&params,
	                         vfs::vfstest::FromMap(
	                             std::unordered_map<std::string,
	                                                vfs::vfstest::MapFileInput>{},
	                             true),
	                         "/");
	assert::NilError(t, err);
	assert::Assert(t, fileSystem->DirectoryExists("/item"));
	assert::Assert(t, !fileSystem->FileExists("/item"));
	assert::Equal(t, fileSystem->Realpath("/item"), std::string("/item"));
	auto [content, ok] = fileSystem->ReadFile("/item/child.ts");
	assert::Assert(t, ok);
	assert::Equal(t, content, std::string("child"));
	auto [node, f1] = fileSystem->paths->lookup("/item");
	assert::Assert(t, dynamic_cast<requestDirectory*>(node->entry) !=
	               nullptr);
	assert::Assert(t, !fileSystem->paths->hasSymlinks);
	assert::DeepEqual(t, fileSystem->GetAccessibleEntries("/item").files,
	              std::vector<std::string>{"child.ts"});
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeDirectoryTakesPrecedenceOverSameLayerSymlink",
                   TestRequestPathTreeDirectoryTakesPrecedenceOverSameLayerSymlink);

void TestRequestPathTreeSymlinkTakesPrecedenceOverListingHint(T* t) {
	t->Parallel();
	RequestFileSystem params{
	    .kind = KindFull,
	    .Files = {{"/target/file.ts", "target"}},
	    .Directories = std::map<std::string, RequestDirectoryEntries>{
	        {"/links", {.Directories = {"pkg"}}}},
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/links/pkg", {.Target = "/target"}}},
	};
	auto [fileSystem, err] =
	    newRequestFileSystem(&params,
	                         vfs::vfstest::FromMap(
	                             std::unordered_map<std::string,
	                                                vfs::vfstest::MapFileInput>{},
	                             true),
	                         "/");
	assert::NilError(t, err);
	auto [node, f1] = fileSystem->paths->lookup("/links/pkg");
	assert::Assert(t, dynamic_cast<requestSymlink*>(node->entry) !=
	               nullptr);
	assert::Assert(t, fileSystem->paths->hasSymlinks);
	auto [content, ok] = fileSystem->ReadFile("/links/pkg/file.ts");
	assert::Assert(t, ok);
	assert::Equal(t, content, std::string("target"));
	assert::Equal(t, fileSystem->Realpath("/links/pkg"),
	              std::string("/target"));
	assert::DeepEqual(t, fileSystem->GetAccessibleEntries("/links"),
	              vfs::Entries{
	                  .directories = {"pkg"},
	                  .symlinks = std::unordered_set<std::string>{"pkg"},
	              });
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeSymlinkTakesPrecedenceOverListingHint",
                   TestRequestPathTreeSymlinkTakesPrecedenceOverListingHint);

void TestRequestPathTreeFileTakesPrecedenceOverSameLayerDirectory(T* t) {
	t->Parallel();
	RequestFileSystem params{
	    .kind = KindFull,
	    .Files = {{"/item", "file"}},
	    .Directories = std::map<std::string, RequestDirectoryEntries>{
	        {"/item", {.Files = {"listed.ts"}}}},
	};
	auto [fileSystem, err] =
	    newRequestFileSystem(&params,
	                         vfs::vfstest::FromMap(
	                             std::unordered_map<std::string,
	                                                vfs::vfstest::MapFileInput>{},
	                             true),
	                         "/");
	assert::NilError(t, err);
	auto [node, f1] = fileSystem->paths->lookup("/item");
	assert::Assert(t, dynamic_cast<requestFile*>(node->entry) != nullptr);
	auto [content, ok] = fileSystem->ReadFile("/item");
	assert::Assert(t, ok);
	assert::Equal(t, content, std::string("file"));
	assert::Assert(t, !fileSystem->DirectoryExists("/item"));
	assert::Equal(
	    t, int(fileSystem->GetAccessibleEntries("/item").files.size()), 0);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeFileTakesPrecedenceOverSameLayerDirectory",
                   TestRequestPathTreeFileTakesPrecedenceOverSameLayerDirectory);

void TestRequestPathTreeFileProvidesStatAndDirEntry(T* t) {
	t->Parallel();
	RequestFileSystem params{
	    .kind = KindFull,
	    .Files = {{"/dir/file.ts", "file content"}},
	};
	auto [fileSystem, err] =
	    newRequestFileSystem(&params,
	                         vfs::vfstest::FromMap(
	                             std::unordered_map<std::string,
	                                                vfs::vfstest::MapFileInput>{},
	                             true),
	                         "/");
	assert::NilError(t, err);
	auto [node, f1] = fileSystem->paths->lookup("/dir/file.ts");
	auto info = fileSystem->Stat("/dir/file.ts");
	assert::Assert(t, info != nullptr);
	assert::Equal(t, info->Name(), std::string("file.ts"));
	assert::Equal(t, info->Size(), int64_t(12));
	assert::Equal(t, info->Mode(), vfs::FileMode{0444});
	assert::Assert(t, !info->IsDir());
	assert::Assert(t, info->ModTime() == vfs::TimePoint{});
	assert::Assert(t, !info->Sys().has_value());
	assert::Assert(t, dynamic_cast<requestFile*>(info.get()) ==
	                  dynamic_cast<requestFile*>(node->entry));
	auto* entry = dynamic_cast<vfs::DirEntry*>(node->entry);
	assert::Assert(t, entry != nullptr);
	assert::Equal(t, entry->Type(), vfs::FileMode{0});
	auto [entryInfo, infoErr] = entry->Info();
	assert::Assert(t, infoErr.impl() == nullptr);
	assert::Assert(t, entryInfo == info);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeFileProvidesStatAndDirEntry",
                   TestRequestPathTreeFileProvidesStatAndDirEntry);

void TestRequestPathTreeDirectoryProvidesStatAndDirEntry(T* t) {
	t->Parallel();
	RequestFileSystem params{
	    .kind = KindFull,
	    .Directories = std::map<std::string, RequestDirectoryEntries>{
	        {"/dir", {}}},
	};
	auto [fileSystem, err] =
	    newRequestFileSystem(&params,
	                         vfs::vfstest::FromMap(
	                             std::unordered_map<std::string,
	                                                vfs::vfstest::MapFileInput>{},
	                             true),
	                         "/");
	assert::NilError(t, err);
	auto [node, f1] = fileSystem->paths->lookup("/dir");
	auto info = fileSystem->Stat("/dir");
	assert::Assert(t, info != nullptr);
	assert::Equal(t, info->Name(), std::string("dir"));
	assert::Equal(t, info->Size(), int64_t(0));
	assert::Equal(t, info->Mode(),
	              vfs::FileMode{vfs::ModeDir.v | 0555});
	assert::Assert(t, info->IsDir());
	assert::Assert(t, info->ModTime() == vfs::TimePoint{});
	assert::Assert(t, !info->Sys().has_value());
	assert::Assert(t, dynamic_cast<requestDirectory*>(info.get()) ==
	                  dynamic_cast<requestDirectory*>(node->entry));
	auto* entry = dynamic_cast<vfs::DirEntry*>(node->entry);
	assert::Assert(t, entry != nullptr);
	assert::Equal(t, entry->Type(), vfs::ModeDir);
	auto [entryInfo, infoErr] = entry->Info();
	assert::Assert(t, infoErr.impl() == nullptr);
	assert::Assert(t, entryInfo == info);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeDirectoryProvidesStatAndDirEntry",
                   TestRequestPathTreeDirectoryProvidesStatAndDirEntry);

void TestRequestPathTreeSymlinkReportsTargetMetadata(T* t) {
	t->Parallel();
	RequestFileSystem params{
	    .kind = KindFull,
	    .Files = {{"/target/file.ts", "target content"}},
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/link.ts", {.Target = "/target/file.ts"}}},
	};
	auto [fileSystem, err] =
	    newRequestFileSystem(&params,
	                         vfs::vfstest::FromMap(
	                             std::unordered_map<std::string,
	                                                vfs::vfstest::MapFileInput>{},
	                             true),
	                         "/");
	assert::NilError(t, err);
	auto info = fileSystem->Stat("/target/file.ts");
	assert::Assert(t, fileSystem->Stat("/link.ts") == info);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeSymlinkReportsTargetMetadata",
                   TestRequestPathTreeSymlinkReportsTargetMetadata);

// requestTestHostMetadata — pathtree_test.go. A host FS that serves a fixed
// FileInfo from Stat and forwards everything else.
struct requestTestHostMetadata final : vfs::FS {
	std::shared_ptr<vfs::FS> inner;
	std::shared_ptr<vfs::FileInfo> info;

	std::shared_ptr<vfs::FileInfo> Stat(const std::string& path) override {
		return info;
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
};

void TestRequestPathTreeStatPreservesHostMetadata(T* t) {
	t->Parallel();
	auto hostFS = vfs::vfstest::FromMap(
	    std::unordered_map<std::string, vfs::vfstest::MapFileInput>{
	        {"/host.ts", "host content"}},
	    true);
	using namespace std::chrono;
	vfs::TimePoint modified =
	    sys_days(year{2026} / month{9} / day{9}) + hours{12};
	assert::Assert(
	    t, hostFS->Chtimes("/host.ts", modified, modified).impl() ==
	           nullptr);
	auto info = hostFS->Stat("/host.ts");
	auto host = std::make_shared<requestTestHostMetadata>();
	host->inner = hostFS;
	host->info = info;
	RequestFileSystem params{.kind = KindLayer};
	auto [fileSystem, err] = newRequestFileSystem(&params, host, "/");
	assert::NilError(t, err);
	assert::Assert(t, fileSystem->Stat("/host.ts") == info);
	auto entryInfo = fileSystem->Stat("/host.ts");
	assert::Assert(t, entryInfo->ModTime() == modified);
	assert::Equal(t, entryInfo->Size(), int64_t(12));
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeStatPreservesHostMetadata",
                   TestRequestPathTreeStatPreservesHostMetadata);

void TestRequestPathTreeStatSupportsExistenceOnlyHost(T* t) {
	t->Parallel();
	auto host = std::make_shared<requestTestHostMetadata>();
	host->inner = vfs::vfstest::FromMap(
	    std::unordered_map<std::string, vfs::vfstest::MapFileInput>{
	        {"/dir/file.ts", "host content"}},
	    true);
	RequestFileSystem params{.kind = KindLayer};
	auto [fileSystem, err] = newRequestFileSystem(&params, host, "/");
	assert::NilError(t, err);
	auto fileInfo = fileSystem->Stat("/dir/file.ts");
	assert::Assert(t, fileInfo != nullptr);
	assert::Equal(t, fileInfo->Name(), std::string("file.ts"));
	assert::Equal(t, fileInfo->Size(), int64_t(0));
	assert::Equal(t, fileInfo->Mode(), vfs::FileMode{0444});
	assert::Assert(t, !fileInfo->IsDir());
	auto directoryInfo = fileSystem->Stat("/dir");
	assert::Assert(t, directoryInfo != nullptr);
	assert::Equal(t, directoryInfo->Name(), std::string("dir"));
	assert::Equal(t, directoryInfo->Mode(),
	              vfs::FileMode{vfs::ModeDir.v | 0555});
	assert::Assert(t, directoryInfo->IsDir());
	assert::Assert(t, fileSystem->Stat("/missing") == nullptr);
}
REGISTER_UNIT_TEST("requestfilesystem.TestRequestPathTreeStatSupportsExistenceOnlyHost",
                   TestRequestPathTreeStatSupportsExistenceOnlyHost);

}  // namespace
}  // namespace tsc::api::requestfilesystem
