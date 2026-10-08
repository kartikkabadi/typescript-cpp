// moduleResolverFactory / programResolutionContext / callbackModuleResolver /
// compileModuleResolutionSpec / moduleResolutionError / handle*ModuleResolver* /
// handleResolveModuleName — module_resolution.go.
#include "internal/api/module_resolution.h"

#include <cstring>
#include <stdexcept>

#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/json/json.h"
#include "internal/locale/locale.h"
#include "internal/packagejson/packagejson.h"
#include "internal/tspath/tspath.h"

namespace tsc::api {

namespace {

// ModuleKind.String — modulekind_stringer_generated.go (stringer output;
// ported faithfully). Used by module_resolution.go error paths.
std::string moduleKindString(ModuleKind i) {
	static const char* names0[] = {"None",   "CommonJS", "AMD",    "UMD",
	                               "System", "ES2015",   "ES2020", "ES2022"};
	static const char* names1[] = {"ESNext", "Node16", "Node18", "Node20"};
	static const char* names2[] = {"NodeNext", "Preserve"};
	int v = static_cast<int32_t>(i);
	if (v >= 0 && v <= 7) return names0[v];
	if (v >= 99 && v <= 102) return names1[v - 99];
	if (v >= 199 && v <= 200) return names2[v - 199];
	return "ModuleKind(" + std::to_string(static_cast<int64_t>(v)) + ")";
}

// stringifyArgs — diagnostics.go:148 StringifyArgs. String args pass through;
// everything else formats like Go's fmt %v.
std::vector<std::string> stringifyArgs(const std::vector<std::any>& args) {
	std::vector<std::string> result;
	result.reserve(args.size());
	for (const std::any& arg : args) {
		const std::type_info& t = arg.type();
		if (t == typeid(std::string)) {
			result.push_back(std::any_cast<std::string>(arg));
		} else if (t == typeid(const char*)) {
			result.push_back(std::any_cast<const char*>(arg));
		} else if (t == typeid(std::string_view)) {
			result.emplace_back(std::any_cast<std::string_view>(arg));
		} else if (t == typeid(int)) {
			result.push_back(std::to_string(std::any_cast<int>(arg)));
		} else if (t == typeid(int64_t)) {
			result.push_back(std::to_string(std::any_cast<int64_t>(arg)));
		} else if (t == typeid(uint64_t)) {
			result.push_back(std::to_string(std::any_cast<uint64_t>(arg)));
		} else if (t == typeid(double)) {
			result.push_back(gostd::fmtArg(std::any_cast<double>(arg)).text);
		} else if (t == typeid(bool)) {
			result.push_back(std::any_cast<bool>(arg) ? "true" : "false");
		} else if (t == typeid(gostd::Error)) {
			auto e = std::any_cast<gostd::Error>(arg);
			result.push_back(e ? e->Error() : "<nil>");
		} else if (t == typeid(gostd::fmtArg)) {
			result.push_back(std::any_cast<gostd::fmtArg>(arg).text);
		} else {
			result.emplace_back("<unsupported>");
		}
	}
	return result;
}

// snapshotResolutionHost adapts a project::Snapshot to module::ResolutionHost
// (`Host: sd.snapshot` — module_resolution.go handleResolveModuleName).
struct snapshotResolutionHost : module::ResolutionHost {
	project::Snapshot* snapshot;
	explicit snapshotResolutionHost(project::Snapshot* s) : snapshot(s) {}
	bool FileExists(std::string_view path) override {
		return snapshot->FileExists(std::string(path));
	}
	bool DirectoryExists(std::string_view path) override {
		return snapshot->DirectoryExists(std::string(path));
	}
	std::optional<std::string> ReadFile(std::string_view path) override {
		auto pair = snapshot->ReadFile(std::string(path));
		if (!pair.second) return std::nullopt;
		return pair.first;
	}
	std::string Realpath(std::string_view path) override {
		return snapshot->FS()->Realpath(std::string(path));
	}
	std::string GetCurrentDirectory() override {
		return snapshot->GetCurrentDirectory();
	}
	bool UseCaseSensitiveFileNames() override {
		return snapshot->UseCaseSensitiveFileNames();
	}
	AccessibleEntries GetAccessibleEntries(std::string_view path) override {
		auto entries = snapshot->FS()->GetAccessibleEntries(std::string(path));
		return {std::move(entries.files), std::move(entries.directories),
		        std::move(entries.symlinks)};
	}
};

} // namespace

// NewResolver — module_resolution.go:43.
// Ownership: the returned resolver is freshly allocated; the program owns it.
// `fallback` is shared into the registered programResolutionContext so
// callback- and context-path resolutions share resolver caches as in Go.
std::pair<module::Resolver*, std::function<void()>>
moduleResolverFactory::NewResolver(const module::ResolverOptions& options) {
	auto opts = options;
	opts.CompilerOptions = registration->compilerOptions.get();
	std::shared_ptr<module::Resolver> fallback(module::NewResolver(opts));
	if (registration->resolveModuleNameCallback.empty()) {
		module::Resolver* resolver = fallback.get();
		std::shared_ptr<module::Resolver> owned;
		if (registration->resolutions) {
			owned = std::shared_ptr<module::Resolver>(
			    module::NewStaticResolver(fallback.get(),
			                              registration->resolutions.get()));
			resolver = owned.get();
		}
		auto keepAlive = [fallback = std::move(fallback),
		                  owned = std::move(owned)] {};
		return {resolver, std::move(keepAlive)};
	}
	uint64_t contextID = session->registerProgramResolutionContext(
	    fallback, opts, registration);
	auto* resolver = new callbackModuleResolver{};
	resolver->registration = registration.get();
	resolver->conn = conn;
	resolver->ctx = ctx;
	resolver->currentDirectory = currentDirectory;
	resolver->programResolutionContextID = contextID;
	resolver->fallbackResolver = fallback.get();
	module::Resolver* out = resolver;
	std::shared_ptr<module::Resolver> staticWrap;
	if (registration->resolutions) {
		staticWrap = std::shared_ptr<module::Resolver>(module::NewStaticResolver(
		    resolver, registration->resolutions.get()));
		out = staticWrap.get();
	}
	// The cleanup keeps the resolver wrappers alive until release, matching
	// Go's GC-managed lifetime tied to the program.
	std::shared_ptr<module::Resolver> cbOwned(resolver);
	return {out, [this, contextID, cbOwned = std::move(cbOwned),
	              staticWrap = std::move(staticWrap)] {
		session->releaseProgramResolutionContext(contextID);
	}};
}

// ResolveModuleName — module_resolution.go:70.
std::pair<std::shared_ptr<module::ResolvedModule>,
          std::vector<module::DiagAndArgs>>
callbackModuleResolver::ResolveModuleName(
    std::string_view moduleName, std::string_view containingFile,
    ResolutionMode resolutionMode,
    const module::ResolvedProjectReference* redirectedReference) {
	return resolveModuleName(
	    std::string(moduleName), std::string(containingFile),
	    tspath::getDirectoryPath(std::string(containingFile)), resolutionMode,
	    redirectedReference);
}

// ResolveModuleNameFromDirectory — module_resolution.go:80.
std::pair<std::shared_ptr<module::ResolvedModule>,
          std::vector<module::DiagAndArgs>>
callbackModuleResolver::ResolveModuleNameFromDirectory(
    std::string_view moduleName, std::string_view containingDirectory,
    ResolutionMode resolutionMode) {
	return resolveModuleName(std::string(moduleName),
	                         std::string(containingDirectory),
	                         std::string(containingDirectory), resolutionMode,
	                         nullptr);
}

// resolveModuleName — module_resolution.go:88. Go's third return value is an
// error; the module::Resolver iface has no error slot, so failures throw.
std::pair<std::shared_ptr<module::ResolvedModule>,
          std::vector<module::DiagAndArgs>>
callbackModuleResolver::resolveModuleName(
    const std::string& moduleName, const std::string& containingFile,
    const std::string& containingDirectory, ResolutionMode resolutionMode,
    const module::ResolvedProjectReference* redirectedReference) {
	ResolveModuleNameCallbackParams params;
	params.ModuleName = moduleName;
	params.ContainingDirectory = containingDirectory;
	if (snapshot != 0) {
		params.Snapshot = snapshot;
	}
	if (programResolutionContextID != 0) {
		params.InProgressSnapshot = programResolutionContextID;
	}
	params.ResolutionMode = api::ResolutionMode(resolutionMode);
	auto marshaled = json::marshal(params);
	if (!marshaled.second.empty()) {
		throw std::runtime_error(marshaled.second);
	}
	auto callResult = conn->Call(ctx, registration->resolveModuleNameCallback,
	                             marshaled.first);
	if (callResult.second) {
		throw std::runtime_error(
		    std::string("resolveModuleName callback failed: ") +
		    callResult.second->Error());
	}
	const json::Value& callbackResult = callResult.first;
	if (callbackResult.empty() || callbackResult == "null") {
		return {nullptr, {}};
	}
	StaticModuleResolution staticResolution;
	if (std::string err =
	        json::unmarshal(callbackResult, &staticResolution);
	    !err.empty()) {
		throw std::runtime_error(
		    std::string("invalid resolveModuleName callback result: ") + err);
	}
	return {std::shared_ptr<module::ResolvedModule>(
	            staticModuleResolutionToResolvedModule(&staticResolution,
	                                                   currentDirectory)
	                .release()),
	        {}};
}

// ResolveTypeReferenceDirective — module_resolution.go:115.
std::pair<std::shared_ptr<module::ResolvedTypeReferenceDirective>,
          std::vector<module::DiagAndArgs>>
callbackModuleResolver::ResolveTypeReferenceDirective(
    std::string_view typeReferenceDirectiveName,
    std::string_view containingFile, ResolutionMode resolutionMode,
    const module::ResolvedProjectReference* redirectedReference) {
	return fallbackResolver->ResolveTypeReferenceDirective(
	    typeReferenceDirectiveName, containingFile, resolutionMode,
	    redirectedReference);
}

// GetResolutionData — module_resolution.go:134.
std::shared_ptr<module::ResolutionData>
callbackModuleResolver::GetResolutionData() {
	return fallbackResolver->GetResolutionData();
}

// compileModuleResolutionSpec — module_resolution.go:149.
std::pair<std::unique_ptr<module::StaticResolutions>, gostd::Error>
compileModuleResolutionSpec(const ModuleResolutionSpec* spec,
                            const std::string& currentDirectory,
                            bool useCaseSensitive) {
	if (spec == nullptr) {
		return {nullptr, nullptr};
	}
	bool fallbackToResolution = false;
	if (spec->Fallback == ModuleResolutionFallbackResolve) {
		fallbackToResolution = true;
	} else if (spec->Fallback == ModuleResolutionFallbackUnresolved) {
		fallbackToResolution = false;
	} else {
		return {nullptr,
		        gostd::errorf("%w: invalid module resolution fallback %q", {
		                      ErrClientError, spec->Fallback})};
	}

	std::vector<module::StaticResolutionEntry> entries;
	entries.reserve(spec->Entries.size());
	for (size_t i = 0; i < spec->Entries.size(); i++) {
		const ModuleResolutionEntry* entry = spec->Entries[i].get();
		if (entry == nullptr) {
			return {nullptr,
			        gostd::errorf("%w: module resolution entry %d is null", {
			                      ErrClientError, static_cast<int>(i)})};
		}
		if (entry->ModuleName.empty()) {
			return {nullptr,
			        gostd::errorf("%w: module resolution entry %d has an empty "
			                      "moduleName", {
			                      ErrClientError, static_cast<int>(i)})};
		}
		if (!entry->Result) {
			return {nullptr,
			        gostd::errorf("%w: module resolution entry %d has no result", {
			                      ErrClientError, static_cast<int>(i)})};
		}

		module::StaticResolutionEntry staticEntry;
		staticEntry.ModuleName = entry->ModuleName;
		if (entry->ContainingDirectory) {
			staticEntry.ContainingDirectory =
			    tspath::getNormalizedAbsolutePath(
			        entry->ContainingDirectory->ToAbsoluteFileName(
			            currentDirectory),
			        currentDirectory);
		}
		if (entry->ResolutionMode.has_value()) {
			ModuleKind mode = *entry->ResolutionMode;
			if (mode != ModuleKind::None && mode != ModuleKind::CommonJS &&
			    mode != ModuleKind::ESNext) {
				return {nullptr,
				        gostd::errorf("%w: module resolution entry %d has "
				                      "invalid resolutionMode %s", {
				                      ErrClientError, static_cast<int>(i),
				                      moduleKindString(mode)})};
			}
			// The ResolutionMode pointer must outlive the StaticResolutions;
			// store it alongside (Go stores a pointer to the loop-local `mode`).
			staticEntry.ResolutionMode_ = new ResolutionMode(mode);
		}
		staticEntry.Result = std::shared_ptr<module::ResolvedModule>(
		    staticModuleResolutionToResolvedModule(entry->Result.get(),
		                                           currentDirectory)
		        .release());
		entries.push_back(std::move(staticEntry));
	}

	std::unique_ptr<module::StaticResolutions> resolutions(
	    module::NewStaticResolutions(entries, fallbackToResolution,
	                                 currentDirectory, useCaseSensitive));
	if (resolutions == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: %w", { ErrClientError,
		                      gostd::newError("invalid static resolutions")})};
	}
	return {std::move(resolutions), nullptr};
}

