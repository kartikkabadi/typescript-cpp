// --- fileloader.go + filesparser.go — program slice ---
// Single-threaded port: Go runs the same logic through a WorkGroup; the
// observable result (filesByPath / include reasons / diagnostics) is
// order-stable, so a synchronous DFS is faithful.
#include "internal/compiler/program.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/parser/parser.h"
#include "internal/scanner/scanner.h"

namespace tsc::compiler {

static std::string joinStrings(
    const std::vector<std::string_view>& parts, std::string_view sep) {
	std::string out;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i)
			out += sep;
		out += parts[i];
	}
	return out;
}

static std::string supportedExtensionsJoin(
    const std::vector<std::vector<std::string_view>>& exts) {
	std::vector<std::string_view> flat;
	for (auto& g : exts)
		for (auto e : g)
			flat.push_back(e);
	return joinStrings(flat, "', '");
}

// ===========================================================================
// filesLoader — fileloader.go
// ===========================================================================

// fileloader.go:364 getDefaultLibFilePriority
int filesLoader::getDefaultLibFilePriority(SourceFile* a) {
	// defaultLibraryPath and a.FileName() are absolute and normalized; a
	// prefix check should suffice.
	std::string_view libPath =
	    tspath::removeTrailingDirectorySeparator(defaultLibraryPath);
	std::string_view aFileName = a->FileName();

	if (aFileName.starts_with(libPath) &&
	    aFileName.size() > libPath.size() &&
	    aFileName[libPath.size()] == '/' /* tspath.DirectorySeparator */) {
		std::string_view basename =
		    aFileName.substr(aFileName.rfind('/') + 1);
		if (basename == "lib.d.ts" || basename == "lib.es6.d.ts")
			return 0;
		std::string_view name = basename;
		if (name.starts_with("lib.")) name = name.substr(4);
		if (name.ends_with(".d.ts"))
			name = name.substr(0, name.size() - 5);
		for (size_t i = 0; i < tsoptions::Libs.size(); i++) {
			if (tsoptions::Libs[i] == name)
				return static_cast<int>(i) + 1;
		}
	}
	return static_cast<int>(tsoptions::Libs.size()) + 2;
}

// fileloader.go:358 sortLibs — Go uses slices.SortFunc (unstable).
void filesLoader::sortLibs(std::vector<SourceFile*>& libFiles) {
	std::sort(libFiles.begin(), libFiles.end(),
	          [this](SourceFile* a, SourceFile* b) {
		          return getDefaultLibFilePriority(a) <
		                 getDefaultLibFilePriority(b);
	          });
}

// fileloader.go:1024 getInferredLibraryNameResolveFrom
static std::string getInferredLibraryNameResolveFrom(
    const CompilerOptions* compilerOptions, const std::string& currentDirectory,
    const std::string& libFileName);

// fileloader.go:1009 getLibraryNameFromLibFileName — lib.dom.d.ts ->
// @typescript/lib-dom, lib.dom.iterable.d.ts -> @typescript/lib-dom/iterable
static std::string getLibraryNameFromLibFileName(
    const std::string& libFileName) {
	std::vector<std::string> components;
	size_t start = 0;
	while (true) {
		size_t dot = libFileName.find('.', start);
		if (dot == std::string::npos) {
			components.push_back(libFileName.substr(start));
			break;
		}
		components.push_back(libFileName.substr(start, dot - start));
		start = dot + 1;
	}
	std::string path = "@typescript/lib-";
	if (components.size() > 1)
		path += components[1];
	for (size_t i = 2; i < components.size() && !components[i].empty() &&
	     components[i] != "d";
	     i++) {
		path += (i == 2) ? '/' : '-';
		path += components[i];
	}
	return path;
}

// fileloader.go:961 pathForLibFile
LibFile* filesLoader::pathForLibFile(const std::string& name) {
	if (auto it = pathForLibFileCache.find(name);
	    it != pathForLibFileCache.end())
		return it->second.get();

	std::string path = tspath::combinePaths(defaultLibraryPath, {name});
	bool replaced = false;
	if (!skipModuleResolution &&
	    compilerOptions->LibReplacement == Tristate::True &&
	    name != "lib.d.ts") {
		std::string libraryName = getLibraryNameFromLibFileName(name);
		std::string resolveFrom = getInferredLibraryNameResolveFrom(
		    compilerOptions, host->GetCurrentDirectory(), name);
		module::ResolvedModule* resolution = resolver->ResolveModuleName(
		    libraryName, resolveFrom, ModuleKind::CommonJS, nullptr).first.get();
		if (resolution != nullptr && resolution->IsResolved()) {
			path = resolution->ResolvedFileName;
			replaced = true;
		}
		pathForLibFileResolutions.emplace(
		    toPath(resolveFrom),
		    libResolution{libraryName, resolution, {}});
	}

	auto lib = std::make_unique<LibFile>(LibFile{name, path, replaced});
	auto* ptr = lib.get();
	pathForLibFileCache.emplace(name, std::move(lib));
	return ptr;
}

// fileloader.go:951 createSyntheticImport
Node* filesLoader::createSyntheticImport(const std::string& text,
                                         SourceFile* file) {
	Node* moduleReference =
	    parser->factory.newStringLiteral(text, TokenFlagsNone);
	Node* importDecl = parser->factory.newImportDeclaration(
	    nullptr, nullptr, moduleReference, nullptr);
	moduleReference->parent = importDecl;
	importDecl->parent = file->asNode();
	return moduleReference;
}

