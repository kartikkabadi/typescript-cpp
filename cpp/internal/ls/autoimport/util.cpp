// util.go — module-ID helpers, word splitting, checker pool, resolution host.
#include <algorithm>
#include <thread>
#include <unordered_map>

#include "internal/ls/autoimport/autoimport.h"
#include "internal/module/util.h"
#include "internal/stringutil/stringutil.h"
#include "internal/vfs/wrapvfs/wrapvfs.h"

namespace tsc::ls::autoimport {
namespace {

// unicode.IsUpper/IsLower — approximated over the JS special-casing table:
// a rune is "upper" when it has a simple uppercase mapping equal to itself
// and a distinct lowercase mapping (and vice versa).
bool isUpperRune(char32_t r) {
	char32_t up = stringutil::toUpperRune(r);
	char32_t lo = stringutil::toLowerRune(r);
	return up == r && lo != r;
}

bool isLowerRune(char32_t r) {
	char32_t up = stringutil::toUpperRune(r);
	char32_t lo = stringutil::toLowerRune(r);
	return lo == r && up != r;
}

// utf8.DecodeLastRuneInString.
char32_t decodeLastRuneInString(std::string_view s, int* width) {
	if (s.empty()) {
		*width = 0;
		return kRuneError;
	}
	size_t end = s.size() - 1;
	size_t start = end;
	while (start > 0 &&
	       (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80 &&
	       end - start < 3) {
		start--;
	}
	int w = 0;
	char32_t r = decodeUtf8Rune(s.substr(start), &w);
	if (static_cast<size_t>(w) != end - start + 1) {
		*width = 1;
		return kRuneError;
	}
	*width = w;
	return r;
}

}  // namespace

// tryGetModuleIDAndFileNameOfModuleSymbol — util.go:24
std::optional<std::pair<ModuleID, std::string>>
tryGetModuleIDAndFileNameOfModuleSymbol(Symbol* symbol) {
	if (!symbol->isExternalModule()) {
		return std::nullopt;
	}
	Node* decl = getNonAugmentationDeclaration(symbol);
	if (decl == nullptr) {
		return std::nullopt;
	}
	if (decl->kind == Kind::SourceFile) {
		SourceFile* sf = decl->as<SourceFile>();
		return std::make_pair(ModuleID(sf->Path()), sf->FileName());
	}
	if (isModuleWithStringLiteralName(decl)) {
		return std::make_pair(ModuleID(decl->name()->text()), "");
	}
	return std::nullopt;
}

// getModuleIDAndFileNameOfModuleSymbol — util.go:41
std::pair<ModuleID, std::string> getModuleIDAndFileNameOfModuleSymbol(
    Symbol* symbol) {
	if (!symbol->isExternalModule()) {
		TSC_UNREACHABLE("symbol is not an external module");
	}
	Node* decl = getNonAugmentationDeclaration(symbol);
	if (decl == nullptr) {
		TSC_UNREACHABLE("module symbol has no non-augmentation declaration");
	}
	if (decl->kind == Kind::SourceFile) {
		SourceFile* sf = decl->as<SourceFile>();
		return {ModuleID(sf->Path()), sf->FileName()};
	}
	if (isModuleWithStringLiteralName(decl)) {
		return {ModuleID(decl->name()->text()), ""};
	}
	TSC_UNREACHABLE("could not determine module ID of module symbol");
}

// wordIndices — util.go:68. CamelCase / snake_case word-start byte indices.
std::vector<int> wordIndices(const std::string& s) {
	std::vector<int> indices;
	int w = 0;
	for (int byteIndex = 0, n = static_cast<int>(s.size()); byteIndex < n;) {
		char32_t runeValue =
		    decodeUtf8Rune(std::string_view(s).substr(byteIndex), &w);
		int cur = byteIndex;
		byteIndex += w;
		if (cur == 0) {
			indices.push_back(cur);
			continue;
		}
		if (runeValue == '_') {
			if (cur + 1 < n && s[cur + 1] != '_') {
				indices.push_back(cur + 1);
			}
			continue;
		}
		if (isUpperRune(runeValue)) {
			int lw = 0;
			char32_t prev = decodeLastRuneInString(
			    std::string_view(s).substr(0, cur), &lw);
			bool prevLower = isLowerRune(prev);
			bool nextLower = false;
			if (cur + 1 < n) {
				int nw = 0;
				char32_t next = decodeUtf8Rune(
				    std::string_view(s).substr(cur + 1), &nw);
				nextLower = isLowerRune(next);
			}
			if (prevLower || nextLower) {
				indices.push_back(cur);
			}
		}
	}
	return indices;
}

// getPackageNamesInNodeModules — util.go:88
std::unique_ptr<collections::Set<std::string>> getPackageNamesInNodeModules(
    const std::string& nodeModulesDir, vfs::FS* fs) {
	auto packageNames = std::make_unique<collections::Set<std::string>>();
	if (tspath::getBaseFileName(nodeModulesDir) != "node_modules") {
		TSC_UNREACHABLE("nodeModulesDir is not a node_modules directory");
	}
	// A missing node_modules directory yields no entries (GetAccessibleEntries returns
	// empty), so there's no need to check existence first: a deleted node_modules is
	// handled upstream in updateBucketAndDirectoryExistence, which drops the bucket.
	vfs::Entries entries = fs->GetAccessibleEntries(nodeModulesDir);
	for (const std::string& baseName : entries.directories) {
		if (baseName.empty() || baseName[0] == '.') {
			continue;
		}
		if (baseName[0] == '@') {
			std::string scopedDirPath =
			    tspath::combinePaths(nodeModulesDir, {baseName});
			for (const std::string& scopedPackageDirName :
			     fs->GetAccessibleEntries(scopedDirPath).directories) {
				std::string scopedBaseName =
				    std::string(tspath::getBaseFileName(scopedPackageDirName));
				if (baseName == "@types") {
					packageNames->Add(module::GetPackageNameFromTypesPackageName(
					    tspath::combinePaths("@types", {scopedBaseName})));
				} else {
					packageNames->Add(
					    tspath::combinePaths(baseName, {scopedBaseName}));
				}
			}
			continue;
		}
		packageNames->Add(baseName);
	}
	return packageNames;
}

// getDefaultLikeExportNameFromDeclaration — util.go:118
std::string getDefaultLikeExportNameFromDeclaration(Symbol* symbol) {
	for (Node* d : symbol->data->declarations) {
		// "export default" in this case. See `ExportAssignment`for more details.
		if (isExportAssignment(d)) {
			Node* innerExpression = skipOuterExpressions(d->expression(), OEKAll);
			if (isIdentifier(innerExpression)) {
				return innerExpression->text();
			}
			continue;
		}
		// "export { ~ as default }"
		if (isExportSpecifier(d) &&
		    (d->as<ExportSpecifier>()->Symbol != nullptr &&
		     (d->as<ExportSpecifier>()->Symbol->flags & SymbolFlagsAlias) != 0) &&
		    d->as<ExportSpecifier>()->PropertyName != nullptr) {
			if (d->as<ExportSpecifier>()->PropertyName->kind == Kind::Identifier) {
				return d->as<ExportSpecifier>()->PropertyName->text();
			}
			continue;
		}
		// GH#52694
		Node* name = getNameOfDeclaration(d);
		if (name != nullptr && name->kind == Kind::Identifier) {
			return name->text();
		}
		if (symbol->data->parent != nullptr &&
		    !checker::isExternalModuleSymbol(symbol->data->parent)) {
			return symbol->data->parent->data->name;
		}
	}
	return "";
}

// getResolvedPackageNames — util.go:145
std::unique_ptr<collections::Set<std::string>> getResolvedPackageNames(
    gostd::Context ctx, compiler::SimpleProgram* program) {
	collections::Set<std::string>* rawNames = program->ResolvedPackageNames();
	collections::Set<std::string>* unresolvedPackageNames =
	    program->UnresolvedPackageNames();

	// Normalize @types/ package names to their actual package names
	auto resolvedPackageNames = std::make_unique<collections::Set<std::string>>(
	    rawNames->Len());
	for (const std::string& name : rawNames->Keys()) {
		resolvedPackageNames->Add(
		    module::GetPackageNameFromTypesPackageName(name));
	}

	for (const std::string& name : program->Options()->Types) {
		if (name != "*") {
			resolvedPackageNames->Add(
			    module::GetPackageNameFromTypesPackageName(name));
		}
	}

	if (unresolvedPackageNames->Len() > 0) {
		auto [ch, done] = program->GetTypeChecker(ctx);
		struct DeferDone {
			std::function<void()> f;
			~DeferDone() { if (f) f(); }
		} _done{done};
		for (const std::string& name : unresolvedPackageNames->Keys()) {
			Symbol* symbol = ch->TryFindAmbientModule(name);
			if (symbol != nullptr) {
				SourceFile* declaringFile = getSourceFileOfModule(symbol);
				std::string packageName =
				    modulespecifiers::GetPackageNameFromDirectory(
				        declaringFile->FileName());
				if (!packageName.empty()) {
					resolvedPackageNames->Add(
					    module::GetPackageNameFromTypesPackageName(packageName));
				}
			}
		}
	}
	return resolvedPackageNames;
}

// addProjectReferenceOutputMappings — util.go:183
void addProjectReferenceOutputMappings(
    compiler::SimpleProgram* program,
    std::unordered_map<tspath::Path, std::string>& result) {
	for (tsoptions::ParsedCommandLine* ref :
	     program->GetResolvedProjectReferences()) {
		if (ref == nullptr) {
			continue;
		}
		ref->ParseInputOutputNames();
		for (const auto& kv : *ref->OutputDtsToProjectReference()) {
			// Only add if not already present (first program wins)
			if (result.find(kv.first) == result.end()) {
				result[kv.first] = kv.second->Source;
			}
		}
	}
}

// checkerPool — util.go:199 createCheckerPool. Go uses a buffered channel;
// C++ equivalent is a mutex + condvar guarded deque.
checkerPool::checkerPool(checker::Program* p)
    : program(p),
      maxSize(static_cast<int32_t>(std::max(
          1u, std::thread::hardware_concurrency()))) {}

std::pair<checker::Checker*, std::function<void()>> checkerPool::getChecker() {
	std::shared_ptr<checker::Checker> ch;
	{
		std::unique_lock<std::mutex> lk(mu);
		// Try to get an existing checker
		if (!pool.empty()) {
			ch = std::move(pool.front());
			pool.pop_front();
		} else {
			// Try to create a new one if under limit
			if (created.load() < maxSize) {
				created.fetch_add(1);
				lk.unlock();
				auto newChecker = std::make_shared<checker::Checker>();
				newChecker->init(program);
				ch = std::move(newChecker);
			} else {
				// At limit, wait for one to become available
				cv.wait(lk, [&] { return !pool.empty() || closed; });
				if (closed) {
					TSC_UNREACHABLE("checker pool closed");
				}
				ch = std::move(pool.front());
				pool.pop_front();
			}
		}
	}
	checker::Checker* raw = ch.get();
	auto done = [this, ch]() mutable {
		{
			std::lock_guard<std::mutex> lk(mu);
			pool.push_back(ch);
		}
		cv.notify_one();
	};
	return {raw, std::move(done)};
}

void checkerPool::closePool() {
	std::lock_guard<std::mutex> lk(mu);
	closed = true;
	cv.notify_all();
}

// addPackageJsonDependencies — util.go:234
void addPackageJsonDependencies(const packagejson::PackageJson& contents,
                                collections::Set<std::string>* deps) {
	contents.RangeDependencies(
	    [&](const std::string& name, const std::string&,
	        const std::string& field) -> bool {
		    if (name.empty() || name == "@types/" || name[0] == '.') {
			    // Edge cases that could make us blow up probably
			    return true;
		    }
		    if (field == "dependencies" || field == "peerDependencies") {
			    deps->Add(module::GetPackageNameFromTypesPackageName(name));
		    }
		    return true;
	    });
}

// getPackageRealpathFuncs — util.go:253
std::pair<std::function<std::string(const std::string&)>,
          std::function<std::string(const std::string&)>>
getPackageRealpathFuncs(vfs::FS* fs, const std::string& packageDir) {
	std::string realPackageDir = fs->Realpath(packageDir);
	bool isSymlinked = realPackageDir != packageDir;
	// Cache of package-directory-level symlink→realpath prefix mappings for
	// external packages encountered via re-exports. Keyed by the node_modules
	// package directory, so all files under that package reuse a single
	// realpath lookup.
	auto replacePrefix = [](const std::string& fileName,
	                        const std::string& prefix,
	                        const std::string& replacement) -> std::string {
		std::string_view relative = fileName;
		if (relative.substr(0, prefix.size()) == prefix) {
			relative = relative.substr(prefix.size());
		}
		while (!relative.empty() &&
		       (relative.front() == '/' || relative.front() == '\\')) {
			relative = relative.substr(1);
		}
		return tspath::combinePaths(replacement, {relative});
	};
	auto dirCache =
	    std::make_shared<std::unordered_map<std::string, std::string>>();
	std::function<std::string(const std::string&)> toRealpath =
	    [fs, packageDir, realPackageDir, isSymlinked, replacePrefix,
	     dirCache](const std::string& fileName) -> std::string {
		// Fast path: files within the package use prefix substitution.
		if (isSymlinked) {
			if (fileName.compare(0, packageDir.size(), packageDir) == 0) {
				std::string_view relative =
				    std::string_view(fileName)
				        .substr(packageDir.size());
				if (relative.empty() || relative.front() == '/' ||
				    relative.front() == '\\') {
					return replacePrefix(fileName, packageDir,
					                     realPackageDir);
				}
			}
		}
		// Files outside the package (e.g. re-exports into symlinked deps):
		// find the node_modules package directory, resolve it once, and cache.
		std::string filePackageDir =
		    module::NodeModulePackageRootForFile(fileName);
		if (filePackageDir.empty()) {
			return fileName;
		}
		// The wrapped FS also calls Realpath while traversing directories.
		// The two parses differ only when the path may be a package root,
		// so establish its kind before using the package cache.
		if (std::string directoryPackage =
		        module::NodeModulePackageRootForDirectory(fileName);
		    directoryPackage != filePackageDir) {
			if (fs->DirectoryExists(fileName)) {
				filePackageDir = directoryPackage;
			}
		}
		auto it = dirCache->find(filePackageDir);
		if (it != dirCache->end()) {
			const std::string& realDir = it->second;
			if (realDir == filePackageDir) {
				return fileName;
			}
			return realDir + fileName.substr(filePackageDir.size());
		}
		std::string realDir = fs->Realpath(filePackageDir);
		(*dirCache)[filePackageDir] = realDir;
		if (realDir == filePackageDir) {
			return fileName;
		}
		return realDir + fileName.substr(filePackageDir.size());
	};
	if (!isSymlinked) {
		return {toRealpath,
		        [](const std::string& s) -> std::string { return s; }};
	}
	// toSymlink only handles files within the package directory (reversing the
	// packageDir→realPackageDir substitution).
	std::function<std::string(const std::string&)> toSymlink =
	    [packageDir, realPackageDir](const std::string& fileName) -> std::string {
		if (fileName.compare(0, realPackageDir.size(), realPackageDir) == 0) {
			return packageDir + fileName.substr(realPackageDir.size());
		}
		return fileName;
	};
	return {toRealpath, toSymlink};
}

// resolutionHost — util.go:302
bool resolutionHost::FileExists(std::string_view path) {
	return fs->FileExists(std::string(path));
}

bool resolutionHost::DirectoryExists(std::string_view path) {
	return fs->DirectoryExists(std::string(path));
}

std::optional<std::string> resolutionHost::ReadFile(std::string_view path) {
	auto [content, ok] = fs->ReadFile(std::string(path));
	if (!ok) {
		return std::nullopt;
	}
	return content;
}

std::string resolutionHost::Realpath(std::string_view path) {
	return fs->Realpath(std::string(path));
}

std::string resolutionHost::GetCurrentDirectory() {
	return currentDirectory;
}

bool resolutionHost::UseCaseSensitiveFileNames() {
	return fs->UseCaseSensitiveFileNames();
}

module::ResolutionHost::AccessibleEntries resolutionHost::GetAccessibleEntries(
    std::string_view path) {
	vfs::Entries e = fs->GetAccessibleEntries(std::string(path));
	return {std::move(e.files), std::move(e.directories), std::move(e.symlinks)};
}

// getModuleResolver — util.go:317. The resolver keeps `rh` alive via
// DefaultResolver::host — leak the resolutionHost like Go's GC does.
module::DefaultResolver* getModuleResolver(
    RegistryCloneHost* host,
    const std::function<std::string(const std::string&)>& realpath,
    module::ResolverOptions opts) {
	auto* rh = new resolutionHost(
	    vfs::wrapvfs::Wrap(host->FS(),
	                       vfs::wrapvfs::Replacements{.Realpath = realpath}),
	    host->GetCurrentDirectory());
	opts.Host = rh;
	static const CompilerOptions emptyCompilerOptions;
	opts.CompilerOptions = &emptyCompilerOptions;
	return module::NewResolver(opts);
}

}  // namespace tsc::ls::autoimport
