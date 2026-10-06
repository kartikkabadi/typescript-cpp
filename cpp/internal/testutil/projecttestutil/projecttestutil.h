// projecttestutil.h — port of tsc/internal/testutil/projecttestutil/
// projecttestutil.go: session test helpers — a SessionUtils bag plus the
// Setup* family that builds a project.Session over a MapFS.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/project/project.h"
#include "internal/project/logging/logging.h"
#include "internal/testutil/projecttestutil/clientmock_generated.h"
#include "internal/testutil/projecttestutil/npmexecutormock_generated.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/iovfs/iovfs.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::testutil::projecttestutil {

namespace iovfs = ::tsc::vfs::iovfs;
namespace vfstest = ::tsc::vfs::vfstest;

// TestTypingsLocation — projecttestutil.go:29.
inline constexpr std::string_view TestTypingsLocation =
	"/home/src/Library/Caches/typescript";

// TypingsInstallerOptions — projecttestutil.go:32.
struct TypingsInstallerOptions {
	std::vector<std::string> TypesRegistry;
	std::unordered_map<std::string, std::string> PackageToFile;
};

// SessionUtils — projecttestutil.go:37.
struct SessionUtils {
	std::string currentDirectory;
	std::shared_ptr<iovfs::FsWithSys> fsFromFileMap;
	std::shared_ptr<vfs::FS> fs;
	std::shared_ptr<ClientMock> client;
	std::shared_ptr<NpmExecutorMock> npmExecutor;
	std::shared_ptr<TypingsInstallerOptions> tiOptions;
	std::shared_ptr<logging::LogCollector> logger;

	// FsFromFileMap — projecttestutil.go:47.
	std::shared_ptr<iovfs::FsWithSys> FsFromFileMap() { return fsFromFileMap; }
	// Client — projecttestutil.go:51.
	std::shared_ptr<ClientMock> Client() { return client; }
	// NpmExecutor — projecttestutil.go:55.
	std::shared_ptr<NpmExecutorMock> NpmExecutor() { return npmExecutor; }
	// SetupNpmExecutorForTypingsInstaller — projecttestutil.go:59.
	void SetupNpmExecutorForTypingsInstaller();
	// ToPath — projecttestutil.go:109.
	tspath::Path ToPath(const std::string& fileName);
	// FS — projecttestutil.go:113.
	std::shared_ptr<vfs::FS> FS() { return fs; }
	// WatchesFile — projecttestutil.go:121.
	bool WatchesFile(const std::string& filePath);
	// Logs — projecttestutil.go:147.
	std::string Logs() { return logger->String(); }
	// BaselineLogs — projecttestutil.go:151.
	void BaselineLogs(gostd::testing::T* t);

private:
	// createTypesRegistryFileContent — projecttestutil.go:199.
	std::string createTypesRegistryFileContent();
	// appendTypesRegistryConfig — projecttestutil.go:216.
	void appendTypesRegistryConfig(std::string* builder, int index,
	                               const std::string& entry);
};

// TypesRegistryConfigText — projecttestutil.go:162.
std::string TypesRegistryConfigText();
// TypesRegistryConfig — projecttestutil.go:182 (Go map; iteration order
// is unspecified like Go's).
const std::unordered_map<std::string, std::string>& TypesRegistryConfig();

// The `files` argument is Go's map[string]any input to vfstest.FromMap;
// its values are vfstest::MapFileInput (string, []byte or *fstest.MapFile).
using FileMap = std::unordered_map<std::string, vfstest::MapFileInput>;

// Setup — projecttestutil.go:223.
std::pair<project::Session*, std::shared_ptr<SessionUtils>>
Setup(const FileMap& files);
// SetupWithRealFS — projecttestutil.go:227.
std::pair<project::Session*, std::shared_ptr<SessionUtils>>
SetupWithRealFS();
// SetupWithOptions — projecttestutil.go:261.
std::pair<project::Session*, std::shared_ptr<SessionUtils>>
SetupWithOptions(const FileMap& files, project::SessionOptions* options);
// SetupWithTypingsInstaller — projecttestutil.go:265.
std::pair<project::Session*, std::shared_ptr<SessionUtils>>
SetupWithTypingsInstaller(const FileMap& files,
                          std::shared_ptr<TypingsInstallerOptions> tiOptions);
// SetupWithOptionsAndTypingsInstaller — projecttestutil.go:269.
std::pair<project::Session*, std::shared_ptr<SessionUtils>>
SetupWithOptionsAndTypingsInstaller(
    const FileMap& files, project::SessionOptions* options,
    std::shared_ptr<TypingsInstallerOptions> tiOptions);
// WithRequestID — projecttestutil.go:276.
gostd::Context WithRequestID(const gostd::Context& ctx);
// GetSessionInitOptions — projecttestutil.go:280.
std::pair<std::unique_ptr<project::SessionInit>,
          std::shared_ptr<SessionUtils>>
GetSessionInitOptions(const FileMap& files, project::SessionOptions* options,
                      std::shared_ptr<TypingsInstallerOptions> tiOptions);

}  // namespace tsc::testutil::projecttestutil