// staticModuleResolutionToResolvedModule — module_resolution.go:196.
std::unique_ptr<module::ResolvedModule>
staticModuleResolutionToResolvedModule(
    const StaticModuleResolution* staticResolution,
    const std::string& currentDirectory) {
	if (staticResolution == nullptr ||
	    !staticResolution->ResolvedFileName) {
		return nullptr;
	}
	auto result = std::make_unique<module::ResolvedModule>();
	result->ResolvedFileName = tspath::getNormalizedAbsolutePath(
	    staticResolution->ResolvedFileName->ToAbsoluteFileName(currentDirectory),
	    currentDirectory);
	result->IsCustomResolution = true;
	if (staticResolution->OriginalPath) {
		result->OriginalPath = tspath::getNormalizedAbsolutePath(
		    staticResolution->OriginalPath->ToAbsoluteFileName(
		        currentDirectory),
		    currentDirectory);
	}
	if (staticResolution->PackageID) {
		result->PackageId = module::PackageId{
		    staticResolution->PackageID->Name,
		    staticResolution->PackageID->SubModuleName,
		    staticResolution->PackageID->Version,
		    staticResolution->PackageID->PeerDependencies,
		};
	}
	std::string originalPath = result->ResolvedFileName;
	if (!result->OriginalPath.empty()) {
		originalPath = result->OriginalPath;
	}
	result->Extension =
	    tspath::tryGetExtensionFromPath(result->ResolvedFileName);
	result->IsExternalLibraryImport =
	    std::strstr(originalPath.c_str(), "/node_modules/") != nullptr;
	return result;
}