// fileloader.go:680 isSupportedExtension
bool filesLoader::isSupportedExtension(
    const std::string& canonicalFileName) const {
	for (auto& group : supportedExtensionsWithJsonIfResolveJsonModule) {
		if (tspath::fileExtensionIsOneOf(canonicalFileName, group))
			return true;
	}
	return false;
}

// fileloader.go:384 loadSourceFileMetaData
SourceFileMetaData filesLoader::loadSourceFileMetaData(
    const std::string& fileName) {
	if (skipModuleResolution) {
		return SourceFileMetaData{
		    "", "", getImpliedNodeFormatForFile(fileName, "")};
	}

	packagejson::InfoCacheEntry* packageJsonScope =
	    resolver->GetPackageScopeForPath(tspath::getDirectoryPath(fileName)).get();
	ModuleResolutionKind moduleResolutionKind =
	    compilerOptions->GetModuleResolutionKind();

	std::string packageJsonType, packageJsonDirectory;
	if (packageJsonScope && packageJsonScope->Exists()) {
		packageJsonDirectory = packageJsonScope->PackageDirectory;
		const std::string& value = packageJsonScope->GetContents()->Type.Value;
		if (!value.empty() &&
		    (!tspath::fileExtensionIsOneOf(
		         fileName,
		         {".mts", ".cts", ".mjs", ".cjs"}) &&
			     ModuleResolutionKind::Node16 <=
			         moduleResolutionKind &&
			         moduleResolutionKind <=
			             ModuleResolutionKind::NodeNext) ||

		    fileName.find("/node_modules/") != std::string::npos) {
			packageJsonType = value;
		}
	}

	ResolutionMode impliedNodeFormat =
	    getImpliedNodeFormatForFile(fileName, packageJsonType);
	return SourceFileMetaData{packageJsonType, packageJsonDirectory,
	                          impliedNodeFormat};
}

// fileloader.go:417 parseSourceFile
SourceFile* filesLoader::parseSourceFile(parseTask* t) {
	tspath::Path path = toPath(t->normalizedFilePath);
	CompilerOptions* options = compilerOptions; // no project-reference redirect
	SourceFileParseOptions parseOptions{
	    .FileName = t->normalizedFilePath,
	    .Path = path,
	    .ExternalModuleIndicatorOptions =
	        getExternalModuleIndicatorOptions(t->normalizedFilePath, options,
	                                          t->metadata),
	};
	return host->GetSourceFile(parseOptions, t->metadata);
}

// fileloader.go:689 getSourceFileFromReference
std::pair<std::string, processingDiagnostic*>
filesLoader::getSourceFileFromReference(
    const std::string& fileName, const std::string& referenceText,
    const std::string& containingFile,
    const FileIncludeReason* includeReason) {
	const CompilerOptions* options = compilerOptions;
	bool allowNonTsExtensions =
	    options->AllowNonTsExtensions == Tristate::True;
	std::string diagnosticFileName = tspath::normalizeSlashes(referenceText);

	auto fail = [&](const DiagnosticMessage* m, std::vector<std::string> args)
	    -> std::pair<std::string, processingDiagnostic*> {
		return {"", ip->newProcessingDiagnostic(
		                processingDiagnosticKind::ExplainingFileInclude,
		                includeExplainingDiagnostic{{}, includeReason, m,
		                                            std::move(args)})};
	};

	if (tspath::hasExtension(fileName)) {
		std::string canonicalFileName = tspath::getCanonicalFileName(
		    fileName, useCaseSensitiveFileNames);
		if (!allowNonTsExtensions &&
		    !isSupportedExtension(canonicalFileName)) {
			if (tspath::hasJSFileExtension(canonicalFileName)) {
				return fail(
				    
				        File_0_is_a_JavaScript_file_Did_you_mean_to_enable_the_allowJs_option,
				    {diagnosticFileName});
			}
			return fail(
			    
			        File_0_has_an_unsupported_extension_The_only_supported_extensions_are_1,
			    {diagnosticFileName,
			     "'" +
			         supportedExtensionsJoin(supportedExtensions) +
			         "'"});
		}

		if (!host->FileExists(fileName)) {
			return fail(File_0_not_found, {diagnosticFileName});
		}

		if (includeReason->isReferencedFile() &&
		    tspath::getCanonicalFileName(containingFile,
		                                 useCaseSensitiveFileNames) ==
		        canonicalFileName) {
			return fail(
			    A_file_cannot_have_a_reference_to_itself, {});
		}
		return {fileName, nullptr};
	}

	if (allowNonTsExtensions && host->FileExists(fileName)) {
		return {fileName, nullptr};
	}
	if (allowNonTsExtensions) {
		return fail(File_0_not_found, {diagnosticFileName});
	}

	for (auto ext : supportedExtensions[0]) {
		std::string candidate = fileName + std::string(ext);
		if (host->FileExists(candidate)) {
			return {candidate, nullptr};
		}
	}

	return fail(
	                Could_not_resolve_the_path_0_with_the_extensions_Colon_1,
	            {diagnosticFileName,
	             "'" +
	                 supportedExtensionsJoin(supportedExtensions) + "'"});
}

// fileloader.go:736 resolveTripleslashPathReference
std::pair<resolvedRef, processingDiagnostic*>
filesLoader::resolveTripleslashPathReference(
    const std::string& moduleName, const std::string& containingFile,
    int index) {
	std::string basePath = tspath::getDirectoryPath(containingFile);
	std::string referencedFileName = moduleName;
	if (!tspath::isRootedDiskPath(moduleName)) {
		referencedFileName = tspath::combinePaths(basePath, {moduleName});
	}
	std::string normalizedFileName = tspath::normalizePath(referencedFileName);
	const FileIncludeReason* includeReason = ip->newReason(
	    FileIncludeKind::ReferenceFile,
	    referencedFileData{toPath(containingFile), index, nullptr});

	auto [ResolvedFileName, diagnostic] = getSourceFileFromReference(
	    normalizedFileName, moduleName, containingFile, includeReason);
	if (diagnostic != nullptr) {
		return {resolvedRef{}, diagnostic};
	}

	return {resolvedRef{ResolvedFileName, false, false, includeReason,
	                    module::PackageId{}},
	        nullptr};
}

