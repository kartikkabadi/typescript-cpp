// autoimporttestutil.h — port of tsc/internal/testutil/autoimporttestutil/
// fixtures.go: builders and handles for auto-import lifecycle fixtures over
// a MapFS-backed project session.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/project.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"

namespace tsc::testutil::autoimporttestutil {

// FileHandle — fixtures.go:18.
struct FileHandle {
	std::string fileName;
	std::string content;

	std::string FileName() const { return fileName; }
	std::string Content() const { return content; }
	lsproto::DocumentUri URI() const;
};

// ProjectFileHandle — fixtures.go:26.
struct ProjectFileHandle : FileHandle {
	std::string exportIdentifier;
};

// NodeModulesPackageHandle — fixtures.go:32.
struct NodeModulesPackageHandle {
	std::string Name;
	std::string Directory;
	FileHandle packageJSON;
	FileHandle declaration;

	FileHandle PackageJSONFile() const { return packageJSON; }
	FileHandle DeclarationFile() const { return declaration; }
};

// ProjectHandle — fixtures.go:63.
struct ProjectHandle {
	std::string root;
	std::vector<ProjectFileHandle> files;
	FileHandle tsconfig;
	FileHandle packageJSON;
	std::vector<NodeModulesPackageHandle> nodeModules;
	std::vector<std::string> dependencies;

	std::string Root() const { return root; }
	std::vector<ProjectFileHandle> Files() const { return files; }
	ProjectFileHandle File(int index) const;
	FileHandle TSConfig() const { return tsconfig; }
	FileHandle PackageJSONFile() const { return packageJSON; }
	std::vector<NodeModulesPackageHandle> NodeModules() const {
		return nodeModules;
	}
	std::vector<std::string> Dependencies() const { return dependencies; }
	const NodeModulesPackageHandle*
	NodeModuleByName(const std::string& name) const;
};

// MonorepoHandle — fixtures.go:42.
struct MonorepoHandle {
	std::string root;
	std::vector<NodeModulesPackageHandle> rootNodeModules;
	std::vector<std::string> rootDependencies;
	std::vector<ProjectHandle> packages;
	FileHandle rootTSConfig;
	FileHandle rootPackageJSON;

	std::string Root() const { return root; }
	std::vector<NodeModulesPackageHandle> RootNodeModules() const {
		return rootNodeModules;
	}
	std::vector<std::string> RootDependencies() const {
		return rootDependencies;
	}
	std::vector<ProjectHandle> Packages() const { return packages; }
	ProjectHandle Package(int index) const;
	FileHandle RootTSConfig() const { return rootTSConfig; }
	FileHandle RootPackageJSONFile() const { return rootPackageJSON; }
};

// Fixture — fixtures.go:104.
struct Fixture {
	project::Session* session = nullptr;
	std::shared_ptr<projecttestutil::SessionUtils> utils;
	std::vector<ProjectHandle> projects;

	project::Session* Session() { return session; }
	std::shared_ptr<projecttestutil::SessionUtils> Utils() {
		return utils;
	}
	std::vector<ProjectHandle> Projects() const { return projects; }
	ProjectHandle Project(int index) const;
	ProjectHandle SingleProject() const { return Project(0); }
};

// MonorepoFixture — fixtures.go:123.
struct MonorepoFixture {
	project::Session* session = nullptr;
	std::shared_ptr<projecttestutil::SessionUtils> utils;
	MonorepoHandle monorepo;
	std::vector<FileHandle> extra;

	project::Session* Session() { return session; }
	std::shared_ptr<projecttestutil::SessionUtils> Utils() {
		return utils;
	}
	MonorepoHandle Monorepo() const { return monorepo; }
	std::vector<FileHandle> ExtraFiles() const { return extra; }
	FileHandle ExtraFile(const std::string& path) const;
};

// MonorepoPackageTemplate — fixtures.go:144.
struct MonorepoPackageTemplate {
	std::string Name;
	std::vector<std::string> NodeModuleNames;
	std::vector<std::string> DependencyNames;
};

// MonorepoPackageConfig — fixtures.go:161.
struct MonorepoPackageConfig {
	int FileCount = 0;
	MonorepoPackageTemplate Template; // embedded MonorepoPackageTemplate
};

// TextFileSpec — fixtures.go:165.
struct TextFileSpec {
	std::string Path;
	std::string Content;
};

// SymlinkSpec — fixtures.go:170.
struct SymlinkSpec {
	std::string Link;   // The symlink path
	std::string Target; // The target path the symlink points to
};

// MonorepoSetupConfig — fixtures.go:154.
struct MonorepoSetupConfig {
	std::string Root;
	MonorepoPackageTemplate Template; // embedded MonorepoPackageTemplate
	std::vector<MonorepoPackageConfig> Packages;
	std::vector<TextFileSpec> ExtraFiles;
	std::vector<SymlinkSpec> Symlinks;
};

// SetupMonorepoLifecycleSession — fixtures.go:191.
std::shared_ptr<MonorepoFixture>
SetupMonorepoLifecycleSession(gostd::testing::T* t,
                              const MonorepoSetupConfig& config);

// SetupLifecycleSession — fixtures.go:254.
std::shared_ptr<Fixture> SetupLifecycleSession(gostd::testing::T* t,
                                               const std::string& projectRoot,
                                               int fileCount);

// normalizeAbsolutePath — fixtures.go:604.
std::string normalizeAbsolutePath(const std::string& path);

}  // namespace tsc::testutil::autoimporttestutil
