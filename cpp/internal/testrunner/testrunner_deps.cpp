// Dep-impls for the testrunner slice — functions declared in dep headers
// that were on the ported code's unconditional paths. The harnessutil,
// baseline, and tsbaseline dep-stubs/dep-impls that used to live here are
// now real: they moved to cpp/internal/testutil/ with the testutil slice.
// What remains is the tsoptions::tsoptionstest VFS parse-config host.
#include <unordered_map>

#include "internal/tsoptions/tsoptionstest/tsoptionstest.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::tsoptions::tsoptionstest {

// fixRoot — vfsparseconfighost.go:9. Unused by the ported functions but
// ported for parity with the Go file.
[[maybe_unused]] static std::string fixRoot(std::string_view path) {
	int rootLength = tspath::getRootLength(path);
	if (rootLength == 0) {
		return std::string(path);
	}
	if ((int)path.size() == rootLength) {
		return ".";
	}
	return std::string(path.substr(rootLength));
}

// NewVFSParseConfigHost — vfsparseconfighost.go:36.
std::unique_ptr<VfsParseConfigHost> NewVFSParseConfigHost(
    const std::unordered_map<std::string, std::string>& files,
    const std::string& currentDirectory, bool useCaseSensitiveFileNames) {
	std::unordered_map<std::string, vfs::vfstest::MapFileInput> entries;
	entries.reserve(files.size());
	for (auto& [name, content] : files) {
		entries.emplace(name, content);
	}
	auto* host = new VfsParseConfigHost{};
	host->Vfs = vfs::vfstest::FromMap(entries, useCaseSensitiveFileNames);
	host->CurrentDirectory = currentDirectory;
	return std::unique_ptr<VfsParseConfigHost>(host);
}

// NewVFSParseConfigHostWithSymlinks — vfsparseconfighost.go:46. Builds a
// parse-config host whose vfs also contains the given symlinks
// (link path -> target path), so config parsing resolves packages through
// symlinks as it would on disk.
std::unique_ptr<VfsParseConfigHost> NewVFSParseConfigHostWithSymlinks(
    const std::unordered_map<std::string, std::string>& files,
    const std::unordered_map<std::string, std::string>& symlinks,
    const std::string& currentDirectory, bool useCaseSensitiveFileNames) {
	if (symlinks.empty()) {
		return NewVFSParseConfigHost(files, currentDirectory,
		                             useCaseSensitiveFileNames);
	}
	std::unordered_map<std::string, vfs::vfstest::MapFileInput> entries;
	entries.reserve(files.size() + symlinks.size());
	for (auto& [name, content] : files) {
		entries.emplace(name, content);
	}
	for (auto& [link, target] : symlinks) {
		entries[tspath::getNormalizedAbsolutePath(link,
		                                         currentDirectory)] =
		    vfs::vfstest::Symlink(tspath::getNormalizedAbsolutePath(
		        target, currentDirectory));
	}
	auto* host = new VfsParseConfigHost{};
	host->Vfs = vfs::vfstest::FromMap(entries, useCaseSensitiveFileNames);
	host->CurrentDirectory = currentDirectory;
	return std::unique_ptr<VfsParseConfigHost>(host);
}

// GetParsedCommandLine — parsedcommandline.go:9.
tsoptions::ParsedCommandLine* GetParsedCommandLine(
    std::string_view jsonText,
    const std::unordered_map<std::string, std::string>& files,
    const std::string& currentDirectory, bool useCaseSensitiveFileNames) {
	auto host = NewVFSParseConfigHost(files, currentDirectory,
	                                  useCaseSensitiveFileNames);
	std::string configFileName =
	    tspath::combinePaths(currentDirectory, {"tsconfig.json"});
	auto* tsconfigSourceFile = tsoptions::NewTsconfigSourceFileFromFilePath(
	    configFileName,
	    tspath::toPath(configFileName, currentDirectory,
	                   useCaseSensitiveFileNames),
	    jsonText);
	return tsoptions::ParseJsonSourceFileConfigFileContent(
	    tsconfigSourceFile, host.get(), currentDirectory, nullptr, {},
	    configFileName, {}, nullptr);
}

}  // namespace tsc::tsoptions::tsoptionstest
