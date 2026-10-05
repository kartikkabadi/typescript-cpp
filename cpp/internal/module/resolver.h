// module/resolver.go — port of tsc/internal/module/resolver.go.
#pragma once

#include <any>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/core/pattern.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/module/cache.h"
#include "internal/module/types.h"
#include "internal/module/util.h"
#include "internal/packagejson/packagejson.h"
#include "internal/tspath/tspath.h"

namespace tsc::module {

// DiagAndArgs — resolver.go.
struct DiagAndArgs {
	const DiagnosticMessage* Message;
	std::vector<std::any> Args;
};

// resolved — resolver.go. nullptr = keep searching (Go's nil);
// non-null with empty path = stopped but unresolved.
struct resolved {
	std::string path;
	std::string extension;
	PackageId packageId;
	std::string originalPath;
	bool resolvedUsingTsExtension = false;
	bool resolvedUsingExtraExtensions = false;

	bool isResolved() const { return !path.empty(); }
};

inline bool shouldContinueSearching(const std::unique_ptr<resolved>& r) {
	return r == nullptr;
}

inline std::unique_ptr<resolved> unresolved() {
	return std::make_unique<resolved>();
}

using resolutionKindSpecificLoader =
    std::function<std::unique_ptr<resolved>(extensions ext,
                                            const std::string& candidate)>;

// tracer — resolver.go: records trace diagnostics when TraceResolution is on.
struct tracer {
	std::vector<DiagAndArgs> traces;

	template <typename... Args>
	void write(const DiagnosticMessage* diag, Args&&... as) {
		traces.push_back(
		    {diag, {std::any(std::forward<Args>(as))...}});
	}

	const std::vector<DiagAndArgs>& getTraces() const { return traces; }

	void traceResolutionUsingProjectReference(
	    const ResolvedProjectReference* redirectedReference);
	void traceTypeReferenceDirectiveResult(
	    std::string_view typeReferenceDirectiveName,
	    const ResolvedTypeReferenceDirective* result);
};

// ParsedPatterns — resolver.go: path-mappings split into exact-matchable
// strings and star patterns.
struct ParsedPatterns {
	collections::Set<std::string> matchableStringSet;
	std::vector<Pattern> patterns;
};

std::shared_ptr<ParsedPatterns> TryParsePatterns(
    const collections::OrderedMap<std::string, std::vector<std::string>>*
        pathMappings);
std::shared_ptr<ParsedPatterns> TryParsePatterns(
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        pathMappings);

Pattern MatchPatternOrExact(const ParsedPatterns* patterns,
                            std::string_view candidate);

// Ending — resolver.go.
enum class Ending : int {
	// Fixed indicates that the module specifier cannot be changed without
	// changing its resolution.
	Fixed = 0,
	// ExtensionChangeable indicates that the module specifier's extension
	// portion was inferred from a file on disk, so an interchangeable one
	// could be used instead (e.g. replacing .d.ts with .js).
	ExtensionChangeable,
	// Changeable indicates that the module specifier's file name and
	// extension portion were inferred from a file on disk without being
	// matched as part of an 'exports' pattern.
	Changeable,
};

struct ResolvedEntrypoint {
	// OriginalFileName is the symlink path if the entrypoint was discovered
	// at a symlink. Empty otherwise.
	std::string OriginalFileName;
	// ResolvedFileName is the real path to the entrypoint file.
	std::string ResolvedFileName;
	std::string ModuleSpecifier;
	// Ending indicates whether the file name and extension portion of
	// ModuleSpecifier is fixed or can be changed.
	Ending Ending_ = Ending::Fixed;
	// IncludeConditions are the conditions that a resolver must have to
	// reach this entrypoint.
	std::unique_ptr<collections::Set<std::string>> IncludeConditions;
	// ExcludeConditions are the conditions that a resolver must not have to
	// reach this entrypoint.
	std::unique_ptr<collections::Set<std::string>> ExcludeConditions;

