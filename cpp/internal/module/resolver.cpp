// module/resolver.go — port of tsc/internal/module/resolver.go.
// === slice: module ===

#include "internal/module/resolver.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <unordered_set>

#include "internal/core/version.h"
#include "internal/module/vfsmatch.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::module {

namespace {

// Local helpers matching Go's slices/strings utilities.

inline bool contains(const std::vector<std::string>& v,
                     std::string_view s) {
	return std::find(v.begin(), v.end(), s) != v.end();
}

inline size_t countOccurrences(std::string_view s, char c) {
	return static_cast<size_t>(std::count(s.begin(), s.end(), c));
}

// strings.Replace(s, old, new, 1)
inline std::string replaceFirst(std::string_view s, std::string_view old,
                                std::string_view replacement) {
	std::string result{s};
	auto pos = result.find(old);
	if (pos != std::string::npos) {
		result.replace(pos, old.size(), replacement);
	}
	return result;
}

// strings.ReplaceAll
inline std::string replaceAll(std::string_view s, std::string_view old,
                              std::string_view replacement) {
	std::string result{s};
	size_t pos = 0;
	while ((pos = result.find(old, pos)) != std::string::npos) {
		result.replace(pos, old.size(), replacement);
		pos += replacement.size();
	}
	return result;
}

inline std::string join(const std::vector<std::string>& v,
                        std::string_view sep) {
	std::string out;
	for (size_t i = 0; i < v.size(); i++) {
		if (i) out += sep;
		out += v[i];
	}
	return out;
}

// pathsValue — paths.GetOrZero for both map shapes used in the port:
// OrderedMap (versionPaths) and vector<pair> (CompilerOptions::Paths).
inline const std::vector<std::string>& pathsGetOrZero(
    const collections::OrderedMap<std::string, std::vector<std::string>>&
        paths,
    const std::string& key) {
	static const std::vector<std::string> empty;
	auto [v, ok] = paths.Get(key);
	return ok ? *v : empty;
}

inline const std::vector<std::string>& pathsGetOrZero(
    const std::vector<std::pair<std::string, std::vector<std::string>>>& paths,
    const std::string& key) {
	static const std::vector<std::string> empty;
	for (auto& [k, v] : paths) {
		if (k == key) return v;
	}
	return empty;
}

inline bool pkgExists(
    const std::shared_ptr<packagejson::InfoCacheEntry>& e) {
	return e != nullptr && e->Exists();
}

}  // namespace

std::string_view moduleResolutionKindToString(ModuleResolutionKind kind) {
	// compileroptions.go — ModuleResolutionKind.String.
	switch (kind) {
	case ModuleResolutionKind::Unknown:
		// panic("should not use zero value of ModuleResolutionKind")
		__builtin_trap();
	case ModuleResolutionKind::Classic: return "Classic";
	case ModuleResolutionKind::Node10: return "Node10";
	case ModuleResolutionKind::Node16: return "Node16";
	case ModuleResolutionKind::NodeNext: return "NodeNext";
	case ModuleResolutionKind::Bundler: return "Bundler";
	default: __builtin_trap();
	}
}

// --- resolver.go: newResolutionState ---

resolutionState newResolutionState(
    const std::string& name, const std::string& containingDirectory,
    bool isTypeReferenceDirective, ResolutionMode resolutionMode,
    const CompilerOptions* compilerOptions,
    const ResolvedProjectReference* redirectedReference,
    DefaultResolver* resolver, tracer* traceBuilder);

const CompilerOptions* GetCompilerOptionsWithRedirect(
    const CompilerOptions* compilerOptions,
    const ResolvedProjectReference* redirectedReference) {
	if (redirectedReference == nullptr) {
		return compilerOptions;
	}
	if (auto* optionsFromRedirect = redirectedReference->CompilerOptions();
	    optionsFromRedirect != nullptr) {
		return optionsFromRedirect;
	}
	return compilerOptions;
}

// --- DefaultResolver ---

DefaultResolver::DefaultResolver(ResolverOptions opts)
    : host(opts.Host),
      compilerOptions(opts.CompilerOptions),
      typingsLocation(std::move(opts.TypingsLocation)),
      projectName(std::move(opts.ProjectName)),
      extraExtensions(std::move(opts.ExtraExtensions)) {
	if (opts.PackageJsonCache != nullptr) {
		packageJsonInfoCache = opts.PackageJsonCache;
	} else {
		static_cast<caches&>(*this) = newCaches(
		    opts.Host->GetCurrentDirectory(),
		    opts.Host->UseCaseSensitiveFileNames(), opts.CompilerOptions);
	}
}

// resolver.go:167 NewResolver — the ctor above performs the field init.
DefaultResolver* NewResolver(ResolverOptions opts) {
	return new DefaultResolver(std::move(opts));
}

std::unique_ptr<tracer> DefaultResolver::newTraceBuilder() {
	if (compilerOptions->TraceResolution == Tristate::True) {
		return std::make_unique<tracer>();
	}
	return nullptr;
}

std::shared_ptr<packagejson::InfoCacheEntry>
DefaultResolver::GetPackageScopeForPath(const std::string& directory) {
	resolutionState state;
	state.compilerOptions = compilerOptions;
	state.resolver = this;
	return state.getPackageScopeForPath(directory);
}

void DefaultResolver::PackageJsonCacheEntries(
    const std::function<bool(const std::string&,
                             std::shared_ptr<packagejson::InfoCacheEntry>)>&
        f) {
	packageJsonInfoCache->Range(f);
}

void tracer::traceResolutionUsingProjectReference(
    const ResolvedProjectReference* redirectedReference) {
	if (redirectedReference != nullptr &&
	    redirectedReference->CompilerOptions() != nullptr) {
		write(Using_compiler_options_of_project_reference_redirect_0,
		      redirectedReference->ConfigName());
	}
}

// ResolveTypeReferenceDirective — resolver.go.
std::pair<std::shared_ptr<ResolvedTypeReferenceDirective>,
          std::vector<DiagAndArgs>>
DefaultResolver::ResolveTypeReferenceDirective(
    std::string_view typeReferenceDirectiveName,
    std::string_view containingFile, ResolutionMode resolutionMode,
    const ResolvedProjectReference* redirectedReference) {
	auto containingDirectory =
	    tspath::getDirectoryPath(containingFile);
	auto traceBuilder = newTraceBuilder();

	bool fromInferredTypesContainingFile =
	    containingFile.ends_with(InferredTypesContainingFile);

	typeRefDirectiveResolutionCacheKey cacheKey{
	    std::string{containingDirectory},
	    std::string{typeReferenceDirectiveName},
	    resolutionMode,
	    getRedirectConfigName(redirectedReference),
	    fromInferredTypesContainingFile,
	};

	if (traceBuilder == nullptr) {
		if (auto [cached, ok] =
		        typeRefDirectiveResolutionCache_.Get(cacheKey);
		    ok) {
			return {cached, {}};
		}
	}

	const CompilerOptions* options = GetCompilerOptionsWithRedirect(
	    compilerOptions, redirectedReference);

	auto [typeRoots, fromConfig] =
	    options->GetEffectiveTypeRoots(host->GetCurrentDirectory());
	if (traceBuilder != nullptr) {
		traceBuilder->write(
		    Resolving_type_reference_directive_0_containing_file_1_root_directory_2,
		    std::string{typeReferenceDirectiveName},
		    std::string{containingFile}, join(typeRoots, ","));
		traceBuilder->traceResolutionUsingProjectReference(
		    redirectedReference);
	}

	auto state = newResolutionState(
	    std::string{typeReferenceDirectiveName}, containingDirectory,
	    true /*isTypeReferenceDirective*/, resolutionMode, options,
	    redirectedReference, this, traceBuilder.get());
	auto result = state.resolveTypeReferenceDirective(
	    typeRoots, fromConfig, fromInferredTypesContainingFile);

	if (traceBuilder != nullptr) {
		traceBuilder->traceTypeReferenceDirectiveResult(
		    typeReferenceDirectiveName, result.get());
	}

	typeRefDirectiveResolutionCache_.Set(cacheKey, result);

	return {result,
	        traceBuilder != nullptr ? traceBuilder->getTraces()
	                                : std::vector<DiagAndArgs>{}};
}

std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
DefaultResolver::ResolveModuleName(
    std::string_view moduleName, std::string_view containingFile,
    ResolutionMode resolutionMode,
    const ResolvedProjectReference* redirectedReference) {
	return resolveModuleName(
	    std::string{moduleName}, std::string{containingFile},
	    tspath::getDirectoryPath(containingFile), resolutionMode,
	    redirectedReference);
}

std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
DefaultResolver::ResolveModuleNameFromDirectory(
    std::string_view moduleName, std::string_view containingDirectory,
    ResolutionMode resolutionMode) {
	return resolveModuleName(
	    std::string{moduleName}, std::string{containingDirectory},
	    std::string{containingDirectory}, resolutionMode, nullptr);
}

std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
DefaultResolver::resolveModuleName(
    const std::string& moduleName, const std::string& containingFile,
    const std::string& containingDirectory, ResolutionMode resolutionMode,
    const ResolvedProjectReference* redirectedReference) {
	auto traceBuilder = newTraceBuilder();

	moduleResolutionCacheKey cacheKey{
	    containingDirectory, moduleName, resolutionMode,
	    getRedirectConfigName(redirectedReference)};

	if (traceBuilder == nullptr) {
		if (auto [cached, ok] = moduleResolutionCache_.Get(cacheKey); ok) {
			return {cached, {}};
		}
	}

	const CompilerOptions* options = GetCompilerOptionsWithRedirect(
	    compilerOptions, redirectedReference);
	if (traceBuilder != nullptr) {
		traceBuilder->write(Resolving_module_0_from_1, moduleName,
		                    containingFile);
		traceBuilder->traceResolutionUsingProjectReference(
		    redirectedReference);
	}

	auto moduleResolution = options->GetModuleResolutionKind();
	if (options->ModuleResolution != moduleResolution) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Module_resolution_kind_is_not_specified_using_0,
			    std::string{moduleResolutionKindToString(
			        moduleResolution)});
		}
	} else {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Explicitly_specified_module_resolution_kind_Colon_0,
			    std::string{moduleResolutionKindToString(
			        moduleResolution)});
		}
	}

	std::shared_ptr<ResolvedModule> result;
	switch (moduleResolution) {
	case ModuleResolutionKind::Node16:
	case ModuleResolutionKind::NodeNext:
	case ModuleResolutionKind::Bundler: {
		auto state = newResolutionState(
		    moduleName, containingDirectory,
		    false /*isTypeReferenceDirective*/, resolutionMode, options,
		    redirectedReference, this, traceBuilder.get());
		result = state.resolveNodeLike();
		break;
	}
	default:
		fprintf(stderr,
		        "tsc internal error: Unexpected moduleResolution: %d\n",
		        static_cast<int>(moduleResolution));
		abort();
	}

	if (traceBuilder != nullptr) {
		if (result->IsResolved()) {
			if (!result->PackageId.Name.empty()) {
				traceBuilder->write(
				    Module_name_0_was_successfully_resolved_to_1_with_Package_ID_2,
				    moduleName, result->ResolvedFileName,
				    result->PackageId.String());
			} else {
				traceBuilder->write(
				    Module_name_0_was_successfully_resolved_to_1,
				    moduleName, result->ResolvedFileName);
			}
		} else {
			traceBuilder->write(Module_name_0_was_not_resolved,
			                    moduleName);
		}
	}

	auto finalResult = tryResolveFromTypingsLocation(
	    moduleName, containingDirectory, result, traceBuilder.get());
	moduleResolutionCache_.Set(cacheKey, finalResult);

	return {finalResult, traceBuilder != nullptr
	                         ? traceBuilder->getTraces()
	                         : std::vector<DiagAndArgs>{}};
}

std::shared_ptr<ResolvedModule> DefaultResolver::ResolvePackageDirectory(
    std::string_view moduleName, std::string_view containingFile,
    ResolutionMode resolutionMode,
    const ResolvedProjectReference* redirectedReference) {
	const CompilerOptions* options = GetCompilerOptionsWithRedirect(
	    compilerOptions, redirectedReference);
	auto containingDirectory = tspath::getDirectoryPath(containingFile);
	auto state = newResolutionState(
	    std::string{moduleName}, containingDirectory,
	    false /*isTypeReferenceDirective*/, resolutionMode, options,
	    redirectedReference, this, nullptr);
	state.resolvePackageDirectoryOnly = true;
	if (auto result =
	        state.loadModuleFromNearestNodeModulesDirectory(
	            false /*typesScopeOnly*/);
	    result != nullptr && !result->path.empty()) {
		return state.createResolvedModuleHandlingSymlink(
		    std::move(result));
	}
	return nullptr;
}

std::shared_ptr<ResolvedModule> DefaultResolver::tryResolveFromTypingsLocation(
    const std::string& moduleName, const std::string& containingDirectory,
    std::shared_ptr<ResolvedModule> originalResult, tracer* traceBuilder) {
	if (typingsLocation.empty() ||
	    tspath::isExternalModuleNameRelative(moduleName) ||
	    (!originalResult->ResolvedFileName.empty() &&
	     tspath::fileExtensionIsOneOf(
	         originalResult->Extension,
	         tspath::supportedTSExtensionsWithJsonFlat))) {
		return originalResult;
	}

	auto state = newResolutionState(
	    moduleName, containingDirectory,
	    false /*isTypeReferenceDirective*/,
	    ResolutionModeNone /*resolutionMode*/,
	    compilerOptions,
	    nullptr /*redirectedReference*/,
	    this,
	    traceBuilder);
	if (traceBuilder != nullptr) {
		traceBuilder->write(
		    Auto_discovery_for_typings_is_enabled_in_project_0_Running_extra_resolution_pass_for_module_1_using_cache_location_2,
		    projectName, moduleName, typingsLocation);
	}
	auto globalResolved = state.loadModuleFromImmediateNodeModulesDirectory(
	    extensionsDeclaration, typingsLocation, false);
	if (globalResolved == nullptr) {
		return originalResult;
	}
	auto result =
	    state.createResolvedModule(std::move(globalResolved), true);
	result->ResolutionDiagnostics.insert(
	    result->ResolutionDiagnostics.end(),
	    originalResult->ResolutionDiagnostics.begin(),
	    originalResult->ResolutionDiagnostics.end());
	return result;
}

