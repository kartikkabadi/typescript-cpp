// Port of tsc/internal/module/resolver.go (+ util.go helpers) — program slice.
//
// Scope: the resolution paths `tsc --noEmit <files>` needs — relative imports,
// extension probing, directory/index and package.json "types"/"typings"/"main"
// lookups, and typeRoots-based type reference resolution. Export/import-map
// branches of package.json resolution are structural no-ops for now (the
// package.json shim does not parse "exports"/"imports" maps) — TSC_UNREACHABLE-
// style TODOs, matching the slice contract ("do not fake success").
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/diagnostics/diagnostics.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/module/resolver.h"

namespace tsc::module {
namespace {

std::string normalizePathForCJSResolution(
    std::string_view containingDirectory, std::string_view moduleName);
NodeResolutionFeatures getNodeResolutionFeatures(
    const CompilerOptions* options);

// --- resolver.go: resolved (internal result) ---
struct resolved {
	std::string path;
	std::string extension;
	PackageId packageId;
	std::string originalPath;
	bool resolvedUsingTsExtension{};
	bool resolvedUsingExtraExtensions{};
};

resolved* unresolved() { return new resolved(); }

bool isResolved_(const resolved* r) { return r != nullptr && !r->path.empty(); }

// ---------------------------------------------------------------------------
// resolutionState — resolver.go:93
// ---------------------------------------------------------------------------
struct resolutionState {
	DefaultResolver* resolver{};

	// request fields
	std::string name;
	std::string containingDirectory;
	bool isConfigLookup{};
	NodeResolutionFeatures features{};
	bool esmMode{};
	std::vector<std::string> conditions;
	extensions extensions_{};
	const CompilerOptions* compilerOptions{};
	bool resolvePackageDirectoryOnly{};

	// state fields
	bool candidateEndingIsFromConfig{};
	bool resolvedPackageDirectory{};
	std::vector<Diagnostic*> diagnostics;

	// arena for resolved results (alive until the whole state is dropped)
	// resolved results intermediates (alive until the state is dropped)
	std::vector<std::unique_ptr<resolved>> resolvedArena;
	// InfoCacheEntry products are owned by resolver->infoCacheEntries_ — they
	// outlive the state (Go: packageJsonInfoCache lives on the resolver).
	// package.json info cache shared with resolver caches (alive on resolver)

	resolved* mk(resolved r) {
		resolvedArena.push_back(std::make_unique<resolved>(std::move(r)));
		return resolvedArena.back().get();
	}

	// --- resolver.go: newResolutionState ---
	static resolutionState make(std::string_view name,
	                            std::string_view containingDirectory,
	                            bool isTypeReferenceDirective,
	                            ResolutionMode resolutionMode,
	                            const CompilerOptions* compilerOptions,
	                            ResolvedProjectReference* redirectedReference,
	                            DefaultResolver* resolver) {
		resolutionState state;
		state.name = std::string(name);
		state.containingDirectory = std::string(containingDirectory);
		state.compilerOptions = compilerOptions; // GetCompilerOptionsWithRedirect
		state.resolver = resolver;

		if (isTypeReferenceDirective) {
			state.extensions_ = extensionsDeclaration;
		} else if (compilerOptions->NoDtsResolution == Tristate::True) {
			state.extensions_ = extensionsImplementationFiles;
		} else {
			state.extensions_ = extensionsTypeScript | extensionsJavaScript |
			                    extensionsDeclaration;
		}

		if (!isTypeReferenceDirective && compilerOptions->GetResolveJsonModule()) {
			state.extensions_ |= extensionsJson;
		}

		switch (compilerOptions->GetModuleResolutionKind()) {
		case ModuleResolutionKind::Node16:
			state.features = NodeResolutionFeaturesNode16Default;
			state.esmMode = resolutionMode == ModuleKind::ESNext;
			state.conditions = getConditions(compilerOptions, resolutionMode);
			break;
		case ModuleResolutionKind::NodeNext:
			state.features = NodeResolutionFeaturesNodeNextDefault;
			state.esmMode = resolutionMode == ModuleKind::ESNext;
			state.conditions = getConditions(compilerOptions, resolutionMode);
			break;
		case ModuleResolutionKind::Bundler:
			state.features = getNodeResolutionFeatures(compilerOptions);
			state.conditions = getConditions(compilerOptions, resolutionMode);
			break;
		default:
			break;
		}
		return state;
	}

	bool directoryExists(std::string_view dir) const {
		return resolver->host->directoryExists(dir);
	}
	bool fileExists(std::string_view file) const {
		return resolver->host->fileExists(file);
	}
	bool useCaseSensitiveFileNames() const {
		return resolver->host->useCaseSensitiveFileNames();
	}
	tspath::ComparePathsOptions comparePathsOptions() const {
		return {useCaseSensitiveFileNames(), resolver->host->GetCurrentDirectory()};
	}

	// --- resolver.go: getPackageJsonInfo ---
	// Minimal package.json shim: reads top-level string fields used by the
	// check path. The "exports"/"imports" maps are not parsed — see TODO below.
	InfoCacheEntry* getPackageJsonInfo(const std::string& packageDirectory) {
		std::string packageJsonPath =
		    tspath::combinePaths(packageDirectory, {"package.json"});

		auto cached = resolver->caches_.packageJsonInfoCache.find(packageJsonPath);
		if (cached != resolver->caches_.packageJsonInfoCache.end()) {
			if (cached->second != nullptr) {
				// clone under the requested PackageDirectory
				auto clone = std::make_unique<InfoCacheEntry>(*cached->second);
				clone->packageDirectory = packageDirectory;
				auto* ptr = clone.get();
				resolver->infoCacheEntries_.push_back(std::move(clone));
				return ptr;
			}
			return nullptr;
		}

		bool directoryExists_ = directoryExists(packageDirectory);
		if (directoryExists_ && fileExists(packageJsonPath)) {
			std::string contents;
			resolver->host->readFile(packageJsonPath, contents);
			auto entry = std::make_unique<InfoCacheEntry>();
			entry->packageDirectory = packageDirectory;
			entry->exists = true;
			entry->contents = parsePackageJsonFields(contents);
			auto* ptr = entry.get();
			resolver->caches_.packageJsonInfoCache.emplace(packageJsonPath, ptr);
			resolver->infoCacheEntries_.push_back(std::move(entry));
			return ptr;
		}
		// Cache the miss (nullptr => "looked up, not found").
		resolver->caches_.packageJsonInfoCache.emplace(packageJsonPath, nullptr);
		return nullptr;
	}