	std::string_view SymlinkOrRealpath() const {
		if (!OriginalFileName.empty()) return OriginalFileName;
		return ResolvedFileName;
	}
};

class DefaultResolver;

// resolutionState — resolver.go.
struct resolutionState {
	DefaultResolver* resolver = nullptr;
	tracer* traceBuilder = nullptr;

	// request fields
	std::string name;
	std::string containingDirectory;
	bool isConfigLookup = false;
	NodeResolutionFeatures features = NodeResolutionFeaturesNone;
	bool esmMode = false;
	std::vector<std::string> conditions;
	extensions ext = 0;
	const CompilerOptions* compilerOptions = nullptr;
	bool resolvePackageDirectoryOnly = false;

	// state fields
	// candidateEndingIsFromConfig is set when the candidate file extension
	// originated from configuration (package.json fields, tsconfig.json paths
	// entries, or wildcard substitutions) rather than from the module
	// specifier written in source code. When true, resolvedUsingTsExtension
	// is suppressed so the checker does not attempt to extract a TS extension
	// from the original specifier.
	bool candidateEndingIsFromConfig = false;
	bool resolvedPackageDirectory = false;
	std::vector<Diagnostic*> diagnostics;

	std::shared_ptr<ResolvedTypeReferenceDirective>
	resolveTypeReferenceDirective(const std::vector<std::string>& typeRoots,
	                              bool fromConfig,
	                              bool fromInferredTypesContainingFile);
	std::string getCandidateFromTypeRoot(const std::string& typeRoot);
	std::string mangleScopedPackageName(const std::string& name);
	std::unique_ptr<resolved> resolveFromTypeRoot();