// fileloader.go:1033 getModeForTypeReferenceDirectiveInFile
static ResolutionMode getModeForTypeReferenceDirectiveInFile(
    const FileReference& ref, SourceFile* file,
    const SourceFileMetaData& meta, const CompilerOptions* options) {
	if (ref.ResolutionMode != ResolutionModeNone) {
		return ref.ResolutionMode;
	}
	return getDefaultResolutionModeForFile(file->FileName(), meta, options);
}

// fileloader.go:769 resolveTypeReferenceDirectives
void filesLoader::resolveTypeReferenceDirectives(parseTask* t) {
	SourceFile* file = t->file;
	if (file->TypeReferenceDirectives.empty())
		return;
	SourceFileMetaData meta = t->metadata;

	auto typeResolutionsInFile =
	    module::ModeAwareCache<module::ResolvedTypeReferenceDirective*>();
	std::vector<module::DiagAndArgs> typeResolutionsTrace;
	for (size_t index = 0; index < file->TypeReferenceDirectives.size();
	     index++) {
		const FileReference* ref = file->TypeReferenceDirectives[index];
		ResolutionMode resolutionMode =
		    getModeForTypeReferenceDirectiveInFile(*ref, file, meta,
		                                           compilerOptions);
		module::ResolvedTypeReferenceDirective* resolved =
		    resolver->ResolveTypeReferenceDirective(
		        ref->FileName, file->FileName(), resolutionMode, nullptr).first.get();
		typeResolutionsInFile[module::ModeAwareCacheKey{ref->FileName,
		                                              resolutionMode}] =
		    resolved;
		const FileIncludeReason* includeReason = ip->newReason(
		    FileIncludeKind::TypeReferenceDirective,
		    referencedFileData{t->path, static_cast<int>(index), nullptr});

		if (resolved != nullptr && resolved->IsResolved()) {
			t->subTasks.push_back(parser->newTask(
			    tspath::normalizePath(resolved->ResolvedFileName)));
			auto* sub = t->subTasks.back();
			sub->increaseDepth = resolved->IsExternalLibraryImport;
			sub->elideOnDepth = false;
			sub->includeReason = includeReason;
			sub->PackageId = resolved->PackageId;
		} else {
			t->processingDiagnostics.push_back(ip->newProcessingDiagnostic(
			    processingDiagnosticKind::UnknownReference, includeReason));
		}
	}

	t->typeResolutionsInFile = std::move(typeResolutionsInFile);
	t->typeResolutionsTrace = std::move(typeResolutionsTrace);
}

// fileloader.go:823 resolveImportsAndModuleAugmentations
static const char* externalHelpersModuleNameText = "tslib";

void filesLoader::resolveImportsAndModuleAugmentations(parseTask* t) {
	SourceFile* file = t->file;
	SourceFileMetaData meta = t->metadata;

	std::vector<Node*> moduleNames;
	moduleNames.reserve(file->imports.size() +
	                    file->ModuleAugmentations.size() + 2);

	bool isJavaScriptFile = isSourceFileJS(file);
	bool isExternalModuleFile = isExternalModule(file);

	CompilerOptions* optionsForFile = compilerOptions;
	std::string_view fileName = file->FileName();
	if (isJavaScriptFile ||
	    (!file->IsDeclarationFile &&
	     (optionsForFile->GetIsolatedModules() || isExternalModuleFile))) {
		if (optionsForFile->ImportHelpers == Tristate::True) {
			Node* specifier =
			    createSyntheticImport(externalHelpersModuleNameText, file);
			moduleNames.push_back(specifier);
			t->importHelpersImportSpecifier = specifier;
		}
	}

	if (isJavaScriptFile || file->ScriptKind == ScriptKind::TSX) {
		std::string jsxImport =
		    getJSXRuntimeImport(getJSXImplicitImportBase(optionsForFile, file),
		                        optionsForFile);
		if (!jsxImport.empty()) {
			Node* specifier = createSyntheticImport(jsxImport, file);
			moduleNames.push_back(specifier);
			t->jsxRuntime = {jsxImport, specifier};
			t->hasJsxRuntime = true;
		}
	}

	int importsStart = static_cast<int>(moduleNames.size());

	for (Node* imp : file->imports)
		moduleNames.push_back(imp);
	for (Node* imp : file->ModuleAugmentations) {
		if (imp->kind == Kind::StringLiteral) {
			moduleNames.push_back(imp);
		}
		// Do nothing if it's an Identifier; we don't need to do module
		// resolution for `declare global`.
	}

	if (skipModuleResolution) {
		return;
	}

	if (!moduleNames.empty()) {
		auto resolutionsInFile =
		    module::ModeAwareCache<module::ResolvedModule*>();
		std::vector<module::DiagAndArgs> resolutionsTrace;

		for (size_t index = 0; index < moduleNames.size(); index++) {
			Node* entry = moduleNames[index];
			std::string_view moduleName = entry->text();
			if (moduleName.empty()) {
				continue;
			}

			ResolutionMode mode = getModeForUsageLocation(
			    file->FileName(), meta, entry, optionsForFile);
			module::ResolvedModule* resolvedModule =
			    resolver->ResolveModuleName(std::string(moduleName),
			                                std::string(fileName), mode,
			                                nullptr).first.get();
			if (resolvedModule == nullptr) {
				resolvedModule =
				    &parser->resolvedModuleArena.emplace_back();
			}
			resolutionsInFile[module::ModeAwareCacheKey{
			    std::string(moduleName), mode}] = resolvedModule;

			if (!resolvedModule->IsResolved()) {
				continue;
			}

			const std::string& ResolvedFileName =
			    resolvedModule->ResolvedFileName;
			bool isFromNodeModulesSearch =
			    resolvedModule->IsExternalLibraryImport;
			bool isJsFile = !resolvedModule->ResolvedUsingExtraExtensions &&
			                !tspath::fileExtensionIsOneOf(
			                    ResolvedFileName,
			                    tspath::supportedTSExtensionsWithJsonFlat);
			bool isJsFileFromNodeModules =
			    isFromNodeModulesSearch && isJsFile &&
			    ResolvedFileName.find("/node_modules/") !=
			        std::string::npos;

			int importIndex = static_cast<int>(index) - importsStart;

			bool shouldAddFile =
			    !moduleName.empty() &&
			    module::getResolutionDiagnostic(optionsForFile,
			                                    *resolvedModule,
			                                    file) == nullptr &&
			    optionsForFile->NoResolve != Tristate::True &&
			    !(isJsFile && !optionsForFile->GetAllowJS()) &&
			    (importIndex < 0 ||
			     (importIndex <
			          static_cast<int>(file->imports.size()) &&
			      (isInJSFile(file->imports[importIndex]) ||
			       (file->imports[importIndex]->flags &
			        NodeFlagsJSDoc) == NodeFlagsNone)));

			if (shouldAddFile) {
				auto* sub = parser->newTask(
				    tspath::normalizePath(ResolvedFileName));
				sub->increaseDepth =
				    resolvedModule->IsExternalLibraryImport;
				sub->elideOnDepth = isJsFileFromNodeModules;
				sub->includeReason = ip->newReason(
				    FileIncludeKind::Import,
				    referencedFileData{
				        t->path, importIndex,
				        importIndex < 0 ? entry : nullptr});
				sub->PackageId = resolvedModule->PackageId;
				t->subTasks.push_back(sub);
			}
		}

		t->resolutionsInFile = std::move(resolutionsInFile);
		t->resolutionsTrace = std::move(resolutionsTrace);
	}
}

