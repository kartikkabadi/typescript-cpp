// autoimporttestutil.cpp — port of tsc/internal/testutil/autoimporttestutil/
// fixtures.go.
#include "internal/testutil/autoimporttestutil/autoimporttestutil.h"

#include <algorithm>
#include <utility>

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/gostd/gostd.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/stringutil/stringutil.h" // decodeUtf8Rune/encodeUtf8Rune
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::testutil::autoimporttestutil {

namespace vfstest = ::tsc::vfs::vfstest;

// FileHandle::URI — fixtures.go:23.
lsproto::DocumentUri FileHandle::URI() const {
	return lsconv::FileNameToDocumentURI(fileName);
}

namespace {

[[noreturn]] void panicf(const std::string& msg) {
	// panic(msg) — the panic value is the message string.
	TSC_UNREACHABLE(msg.c_str());
}

}  // namespace

// ProjectHandle::File — fixtures.go:74.
ProjectFileHandle ProjectHandle::File(int index) const {
	if (index < 0 || index >= (int)files.size()) {
		panicf(gostd::sprintf("file index %d out of range", {index}));
	}
	return files[index];
}

// ProjectHandle::NodeModuleByName — fixtures.go:88.
const NodeModulesPackageHandle*
ProjectHandle::NodeModuleByName(const std::string& name) const {
	for (size_t i = 0; i < nodeModules.size(); i++) {
		if (nodeModules[i].Name == name) {
			return &nodeModules[i];
		}
	}
	return nullptr;
}

// MonorepoHandle::Package — fixtures.go:55.
ProjectHandle MonorepoHandle::Package(int index) const {
	if (index < 0 || index >= (int)packages.size()) {
		panicf(gostd::sprintf("package index %d out of range", {index}));
	}
	return packages[index];
}

// Fixture::Project — fixtures.go:111.
ProjectHandle Fixture::Project(int index) const {
	if (index < 0 || index >= (int)projects.size()) {
		panicf(gostd::sprintf("project index %d out of range", {index}));
	}
	return projects[index];
}

// MonorepoFixture::ExtraFile — fixtures.go:133.
FileHandle MonorepoFixture::ExtraFile(const std::string& path) const {
	std::string normalized = normalizeAbsolutePath(path);
	for (const auto& handle : extra) {
		if (handle.fileName == normalized) {
			return handle;
		}
	}
	panicf("extra file not found: " + path);
}

namespace {

// Forward decls — defined below (fixtures.go order).
std::vector<std::string>
packageNames(const std::vector<NodeModulesPackageHandle>& deps);
std::string sanitizeIdentifier(const std::string& name);

// projectFile — fixtures.go:327.
struct projectFile {
	std::string FileName;
	std::string ExportIdentifier;
	std::string Content;
};

// projectRecord — fixtures.go:320.
struct projectRecord {
	std::string root;
	std::vector<projectFile> sourceFiles;
	FileHandle tsconfig;
	std::shared_ptr<FileHandle> packageJSON;
	std::vector<NodeModulesPackageHandle> nodeModules;
	std::vector<std::string> dependencies;

	// toHandles — fixtures.go:366.
	ProjectHandle toHandles() const {
		std::vector<ProjectFileHandle> files_(sourceFiles.size());
		for (size_t i = 0; i < sourceFiles.size(); i++) {
			const projectFile& file = sourceFiles[i];
			ProjectFileHandle h;
			h.fileName = file.FileName;
			h.content = file.Content;
			h.exportIdentifier = file.ExportIdentifier;
			files_[i] = std::move(h);
		}
		FileHandle packageJSON_;
		if (packageJSON) {
			packageJSON_ = *packageJSON;
		}
		ProjectHandle out;
		out.root = root;
		out.files = std::move(files_);
		out.tsconfig = tsconfig;
		out.packageJSON = packageJSON_;
		out.nodeModules = nodeModules;
		out.dependencies = dependencies;
		return out;
	}
};

// fileMapBuilder — fixtures.go:305.
struct fileMapBuilder {
	std::unordered_map<std::string, vfstest::MapFileInput> files;
	int nextPackageID = 0;
	int nextProjectID = 0;
	std::unordered_map<std::string, std::shared_ptr<projectRecord>>
	    projects;

	void ensureFiles() {}
	// projectHandles — fixtures.go:355.
	std::vector<ProjectHandle> projectHandles() {
		std::vector<std::string> keys;
		keys.reserve(projects.size());
		for (const auto& [key, _] : projects) {
			keys.push_back(key);
		}
		std::sort(keys.begin(), keys.end());
		std::vector<ProjectHandle> result;
		result.reserve(keys.size());
		for (const auto& key : keys) {
			result.push_back(projects[key]->toHandles());
		}
		return result;
	}
	// ensureProjectRecord — fixtures.go:345.
	projectRecord* ensureProjectRecord(const std::string& root) {
		auto it = projects.find(root);
		if (it != projects.end()) {
			return it->second.get();
		}
		auto record = std::make_shared<projectRecord>();
		record->root = root;
		projects[root] = record;
		return record.get();
	}
	// Files — fixtures.go:384 (maps.Clone).
	std::unordered_map<std::string, vfstest::MapFileInput> Files() {
		return files;
	}
	// AddTextFile — fixtures.go:388.
	void AddTextFile(const std::string& path, const std::string& contents) {
		files[normalizeAbsolutePath(path)] = contents;
	}
	// AddSymlink — fixtures.go:393.
	void AddSymlink(const std::string& linkPath,
	                const std::string& targetPath) {
		files[normalizeAbsolutePath(linkPath)] =
		    vfstest::Symlink(normalizeAbsolutePath(targetPath));
	}
	// AddNodeModulesPackages — fixtures.go:398.
	std::vector<NodeModulesPackageHandle>
	AddNodeModulesPackages(const std::string& nodeModulesDir, int count) {
		std::vector<NodeModulesPackageHandle> packages;
		packages.reserve(count);
		for (int i = 0; i < count; i++) {
			packages.push_back(AddNodeModulesPackage(nodeModulesDir));
		}
		return packages;
	}
	// AddNodeModulesPackagesWithNames — fixtures.go:407.
	std::vector<NodeModulesPackageHandle> AddNodeModulesPackagesWithNames(
	    const std::string& nodeModulesDir,
	    const std::vector<std::string>& names) {
		if (names.empty()) {
			return {};
		}
		std::vector<NodeModulesPackageHandle> packages;
		packages.reserve(names.size());
		for (const auto& name : names) {
			packages.push_back(
			    AddNamedNodeModulesPackage(nodeModulesDir, name));
		}
		return packages;
	}
	// AddNodeModulesPackage — fixtures.go:418.
	NodeModulesPackageHandle
	AddNodeModulesPackage(const std::string& nodeModulesDir) {
		return AddNamedNodeModulesPackage(nodeModulesDir, "");
	}
	// AddNamedNodeModulesPackage — fixtures.go:422.
	NodeModulesPackageHandle AddNamedNodeModulesPackage(
	    const std::string& nodeModulesDir, const std::string& name) {
		std::string normalizedDir = normalizeAbsolutePath(nodeModulesDir);
		if (tspath::getBaseFileName(normalizedDir) != "node_modules") {
			panicf("nodeModulesDir must point to a node_modules "
			       "directory: " +
			       nodeModulesDir);
		}
		nextPackageID++;
		std::string resolvedName = name;
		if (resolvedName.empty()) {
			resolvedName =
			    gostd::sprintf("pkg%d", {nextPackageID});
		}
		std::string exportName =
		    sanitizeIdentifier(resolvedName) + "_value";
		std::string pkgDir =
		    tspath::combinePaths(normalizedDir, {resolvedName});
		std::string packageJSONPath =
		    tspath::combinePaths(pkgDir, {"package.json"});
		std::string packageJSONContent = gostd::sprintf(
		    "{\"name\":\"%s\",\"types\":\"index.d.ts\"}",
		    {resolvedName});
		files[packageJSONPath] = packageJSONContent;
		std::string declarationPath =
		    tspath::combinePaths(pkgDir, {"index.d.ts"});
		std::string declarationContent = gostd::sprintf(
		    "export declare const %s: number;\n", {exportName});
		files[declarationPath] = declarationContent;
		NodeModulesPackageHandle packageHandle;
		packageHandle.Name = resolvedName;
		packageHandle.Directory = pkgDir;
		packageHandle.packageJSON = FileHandle{packageJSONPath,
		                                       packageJSONContent};
		packageHandle.declaration =
		    FileHandle{declarationPath, declarationContent};
		std::string projectRoot =
		    tspath::getDirectoryPath(normalizedDir);
		projectRecord* record = ensureProjectRecord(projectRoot);
		record->nodeModules.push_back(packageHandle);
		return packageHandle;
	}
	// AddLocalProject — fixtures.go:455.
	void AddLocalProject(const std::string& projectDir, int fileCount) {
		if (fileCount < 0) {
			panicf("fileCount must be non-negative");
		}
		std::string dir = normalizeAbsolutePath(projectDir);
		projectRecord* record = ensureProjectRecord(dir);
		nextProjectID++;
		std::string tsConfigPath =
		    tspath::combinePaths(dir, {"tsconfig.json"});
		std::string tsConfigContent =
		    "{\n  \"compilerOptions\": {\n    \"module\": \"esnext\","
		    "\n    \"target\": \"esnext\",\n    \"strict\": true,"
		    "\n    \"allowJs\": true,\n    \"checkJs\": true\n  }\n}\n";
		files[tsConfigPath] = tsConfigContent;
		record->tsconfig =
		    FileHandle{tsConfigPath, tsConfigContent};
		for (int i = 1; i <= fileCount; i++) {
			std::string path = tspath::combinePaths(
			    dir, {gostd::sprintf("file%d.ts", {i})});
			std::string exportName = gostd::sprintf(
			    "localExport%d_%d", {nextProjectID, i});
			std::string content = gostd::sprintf(
			    "export const %s = %d;\n", {exportName, i});
			files[path] = content;
			record->sourceFiles.push_back(projectFile{
			    .FileName = path,
			    .ExportIdentifier = exportName,
			    .Content = content});
		}
	}
	// AddPackageJSONWithDependencies — fixtures.go:478.
	FileHandle AddPackageJSONWithDependencies(
	    const std::string& projectDir,
	    const std::vector<NodeModulesPackageHandle>& deps) {
		nextProjectID++;
		return AddPackageJSONWithDependenciesNamed(
		    projectDir,
		    gostd::sprintf("local-project-%d", {nextProjectID}),
		    deps);
	}
	// AddPackageJSONWithDependenciesNamed — fixtures.go:483.
	FileHandle AddPackageJSONWithDependenciesNamed(
	    const std::string& projectDir, const std::string& packageName,
	    const std::vector<NodeModulesPackageHandle>& deps) {
		std::string dir = normalizeAbsolutePath(projectDir);
		std::string packageJSONPath =
		    tspath::combinePaths(dir, {"package.json"});
		std::vector<std::string> dependencyLines;
		dependencyLines.reserve(deps.size());
		for (const auto& dep : deps) {
			dependencyLines.push_back(
			    gostd::sprintf("\"%s\": \"*\"", {dep.Name}));
		}
		std::string builder;
		std::string name = packageName;
		if (name.empty()) {
			nextProjectID++;
			name = gostd::sprintf("local-project-%d",
			                      {nextProjectID});
		}
		builder +=
		    gostd::sprintf("{\n  \"name\": \"%s\"", {name});
		if (!dependencyLines.empty()) {
			builder += ",\n  \"dependencies\": {\n    ";
			// strings.Join(dependencyLines, ",\n    ")
			for (size_t i = 0; i < dependencyLines.size(); i++) {
				if (i) builder += ",\n    ";
				builder += dependencyLines[i];
			}
			builder += "\n  }\n";
		} else {
			builder += "\n";
		}
		builder += "}\n";
		std::string content = builder;
		files[packageJSONPath] = content;
		projectRecord* record = ensureProjectRecord(dir);
		auto packageHandle = std::make_shared<FileHandle>(
		    FileHandle{packageJSONPath, content});
		record->packageJSON = packageHandle;
		record->dependencies = packageNames(deps);
		return *packageHandle;
	}
	// addRootPackageJSON — fixtures.go:514.
	FileHandle addRootPackageJSON(
	    const std::string& rootDir, const std::string& packageName,
	    const std::vector<NodeModulesPackageHandle>& deps) {
		std::string dir = normalizeAbsolutePath(rootDir);
		std::string packageJSONPath =
		    tspath::combinePaths(dir, {"package.json"});
		std::vector<std::string> dependencyLines;
		dependencyLines.reserve(deps.size());
		for (const auto& dep : deps) {
			dependencyLines.push_back(
			    gostd::sprintf("\"%s\": \"*\"", {dep.Name}));
		}
		std::string builder;
		std::string pkgName = packageName;
		if (pkgName.empty()) {
			pkgName = "monorepo-root";
		}
		builder += gostd::sprintf(
		    "{\n  \"name\": \"%s\",\n  \"private\": true",
		    {pkgName});
		if (!dependencyLines.empty()) {
			builder += ",\n  \"dependencies\": {\n    ";
			for (size_t i = 0; i < dependencyLines.size(); i++) {
				if (i) builder += ",\n    ";
				builder += dependencyLines[i];
			}
			builder += "\n  }\n";
		} else {
			builder += "\n";
		}
		builder += "}\n";
		std::string content = builder;
		files[packageJSONPath] = content;
		return FileHandle{packageJSONPath, content};
	}
};

// newFileMapBuilder — fixtures.go:330.
std::unique_ptr<fileMapBuilder>
newFileMapBuilder(const std::unordered_map<
                  std::string, vfstest::MapFileInput>& initial) {
	auto b = std::make_unique<fileMapBuilder>();
	if (initial.empty()) {
		return b;
	}
	for (const auto& [path, content] : initial) {
		b->files[normalizeAbsolutePath(path)] = content;
	}
	return b;
}

// selectPackagesByName — fixtures.go:549.
std::vector<NodeModulesPackageHandle>
selectPackagesByName(const std::vector<NodeModulesPackageHandle>& available,
                     const std::vector<std::string>& names) {
	if (names.empty()) {
		return available;
	}
	std::vector<NodeModulesPackageHandle> result;
	result.reserve(names.size());
	for (const auto& name : names) {
		bool found = false;
		for (const auto& candidate : available) {
			if (candidate.Name == name) {
				result.push_back(candidate);
				found = true;
				break;
			}
		}
		if (!found) {
			panicf("dependency not found: " + name);
		}
	}
	return result;
}

// packageNames — fixtures.go:566.
std::vector<std::string>
packageNames(const std::vector<NodeModulesPackageHandle>& deps) {
	if (deps.empty()) {
		return {};
	}
	std::vector<std::string> names;
	names.reserve(deps.size());
	for (const auto& dep : deps) {
		names.push_back(dep.Name);
	}
	return names;
}

// sanitizeIdentifier — fixtures.go:578 (strings.Map over runes; a rune
// mapped to -1 is dropped).
std::string sanitizeIdentifier(const std::string& name) {
	std::string sanitized;
	for (size_t i = 0; i < name.size();) {
		int width = 0;
		char32_t r = tsc::decodeUtf8Rune(
		    std::string_view(name).substr(i), &width);
		char32_t mapped = 0;
		bool drop = false;
		if ((r >= U'a' && r <= U'z') || (r >= U'A' && r <= U'Z') ||
		    (r >= U'0' && r <= U'9')) {
			mapped = r;
		} else if (r == U'_' || r == U'-') {
			mapped = U'_';
		} else {
			drop = true;
		}
		if (!drop) {
			char buf[8];
			int n = tsc::encodeUtf8Rune(mapped, buf);
			sanitized.append(buf, n);
		}
		i += width;
	}
	if (sanitized.empty()) {
		return "pkg";
	}
	return sanitized;
}

}  // namespace

// SetupMonorepoLifecycleSession — fixtures.go:191.
std::shared_ptr<MonorepoFixture>
SetupMonorepoLifecycleSession(gostd::testing::T* t,
                              const MonorepoSetupConfig& config) {
	t->Helper();
	auto builder = newFileMapBuilder({});

	std::string monorepoRoot = normalizeAbsolutePath(config.Root);
	std::string monorepoName = config.Template.Name;
	if (monorepoName.empty()) {
		monorepoName = "monorepo";
	}

	// Add root tsconfig.json
	std::string rootTSConfigPath =
	    tspath::combinePaths(monorepoRoot, {"tsconfig.json"});
	std::string rootTSConfigContent =
	    "{\n  \"compilerOptions\": {\n    \"module\": \"esnext\",\n    "
	    "\"target\": \"esnext\",\n    \"strict\": true,\n    \"baseUrl\": "
	    "\".\",\n    \"allowJs\": true,\n    \"checkJs\": true\n  }\n}\n";
	builder->AddTextFile(rootTSConfigPath, rootTSConfigContent);
	FileHandle rootTSConfig{rootTSConfigPath, rootTSConfigContent};

	// Add root node_modules
	std::string rootNodeModulesDir =
	    tspath::combinePaths(monorepoRoot, {"node_modules"});
	auto rootNodeModules = builder->AddNodeModulesPackagesWithNames(
	    rootNodeModulesDir, config.Template.NodeModuleNames);

	// Add root package.json with dependencies (default to all root
	// node_modules if unspecified)
	auto rootDependencies = selectPackagesByName(
	    rootNodeModules, config.Template.DependencyNames);
	FileHandle rootPackageJSON = builder->addRootPackageJSON(
	    monorepoRoot, monorepoName, rootDependencies);
	auto rootDependencyNames = packageNames(rootDependencies);

	// Build each package in packages/
	std::string packagesDir =
	    tspath::combinePaths(monorepoRoot, {"packages"});
	std::vector<ProjectHandle> packageHandles;
	packageHandles.reserve(config.Packages.size());
	for (const auto& pkg : config.Packages) {
		std::string pkgDir =
		    tspath::combinePaths(packagesDir, {pkg.Template.Name});
		builder->AddLocalProject(pkgDir, pkg.FileCount);

		std::vector<NodeModulesPackageHandle> pkgNodeModules;
		if (!pkg.Template.NodeModuleNames.empty()) {
			std::string pkgNodeModulesDir =
			    tspath::combinePaths(pkgDir, {"node_modules"});
			pkgNodeModules = builder->AddNodeModulesPackagesWithNames(
			    pkgNodeModulesDir, pkg.Template.NodeModuleNames);
		}

		auto availableDeps = rootNodeModules;
		availableDeps.insert(availableDeps.end(),
		                     pkgNodeModules.begin(),
		                     pkgNodeModules.end());
		auto selectedDeps =
		    selectPackagesByName(availableDeps,
		                         pkg.Template.DependencyNames);
		if (!selectedDeps.empty()) {
			builder->AddPackageJSONWithDependenciesNamed(
			    pkgDir, pkg.Template.Name, selectedDeps);
		}
	}

	// Add arbitrary extra files
	std::vector<FileHandle> extraHandles;
	extraHandles.reserve(config.ExtraFiles.size());
	for (const auto& extra : config.ExtraFiles) {
		builder->AddTextFile(extra.Path, extra.Content);
		extraHandles.push_back(FileHandle{
		    normalizeAbsolutePath(extra.Path), extra.Content});
	}

	// Add symlinks
	for (const auto& symlink : config.Symlinks) {
		builder->AddSymlink(symlink.Link, symlink.Target);
	}

	// Build project handles after all packages are created
	for (const auto& pkg : config.Packages) {
		std::string pkgDir =
		    tspath::combinePaths(packagesDir, {pkg.Template.Name});
		auto it = builder->projects.find(pkgDir);
		if (it != builder->projects.end()) {
			packageHandles.push_back(it->second->toHandles());
		}
	}

	auto setupPair = projecttestutil::Setup(builder->Files());
	auto session = setupPair.first;
	auto sessionUtils = setupPair.second;
	t->Cleanup([session] { session->Close(); });

	// Build root node_modules handle by looking at the project record for
	// the workspace root (created as side effect of
	// AddNodeModulesPackages)
	std::vector<NodeModulesPackageHandle> rootNodeModulesHandles;
	auto rootIt = builder->projects.find(monorepoRoot);
	if (rootIt != builder->projects.end()) {
		rootNodeModulesHandles = rootIt->second->nodeModules;
	}

	auto fixture = std::make_shared<MonorepoFixture>();
	fixture->session = session;
	fixture->utils = sessionUtils;
	fixture->monorepo.root = monorepoRoot;
	fixture->monorepo.rootNodeModules = rootNodeModulesHandles;
	fixture->monorepo.rootDependencies = rootDependencyNames;
	fixture->monorepo.packages = packageHandles;
	fixture->monorepo.rootTSConfig = rootTSConfig;
	fixture->monorepo.rootPackageJSON = rootPackageJSON;
	fixture->extra = extraHandles;
	return fixture;
}

// SetupLifecycleSession — fixtures.go:254.
std::shared_ptr<Fixture> SetupLifecycleSession(
    gostd::testing::T* t, const std::string& projectRoot, int fileCount) {
	t->Helper();
	auto builder = newFileMapBuilder({});
	builder->AddLocalProject(projectRoot, fileCount);
	std::string nodeModulesDir =
	    tspath::combinePaths(projectRoot, {"node_modules"});
	auto deps = builder->AddNodeModulesPackages(nodeModulesDir, 1);
	builder->AddPackageJSONWithDependencies(projectRoot, deps);
	auto setupPair = projecttestutil::Setup(builder->Files());
	auto session = setupPair.first;
	auto sessionUtils = setupPair.second;
	t->Cleanup([session] { session->Close(); });
	auto fixture = std::make_shared<Fixture>();
	fixture->session = session;
	fixture->utils = sessionUtils;
	fixture->projects = builder->projectHandles();
	return fixture;
}

// normalizeAbsolutePath — fixtures.go:604.
std::string normalizeAbsolutePath(const std::string& path) {
	std::string normalized = tspath::normalizePath(path);
	if (!tspath::pathIsAbsolute(normalized)) {
		panicf("paths used in lifecycle tests must be absolute: " + path);
	}
	return normalized;
}

}  // namespace tsc::testutil::autoimporttestutil