std::shared_ptr<ResolvedModule> DefaultResolver::resolveConfig(
    const std::string& moduleName, const std::string& containingFile) {
	auto containingDirectory = tspath::getDirectoryPath(containingFile);
	auto state = newResolutionState(
	    moduleName, containingDirectory,
	    false /*isTypeReferenceDirective*/, ResolutionModeCommonJS,
	    compilerOptions, nullptr, this, nullptr);
	state.isConfigLookup = true;
	state.ext = extensionsJson;
	return state.resolveNodeLike();
}

void tracer::traceTypeReferenceDirectiveResult(
    std::string_view typeReferenceDirectiveName,
    const ResolvedTypeReferenceDirective* result) {
	if (!result->IsResolved()) {
		write(Type_reference_directive_0_was_not_resolved,
		      std::string{typeReferenceDirectiveName});
	} else if (!result->PackageId.Name.empty()) {
		write(
		    Type_reference_directive_0_was_successfully_resolved_to_1_with_Package_ID_2_primary_Colon_3,
		    std::string{typeReferenceDirectiveName},
		    result->ResolvedFileName, result->PackageId.String(),
		    result->Primary);
	} else {
		write(
		    Type_reference_directive_0_was_successfully_resolved_to_1_primary_Colon_2,
		    std::string{typeReferenceDirectiveName},
		    result->ResolvedFileName, result->Primary);
	}
}

// --- resolver.go: resolutionState ---

resolutionState newResolutionState(
    const std::string& name, const std::string& containingDirectory,
    bool isTypeReferenceDirective, ResolutionMode resolutionMode,
    const CompilerOptions* compilerOptions,
    const ResolvedProjectReference* redirectedReference,
    DefaultResolver* resolver, tracer* traceBuilder) {
	resolutionState state;
	state.name = name;
	state.containingDirectory = containingDirectory;
	state.compilerOptions = GetCompilerOptionsWithRedirect(
	    compilerOptions, redirectedReference);
	state.resolver = resolver;
	state.traceBuilder = traceBuilder;

	if (isTypeReferenceDirective) {
		state.ext = extensionsDeclaration;
	} else if (state.compilerOptions->NoDtsResolution == Tristate::True) {
		state.ext = extensionsImplementationFiles;
	} else {
		state.ext = extensionsTypeScript | extensionsJavaScript |
		            extensionsDeclaration;
	}

	if (!isTypeReferenceDirective &&
	    state.compilerOptions->GetResolveJsonModule()) {
		state.ext |= extensionsJson;
	}

	switch (state.compilerOptions->GetModuleResolutionKind()) {
	case ModuleResolutionKind::Node16:
		state.features = NodeResolutionFeaturesNode16Default;
		state.esmMode = resolutionMode == ModuleKind::ESNext;
		state.conditions =
		    GetConditions(*state.compilerOptions, resolutionMode);
		break;
	case ModuleResolutionKind::NodeNext:
		state.features = NodeResolutionFeaturesNodeNextDefault;
		state.esmMode = resolutionMode == ModuleKind::ESNext;
		state.conditions =
		    GetConditions(*state.compilerOptions, resolutionMode);
		break;
	case ModuleResolutionKind::Bundler:
		state.features =
		    getNodeResolutionFeatures(*state.compilerOptions);
		state.conditions =
		    GetConditions(*state.compilerOptions, resolutionMode);
		break;
	default:;
	}
	return state;
}

std::shared_ptr<ResolvedTypeReferenceDirective>
resolutionState::resolveTypeReferenceDirective(
    const std::vector<std::string>& typeRoots, bool fromConfig,
    bool fromInferredTypesContainingFile) {
	// Primary lookup
	if (!typeRoots.empty()) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(Resolving_with_primary_search_path_0,
			                    join(typeRoots, ", "));
		}
		for (auto& typeRoot : typeRoots) {
			auto candidate = getCandidateFromTypeRoot(typeRoot);
			bool directoryExists =
			    resolver->host->DirectoryExists(typeRoot);
			if (!directoryExists) {
				if (traceBuilder != nullptr) {
					traceBuilder->write(
					    Directory_0_does_not_exist_skipping_all_lookups_in_it,
					    typeRoot);
				}
				continue;
			}
			if (fromConfig) {
				// Custom typeRoots resolve as file or directory just like
				// we do modules
				if (auto resolvedFromFile = loadModuleFromFile(
				        extensionsDeclaration, candidate);
				    !shouldContinueSearching(resolvedFromFile)) {
					auto packageDirectory = ParseNodeModuleFromPath(
					    resolvedFromFile->path, false);
					if (!packageDirectory.empty()) {
						resolvedFromFile->packageId = getPackageId(
						    resolvedFromFile->path,
						    getPackageJsonInfo(
						        packageDirectory));
					}
					return createResolvedTypeReferenceDirective(
					    std::move(resolvedFromFile), true /*primary*/);
				}
			}
			if (auto resolvedFromDirectory =
			        loadNodeModuleFromDirectory(
			            extensionsDeclaration, candidate,
			            true /*considerPackageJson*/);
			    !shouldContinueSearching(resolvedFromDirectory)) {
				return createResolvedTypeReferenceDirective(
				    std::move(resolvedFromDirectory),
				    true /*primary*/);
			}
		}
	} else if (traceBuilder != nullptr) {
		traceBuilder->write(
		    Root_directory_cannot_be_determined_skipping_primary_search_paths);
	}

	// Secondary lookup
	std::unique_ptr<resolved> r;
	if (!fromConfig || !fromInferredTypesContainingFile) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Looking_up_in_node_modules_folder_initial_location_0,
			    containingDirectory);
		}
		if (!tspath::isExternalModuleNameRelative(name)) {
			r = loadModuleFromNearestNodeModulesDirectory(
			    false /*typesScopeOnly*/);
		} else {
			auto candidate = normalizePathForCJSResolution(
			    containingDirectory, name);
			r = nodeLoadModuleByRelativeName(
			    extensionsDeclaration, candidate,
			    true /*considerPackageJson*/);
		}
	} else if (traceBuilder != nullptr) {
		traceBuilder->write(
		    Resolving_type_reference_directive_for_program_that_specifies_custom_typeRoots_skipping_lookup_in_node_modules_folder);
	}
	return createResolvedTypeReferenceDirective(std::move(r),
	                                            false /*primary*/);
}

std::string resolutionState::getCandidateFromTypeRoot(
    const std::string& typeRoot) {
	std::string_view nameForLookup = name;
	if (typeRoot.ends_with("/node_modules/@types") ||
	    typeRoot.ends_with("/node_modules/@types/")) {
		nameForLookup = mangleScopedPackageName(name);
	}
	return tspath::combinePaths(typeRoot, {nameForLookup});
}

std::string resolutionState::mangleScopedPackageName(
    const std::string& name_) {
	auto mangled = MangleScopedPackageName(name_);
	if (traceBuilder != nullptr && mangled != name_) {
		traceBuilder->write(Scoped_package_detected_looking_in_0,
		                    mangled);
	}
	return mangled;
}

// resolveFromTypeRoot — fallback after node_modules resolution fails, for
// declaration file lookups. Returns nullptr if typeRoots is not configured
// or no matching module is found in any typeRoot directory.
std::unique_ptr<resolved> resolutionState::resolveFromTypeRoot() {
	if (compilerOptions->TypeRoots.empty()) {
		return nullptr;
	}
	for (auto& typeRoot : compilerOptions->TypeRoots) {
		auto candidate = getCandidateFromTypeRoot(typeRoot);
		bool directoryExists =
		    resolver->host->DirectoryExists(typeRoot);
		if (!directoryExists) {
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    Directory_0_does_not_exist_skipping_all_lookups_in_it,
				    typeRoot);
			}
			continue;
		}
		if (auto resolvedFromFile =
		        loadModuleFromFile(extensionsDeclaration, candidate);
		    !shouldContinueSearching(resolvedFromFile)) {
			auto packageDirectory = ParseNodeModuleFromPath(
			    resolvedFromFile->path, false);
			if (!packageDirectory.empty()) {
				resolvedFromFile->packageId = getPackageId(
				    resolvedFromFile->path,
				    getPackageJsonInfo(packageDirectory));
			}
			return resolvedFromFile;
		}
		if (auto r = loadNodeModuleFromDirectory(
		        extensionsDeclaration, candidate,
		        true /*considerPackageJson*/);
		    !shouldContinueSearching(r)) {
			return r;
		}
	}
	return nullptr;
}

std::shared_ptr<packagejson::InfoCacheEntry>
resolutionState::getPackageScopeForPath(const std::string& directory) {
	return tspath::forEachAncestorDirectoryStoppingAtGlobalCache<
	    std::shared_ptr<packagejson::InfoCacheEntry>>(
	    resolver->typingsLocation, directory,
	    [&](std::string_view dir)
	        -> std::pair<std::shared_ptr<packagejson::InfoCacheEntry>,
	                     bool> {
		    if (auto result =
		            getPackageJsonInfo(std::string{dir});
		        result != nullptr) {
			    return {result, true};
		    }
		    return {nullptr, false};
	    });
}

std::shared_ptr<ResolvedModule> resolutionState::resolveNodeLike() {
	if (traceBuilder != nullptr) {
		std::vector<std::string> quoted;
		for (auto& c : conditions) {
			quoted.push_back("'" + c + "'");
		}
		auto conds = join(quoted, ", ");
		if (esmMode) {
			traceBuilder->write(
			    Resolving_in_0_mode_with_conditions_1,
			    std::string{"ESM"}, conds);
		} else {
			traceBuilder->write(
			    Resolving_in_0_mode_with_conditions_1,
			    std::string{"CJS"}, conds);
		}
	}
	auto result = resolveNodeLikeWorker();
	if (resolvedPackageDirectory && !isConfigLookup &&
	    (features & NodeResolutionFeaturesExports) != 0 &&
	    (ext & (extensionsTypeScript | extensionsDeclaration)) != 0 &&
	    !tspath::isExternalModuleNameRelative(name) &&
	    result->IsResolved() && result->IsExternalLibraryImport &&
	    !extensionIsOk(extensionsTypeScript | extensionsDeclaration,
	                   result->Extension) &&
	    contains(conditions, "import")) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Resolution_of_non_relative_name_failed_trying_with_modern_Node_resolution_features_disabled_to_see_if_npm_library_needs_configuration_update);
		}
		features = features & ~NodeResolutionFeaturesExports;
		ext = ext & (extensionsTypeScript | extensionsDeclaration);
		auto diagnosticsCount = diagnostics.size();
		if (auto diagnosticResult = resolveNodeLikeWorker();
		    diagnosticResult->IsResolved() &&
		    diagnosticResult->IsExternalLibraryImport) {
			result->AlternateResult =
			    diagnosticResult->ResolvedFileName;
		}
		diagnostics.resize(diagnosticsCount);
	}
	return result;
}

std::shared_ptr<ResolvedModule> resolutionState::resolveNodeLikeWorker() {
	if (auto r = tryLoadModuleUsingOptionalResolutionSettings();
	    !shouldContinueSearching(r)) {
		return createResolvedModuleHandlingSymlink(std::move(r));
	}

	if (!tspath::isExternalModuleNameRelative(name)) {
		if ((features & NodeResolutionFeaturesImports) != 0 &&
		    name.starts_with("#")) {
			if (auto r = loadModuleFromImports();
			    !shouldContinueSearching(r)) {
				return createResolvedModuleHandlingSymlink(
				    std::move(r));
			}
		}
		if ((features & NodeResolutionFeaturesSelfName) != 0) {
			if (auto r = loadModuleFromSelfNameReference();
			    !shouldContinueSearching(r)) {
				return createResolvedModuleHandlingSymlink(
				    std::move(r));
			}
		}
		if (name.find(':') != std::string::npos) {
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    Skipping_module_0_that_looks_like_an_absolute_URI_target_file_types_Colon_1,
				    name, extensionsString(ext));
			}
			return createResolvedModule(nullptr, false);
		}
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Loading_module_0_from_node_modules_folder_target_file_types_Colon_1,
			    name, extensionsString(ext));
		}
		if (auto r = loadModuleFromNearestNodeModulesDirectory(
		        false /*typesScopeOnly*/);
		    !shouldContinueSearching(r)) {
			return createResolvedModuleHandlingSymlink(std::move(r));
		}
		if ((ext & extensionsDeclaration) != 0) {
			if (auto r = resolveFromTypeRoot();
			    !shouldContinueSearching(r)) {
				return createResolvedModuleHandlingSymlink(
				    std::move(r));
			}
		}
	} else {
		auto candidate =
		    normalizePathForCJSResolution(containingDirectory, name);
		auto r = nodeLoadModuleByRelativeName(ext, candidate, true);
		return createResolvedModule(
		    std::move(r),
		    r != nullptr &&
		        r->path.find("/node_modules/") != std::string::npos);
	}
	return createResolvedModule(nullptr, false);
}

std::unique_ptr<resolved>
resolutionState::loadModuleFromSelfNameReference() {
	auto directoryPath = tspath::getNormalizedAbsolutePath(
	    containingDirectory, resolver->host->GetCurrentDirectory());
	auto scope = getPackageScopeForPath(directoryPath);
	if (!pkgExists(scope) || scope->Contents->Exports.IsFalsy()) {
		// !!! falsy check seems wrong?
		return nullptr;
	}
	auto [pkgName, ok] = scope->Contents->Name.GetValue();
	if (!ok) {
		return nullptr;
	}
	auto parts = tspath::getPathComponents(name, "");
	auto nameParts = tspath::getPathComponents(pkgName, "");
	if (parts.size() < nameParts.size() ||
	    !std::equal(nameParts.begin(), nameParts.end(), parts.begin())) {
		return nullptr;
	}
	std::vector<std::string_view> trailingParts(
	    parts.begin() + nameParts.size(), parts.end());
	std::string subpath;
	if (!trailingParts.empty()) {
		subpath = tspath::combinePaths(".", trailingParts);
	} else {
		subpath = ".";
	}
	// Maybe TODO: splitting extensions into two priorities should be
	// unnecessary, except
	// https://github.com/microsoft/TypeScript/issues/50762 makes the
	// behavior different. As long as that bug exists, we need to do two
	// passes here in self-name loading in order to be consistent with
	// (non-self) library-name loading in
	// `loadModuleFromNearestNodeModulesDirectoryWorker`, which uses two
	// passes in order to prioritize `@types` packages higher up the
	// directory tree over untyped implementation packages. See the
	// selfNameModuleAugmentation.ts test for why this matters.
	//
	// However, there's an exception. If the user has `allowJs` and
	// `declaration`, we need to ensure that self-name imports of their own
	// package can resolve back to their input JS files via
	// `tryLoadInputFileForPath` at a higher priority than their output
	// declaration files, so we need to do a single pass with all
	// extensions for that case.
	if (compilerOptions->GetAllowJS() &&
	    containingDirectory.find("/node_modules/") == std::string::npos) {
		return loadModuleFromExports(scope, ext, subpath);
	}
	auto priorityExtensions =
	    ext & (extensionsTypeScript | extensionsDeclaration);
	auto secondaryExtensions =
	    ext & ~(extensionsTypeScript | extensionsDeclaration);
	if (auto r = loadModuleFromExports(scope, priorityExtensions, subpath);
	    !shouldContinueSearching(r)) {
		return r;
	}
	return loadModuleFromExports(scope, secondaryExtensions, subpath);
}

