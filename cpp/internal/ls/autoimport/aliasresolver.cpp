// aliasresolver.go — checker.Program adapter for the alias-resolution checker
// used when extracting exports. Most methods panic in Go → TSC_UNREACHABLE.
#include "internal/binder/binder.h"
#include "internal/ls/autoimport/autoimport.h"

namespace tsc::ls::autoimport {
namespace {

// Adapts module::ResolvedModule to checker::ResolvedModule (same conversion
// as SimpleProgram::GetResolvedModule, program.cpp:438).
checker::ResolvedModule toCheckerResolvedModule(
    const module::ResolvedModule& rm) {
	checker::ResolvedModule out;
	out.resolved = rm.IsResolved();
	out.resolvedFileName = rm.ResolvedFileName;
	out.resolvedUsingTsExtension = rm.ResolvedUsingTsExtension;
	out.isExternalLibraryImport = rm.IsExternalLibraryImport;
	out.extension = rm.Extension;
	out.alternateResult = rm.AlternateResult;
	out.packageId = checker::PackageId{rm.PackageId.Name};
	out.resolvedUsingExtraExtensions = rm.ResolvedUsingExtraExtensions;
	return out;
}

}  // namespace

// newAliasResolver — aliasresolver.go:33
std::unique_ptr<aliasResolver> newAliasResolver(
    const std::vector<SourceFile*>& rootFiles,
    std::unordered_map<tspath::Path, pathAndFileName> symlinks,
    RegistryCloneHost* host, module::DefaultResolver* moduleResolver,
    std::function<tspath::Path(const std::string&)> toPath,
    std::function<void(SourceFile*, const std::string&)>
        onFailedAmbientModuleLookup) {
	auto r = std::make_unique<aliasResolver>();
	r->toPath = std::move(toPath);
	r->host = host;
	r->moduleResolver = moduleResolver;
	r->rootFiles = rootFiles;
	r->symlinks = std::move(symlinks);
	r->onFailedAmbientModuleLookup =
	    std::move(onFailedAmbientModuleLookup);
	return r;
}

// BindSourceFiles — aliasresolver.go:53
void aliasResolver::BindSourceFiles() {
	// We will bind as we parse
}

// SourceFiles — aliasresolver.go:58
std::vector<SourceFile*> aliasResolver::SourceFiles() {
	return rootFiles;
}

// Options — aliasresolver.go:63
const CompilerOptions* aliasResolver::Options() {
	static const CompilerOptions opts = [] {
		CompilerOptions o;
		o.NoCheck = Tristate::True;
		return o;
	}();
	return &opts;
}

// GetCurrentDirectory — aliasresolver.go:70
std::string aliasResolver::GetCurrentDirectory() {
	return host->GetCurrentDirectory();
}

// UseCaseSensitiveFileNames — aliasresolver.go:75
bool aliasResolver::UseCaseSensitiveFileNames() {
	return host->FS()->UseCaseSensitiveFileNames();
}

// GetSourceFile — aliasresolver.go:80
SourceFile* aliasResolver::GetSourceFile(const std::string& fileName) {
	SourceFile* file = host->GetSourceFile(fileName, toPath(fileName));
	// file may be nil due to symlink/realpath mismatch; see TestAutoImportBuilderFS
	if (file == nullptr) {
		return nullptr;
	}
	bindSourceFile(file);
	return file;
}

// GetDefaultResolutionModeForFile — aliasresolver.go:91
ResolutionMode aliasResolver::GetDefaultResolutionModeForFile(SourceFile*) {
	return ModuleKind::ESNext;
}

// GetEmitModuleFormatOfFile — aliasresolver.go:96
ModuleKind aliasResolver::GetEmitModuleFormatOfFile(SourceFile*) {
	return ModuleKind::ESNext;
}

// GetEmitSyntaxForUsageLocation — aliasresolver.go:101
ResolutionMode aliasResolver::GetEmitSyntaxForUsageLocation(SourceFile*,
                                                          Node*) {
	return ModuleKind::ESNext;
}

// GetImpliedNodeFormatForEmit — aliasresolver.go:106
ModuleKind aliasResolver::GetImpliedNodeFormatForEmit(SourceFile*) {
	return ModuleKind::ESNext;
}

// GetModeForUsageLocation — aliasresolver.go:111
ResolutionMode aliasResolver::GetModeForUsageLocation(SourceFile*, Node*) {
	return ModuleKind::ESNext;
}

// GetResolvedModule — aliasresolver.go:116
std::optional<checker::ResolvedModule> aliasResolver::GetResolvedModule(
    SourceFile* currentSourceFile, const std::string& moduleReference,
    ResolutionMode mode) {
	auto [cache, _stored] = resolvedModules.LoadOrStore(
	    currentSourceFile->Path(),
	    std::make_shared<collections::SyncMap<
	        module::ModeAwareCacheKey,
	        std::shared_ptr<module::ResolvedModule>,
	        module::ModeAwareCacheKeyHash>>());
	module::ModeAwareCacheKey key{moduleReference, mode};
	if (auto [resolved, ok] = cache->Load(key); ok) {
		return toCheckerResolvedModule(*resolved);
	}
	auto resolved =
	    moduleResolver->ResolveModuleName(
	        moduleReference, currentSourceFile->FileName(), mode, nullptr)
	        .first;
	auto [stored, _loaded] = cache->LoadOrStore(key, resolved);
	if (!stored->IsResolved() && !tspath::pathIsRelative(moduleReference)) {
		onFailedAmbientModuleLookup(currentSourceFile, moduleReference);
	}
	return toCheckerResolvedModule(*stored);
}

// GetSourceFileForResolvedModule — aliasresolver.go:130
SourceFile* aliasResolver::GetSourceFileForResolvedModule(
    const std::string& fileName) {
	return GetSourceFile(fileName);
}

// GetResolvedModules — aliasresolver.go:135
std::vector<checker::ResolvedModule> aliasResolver::GetResolvedModules() {
	// only used when producing diagnostics, which hopefully the checker won't do
	return {};
}

// ---

// GetSymlinkCache — aliasresolver.go:143
symlinks::KnownSymlinks* aliasResolver::GetSymlinkCache() {
	TSC_UNREACHABLE("unimplemented");
}

// GetSourceFileMetaData — aliasresolver.go:148
const SourceFileMetaData& aliasResolver::GetSourceFileMetaData(
    const std::string&) const {
	TSC_UNREACHABLE("unimplemented");
}

// CommonSourceDirectory — aliasresolver.go:153
std::string aliasResolver::CommonSourceDirectory() {
	TSC_UNREACHABLE("unimplemented");
}

// ContentMapperExtensions — aliasresolver.go:158
std::vector<std::string> aliasResolver::ContentMapperExtensions() {
	return {};
}

// FileExists — aliasresolver.go:163
bool aliasResolver::FileExists(const std::string&) {
	TSC_UNREACHABLE("unimplemented");
}

// GetGlobalTypingsCacheLocation — aliasresolver.go:168
std::string aliasResolver::GetGlobalTypingsCacheLocation() {
	TSC_UNREACHABLE("unimplemented");
}

// GetImportHelpersImportSpecifier — aliasresolver.go:173
Node* aliasResolver::GetImportHelpersImportSpecifier(const std::string&) {
	TSC_UNREACHABLE("unimplemented");
}

// GetJSXRuntimeImportSpecifier — aliasresolver.go:178
std::pair<std::string, Node*> aliasResolver::GetJSXRuntimeImportSpecifier(
    const std::string&) {
	return {"", nullptr};
}

// GetNearestAncestorDirectoryWithPackageJson — aliasresolver.go:183
std::string aliasResolver::GetNearestAncestorDirectoryWithPackageJson(
    const std::string&) {
	TSC_UNREACHABLE("unimplemented");
}

// GetPackageJsonInfo — aliasresolver.go:188
std::shared_ptr<packagejson::InfoCacheEntry>
aliasResolver::GetPackageJsonInfo(const std::string&) {
	TSC_UNREACHABLE("unimplemented");
}

// GetProjectReferenceFromOutputDts — aliasresolver.go:193
const checker::SourceOutputAndProjectReference*
aliasResolver::GetProjectReferenceFromOutputDts(const std::string&) {
	TSC_UNREACHABLE("unimplemented");
}

// GetProjectReferenceFromSource — aliasresolver.go:198
checker::SourceOutputAndProjectReference*
aliasResolver::GetProjectReferenceFromSource(const tspath::Path&) {
	TSC_UNREACHABLE("unimplemented");
}

// GetRedirectForResolution — aliasresolver.go:203
checker::RedirectInfo* aliasResolver::GetRedirectForResolution(SourceFile*) {
	TSC_UNREACHABLE("unimplemented");
}

// GetRedirectTargets — aliasresolver.go:208
std::vector<std::string> aliasResolver::GetRedirectTargets(
    const tspath::Path&) {
	TSC_UNREACHABLE("unimplemented");
}

// GetResolvedModuleFromModuleSpecifier — aliasresolver.go:213
module::ResolvedModule* aliasResolver::GetResolvedModuleFromModuleSpecifier(
    SourceFile*, Node*) {
	TSC_UNREACHABLE("unimplemented");
}

// GetSourceOfProjectReferenceIfOutputIncluded — aliasresolver.go:218
std::string aliasResolver::GetSourceOfProjectReferenceIfOutputIncluded(
    SourceFile*) {
	TSC_UNREACHABLE("unimplemented");
}

// IsSourceFileDefaultLibrary — aliasresolver.go:223
bool aliasResolver::IsSourceFileDefaultLibrary(const std::string&) const {
	return false;
}

// IsSourceFromProjectReference — aliasresolver.go:228
bool aliasResolver::IsSourceFromProjectReference(const tspath::Path&) {
	TSC_UNREACHABLE("unimplemented");
}

// SourceFileMayBeEmitted — aliasresolver.go:233
bool aliasResolver::SourceFileMayBeEmitted(SourceFile*, bool) {
	TSC_UNREACHABLE("unimplemented");
}

// GetPackagesMap — aliasresolver.go:237
const std::unordered_map<std::string, bool>& aliasResolver::GetPackagesMap() {
	static const std::unordered_map<std::string, bool> empty;
	return empty;
}

// IsSourceFileFromExternalLibrary — checker.Program pure virtual added by the
// C++ port (no Go counterpart); the alias resolver never owns external
// libraries, matching Go's implicit false.
bool aliasResolver::IsSourceFileFromExternalLibrary(SourceFile*) const {
	return false;
}

}  // namespace tsc::ls::autoimport