// fileloader.go:1009 getLibraryNameFromLibFileName / :1024 resolve-from helper
std::string getInferredLibraryNameResolveFrom(
    const CompilerOptions* options, const std::string& currentDirectory,
    const std::string& libFileName) {
	std::string containingDirectory;
	if (!options->ConfigFilePath.empty()) {
		containingDirectory =
		    tspath::getDirectoryPath(options->ConfigFilePath);
	} else {
		containingDirectory = currentDirectory;
	}
	return tspath::combinePaths(
	    containingDirectory,
	    {"__lib_node_modules_lookup_" + libFileName + "__.ts"});
}

// fileloader.go:283 addAutomaticTypeDirectiveTasks
void filesLoader::addAutomaticTypeDirectiveTasks() {
	std::string containingDirectory;
	if (!compilerOptions->ConfigFilePath.empty()) {
		containingDirectory =
		    tspath::getDirectoryPath(compilerOptions->ConfigFilePath);
	} else {
		containingDirectory = host->GetCurrentDirectory();
	}
	std::string containingFileName = tspath::combinePaths(
	    containingDirectory, {std::string(module::InferredTypesContainingFile)});
	auto* task = parser->newTask(containingFileName);
	task->isForAutomaticTypeDirective = true;
	rootTasks.push_back(task);
}

// fileloader.go:299 resolveAutomaticTypeDirectives
void filesLoader::resolveAutomaticTypeDirectives(parseTask* t) {
	std::vector<std::string> automaticTypeDirectiveNames =
	    module::GetAutomaticTypeDirectiveNames(*compilerOptions, host);
	if (!automaticTypeDirectiveNames.empty()) {
		for (auto& name : automaticTypeDirectiveNames) {
			// Under node16/nodenext module resolution, load `types`/ata
			// include names as cjs resolution results by passing an
			// "undefined" mode. Under bundler, this also triggers the
			// "import" condition to be used.
			ResolutionMode resolutionMode = ResolutionModeNone;
			module::ResolvedTypeReferenceDirective* resolved =
			    resolver->ResolveTypeReferenceDirective(
			        name, t->normalizedFilePath, resolutionMode, nullptr).first.get();
			t->typeResolutionsInFile[module::ModeAwareCacheKey{
			    name, resolutionMode}] = resolved;
			if (resolved != nullptr && resolved->IsResolved()) {
				auto* sub = parser->newTask(resolved->ResolvedFileName);
				sub->increaseDepth = resolved->IsExternalLibraryImport;
				sub->elideOnDepth = false;
				sub->includeReason = ip->newReason(
				    FileIncludeKind::AutomaticTypeDirectiveFile,
				    automaticTypeDirectiveFileData{
				        name, resolved->PackageId});
				sub->PackageId = resolved->PackageId;
				t->subTasks.push_back(sub);
			} else {
				t->processingDiagnostics.push_back(
				    ip->newProcessingDiagnostic(
				        processingDiagnosticKind::
				            ExplainingFileInclude,
				        includeExplainingDiagnostic{
				            {},
				            ip->newReason(
				                FileIncludeKind::
				                    AutomaticTypeDirectiveFile,
				                automaticTypeDirectiveFileData{name,
				                                               module::
				                                                   PackageId{}}),
				            
				                Cannot_find_type_definition_file_for_0,
				        {name}}));
			}
		}
	}
}