	// Very small JSON string-field scanner for top-level "k": "v" pairs —
	// enough for {type, name, version, types, typings, main}. Structured fields
	// ("exports", "imports", "peerDependencies") are NOT parsed (TODO for the
	// deeper-resolution slice).
	static std::string_view findJsonStringField(std::string_view json,
	                                            std::string_view key) {
		std::string needle = "\"" + std::string(key) + "\"";
		size_t pos = 0;
		for (;;) {
			pos = json.find(needle, pos);
			if (pos == std::string_view::npos) return "";
			size_t end = pos + needle.size();
			// require ':'
			while (end < json.size() &&
			       (json[end] == ' ' || json[end] == '\t' || json[end] == '\n' ||
			        json[end] == '\r'))
				end++;
			if (end >= json.size() || json[end] != ':') {
				pos += needle.size();
				continue;
			}
			end++;
			while (end < json.size() &&
			       (json[end] == ' ' || json[end] == '\t' || json[end] == '\n' ||
			        json[end] == '\r'))
				end++;
			if (end >= json.size() || json[end] != '"') return "";
			size_t close = json.find('"', end + 1);
			if (close == std::string_view::npos) return "";
			return json.substr(end + 1, close - end - 1);
		}
	}

	static PackageJsonContents parsePackageJsonFields(std::string_view json) {
		PackageJsonContents c;
		c.type = std::string(findJsonStringField(json, "type"));
		c.name = std::string(findJsonStringField(json, "name"));
		c.version = std::string(findJsonStringField(json, "version"));
		c.types = std::string(findJsonStringField(json, "types"));
		c.typings = std::string(findJsonStringField(json, "typings"));
		c.main = std::string(findJsonStringField(json, "main"));
		return c;
	}

	static bool packageJsonFieldPresent(std::string_view json,
	                                    std::string_view key) {
		std::string needle = "\"" + std::string(key) + "\"";
		size_t pos = json.find(needle);
		if (pos == std::string_view::npos) return false;
		size_t end = pos + needle.size();
		while (end < json.size() &&
		       (json[end] == ' ' || json[end] == '\t' || json[end] == '\n' ||
		        json[end] == '\r'))
			end++;
		return end < json.size() && json[end] == ':';
	}

	// resolver.go: getPackageScopeForPath
	InfoCacheEntry* getPackageScopeForPath(const std::string& directory) {
		// ForEachAncestorDirectoryStoppingAtGlobalCache — typingsLocation empty
		// means no early stop.
		return tspath::forEachAncestorDirectory<InfoCacheEntry*>(
		    directory,
		    [this](const std::string& dir)
		        -> std::pair<InfoCacheEntry*, bool> {
			    if (auto* result = getPackageJsonInfo(dir)) {
				    return {result, true};
			    }
			    return {nullptr, false};
		    });
	}

	// --- resolver.go: nodeLoadModuleByRelativeName (1366) ---
	resolved* nodeLoadModuleByRelativeName(extensions exts,
	                                       const std::string& candidate,
	                                       bool considerPackageJson) {
		if (!tspath::hasTrailingDirectorySeparator(candidate)) {
			std::string parentOfCandidate = tspath::getDirectoryPath(candidate);
			if (!directoryExists(parentOfCandidate)) {
				return nullptr; // continueSearching
			}
			resolved* resolvedFromFile = loadModuleFromFile(exts, candidate);
			if (resolvedFromFile != nullptr) {
				if (considerPackageJson) {
					NodeModuleFromPath nm;
					if (parseNodeModuleFromPath(resolvedFromFile->path, false,
					                            nm) &&
					    !nm.packageRootName.empty()) {
						resolvedFromFile->packageId = getPackageId(
						    resolvedFromFile->path,
						    getPackageJsonInfo(tspath::getDirectoryPath(
						        nm.packageRootName + "/")));
					}
				}
				return resolvedFromFile;
			}
		}
		if (!directoryExists(candidate)) {
			return nullptr;
		}
		// esm mode relative imports shouldn't do any directory lookups
		if (!esmMode) {
			return loadNodeModuleFromDirectory(exts, candidate,
			                                   considerPackageJson);
		}
		return nullptr;
	}

	// --- resolver.go: loadModuleFromFile (1403) ---
	resolved* loadModuleFromFile(extensions exts, const std::string& candidate) {
		// ./foo.js -> ./foo.ts
		resolved* resolvedByReplacingExtension =
		    loadModuleFromFileNoImplicitExtensions(exts, candidate);
		if (resolvedByReplacingExtension != nullptr) {
			return resolvedByReplacingExtension;
		}

		// ./foo -> ./foo.ts
		if (!esmMode) {
			return tryAddingExtensions(candidate, exts, "");
		}
		return nullptr;
	}

	// --- resolver.go: loadModuleFromFileNoImplicitExtensions (1418) ---
	resolved* loadModuleFromFileNoImplicitExtensions(
	    extensions exts, const std::string& candidate) {
		std::string_view base = tspath::getBaseFileName(candidate);
		if (base.find('.') == std::string_view::npos) {
			return nullptr; // extensionless import, no lookups performed
		}
		std::string extensionless{
		    tspath::removeFileExtension(candidate)};
		if (extensionless == candidate) {
			// Once TS native extensions are handled, handle arbitrary
			// extensions for declaration file mapping
			std::vector<std::string_view> extraExtViews;
			extraExtViews.reserve(resolver->extraExtensions.size());
			for (auto& e : resolver->extraExtensions)
				extraExtViews.emplace_back(e);
			std::string_view extension = tspath::getLongestExtensionFromPath(
			    candidate, extraExtViews, false);
			if (extension.empty()) {
				extension = candidate.substr(candidate.rfind('.'));
			}
			extensionless = std::string(
			    tspath::removeExtension(candidate, extension));
		}

		std::string_view extension =
		    std::string_view(candidate).substr(extensionless.size());
		return tryAddingExtensions(extensionless, exts, extension);
	}

