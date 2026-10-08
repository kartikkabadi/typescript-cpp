// autoimport.go — autoImportBuilderFS + autoImportRegistryCloneHost.
//
// autoimport.h
#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/packagejson/packagejson.h"
#include "internal/project/parsecache.h"
#include "internal/project/projectcollection.h"
#include "internal/project/snapshotfs.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc::project {

// autoImportBuilderFS — autoimport.go:16.
struct autoImportBuilderFS : FileSource {
	snapshotFSBuilder* snapshotFSBuilder_ = nullptr;
	collections::SyncMap<tspath::Path, FileHandle*> untrackedFiles;

	// FS implements FileSource.
	vfs::FS* FS() override { return snapshotFSBuilder_->fs; }

	// GetFile implements FileSource.
	FileHandle* GetFile(const std::string& fileName) override {
		auto path = snapshotFSBuilder_->toPath(fileName);
		return GetFileByPath(fileName, path);
	}

	// GetFileByPath implements FileSource. We want to avoid long-term
	// caching of files referenced only by auto-imports, so we override
	// GetFileByPath to avoid collecting more files into the
	// snapshotFSBuilder's cacheFiles. (Note the reason we can't just
	// use the finalized SnapshotFS is that changed files not read
	// during other parts of the snapshot clone will be marked as
	// dirty, but not yet refreshed from the source filesystem.)
	FileHandle* GetFileByPath(
	    const std::string& fileName,
	    const tspath::Path& path) override {
		auto [cachedFile, ok] =
		    snapshotFSBuilder_->cacheFiles->Load(path);
		if (ok) {
			return snapshotFSBuilder_->reloadEntryIfNeeded(
			    cachedFile);
		}
		auto [fh, ok2] = untrackedFiles.Load(path);
		if (ok2) {
			return fh;
		}
		auto* fh2 =
		    snapshotFSBuilder_->fs->GetFileByPath(fileName, path);
		auto [storedFh, _loaded] =
		    untrackedFiles.LoadOrStore(path, fh2);
		return storedFh;
	}

	vfs::Entries GetAccessibleEntries(
	    const std::string& path) override {
		return snapshotFSBuilder_->GetAccessibleEntries(path);
	}

	// FileExists implements FileSource.
	bool FileExists(const std::string& fileName,
	                const tspath::Path& path) override {
		return snapshotFSBuilder_->FileExists(fileName, path);
	}
};

// autoImportRegistryCloneHost — autoimport.go:71.
struct autoImportRegistryCloneHost : ls::autoimport::RegistryCloneHost {
	ProjectCollection* projectCollection = nullptr;
	ParseCache* parseCache = nullptr;
	sourceFS* fs = nullptr;
	std::string currentDirectory;

	std::mutex filesMu;
	std::vector<ParseCacheKey> files;

	// ls::autoimport::InternProjectID is the single canonical intern
	// cache — pointer identity must match every other place a
	// ProjectID* is produced (Session::internProjectID, the API
	// layer), since Go keys these maps by the ProjectID interface's
	// value.
	ls::autoimport::ProjectID* internID(const ID& id) {
		// Global intern — Go keys these maps by the ProjectID
		// interface's VALUE, so every site must produce the
		// canonical pointer for a given id string.
		return ls::autoimport::InternProjectID(idString(id));
	}

	// FS implements autoimport.RegistryCloneHost.
	std::shared_ptr<vfs::FS> FS() override {
		return std::shared_ptr<vfs::FS>(
		    fs, [](vfs::FS*) {});
	}

	// GetCurrentDirectory implements module::ResolutionHost.
	std::string GetCurrentDirectory() override {
		return currentDirectory;
	}

	// Remaining module::ResolutionHost methods forward to fs.
	bool FileExists(std::string_view path) override {
		return fs->FileExists(std::string{path});
	}
	bool DirectoryExists(std::string_view path) override {
		return fs->DirectoryExists(std::string{path});
	}
	std::optional<std::string> ReadFile(
	    std::string_view path) override {
		auto res = fs->ReadFile(std::string{path});
		if (!res.second) {
			return std::nullopt;
		}
		return res.first;
	}
	std::string Realpath(std::string_view path) override {
		return fs->Realpath(std::string{path});
	}
	bool UseCaseSensitiveFileNames() override {
		return fs->UseCaseSensitiveFileNames();
	}
	module::ResolutionHost::AccessibleEntries
	GetAccessibleEntries(std::string_view path) override {
		auto entries = fs->GetAccessibleEntries(std::string{path});
		return {std::move(entries.files),
		        std::move(entries.directories),
		        std::move(entries.symlinks)};
	}