void filesLoader::addRootTask(const std::string& fileName, LibFile* libFile,
                              const FileIncludeReason* includeReason) {
	std::string absPath = tspath::getNormalizedAbsolutePath(
	    fileName, host->GetCurrentDirectory());
	if (compilerOptions->AllowNonTsExtensions == Tristate::True ||
	    tspath::hasExtension(absPath)) {
		auto* task = parser->newTask(absPath);
		task->libFile = libFile;
		task->includeReason = includeReason;
		rootTasks.push_back(task);
	}
}

void filesLoader::addRootFileTask(const std::string& fileName,
                                  LibFile* libFile,
                                  const FileIncludeReason* includeReason) {
	std::string currDir = host->GetCurrentDirectory();
	std::string absPath =
	    tspath::getNormalizedAbsolutePath(fileName, currDir);
	std::string containingFile = currDir; // no config file in this slice
	auto [resolvedFile, diagnostic] = getSourceFileFromReference(
	    absPath, fileName, containingFile, includeReason);
	auto* rootTask = parser->newTask(resolvedFile.empty() ? absPath
	                                                    : resolvedFile);
	rootTask->libFile = libFile;
	rootTask->includeReason = includeReason;
	if (diagnostic != nullptr) {
		rootTask->failedLookup = true;
		rootTask->processingDiagnostics.push_back(diagnostic);
	}
	rootTasks.push_back(rootTask);
}

// ===========================================================================
// filesParser — filesparser.go
// ===========================================================================

void filesParser::load(parseTask* t) {
	t->loaded = true;
	if (t->isForAutomaticTypeDirective) {
		loader->resolveAutomaticTypeDirectives(t);
		return;
	}
	if (t->failedLookup) {
		// The root file name did not resolve to a supported Extension; the
		// task exists only to carry its processing diagnostic.
		return;
	}

	if (!t->isContentMapperSupplemental &&
	    tspath::hasExtension(t->normalizedFilePath)) {
		CompilerOptions* compilerOptions = loader->compilerOptions;
		bool allowNonTsExtensions =
		    compilerOptions->AllowNonTsExtensions == Tristate::True;
		if (!allowNonTsExtensions) {
			std::string canonicalFileName = tspath::getCanonicalFileName(
			    t->normalizedFilePath,
			    loader->useCaseSensitiveFileNames);
			if (!loader->isSupportedExtension(canonicalFileName)) {
				if (tspath::hasJSFileExtension(canonicalFileName)) {
					t->processingDiagnostics.push_back(
					    loader->ip->newProcessingDiagnostic(
					        processingDiagnosticKind::
					            ExplainingFileInclude,
					        includeExplainingDiagnostic{
					            {}, t->includeReason,
					            
					                File_0_is_a_JavaScript_file_Did_you_mean_to_enable_the_allowJs_option,
					            {t->normalizedFilePath}}));
				} else {
					t->processingDiagnostics.push_back(
					    loader->ip->newProcessingDiagnostic(
					        processingDiagnosticKind::
					            ExplainingFileInclude,
					        includeExplainingDiagnostic{
					            {}, t->includeReason,
					            
					                File_0_has_an_unsupported_extension_The_only_supported_extensions_are_1,
					            {t->normalizedFilePath,
					             "'" +
					                 supportedExtensionsJoin(
					                     loader->supportedExtensions) +
					                 "'"}}));
				}
				return;
			}
		}
	}

	if (t->libFile != nullptr) {
		// Default lib files are all scripts; skip package.json lookup.
		t->metadata =
		    SourceFileMetaData{"", "", ResolutionModeCommonJS};
	} else {
		t->metadata = loader->loadSourceFileMetaData(t->normalizedFilePath);
	}

	SourceFile* file = t->file;
	if (file == nullptr) {
		file = loader->parseSourceFile(t);
	}
	if (file == nullptr) {
		return;
	}
	t->file = file;

	CompilerOptions* compilerOptions = loader->compilerOptions;
	if (compilerOptions->NoResolve != Tristate::True &&
	    !loader->skipModuleResolution) {
		for (size_t index = 0; index < file->ReferencedFiles.size();
		     index++) {
			const FileReference* ref = file->ReferencedFiles[index];
			auto [resolvedRefV, processingDiag] =
			    loader->resolveTripleslashPathReference(
			        ref->FileName, file->FileName(),
			        static_cast<int>(index));
			if (processingDiag != nullptr) {
				t->processingDiagnostics.push_back(processingDiag);
				continue;
			}
			auto* sub = newTask(
			    tspath::normalizePath(resolvedRefV.fileName));
			sub->increaseDepth = resolvedRefV.increaseDepth;
			sub->elideOnDepth = resolvedRefV.elideOnDepth;
			sub->includeReason = resolvedRefV.includeReason;
			sub->PackageId = resolvedRefV.PackageId;
			t->subTasks.push_back(sub);
		}

		loader->resolveTypeReferenceDirectives(t);
	}

	if (compilerOptions->NoLib != Tristate::True &&
	    !loader->skipModuleResolution) {
		for (size_t index = 0; index < file->LibReferenceDirectives.size();
		     index++) {
			const FileReference* lib = file->LibReferenceDirectives[index];
			const FileIncludeReason* includeReason =
			    loader->ip->newReason(
			        FileIncludeKind::LibReferenceDirective,
			        referencedFileData{t->path, static_cast<int>(index),
			                           nullptr});
			if (auto [name, ok] =
			        tsoptions::getLibFileName(lib->FileName);
			    ok) {
				LibFile* libFile =
				    loader->pathForLibFile(std::string(name));
				auto* sub = newTask(tspath::normalizePath(libFile->path));
				sub->libFile = libFile;
				sub->includeReason = includeReason;
				t->subTasks.push_back(sub);
			} else {
				t->processingDiagnostics.push_back(
				    loader->ip->newProcessingDiagnostic(
				        processingDiagnosticKind::UnknownReference,
				        includeReason));
			}
		}
	}

	loader->resolveImportsAndModuleAugmentations(t);
	// no content-mapped supplemental files in this slice
}