	std::shared_ptr<packagejson::InfoCacheEntry> getPackageScopeForPath(
	    const std::string& directory);
	std::shared_ptr<ResolvedModule> resolveNodeLike();
	std::shared_ptr<ResolvedModule> resolveNodeLikeWorker();
	std::unique_ptr<resolved> loadModuleFromSelfNameReference();
	std::unique_ptr<resolved> loadModuleFromImports();
	std::unique_ptr<resolved> loadModuleFromExports(
	    std::shared_ptr<packagejson::InfoCacheEntry> packageInfo,
	    extensions ext, const std::string& subpath);
	std::unique_ptr<resolved> loadModuleFromExportsOrImports(
	    extensions ext, const std::string& moduleName,
	    const collections::OrderedMap<std::string,
	                                  packagejson::ExportsOrImports>*
	        lookupTable,
	    std::shared_ptr<packagejson::InfoCacheEntry> scope, bool isImports);
	std::unique_ptr<resolved> loadModuleFromTargetExportOrImport(
	    extensions ext, const std::string& moduleName,
	    std::shared_ptr<packagejson::InfoCacheEntry> scope, bool isImports,
	    const packagejson::ExportsOrImports& target,
	    const std::string& subpath, bool isPattern, const std::string& key);
	std::unique_ptr<resolved> tryLoadInputFileForPath(
	    const std::string& finalPath, const std::string& entry,
	    const std::string& packagePath, bool isImports);
	std::vector<std::string> getOutputDirectoriesForBaseDirectory(
	    std::string_view commonSourceDirGuess);
	std::unique_ptr<resolved> loadModuleFromNearestNodeModulesDirectory(
	    bool typesScopeOnly);
	std::unique_ptr<resolved>
	loadModuleFromNearestNodeModulesDirectoryWorker(extensions ext,
	                                                ResolutionMode mode,
	                                                bool typesScopeOnly);
	std::unique_ptr<resolved> loadModuleFromImmediateNodeModulesDirectory(
	    extensions ext, const std::string& directory, bool typesScopeOnly);
	std::unique_ptr<resolved> loadModuleFromSpecificNodeModulesDirectory(
	    extensions ext, std::string_view moduleName,
	    const std::string& nodeModulesDirectory);
	std::shared_ptr<ResolvedModule> createResolvedModuleHandlingSymlink(
	    std::unique_ptr<resolved> r);
	std::shared_ptr<ResolvedModule> createResolvedModule(
	    std::unique_ptr<resolved> r, bool isExternalLibraryImport);
	std::shared_ptr<ResolvedTypeReferenceDirective>
	createResolvedTypeReferenceDirective(std::unique_ptr<resolved> r,
	                                     bool primary);
	std::pair<std::string, std::string> getOriginalAndResolvedFileName(
	    const std::string& fileName);
	std::unique_ptr<resolved> tryLoadModuleUsingOptionalResolutionSettings();
	const ParsedPatterns* getParsedPatternsForPaths();
	std::unique_ptr<resolved> tryLoadModuleUsingPathsIfEligible();
	std::unique_ptr<resolved> tryLoadModuleUsingRootDirs();
	std::unique_ptr<resolved> nodeLoadModuleByRelativeName(
	    extensions ext, const std::string& candidate,
	    bool considerPackageJson);
	std::unique_ptr<resolved> loadModuleFromFile(
	    extensions ext, const std::string& candidate);
	std::unique_ptr<resolved> loadModuleFromFileNoImplicitExtensions(
	    extensions ext, const std::string& candidate);
	std::unique_ptr<resolved> tryAddingExtensions(
	    const std::string& extensionless, extensions ext,
	    std::string_view originalExtension);
	std::unique_ptr<resolved> tryExtension(std::string_view extension,
	                                       const std::string& extensionless,
	                                       bool resolvedUsingTsExtension);
	std::pair<std::string, bool> tryFile(const std::string& fileName);
	bool tryFileLookup(const std::string& fileName);
	std::unique_ptr<resolved> loadNodeModuleFromDirectory(
	    extensions ext, const std::string& candidate,
	    bool considerPackageJson);
	std::unique_ptr<resolved> loadNodeModuleFromDirectoryWorker(
	    extensions ext, const std::string& candidate,
	    std::shared_ptr<packagejson::InfoCacheEntry> packageInfo);
	std::unique_ptr<resolved> loadFileNameFromPackageJSONField(
	    extensions ext, const std::string& candidate,
	    std::string_view packageJSONValue);
	std::pair<std::string, bool> getPackageFile(
	    extensions ext,
	    std::shared_ptr<packagejson::InfoCacheEntry> packageInfo);
	std::shared_ptr<packagejson::InfoCacheEntry> getPackageJsonInfo(
	    const std::string& packageDirectory);
	PackageId getPackageId(
	    const std::string& resolvedFileName,
	    std::shared_ptr<packagejson::InfoCacheEntry> packageInfo);
	std::string readPackageJsonPeerDependencies(
	    std::shared_ptr<packagejson::InfoCacheEntry> packageJsonInfo);
	std::string realPath(std::string_view path);
	bool conditionMatches(std::string_view condition);
	std::function<void(const DiagnosticMessage*,
	                   const std::vector<std::string>&)>
	getTraceFunc();

	template <typename MapLike>
	std::unique_ptr<resolved> tryLoadModuleUsingPaths(
	    extensions ext, const std::string& moduleName,
	    const std::string& containingDirectory, const MapLike& paths,
	    const ParsedPatterns* pathPatterns,
	    const resolutionKindSpecificLoader& loader);