std::unique_ptr<resolved> resolutionState::loadModuleFromImports() {
	if (name == "#" ||
	    (name.starts_with("#/") &&
	     (features & NodeResolutionFeaturesImportsPatternRoot) == 0)) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Invalid_import_specifier_0_has_no_possible_resolutions,
			    name);
		}
		return nullptr;
	}
	auto directoryPath = tspath::getNormalizedAbsolutePath(
	    containingDirectory, resolver->host->GetCurrentDirectory());
	auto scope = getPackageScopeForPath(directoryPath);
	if (!pkgExists(scope)) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Directory_0_has_no_containing_package_json_scope_Imports_will_not_resolve,
			    directoryPath);
		}
		return nullptr;
	}
	if (scope->Contents->Imports.type != packagejson::JSONValueType::Object) {
		// !!! Old compiler only checks for undefined, but then assumes
		// `imports` is an object if present. Maybe should have a new
		// diagnostic for imports of an invalid type. Also, array should be
		// handled?
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    X_package_json_scope_0_has_no_imports_defined,
			    scope->PackageDirectory);
		}
		return nullptr;
	}

	if (auto result = loadModuleFromExportsOrImports(
	        ext, name, scope->Contents->Imports.AsObject(), scope,
	        true /*isImports*/);
	    !shouldContinueSearching(result)) {
		return result;
	}

	if (traceBuilder != nullptr) {
		traceBuilder->write(
		    Import_specifier_0_does_not_exist_in_package_json_scope_at_path_1,
		    name, scope->PackageDirectory);
	}
	return nullptr;
}

std::unique_ptr<resolved> resolutionState::loadModuleFromExports(
    std::shared_ptr<packagejson::InfoCacheEntry> packageInfo,
    extensions ext_, const std::string& subpath) {
	// !!! This is ported exactly, but the falsy check seems wrong
	if (!pkgExists(packageInfo) ||
	    packageInfo->Contents->Exports.IsFalsy()) {
		return nullptr;
	}

	if (subpath == ".") {
		packagejson::ExportsOrImports mainExport;
		switch (packageInfo->Contents->Exports.type) {
		case packagejson::JSONValueType::String:
		case packagejson::JSONValueType::Array:
			mainExport = packageInfo->Contents->Exports;
			break;
		case packagejson::JSONValueType::Object:
			if (packageInfo->Contents->Exports.IsConditions()) {
				mainExport = packageInfo->Contents->Exports;
			} else if (auto [dot, ok] =
			               packageInfo->Contents->Exports.AsObject()->Get(
			                   ".");
			           ok) {
				mainExport = *dot;
			}
			break;
		default:;
		}
		if (mainExport.type != packagejson::JSONValueType::NotPresent) {
			return loadModuleFromTargetExportOrImport(
			    ext_, subpath, packageInfo, false /*isImports*/,
			    mainExport, "", false /*isPattern*/, ".");
		}
	} else if (packageInfo->Contents->Exports.type ==
	               packagejson::JSONValueType::Object &&
	           packageInfo->Contents->Exports.IsSubpaths()) {
		if (auto result = loadModuleFromExportsOrImports(
		        ext_, subpath,
		        packageInfo->Contents->Exports.AsObject(), packageInfo,
		        false /*isImports*/);
		    !shouldContinueSearching(result)) {
			return result;
		}
	}

	if (traceBuilder != nullptr) {
		traceBuilder->write(
		    Export_specifier_0_does_not_exist_in_package_json_scope_at_path_1,
		    subpath, packageInfo->PackageDirectory);
	}
	return nullptr;
}

std::unique_ptr<resolved> resolutionState::loadModuleFromExportsOrImports(
    extensions ext_, const std::string& moduleName,
    const collections::OrderedMap<std::string, packagejson::ExportsOrImports>*
        lookupTable,
    std::shared_ptr<packagejson::InfoCacheEntry> scope, bool isImports) {
	if (!moduleName.ends_with("/") &&
	    moduleName.find('*') == std::string::npos) {
		if (auto [target, ok] = lookupTable->Get(moduleName); ok) {
			return loadModuleFromTargetExportOrImport(
			    ext_, moduleName, scope, isImports, *target, "",
			    false /*isPattern*/, moduleName);
		}
	}

	std::vector<std::string> expandingKeys;
	expandingKeys.reserve(lookupTable->Size());
	for (auto& key : lookupTable->Keys()) {
		if (countOccurrences(key, '*') == 1 || key.ends_with("/")) {
			expandingKeys.push_back(key);
		}
	}
	std::sort(expandingKeys.begin(), expandingKeys.end(),
	          [](const std::string& a, const std::string& b) {
		          return ComparePatternKeys(a, b) < 0;
	          });

	for (auto& potentialTarget : expandingKeys) {
		if ((features & NodeResolutionFeaturesExportsPatternTrailers) !=
		        0 &&
		    matchesPatternWithTrailer(potentialTarget, moduleName)) {
			auto [target, _] = lookupTable->Get(potentialTarget);
			auto starPos = potentialTarget.find('*');
			auto subpath = moduleName.substr(
			    starPos /*len(potentialTarget[:starPos])*/,
			    moduleName.size() -
			        (potentialTarget.size() - 1 - starPos) - starPos);
			return loadModuleFromTargetExportOrImport(
			    ext_, moduleName, scope, isImports, *target, subpath,
			    true, potentialTarget);
		} else if (potentialTarget.ends_with("*") &&
		           moduleName.starts_with(potentialTarget.substr(
		               0, potentialTarget.size() - 1))) {
			auto [target, _] = lookupTable->Get(potentialTarget);
			auto subpath =
			    moduleName.substr(potentialTarget.size() - 1);
			return loadModuleFromTargetExportOrImport(
			    ext_, moduleName, scope, isImports, *target, subpath,
			    true, potentialTarget);
		} else if (moduleName.starts_with(potentialTarget)) {
			auto [target, _] = lookupTable->Get(potentialTarget);
			auto subpath = moduleName.substr(potentialTarget.size());
			return loadModuleFromTargetExportOrImport(
			    ext_, moduleName, scope, isImports, *target, subpath,
			    false, potentialTarget);
		}
	}

	return nullptr;
}

std::unique_ptr<resolved>
resolutionState::loadModuleFromTargetExportOrImport(
    extensions ext_, const std::string& moduleName,
    std::shared_ptr<packagejson::InfoCacheEntry> scope, bool isImports,
    const packagejson::ExportsOrImports& target, const std::string& subpath,
    bool isPattern, const std::string& key) {
	switch (target.type) {
	case packagejson::JSONValueType::String: {
		const std::string& targetString = target.str;
		if (!isPattern && !subpath.empty() &&
		    !targetString.ends_with("/")) {
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    X_package_json_scope_0_has_invalid_type_for_target_of_specifier_1,
				    scope->PackageDirectory, moduleName);
			}
			return nullptr;
		}
		if (!targetString.starts_with("./")) {
			if (isImports && !targetString.starts_with("../") &&
			    !targetString.starts_with("/") &&
			    !tspath::isRootedDiskPath(targetString)) {
				auto combinedLookup = targetString + subpath;
				if (isPattern) {
					combinedLookup =
					    replaceAll(targetString, "*", subpath);
				}
				auto scopeContainingDirectory =
				    tspath::ensureTrailingDirectorySeparator(
				        scope->PackageDirectory);
				if (traceBuilder != nullptr) {
					traceBuilder->write(
					    Using_0_subpath_1_with_target_2,
					    std::string{"imports"}, key,
					    combinedLookup);
					traceBuilder->write(Resolving_module_0_from_1,
					                    combinedLookup,
					                    scopeContainingDirectory);
				}
				auto savedName = name;
				auto savedContainingDirectory = containingDirectory;
				name = combinedLookup;
				containingDirectory = scopeContainingDirectory;
				auto result = resolveNodeLike();
				name = std::move(savedName);
				containingDirectory =
				    std::move(savedContainingDirectory);
				if (result->IsResolved()) {
					auto r = std::make_unique<resolved>();
					r->path = result->ResolvedFileName;
					r->extension = result->Extension;
					r->packageId = result->PackageId;
					r->originalPath = result->OriginalPath;
					r->resolvedUsingTsExtension =
					    result->ResolvedUsingTsExtension;
					return r;
				}
				return nullptr;
			}
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    X_package_json_scope_0_has_invalid_type_for_target_of_specifier_1,
				    scope->PackageDirectory, moduleName);
			}
			return nullptr;
		}
		std::vector<std::string> parts;
		if (tspath::pathIsRelative(targetString)) {
			auto pc = tspath::getPathComponents(targetString, "");
			parts.assign(pc.begin() + 1, pc.end());
		} else {
			parts = tspath::getPathComponents(targetString, "");
		}
		std::vector<std::string> partsAfterFirst(parts.begin() + 1,
		                                         parts.end());
		if (contains(partsAfterFirst, "..") ||
		    contains(partsAfterFirst, ".") ||
		    contains(partsAfterFirst, "node_modules")) {
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    X_package_json_scope_0_has_invalid_type_for_target_of_specifier_1,
				    scope->PackageDirectory, moduleName);
			}
			return nullptr;
		}
		auto resolvedTarget = tspath::combinePaths(
		    scope->PackageDirectory, {targetString});
		// TODO: Assert that `resolvedTarget` is actually within the
		// package directory? That's what the spec says.... but I'm not
		// sure we need to be in the business of validating everyone's
		// import and export map correctness.
		auto subpathParts = tspath::getPathComponents(subpath, "");
		if (contains(subpathParts, "..") || contains(subpathParts, ".") ||
		    contains(subpathParts, "node_modules")) {
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    X_package_json_scope_0_has_invalid_type_for_target_of_specifier_1,
				    scope->PackageDirectory, moduleName);
			}
			return nullptr;
		}

		if (traceBuilder != nullptr) {
			std::string messageTarget;
			if (isPattern) {
				messageTarget = replaceAll(targetString, "*", subpath);
			} else {
				messageTarget = targetString + subpath;
			}
			traceBuilder->write(
			    Using_0_subpath_1_with_target_2,
			    std::string{isImports ? "imports" : "exports"}, key,
			    messageTarget);
		}
		std::string finalPath;
		if (isPattern) {
			finalPath = tspath::getNormalizedAbsolutePath(
			    replaceAll(resolvedTarget, "*", subpath),
			    resolver->host->GetCurrentDirectory());
		} else {
			finalPath = tspath::getNormalizedAbsolutePath(
			    resolvedTarget + subpath,
			    resolver->host->GetCurrentDirectory());
		}
		if (auto inputLink = tryLoadInputFileForPath(
		        finalPath, subpath,
		        tspath::combinePaths(scope->PackageDirectory,
		                             {"package.json"}),
		        isImports);
		    !shouldContinueSearching(inputLink)) {
			inputLink->packageId = getPackageId(inputLink->path, scope);
			return inputLink;
		}
		if (auto result = loadFileNameFromPackageJSONField(
		        ext_, finalPath, targetString);
		    !shouldContinueSearching(result)) {
			result->packageId = getPackageId(result->path, scope);
			return result;
		}
		return nullptr;
	}

	case packagejson::JSONValueType::Object: {
		if (traceBuilder != nullptr) {
			traceBuilder->write(Entering_conditional_exports);
		}
		for (auto& condition : target.AsObject()->Keys()) {
			if (conditionMatches(condition)) {
				if (traceBuilder != nullptr) {
					traceBuilder->write(
					    Matched_0_condition_1,
					    std::string{isImports ? "imports" : "exports"},
					    condition);
				}
				auto [subTarget, _] =
				    target.AsObject()->Get(condition);
				auto result = loadModuleFromTargetExportOrImport(
				    ext_, moduleName, scope, isImports, *subTarget,
				    subpath, isPattern, key);
				if (!shouldContinueSearching(result)) {
					if (result->isResolved() &&
					    traceBuilder != nullptr) {
						traceBuilder->write(
						    Resolved_under_condition_0,
						    condition);
					}
					if (traceBuilder != nullptr) {
						traceBuilder->write(
						    Exiting_conditional_exports);
					}
					return result;
				} else if (traceBuilder != nullptr) {
					traceBuilder->write(
					    Failed_to_resolve_under_condition_0,
					    condition);
				}
			} else {
				if (traceBuilder != nullptr) {
					traceBuilder->write(
					    Saw_non_matching_condition_0, condition);
				}
			}
		}
		if (traceBuilder != nullptr) {
			traceBuilder->write(Exiting_conditional_exports);
		}
		return nullptr;
	}
	case packagejson::JSONValueType::Array: {
		if (target.AsArray()->empty()) {
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    X_package_json_scope_0_has_invalid_type_for_target_of_specifier_1,
				    scope->PackageDirectory, moduleName);
			}
			return nullptr;
		}
		for (auto& elem : *target.AsArray()) {
			if (auto result = loadModuleFromTargetExportOrImport(
			        ext_, moduleName, scope, isImports, elem, subpath,
			        isPattern, key);
			    !shouldContinueSearching(result)) {
				return result;
			}
		}
		break;
	}

	case packagejson::JSONValueType::Null:
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    X_package_json_scope_0_explicitly_maps_specifier_1_to_null,
			    scope->PackageDirectory, moduleName);
		}
		return unresolved();
	default:;
	}

	if (traceBuilder != nullptr) {
		traceBuilder->write(
		    X_package_json_scope_0_has_invalid_type_for_target_of_specifier_1,
		    scope->PackageDirectory, moduleName);
	}
	return nullptr;
}