// filesparser.go:269 filesParser.start (single-threaded DFS)
void filesParser::start(const std::vector<parseTask*>& tasks, int depth) {
	for (auto* task : tasks) {
		task->path = loader->toPath(task->normalizedFilePath);
		auto& dataSlot = taskDataByPath[task->path];
		parseTaskData* data;
		bool loaded;
		if (!dataSlot) {
			dataSlot = std::make_unique<parseTaskData>();
			data = dataSlot.get();
			data->tasks[task->normalizedFilePath] = task;
			loaded = false;
		} else {
			data = dataSlot.get();
			loaded = true;
		}

		bool startSubtasks = false;
		if (loaded) {
			if (auto it = data->tasks.find(task->normalizedFilePath);
			    it != data->tasks.end()) {
				task->loadedTask = it->second;
			} else {
				data->tasks[task->normalizedFilePath] = task;
				// new task for file name — load subtasks if there was
				// loading for any other casing
				startSubtasks = data->startedSubTasks;
			}
		}

		// Propagate PackageId to data if we have one and data doesn't yet
		if (data->PackageId.Name.empty() && !task->PackageId.Name.empty()) {
			data->PackageId = task->PackageId;
		}

		int currentDepth =
		    task->increaseDepth ? depth + 1 : depth;
		if (currentDepth < data->lowestDepth) {
			// reprocess subtasks to ensure they are loaded
			data->lowestDepth = currentDepth;
			startSubtasks = true;
			data->startedSubTasks = true;
		}

		if (task->elideOnDepth && currentDepth > maxDepth) {
			continue;
		}

		for (auto& [name, taskByFileName] : data->tasks) {
			bool loadSubTasks = startSubtasks;
			if (!taskByFileName->loaded) {
				load(taskByFileName);
				if (taskByFileName->redirectedParseTask != nullptr) {
					loadSubTasks = true;
					data->startedSubTasks = true;
				}
			}
			if (!taskByFileName->startedSubTasksTask && loadSubTasks) {
				taskByFileName->startedSubTasksTask = true;
				start(taskByFileName->subTasks, data->lowestDepth);
			}
		}
	}
}