	template <typename T>
	bool validatePackageJSONField(const std::string& fieldName,
	                              const packagejson::Expected<T>* field) {
		if (field->IsPresent()) {
			if (field->IsValid()) {
				return true;
			}
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    Expected_type_of_0_field_in_package_json_to_be_1_got_2,
				    fieldName, std::string{field->ExpectedJSONType()},
				    field->ActualJSONType());
			}
		}
		if (traceBuilder != nullptr) {
			traceBuilder->write(X_package_json_does_not_have_a_0_field,
			                    fieldName);
		}
		return false;
	}

	template <typename T>
	std::pair<std::string, bool> getPackageJSONPathField(
	    const std::string& fieldName, const packagejson::Expected<T>* field,
	    const std::string& directory) {
		if (!validatePackageJSONField(fieldName, field)) {
			return {"", false};
		}
		if (field->Value.empty()) {
			if (traceBuilder != nullptr) {
				traceBuilder->write(X_package_json_had_a_falsy_0_field,
				                    fieldName);
			}
			return {"", false};
		}
		auto path = tspath::normalizePath(
		    tspath::combinePaths(directory, {field->Value}));
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    X_package_json_has_0_field_1_that_references_2, fieldName,
			    field->Value, path);
		}
		return {path, true};
	}

	// Entrypoint enumeration — loadEntrypointsFromExportMap.
	std::vector<std::unique_ptr<ResolvedEntrypoint>>
	loadEntrypointsFromExportMap(
	    std::shared_ptr<packagejson::InfoCacheEntry> packageJson,
	    const std::string& packageName,
	    const packagejson::ExportsOrImports& exports);
	std::pair<std::string, bool> getMatchedStarForPatternEntrypoint(
	    const std::string& file, const std::string& leadingSlice,
	    const std::string& trailingSlice, bool caseSensitive);
};

// Resolver — module.Resolver interface (types.go).
struct Resolver {
	virtual ~Resolver() = default;
	virtual std::pair<std::shared_ptr<ResolvedModule>,
	                  std::vector<DiagAndArgs>>
	ResolveModuleName(
	    std::string_view moduleName, std::string_view containingFile,
	    ResolutionMode resolutionMode,
	    const ResolvedProjectReference* redirectedReference) = 0;
	virtual std::pair<std::shared_ptr<ResolvedModule>,
	                  std::vector<DiagAndArgs>>
	ResolveModuleNameFromDirectory(std::string_view moduleName,
	                               std::string_view containingDirectory,
	                               ResolutionMode resolutionMode) = 0;
	virtual std::pair<std::shared_ptr<ResolvedTypeReferenceDirective>,
	                  std::vector<DiagAndArgs>>
	ResolveTypeReferenceDirective(
	    std::string_view typeReferenceDirectiveName,
	    std::string_view containingFile, ResolutionMode resolutionMode,
	    const ResolvedProjectReference* redirectedReference) = 0;
	virtual std::shared_ptr<packagejson::InfoCacheEntry>
	GetPackageScopeForPath(const std::string& directory) = 0;
	virtual void PackageJsonCacheEntries(
	    const std::function<bool(
	        const std::string&,
	        std::shared_ptr<packagejson::InfoCacheEntry>)>& f) = 0;
	virtual std::shared_ptr<ResolvedModule> ResolvePackageDirectory(
	    std::string_view moduleName, std::string_view containingFile,
	    ResolutionMode resolutionMode,
	    const ResolvedProjectReference* redirectedReference) = 0;
};

struct ResolverOptions {
	ResolutionHost* Host = nullptr;
	const CompilerOptions* CompilerOptions = nullptr;
	std::string TypingsLocation;
	std::string ProjectName;
	std::vector<std::string> ExtraExtensions;
	std::shared_ptr<packagejson::InfoCache> PackageJsonCache;
};

// DefaultResolver — resolver.go.
class DefaultResolver : public Resolver, public caches {
public:
	ResolutionHost* host;
	const CompilerOptions* compilerOptions;
	std::string typingsLocation;
	std::string projectName;
	std::vector<std::string> extraExtensions;

	explicit DefaultResolver(ResolverOptions opts);

	// newTraceBuilder — non-null only when TraceResolution is on.
	tracer* newTraceBuilder();
	tracer traceBuilderStorage;

	std::shared_ptr<packagejson::InfoCacheEntry> GetPackageScopeForPath(
	    const std::string& directory) override;
	void PackageJsonCacheEntries(
	    const std::function<bool(
	        const std::string&,
	        std::shared_ptr<packagejson::InfoCacheEntry>)>& f) override;