std::unique_ptr<resolved> resolutionState::tryLoadInputFileForPath(
    const std::string& finalPath, const std::string& entry,
    const std::string& packagePath, bool isImports) {
	// Replace any references to outputs for files in the program with the
	// input files to support package self-names used with outDir
	if (!isConfigLookup &&
	    (!compilerOptions->DeclarationDir.empty() ||
	     !compilerOptions->OutDir.empty()) &&
	    finalPath.find("/node_modules/") == std::string::npos &&
	    (compilerOptions->ConfigFilePath.empty() ||
	     tspath::containsPath(
	         tspath::getDirectoryPath(packagePath),
	         compilerOptions->ConfigFilePath,
	         tspath::ComparePathsOptions{
	             resolver->host->UseCaseSensitiveFileNames(),
	             std::string{resolver->host->GetCurrentDirectory()},
	         }))) {
		// Note: this differs from Strada's tryLoadInputFileForPath in that
		// it does not attempt to perform "guesses", instead requring a
		// clear root indicator.

		std::string rootDir;
		if (!compilerOptions->RootDir.empty()) {
			// A `rootDir` compiler option strongly indicates the root
			// location
			rootDir = compilerOptions->RootDir;
		} else if (!compilerOptions->ConfigFilePath.empty()) {
			// When no explicit rootDir is set, treat the config file's
			// directory as the project root, which establishes the common
			// source directory, so no other locations need to be checked.
			rootDir = tspath::getDirectoryPath(
			    compilerOptions->ConfigFilePath);
		} else {
			auto* diagnostic = newDiagnostic(
			    nullptr, TextRange{},
			    isImports
			        ? The_project_root_is_ambiguous_but_is_required_to_resolve_import_map_entry_0_in_file_1_Supply_the_rootDir_compiler_option_to_disambiguate
			        : The_project_root_is_ambiguous_but_is_required_to_resolve_export_map_entry_0_in_file_1_Supply_the_rootDir_compiler_option_to_disambiguate,
			    {entry.empty() ? "." : entry,  // replace empty string
			                                     // with `.` — the reverse
			                                     // of the operation done
			                                     // when entries are built
			     packagePath});
			diagnostics.push_back(diagnostic);
			return unresolved();
		}

		auto candidateDirectories =
		    getOutputDirectoriesForBaseDirectory(rootDir);
		for (auto& candidateDir : candidateDirectories) {
			if (tspath::containsPath(
			        candidateDir, finalPath,
			        tspath::ComparePathsOptions{
			            resolver->host->UseCaseSensitiveFileNames(),
			            std::string{
			                resolver->host->GetCurrentDirectory()},
			        })) {
				// The matched export is looking up something in either
				// the out declaration or js dir, now map the written path
				// back into the source dir and source extension
				std::string pathFragment;
				if (finalPath.size() > candidateDir.size()) {
					pathFragment = finalPath.substr(
					    candidateDir.size() + 1);  // +1 to also remove
					                               // directory separator
				}
				auto possibleInputBase =
				    tspath::combinePaths(rootDir, {pathFragment});
				std::vector<std::string_view> jsAndDtsExtensions = {
				    tspath::extensionMjs, tspath::extensionCjs,
				    tspath::extensionJs,  tspath::extensionJson,
				    tspath::extensionDmts, tspath::extensionDcts,
				    tspath::extensionDts};
				for (auto ext : jsAndDtsExtensions) {
					if (tspath::fileExtensionIs(possibleInputBase,
					                            ext)) {
						auto inputExts =
						    tspath::
						        getPossibleOriginalInputExtensionForExtension(
						            possibleInputBase);
						for (auto possibleExt : inputExts) {
							if (!extensionIsOk(this->ext, possibleExt)) {
								continue;
							}
							auto possibleInputWithInputExtension =
							    tspath::changeExtension(
							        possibleInputBase, possibleExt);
							if (resolver->host->FileExists(
							        possibleInputWithInputExtension)) {
								auto r =
								    loadFileNameFromPackageJSONField(
								        this->ext,
								        possibleInputWithInputExtension,
								        "");
								if (!shouldContinueSearching(r)) {
									return r;
								}
							}
						}
					}
				}
			}
		}
	}
	return nullptr;
}

std::vector<std::string>
resolutionState::getOutputDirectoriesForBaseDirectory(
    std::string_view commonSourceDirGuess) {
	// Config file output paths are processed to be relative to the host's
	// current directory, while otherwise the paths are resolved relative to
	// the common source dir the compiler puts together
	auto currentDir = !compilerOptions->ConfigFilePath.empty()
	                      ? resolver->host->GetCurrentDirectory()
	                      : std::string{commonSourceDirGuess};
	std::vector<std::string> candidateDirectories;
	if (!compilerOptions->DeclarationDir.empty()) {
		candidateDirectories.push_back(tspath::getNormalizedAbsolutePath(
		    tspath::combinePaths(currentDir,
		                         {compilerOptions->DeclarationDir}),
		    resolver->host->GetCurrentDirectory()));
	}
	if (!compilerOptions->OutDir.empty() &&
	    compilerOptions->OutDir != compilerOptions->DeclarationDir) {
		candidateDirectories.push_back(tspath::getNormalizedAbsolutePath(
		    tspath::combinePaths(currentDir, {compilerOptions->OutDir}),
		    resolver->host->GetCurrentDirectory()));
	}
	return candidateDirectories;
}

std::unique_ptr<resolved>
resolutionState::loadModuleFromNearestNodeModulesDirectory(
    bool typesScopeOnly) {
	auto mode = ResolutionModeCommonJS;
	if (esmMode || conditionMatches("import")) {
		mode = ResolutionModeESM;
	}
	// Do (up to) two passes through node_modules:
	//   1. For each ancestor node_modules directory, try to find:
	//      i.  TS/DTS files in the implementation package
	//      ii. DTS files in the @types package
	//   2. For each ancestor node_modules directory, try to find:
	//      i.  JS files in the implementation package
	auto priorityExtensions =
	    ext & (extensionsTypeScript | extensionsDeclaration);
	auto secondaryExtensions =
	    ext & ~(extensionsTypeScript | extensionsDeclaration);
	// (1)
	if (priorityExtensions != 0) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Searching_all_ancestor_node_modules_directories_for_preferred_extensions_Colon_0,
			    extensionsString(priorityExtensions));
		}
		if (auto result =
		        loadModuleFromNearestNodeModulesDirectoryWorker(
		            priorityExtensions, mode, typesScopeOnly);
		    !shouldContinueSearching(result)) {
			return result;
		}
	}
	// (2)
	if (secondaryExtensions != 0 && !typesScopeOnly) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Searching_all_ancestor_node_modules_directories_for_fallback_extensions_Colon_0,
			    extensionsString(secondaryExtensions));
		}
		return loadModuleFromNearestNodeModulesDirectoryWorker(
		    secondaryExtensions, mode, typesScopeOnly);
	}
	return nullptr;
}

std::unique_ptr<resolved>
resolutionState::loadModuleFromNearestNodeModulesDirectoryWorker(
    extensions ext_, ResolutionMode mode, bool typesScopeOnly) {
	(void)mode;  // unused, same as Go
	auto [result, _] = tspath::forEachAncestorDirectory<
	    std::unique_ptr<resolved>>(
	    containingDirectory,
	    [&](std::string_view directory)
	        -> std::pair<std::unique_ptr<resolved>, bool> {
		    // !!! stop at global cache
		    if (tspath::getBaseFileName(directory) != "node_modules") {
			    auto result =
			        loadModuleFromImmediateNodeModulesDirectory(
			            ext_, std::string{directory}, typesScopeOnly);
			    return {std::move(result),
			            !shouldContinueSearching(result)};
		    }
		    return {nullptr, false};
	    });
	return std::move(result);
}

std::unique_ptr<resolved>
resolutionState::loadModuleFromImmediateNodeModulesDirectory(
    extensions ext_, const std::string& directory, bool typesScopeOnly) {
	auto nodeModulesFolder =
	    tspath::combinePaths(directory, {"node_modules"});
	if (!resolver->host->DirectoryExists(nodeModulesFolder)) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Directory_0_does_not_exist_skipping_all_lookups_in_it,
			    nodeModulesFolder);
		}
		return nullptr;
	}

	if (!typesScopeOnly) {
		if (auto packageResult =
		        loadModuleFromSpecificNodeModulesDirectory(
		            ext_, name, nodeModulesFolder);
		    !shouldContinueSearching(packageResult)) {
			return packageResult;
		}
	}

	if ((ext_ & extensionsDeclaration) != 0) {
		auto nodeModulesAtTypes =
		    tspath::combinePaths(nodeModulesFolder, {"@types"});
		if (!resolver->host->DirectoryExists(nodeModulesAtTypes)) {
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    Directory_0_does_not_exist_skipping_all_lookups_in_it,
				    nodeModulesAtTypes);
			}
			return nullptr;
		}
		return loadModuleFromSpecificNodeModulesDirectory(
		    extensionsDeclaration, mangleScopedPackageName(name),
		    nodeModulesAtTypes);
	}

	return nullptr;
}

std::unique_ptr<resolved>
resolutionState::loadModuleFromSpecificNodeModulesDirectory(
    extensions ext_, std::string_view moduleName,
    const std::string& nodeModulesDirectory) {
	// Strip any trailing directory separator so that imports like `pkg/`
	// and `pkg` produce identical `candidate` and `packageDirectory`
	// strings. Otherwise the `package.json` info cache (which is keyed by
	// normalized path but stores the caller's `PackageDirectory` verbatim)
	// can hand back, under concurrent inserts, an entry whose
	// `PackageDirectory` doesn't match `candidate`, causing
	// `loadNodeModuleFromDirectoryWorker`'s `ComparePaths(candidate, ...)`
	// check to fail and skip loading the package's `main`/`types` entry.
	// https://github.com/microsoft/TypeScript/tsc/issues/3526
	auto candidate = std::string{tspath::removeTrailingDirectorySeparator(
	    tspath::normalizePath(
	        tspath::combinePaths(nodeModulesDirectory, {moduleName})))};
	auto [packageName, rest] = ParsePackageName(moduleName);
	const std::string restStr{rest};
	auto packageDirectory =
	    tspath::combinePaths(nodeModulesDirectory, {packageName});
	if (packageName.empty()) {
		packageDirectory = candidate;
	}

	if (resolvePackageDirectoryOnly) {
		if (resolver->host->DirectoryExists(packageDirectory)) {
			auto r = std::make_unique<resolved>();
			r->path = packageDirectory;
			return r;
		}
		return nullptr;
	}

	std::shared_ptr<packagejson::InfoCacheEntry> rootPackageInfo;
	// First look for a nested package.json, as in
	// `node_modules/foo/bar/package.json`
	auto packageInfo = getPackageJsonInfo(candidate);
	// But only if we're not respecting export maps (if we are, we might
	// redirect around this location)
	if (!rest.empty() && pkgExists(packageInfo)) {
		if ((features & NodeResolutionFeaturesExports) != 0) {
			rootPackageInfo = getPackageJsonInfo(packageDirectory);
		}
		if (!pkgExists(rootPackageInfo) ||
		    rootPackageInfo->Contents->Exports.type ==
		        packagejson::JSONValueType::NotPresent) {
			if (auto fromFile = loadModuleFromFile(ext_, candidate);
			    !shouldContinueSearching(fromFile)) {
				return fromFile;
			}

			if (auto fromDirectory =
			        loadNodeModuleFromDirectoryWorker(ext_, candidate,
			                                          packageInfo);
			    !shouldContinueSearching(fromDirectory)) {
				fromDirectory->packageId =
				    getPackageId(fromDirectory->path, packageInfo);
				return fromDirectory;
			}
		}
	}

	resolutionKindSpecificLoader loader =
	    [this, packageInfo, &restStr](
	        extensions extensions_,
	        const std::string& candidate_) -> std::unique_ptr<resolved> {
		if (!restStr.empty() || !esmMode) {
			if (auto fromFile =
			        loadModuleFromFile(extensions_, candidate_);
			    !shouldContinueSearching(fromFile)) {
				fromFile->packageId =
				    getPackageId(fromFile->path, packageInfo);
				return fromFile;
			}
		}
		if (auto fromDirectory = loadNodeModuleFromDirectoryWorker(
		        extensions_, candidate_, packageInfo);
		    !shouldContinueSearching(fromDirectory)) {
			fromDirectory->packageId =
			    getPackageId(fromDirectory->path, packageInfo);
			return fromDirectory;
		}
		if (restStr.empty() && pkgExists(packageInfo) &&
		    (packageInfo->Contents->Exports.type ==
		         packagejson::JSONValueType::NotPresent ||
		     packageInfo->Contents->Exports.type ==
		         packagejson::JSONValueType::Null) &&
		    esmMode) {
			// EsmMode disables index lookup in
			// `loadNodeModuleFromDirectoryWorker` generally, however
			// non-relative package resolutions still assume a default
			// `index.js` entrypoint if no `main` or `exports` are present
			if (auto indexResult = loadModuleFromFile(
			        extensions_,
			        tspath::combinePaths(candidate_, {"index.js"}));
			    !shouldContinueSearching(indexResult)) {
				indexResult->packageId =
				    getPackageId(indexResult->path, packageInfo);
				return indexResult;
			}
		}
		return nullptr;
	};

	if (!rest.empty()) {
		packageInfo = rootPackageInfo;
		if (packageInfo == nullptr) {
			// Previous `packageInfo` may have been from a nested
			// package.json; ensure we have the one from the package root
			// now.
			packageInfo = getPackageJsonInfo(packageDirectory);
		}
	}
	if (packageInfo != nullptr) {
		resolvedPackageDirectory = true;
		if ((features & NodeResolutionFeaturesExports) != 0 &&
		    pkgExists(packageInfo) &&
		    !packageInfo->Contents->Exports.IsFalsy()) {
			// package exports are higher priority than
			// file/directory/typesVersions lookups and (and, if there's
			// exports present*, blocks them)
			// *Well, weirdly enough a top-level `"exports": null` does NOT
			// block fallback resolution.
			// https://github.com/microsoft/TypeScript/pull/49327
			return loadModuleFromExports(
			    packageInfo, ext_,
			    tspath::combinePaths(".", {rest}));
		}
		if (!rest.empty() && pkgExists(packageInfo)) {
			auto* versionPaths =
			    packageInfo->Contents->GetVersionPaths(getTraceFunc());
			if (versionPaths->Exists()) {
				if (traceBuilder != nullptr) {
					traceBuilder->write(
					    X_package_json_has_a_typesVersions_entry_0_that_matches_compiler_version_1_looking_for_a_pattern_to_match_module_name_2,
					    versionPaths->Version,
					    std::string{version()}, std::string{rest});
				}
				auto pathPatterns =
				    TryParsePatterns(versionPaths->GetPaths());
				if (auto fromPaths = tryLoadModuleUsingPaths(
				        ext_, std::string{rest}, packageDirectory,
				        *versionPaths->GetPaths(),
				        pathPatterns.get(), loader);
				    !shouldContinueSearching(fromPaths)) {
					return fromPaths;
				}
			}
		}
	}
	return loader(ext_, candidate);
}