// filesparser.go:330 getProcessedFiles — collectFiles
// `seen` dedupes by shared parseTaskData (multiple file-name casings for one
// path); `tasksSeenByNameIgnoreCase` is only enabled on case-sensitive hosts;
// `packageIdToSourceFile` implements deduplicatePackages (on by default).
void filesParser::collectFiles(const std::vector<parseTask*>& tasks) {
	std::unordered_map<parseTaskData*, std::string> seen;
	// filename -> task association ignoring case (case-sensitive hosts only)
	std::unordered_map<std::string, parseTask*> tasksSeenByNameIgnoreCase;
	bool checkIgnoreCase = loader->useCaseSensitiveFileNames;
	// DeduplicatePackages.IsFalse() is unset (default: dedupe enabled) →
	// allocate the package dedup maps exactly like Go.
	bool dedupePackages =
	    loader->compilerOptions->DeduplicatePackages != Tristate::False;
	std::unordered_map<module::PackageId, SourceFile*,
	                   module::PackageIdHash>
	    packageIdToSourceFile;

	std::function<void(const std::vector<parseTask*>&)> collect;
	collect = [&](const std::vector<parseTask*>& ts) {
		for (auto* task : ts) {
			const FileIncludeReason* includeReason = task->includeReason;
			// Exclude automatic type directive tasks from include reason
			// processing — internal implementation details.
			if (task->redirectedParseTask == nullptr &&
			    !task->isForAutomaticTypeDirective) {
				if (task->loadedTask != nullptr) {
					task = task->loadedTask;
				}
				addIncludeReason(task, includeReason);
			}
			auto dataIt = taskDataByPath.find(task->path);
			parseTaskData* data =
			    dataIt != taskDataByPath.end() ? dataIt->second.get()
			                                 : nullptr;
			if (!task->loaded) {
				continue;
			}

			// ensure we only walk each task once (per parseTaskData)
			if (auto seenIt = seen.find(data); seenIt != seen.end()) {
				const std::string& checkedName = seenIt->second;
				if (loader->compilerOptions
				        ->ForceConsistentCasingInFileNames !=
				    Tristate::False) {
					// Check if it differs only in drive letters — ok to
					// ignore that error.
					std::string checkedAbsolutePath =
					    tspath::getNormalizedAbsolutePathWithoutRoot(
					        checkedName,
					        loader->comparePathsOptions()
					            .currentDirectory);
					std::string inputAbsolutePath =
					    tspath::getNormalizedAbsolutePathWithoutRoot(
					        task->normalizedFilePath,
					        loader->comparePathsOptions()
					            .currentDirectory);
					if (checkedAbsolutePath != inputAbsolutePath) {
						loader->ip
						    ->addProcessingDiagnosticsForFileCasing(
						        task->path, checkedName,
						        task->normalizedFilePath,
						        includeReason);
					}
				}
				continue;
			} else {
				seen[data] = task->normalizedFilePath;
			}

			if (checkIgnoreCase) {
				std::string pathLowerCase =
				    tspath::toFileNameLowerCase(
				        std::string_view(task->path));
				if (auto it2 =
				        tasksSeenByNameIgnoreCase.find(pathLowerCase);
				    it2 != tasksSeenByNameIgnoreCase.end()) {
					loader->ip
					    ->addProcessingDiagnosticsForFileCasing(
					        it2->second->path,
					        it2->second->normalizedFilePath,
					        task->normalizedFilePath,
					        includeReason);
				} else {
					tasksSeenByNameIgnoreCase[pathLowerCase] = task;
				}
			}

			SourceFile* file = task->file;
			if (dedupePackages && !data->PackageId.Name.empty()) {
				if (auto pkgIt = packageIdToSourceFile.find(
				        data->PackageId);
				    pkgIt != packageIdToSourceFile.end()) {
					SourceFile* packageIdFile = pkgIt->second;
					// Package deduplication keeps the first package
					// instance in the program.
					redirectTargetsMap[packageIdFile->Path()]
					    .push_back(task->normalizedFilePath);
					redirectFilesByPath[task->path] = redirectsFile{
					    static_cast<int>(
					        files.size() +
					        redirectFilesByPath.size()),
					    task->normalizedFilePath, task->path,
					    packageIdFile->Path()};
					filesByPath[task->path] = packageIdFile;
					if (data->lowestDepth > 0) {
						sourceFilesFoundSearchingNodeModules.insert(
						    task->path);
					}
					continue;
				} else if (file != nullptr) {
					packageIdToSourceFile[data->PackageId] = file;
				}
			}

			if (!task->subTasks.empty()) {
				collect(task->subTasks);
			}

			if (task->redirectedParseTask != nullptr) {
				// No project references — output map unused.
				continue;
			}

			if (task->isForAutomaticTypeDirective) {
				typeResolutionsInFile[task->path] =
				    task->typeResolutionsInFile;
				for (auto* pd : task->processingDiagnostics)
					loader->ip->processingDiagnostics.push_back(pd);
				continue;
			}

			tspath::Path path = task->path;

			for (auto* pd : task->processingDiagnostics)
				loader->ip->processingDiagnostics.push_back(pd);

			if (file == nullptr) {
				missingFiles.push_back(task->normalizedFilePath);
				continue;
			}

			if (task->libFile != nullptr) {
				libFileList.push_back(file);
				libFiles[path] = task->libFile;
			} else {
				files.push_back(file);
			}
			filesByPath[path] = file;
			resolvedModules[path] = task->resolutionsInFile;
			typeResolutionsInFile[path] = task->typeResolutionsInFile;
			sourceFileMetaDatas[path] = task->metadata;

			if (task->hasJsxRuntime) {
				jsxRuntimeImportSpecifiers[path] = task->jsxRuntime;
			}
			if (task->importHelpersImportSpecifier != nullptr) {
				importHelpersImportSpecifiers[path] =
				    task->importHelpersImportSpecifier;
			}
			if (data->lowestDepth > 0) {
				sourceFilesFoundSearchingNodeModules.insert(path);
			}
		}
	};
	collect(tasks);
}

void filesParser::addIncludeReason(parseTask* task,
                                   const FileIncludeReason* reason) {
	if (task->redirectedParseTask != nullptr) {
		addIncludeReason(task->redirectedParseTask, reason);
	} else if (task->loaded) {
		auto& v = loader->ip->fileIncludeReasons[task->path];
		if (reason != nullptr)
			v.push_back(reason);
	}
}

// ===========================================================================
// mode-for-usage workers — fileloader.go:1041+
// ===========================================================================

bool importSyntaxAffectsModuleResolution(const CompilerOptions* options) {
	ModuleResolutionKind moduleResolution =
	    options->GetModuleResolutionKind();
	return (moduleResolution >= ModuleResolutionKind::Node16 &&
	        moduleResolution <= ModuleResolutionKind::NodeNext) ||
	       options->GetResolvePackageJsonExports() ||
	       options->GetResolvePackageJsonImports();
}

ResolutionMode getDefaultResolutionModeForFile(
    std::string_view fileName, const SourceFileMetaData& meta,
    const CompilerOptions* options) {
	if (importSyntaxAffectsModuleResolution(options)) {
		return getImpliedNodeFormatForEmitWorker(
		    fileName, options->GetEmitModuleKind(), meta);
	}
	return ResolutionModeNone;
}

ResolutionMode getEmitSyntaxForUsageLocationWorker(
    std::string_view fileName, const SourceFileMetaData& meta,
    Node* usage, const CompilerOptions* options) {
	if (isRequireCall(usage->parent, false) ||
	    (isExternalModuleReference(usage->parent) &&
	     isImportEqualsDeclaration(usage->parent->parent))) {
		return ResolutionModeCommonJS;
	}
	ModuleKind fileEmitMode = getEmitModuleFormatOfFileWorker(
	    fileName, options, meta);
	if (isImportCall(walkUpParenthesizedExpressions(usage->parent))) {
		if (shouldTransformImportCall(fileName, options, fileEmitMode)) {
			return ResolutionModeCommonJS;
		} else {
			return ResolutionModeESM;
		}
	}
	// If we're in --module preserve on an input file, we know that an
	// import is an import. But if this is a declaration file, we'd prefer to
	// use the impliedNodeFormat. Since we want things to be consistent
	// between the two, we need to issue errors when the user writes ESM
	// syntax in a definitely-CJS file, until/unless declaration emit can
	// indicate a true ESM import. On the other hand, writing CJS syntax in
	// a definitely-ESM file is fine, since declaration emit preserves the
	// CJS syntax.
	if (fileEmitMode == ModuleKind::CommonJS) {
		return ModuleKind::CommonJS;
	} else {
		if (moduleKindIsNonNodeESM(fileEmitMode) ||
		    fileEmitMode == ModuleKind::Preserve) {
			return ModuleKind::ESNext;
		}
	}
	return ModuleKind::None;
}