	std::pair<std::shared_ptr<ResolvedTypeReferenceDirective>,
	          std::vector<DiagAndArgs>>
	ResolveTypeReferenceDirective(
	    std::string_view typeReferenceDirectiveName,
	    std::string_view containingFile, ResolutionMode resolutionMode,
	    const ResolvedProjectReference* redirectedReference) override;

	std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
	ResolveModuleName(
	    std::string_view moduleName, std::string_view containingFile,
	    ResolutionMode resolutionMode,
	    const ResolvedProjectReference* redirectedReference) override;

	std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
	ResolveModuleNameFromDirectory(
	    std::string_view moduleName, std::string_view containingDirectory,
	    ResolutionMode resolutionMode) override;

	std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
	resolveModuleName(const std::string& moduleName,
	                  const std::string& containingFile,
	                  const std::string& containingDirectory,
	                  ResolutionMode resolutionMode,
	                  const ResolvedProjectReference* redirectedReference);

	std::shared_ptr<ResolvedModule> ResolvePackageDirectory(
	    std::string_view moduleName, std::string_view containingFile,
	    ResolutionMode resolutionMode,
	    const ResolvedProjectReference* redirectedReference) override;

	std::shared_ptr<ResolvedModule> tryResolveFromTypingsLocation(
	    const std::string& moduleName, const std::string& containingDirectory,
	    std::shared_ptr<ResolvedModule> originalResult, tracer* traceBuilder);

	std::shared_ptr<ResolvedModule> resolveConfig(
	    const std::string& moduleName, const std::string& containingFile);

	// getParsedPatternsForPaths — resolver.go.
	const ParsedPatterns* getParsedPatternsForPaths(
	    const CompilerOptions* options);

	std::unique_ptr<ResolvedEntrypoint> createResolvedEntrypointHandlingSymlink(
	    const std::string& fileName, const std::string& moduleSpecifier,
	    const collections::Set<std::string>* includeConditions,
	    const collections::Set<std::string>* excludeConditions, Ending ending);

	std::vector<std::unique_ptr<ResolvedEntrypoint>>
	GetEntrypointsFromPackageJsonInfo(
	    std::shared_ptr<packagejson::InfoCacheEntry> packageJson,
	    const std::string& packageName, bool enableDirectorySearch);
};

DefaultResolver* NewResolver(ResolverOptions opts);

// GetCompilerOptionsWithRedirect — resolver.go.
const CompilerOptions* GetCompilerOptionsWithRedirect(
    const CompilerOptions* compilerOptions,
    const ResolvedProjectReference* redirectedReference);

std::vector<std::string> GetConditions(const CompilerOptions& options,
                                       ResolutionMode resolutionMode);
NodeResolutionFeatures getNodeResolutionFeatures(
    const CompilerOptions& options);

// moveToNextDirectorySeparatorIfAvailable — resolver.go.
int moveToNextDirectorySeparatorIfAvailable(std::string_view path,
                                            int prevSeparatorIndex,
                                            bool isFolder);

std::string normalizePathForCJSResolution(std::string_view containingDirectory,
                                          std::string_view moduleName);
bool matchesPatternWithTrailer(std::string_view target,
                               std::string_view name);
bool extensionIsOk(extensions ext, std::string_view extension);

std::shared_ptr<ResolvedModule> ResolveConfig(std::string_view moduleName,
                                              std::string_view containingFile,
                                              ResolutionHost* host);
std::vector<std::string> GetAutomaticTypeDirectiveNames(
    const CompilerOptions& options, ResolutionHost* host);

std::string_view moduleResolutionKindToString(ModuleResolutionKind kind);

// GetResolutionDiagnostic — util.go:125 (ported with program slice).
const DiagnosticMessage* getResolutionDiagnostic(
    const CompilerOptions* options, const ResolvedModule& resolvedModule,
    SourceFile* file);

}  // namespace tsc::module