// moduleResolutionTraceToStrings — module_resolution.go:223.
std::vector<std::string> moduleResolutionTraceToStrings(
    const std::vector<module::DiagAndArgs>& trace) {
	return mapVec<std::string>(trace, [](const module::DiagAndArgs& entry) {
		return ::tsc::localize(locale::Default, entry.Message, "",
		                       stringifyArgs(entry.Args));
	});
}

// moduleResolverFactory — module_resolution.go:229.
std::pair<std::unique_ptr<tsc::api::moduleResolverFactory>, gostd::Error>
Session::moduleResolverFactory(gostd::Context ctx,
                               const CreateProgramOptions* options) {
	if (options->ModuleResolver == 0) {
		return {nullptr, nullptr};
	}
	std::shared_ptr<api::moduleResolverRegistration> data;
	{
		std::shared_lock lock(moduleResolversMu);
		if (auto it = moduleResolvers.find(options->ModuleResolver);
		    it != moduleResolvers.end()) {
			data = it->second;
		}
	}
	if (data == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: module resolver %d not found", {
		                      ErrClientError,
		                      static_cast<int64_t>(options->ModuleResolver)})};
	}
	if (!data->resolveModuleNameCallback.empty() && conn == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: API connection is not initialized", {
		                      ErrClientError})};
	}
	auto factory = std::make_unique<api::moduleResolverFactory>();
	factory->registration = data;
	factory->session = this;
	factory->conn = conn;
	factory->ctx = ctx;
	factory->currentDirectory = GetCurrentDirectory();
	return {std::move(factory), nullptr};
}