ResolutionMode getModeForUsageLocation(
    std::string_view fileName, const SourceFileMetaData& meta, Node* usage,
    const CompilerOptions* options) {
	if (usage->parent != nullptr &&
	    (isImportDeclaration(usage->parent) ||
	     usage->parent->kind == Kind::JSImportDeclaration ||
	     isExportDeclaration(usage->parent) ||
	     isJSDocImportTag(usage->parent))) {
		bool isTypeOnly =
		    isExclusivelyTypeOnlyImportOrExport(usage->parent);
		if (isTypeOnly) {
			std::pair<ResolutionMode, bool> override_{
			    ResolutionModeNone, false};
			switch (usage->parent->kind) {
				case Kind::ImportDeclaration:
				case Kind::JSImportDeclaration:
					override_ = getResolutionModeOverride(
					    usage->parent->as<ImportDeclaration>()
					        ->Attributes,
					    {});
					break;
				case Kind::ExportDeclaration:
					override_ = getResolutionModeOverride(
					    usage->parent->as<ExportDeclaration>()
					        ->Attributes,
					    {});
					break;
				case Kind::JSDocImportTag:
					override_ = getResolutionModeOverride(
					    usage->parent->as<JSDocImportTag>()
					        ->Attributes,
					    {});
					break;
				default: break;
			}
			if (override_.second)
				return override_.first;
		}
	}
	if (usage->parent != nullptr && isLiteralTypeNode(usage->parent) &&
	    usage->parent->parent != nullptr &&
	    isImportTypeNode(usage->parent->parent)) {
		if (auto [override_, ok] = getResolutionModeOverride(
		        usage->parent->parent->as<ImportTypeNode>()->Attributes,
		        {});
		    ok) {
			return override_;
		}
	}

	if (options != nullptr &&
	    importSyntaxAffectsModuleResolution(options)) {
		return getEmitSyntaxForUsageLocationWorker(fileName, meta, usage,
		                                           options);
	}
	return ResolutionModeNone;
}

// fileloader.go:152 processAllProgramFiles — root tasks + lib set + ATD
// task, then the parser run and collection.
void filesLoader::processAllProgramFiles(
    const std::vector<std::string>& rootFileNames) {
	for (size_t index = 0; index < rootFileNames.size(); index++) {
		addRootFileTask(
		    rootFileNames[index], nullptr,
		    ip->newReason(FileIncludeKind::RootFile,
		                  FileIncludeReason::DataV{
		                      static_cast<int>(index)}));
	}
	if (!rootFileNames.empty() &&
	    compilerOptions->NoLib != Tristate::True) {
		if (compilerOptions->Lib.empty()) { // Lib == nil in Go
			std::string name = std::string(
			    tsoptions::getDefaultLibFileName(compilerOptions));
			LibFile* libFile = pathForLibFile(name);
			addRootTask(libFile->path, libFile,
			            ip->newReason(FileIncludeKind::LibFile,
			                          FileIncludeReason::DataV{
			                              std::monostate{}}));
		} else {
			for (size_t index = 0; index < compilerOptions->Lib.size();
			     index++) {
				if (auto [name, ok] = tsoptions::getLibFileName(
				        compilerOptions->Lib[index]);
				    ok) {
					LibFile* libFile =
					    pathForLibFile(std::string(name));
					addRootTask(libFile->path, libFile,
					            ip->newReason(
					                FileIncludeKind::LibFile,
					                FileIncludeReason::DataV{
					                    static_cast<int>(index)}));
				}
				// !!! error on unknown name
			}
		}
	}

	if (!rootFileNames.empty() && !skipModuleResolution) {
		addAutomaticTypeDirectiveTasks();
	}

	parser->getProcessedFiles(rootTasks);
}

// filesparser.go:330 getProcessedFiles — parse, DFS collect, then the
// lib-sort / allFiles / redirect-index / libResolution tail.
void filesParser::getProcessedFiles(
    const std::vector<parseTask*>& tasks) {
	// parse() — single-threaded DFS.
	start(tasks, 0);

	collectFiles(tasks);

	loader->sortLibs(libFileList);

	// allFiles = append(libFiles, files...)
	std::vector<SourceFile*> allFiles;
	allFiles.reserve(libFileList.size() + files.size());
	for (auto* f : libFileList)
		allFiles.push_back(f);
	for (auto* f : files)
		allFiles.push_back(f);
	files = std::move(allFiles);

	for (auto& [key, redirectFile] : redirectFilesByPath) {
		redirectFile.index += static_cast<int>(libFileList.size());
	}

	// pathForLibFileResolutions → resolvedModules (sorted keys)
	std::vector<tspath::Path> keys;
	keys.reserve(loader->pathForLibFileResolutions.size());
	for (auto& [key, value] : loader->pathForLibFileResolutions) {
		keys.push_back(key);
	}
	std::sort(keys.begin(), keys.end());
	for (auto& key : keys) {
		auto& value = loader->pathForLibFileResolutions[key];
		resolvedModules[key] =
		    module::ModeAwareCache<module::ResolvedModule*>{
		        {module::ModeAwareCacheKey{value.libraryName,
		                                   ModuleKind::CommonJS},
		         value.resolution}};
	}
}

}  // namespace tsc::compiler