std::shared_ptr<ResolvedModule>
resolutionState::createResolvedModuleHandlingSymlink(
    std::unique_ptr<resolved> r) {
	bool isExternalLibraryImport =
	    r != nullptr &&
	    r->path.find("/node_modules/") != std::string::npos;
	if (compilerOptions->PreserveSymlinks != Tristate::True &&
	    isExternalLibraryImport && r->originalPath.empty() &&
	    !tspath::isExternalModuleNameRelative(name)) {
		auto [originalPath, resolvedFileName] =
		    getOriginalAndResolvedFileName(r->path);
		if (!originalPath.empty()) {
			r->path = resolvedFileName;
			r->originalPath = originalPath;
		}
	}
	return createResolvedModule(std::move(r), isExternalLibraryImport);
}

std::shared_ptr<ResolvedModule> resolutionState::createResolvedModule(
    std::unique_ptr<resolved> r, bool isExternalLibraryImport) {
	auto resolvedModule = std::make_shared<ResolvedModule>();
	resolvedModule->ResolutionDiagnostics = diagnostics;

	if (r != nullptr) {
		resolvedModule->ResolvedFileName = r->path;
		resolvedModule->OriginalPath = r->originalPath;
		resolvedModule->IsExternalLibraryImport = isExternalLibraryImport;
		resolvedModule->ResolvedUsingTsExtension =
		    r->resolvedUsingTsExtension;
		resolvedModule->ResolvedUsingExtraExtensions =
		    r->resolvedUsingExtraExtensions;
		resolvedModule->Extension = r->extension;
		resolvedModule->PackageId = r->packageId;
	}
	return resolvedModule;
}

std::shared_ptr<ResolvedTypeReferenceDirective>
resolutionState::createResolvedTypeReferenceDirective(
    std::unique_ptr<resolved> r, bool primary) {
	auto resolvedTypeReferenceDirective =
	    std::make_shared<ResolvedTypeReferenceDirective>();
	resolvedTypeReferenceDirective->ResolutionDiagnostics = diagnostics;

	if (r != nullptr && r->isResolved()) {
		if (!tspath::extensionIsTs(r->extension)) {
			fprintf(stderr,
			        "tsc internal error: expected a TypeScript file "
			        "extension\n");
			abort();
		}
		resolvedTypeReferenceDirective->ResolvedFileName = r->path;
		resolvedTypeReferenceDirective->Primary = primary;
		resolvedTypeReferenceDirective->PackageId = r->packageId;
		resolvedTypeReferenceDirective->IsExternalLibraryImport =
		    r->path.find("/node_modules/") != std::string::npos;

		if (compilerOptions->PreserveSymlinks != Tristate::True) {
			auto [originalPath, resolvedFileName] =
			    getOriginalAndResolvedFileName(r->path);
			if (!originalPath.empty()) {
				resolvedTypeReferenceDirective->ResolvedFileName =
				    resolvedFileName;
				resolvedTypeReferenceDirective->OriginalPath =
				    originalPath;
			}
		}
	}
	return resolvedTypeReferenceDirective;
}

std::pair<std::string, std::string>
resolutionState::getOriginalAndResolvedFileName(const std::string& fileName) {
	auto resolvedFileName = realPath(fileName);
	tspath::ComparePathsOptions comparePathsOptions;
	comparePathsOptions.useCaseSensitiveFileNames =
	    resolver->host->UseCaseSensitiveFileNames();
	comparePathsOptions.currentDirectory =
	    resolver->host->GetCurrentDirectory();
	if (tspath::comparePaths(fileName, resolvedFileName,
	                         comparePathsOptions) == 0) {
		// If the fileName and realpath are differing only in casing,
		// prefer fileName so that we can issue correct errors for casing
		// under forceConsistentCasingInFileNames
		return {"", fileName};
	}
	return {fileName, resolvedFileName};
}

std::unique_ptr<resolved>
resolutionState::tryLoadModuleUsingOptionalResolutionSettings() {
	if (auto r = tryLoadModuleUsingPathsIfEligible();
	    !shouldContinueSearching(r)) {
		return r;
	}

	if (!tspath::isExternalModuleNameRelative(name)) {
		// No more tryLoadModuleUsingBaseUrl.
		return nullptr;
	}
	return tryLoadModuleUsingRootDirs();
}

const ParsedPatterns* resolutionState::getParsedPatternsForPaths() {
	return resolver->getParsedPatternsForPaths(compilerOptions);
}

std::unique_ptr<resolved>
resolutionState::tryLoadModuleUsingPathsIfEligible() {
	if (!compilerOptions->Paths.empty() &&
	    !tspath::pathIsRelative(name)) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    X_paths_option_is_specified_looking_for_a_pattern_to_match_module_name_0,
			    name);
		}
	} else {
		return nullptr;
	}
	auto baseDirectory =
	    compilerOptions->GetPathsBasePath(
	        resolver->host->GetCurrentDirectory());
	auto* pathPatterns = getParsedPatternsForPaths();
	return tryLoadModuleUsingPaths(
	    ext, name, baseDirectory, compilerOptions->Paths, pathPatterns,
	    [this](extensions extensions_,
	           const std::string& candidate) -> std::unique_ptr<resolved> {
		    return nodeLoadModuleByRelativeName(extensions_, candidate,
		                                        true /*considerPackageJson*/);
	    });
}

template <typename MapLike>
std::unique_ptr<resolved> resolutionState::tryLoadModuleUsingPaths(
    extensions ext_, const std::string& moduleName,
    const std::string& containingDirectory_, const MapLike& paths,
    const ParsedPatterns* pathPatterns,
    const resolutionKindSpecificLoader& loader) {
	if (auto matchedPattern =
	        MatchPatternOrExact(pathPatterns, moduleName);
	    matchedPattern.isValid()) {
		auto matchedStar = matchedPattern.matchedText(moduleName);
		if (traceBuilder != nullptr) {
			traceBuilder->write(Module_name_0_matched_pattern_1,
			                    moduleName, matchedPattern.text);
		}
		for (auto& subst : pathsGetOrZero(paths, matchedPattern.text)) {
			auto path = replaceFirst(subst, "*", matchedStar);
			auto candidate = tspath::normalizePath(
			    tspath::combinePaths(containingDirectory_, {path}));
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    Trying_substitution_0_candidate_module_location_Colon_1,
				    subst, path);
			}
			// A path mapping may have an extension
			auto extensionFromSubst =
			    tspath::tryGetExtensionFromPath(subst);
			if (!extensionFromSubst.empty()) {
				if (auto [p, ok] = tryFile(candidate); ok) {
					auto r = std::make_unique<resolved>();
					r->path = p;
					r->extension =
					    std::string{extensionFromSubst};
					return r;
				}
			}
			// When the substitution path has an explicit extension, the
			// extension came from the paths config, not the module
			// specifier. Suppress resolvedUsingTsExtension in that case.
			auto saveCandidateEndingIsFromConfig =
			    candidateEndingIsFromConfig;
			if (!extensionFromSubst.empty()) {
				candidateEndingIsFromConfig = true;
			}
			auto r = loader(ext_, candidate);
			candidateEndingIsFromConfig = saveCandidateEndingIsFromConfig;
			if (!shouldContinueSearching(r)) {
				return r;
			}
		}
	}
	return nullptr;
}

std::unique_ptr<resolved> resolutionState::tryLoadModuleUsingRootDirs() {
	if (compilerOptions->RootDirs.empty()) {
		return nullptr;
	}

	if (traceBuilder != nullptr) {
		traceBuilder->write(
		    X_rootDirs_option_is_set_using_it_to_resolve_relative_module_name_0,
		    name);
	}

	auto candidate = tspath::normalizePath(
	    tspath::combinePaths(containingDirectory, {name}));

	std::string matchedRootDir;
	std::string matchedNormalizedPrefix;
	for (auto& rootDir : compilerOptions->RootDirs) {
		// rootDirs are expected to be absolute
		// in case of tsconfig.json this will happen automatically -
		// compiler will expand relative names using location of
		// tsconfig.json as base location
		auto normalizedRoot = tspath::normalizePath(rootDir);
		if (!normalizedRoot.ends_with("/")) {
			normalizedRoot += "/";
		}
		bool isLongestMatchingPrefix =
		    candidate.starts_with(normalizedRoot) &&
		    (matchedNormalizedPrefix.empty() ||
		     matchedNormalizedPrefix.size() < normalizedRoot.size());

		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Checking_if_0_is_the_longest_matching_prefix_for_1_2,
			    normalizedRoot, candidate, isLongestMatchingPrefix);
		}

		if (isLongestMatchingPrefix) {
			matchedNormalizedPrefix = normalizedRoot;
			matchedRootDir = rootDir;
		}
	}

	if (!matchedNormalizedPrefix.empty()) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(Longest_matching_prefix_for_0_is_1,
			                    candidate, matchedNormalizedPrefix);
		}
		auto suffix = candidate.substr(matchedNormalizedPrefix.size());

		// first - try to load from an initial location
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Loading_0_from_the_root_dir_1_candidate_location_2,
			    suffix, matchedNormalizedPrefix, candidate);
		}
		resolutionKindSpecificLoader loader =
		    [this](extensions extensions_, const std::string& candidate_)
		        -> std::unique_ptr<resolved> {
			return nodeLoadModuleByRelativeName(extensions_, candidate_,
			                                    true /*considerPackageJson*/);
		};
		if (auto resolvedFileName = loader(ext, candidate);
		    !shouldContinueSearching(resolvedFileName)) {
			return resolvedFileName;
		}

		if (traceBuilder != nullptr) {
			traceBuilder->write(Trying_other_entries_in_rootDirs);
		}
		// then try to resolve using remaining entries in rootDirs
		for (auto& rootDir : compilerOptions->RootDirs) {
			if (rootDir == matchedRootDir) {
				// skip the initially matched entry
				continue;
			}
			auto candidate2 = tspath::combinePaths(
			    tspath::normalizePath(rootDir), {suffix});
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    Loading_0_from_the_root_dir_1_candidate_location_2,
				    suffix, rootDir, candidate2);
			}
			if (auto resolvedFileName = loader(ext, candidate2);
			    !shouldContinueSearching(resolvedFileName)) {
				return resolvedFileName;
			}
		}
		if (traceBuilder != nullptr) {
			traceBuilder->write(Module_resolution_using_rootDirs_has_failed);
		}
	}
	return nullptr;
}

std::unique_ptr<resolved> resolutionState::nodeLoadModuleByRelativeName(
    extensions ext_, const std::string& candidate,
    bool considerPackageJson) {
	if (traceBuilder != nullptr) {
		traceBuilder->write(
		    Loading_module_as_file_Slash_folder_candidate_module_location_0_target_file_types_Colon_1,
		    candidate, extensionsString(ext_));
	}
	if (!tspath::hasTrailingDirectorySeparator(candidate)) {
		auto parentOfCandidate = tspath::getDirectoryPath(candidate);
		if (!resolver->host->DirectoryExists(parentOfCandidate)) {
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    Directory_0_does_not_exist_skipping_all_lookups_in_it,
				    parentOfCandidate);
			}
			return nullptr;
		}
		auto resolvedFromFile = loadModuleFromFile(ext_, candidate);
		if (resolvedFromFile != nullptr) {
			if (considerPackageJson) {
				if (auto packageDirectory = ParseNodeModuleFromPath(
				        resolvedFromFile->path, false /*isFolder*/);
				    !packageDirectory.empty()) {
					resolvedFromFile->packageId = getPackageId(
					    resolvedFromFile->path,
					    getPackageJsonInfo(packageDirectory));
				}
			}
			return resolvedFromFile;
		}
	}
	if (!resolver->host->DirectoryExists(candidate)) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    Directory_0_does_not_exist_skipping_all_lookups_in_it,
			    candidate);
		}
		return nullptr;
	}
	// esm mode relative imports shouldn't do any directory lookups (either
	// inside `package.json` files or implicit `index.js`es). This is a
	// notable departure from cjs norms, where `./foo/pkg` could have been
	// redirected by `./foo/pkg/package.json` to an arbitrary location!
	if (!esmMode) {
		return loadNodeModuleFromDirectory(ext_, candidate,
		                                 considerPackageJson);
	}
	return nullptr;
}

std::unique_ptr<resolved> resolutionState::loadModuleFromFile(
    extensions ext_, const std::string& candidate) {
	// ./foo.js -> ./foo.ts
	auto resolvedByReplacingExtension =
	    loadModuleFromFileNoImplicitExtensions(ext_, candidate);
	if (resolvedByReplacingExtension != nullptr) {
		return resolvedByReplacingExtension;
	}

	// ./foo -> ./foo.ts
	if (!esmMode) {
		return tryAddingExtensions(candidate, ext_, "");
	}

	return nullptr;
}

std::unique_ptr<resolved>
resolutionState::loadModuleFromFileNoImplicitExtensions(
    extensions ext_, const std::string& candidate) {
	auto base = tspath::getBaseFileName(candidate);
	if (base.find('.') == std::string_view::npos) {
		return nullptr;  // extensionless import, no lookups performed,
		                 // since we don't support extensionless files
	}
	auto extensionless = tspath::removeFileExtension(candidate);
	if (extensionless == candidate) {
		// Once TS native extensions are handled, handle arbitrary
		// extensions for declaration file mapping
		std::vector<std::string_view> extraExtensions(
		    resolver->extraExtensions.begin(),
		    resolver->extraExtensions.end());
		auto extension = tspath::getLongestExtensionFromPath(
		    candidate, extraExtensions, false);
		if (extension.empty()) {
			extension =
			    candidate.substr(candidate.find_last_of('.'));
		}
		extensionless = tspath::removeExtension(candidate, extension);
	}

	auto extension = candidate.substr(extensionless.size());
	if (traceBuilder != nullptr) {
		traceBuilder->write(File_name_0_has_a_1_extension_stripping_it,
		                    candidate, std::string{extension});
	}
	return tryAddingExtensions(std::string{extensionless}, ext_,
	                           extension);
}