// registerProgramResolutionContext — module_resolution.go:250.
uint64_t Session::registerProgramResolutionContext(
    std::shared_ptr<module::Resolver> resolver,
    const module::ResolverOptions& options,
    std::shared_ptr<moduleResolverRegistration> registration) {
	uint64_t id = ++nextProgramResolutionContextID;
	auto ctx = std::make_shared<programResolutionContext>();
	ctx->options = options;
	ctx->resolvers[registration->id] = std::move(resolver);
	std::unique_lock lock(programResolutionContextsMu);
	programResolutionContexts[id] = std::move(ctx);
	return id;
}

// releaseProgramResolutionContext — module_resolution.go:267.
void Session::releaseProgramResolutionContext(uint64_t id) {
	std::unique_lock lock(programResolutionContextsMu);
	programResolutionContexts.erase(id);
}

// resolverFor — module_resolution.go:272.
std::shared_ptr<module::Resolver> programResolutionContext::resolverFor(
    moduleResolverRegistration* registration) {
	std::lock_guard lock(mu);
	if (auto it = resolvers.find(registration->id); it != resolvers.end()) {
		return it->second;
	}
	auto opts = options;
	opts.CompilerOptions = registration->compilerOptions.get();
	auto resolver =
	    std::shared_ptr<module::Resolver>(module::NewResolver(opts));
	resolvers[registration->id] = resolver;
	return resolver;
}

