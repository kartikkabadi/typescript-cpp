// fs.cpp — port of tsctests/fs.go. testFs wraps the in-memory map fs:
// on read it swaps FakeTSVersion back to the real core.Version(), and on
// write it swaps it in + writes a <file>.readable.baseline.txt sibling.
#include <string>
#include <utility>

#include "internal/execute/tsctests/tsctests.h"

namespace tsc::execute::tsctests {

// testFs — fs.go:16. Embedded vfs.FS forwards every method unchanged
// except the ones overridden below (ReadFile/WriteFile/Remove call
// removeIgnoreLibPath first).
bool testFs::UseCaseSensitiveFileNames() {
	return FS->UseCaseSensitiveFileNames();
}
bool testFs::FileExists(const std::string& path) {
	return FS->FileExists(path);
}
std::pair<std::string, bool> testFs::ReadFile(const std::string& path) {
	// ReadFile — fs.go:30.
	removeIgnoreLibPath(path);
	return readFileHandlingBuildInfo(path);
}
vfs::Error testFs::WriteFile(const std::string& path,
                             const std::string& data) {
	// WriteFile — fs.go:53.
	removeIgnoreLibPath(path);
	writtenFiles.Add(path);
	return writeFileHandlingBuildInfo(path, data);
}
vfs::Error testFs::AppendFile(const std::string& path,
                              const std::string& data) {
	return FS->AppendFile(path, data);
}
vfs::Error testFs::Remove(const std::string& path) {
	// Remove — fs.go:87.
	removeIgnoreLibPath(path);
	return FS->Remove(path);
}
vfs::Error testFs::Chtimes(const std::string& path, vfs::TimePoint aTime,
                           vfs::TimePoint mTime) {
	return FS->Chtimes(path, aTime, mTime);
}
bool testFs::DirectoryExists(const std::string& path) {
	return FS->DirectoryExists(path);
}
vfs::Entries testFs::GetAccessibleEntries(const std::string& path) {
	return FS->GetAccessibleEntries(path);
}
std::shared_ptr<vfs::FileInfo> testFs::Stat(const std::string& path) {
	return FS->Stat(path);
}
std::string testFs::Realpath(const std::string& path) {
	return FS->Realpath(path);
}

// removeIgnoreLibPath — fs.go:22. Deleting a written lib path marks it
// real so GetModTime stops treating it as "always absent".
void testFs::removeIgnoreLibPath(const std::string& path) {
	if (defaultLibs && defaultLibs->Has(path)) {
		defaultLibs->Delete(path);
	}
}

// readFileHandlingBuildInfo — fs.go:35. On read, FakeTSVersion is swapped
// back to the actual version so buildinfo load paths see a real version.
std::pair<std::string, bool>
testFs::readFileHandlingBuildInfo(const std::string& path) {
	auto [contents, ok] = FS->ReadFile(path);
	if (ok && tspath::fileExtensionIs(path, tspath::extensionTsBuildInfo)) {
		// read buildinfo and modify version
		std::unique_ptr<incremental::BuildInfo> buildInfo(
		    incremental::unmarshalBuildInfo(contents));
		if (buildInfo && buildInfo->Version == testutil::harnessutil::FakeTSVersion) {
			buildInfo->Version = ::tsc::version();
			// json.Marshal is infallible for this structure.
			contents = incremental::marshalBuildInfo(buildInfo.get());
		}
	}
	return {std::move(contents), ok};
}

// writeFileHandlingBuildInfo — fs.go:59. On write of a .tsbuildinfo, swap
// the real version for FakeTSVersion (so baselines are stable) and emit a
// readable dump beside it.
vfs::Error testFs::writeFileHandlingBuildInfo(const std::string& path,
                                              const std::string& data_) {
	std::string data = data_;
	if (tspath::fileExtensionIs(path, tspath::extensionTsBuildInfo)) {
		std::unique_ptr<incremental::BuildInfo> buildInfo(
		    incremental::unmarshalBuildInfo(data));
		if (buildInfo) {
			if (buildInfo->Version == ::tsc::version()) {
				// Change it to harnessutil.FakeTSVersion
				buildInfo->Version = std::string(testutil::harnessutil::FakeTSVersion);
				data = incremental::marshalBuildInfo(buildInfo.get());
			}
			// Write readable build info version
			if (auto err = WriteFile(
			        path + ".readable.baseline.txt",
			        toReadableBuildInfo(
			            buildInfo.get(),
			            testutil::fsbaselineutil::SanitizeInternalSymbolName(data)));
			    err) {
				// Go: fmt.Errorf("...: %w", err)
				return vfs::Error::wrap(
				    "testFs.WriteFile: failed to write readable "
				    "build info",
				    err);
			}
		} else {
			// Go panics with the unmarshal error appended; our parser
			// reports failure as nullptr (the input itself is the clue).
			TSC_UNREACHABLE((
			    std::string("testFs.WriteFile: failed to unmarshal "
			                "build info: - use underlying FS's write "
			                "method if this is intended use for "
			                "testcase: ") +
			    path)
			    .c_str());
		}
	}
	return FS->WriteFile(path, data);
}

}  // namespace tsc::execute::tsctests