	// --- resolver.go: tryAddingExtensions (1440) — verbatim branch order ---
	resolved* tryAddingExtensions(const std::string& extensionless, extensions exts,
	                              std::string_view originalExtension) {
		std::string directory = tspath::getDirectoryPath(extensionless);
		if (!directory.empty() && !directoryExists(directory)) {
			return nullptr;
		}

		auto cont = []() -> resolved* { return nullptr; };

		if (originalExtension == tspath::extensionMjs ||
		    originalExtension == tspath::extensionMts ||
		    originalExtension == tspath::extensionDmts) {
			if (exts & extensionsTypeScript) {
				if (auto* resolved = tryExtension(
				        tspath::extensionMts, extensionless,
				        originalExtension == tspath::extensionMts ||
				            originalExtension == tspath::extensionDmts);
				    resolved != nullptr) {
					return resolved;
				}
			}
			if (exts & extensionsDeclaration) {
				if (auto* resolved = tryExtension(
				        tspath::extensionDmts, extensionless,
				        originalExtension == tspath::extensionMts ||
				            originalExtension == tspath::extensionDmts);
				    resolved != nullptr) {
					return resolved;
				}
			}
			if (exts & extensionsJavaScript) {
				if (auto* resolved = tryExtension(tspath::extensionMjs,
				                                  extensionless, false);
				    resolved != nullptr) {
					return resolved;
				}
			}
			return cont();
		}
		if (originalExtension == tspath::extensionCjs ||
		    originalExtension == tspath::extensionCts ||
		    originalExtension == tspath::extensionDcts) {
			if (exts & extensionsTypeScript) {
				if (auto* resolved = tryExtension(
				        tspath::extensionCts, extensionless,
				        originalExtension == tspath::extensionCts ||
				            originalExtension == tspath::extensionDcts);
				    resolved != nullptr) {
					return resolved;
				}
			}
			if (exts & extensionsDeclaration) {
				if (auto* resolved = tryExtension(
				        tspath::extensionDcts, extensionless,
				        originalExtension == tspath::extensionCts ||
				            originalExtension == tspath::extensionDcts);
				    resolved != nullptr) {
					return resolved;
				}
			}
			if (exts & extensionsJavaScript) {
				if (auto* resolved = tryExtension(tspath::extensionCjs,
				                                  extensionless, false);
				    resolved != nullptr) {
					return resolved;
				}
			}
			return cont();
		}
		if (originalExtension == tspath::extensionJson) {
			if (exts & extensionsDeclaration) {
				if (auto* resolved =
				        tryExtension(".d.json.ts", extensionless, false);
				    resolved != nullptr) {
					return resolved;
				}
			}
			if (exts & extensionsJson) {
				if (auto* resolved = tryExtension(tspath::extensionJson,
				                                  extensionless, false);
				    resolved != nullptr) {
					return resolved;
				}
			}
			return cont();
		}
		if (originalExtension == tspath::extensionTsx ||
		    originalExtension == tspath::extensionJsx) {
			if (exts & extensionsTypeScript) {
				if (auto* resolved = tryExtension(
				        tspath::extensionTsx, extensionless,
				        originalExtension == tspath::extensionTsx);
				    resolved != nullptr) {
					return resolved;
				}
				if (auto* resolved = tryExtension(
				        tspath::extensionTs, extensionless,
				        originalExtension == tspath::extensionTsx);
				    resolved != nullptr) {
					return resolved;
				}
			}
			if (exts & extensionsDeclaration) {
				if (auto* resolved = tryExtension(
				        tspath::extensionDts, extensionless,
				        originalExtension == tspath::extensionTsx);
				    resolved != nullptr) {
					return resolved;
				}
			}
			if (exts & extensionsJavaScript) {
				if (auto* resolved = tryExtension(tspath::extensionJsx,
				                                  extensionless, false);
				    resolved != nullptr) {
					return resolved;
				}
				if (auto* resolved = tryExtension(tspath::extensionJs,
				                                  extensionless, false);
				    resolved != nullptr) {
					return resolved;
				}
			}
			return cont();
		}
		if (originalExtension.empty() ||
		    originalExtension == tspath::extensionTs ||
		    originalExtension == tspath::extensionDts ||
		    originalExtension == tspath::extensionJs) {
			if (exts & extensionsTypeScript) {
				if (auto* resolved = tryExtension(
				        tspath::extensionTs, extensionless,
				        originalExtension == tspath::extensionTs ||
				            originalExtension == tspath::extensionDts);
				    resolved != nullptr) {
					return resolved;
				}
				if (auto* resolved = tryExtension(
				        tspath::extensionTsx, extensionless,
				        originalExtension == tspath::extensionTs ||
				            originalExtension == tspath::extensionDts);
				    resolved != nullptr) {
					return resolved;
				}
			}
			if (exts & extensionsDeclaration) {
				if (auto* resolved = tryExtension(
				        tspath::extensionDts, extensionless,
				        originalExtension == tspath::extensionTs ||
				            originalExtension == tspath::extensionDts);
				    resolved != nullptr) {
					return resolved;
				}
			}
			if (exts & extensionsJavaScript) {
				if (auto* resolved = tryExtension(tspath::extensionJs,
				                                  extensionless, false);
				    resolved != nullptr) {
					return resolved;
				}
				if (auto* resolved = tryExtension(tspath::extensionJsx,
				                                  extensionless, false);
				    resolved != nullptr) {
					return resolved;
				}
			}
			if (isConfigLookup) {
				if (auto* resolved = tryExtension(tspath::extensionJson,
				                                  extensionless, false);
				    resolved != nullptr) {
					return resolved;
				}
			}
			return cont();
		}
		// default
		if (std::find(resolver->extraExtensions.begin(),
		              resolver->extraExtensions.end(),
		              originalExtension) != resolver->extraExtensions.end()) {
			// A fully specified import of an extraExtension resolves
			// directly to the file.
			if (auto* resolved =
			        tryExtension(originalExtension, extensionless, false);
			    resolved != nullptr) {
				resolved->resolvedUsingExtraExtensions = true;
				return resolved;
			}
		}
		if ((exts & extensionsDeclaration) &&
		    !tspath::isDeclarationFileName(extensionless +
		                                 std::string(originalExtension))) {
			if (auto* resolved =
			        tryExtension(".d" + std::string(originalExtension) + ".ts",
			                     extensionless, false);
			    resolved != nullptr) {
				return resolved;
			}
		}
		return cont();
	}

	// --- resolver.go: tryExtension (1564) ---
	resolved* tryExtension(std::string_view extension,
	                       const std::string& extensionless,
	                       bool resolvedUsingTsExtension) {
		std::string fileName = extensionless + std::string(extension);
		if (auto [path, ok] = tryFile(fileName); ok) {
			resolved r;
			r.path = std::move(path);
			r.extension = std::string(extension);
			r.resolvedUsingTsExtension =
			    !candidateEndingIsFromConfig && resolvedUsingTsExtension;
			return mk(std::move(r));
		}
		return nullptr;
	}

	// --- resolver.go: tryFile (1576) ---
	std::pair<std::string, bool> tryFile(const std::string& fileName) {
		if (compilerOptions->ModuleSuffixes.empty()) {
			return {fileName, tryFileLookup(fileName)};
		}
		std::string_view ext = tspath::tryGetExtensionFromPath(fileName);
		std::string_view fileNameNoExtension =
		    tspath::removeExtension(fileName, ext);
		for (const auto& suffix : compilerOptions->ModuleSuffixes) {
			std::string path =
			    std::string(fileNameNoExtension) + suffix + std::string(ext);
			if (tryFileLookup(path)) {
				return {path, true};
			}
		}
		return {fileName, false};
	}

