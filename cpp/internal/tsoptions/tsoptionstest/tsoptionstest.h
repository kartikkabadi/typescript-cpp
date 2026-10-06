// Declarations for tsc/internal/tsoptions/tsoptionstest — VFS-backed
// ParseConfigHost used by tests. Declared for the testrunner slice; bodies
// are real dep-impls in testrunner_deps.cpp (they sit on makeUnitsFromTest's
// unconditional path) — replace when the owning slice lands.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "internal/tsoptions/tsoptions.h"
#include "internal/vfs/vfs.h"

namespace tsc::tsoptions::tsoptionstest {

// VfsParseConfigHost — vfsparseconfighost.go:15.
struct VfsParseConfigHost : tsoptions::ParseConfigHost {
	std::shared_ptr<vfs::FS> Vfs;
	std::string CurrentDirectory;

	bool FileExists(std::string_view path) override {
		return Vfs->FileExists(std::string(path));
	}
	bool DirectoryExists(std::string_view path) override {
		return Vfs->DirectoryExists(std::string(path));
	}
	std::optional<std::string> ReadFile(std::string_view path) override {
		auto [text, ok] = Vfs->ReadFile(std::string(path));
		if (!ok) return std::nullopt;
		return text;
	}
	std::string Realpath(std::string_view path) override {
		return Vfs->Realpath(std::string(path));
	}
	std::string GetCurrentDirectory() override { return CurrentDirectory; }
	bool UseCaseSensitiveFileNames() override {
		return Vfs->UseCaseSensitiveFileNames();
	}
	module::ResolutionHost::AccessibleEntries GetAccessibleEntries(
	    std::string_view path) override {
		auto entries = Vfs->GetAccessibleEntries(std::string(path));
		return {std::move(entries.files), std::move(entries.directories),
		        std::move(entries.symlinks)};
	}
};

// NewVFSParseConfigHost — vfsparseconfighost.go:36.
std::unique_ptr<VfsParseConfigHost> NewVFSParseConfigHost(
    const std::unordered_map<std::string, std::string>& files,
    const std::string& currentDirectory, bool useCaseSensitiveFileNames);

// NewVFSParseConfigHostWithSymlinks — vfsparseconfighost.go:45.
std::unique_ptr<VfsParseConfigHost> NewVFSParseConfigHostWithSymlinks(
    const std::unordered_map<std::string, std::string>& files,
    const std::unordered_map<std::string, std::string>& symlinks,
    const std::string& currentDirectory, bool useCaseSensitiveFileNames);

}  // namespace tsc::tsoptions::tsoptionstest