// moduleResolutionError — module_resolution.go:286.
gostd::Error moduleResolutionError(project::Snapshot* snapshot) {
	for (project::Project* proj :
	     snapshot->ProjectCollection->Projects()) {
		if (proj->Program != nullptr) {
			if (gostd::Error err = proj->Program->ModuleResolutionError();
			    err != nullptr) {
				return err;
			}
		}
	}
	return nullptr;
}

// handleCreateModuleResolver — module_resolution.go:297.
std::pair<ModuleResolverID, gostd::Error> Session::handleCreateModuleResolver(
    const CreateModuleResolverParams* params) {
	auto providerResult = compileModuleResolutionSpec(
	    params->ModuleResolutions.get(), GetCurrentDirectory(),
	    FS()->UseCaseSensitiveFileNames());
	if (providerResult.second) {
		return {0, providerResult.second};
	}
	ModuleResolverID id = ++nextModuleResolverID;
	auto data = std::make_shared<moduleResolverRegistration>();
	data->id = id;
	// Go stores &params.CompilerOptions; params are request-scoped, so the
	// registration owns a copy.
	data->compilerOptions =
	    std::make_unique<CompilerOptions>(params->CompilerOptions);
	data->resolutions = std::move(providerResult.first);
	data->resolveModuleNameCallback = params->ResolveModuleNameCallback;
	std::unique_lock lock(moduleResolversMu);
	moduleResolvers[id] = std::move(data);
	return {id, nullptr};
}

// handleReleaseModuleResolver — module_resolution.go:316.
std::pair<ResultValue, gostd::Error> Session::handleReleaseModuleResolver(
    const ReleaseModuleResolverParams* params) {
	bool ok;
	{
		std::unique_lock lock(moduleResolversMu);
		auto it = moduleResolvers.find(params->Resolver);
		ok = it != moduleResolvers.end();
		if (ok) {
			moduleResolvers.erase(it);
		}
	}
	if (!ok) {
		return {{},
		        gostd::errorf("%w: module resolver %d not found", { ErrClientError,
		                      static_cast<int64_t>(params->Resolver)})};
	}
	return {{.data = "null"}, nullptr};
}