	// --- resolver.go: tryFileLookup (1592) ---
	bool tryFileLookup(const std::string& fileName) {
		return fileExists(fileName);
	}

	// --- resolver.go: loadNodeModuleFromDirectory (1604) ---
	resolved* loadNodeModuleFromDirectory(extensions exts,
	                                      const std::string& candidate,
	                                      bool considerPackageJson) {
		InfoCacheEntry* packageInfo = nullptr;
		if (considerPackageJson) {
			packageInfo = getPackageJsonInfo(candidate);
		}
		return loadNodeModuleFromDirectoryWorker(exts, candidate, packageInfo);
	}

	// --- resolver.go: loadNodeModuleFromDirectoryWorker (1613) ---
	resolved* loadNodeModuleFromDirectoryWorker(
	    extensions exts, const std::string& candidate,
	    InfoCacheEntry* packageInfo) {
		std::string packageFile;
		if (packageInfo != nullptr && packageInfo->exists) {
			if (tspath::comparePaths(candidate, packageInfo->packageDirectory,
			                         {useCaseSensitiveFileNames(), ""}) == 0) {
				if (auto [file, ok] = getPackageFile(exts, packageInfo); ok) {
					packageFile = std::move(file);
				}
			}
		}
		// (Go also reads VersionPaths — package.json "typesVersions"; not in
		// the minimal shim, so versionPaths.Exists() is false → skip that
		// whole branch faithfully.)

		auto loader = [this, &packageFile,
		               packageInfo](extensions exts2,
		                            const std::string& candidate2) -> resolved* {
			if (auto* fromFile = loadFileNameFromPackageJSONField(
			        exts2, candidate2, packageFile);
			    fromFile != nullptr) {
				return fromFile;
			}

			// Even if `extensions == extensionsDeclaration`, we can still look
			// up a .ts file as a result of package.json "types"
			extensions expandedExtensions = exts2;
			if (exts2 == extensionsDeclaration) {
				expandedExtensions =
				    extensionsTypeScript | extensionsDeclaration;
			}

			// Disable `esmMode` for the resolution of the package path for
			// CJS-mode packages (so the `main` field can omit extensions)
			bool saveESMMode = esmMode;
			bool saveCandidateEndingIsFromConfig = candidateEndingIsFromConfig;
			candidateEndingIsFromConfig = true;
			if (packageInfo != nullptr && packageInfo->exists &&
			    packageInfo->contents.type != "module") {
				esmMode = false;
			}
			resolved* result = nodeLoadModuleByRelativeName(
			    expandedExtensions, candidate2, false /*considerPackageJson*/);
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

		if (!packageFile.empty()) {
			if (auto* packageFileResult = loader(exts, packageFile);
			    packageFileResult != nullptr) {
				return packageFileResult;
			}
		}

		// ESM mode resolutions don't do package 'index' lookups
		if (!esmMode) {
			if (!directoryExists(candidate)) {
				return nullptr;
			}
			return loadModuleFromFile(exts, indexPath);
		}
		return nullptr;
	}

	// --- resolver.go: loadFileNameFromPackageJSONField (1702) ---
	resolved* loadFileNameFromPackageJSONField(
	    extensions exts, const std::string& candidate,
	    const std::string& packageJSONValue) {
		if ((exts & extensionsTypeScript &&
		     tspath::hasImplementationTSFileExtension(candidate)) ||
		    (exts & extensionsDeclaration &&
		     tspath::isDeclarationFileName(candidate))) {
			if (auto [path, ok] = tryFile(candidate); ok) {
				std::string_view extension =
				    tspath::tryExtractTSExtension(path);
				resolved r;
				r.path = std::move(path);
				r.extension = std::string(extension);
				r.resolvedUsingTsExtension =
				    packageJSONValue.size() > 0 &&
				    packageJSONValue.back() == '*' && !extension.empty();
				return mk(std::move(r));
			}
			return nullptr;
		}

		if (isConfigLookup && (exts & extensionsJson) &&
		    tspath::fileExtensionIs(candidate, tspath::extensionJson)) {
			if (auto [path, ok] = tryFile(candidate); ok) {
				resolved r;
				r.path = std::move(path);
				r.extension = std::string(tspath::extensionJson);
				return mk(std::move(r));
			}
		}

		return loadModuleFromFileNoImplicitExtensions(exts, candidate);
	}

	// --- resolver.go: getPackageFile (1734) ---
	std::pair<std::string, bool> getPackageFile(extensions exts,
	                                            InfoCacheEntry* packageInfo) {
		if (packageInfo == nullptr || !packageInfo->exists) {
			return {"", false};
		}
		if (isConfigLookup) {
			return getPackageJSONPathField("tsconfig",
			                               packageInfo->packageDirectory);
		}
		if (exts & extensionsDeclaration) {
			if (auto [packageFile, ok] = getPackageJSONPathField(
			        packageInfo->contents.typings,
			        packageInfo->packageDirectory);
			    ok) {
				return {std::move(packageFile), ok};
			}
			if (auto [packageFile, ok] = getPackageJSONPathField(
			        packageInfo->contents.types,
			        packageInfo->packageDirectory);
			    ok) {
				return {std::move(packageFile), ok};
			}
		}
		if (exts & (extensionsImplementationFiles | extensionsDeclaration)) {
			return getPackageJSONPathField(
			    packageInfo->contents.main, packageInfo->packageDirectory);
		}
		return {"", false};
	}

	// getPackageJSONPathField — `field` is the package.json field value;
	// joined to `directory`, the file must exist.
	std::pair<std::string, bool> getPackageJSONPathField(
	    const std::string& field, const std::string& directory) {
		if (field.empty()) {
			return {"", false};
		}
		// package.json path fields are relative "./..." paths
		std::string path = field;
		if (path == ".") {
			path = "./index.js";
		}
		if (path.substr(0, 2) != "./") {
			return {"", false};
		}
		std::string targetFile =
		    tspath::normalizePath(tspath::combinePaths(directory, {path}));
		if (!esmMode &&
		    tspath::fileExtensionIs(targetFile, tspath::extensionMjs)) {
			return {"", false};
		}
		return {targetFile, true};
	}

	// --- resolver.go: getPackageId (1802) ---
	PackageId getPackageId(const std::string& resolvedFileName,
	                       InfoCacheEntry* packageInfo) {
		if (packageInfo != nullptr && packageInfo->exists) {
			const auto& c = packageInfo->contents;
			if (!c.name.empty() && !c.version.empty()) {
				std::string subModuleName;
				if (resolvedFileName.size() >
				    packageInfo->packageDirectory.size()) {
					subModuleName = resolvedFileName.substr(
					    packageInfo->packageDirectory.size() + 1);
				}
				return PackageId{c.name, subModuleName, c.version,
				                 readPackageJsonPeerDependencies(packageInfo)};
			}
		}
		return PackageId{};
	}

	// --- resolver.go: readPackageJsonPeerDependencies (1823) ---
	// TODO(program-slice): peerDependencies map parsing is not ported —
	// returns "" (equivalent to "no peerDependencies field").
	std::string readPackageJsonPeerDependencies(InfoCacheEntry*) {
		return "";
	}

	// --- resolver.go: loadModuleFromNearestNodeModulesDirectory (969) ---
	resolved* loadModuleFromNearestNodeModulesDirectory(bool typesScopeOnly) {
		ResolutionMode mode = ResolutionModeCommonJS;
		if (esmMode || conditionMatches("import")) {
			mode = ResolutionModeESM;
		}
		(void)mode;
		extensions priorityExtensions =
		    extensions_ & (extensionsTypeScript | extensionsDeclaration);
		extensions secondaryExtensions =
		    extensions_ & ~(extensionsTypeScript | extensionsDeclaration);
		// (1)
		if (priorityExtensions != 0) {
			if (auto* result = loadModuleFromNearestNodeModulesDirectoryWorker(
			        priorityExtensions, typesScopeOnly);
			    result != nullptr) {
				return result;
			}
		}
		// (2)
		if (secondaryExtensions != 0 && !typesScopeOnly) {
			return loadModuleFromNearestNodeModulesDirectoryWorker(
			    secondaryExtensions, typesScopeOnly);
		}
		return nullptr;
	}

	// --- resolver.go: loadModuleFromNearestNodeModulesDirectoryWorker (1001) ---
	resolved* loadModuleFromNearestNodeModulesDirectoryWorker(
	    extensions exts, bool typesScopeOnly) {
		return tspath::forEachAncestorDirectory<resolved*>(
		    containingDirectory,
		    [this, exts, typesScopeOnly](const std::string& directory)
		        -> std::pair<resolved*, bool> {
			    if (tspath::getBaseFileName(directory) != "node_modules") {
				    resolved* result =
				        loadModuleFromImmediateNodeModulesDirectory(
				            exts, directory, typesScopeOnly);
				    return {result, result != nullptr};
			    }
			    return {nullptr, false};
		    });
	}

	// --- resolver.go: loadModuleFromImmediateNodeModulesDirectory (1016) ---
	resolved* loadModuleFromImmediateNodeModulesDirectory(
	    extensions exts, const std::string& directory, bool typesScopeOnly) {
		std::string nodeModulesFolder =
		    tspath::combinePaths(directory, {"node_modules"});
		if (!directoryExists(nodeModulesFolder)) {
			return nullptr;
		}

		if (!typesScopeOnly) {
			if (auto* packageResult = loadModuleFromSpecificNodeModulesDirectory(
			        exts, name, nodeModulesFolder);
			    packageResult != nullptr) {
				return packageResult;
			}
		}

		if (exts & extensionsDeclaration) {
			std::string nodeModulesAtTypes =
			    tspath::combinePaths(nodeModulesFolder, {"@types"});
			if (!directoryExists(nodeModulesAtTypes)) {
				return nullptr;
			}
			return loadModuleFromSpecificNodeModulesDirectory(
			    extensionsDeclaration, mangleScopedPackageName(name),
			    nodeModulesAtTypes);
		}
		return nullptr;
	}

	// --- resolver.go: loadModuleFromSpecificNodeModulesDirectory (1045) ---
	// NOTE(program-slice): the exports/imports-map branches are TODO — the
	// package.json shim does not parse structured fields, so packages that
	// only declare "exports" are treated as having no exports map.
	resolved* loadModuleFromSpecificNodeModulesDirectory(
	    extensions exts, const std::string& moduleName,
	    const std::string& nodeModulesDirectory) {
		std::string candidate{tspath::removeTrailingDirectorySeparator(
		    tspath::normalizePath(
		        tspath::combinePaths(nodeModulesDirectory, {moduleName})))};
		auto [packageName, rest] = parsePackageName(moduleName);
		auto& restCap = rest;
		std::string packageDirectory =
		    tspath::combinePaths(nodeModulesDirectory, {packageName});
		if (packageName.empty()) {
			packageDirectory = candidate;
		}

		if (resolvePackageDirectoryOnly) {
			if (directoryExists(packageDirectory)) {
				return mk(resolved{.path = packageDirectory});
			}
			return nullptr;
		}

		InfoCacheEntry* rootPackageInfo = nullptr;
		// First look for a nested package.json, as in
		// `node_modules/foo/bar/package.json`
		InfoCacheEntry* packageInfo = getPackageJsonInfo(candidate);
		// But only if we're not respecting export maps
		if (!rest.empty() && packageInfo != nullptr && packageInfo->exists) {
			if (features & NodeResolutionFeaturesExports) {
				rootPackageInfo = getPackageJsonInfo(packageDirectory);
			}
			// exports map is never "present" under the minimal shim →
			// `!rootPackageInfo.Exists() || Exports == NotPresent` == true.
			if (auto* fromFile = loadModuleFromFile(exts, candidate);
			    fromFile != nullptr) {
				return fromFile;
			}
			if (auto* fromDirectory = loadNodeModuleFromDirectoryWorker(
			        exts, candidate, packageInfo);
			    fromDirectory != nullptr) {
				fromDirectory->packageId =
				    getPackageId(fromDirectory->path, packageInfo);
				return fromDirectory;
			}
		}

		auto loader = [this, &packageInfo, &restCap](extensions exts2,
		                                       const std::string& candidate2)
		    -> resolved* {
			if (!restCap.empty() || !esmMode) {
				if (auto* fromFile = loadModuleFromFile(exts2, candidate2);
				    fromFile != nullptr) {
					fromFile->packageId =
					    getPackageId(fromFile->path, packageInfo);
					return fromFile;
				}
			}
			if (auto* fromDirectory = loadNodeModuleFromDirectoryWorker(
			        exts2, candidate2, packageInfo);
			    fromDirectory != nullptr) {
				fromDirectory->packageId =
				    getPackageId(fromDirectory->path, packageInfo);
				return fromDirectory;
			}
			if (restCap.empty() && packageInfo != nullptr &&
			    packageInfo->exists && esmMode) {
				// EsmMode disables index lookup generally, however non-relative
				// package resolutions still assume a default `index.js`
				// entrypoint if no `main` or `exports` are present.
				if (auto* indexResult = loadModuleFromFile(
			        exts2, tspath::combinePaths(candidate2, {"index.js"}));
				    indexResult != nullptr) {
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
			// TODO(program-slice): package.json "exports"/"imports"/"self
			// name" maps are not resolved — the minimal package.json shim
			// does not parse structured fields, so we treat Exports as
			// falsy/not-present here (Go would consult the exports map).
		}
		return loader(exts, candidate);
	}

	// --- resolver.go: getCandidateFromTypeRoot / mangleScopedPackageName ---
	std::string mangleScopedPackageName(std::string_view n) {
		return tsc::module::mangleScopedPackageName(n);
	}

	std::string getCandidateFromTypeRoot(const std::string& typeRoot) {
		std::string_view nameForLookup = name;
		if (typeRoot.ends_with("/node_modules/@types") ||
		    typeRoot.ends_with("/node_modules/@types/")) {
			nameForLookup = mangleScopedPackageName(name);
		}
		return tspath::combinePaths(typeRoot, {nameForLookup});
	}

	// --- resolver.go: resolveFromTypeRoot (458) ---
	resolved* resolveFromTypeRoot() {
		if (compilerOptions->TypeRoots.empty()) {
			return nullptr;
		}
		// (full body only needed under explicit typeRoots — verifier only)
		for (const auto& typeRoot : compilerOptions->TypeRoots) {
			std::string candidate = getCandidateFromTypeRoot(typeRoot);
			if (!directoryExists(typeRoot)) {
				continue;
			}
			// Custom typeRoots resolve as file or directory just like modules
			if (auto* resolvedFromFile =
			        loadModuleFromFile(extensionsDeclaration, candidate);
			    resolvedFromFile != nullptr) {
				return resolvedFromFile;
			}
			if (auto* resolvedFromDirectory = loadNodeModuleFromDirectory(
			        extensionsDeclaration, candidate,
			        true /*considerPackageJson*/);
			    resolvedFromDirectory != nullptr) {
				return resolvedFromDirectory;
			}
		}
		return nullptr;
	}

	// --- resolver.go: tryLoadModuleUsingOptionalResolutionSettings (1214) ---
	// Paths/rootDirs resolution — ported structurally; both are no-ops under
	// `tsc --noEmit <files>` (no tsconfig → Paths/RootDirs empty).
	resolved* tryLoadModuleUsingOptionalResolutionSettings() {
		// tryLoadModuleUsingPathsIfEligible: requires Paths configured
		if (!compilerOptions->Paths.empty() &&
		    !tspath::pathIsRelative(name)) {
			// TODO(program-slice): paths mapping resolution not ported —
			// falls through to relative-only handling.
		}
		if (!tspath::isExternalModuleNameRelative(name)) {
			// No more tryLoadModuleUsingBaseUrl.
			return nullptr;
		}
		// tryLoadModuleUsingRootDirs — RootDirs empty under CLI mode
		return nullptr;
	}

	// --- resolver.go: resolveNodeLikeWorker (532) ---
	ResolvedModule* resolveNodeLikeWorker() {
		if (resolved* resolved_ = tryLoadModuleUsingOptionalResolutionSettings();
		    resolved_ != nullptr) {
			return createResolvedModuleHandlingSymlink(resolved_);
		}

		if (!tspath::isExternalModuleNameRelative(name)) {
			// TODO(program-slice): "#imports" and self-name references
			// (loadModuleFromImports / loadModuleFromSelfNameReference) are
			// not ported — they require package.json exports/imports maps.
			if (name.find(':') != std::string::npos) {
				return createResolvedModule(nullptr, false);
			}
			if (resolved* resolved_ =
			        loadModuleFromNearestNodeModulesDirectory(
			            false /*typesScopeOnly*/);
			    resolved_ != nullptr) {
				return createResolvedModuleHandlingSymlink(resolved_);
			}
			if (extensions_ & extensionsDeclaration) {
				if (resolved* resolved_ = resolveFromTypeRoot();
				    resolved_ != nullptr) {
					return createResolvedModuleHandlingSymlink(resolved_);
				}
			}
		} else {
			std::string candidate =
			    normalizePathForCJSResolution(containingDirectory, name);
			resolved* resolved_ = nodeLoadModuleByRelativeName(
			    extensions_, candidate, true);
			return createResolvedModule(
			    resolved_,
			    resolved_ != nullptr &&
			        resolved_->path.find("/node_modules/") !=
			            std::string::npos);
		}
		return createResolvedModule(nullptr, false);
	}

	// --- resolver.go: resolveNodeLike (499) ---
	ResolvedModule* resolveNodeLike() {
		ResolvedModule* result = resolveNodeLikeWorker();
		// exports-related re-resolution retry is not applicable (features with
		// Exports maps unported)
		return result;
	}

	// --- resolver.go: resolveTypeReferenceDirective (388) ---
	ResolvedTypeReferenceDirective* resolveTypeReferenceDirective(
	    const std::vector<std::string>& typeRoots, bool fromConfig,
	    bool fromInferredTypesContainingFile) {
		// Primary lookup
		if (!typeRoots.empty()) {
			for (const auto& typeRoot : typeRoots) {
				std::string candidate = getCandidateFromTypeRoot(typeRoot);
				bool dirExists = directoryExists(typeRoot);
				if (!dirExists) {
					continue;
				}
				if (fromConfig) {
					// Custom typeRoots resolve as file or directory
					if (resolved* resolvedFromFile = loadModuleFromFile(
					        extensionsDeclaration, candidate);
					    isResolved_(resolvedFromFile)) {
						NodeModuleFromPath nm;
						if (parseNodeModuleFromPath(resolvedFromFile->path,
						                            false, nm)) {
							resolvedFromFile->packageId = getPackageId(
							    resolvedFromFile->path,
							    getPackageJsonInfo(nm.packageRootName));
						}
						return createResolvedTypeReferenceDirective(
						    resolvedFromFile, true /*primary*/);
					}
				}
				if (resolved* resolvedFromDirectory = loadNodeModuleFromDirectory(
				        extensionsDeclaration, candidate,
				        true /*considerPackageJson*/);
				    isResolved_(resolvedFromDirectory)) {
					return createResolvedTypeReferenceDirective(
					    resolvedFromDirectory, true /*primary*/);
				}
			}
		}

		// Secondary lookup
		resolved* resolved_ = nullptr;
		if (!fromConfig || !fromInferredTypesContainingFile) {
			if (!tspath::isExternalModuleNameRelative(name)) {
				resolved_ = loadModuleFromNearestNodeModulesDirectory(
				    false /*typesScopeOnly*/);
			} else {
				std::string candidate =
				    normalizePathForCJSResolution(containingDirectory, name);
				resolved_ = nodeLoadModuleByRelativeName(
				    extensionsDeclaration, candidate,
				    true /*considerPackageJson*/);
			}
		}
		return createResolvedTypeReferenceDirective(resolved_,
		                                            false /*primary*/);
	}

	// --- resolver.go: createResolvedModuleHandlingSymlink (1145) ---
	// TODO(program-slice): realpath/symlink processing not ported — on a
	// regular filesystem realpath == input, so originalPath stays "".
	ResolvedModule* createResolvedModuleHandlingSymlink(resolved* r) {
		bool isExternalLibraryImport =
		    r != nullptr &&
		    r->path.find("/node_modules/") != std::string::npos;
		return createResolvedModule(r, isExternalLibraryImport);
	}

	// --- resolver.go: createResolvedModule (1160) ---
	ResolvedModule* createResolvedModule(resolved* r,
	                                     bool isExternalLibraryImport) {
		auto m = std::make_unique<ResolvedModule>();
		m->resolutionDiagnostics = diagnostics;
		if (r != nullptr) {
			m->resolvedFileName = r->path;
			m->originalPath = r->originalPath;
			m->isExternalLibraryImport = isExternalLibraryImport;
			m->resolvedUsingTsExtension = r->resolvedUsingTsExtension;
			m->resolvedUsingExtraExtensions = r->resolvedUsingExtraExtensions;
			m->extension = r->extension;
			m->packageId = r->packageId;
		}
		auto* ptr = m.get();
		resolver->resolvedModules_.push_back(std::move(m));
		return ptr;
	}

	// --- resolver.go: createResolvedTypeReferenceDirective (1176) ---
	ResolvedTypeReferenceDirective* createResolvedTypeReferenceDirective(
	    resolved* r, bool primary) {
		auto d = std::make_unique<ResolvedTypeReferenceDirective>();
		d->resolutionDiagnostics = diagnostics;
		if (isResolved_(r)) {
			d->resolvedFileName = r->path;
			d->primary = primary;
			d->packageId = r->packageId;
			d->isExternalLibraryImport =
			    r->path.find("/node_modules/") != std::string::npos;
			// getOriginalAndResolvedFileName — realpath shim not ported;
			// identical on a symlink-free filesystem.
		}
		auto* ptr = d.get();
		resolver->resolvedTypeRefs_.push_back(std::move(d));
		return ptr;
	}

	bool conditionMatches(std::string_view condition) {
		return std::find(conditions.begin(), conditions.end(), condition) !=
		       conditions.end();
	}
};

// resolver.go: normalizePathForCJSResolution
std::string normalizePathForCJSResolution(std::string_view containingDirectory,
                                          std::string_view moduleName) {
	std::string combined =
	    tspath::combinePaths(containingDirectory, {moduleName});
	auto parts = tspath::getPathComponents(combined, "");
	const auto& lastPart = parts.back();
	if (lastPart == "." || lastPart == "..") {
		return tspath::ensureTrailingDirectorySeparator(
		    tspath::normalizePath(combined));
	}
	return tspath::normalizePath(combined);
}

// resolver.go: getNodeResolutionFeatures (1938)
NodeResolutionFeatures getNodeResolutionFeatures(
    const CompilerOptions* options) {
	NodeResolutionFeatures features = NodeResolutionFeaturesNone;
	switch (options->GetModuleResolutionKind()) {
	case ModuleResolutionKind::Node16:
		features = NodeResolutionFeaturesNode16Default;
		break;
	case ModuleResolutionKind::NodeNext:
		features = NodeResolutionFeaturesNodeNextDefault;
		break;
	case ModuleResolutionKind::Bundler:
		features = NodeResolutionFeaturesBundlerDefault;
		break;
	default:
		break;
	}
	if (options->ResolvePackageJsonExports == Tristate::True) {
		features |= NodeResolutionFeaturesExports;
	} else if (options->ResolvePackageJsonExports == Tristate::False) {
		features &= ~NodeResolutionFeaturesExports;
	}
	if (options->ResolvePackageJsonImports == Tristate::True) {
		features |= NodeResolutionFeaturesImports;
	} else if (options->ResolvePackageJsonImports == Tristate::False) {
		features &= ~NodeResolutionFeaturesImports;
	}
	return features;
}

}  // namespace

// --- resolver.go: GetConditions (1916) ---
std::vector<std::string> getConditions(const CompilerOptions* options,
                                       ResolutionMode resolutionMode) {
	ModuleResolutionKind moduleResolution = options->GetModuleResolutionKind();
	if (resolutionMode == ModuleKind::None &&
	    moduleResolution == ModuleResolutionKind::Bundler) {
		resolutionMode = ModuleKind::ESNext;
	}
	std::vector<std::string> conditions;
	conditions.reserve(3 + options->CustomConditions.size());
	if (resolutionMode == ModuleKind::ESNext) {
		conditions.push_back("import");
	} else {
		conditions.push_back("require");
	}

	if (options->NoDtsResolution != Tristate::True) {
		conditions.push_back("types");
	}
	if (moduleResolution != ModuleResolutionKind::Bundler) {
		conditions.push_back("node");
	}
	for (const auto& c : options->CustomConditions) {
		conditions.push_back(c);
	}
	return conditions;
}

// --- resolver.go: NewResolver (160) ---
std::unique_ptr<DefaultResolver> newResolver(const ResolverOptions& opts) {
	auto r = std::make_unique<DefaultResolver>();
	r->host = opts.host;
	r->compilerOptions = opts.compilerOptions;
	r->typingsLocation = opts.typingsLocation;
	r->projectName = opts.projectName;
	r->extraExtensions = opts.extraExtensions;
	return r;
}

// --- resolver.go: ResolveModuleName / ResolveModuleNameFromDirectory /
//     resolveModuleName (240) ---
ResolvedModule* DefaultResolver::ResolveModuleName(
    const std::string& moduleName, const std::string& containingFile,
    ResolutionMode resolutionMode,
    ResolvedProjectReference* redirectedReference) {
	std::string containingDirectory =
	    tspath::getDirectoryPath(containingFile);
	return resolveModuleName(moduleName, containingDirectory,
	                         resolutionMode);
}

ResolvedModule* DefaultResolver::ResolveModuleNameFromDirectory(
    const std::string& moduleName, const std::string& containingDirectory,
    ResolutionMode resolutionMode) {
	return resolveModuleName(moduleName, containingDirectory,
	                         resolutionMode);
}

// (internal worker; named to mirror Go's resolveModuleName)
ResolvedModule* DefaultResolver::resolveModuleName(
    const std::string& moduleName, const std::string& containingDirectory,
    ResolutionMode resolutionMode) {
	ModuleResolutionKind moduleResolution =
	    compilerOptions->GetModuleResolutionKind();
	ResolvedModule* result;
	switch (moduleResolution) {
	case ModuleResolutionKind::Node16:
	case ModuleResolutionKind::NodeNext:
	case ModuleResolutionKind::Bundler: {
		auto state = resolutionState::make(
		    moduleName, containingDirectory, false /*isTypeReferenceDirective*/,
		    resolutionMode, compilerOptions, nullptr, this);
		result = state.resolveNodeLike();
		break;
	}
	default:
		TSC_UNREACHABLE("unexpected moduleResolution in ResolveModuleName");
	}

	// tryResolveFromTypingsLocation — typingsLocation empty under CLI mode.
	return result;
}

// --- resolver.go: ResolveTypeReferenceDirective (209) ---
ResolvedTypeReferenceDirective* DefaultResolver::ResolveTypeReferenceDirective(
    const std::string& typeReferenceDirectiveName,
    const std::string& containingFile, ResolutionMode resolutionMode,
    ResolvedProjectReference* redirectedReference) {
	std::string containingDirectory =
	    tspath::getDirectoryPath(containingFile);
	bool fromInferredTypesContainingFile =
	    containingFile.size() >= InferredTypesContainingFile.size() &&
	    containingFile.substr(containingFile.size() -
	                          InferredTypesContainingFile.size()) ==
	        InferredTypesContainingFile;

	auto [typeRoots, fromConfig] = compilerOptions->GetEffectiveTypeRoots(
	    host->GetCurrentDirectory());

	auto state = resolutionState::make(
	    typeReferenceDirectiveName, containingDirectory,
	    true /*isTypeReferenceDirective*/, resolutionMode, compilerOptions,
	    nullptr, this);
	return state.resolveTypeReferenceDirective(
	    typeRoots, fromConfig, fromInferredTypesContainingFile);
}

// --- resolver.go: GetPackageScopeForPath (182) ---
InfoCacheEntry* DefaultResolver::GetPackageScopeForPath(
    const std::string& directory) {
	// &resolutionState{compilerOptions, resolver} — call through a fresh state
	// so package-info results land in the shared cache.
	resolutionState state;
	state.compilerOptions = compilerOptions;
	state.resolver = this;
	return state.getPackageScopeForPath(directory);
}

// --- resolver.go: ResolvePackageDirectory (312) ---
ResolvedModule* DefaultResolver::ResolvePackageDirectory(
    const std::string& moduleName, const std::string& containingFile,
    ResolutionMode resolutionMode,
    ResolvedProjectReference* redirectedReference) {
	std::string containingDirectory =
	    tspath::getDirectoryPath(containingFile);
	auto state = resolutionState::make(moduleName, containingDirectory,
	                                 false /*isTypeReferenceDirective*/,
	                                 resolutionMode, compilerOptions, nullptr,
	                                 this);
	state.resolvePackageDirectoryOnly = true;
	if (auto* result = state.loadModuleFromNearestNodeModulesDirectory(
	        false /*typesScopeOnly*/);
	    result != nullptr && !result->path.empty()) {
		return state.createResolvedModuleHandlingSymlink(result);
	}
	return nullptr;
}

// --- resolver.go: GetAutomaticTypeDirectiveNames (2080) ---
std::vector<std::string> getAutomaticTypeDirectiveNames(
    const CompilerOptions* options, ResolutionHost* host) {
	if (!options->UsesWildcardTypes()) {
		if (!options->Types.empty()) {
			return options->Types;
		}
		return {};
	}

	// Walk the primary type lookup locations
	std::vector<std::string> wildcardMatches;
	auto [typeRoots, _] =
	    options->GetEffectiveTypeRoots(host->GetCurrentDirectory());
	for (const auto& root : typeRoots) {
		if (host->directoryExists(root)) {
			for (const auto& typeDirectivePath :
			     host->getAccessibleDirectories(root)) {
				std::string normalized =
				    tspath::normalizePath(typeDirectivePath);
				std::string packageJsonPath = tspath::combinePaths(
				    root, {normalized, "package.json"});
				bool isNotNeededPackage = false;
				if (host->fileExists(packageJsonPath)) {
					std::string contents;
					if (host->readFile(packageJsonPath, contents)) {
						// `types-publisher` sometimes creates packages with
						// `"typings": null` for packages that don't provide
						// their own types.
						std::string needle = "\"typings\"";
						auto pos = contents.find(needle);
						if (pos != std::string::npos) {
							auto colon = contents.find(':', pos + needle.size());
							auto valueStart = colon + 1;
							while (valueStart < contents.size() &&
							       (contents[valueStart] == ' ' ||
							        contents[valueStart] == '\t'))
								valueStart++;
							isNotNeededPackage =
							    contents.substr(valueStart, 4) == "null";
						}
					}
				}
				if (!isNotNeededPackage) {
					std::string_view baseFileName =
					    tspath::getBaseFileName(normalized);
					if (baseFileName.empty() || baseFileName[0] != '.') {
						wildcardMatches.emplace_back(baseFileName);
					}
				}
			}
		}
	}

	// Order potentially matters in program construction, so substitute
	// in the wildcard in the position it was specified in the types array
	std::vector<std::string> result;
	for (const auto& t : options->Types) {
		if (t == "*") {
			result.insert(result.end(), wildcardMatches.begin(),
			              wildcardMatches.end());
		} else {
			result.push_back(t);
		}
	}
	// core.Deduplicate
	std::vector<std::string> deduped;
	std::unordered_map<std::string, bool> seen;
	for (auto& r : result) {
		if (seen.emplace(r, true).second) deduped.push_back(r);
	}
	return deduped;
}

// file->IsDeclarationFile — helper for needAllowArbitraryExtensions
static bool isDeclarationFileName_(SourceFile* file);

// --- util.go: GetResolutionDiagnostic (125) ---
const DiagnosticMessage* getResolutionDiagnostic(
    const CompilerOptions* options, const ResolvedModule& resolvedModule,
    SourceFile* file) {
	if (resolvedModule.resolvedUsingExtraExtensions) {
		return nullptr;
	}

	const std::string& extension = resolvedModule.extension;
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

static bool isDeclarationFileName_(SourceFile* file) {
	return file != nullptr && file->IsDeclarationFile;
}

}  // namespace tsc::module