std::unique_ptr<resolved> resolutionState::tryAddingExtensions(
    const std::string& extensionless, extensions ext_,
    std::string_view originalExtension) {
	auto directory = tspath::getDirectoryPath(extensionless);
	if (!directory.empty() &&
	    !resolver->host->DirectoryExists(directory)) {
		return nullptr;
	}

	if (originalExtension == tspath::extensionMjs ||
	    originalExtension == tspath::extensionMts ||
	    originalExtension == tspath::extensionDmts) {
		if ((ext_ & extensionsTypeScript) != 0) {
			if (auto r = tryExtension(
			        tspath::extensionMts, extensionless,
			        originalExtension == tspath::extensionMts ||
			            originalExtension == tspath::extensionDmts);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		if ((ext_ & extensionsDeclaration) != 0) {
			if (auto r = tryExtension(
			        tspath::extensionDmts, extensionless,
			        originalExtension == tspath::extensionMts ||
			            originalExtension == tspath::extensionDmts);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		if ((ext_ & extensionsJavaScript) != 0) {
			if (auto r = tryExtension(tspath::extensionMjs,
			                          extensionless, false);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		return nullptr;
	}
	if (originalExtension == tspath::extensionCjs ||
	    originalExtension == tspath::extensionCts ||
	    originalExtension == tspath::extensionDcts) {
		if ((ext_ & extensionsTypeScript) != 0) {
			if (auto r = tryExtension(
			        tspath::extensionCts, extensionless,
			        originalExtension == tspath::extensionCts ||
			            originalExtension == tspath::extensionDcts);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		if ((ext_ & extensionsDeclaration) != 0) {
			if (auto r = tryExtension(
			        tspath::extensionDcts, extensionless,
			        originalExtension == tspath::extensionCts ||
			            originalExtension == tspath::extensionDcts);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		if ((ext_ & extensionsJavaScript) != 0) {
			if (auto r = tryExtension(tspath::extensionCjs,
			                          extensionless, false);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		return nullptr;
	}
	if (originalExtension == tspath::extensionJson) {
		if ((ext_ & extensionsDeclaration) != 0) {
			if (auto r = tryExtension(".d.json.ts", extensionless,
			                          false);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		if ((ext_ & extensionsJson) != 0) {
			if (auto r = tryExtension(tspath::extensionJson,
			                          extensionless, false);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		return nullptr;
	}
	if (originalExtension == tspath::extensionTsx ||
	    originalExtension == tspath::extensionJsx) {
		// basically identical to the ts/js case below, but prefers
		// matching tsx and jsx files exactly before falling back to the
		// ts or js file path (historically, we disallow having both a a.ts
		// and a.tsx file in the same compilation, since their outputs
		// clash)
		// TODO: We should probably error if `"./a.tsx"` resolved to
		// `"./a.ts"`, right?
		if ((ext_ & extensionsTypeScript) != 0) {
			if (auto r = tryExtension(
			        tspath::extensionTsx, extensionless,
			        originalExtension == tspath::extensionTsx);
			    !shouldContinueSearching(r)) {
				return r;
			}
			if (auto r = tryExtension(
			        tspath::extensionTs, extensionless,
			        originalExtension == tspath::extensionTsx);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		if ((ext_ & extensionsDeclaration) != 0) {
			if (auto r = tryExtension(
			        tspath::extensionDts, extensionless,
			        originalExtension == tspath::extensionTsx);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		if ((ext_ & extensionsJavaScript) != 0) {
			if (auto r = tryExtension(tspath::extensionJsx,
			                          extensionless, false);
			    !shouldContinueSearching(r)) {
				return r;
			}
			if (auto r = tryExtension(tspath::extensionJs,
			                          extensionless, false);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		return nullptr;
	}
	if (originalExtension == tspath::extensionTs ||
	    originalExtension == tspath::extensionDts ||
	    originalExtension == tspath::extensionJs ||
	    originalExtension.empty()) {
		if ((ext_ & extensionsTypeScript) != 0) {
			if (auto r = tryExtension(
			        tspath::extensionTs, extensionless,
			        originalExtension == tspath::extensionTs ||
			            originalExtension == tspath::extensionDts);
			    !shouldContinueSearching(r)) {
				return r;
			}
			if (auto r = tryExtension(
			        tspath::extensionTsx, extensionless,
			        originalExtension == tspath::extensionTs ||
			            originalExtension == tspath::extensionDts);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		if ((ext_ & extensionsDeclaration) != 0) {
			if (auto r = tryExtension(
			        tspath::extensionDts, extensionless,
			        originalExtension == tspath::extensionTs ||
			            originalExtension == tspath::extensionDts);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		if ((ext_ & extensionsJavaScript) != 0) {
			if (auto r = tryExtension(tspath::extensionJs,
			                          extensionless, false);
			    !shouldContinueSearching(r)) {
				return r;
			}
			if (auto r = tryExtension(tspath::extensionJsx,
			                          extensionless, false);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		if (isConfigLookup) {
			if (auto r = tryExtension(tspath::extensionJson,
			                          extensionless, false);
			    !shouldContinueSearching(r)) {
				return r;
			}
		}
		return nullptr;
	}
	if (contains(resolver->extraExtensions, originalExtension)) {
		// A fully specified import of an extraExtension resolves directly
		// to the file.
		if (auto r =
		        tryExtension(originalExtension, extensionless, false);
		    !shouldContinueSearching(r)) {
			r->resolvedUsingExtraExtensions = true;
			return r;
		}
	}
	if ((ext_ & extensionsDeclaration) != 0 &&
	    !tspath::isDeclarationFileName(
	        extensionless + std::string{originalExtension})) {
		if (auto r = tryExtension(
		        ".d" + std::string{originalExtension} + ".ts",
		        extensionless, false);
		    !shouldContinueSearching(r)) {
			return r;
		}
	}
	return nullptr;
}

std::unique_ptr<resolved> resolutionState::tryExtension(
    std::string_view extension, const std::string& extensionless,
    bool resolvedUsingTsExtension_) {
	auto fileName = extensionless + std::string{extension};
	if (auto [path, ok] = tryFile(fileName); ok) {
		auto r = std::make_unique<resolved>();
		r->path = path;
		r->extension = std::string{extension};
		r->resolvedUsingTsExtension =
		    !candidateEndingIsFromConfig && resolvedUsingTsExtension_;
		return r;
	}
	return nullptr;
}

std::pair<std::string, bool> resolutionState::tryFile(
    const std::string& fileName) {
	if (compilerOptions->ModuleSuffixes.empty()) {
		return {fileName, tryFileLookup(fileName)};
	}

	auto ext = tspath::tryGetExtensionFromPath(fileName);
	auto fileNameNoExtension = tspath::removeExtension(fileName, ext);
	for (auto& suffix : compilerOptions->ModuleSuffixes) {
		auto path = std::string{fileNameNoExtension} + suffix +
		    std::string{ext};
		if (tryFileLookup(path)) {
			return {path, true};
		}
	}
	return {fileName, false};
}

bool resolutionState::tryFileLookup(const std::string& fileName) {
	if (resolver->host->FileExists(fileName)) {
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    File_0_exists_use_it_as_a_name_resolution_result,
			    fileName);
		}
		return true;
	}
	if (traceBuilder != nullptr) {
		traceBuilder->write(File_0_does_not_exist, fileName);
	}
	return false;
}

std::unique_ptr<resolved> resolutionState::loadNodeModuleFromDirectory(
    extensions ext_, const std::string& candidate,
    bool considerPackageJson) {
	std::shared_ptr<packagejson::InfoCacheEntry> packageInfo;
	if (considerPackageJson) {
		packageInfo = getPackageJsonInfo(candidate);
	}

	return loadNodeModuleFromDirectoryWorker(ext_, candidate, packageInfo);
}

std::unique_ptr<resolved> resolutionState::loadNodeModuleFromDirectoryWorker(
    extensions ext_, const std::string& candidate,
    std::shared_ptr<packagejson::InfoCacheEntry> packageInfo) {
	std::string packageFile;
	packagejson::VersionPaths* versionPaths = nullptr;
	if (pkgExists(packageInfo)) {
		versionPaths =
		    packageInfo->Contents->GetVersionPaths(getTraceFunc());
		tspath::ComparePathsOptions opts;
		opts.useCaseSensitiveFileNames =
		    resolver->host->UseCaseSensitiveFileNames();
		if (tspath::comparePaths(candidate, packageInfo->PackageDirectory,
		                         opts) == 0) {
			if (auto [file, ok] = getPackageFile(ext_, packageInfo); ok) {
				packageFile = file;
			}
		}
	}

	resolutionKindSpecificLoader loader =
	    [this, packageFile, packageInfo](
	        extensions extensions_,
	        const std::string& candidate_) -> std::unique_ptr<resolved> {
		if (auto fromFile = loadFileNameFromPackageJSONField(
		        extensions_, candidate_, packageFile);
		    !shouldContinueSearching(fromFile)) {
			return fromFile;
		}

		// Even if `extensions == extensionsDeclaration`, we can still look
		// up a .ts file as a result of package.json "types"
		// !!! should we not set this before the filename lookup above?
		auto expandedExtensions = extensions_;
		if (extensions_ == extensionsDeclaration) {
			expandedExtensions =
			    extensionsTypeScript | extensionsDeclaration;
		}

		// Disable `esmMode` for the resolution of the package path for
		// CJS-mode packages (so the `main` field can omit extensions)
		auto saveESMMode = esmMode;
		auto saveCandidateEndingIsFromConfig = candidateEndingIsFromConfig;
		candidateEndingIsFromConfig = true;
		if (pkgExists(packageInfo) &&
		    packageInfo->Contents->Type.Value != "module") {
			esmMode = false;
		}
		auto result = nodeLoadModuleByRelativeName(
		    expandedExtensions, candidate_, false /*considerPackageJson*/);
		esmMode = saveESMMode;
		candidateEndingIsFromConfig = saveCandidateEndingIsFromConfig;
		return result;
	};

	std::string indexPath;
	if (isConfigLookup) {
		indexPath = tspath::combinePaths(candidate, {"tsconfig"});
	} else {
		indexPath = tspath::combinePaths(candidate, {"index"});
	}

	if (versionPaths != nullptr && versionPaths->Exists() &&
	    (packageFile.empty() ||
	     tspath::containsPath(candidate, packageFile,
	                          tspath::ComparePathsOptions{}))) {
		std::string moduleName;
		if (!packageFile.empty()) {
			moduleName = tspath::getRelativePathFromDirectory(
			    candidate, packageFile, tspath::ComparePathsOptions{});
		} else {
			moduleName = tspath::getRelativePathFromDirectory(
			    candidate, indexPath, tspath::ComparePathsOptions{});
		}
		if (traceBuilder != nullptr) {
			traceBuilder->write(
			    X_package_json_has_a_typesVersions_entry_0_that_matches_compiler_version_1_looking_for_a_pattern_to_match_module_name_2,
			    versionPaths->Version, std::string{version()},
			    moduleName);
		}
		auto pathPatterns = TryParsePatterns(versionPaths->GetPaths());
		if (auto result = tryLoadModuleUsingPaths(
		        ext_, moduleName, candidate, *versionPaths->GetPaths(),
		        pathPatterns.get(), loader);
		    !shouldContinueSearching(result)) {
			if (!result->packageId.Name.empty()) {
				// !!! are these asserts really necessary?
				fprintf(stderr,
				        "tsc internal error: expected packageId to be "
				        "empty\n");
				abort();
			}
			return result;
		}
	}

	if (!packageFile.empty()) {
		if (auto packageFileResult = loader(ext_, packageFile);
		    !shouldContinueSearching(packageFileResult)) {
			if (!packageFileResult->packageId.Name.empty()) {
				// !!! are these asserts really necessary?
				fprintf(stderr,
				        "tsc internal error: expected packageId to be "
				        "empty\n");
				abort();
			}
			return packageFileResult;
		}
	}

	// ESM mode resolutions don't do package 'index' lookups
	if (!esmMode) {
		if (!resolver->host->DirectoryExists(candidate)) {
			return nullptr;
		}
		return loadModuleFromFile(ext_, indexPath);
	}
	return nullptr;
}

// loadFileNameFromPackageJSONField — only ever called with paths written in
// package.json files — never module specifiers written in source files — so
// it always allows the candidate to end with a TS extension (but will also
// try substituting a JS extension for a TS extension).
std::unique_ptr<resolved> resolutionState::loadFileNameFromPackageJSONField(
    extensions ext_, const std::string& candidate,
    std::string_view packageJSONValue) {
	if (((ext_ & extensionsTypeScript) != 0 &&
	     tspath::hasImplementationTSFileExtension(candidate)) ||
	    ((ext_ & extensionsDeclaration) != 0 &&
	     tspath::isDeclarationFileName(candidate))) {
		if (auto [path, ok] = tryFile(candidate); ok) {
			auto extension = tspath::tryExtractTSExtension(path);
			// resolvedUsingTsExtension should be true when the pattern
			// ends with * and the candidate file ends in a TS extension.
			// This means the * matched a TS extension from the module
			// specifier. For example:
			// - import "pkg/foo.ts" with pattern "./*" -> true
			// - import "pkg/foo.ts.omg" with pattern "./*.omg" -> true
			//   (star matched .ts)
			// - import "pkg/foo" with pattern "./*.ts" -> false
			//   (extension in pattern, not specifier)
			bool resolvedUsingTsExtension =
			    packageJSONValue.ends_with("*") && !extension.empty();
			auto r = std::make_unique<resolved>();
			r->path = path;
			r->extension = std::string{extension};
			r->resolvedUsingTsExtension = resolvedUsingTsExtension;
			return r;
		}
		return nullptr;
	}

	if (isConfigLookup && (ext_ & extensionsJson) != 0 &&
	    tspath::fileExtensionIs(candidate, tspath::extensionJson)) {
		if (auto [path, ok] = tryFile(candidate); ok) {
			auto r = std::make_unique<resolved>();
			r->path = path;
			r->extension = std::string{tspath::extensionJson};
			return r;
		}
	}

	return loadModuleFromFileNoImplicitExtensions(ext_, candidate);
}

std::pair<std::string, bool> resolutionState::getPackageFile(
    extensions ext_,
    std::shared_ptr<packagejson::InfoCacheEntry> packageInfo) {
	if (!pkgExists(packageInfo)) {
		return {"", false};
	}
	if (isConfigLookup) {
		return getPackageJSONPathField("tsconfig",
		                               &packageInfo->Contents->TSConfig,
		                               packageInfo->PackageDirectory);
	}
	if ((ext_ & extensionsDeclaration) != 0) {
		if (auto [packageFile, ok] = getPackageJSONPathField(
		        "typings", &packageInfo->Contents->Typings,
		        packageInfo->PackageDirectory);
		    ok) {
			return {packageFile, ok};
		}
		if (auto [packageFile, ok] = getPackageJSONPathField(
		        "types", &packageInfo->Contents->Types,
		        packageInfo->PackageDirectory);
		    ok) {
			return {packageFile, ok};
		}
	}
	if ((ext_ & (extensionsImplementationFiles | extensionsDeclaration)) !=
	    0) {
		return getPackageJSONPathField("main",
		                               &packageInfo->Contents->Main,
		                               packageInfo->PackageDirectory);
	}
	return {"", false};
}

std::shared_ptr<packagejson::InfoCacheEntry>
resolutionState::getPackageJsonInfo(const std::string& packageDirectory) {
	auto packageJsonPath =
	    tspath::combinePaths(packageDirectory, {"package.json"});

	if (auto existing =
	        resolver->packageJsonInfoCache->Get(packageJsonPath);
	    existing != nullptr) {
		if (existing->Contents != nullptr) {
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    File_0_exists_according_to_earlier_cached_lookups,
				    packageJsonPath);
			}
			return existing->WithPackageDirectory(packageDirectory);
		}
		if (existing->DirectoryExists && traceBuilder != nullptr) {
			traceBuilder->write(
			    File_0_does_not_exist_according_to_earlier_cached_lookups,
			    packageJsonPath);
		}
		return nullptr;
	}

	bool directoryExists =
	    resolver->host->DirectoryExists(packageDirectory);
	if (directoryExists &&
	    resolver->host->FileExists(packageJsonPath)) {
		// Ignore error
		auto contents = resolver->host->ReadFile(packageJsonPath);
		auto [packageJsonContent, parseOk] =
		    packagejson::Parse(contents.value_or(""));
		if (traceBuilder != nullptr) {
			traceBuilder->write(Found_package_json_at_0,
			                    packageJsonPath);
		}
		auto result = std::make_shared<packagejson::InfoCacheEntry>();
		result->PackageDirectory = packageDirectory;
		result->DirectoryExists = true;
		result->Contents = std::make_shared<packagejson::PackageJson>();
		static_cast<packagejson::Fields&>(*result->Contents) =
		    packageJsonContent;
		result->Contents->Parseable = parseOk;
		result = resolver->packageJsonInfoCache->Set(packageJsonPath,
		                                             result);
		return result->WithPackageDirectory(packageDirectory);
	}
	if (directoryExists && traceBuilder != nullptr) {
		traceBuilder->write(File_0_does_not_exist, packageJsonPath);
	}
	auto absent = std::make_shared<packagejson::InfoCacheEntry>();
	absent->PackageDirectory = packageDirectory;
	absent->DirectoryExists = directoryExists;
	resolver->packageJsonInfoCache->Set(packageJsonPath, absent);
	return nullptr;
}

PackageId resolutionState::getPackageId(
    const std::string& resolvedFileName,
    std::shared_ptr<packagejson::InfoCacheEntry> packageInfo) {
	if (pkgExists(packageInfo)) {
		auto* packageJsonContent = packageInfo->Contents.get();
		if (auto [name, ok] = packageJsonContent->Name.GetValue(); ok) {
			if (auto [version, ok2] =
			        packageJsonContent->Version.GetValue();
			    ok2) {
				std::string subModuleName;
				if (resolvedFileName.size() >
				    packageInfo->PackageDirectory.size()) {
					subModuleName = resolvedFileName.substr(
					    packageInfo->PackageDirectory.size() + 1);
				}
				return PackageId{
				    name, subModuleName, version,
				    readPackageJsonPeerDependencies(
				        packageInfo)};
			}
		}
	}
	return PackageId{};
}

std::string resolutionState::readPackageJsonPeerDependencies(
    std::shared_ptr<packagejson::InfoCacheEntry> packageJsonInfo) {
	auto peerDependencies =
	    packageJsonInfo->Contents->PeerDependencies;
	if (!validatePackageJSONField("peerDependencies", &peerDependencies) ||
	    peerDependencies.Value.empty()) {
		return "";
	}
	if (traceBuilder != nullptr) {
		traceBuilder->write(X_package_json_has_a_peerDependencies_field);
	}
	auto packageDirectory = realPath(packageJsonInfo->PackageDirectory);
	auto nodeModulesIndex = packageDirectory.rfind("/node_modules");
	if (nodeModulesIndex == std::string::npos) {
		return "";
	}
	auto nodeModules =
	    packageDirectory.substr(
	        0, nodeModulesIndex + std::string{"/node_modules"}.size()) +
	    "/";
	std::vector<std::string> names;
	names.reserve(peerDependencies.Value.size());
	for (auto& [n, _] : peerDependencies.Value) {
		names.push_back(n);
	}
	std::sort(names.begin(), names.end());
	std::string builder;
	for (auto& name_ : names) {
		auto peerPackageJson =
		    getPackageJsonInfo(nodeModules + name_);
		if (pkgExists(peerPackageJson)) {
			auto version = peerPackageJson->Contents->Version.Value;
			builder += "+";
			builder += name_;
			builder += "@";
			builder += version;
			if (traceBuilder != nullptr) {
				traceBuilder->write(
				    Found_peerDependency_0_with_1_version, name_,
				    version);
			}
		} else if (traceBuilder != nullptr) {
			traceBuilder->write(Failed_to_find_peerDependency_0, name_);
		}
	}
	return builder;
}

std::string resolutionState::realPath(std::string_view path) {
	auto rp =
	    tspath::normalizePath(resolver->host->Realpath(path));
	if (traceBuilder != nullptr) {
		traceBuilder->write(Resolving_real_path_for_0_result_1,
		                    std::string{path}, rp);
	}
	return rp;
}

bool resolutionState::conditionMatches(std::string_view condition) {
	if (condition == "default" || contains(conditions, condition)) {
		return true;
	}
	if (!contains(conditions, "types")) {
		return false;  // only apply versioned types conditions if the types
		               // condition is applied
	}
	return IsApplicableVersionedTypesKey(condition);
}

std::function<void(const DiagnosticMessage*,
                   const std::vector<std::string>&)>
resolutionState::getTraceFunc() {
	if (traceBuilder != nullptr) {
		return [this](const DiagnosticMessage* m,
		              const std::vector<std::string>& args) {
			// resolver.go:1911 — the Go trace callback takes args ...any;
			// splat the collected vector instead of storing it as one arg.
			std::vector<std::any> anyArgs;
			anyArgs.reserve(args.size());
			for (const auto& a : args) {
				anyArgs.emplace_back(a);
			}
			traceBuilder->traces.push_back({m, std::move(anyArgs)});
		};
	}
	return {};
}

// --- resolver.go: top-level helpers ---

std::vector<std::string> GetConditions(const CompilerOptions& options,
                                       ResolutionMode resolutionMode) {
	auto moduleResolution = options.GetModuleResolutionKind();
	if (resolutionMode == ModuleKind::None &&
	    moduleResolution == ModuleResolutionKind::Bundler) {
		resolutionMode = ModuleKind::ESNext;
	}
	std::vector<std::string> conditions;
	conditions.reserve(3 + options.CustomConditions.size());
	if (resolutionMode == ModuleKind::ESNext) {
		conditions.push_back("import");
	} else {
		conditions.push_back("require");
	}

	if (options.NoDtsResolution != Tristate::True) {
		conditions.push_back("types");
	}
	if (moduleResolution != ModuleResolutionKind::Bundler) {
		conditions.push_back("node");
	}
	conditions.insert(conditions.end(), options.CustomConditions.begin(),
	                  options.CustomConditions.end());
	return conditions;
}

NodeResolutionFeatures getNodeResolutionFeatures(
    const CompilerOptions& options) {
	auto features = NodeResolutionFeaturesNone;

	switch (options.GetModuleResolutionKind()) {
	case ModuleResolutionKind::Node16:
		features = NodeResolutionFeaturesNode16Default;
		break;
	case ModuleResolutionKind::NodeNext:
		features = NodeResolutionFeaturesNodeNextDefault;
		break;
	case ModuleResolutionKind::Bundler:
		features = NodeResolutionFeaturesBundlerDefault;
		break;
	default:;
	}
	if (options.ResolvePackageJsonExports == Tristate::True) {
		features |= NodeResolutionFeaturesExports;
	} else if (options.ResolvePackageJsonExports == Tristate::False) {
		features &= ~NodeResolutionFeaturesExports;
	}
	if (options.ResolvePackageJsonImports == Tristate::True) {
		features |= NodeResolutionFeaturesImports;
	} else if (options.ResolvePackageJsonImports == Tristate::False) {
		features &= ~NodeResolutionFeaturesImports;
	}
	return features;
}

int moveToNextDirectorySeparatorIfAvailable(std::string_view path,
                                            int prevSeparatorIndex,
                                            bool isFolder) {
	size_t offset = static_cast<size_t>(prevSeparatorIndex) + 1;
	long nextSeparatorIndex = -1;
	if (offset <= path.size()) {
		auto idx = path.substr(offset).find('/');
		nextSeparatorIndex =
		    idx == std::string_view::npos
		        ? -1
		        : static_cast<long>(idx);
	}
	if (nextSeparatorIndex == -1) {
		if (isFolder) {
			return static_cast<int>(path.size());
		}
		return prevSeparatorIndex;
	}
	return static_cast<int>(nextSeparatorIndex + offset);
}

const ParsedPatterns* DefaultResolver::getParsedPatternsForPaths(
    const CompilerOptions* options) {
	// Go keys the cache on the paths map object itself; the C++ port stores
	// CompilerOptions::Paths inline, so the member's address plays the same
	// role.
	return parsedPatternsForPaths
	    .Get(static_cast<const void*>(&options->Paths),
	         TryParsePatterns(options->Paths))
	    .get();
}

namespace {
template <typename Keys>
std::shared_ptr<ParsedPatterns> tryParsePatternsImpl(const Keys& paths) {
	size_t numPatterns = 0;
	for (auto& path : paths) {
		if (auto pattern = tryParsePattern(path); pattern.isValid()) {
			if (pattern.starIndex != -1) {
				numPatterns++;
			}
		}
	}

	auto result = std::make_shared<ParsedPatterns>();
	result->patterns.reserve(numPatterns);
	
	for (auto& path : paths) {
		if (auto pattern = tryParsePattern(path); pattern.isValid()) {
			if (pattern.starIndex == -1) {
				result->matchableStringSet.Add(std::string{path});
			} else {
				result->patterns.push_back(pattern);
			}
		}
	}
	return result;
}
}  // namespace

std::shared_ptr<ParsedPatterns> TryParsePatterns(
    const collections::OrderedMap<std::string, std::vector<std::string>>*
        pathMappings) {
	return tryParsePatternsImpl(pathMappings->Keys());
}

std::shared_ptr<ParsedPatterns> TryParsePatterns(
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        pathMappings) {
	std::vector<std::string_view> keys;
	keys.reserve(pathMappings.size());
	for (auto& [k, _] : pathMappings) keys.push_back(k);
	return tryParsePatternsImpl(keys);
}

Pattern MatchPatternOrExact(const ParsedPatterns* patterns,
                            std::string_view candidate) {
	if (patterns->matchableStringSet.Has(std::string{candidate})) {
		Pattern p;
		p.text = std::string{candidate};
		p.starIndex = -1;
		return p;
	}
	if (patterns->patterns.empty()) {
		return Pattern{};
	}
	static Pattern (*identity)(const Pattern&) =
	    [](const Pattern& p) -> Pattern { return p; };
	auto* best = findBestPatternMatch(patterns->patterns, identity,
	                                  candidate);
	return best != nullptr ? *best : Pattern{};
}

// normalizePathForCJSResolution — resolver.go. `import "."` inside `/foo`
// must keep the trailing separator so we look inside `foo`.
std::string normalizePathForCJSResolution(
    std::string_view containingDirectory, std::string_view moduleName) {
	auto combined = tspath::combinePaths(containingDirectory, {moduleName});
	auto parts = tspath::getPathComponents(combined, "");
	auto& lastPart = parts.back();
	if (lastPart == "." || lastPart == "..") {
		return tspath::ensureTrailingDirectorySeparator(
		    tspath::normalizePath(combined));
	}
	return tspath::normalizePath(combined);
}

bool matchesPatternWithTrailer(std::string_view target,
                               std::string_view name_) {
	if (target.ends_with("*")) {
		return false;
	}
	auto star = target.find('*');
	if (star == std::string_view::npos) {
		return false;
	}
	auto before = target.substr(0, star);
	auto after = target.substr(star + 1);
	return name_.starts_with(before) && name_.ends_with(after);
}

// extensionIsOk — resolver.go.
bool extensionIsOk(extensions ext_, std::string_view extension) {
	return ((ext_ & extensionsJavaScript) != 0 &&
	        (extension == tspath::extensionJs ||
	         extension == tspath::extensionJsx ||
	         extension == tspath::extensionMjs ||
	         extension == tspath::extensionCjs)) ||
	       ((ext_ & extensionsTypeScript) != 0 &&
	        (extension == tspath::extensionTs ||
	         extension == tspath::extensionTsx ||
	         extension == tspath::extensionMts ||
	         extension == tspath::extensionCts)) ||
	       ((ext_ & extensionsDeclaration) != 0 &&
	        (extension == tspath::extensionDts ||
	         extension == tspath::extensionDmts ||
	         extension == tspath::extensionDcts)) ||
	       ((ext_ & extensionsJson) != 0 &&
	        extension == tspath::extensionJson);
}

std::shared_ptr<ResolvedModule> ResolveConfig(std::string_view moduleName,
                                              std::string_view containingFile,
                                              ResolutionHost* host) {
	auto* options = new CompilerOptions();
	options->ModuleResolution = ModuleResolutionKind::NodeNext;
	// Leaked intentionally — the resolver (and its caches) hold a pointer
	// for the program's lifetime, mirroring Go's GC'd options.
	auto* resolver = new DefaultResolver(ResolverOptions{
	    host, options, "", "", {}, nullptr});
	return resolver->resolveConfig(std::string{moduleName},
	                               std::string{containingFile});
}

std::vector<std::string> GetAutomaticTypeDirectiveNames(
    const CompilerOptions& options, ResolutionHost* host) {
	if (!options.UsesWildcardTypes()) {
		if (!options.Types.empty()) {
			return options.Types;
		}
		return {};
	}

	// Walk the primary type lookup locations
	std::vector<std::string> wildcardMatches;
	auto [typeRoots, _] =
	    options.GetEffectiveTypeRoots(host->GetCurrentDirectory());
	for (auto& root : typeRoots) {
		if (host->DirectoryExists(root)) {
			for (auto& typeDirectivePath :
			     host->GetAccessibleEntries(root).directories) {
				auto normalized =
				    tspath::normalizePath(typeDirectivePath);
				auto packageJsonPath = tspath::combinePaths(
				    root, {normalized, "package.json"});
				bool isNotNeededPackage = false;
				if (host->FileExists(packageJsonPath)) {
					auto contents =
					    host->ReadFile(packageJsonPath);
					auto [packageJsonContent, _ok] =
					    packagejson::Parse(
					        contents.value_or(""));
					// `types-publisher` sometimes creates packages with
					// `"typings": null` for packages that don't provide
					// their own types. See `createNotNeededPackageJSON`
					// in the types-publisher` repo.
					isNotNeededPackage =
					    packageJsonContent.Typings.Null;
				}
				if (!isNotNeededPackage) {
					auto baseFileName =
					    tspath::getBaseFileName(normalized);
					if (!baseFileName.starts_with(".")) {
						wildcardMatches.emplace_back(baseFileName);
					}
				}
			}
		}
	}

	// Order potentially matters in program construction, so substitute in
	// the wildcard in the position it was specified in the types array
	std::vector<std::string> result;
	for (auto& t : options.Types) {
		if (t == "*") {
			result.insert(result.end(), wildcardMatches.begin(),
			              wildcardMatches.end());
		} else {
			result.push_back(t);
		}
	}
	// core.Deduplicate
	std::vector<std::string> deduped;
	std::unordered_set<std::string> seen;
	for (auto& s : result) {
		if (seen.insert(s).second) deduped.push_back(s);
	}
	return deduped;
}

// --- resolver.go: entrypoints ---

std::unique_ptr<ResolvedEntrypoint>
DefaultResolver::createResolvedEntrypointHandlingSymlink(
    const std::string& fileName, const std::string& moduleSpecifier,
    const collections::Set<std::string>* includeConditions,
    const collections::Set<std::string>* excludeConditions, Ending ending) {
	std::string originalFileName;
	auto resolvedFileName = fileName;
	if (auto realPath = host->Realpath(fileName); realPath != fileName) {
		originalFileName = fileName;
		resolvedFileName = realPath;
	}
	auto e = std::make_unique<ResolvedEntrypoint>();
	e->OriginalFileName = originalFileName;
	e->ResolvedFileName = resolvedFileName;
	e->ModuleSpecifier = moduleSpecifier;
	if (includeConditions != nullptr) {
		e->IncludeConditions = std::make_unique<collections::Set<std::string>>(
		    *includeConditions);
	}
	if (excludeConditions != nullptr) {
		e->ExcludeConditions = std::make_unique<collections::Set<std::string>>(
		    *excludeConditions);
	}
	e->Ending_ = ending;
	return e;
}

std::vector<std::unique_ptr<ResolvedEntrypoint>>
DefaultResolver::GetEntrypointsFromPackageJsonInfo(
    std::shared_ptr<packagejson::InfoCacheEntry> packageJson,
    const std::string& packageName, bool enableDirectorySearch) {
	extensions exts = extensionsTypeScript | extensionsDeclaration;
	auto features = NodeResolutionFeaturesAll;
	resolutionState state;
	state.resolver = this;
	state.ext = exts;
	state.features = features;
	state.compilerOptions = compilerOptions;
	if (pkgExists(packageJson) &&
	    packageJson->Contents->Exports.IsPresent()) {
		return state.loadEntrypointsFromExportMap(
		    packageJson, packageName, packageJson->Contents->Exports);
	}

	std::vector<std::unique_ptr<ResolvedEntrypoint>> result;
	auto mainResolution = state.loadNodeModuleFromDirectoryWorker(
	    exts, packageJson->PackageDirectory, packageJson);

	if (mainResolution != nullptr && mainResolution->isResolved()) {
		result.push_back(createResolvedEntrypointHandlingSymlink(
		    mainResolution->path, packageName, nullptr, nullptr,
		    Ending::Fixed));
	}

	if (enableDirectorySearch) {
		auto otherFiles = vfsmatch::ReadDirectory(
		    host, host->GetCurrentDirectory(),
		    packageJson->PackageDirectory, extensionsArray(exts),
		    {"node_modules"}, {"**/*"}, vfsmatch::UnlimitedDepth);

		tspath::ComparePathsOptions comparePathsOptions;
		comparePathsOptions.useCaseSensitiveFileNames =
		    host->UseCaseSensitiveFileNames();
		for (auto& file : otherFiles) {
			if (mainResolution != nullptr && mainResolution->isResolved() &&
			    tspath::comparePaths(file, mainResolution->path,
			                         comparePathsOptions) == 0) {
				continue;
			}

			result.push_back(createResolvedEntrypointHandlingSymlink(
			    file,
			    tspath::resolvePath(
			        packageName,
			        {tspath::getRelativePathFromDirectory(
			            packageJson->PackageDirectory, file,
			            comparePathsOptions)}),
			    nullptr, nullptr, Ending::Changeable));
		}
	}

	return result;
}

std::vector<std::unique_ptr<ResolvedEntrypoint>>
resolutionState::loadEntrypointsFromExportMap(
    std::shared_ptr<packagejson::InfoCacheEntry> packageJson,
    const std::string& packageName,
    const packagejson::ExportsOrImports& exports) {
	std::vector<std::unique_ptr<ResolvedEntrypoint>> entrypoints;

	// Sets are shared/immutable in Go (cloned before mutation); model as
	// shared_ptr<const Set>.
	using SetPtr = std::shared_ptr<collections::Set<std::string>>;
	auto cloneSet = [](const SetPtr& s) -> SetPtr {
		if (s == nullptr) {
			return std::make_shared<collections::Set<std::string>>();
		}
		return std::make_shared<collections::Set<std::string>>(*s);
	};

	std::function<void(const std::string&, const SetPtr&, const SetPtr&,
	                   const packagejson::ExportsOrImports&)>
	    loadEntrypointsFromTargetExports =
	        [&](const std::string& subpath, const SetPtr& includeConditions,
	            const SetPtr& excludeConditions,
	            const packagejson::ExportsOrImports& exports_) {
		if (exports_.type == packagejson::JSONValueType::String &&
		    exports_.AsString().starts_with("./")) {
			if (exports_.AsString().find('*') != std::string::npos) {
				if (exports_.AsString().find('*') !=
				    exports_.AsString().rfind('*')) {
					return;
				}
				auto patternPath = tspath::resolvePath(
				    packageJson->PackageDirectory,
				    {exports_.AsString()});
				auto starPos = patternPath.find('*');
				auto leadingSlice = patternPath.substr(0, starPos);
				auto trailingSlice = patternPath.substr(starPos + 1);
				bool caseSensitive =
				    resolver->host->UseCaseSensitiveFileNames();
				auto files = vfsmatch::ReadDirectory(
				    resolver->host,
				    resolver->host->GetCurrentDirectory(),
				    packageJson->PackageDirectory,
				    extensionsArray(ext),
				    {},
				    {tspath::changeFullExtension(
				        replaceFirst(exports_.AsString(), "*", "**/*"),
				        ".*")},
				    vfsmatch::UnlimitedDepth);
				for (auto& file : files) {
					auto [matchedStar, ok] =
					    getMatchedStarForPatternEntrypoint(
					        file, leadingSlice, trailingSlice,
					        caseSensitive);
					if (!ok) {
						continue;
					}
					auto moduleSpecifier = tspath::resolvePath(
					    packageName,
					    {replaceFirst(subpath, "*", matchedStar)});
					entrypoints.push_back(
					    resolver
					        ->createResolvedEntrypointHandlingSymlink(
					            file, moduleSpecifier,
					            includeConditions.get(),
					            excludeConditions.get(),
					            exports_.AsString().ends_with("*")
					                ? Ending::ExtensionChangeable
					                : Ending::Fixed));
				}
			} else {
				auto pc =
				    tspath::getPathComponents(exports_.AsString(), "");
				std::vector<std::string> partsAfterFirst(pc.begin() + 2,
				                                         pc.end());
				if (contains(partsAfterFirst, "..") ||
				    contains(partsAfterFirst, ".") ||
				    contains(partsAfterFirst, "node_modules")) {
					return;
				}
				auto resolvedTarget = tspath::resolvePath(
				    packageJson->PackageDirectory,
				    {exports_.AsString()});
				if (auto result = loadFileNameFromPackageJSONField(
				        ext, resolvedTarget, exports_.AsString());
				    result != nullptr && result->isResolved()) {
					entrypoints.push_back(
					    resolver
					        ->createResolvedEntrypointHandlingSymlink(
					            result->path,
					            tspath::resolvePath(packageName,
					                                {subpath}),
					            includeConditions.get(),
					            excludeConditions.get(),
					            exports_.AsString().ends_with("*")
					                ? Ending::ExtensionChangeable
					                : Ending::Fixed));
				}
			}
		} else if (exports_.type == packagejson::JSONValueType::Array) {
			for (auto& element : *exports_.AsArray()) {
				loadEntrypointsFromTargetExports(subpath,
				                                 includeConditions,
				                                 excludeConditions, element);
			}
		} else if (exports_.type == packagejson::JSONValueType::Object) {
			std::vector<std::string> prevConditions;
			for (auto& condition : exports_.AsObject()->Keys()) {
				auto [export_, _g] =
				    exports_.AsObject()->Get(condition);
				if (excludeConditions != nullptr &&
				    excludeConditions->Has(condition)) {
					continue;
				}

				bool conditionAlwaysMatches =
				    condition == "default" || condition == "types" ||
				    IsApplicableVersionedTypesKey(condition);
				auto newIncludeConditions = includeConditions;
				auto newExcludeConditions = excludeConditions;
				if (!conditionAlwaysMatches) {
					newIncludeConditions =
					    cloneSet(includeConditions);
					newExcludeConditions =
					    cloneSet(excludeConditions);
					newIncludeConditions->Add(condition);
					for (auto& prevCondition : prevConditions) {
						newExcludeConditions->Add(prevCondition);
					}
				}

				prevConditions.push_back(condition);
				loadEntrypointsFromTargetExports(
				    subpath, newIncludeConditions,
				    newExcludeConditions, *export_);
				if (conditionAlwaysMatches) {
					break;
				}
			}
		}
	};

	switch (exports.type) {
	case packagejson::JSONValueType::Array:
		for (auto& element : *exports.AsArray()) {
			loadEntrypointsFromTargetExports(".", nullptr, nullptr,
			                                 element);
		}
		break;
	case packagejson::JSONValueType::Object:
		if (exports.IsSubpaths()) {
			for (auto& subpath : exports.AsObject()->Keys()) {
				auto [export_, _g] =
				    exports.AsObject()->Get(subpath);
				loadEntrypointsFromTargetExports(subpath, nullptr,
				                                 nullptr, *export_);
			}
		} else {
			loadEntrypointsFromTargetExports(".", nullptr, nullptr,
			                                 exports);
		}
		break;
	default:
		loadEntrypointsFromTargetExports(".", nullptr, nullptr, exports);
	}

	return entrypoints;
}

std::pair<std::string, bool>
resolutionState::getMatchedStarForPatternEntrypoint(
    const std::string& file, const std::string& leadingSlice,
    const std::string& trailingSlice, bool caseSensitive) {
	if (stringutil::HasPrefixAndSuffixWithoutOverlap(
	        file, leadingSlice, trailingSlice, caseSensitive)) {
		return {file.substr(leadingSlice.size(),
		                    file.size() - leadingSlice.size() -
		                        trailingSlice.size()),
		        true};
	}

	if (auto jsExtension =
	        TryGetJSExtensionForFile(file, *compilerOptions);
	    !jsExtension.empty()) {
		auto swapped = tspath::changeFullExtension(file, jsExtension);
		if (stringutil::HasPrefixAndSuffixWithoutOverlap(
		        swapped, leadingSlice, trailingSlice, caseSensitive)) {
			return {swapped.substr(leadingSlice.size(),
			                       swapped.size() - leadingSlice.size() -
			                           trailingSlice.size()),
			        true};
		}
	}

	return {"", false};
}


// GetResolutionDiagnostic — util.go:125 (ported with program slice).
const DiagnosticMessage* getResolutionDiagnostic(
    const CompilerOptions* options, const ResolvedModule& resolvedModule,
    SourceFile* file) {
	if (resolvedModule.ResolvedUsingExtraExtensions) {
		return nullptr;
	}

	const std::string& extension = resolvedModule.Extension;
	// Needing a resolution diagnostic means these two things are in conflict:
	// - the current file's syntax (e.g. needJsx || needAllowJs)
	// - the resolved file's extension (tsx, jsx, js, mjs, cjs, json or
	//   arbitrary extension)

	auto needJsx = [&]() -> const DiagnosticMessage* {
		if (options->Jsx != JsxEmit::None) {
			return nullptr;
		}
		return Module_0_was_resolved_to_1_but_jsx_is_not_set;
	};
	auto needAllowJs = [&]() -> const DiagnosticMessage* {
		if (options->GetAllowJS() ||
		    !options->DefaultIfUnknown(options->NoImplicitAny,
		                               options->Strict)) {
			return nullptr;
		}
		return Could_not_find_a_declaration_file_for_module_0_1_implicitly_has_an_any_type;
	};
	auto needResolveJsonModule = [&]() -> const DiagnosticMessage* {
		if (options->GetResolveJsonModule()) {
			return nullptr;
		}
		return Module_0_was_resolved_to_1_but_resolveJsonModule_is_not_used;
	};
	auto needAllowArbitraryExtensions = [&]() -> const DiagnosticMessage* {
		if (file->IsDeclarationFile ||
		    tristateIsTrue(options->AllowArbitraryExtensions)) {
			return nullptr;
		}
		return Module_0_was_resolved_to_1_but_allowArbitraryExtensions_is_not_set;
	};

	if (extension == tspath::extensionTs || extension == tspath::extensionDts ||
	    extension == tspath::extensionMts ||
	    extension == tspath::extensionDmts ||
	    extension == tspath::extensionCts ||
	    extension == tspath::extensionDcts) {
		return nullptr;
	}
	if (extension == tspath::extensionTsx) {
		return needJsx();
	}
	if (extension == tspath::extensionJsx) {
		if (auto* msg = needJsx()) return msg;
		return needAllowJs();
	}
	if (extension == tspath::extensionJs ||
	    extension == tspath::extensionMjs ||
	    extension == tspath::extensionCjs) {
		return needAllowJs();
	}
	if (extension == tspath::extensionJson) {
		return needResolveJsonModule();
	}
	return needAllowArbitraryExtensions();
}

}  // namespace tsc::module