	// GetDefaultProject implements autoimport.RegistryCloneHost.
	std::pair<ls::autoimport::ProjectID*, compiler::SimpleProgram*>
	GetDefaultProject(const tspath::Path& path) override {
		auto* project = projectCollection->GetDefaultProject(path);
		if (project == nullptr) {
			return {nullptr, nullptr};
		}
		return {internID(project->ID()), project->GetProgram()};
	}

	// GetPackageJson implements autoimport.RegistryCloneHost.
	std::shared_ptr<packagejson::InfoCacheEntry> GetPackageJson(
	    const std::string& fileName) override {
		// !!! ref-counted shared cache
		auto* fh = fs->GetFile(fileName);
		auto packageDirectory = tspath::getDirectoryPath(fileName);
		if (fh == nullptr) {
			auto* entry = new packagejson::InfoCacheEntry();
			entry->DirectoryExists =
			    fs->DirectoryExists(packageDirectory);
			entry->PackageDirectory = packageDirectory;
			return std::shared_ptr<packagejson::InfoCacheEntry>(
			    entry);
		}
		auto [fields, ok] = packagejson::Parse(fh->Content());
		if (!ok) {
			auto* entry = new packagejson::InfoCacheEntry();
			entry->DirectoryExists = true;
			entry->PackageDirectory =
			    tspath::getDirectoryPath(fileName);
			entry->Contents =
			    std::make_shared<packagejson::PackageJson>();
			entry->Contents->Parseable = false;
			return std::shared_ptr<packagejson::InfoCacheEntry>(
			    entry);
		}
		auto* entry = new packagejson::InfoCacheEntry();
		entry->DirectoryExists = true;
		entry->PackageDirectory =
		    tspath::getDirectoryPath(fileName);
		entry->Contents =
		    std::make_shared<packagejson::PackageJson>();
		static_cast<packagejson::Fields&>(
		    *entry->Contents) = std::move(fields);
		entry->Contents->Parseable = true;
		return std::shared_ptr<packagejson::InfoCacheEntry>(entry);
	}

	// GetProgramForProject implements autoimport.RegistryCloneHost.
	compiler::SimpleProgram* GetProgramForProject(
	    ls::autoimport::ProjectID* projectID) override {
		auto* project =
		    projectCollection->GetProject(ID(projectID->String()));
		if (project == nullptr) {
			return nullptr;
		}
		return project->GetProgram();
	}

	// GetSourceFile implements autoimport.RegistryCloneHost.
	SourceFile* GetSourceFile(
	    const std::string& fileName,
	    const tspath::Path& path) override {
		auto* fh = fs->GetFile(fileName);
		if (fh == nullptr) {
			return nullptr;
		}
		SourceFileParseOptions opts;
		opts.FileName = fileName;
		opts.Path = path;
		auto key = newParseCacheKey(opts, fh->Hash(), fh->Kind());
		auto* result = parseCache->Acquire(key, fh);

		std::lock_guard<std::mutex> lk(filesMu);
		files.push_back(key);

		return result;
	}

	// Dispose implements autoimport.RegistryCloneHost.
	void Dispose() override {
		std::lock_guard<std::mutex> lk(filesMu);
		for (auto& key : files) {
			parseCache->Deref(key);
		}
	}
};

// newAutoImportRegistryCloneHost — autoimport.go:83.
inline autoImportRegistryCloneHost* newAutoImportRegistryCloneHost(
    ProjectCollection* projectCollection, ParseCache* parseCache,
    snapshotFSBuilder* builder, const std::string& currentDirectory,
    const std::function<tspath::Path(const std::string&)>& toPath) {
	auto* host = new autoImportRegistryCloneHost();
	host->projectCollection = projectCollection;
	host->parseCache = parseCache;
	auto* fs = new autoImportBuilderFS();
	fs->snapshotFSBuilder_ = builder;
	host->fs = newSourceFS(false, fs, toPath);
	host->currentDirectory = currentDirectory;
	return host;
}

} // namespace tsc::project