// handleResolveModuleName — module_resolution.go:331.
std::pair<std::unique_ptr<ResolveModuleNameResult>, gostd::Error>
Session::handleResolveModuleName(gostd::Context ctx,
                                 const ResolveModuleNameParams* params) {
	if (params->ModuleName.empty()) {
		return {nullptr,
		        gostd::errorf("%w: moduleName is empty", { ErrClientError})};
	}
	std::shared_ptr<api::moduleResolverRegistration> data;
	{
		std::shared_lock lock(moduleResolversMu);
		if (auto it = moduleResolvers.find(params->Resolver);
		    it != moduleResolvers.end()) {
			data = it->second;
		}
	}
	if (data == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: module resolver %d not found", {
		                      ErrClientError,
		                      static_cast<int64_t>(params->Resolver)})};
	}

	ResolutionMode mode = ResolutionModeNone;
	if (params->ResolutionMode.has_value()) {
		mode = *params->ResolutionMode;
		if (mode != ResolutionModeNone && mode != ResolutionModeCommonJS &&
		    mode != ResolutionModeESM) {
			return {nullptr,
			        gostd::errorf("%w: invalid resolutionMode %s", {
			                      ErrClientError, moduleKindString(mode)})};
		}
	}
	std::string containingDirectory = tspath::getNormalizedAbsolutePath(
	    params->ContainingDirectory.ToAbsoluteFileName(GetCurrentDirectory()),
	    GetCurrentDirectory());

	std::shared_ptr<module::Resolver> resolverOwned;
	module::Resolver* resolver = nullptr;
	if (params->Snapshot != 0 && params->InProgressSnapshot != 0) {
		return {nullptr,
		        gostd::errorf("%w: snapshot and inProgressSnapshot are mutually "
		                      "exclusive", {
		                      ErrClientError})};
	}
	snapshotResolutionHost snapshotHostAdapter(nullptr);
	if (params->InProgressSnapshot != 0) {
		std::shared_ptr<programResolutionContext> resolutionContext;
		{
			std::shared_lock lock(programResolutionContextsMu);
			if (auto it = programResolutionContexts.find(
			        params->InProgressSnapshot);
			    it != programResolutionContexts.end()) {
				resolutionContext = it->second;
			}
		}
		if (resolutionContext == nullptr) {
			return {nullptr,
			        gostd::errorf("%w: in-progress snapshot %d not found", {
			                      ErrClientError,
			                      static_cast<int64_t>(
			                          params->InProgressSnapshot)})};
		}
		resolverOwned = resolutionContext->resolverFor(data.get());
		resolver = resolverOwned.get();
	} else if (params->Snapshot != 0) {
		auto sdResult = getSnapshotData(params->Snapshot);
		if (sdResult.second) {
			return {nullptr, sdResult.second};
		}
		snapshotData* sd = sdResult.first;
		snapshotHostAdapter.snapshot = sd->snapshot;
		module::ResolverOptions opts;
		opts.Host = &snapshotHostAdapter;
		opts.CompilerOptions = data->compilerOptions.get();
		opts.ExtraExtensions = sd->snapshot->ContentMapperExtensions();
		resolverOwned =
		    std::shared_ptr<module::Resolver>(module::NewResolver(opts));
		resolver = resolverOwned.get();
	} else {
		module::ResolverOptions opts;
		opts.Host = this;
		opts.CompilerOptions = data->compilerOptions.get();
		resolverOwned =
		    std::shared_ptr<module::Resolver>(module::NewResolver(opts));
		resolver = resolverOwned.get();
	}
	std::unique_ptr<callbackModuleResolver> callbackResolver;
	if (!data->resolveModuleNameCallback.empty()) {
		callbackResolver = std::make_unique<api::callbackModuleResolver>();
		callbackResolver->registration = data.get();
		callbackResolver->conn = conn;
		callbackResolver->ctx = ctx;
		callbackResolver->currentDirectory = GetCurrentDirectory();
		callbackResolver->snapshot = params->Snapshot;
		callbackResolver->programResolutionContextID =
		    params->InProgressSnapshot;
		callbackResolver->fallbackResolver = resolver;
		resolver = callbackResolver.get();
	}
	std::unique_ptr<module::StaticResolver> staticResolver;
	if (data->resolutions) {
		staticResolver.reset(module::NewStaticResolver(
		    resolver, data->resolutions.get()));
		resolver = staticResolver.get();
	}
	auto result = resolver->ResolveModuleNameFromDirectory(
	    params->ModuleName, containingDirectory, mode);
	return {std::make_unique<ResolveModuleNameResult>(
	            ResolveModuleNameResult{
	                .ResolvedModule =
	                    newResolvedModuleResponse(result.first.get()),
	                .Trace =
	                    moduleResolutionTraceToStrings(result.second)}),
	        nullptr};
}

} // namespace tsc::api
