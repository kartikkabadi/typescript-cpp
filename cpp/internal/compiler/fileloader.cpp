// --- fileloader.go + filesparser.go — program slice ---
// Single-threaded port: Go runs the same logic through a WorkGroup; the
// observable result (filesByPath / include reasons / diagnostics) is
// order-stable, so a synchronous DFS is faithful.
#include "internal/compiler/program.h"
#include "internal/core/utilities.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/parser/parser.h"
#include "internal/project/parsecache.h"
#include "internal/scanner/scanner.h"
#include "internal/vfs/cachedvfs/cachedvfs.h"

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
	// Serializes the cache fills — called concurrently by parse workers
	// (Go: pathForLibFileCache / pathForLibFileResolutions are SyncMaps).
	std::lock_guard<std::mutex> lock(libMu);
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
		// fileloader.go:989 — resolveLibrary's `defer tr.Push(PhaseProgram,
		// "resolveLibrary", {"resolveFrom"}, false)()`.
		tracing::TraceScope traceResolveLibrary(
		    tracing, tracing::PhaseProgram, "resolveLibrary",
		    tracing::TraceArgs{{"resolveFrom", resolveFrom}}, false);
		// fileloader.go:991-996 — Go records the first resolver error
		// via moduleResolutionErrorOnce; C++ catches the throw.
		std::shared_ptr<module::ResolvedModule> resolutionShared;
		std::vector<module::DiagAndArgs> libTrace;
		try {
			std::tie(resolutionShared, libTrace) =
			    resolver->ResolveModuleName(
			        libraryName, resolveFrom, ModuleKind::CommonJS,
			        nullptr);
		} catch (const std::exception& e) {
			moduleResolutionErrorOnce.run([&] {
				moduleResolutionError = gostd::newError(e.what());
			});
		}
		module::ResolvedModule* resolution = resolutionShared.get();
		if (resolution != nullptr) {
			// raw ptr outlives resolutionShared — keep the result alive
			// (cache.LoadOrStore losers aren't owned by the cache).
			std::lock_guard<std::mutex> arenaLock(parser->arenaMu);
			parser->resolvedModuleKeepAlive.push_back(
			    std::move(resolutionShared));
		}
		if (resolution != nullptr && resolution->IsResolved()) {
			path = resolution->ResolvedFileName;
			replaced = true;
		}
		pathForLibFileResolutions.emplace(
		    toPath(resolveFrom),
		    libResolution{libraryName, resolution, std::move(libTrace)});
	}

	auto lib = std::make_unique<LibFile>(LibFile{name, path, replaced});
	auto* ptr = lib.get();
	pathForLibFileCache.emplace(name, std::move(lib));
	return ptr;
}

// fileloader.go:951 createSyntheticImport
Node* filesLoader::createSyntheticImport(const std::string& text,
                                         SourceFile* file) {
	// fileloader.go:58 — the loader's NodeFactory is shared; Go guards it
	// with factoryMu because imports can be synthesized on parse workers.
	std::lock_guard<std::mutex> lock(factoryMu);
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

// filesparser.go:185 redirect — replaced work now runs as its own task
// through the parser's arena.
void parseTask::redirect(filesLoader* loader,
                         const std::string& fileName) {
	// increaseDepth and elideOnDepth are not copied to redirects,
	// otherwise their depth would be double counted (filesparser.go:191).
	redirectedParseTask = loader->parser->newTask(
	    tspath::normalizePath(fileName));
	redirectedParseTask->libFile = libFile;
	redirectedParseTask->includeReason = includeReason;
	subTasks = {redirectedParseTask};
}

// fileloader.go:413 parseSourceFile
SourceFile* filesLoader::parseSourceFile(parseTask* t) {
	// fileloader.go:415 — `defer p.opts.Tracing.Push(PhaseParse,
	// "createSourceFile", {"path"}, true)()`.
	tracing::TraceScope traceCreateSourceFile(
	    tracing, tracing::PhaseParse, "createSourceFile",
	    tracing::TraceArgs{{"path", t->normalizedFilePath}}, true);
	tspath::Path path = toPath(t->normalizedFilePath);
	// fileloader.go:414 — the redirected project's options when this file
	// comes from a project reference.
	const CompilerOptions* options =
	    projectReferenceFileMapper->getCompilerOptionsForFile(
	        HasFileName{t->normalizedFilePath, t->path});
	SourceFileParseOptions parseOptions{
	    .FileName = t->normalizedFilePath,
	    .Path = path,
	    .ExternalModuleIndicatorOptions =
	        getExternalModuleIndicatorOptions(t->normalizedFilePath, options,
	                                          t->metadata),
	};
	// tspath.FileExtensionIsOneOf(t.normalizedFilePath, mapperExtensions)
	if (std::any_of(contentMapperExtensions.begin(),
	                contentMapperExtensions.end(), [&](const std::string& ext) {
		                return tspath::fileExtensionIs(t->normalizedFilePath, ext);
	                })) {
		return parseContentMappedFile(parseOptions);
	}
	return host->GetSourceFile(parseOptions, t->metadata);
}

// fileloader.go:32 maxContentMapperFailures — transform failures a single
// content mapper may accumulate before it is disabled for the rest of the
// program.
static constexpr int maxContentMapperFailures = 5;

// fileloader.go:467 contentMapperTransformDiagnosticChain.
static Diagnostic* contentMapperTransformDiagnosticChain(
    SourceFile* file, const std::string& label,
    const DiagnosticMessage* message, std::vector<std::string> args = {});

// fileloader.go:545 contentMapperTransformDiagnosticWithDetail.
static Diagnostic* contentMapperTransformDiagnosticWithDetail(
    SourceFile* file, const std::string& label, Diagnostic* detail) {
	return newDiagnostic(
	       file, TextRange{0, 0},
	       The_content_mapper_0_failed_to_transform_this_file,
	       {label})
	    ->AddMessageChain(detail);
}

static Diagnostic* contentMapperTransformDiagnosticChain(
    SourceFile* file, const std::string& label,
    const DiagnosticMessage* message, std::vector<std::string> args) {
	return contentMapperTransformDiagnosticWithDetail(
	    file, label, tsoptions::newCompilerDiagnostic(message, args));
}

// fileloader.go:556 contentMapperMappingDiagnostic — diagnostic reported
// against a mapper that produced an invalid span map, including the
// offsets involved.
static Diagnostic* contentMapperMappingDiagnostic(
    SourceFile* file, const std::string& label,
    const spanmap::MappingError* problem) {
	TextRange loc{0, 0};
	switch (problem->Kind) {
	case spanmap::MappingErrorKindOverlap:
		return newDiagnostic(
		    file, loc,
		    The_content_mapper_0_produced_overlapping_or_out_of_order_position_mappings_near_virtual_offset_1,
		    {label, std::to_string(problem->VirtualPos)});
	case spanmap::MappingErrorKindOutOfBounds:
		return newDiagnostic(
		    file, loc,
		    The_content_mapper_0_produced_a_position_mapping_that_points_outside_the_original_content_original_offset_1,
		    {label, std::to_string(problem->OriginalPos)});
	case spanmap::MappingErrorKindVerbatimMismatch:
		return newDiagnostic(
		    file, loc,
		    The_content_mapper_0_produced_a_verbatim_mapping_that_does_not_match_the_original_content_virtual_offset_1_original_offset_2,
		    {label, std::to_string(problem->VirtualPos),
		     std::to_string(problem->OriginalPos)});
	case spanmap::MappingErrorKindKind:
		return newDiagnostic(
		    file, loc,
		    The_content_mapper_0_produced_a_position_mapping_with_an_invalid_kind_near_virtual_offset_1,
		    {label, std::to_string(problem->VirtualPos)});
	case spanmap::MappingErrorKindFeature:
		return newDiagnostic(
		    file, loc,
		    The_content_mapper_0_produced_invalid_mapping_features_near_original_offset_1,
		    {label, std::to_string(problem->OriginalPos)});
	default:
		return newDiagnostic(
		    file, loc,
		    The_content_mapper_0_did_not_provide_the_required_position_mappings,
		    {label});
	}
}

// fileloader.go:522 ContentMapperProjectErrorDiagnostic — localized
// diagnostic message for a project setup error.
const DiagnosticMessage*
ContentMapperProjectErrorDiagnostic(const gostd::Error& err) {
	if (auto* projectError =
	        gostd::errorAs<contentmapper::ProjectError*>(err)) {
		switch (projectError->Kind) {
		case contentmapper::ProjectErrorKindMalformedResponse:
			return The_content_mapper_returned_a_project_response_that_could_not_be_decoded;
		case contentmapper::ProjectErrorKindMissingConfigIdentity:
			return The_content_mapper_did_not_return_configIdentity_which_is_required_when_the_content_mapper_has_dynamicConfig_Colon_true_in_its_package_json;
		case contentmapper::ProjectErrorKindNonAbsoluteWatchedFile:
			return The_content_mapper_returned_a_non_absolute_path_in_watchedFiles;
		case contentmapper::ProjectErrorKindUnexpectedConfigIdentity:
			return The_content_mapper_returned_configIdentity_which_is_only_allowed_when_it_declares_dynamicConfig_Colon_true_in_its_package_json;
		case contentmapper::ProjectErrorKindUnexpectedWatchedFiles:
			return The_content_mapper_returned_watchedFiles_which_is_only_allowed_when_it_declares_dynamicConfig_Colon_true_in_its_package_json;
		}
	}
	return The_content_mapper_process_failed_while_handling_the_project_request;
}

// fileloader.go:467 contentMapperTransformDiagnostic.
static Diagnostic* contentMapperTransformDiagnostic(
    SourceFile* file, const std::string& label, const gostd::Error& err) {
	if (auto* collision =
	        gostd::errorAs<contentmapper::SupplementalFileCollisionError*>(
	            err)) {
		return contentMapperTransformDiagnosticChain(
		    file, label,
		    Content_mapper_supplemental_output_file_0_conflicts_with_an_existing_file,
		    {collision->FileName});
	}
	if (auto* transformError =
	        gostd::errorAs<contentmapper::TransformError*>(err)) {
		switch (transformError->Kind) {
		case contentmapper::TransformErrorKindInitialize: {
			if (auto* initializeError =
			        gostd::errorAs<contentmapper::InitializeError*>(err)) {
				switch (initializeError->Kind) {
				case contentmapper::InitializeErrorKindPositionEncoding:
					return contentMapperTransformDiagnosticChain(
					    file, label,
					    The_content_mapper_selected_unsupported_position_encoding_0,
					    {initializeError->PositionEncoding});
				case contentmapper::InitializeErrorKindEmptyDiagnosticSource:
					return contentMapperTransformDiagnosticChain(
					    file, label,
					    The_content_mapper_diagnostic_source_must_not_be_empty);
				case contentmapper::InitializeErrorKindReservedDiagnosticSource:
					return contentMapperTransformDiagnosticChain(
					    file, label,
					    The_content_mapper_diagnostic_source_0_is_reserved_by_TypeScript,
					    {initializeError->DiagnosticSource});
				}
			}
			return contentMapperTransformDiagnosticChain(
			    file, label,
			    The_content_mapper_process_could_not_be_started_or_initialized);
		}
		case contentmapper::TransformErrorKindProject:
			return contentMapperTransformDiagnosticChain(
			    file, label, ContentMapperProjectErrorDiagnostic(err));
		case contentmapper::TransformErrorKindRequest:
			return contentMapperTransformDiagnosticChain(
			    file, label,
			    The_content_mapper_process_failed_while_handling_the_transform_request);
		case contentmapper::TransformErrorKindResponse: {
			if (auto* extensionError = gostd::errorAs<
			        contentmapper::InvalidVirtualExtensionError*>(err)) {
				return contentMapperTransformDiagnosticChain(
				    file, label,
				    The_content_mapper_returned_an_output_with_unsupported_virtual_extension_0,
				    {extensionError->Extension});
			}
			if (auto* directiveError = gostd::errorAs<
			        contentmapper::DiagnosticDirectiveError*>(err)) {
				Diagnostic* detail = nullptr;
				switch (directiveError->Kind) {
				case contentmapper::DiagnosticDirectiveErrorKindInvalidRange:
					detail = tsoptions::newCompilerDiagnostic(
					    Diagnostic_directive_0_returned_by_the_content_mapper_has_an_invalid_range,
					    {std::to_string(directiveError->Index)});
					break;
				case contentmapper::DiagnosticDirectiveErrorKindInvalidPolicy:
					detail = tsoptions::newCompilerDiagnostic(
					    The_content_mapper_returned_a_diagnostic_directive_with_invalid_policy_0,
					    {std::to_string(directiveError->Policy)});
					break;
				case contentmapper::DiagnosticDirectiveErrorKindExpectMissingUnusedDiagnostic:
					detail = tsoptions::newCompilerDiagnostic(
					    Diagnostic_directive_0_returned_by_the_content_mapper_must_specify_unusedExpectDirectiveIndex_when_there_is_not_exactly_one_unusedExpectDirectiveDiagnostics_entry,
					    {std::to_string(directiveError->Index)});
					break;
				case contentmapper::DiagnosticDirectiveErrorKindInvalidUnusedDiagnosticIndex:
					detail = tsoptions::newCompilerDiagnostic(
					    Diagnostic_directive_0_returned_by_the_content_mapper_has_an_invalid_unusedExpectDirectiveIndex,
					    {std::to_string(directiveError->Index)});
					break;
				case contentmapper::DiagnosticDirectiveErrorKindOverlap:
					detail = tsoptions::newCompilerDiagnostic(
					    The_content_mapper_returned_diagnostic_directives_with_overlapping_virtual_ranges);
					break;
				}
				if (detail != nullptr) {
					if (directiveError->SupplementalIndex >= 0) {
						detail = newDiagnosticChain(
						    detail,
						    The_invalid_diagnostic_directive_is_in_supplemental_output_0_returned_by_the_content_mapper,
						    {std::to_string(
						        directiveError->SupplementalIndex)});
					}
					return contentMapperTransformDiagnosticWithDetail(
					    file, label, detail);
				}
			}
			return contentMapperTransformDiagnosticChain(
			    file, label,
			    The_content_mapper_returned_an_invalid_transform_response);
		}
		case contentmapper::TransformErrorKindMappings:
			return newDiagnostic(
			    file, TextRange{0, 0},
			    The_content_mapper_0_did_not_provide_the_required_position_mappings,
			    {label});
		default:
			break;
		}
	}
	return newDiagnostic(
	    file, TextRange{0, 0},
	    The_content_mapper_0_failed_to_transform_this_file, {label});
}

// fileloader.go:578 getContentMapperTransformIdentity.
std::string filesLoader::getContentMapperTransformIdentity(
    contentmapper::Mapper* mapper) {
	if (contentmapper::Project* project = host->ContentMapperProject();
	    project != nullptr) {
		if (auto [identity, ierr] = project->Identity(mapper);
		    ierr == nullptr) {
			return identity;
		}
	}
	// fmt.Sprintf("%x", bytes) — lowercase hex of the 16-byte fingerprint.
	static constexpr char hexdigits[] = "0123456789abcdef";
	std::string hex;
	hex.reserve(32);
	for (uint8_t b :
	     mapper->TransformIdentity(compilerOptions).Bytes()) {
		hex += hexdigits[b >> 4];
		hex += hexdigits[b & 0xF];
	}
	return hex;
}

// fileloader.go:587 emptyContentMappedFile — empty TS source file retaining
// the original content for diagnostics; importers see an empty module. It
// is still marked content-mapped so it is excluded from emit.
SourceFile* filesLoader::emptyContentMappedFile(
    SourceFileParseOptions& opts, const std::string& mapperIdentity,
    const std::string& transformIdentity) {
	auto content = host->fs->ReadFile(opts.FileName);
	SourceFile* sourceFile = tsc::parseSourceFile(
	    opts, "", ScriptKind::TS);
	sourceFile->SetContentMapperInfo(ContentMapperSourceFileInfo{
	    /*ContentMapper*/ mapperIdentity,
	    /*TransformIdentity*/ transformIdentity,
	    /*ParseOptions*/ opts,
	    /*VirtualFileName*/ opts.FileName +
	        std::string(tspath::extensionTs),
	    /*OriginalText*/ content.first,
	});
	return sourceFile;
}

// fileloader.go:602 ContentMapperInitializationDiagnostic — fileless
// diagnostic for a mapper initialization failure.
static Diagnostic* contentMapperInitializationDiagnostic(
    const std::string& labelIn, const gostd::Error& err) {
	auto* initializeError =
	    gostd::errorAs<contentmapper::InitializeError*>(err);
	std::string label = labelIn;
	if (initializeError != nullptr && label.empty()) {
		label = initializeError->MapperName;
	}
	Diagnostic* diagnostic = tsoptions::newCompilerDiagnostic(
	    The_content_mapper_0_could_not_be_initialized, {label});
	if (initializeError != nullptr) {
		switch (initializeError->Kind) {
		case contentmapper::InitializeErrorKindProcessStart:
			return diagnostic->AddMessageChain(tsoptions::newCompilerDiagnostic(
			    The_content_mapper_command_0_could_not_be_started_Colon_1,
			    {initializeError->Command, initializeError->Detail}));
		case contentmapper::InitializeErrorKindProcessExit:
			return diagnostic->AddMessageChain(tsoptions::newCompilerDiagnostic(
			    The_content_mapper_process_exited_before_responding_to_the_initialize_request_exit_code_0,
			    {std::to_string(initializeError->ExitCode)}));
		case contentmapper::InitializeErrorKindNoResponse:
			return diagnostic->AddMessageChain(tsoptions::newCompilerDiagnostic(
			    The_content_mapper_did_not_respond_to_the_initialize_request_within_0_seconds,
			    {std::to_string(initializeError->TimeoutSeconds)}));
		case contentmapper::InitializeErrorKindInvalidResponse:
			return diagnostic->AddMessageChain(tsoptions::newCompilerDiagnostic(
			    The_content_mapper_returned_an_initialize_response_that_could_not_be_decoded_Colon_0,
			    {initializeError->Detail}));
		case contentmapper::InitializeErrorKindRequest:
			return diagnostic->AddMessageChain(tsoptions::newCompilerDiagnostic(
			    The_content_mapper_s_initialize_request_failed_Colon_0,
			    {initializeError->Detail}));
		case contentmapper::InitializeErrorKindPositionEncoding:
			return diagnostic->AddMessageChain(tsoptions::newCompilerDiagnostic(
			    The_content_mapper_selected_unsupported_position_encoding_0,
			    {initializeError->PositionEncoding}));
		case contentmapper::InitializeErrorKindEmptyDiagnosticSource:
			return diagnostic->AddMessageChain(tsoptions::newCompilerDiagnostic(
			    The_content_mapper_diagnostic_source_must_not_be_empty));
		case contentmapper::InitializeErrorKindReservedDiagnosticSource:
			return diagnostic->AddMessageChain(tsoptions::newCompilerDiagnostic(
			    The_content_mapper_diagnostic_source_0_is_reserved_by_TypeScript,
			    {initializeError->DiagnosticSource}));
		}
	}
	return diagnostic->AddMessageChain(tsoptions::newCompilerDiagnostic(
	    The_content_mapper_process_could_not_be_started_or_initialized));
}

// fileloader.go:630 ContentMapperProjectDiagnostic — fileless diagnostic
// for project setup or mapper initialization.
Diagnostic* ContentMapperProjectDiagnostic(const gostd::Error& err) {
	if (gostd::errorAs<contentmapper::InitializeError*>(err) != nullptr) {
		return contentMapperInitializationDiagnostic("" /*label*/, err);
	}
	return tsoptions::newCompilerDiagnostic(
	    ContentMapperProjectErrorDiagnostic(err));
}

// fileloader.go:637 contentMapperUnavailable — whether mapper failed
// initialization or exceeded its failure budget.
bool filesLoader::contentMapperUnavailable(contentmapper::Mapper* mapper) {
	if (mapper == nullptr) {
		return false;
	}
	std::lock_guard<std::mutex> lock(contentMapperMu);
	return contentMapperInitFailed.count(mapper) != 0 ||
	       contentMapperFailures[mapper] >= maxContentMapperFailures;
}

// fileloader.go:647 recordContentMapperInitializationFailure.
void filesLoader::recordContentMapperInitializationFailure(
    contentmapper::Mapper* mapper, const std::string& label,
    const gostd::Error& err) {
	std::lock_guard<std::mutex> lock(contentMapperMu);
	if (contentMapperInitFailed.count(mapper) != 0) {
		return;
	}
	contentMapperInitFailed.insert(mapper);
	contentMapperDiagnostics.push_back(
	    contentMapperInitializationDiagnostic(label, err));
}

// fileloader.go:660 recordContentMapperFailure — counts a transform failure
// for mapper; returns whether the failure should be reported for this file
// (false once the mapper is already disabled). On the failure that reaches
// maxContentMapperFailures a single program diagnostic disables the mapper.
bool filesLoader::recordContentMapperFailure(contentmapper::Mapper* mapper,
                                             const std::string& label) {
	std::lock_guard<std::mutex> lock(contentMapperMu);
	if (contentMapperFailures[mapper] >= maxContentMapperFailures) {
		return false;
	}
	contentMapperFailures[mapper]++;
	if (contentMapperFailures[mapper] >= maxContentMapperFailures) {
		contentMapperDiagnostics.push_back(tsoptions::newCompilerDiagnostic(
		    The_content_mapper_0_failed_1_times_and_will_not_be_used,
		    {label, std::to_string(maxContentMapperFailures)}));
	}
	return true;
}

// fileloader.go:438 parseContentMappedFile.
SourceFile* filesLoader::parseContentMappedFile(SourceFileParseOptions opts) {
	contentmapper::Mapper* mapper = program->CommandLine()
	                                    ->GetContentMapperForFileName(
	                                        opts.FileName);
	std::string label = mapper->DiagnosticName();
	std::string transformIdentity =
	    getContentMapperTransformIdentity(mapper);
	if (contentMapperUnavailable(mapper)) {
		// The mapper failed initialization or exceeded its failure budget;
		// add the file empty without re-reporting.
		return emptyContentMappedFile(opts, mapper->Identity(),
		                              transformIdentity);
	}
	auto [files, err] = host->GetContentMappedSourceFiles(opts, mapper);
	if (err != nullptr) {
		SourceFile* sourceFile = emptyContentMappedFile(
		    opts, mapper->Identity(), transformIdentity);
		if (auto* transformError =
		        gostd::errorAs<contentmapper::TransformError*>(err);
		    transformError != nullptr &&
		    transformError->Kind ==
		        contentmapper::TransformErrorKindInitialize) {
			recordContentMapperInitializationFailure(mapper, label, err);
			return sourceFile;
		}
		if (recordContentMapperFailure(mapper, label)) {
			Diagnostic* diagnostic;
			if (auto* problem =
			        gostd::errorAs<spanmap::MappingError*>(err);
			    problem != nullptr) {
				diagnostic = contentMapperMappingDiagnostic(
				    sourceFile, label, problem);
			} else {
				diagnostic = contentMapperTransformDiagnostic(
				    sourceFile, label, err);
			}
			sourceFile->diagnostics.push_back(diagnostic);
		}
		return sourceFile;
	}
	return files.Canonical;
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
	// fileloader.go:781 — `defer p.opts.Tracing.Push(PhaseProgram,
	// "resolveTypeReferenceDirectiveNamesWorker",
	// {"containingFileName"}, false)()`.
	tracing::TraceScope traceResolveWorker(
	    tracing, tracing::PhaseProgram,
	    "resolveTypeReferenceDirectiveNamesWorker",
	    tracing::TraceArgs{{"containingFileName", file->FileName()}},
	    false);
	SourceFileMetaData meta = t->metadata;

	auto typeResolutionsInFile =
	    module::ModeAwareCache<module::ResolvedTypeReferenceDirective*>();
	std::vector<module::DiagAndArgs> typeResolutionsTrace;
	for (size_t index = 0; index < file->TypeReferenceDirectives.size();
	     index++) {
		const FileReference* ref = file->TypeReferenceDirectives[index];
		// fileloader.go:788-790 — resolve through the containing
		// project's options when this file comes from a project
		// reference.
		auto [redirect, fileName] =
		    projectReferenceFileMapper->getRedirectForResolution(
		        HasFileName{file->FileName(), file->Path()});
		ResolutionMode resolutionMode =
		    getModeForTypeReferenceDirectiveInFile(
		        *ref, file, meta,
		        module::GetCompilerOptionsWithRedirect(
		            opts->Config->CompilerOptions(), redirect));
		auto [resolvedShared, trace] =
		    resolver->ResolveTypeReferenceDirective(
		        ref->FileName, fileName, resolutionMode, redirect);
		module::ResolvedTypeReferenceDirective* resolved =
		    resolvedShared.get();
		// fileloader.go:793 — non-defer `traceDone = p.opts.Tracing.Push(...)`;
		// invoked at the end of the iteration.
		std::function<void()> traceDone;
		if (tracing != nullptr) {
			traceDone = tracing->Push(
			    tracing::PhaseProgram, "processTypeReferenceDirective",
			    tracing::TraceArgs{
			        {"directive", ref->FileName},
			        {"hasResolved",
			         resolved != nullptr && resolved->IsResolved()},
			        {"refKind",
			         (int)FileIncludeKind::TypeReferenceDirective},
			        {"refPath", std::string(t->path)}},
			    false);
		}
		typeResolutionsInFile[module::ModeAwareCacheKey{ref->FileName,
		                                              resolutionMode}] =
		    resolved;
		const FileIncludeReason* includeReason = ip->newReason(
		    FileIncludeKind::TypeReferenceDirective,
		    referencedFileData{t->path, static_cast<int>(index), nullptr});
		// fileloader.go:803 — capture the resolver's trace (the C++
		// resolver returns it alongside the result).
		typeResolutionsTrace.insert(typeResolutionsTrace.end(),
		                          trace.begin(), trace.end());

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
		if (traceDone) {
			traceDone();
		}
	}

	t->typeResolutionsInFile = std::move(typeResolutionsInFile);
	t->typeResolutionsTrace = std::move(typeResolutionsTrace);
}

// fileloader.go:823 resolveImportsAndModuleAugmentations
static const char* externalHelpersModuleNameText = "tslib";

void filesLoader::resolveImportsAndModuleAugmentations(parseTask* t) {
	// fileloader.go:832 — `defer p.opts.Tracing.Push(PhaseProgram,
	// "resolveModuleNamesWorker", {"containingFileName"}, false)()`.
	tracing::TraceScope traceResolveModules(
	    tracing, tracing::PhaseProgram, "resolveModuleNamesWorker",
	    tracing::TraceArgs{{"containingFileName", t->file->FileName()}},
	    false);
	SourceFile* file = t->file;
	SourceFileMetaData meta = t->metadata;

	std::vector<Node*> moduleNames;
	moduleNames.reserve(file->imports.size() +
	                    file->ModuleAugmentations.size() + 2);

	bool isJavaScriptFile = isSourceFileJS(file);
	bool isExternalModuleFile = isExternalModule(file);

	// fileloader.go:842-843 — resolve through the containing project's
	// options when this file comes from a project reference; fileName is
	// the mapped source name.
	auto [redirect, fileName] =
	    projectReferenceFileMapper->getRedirectForResolution(
	        HasFileName{file->FileName(), file->Path()});
	const CompilerOptions* optionsForFile =
	    module::GetCompilerOptionsWithRedirect(
	        opts->Config->CompilerOptions(), redirect);
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
			std::string moduleName = entry->text();
			if (moduleName.empty()) {
				continue;
			}

			ResolutionMode mode = getModeForUsageLocation(
			    file->FileName(), meta, entry, optionsForFile);
			// fileloader.go:889-901 — Go's resolver returns an error
			// recorded through moduleResolutionErrorOnce; the C++
			// callbackModuleResolver throws instead, so catch and
			// record once, continuing with a nil resolution as Go
			// does.
			std::shared_ptr<module::ResolvedModule> resolvedShared;
			std::vector<module::DiagAndArgs> trace;
			try {
				std::tie(resolvedShared, trace) =
				    resolver->ResolveModuleName(std::string(moduleName),
				                                std::string(fileName),
				                                mode, redirect);
			} catch (const std::exception& e) {
				moduleResolutionErrorOnce.run([&] {
					moduleResolutionError = gostd::newError(e.what());
				});
			}
			module::ResolvedModule* resolvedModule =
			    resolvedShared.get();
			if (resolvedModule == nullptr) {
				std::lock_guard<std::mutex> arenaLock(parser->arenaMu);
				resolvedModule =
				    &parser->resolvedModuleArena.emplace_back();
			} else {
				// Raw pointer kept in resolutionsInFile — anchor the
				// shared_ptr so LoadOrStore losers stay alive (Go: GC).
				std::lock_guard<std::mutex> arenaLock(parser->arenaMu);
				parser->resolvedModuleKeepAlive.push_back(
				    std::move(resolvedShared));
			}
			resolutionsInFile[module::ModeAwareCacheKey{
			    std::string(moduleName), mode}] = resolvedModule;
			resolutionsTrace.insert(resolutionsTrace.end(),
			                        trace.begin(), trace.end());

			if (!resolvedModule->IsResolved()) {
				continue;
			}

			const std::string& ResolvedFileName =
			    resolvedModule->ResolvedFileName;
			bool isFromNodeModulesSearch =
			    resolvedModule->IsExternalLibraryImport;
			// fileloader.go:911 — Don't treat redirected files as JS
			// files.
			bool isJsFile =
			    !resolvedModule->ResolvedUsingExtraExtensions &&
			    !tspath::fileExtensionIsOneOf(
			        ResolvedFileName,
			        tspath::supportedTSExtensionsWithJsonFlat) &&
			    projectReferenceFileMapper
			            ->getRedirectParsedCommandLineForResolution(
			                HasFileName{ResolvedFileName,
			                            toPath(ResolvedFileName)}) == nullptr;
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
	// filesparser.go:197 — `defer loader.opts.Tracing.Push(PhaseProgram,
	// "processTypeReferences", nil, false)()` (wraps the
	// loadAutomaticTypeDirectives equivalent).
	tracing::TraceScope traceProcessTypeRefs(
	    tracing, tracing::PhaseProgram, "processTypeReferences",
	    [&] { return tracing::TraceArgs{}; }, false);
	std::vector<std::string> automaticTypeDirectiveNames =
	    module::GetAutomaticTypeDirectiveNames(*compilerOptions, host);
	if (!automaticTypeDirectiveNames.empty()) {
		for (auto& name : automaticTypeDirectiveNames) {
			// Under node16/nodenext module resolution, load `types`/ata
			// include names as cjs resolution results by passing an
			// "undefined" mode. Under bundler, this also triggers the
			// "import" condition to be used.
			ResolutionMode resolutionMode = ResolutionModeNone;
			auto [resolvedShared, trace] =
			    resolver->ResolveTypeReferenceDirective(
			        name, t->normalizedFilePath, resolutionMode, nullptr);
			module::ResolvedTypeReferenceDirective* resolved =
			    resolvedShared.get();
			// fileloader.go:304 — `traceDone = opts.Tracing.Push(PhaseProgram,
			// "processTypeReferenceDirective", {...}, false)` — non-defer;
			// called at the end of the iteration.
			std::function<void()> traceDone;
			if (tracing != nullptr) {
				traceDone = tracing->Push(
				    tracing::PhaseProgram, "processTypeReferenceDirective",
				    tracing::TraceArgs{
				        {"directive", name},
				        {"hasResolved",
				         resolved != nullptr && resolved->IsResolved()},
				        {"refKind",
				         (int)FileIncludeKind::AutomaticTypeDirectiveFile}},
				    false);
			}
			t->typeResolutionsInFile[module::ModeAwareCacheKey{
			    name, resolutionMode}] = resolved;
			// fileloader.go:307 — capture the resolver's trace.
			t->typeResolutionsTrace.insert(
			    t->typeResolutionsTrace.end(), trace.begin(),
			    trace.end());
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
			if (traceDone) {
				traceDone();
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
	// fileloader.go:252-257 — resolution roots at the config file when
	// there is one, else the current directory.
	std::string containingFile =
	    opts->Config != nullptr && opts->Config->ConfigFile != nullptr &&
	            opts->Config->ConfigFile->SourceFile != nullptr
	        ? opts->Config->ConfigFile->SourceFile->FileName()
	        : currDir;
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
// projectreferencedtsfakinghost.go / projectreferencefilemapper.go /
// projectreferenceparser.go
// ===========================================================================

// projectreferencedtsfakinghost.go:55 UseCaseSensitiveFileNames —
// delegates to the real host's fs (out-of-line: ProgramOptions is
// forward-declared where the struct sits).
bool projectReferenceDtsFakingVfs::UseCaseSensitiveFileNames() {
	return mapper->opts->Host->FS()->UseCaseSensitiveFileNames();
}

// projectreferencedtsfakinghost.go:81 ReadFile — passthrough to the real
// fs (a faked dts has no content to serve).
std::pair<std::string, bool>
projectReferenceDtsFakingVfs::ReadFile(const std::string& path) {
	return mapper->opts->Host->FS()->ReadFile(path);
}

// projectreferencedtsfakinghost.go:60 FileExists — a real file wins;
// a missing .d.ts is faked from its project-reference source.
bool projectReferenceDtsFakingVfs::FileExists(const std::string& path) {
	if (mapper->opts->Host->FS()->FileExists(path)) {
		return true;
	}
	if (!tspath::isDeclarationFileName(path)) {
		return false;
	}
	// Project references go to source file instead of .d.ts file.
	return fileOrDirectoryExistsUsingSource(path, /*isFile*/ true);
}

// projectreferencedtsfakinghost.go:98 DirectoryExists.
bool projectReferenceDtsFakingVfs::DirectoryExists(
    const std::string& path) {
	if (mapper->opts->Host->FS()->DirectoryExists(path)) {
		handleDirectoryCouldBeSymlink(path);
		return true;
	}
	return fileOrDirectoryExistsUsingSource(path, /*isFile*/ false);
}

// projectreferencedtsfakinghost.go:117 Realpath — a symlinked dts we
// recorded reports its real path; everything else defers to the fs.
std::string projectReferenceDtsFakingVfs::Realpath(
    const std::string& path) {
	if (auto [result, ok] =
	        knownSymlinks.Files()->Load(toPath(path));
	    ok) {
		return result;
	}
	return mapper->opts->Host->FS()->Realpath(path);
}

// projectreferencedtsfakinghost.go:125 toPath.
tspath::Path projectReferenceDtsFakingVfs::toPath(
    const std::string& path) const {
	return tspath::toPath(
	    path, mapper->opts->Host->GetCurrentDirectory(),
	    mapper->opts->Host->FS()->UseCaseSensitiveFileNames());
}

// projectreferencedtsfakinghost.go:129 handleDirectoryCouldBeSymlink —
// record a node_modules directory's symlink target in knownSymlinks.
void projectReferenceDtsFakingVfs::handleDirectoryCouldBeSymlink(
    const std::string& directory) {
	if (symlinks::containsIgnoredPath(directory)) {
		return;
	}
	// Because we already watch node_modules, handle symlinks in there.
	if (directory.find("/node_modules/") == std::string::npos) {
		return;
	}
	tspath::Path directoryPath{tspath::ensureTrailingDirectorySeparator(
	    std::string_view(toPath(directory)))};
	if (knownSymlinks.Directories()->Load(directoryPath).second) {
		return;
	}
	std::string realDirectory = Realpath(directory);
	if (realDirectory == directory) {
		// not symlinked
		return;
	}
	tspath::Path realPath{tspath::ensureTrailingDirectorySeparator(
	    std::string_view(toPath(realDirectory)))};
	if (realPath == directoryPath) {
		// not symlinked
		return;
	}
	knownSymlinks.SetDirectory(
	    directory, directoryPath,
	    std::make_shared<symlinks::KnownDirectoryLink>(
	        symlinks::KnownDirectoryLink{
	            tspath::ensureTrailingDirectorySeparator(realDirectory),
	            realPath}));
}

// projectreferencedtsfakinghost.go:160
// fileOrDirectoryExistsUsingSource.
bool projectReferenceDtsFakingVfs::fileOrDirectoryExistsUsingSource(
    const std::string& fileOrDirectory, bool isFile) {
	// Check current directory or file.
	Tristate result = isFile
	                      ? fileExistsIfProjectReferenceDts(fileOrDirectory)
	                      : directoryExistsIfProjectReferenceDeclDir(
	                            fileOrDirectory);
	if (result != Tristate::Unknown) {
		return result == Tristate::True;
	}

	tspath::Path fileOrDirectoryPath = toPath(fileOrDirectory);
	if (fileOrDirectoryPath.find("/node_modules/") ==
	    std::string::npos) {
		return false;
	}
	// Check if the directory or file is a symlinked package.
	if (std::string packageRoot = module::ParseNodeModuleFromPath(
	        fileOrDirectory, /*isFolder*/ true);
	    !packageRoot.empty()) {
		handleDirectoryCouldBeSymlink(packageRoot);
	}
	auto* knownDirectoryLinks = knownSymlinks.Directories();
	if (knownDirectoryLinks->Size() == 0) {
		return false;
	}
	if (isFile) {
		if (knownSymlinks.Files()->Load(fileOrDirectoryPath).second) {
			return true;
		}
	}

	// If it contains node_modules check if its one of the symlinked
	// paths we know of.
	bool exists = false;
	knownDirectoryLinks->Range(
	    [&](tspath::Path directoryPath,
	        std::shared_ptr<symlinks::KnownDirectoryLink> link) -> bool {
		    std::string_view dirPrefix = directoryPath;
		    if (fileOrDirectoryPath.compare(0, dirPrefix.size(),
		                                    dirPrefix) != 0) {
			    return true; // keep ranging
		    }
		    std::string relative{
		        std::string_view(fileOrDirectoryPath)
		            .substr(dirPrefix.size())};
		    Tristate sub = isFile
		                       ? fileExistsIfProjectReferenceDts(
		                             link->RealPath + relative)
		                       : directoryExistsIfProjectReferenceDeclDir(
		                             link->RealPath + relative);
		    if (tristateIsTrue(sub)) {
			    exists = true;
			    if (isFile) {
				    // Store the real path for the file.
				    std::string absolutePath =
				        tspath::getNormalizedAbsolutePath(
				            fileOrDirectory,
				            mapper->opts->Host->GetCurrentDirectory());
				    knownSymlinks.SetFile(
				        absolutePath, fileOrDirectoryPath,
				        link->Real +
				            absolutePath.substr(dirPrefix.size()));
			    }
			    return false; // stop ranging
		    }
		    return true;
	    });
	return exists;
}

// projectreferencedtsfakinghost.go:211 fileExistsIfProjectReferenceDts.
Tristate projectReferenceDtsFakingVfs::fileExistsIfProjectReferenceDts(
    const std::string& file) {
	tsoptions::SourceOutputAndProjectReference* source =
	    mapper->getProjectReferenceFromOutputDts(toPath(file));
	if (source != nullptr) {
		return mapper->opts->Host->FS()->FileExists(source->Source)
		           ? Tristate::True
		           : Tristate::False;
	}
	return Tristate::Unknown;
}

// projectreferencedtsfakinghost.go:219
// directoryExistsIfProjectReferenceDeclDir.
Tristate
projectReferenceDtsFakingVfs::directoryExistsIfProjectReferenceDeclDir(
    const std::string& dir) {
	tspath::Path dirPath = toPath(dir);
	for (const tspath::Path& declDirPath : dtsDirectories->Keys()) {
		if (tspath::pathContainsPath(dirPath, declDirPath) ||
		    tspath::pathContainsPath(declDirPath, dirPath)) {
			return Tristate::True;
		}
	}
	return Tristate::Unknown;
}

// projectreferencedtsfakinghost.go:23 newProjectReferenceDtsFakingHost —
// the faking vfs, its cachedvfs wrapper, and the host all live on the
// mapper so the resolver's captured Host stays valid after parse.
module::ResolutionHost* newProjectReferenceDtsFakingHost(
    filesLoader* loader) {
	auto* mapper = loader->projectReferenceFileMapper.get();
	mapper->fakingVfsImpl =
	    std::make_unique<projectReferenceDtsFakingVfs>();
	mapper->fakingVfsImpl->mapper = mapper;
	// projectreferencedtsfakinghost.go:29 — Go copies the map header:
	// share the shared_ptr so the Set outlives this stack-local loader.
	mapper->fakingVfsImpl->dtsDirectories = loader->dtsDirectories;
	mapper->fakingVfs =
	    vfs::cachedvfs::From(mapper->fakingVfsImpl.get());
	mapper->fakingHost =
	    std::make_unique<projectReferenceDtsFakingHost>();
	mapper->fakingHost->host = loader->opts->Host;
	mapper->fakingHost->fs = mapper->fakingVfs.get();
	return mapper->fakingHost.get();
}

// projectreferencefilemapper.go:28 rootConfigPath.
tspath::Path projectReferenceFileMapper::rootConfigPath() const {
	if (opts->Config == nullptr || opts->Config->ConfigFile == nullptr ||
	    opts->Config->ConfigFile->SourceFile == nullptr) {
		return {};
	}
	return opts->Config->ConfigFile->SourceFile->Path();
}

// projectreferencefilemapper.go:35 getParseFileRedirect — map a
// project-reference dts to its source (useSourceOfProjectReference) or a
// project-reference source to its dts.
std::string projectReferenceFileMapper::getParseFileRedirect(
    const HasFileName& file) {
	if (opts->canUseProjectReferenceSource()) {
		// Map to source file from project reference.
		tsoptions::SourceOutputAndProjectReference* source =
		    getProjectReferenceFromOutputDts(file.Path());
		if (source == nullptr) {
			source = getSourceToDtsIfSymlink(file);
		}
		if (source != nullptr) {
			return source->Source;
		}
	} else {
		// Map to dts file from project reference.
		tsoptions::SourceOutputAndProjectReference* output =
		    getProjectReferenceFromSource(file.Path());
		if (output != nullptr && !output->OutputDts.empty()) {
			return output->OutputDts;
		}
	}
	return {};
}

// projectreferencefilemapper.go:55 getResolvedProjectReferences.
std::vector<tsoptions::ParsedCommandLine*>
projectReferenceFileMapper::getResolvedProjectReferences() {
	std::vector<tsoptions::ParsedCommandLine*> result;
	auto it = referencesInConfigFile.find(rootConfigPath());
	if (it != referencesInConfigFile.end()) {
		result.reserve(it->second.size());
		for (const tspath::Path& refPath : it->second) {
			auto cfgIt = configToProjectReference.find(refPath);
			result.push_back(
			    cfgIt != configToProjectReference.end() ? cfgIt->second
			                                            : nullptr);
		}
	}
	return result;
}

// projectreferencefilemapper.go:68 getProjectReferenceFromSource.
tsoptions::SourceOutputAndProjectReference*
projectReferenceFileMapper::getProjectReferenceFromSource(
    const tspath::Path& path) {
	auto it = sourceToProjectReference.find(path);
	return it != sourceToProjectReference.end() ? it->second : nullptr;
}

// projectreferencefilemapper.go:72 getProjectReferenceFromOutputDts.
tsoptions::SourceOutputAndProjectReference*
projectReferenceFileMapper::getProjectReferenceFromOutputDts(
    const tspath::Path& path) {
	auto it = outputDtsToProjectReference.find(path);
	return it != outputDtsToProjectReference.end() ? it->second : nullptr;
}

// projectreferencefilemapper.go:76 isSourceFromProjectReference.
bool projectReferenceFileMapper::isSourceFromProjectReference(
    const tspath::Path& path) {
	return opts->canUseProjectReferenceSource() &&
	       getProjectReferenceFromSource(path) != nullptr;
}

// projectreferencefilemapper.go:80 getCompilerOptionsForFile — the
// redirecting project's compiler options for a referenced file.
const CompilerOptions*
projectReferenceFileMapper::getCompilerOptionsForFile(
    const HasFileName& file) {
	tsoptions::ParsedCommandLine* redirect =
	    getRedirectParsedCommandLineForResolution(file);
	return module::GetCompilerOptionsWithRedirect(
	    opts->Config->CompilerOptions(), redirect);
}

// projectreferencefilemapper.go:85 getRedirectParsedCommandLineForResolution.
tsoptions::ParsedCommandLine*
projectReferenceFileMapper::getRedirectParsedCommandLineForResolution(
    const HasFileName& file) {
	return getRedirectForResolution(file).first;
}

// projectreferencefilemapper.go:90 getRedirectForResolution.
std::pair<tsoptions::ParsedCommandLine*, std::string>
projectReferenceFileMapper::getRedirectForResolution(
    const HasFileName& file) {
	const tspath::Path& path = file.Path();
	// Check if outputdts of source file from project reference.
	if (tsoptions::SourceOutputAndProjectReference* output =
	        getProjectReferenceFromSource(path);
	    output != nullptr) {
		return {output->Resolved, output->Source};
	}
	// Source file from project reference.
	if (tsoptions::SourceOutputAndProjectReference* resultFromDts =
	        getProjectReferenceFromOutputDts(path);
	    resultFromDts != nullptr) {
		return {resultFromDts->Resolved, resultFromDts->Source};
	}
	if (tsoptions::SourceOutputAndProjectReference* realpathDtsToSource_ =
	        getSourceToDtsIfSymlink(file);
	    realpathDtsToSource_ != nullptr) {
		return {realpathDtsToSource_->Resolved,
		        realpathDtsToSource_->Source};
	}
	return {nullptr, file.FileName()};
}

// projectreferencefilemapper.go:111 getResolvedReferenceFor.
std::pair<tsoptions::ParsedCommandLine*, bool>
projectReferenceFileMapper::getResolvedReferenceFor(
    const tspath::Path& path) {
	auto it = configToProjectReference.find(path);
	return {it != configToProjectReference.end() ? it->second : nullptr,
	        it != configToProjectReference.end()};
}

// projectreferencefilemapper.go:116 rangeResolvedProjectReference.
bool projectReferenceFileMapper::rangeResolvedProjectReference(
    const std::function<bool(tspath::Path, tsoptions::ParsedCommandLine*,
                             tsoptions::ParsedCommandLine*, int)>& f) {
	if (opts->Config == nullptr ||
	    opts->Config->ProjectReferences().empty()) {
		return false;
	}
	collections::Set<tspath::Path> seenRef(referencesInConfigFile.size());
	tspath::Path rootConfig = rootConfigPath();
	seenRef.Add(rootConfig);
	return rangeResolvedReferenceWorker(referencesInConfigFile[rootConfig],
	                                    f, opts->Config, &seenRef);
}

// projectreferencefilemapper.go:129 rangeResolvedReferenceWorker.
bool projectReferenceFileMapper::rangeResolvedReferenceWorker(
    const std::vector<tspath::Path>& references,
    const std::function<bool(tspath::Path, tsoptions::ParsedCommandLine*,
                             tsoptions::ParsedCommandLine*, int)>& f,
    tsoptions::ParsedCommandLine* parent,
    collections::Set<tspath::Path>* seenRef) {
	for (size_t index = 0; index < references.size(); index++) {
		const tspath::Path& path = references[index];
		if (!seenRef->AddIfAbsent(path)) {
			continue;
		}
		auto cfgIt = configToProjectReference.find(path);
		tsoptions::ParsedCommandLine* config =
		    cfgIt != configToProjectReference.end() ? cfgIt->second
		                                            : nullptr;
		if (!f(path, config, parent, static_cast<int>(index))) {
			return false;
		}
		auto refIt = referencesInConfigFile.find(path);
		if (!rangeResolvedReferenceWorker(
		        refIt != referencesInConfigFile.end()
		            ? refIt->second
		            : std::vector<tspath::Path>{},
		        f, config, seenRef)) {
			return false;
		}
	}
	return true;
}

// projectreferencefilemapper.go:150
// rangeResolvedProjectReferenceInChildConfig.
bool projectReferenceFileMapper::
    rangeResolvedProjectReferenceInChildConfig(
        tsoptions::ParsedCommandLine* childConfig,
        const std::function<bool(tspath::Path,
                                 tsoptions::ParsedCommandLine*,
                                 tsoptions::ParsedCommandLine*, int)>& f) {
	if (childConfig == nullptr || childConfig->ConfigFile == nullptr ||
	    childConfig->ConfigFile->SourceFile == nullptr) {
		return false;
	}
	collections::Set<tspath::Path> seenRef(referencesInConfigFile.size());
	tspath::Path childPath =
	    childConfig->ConfigFile->SourceFile->Path();
	seenRef.Add(childPath);
	return rangeResolvedReferenceWorker(referencesInConfigFile[childPath],
	                                    f, opts->Config, &seenRef);
}

// projectreferencefilemapper.go:163 getSourceToDtsIfSymlink — with
// preserveSymlinks the resolved real path may be the .d.ts from a
// project reference. Only probed for node_modules paths (to avoid a
// realpath on every file) and only while the loader/host are attached.
tsoptions::SourceOutputAndProjectReference*
projectReferenceFileMapper::getSourceToDtsIfSymlink(
    const HasFileName& file) {
	const tspath::Path& path = file.Path();
	if (auto [cached, ok] = realpathDtsToSource.Load(path); ok) {
		return cached;
	}
	if (loader != nullptr &&
	    tristateIsTrue(opts->Config->CompilerOptions()->PreserveSymlinks)) {
		const std::string& fileName = file.FileName();
		if (fileName.find("/node_modules/") == std::string::npos) {
			realpathDtsToSource.Store(path, nullptr);
		} else {
			tspath::Path realDeclarationPath =
			    loader->toPath(host->Realpath(fileName));
			if (realDeclarationPath == path) {
				realpathDtsToSource.Store(path, nullptr);
			} else {
				tsoptions::SourceOutputAndProjectReference* source =
				    getProjectReferenceFromOutputDts(
				        realDeclarationPath);
				if (source != nullptr) {
					realpathDtsToSource.Store(path, source);
					return source;
				}
				realpathDtsToSource.Store(path, nullptr);
			}
		}
	}
	return nullptr;
}

// projectreferenceparser.go:42 — parses each referenced project's
// config and fills the loader's projectReferenceFileMapper. The C++
// parser is single-threaded: start() queues onto a
// singleThreadedWorkGroup and RunAndWait drains it (the workGroup keeps
// the Go Queue/RunAndWait shape). Defined here — naming tsc::workGroup
// in program.h would hijack `struct workGroup` elaborated specifiers
// in TUs that define a same-named local type (execute/build).
struct projectReferenceParser {
	filesLoader* loader{};
	std::unique_ptr<workGroup> wg{};
	std::unordered_map<tspath::Path, projectReferenceParseTask*>
	    tasksByFileName;
	// Arena owning every task (Go relies on GC).
	std::deque<std::unique_ptr<projectReferenceParseTask>> taskArena;

	void parse(std::vector<projectReferenceParseTask*>& tasks);
	void start(std::vector<projectReferenceParseTask*>& tasks);
	void initMapper(std::vector<projectReferenceParseTask*>& tasks);
	std::vector<tspath::Path> initMapperWorker(
	    const std::vector<projectReferenceParseTask*>& tasks,
	    collections::Set<projectReferenceParseTask*>* seen);
};

// projectreferenceparser.go:19 — resolve the config through the loader's
// host and queue its own project references.
void projectReferenceParseTask::parse(projectReferenceParser* parser) {
	filesLoader* loader = parser->loader;
	// projectreferenceparser.go:21-23 — `tr.Push(PhaseParse,
	// "parseJsonSourceFileConfigFileContent", {"path"}, false)`.
	tracing::TraceScope traceParseConfig(
	    loader->tracing, tracing::PhaseParse,
	    "parseJsonSourceFileConfigFileContent",
	    [&] { return tracing::TraceArgs{{"path", configName}}; }, false);
	resolved = loader->host->GetResolvedProjectReference(
	    configName, loader->toPath(configName));
	if (resolved == nullptr) {
		return;
	}
	resolved->ParseInputOutputNames();
	if (std::vector<std::string> subReferences =
	        resolved->ResolvedProjectReferencePaths();
	    !subReferences.empty()) {
		subTasks = createProjectReferenceParseTasks(subReferences,
		                                          parser->taskArena);
	}
}

// projectreferenceparser.go:34 createProjectReferenceParseTasks.
std::vector<projectReferenceParseTask*> createProjectReferenceParseTasks(
    const std::vector<std::string>& projectReferences,
    std::deque<std::unique_ptr<projectReferenceParseTask>>& arena) {
	std::vector<projectReferenceParseTask*> tasks;
	tasks.reserve(projectReferences.size());
	for (const std::string& configName : projectReferences) {
		auto task =
		    std::make_unique<projectReferenceParseTask>();
		task->configName = configName;
		tasks.push_back(task.get());
		arena.push_back(std::move(task));
	}
	return tasks;
}

// projectreferenceparser.go:48 — attach the loader, run every queued
// task, then fill the mapper.
void projectReferenceParser::parse(
    std::vector<projectReferenceParseTask*>& tasks) {
	loader->projectReferenceFileMapper->loader = loader;
	start(tasks);
	wg->RunAndWait();
	initMapper(tasks);
}

// projectreferenceparser.go:55 — dedup on config path (later duplicates
// point at the already-queued task), queue new tasks on the work group.
void projectReferenceParser::start(
    std::vector<projectReferenceParseTask*>& tasks) {
	for (size_t i = 0; i < tasks.size(); i++) {
		projectReferenceParseTask* task = tasks[i];
		tspath::Path path = loader->toPath(task->configName);
		auto [it, inserted] = tasksByFileName.try_emplace(path, task);
		if (!inserted) {
			// dedup tasks to ensure correct file order, regardless of
			// which task would be started first (projectreferenceparser.go:59).
			tasks[i] = it->second;
		} else {
			wg->Queue([this, task] {
				task->parse(this);
				start(task->subTasks);
			});
		}
	}
}

// projectreferenceparser.go:70 initMapper.
void projectReferenceParser::initMapper(
    std::vector<projectReferenceParseTask*>& tasks) {
	auto* mapper = loader->projectReferenceFileMapper.get();
	size_t totalReferences = tasksByFileName.size() + 1;
	mapper->configToProjectReference.reserve(totalReferences);
	mapper->referencesInConfigFile.reserve(totalReferences);
	collections::Set<projectReferenceParseTask*> seen;
	mapper->referencesInConfigFile[mapper->rootConfigPath()] =
	    initMapperWorker(tasks, &seen);
	if (mapper->opts->canUseProjectReferenceSource() &&
	    !mapper->outputDtsToProjectReference.empty()) {
		mapper->host = newProjectReferenceDtsFakingHost(loader);
	}
}

// projectreferenceparser.go:82 initMapperWorker — fill the four maps and
// the loader's dtsDirectories; returns this config's reference paths in
// order.
std::vector<tspath::Path> projectReferenceParser::initMapperWorker(
    const std::vector<projectReferenceParseTask*>& tasks,
    collections::Set<projectReferenceParseTask*>* seen) {
	if (tasks.empty()) {
		return {};
	}
	auto* mapper = loader->projectReferenceFileMapper.get();
	std::vector<tspath::Path> results;
	results.reserve(tasks.size());
	for (auto* task : tasks) {
		tspath::Path path = loader->toPath(task->configName);
		results.push_back(path);
		// ensure we only walk each task once
		if (!seen->AddIfAbsent(task)) {
			continue;
		}
		mapper->configToProjectReference[path] = task->resolved;
		if (task->resolved != nullptr &&
		    mapper->opts->Config->ConfigFile !=
		        task->resolved->ConfigFile) {
			// Map current task's files first, before recursing into
			// subtasks. This matches TypeScript's behavior where child
			// project references overwrite parent entries when a file
			// belongs to multiple projects
			// (projectreferenceparser.go:96-100).
			for (auto& [k, v] :
			     *task->resolved->SourceToProjectReference()) {
				mapper->sourceToProjectReference[k] = v;
			}
			for (auto& [k, v] :
			     *task->resolved->OutputDtsToProjectReference()) {
				mapper->outputDtsToProjectReference[k] = v;
			}
			if (mapper->opts->canUseProjectReferenceSource()) {
				std::string declDir =
				    task->resolved->CompilerOptions()
				        ->DeclarationDir;
				if (declDir.empty()) {
					declDir = task->resolved->CompilerOptions()->OutDir;
				}
				if (!declDir.empty()) {
					loader->dtsDirectories->Add(
					    loader->toPath(declDir));
				}
			}
		}
		std::vector<tspath::Path> referencesInConfig =
		    initMapperWorker(task->subTasks, seen);
		mapper->referencesInConfigFile[path] = referencesInConfig;
	}
	return results;
}

// fileloader.go:340 addProjectReferenceTasks — construct the mapper
// unconditionally (the produced program always has one) and parse every
// referenced project's config through the host.
void filesLoader::addProjectReferenceTasks(bool singleThreaded) {
	(void)singleThreaded; // the C++ parser is single-threaded
	projectReferenceFileMapper =
	    std::make_shared<compiler::projectReferenceFileMapper>();
	projectReferenceFileMapper->opts = opts;
	projectReferenceFileMapper->host = host;
	std::vector<std::string> projectReferences =
	    opts->Config != nullptr ? opts->Config->ResolvedProjectReferencePaths()
	                            : std::vector<std::string>{};
	if (projectReferences.empty()) {
		return;
	}

	// projectreferenceparser.go:48 — {loader, wg:
	// core.NewWorkGroup(singleThreaded)}.
	projectReferenceParser parser;
	parser.loader = this;
	parser.wg.reset(newWorkGroup(singleThreaded));
	auto rootTasks = createProjectReferenceParseTasks(
	    projectReferences, parser.taskArena);
	parser.parse(rootTasks);
}

// ===========================================================================
// filesParser — filesparser.go
// ===========================================================================

// module.DiagAndArgs args are []any in Go — stringify like fmt's %v for
// the host's string-typed trace callback (filesparser.go:441-444,
// :560-562).
static std::vector<std::string> diagArgsToStrings(
    const std::vector<std::any>& args) {
	std::vector<std::string> out;
	out.reserve(args.size());
	for (auto& a : args) {
		if (auto* s = std::any_cast<std::string>(&a))
			out.push_back(*s);
		else if (auto* s = std::any_cast<const char*>(&a))
			out.push_back(*s);
		else if (auto* s = std::any_cast<std::string_view>(&a))
			out.emplace_back(*s);
		else if (auto* b = std::any_cast<bool>(&a))
			out.push_back(*b ? "true" : "false");
		else if (auto* i = std::any_cast<int>(&a))
			out.push_back(std::to_string(*i));
		else if (auto* i = std::any_cast<int64_t>(&a))
			out.push_back(std::to_string(*i));
		else if (auto* u = std::any_cast<uint64_t>(&a))
			out.push_back(std::to_string(*u));
		else
			out.push_back("");
	}
	return out;
}

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

	// filesparser.go:71 — `defer loader.opts.Tracing.Push(PhaseProgram,
	// "findSourceFile", {"fileName"}, false)()`.
	tracing::TraceScope traceFindSourceFile(
	    loader->tracing, tracing::PhaseProgram, "findSourceFile",
	    [&] { return tracing::TraceArgs{{"fileName", t->normalizedFilePath}}; },
	    false);

	// filesparser.go:73-77 — Try to find the project redirect
	if (std::string redirect =
	        loader->projectReferenceFileMapper->getParseFileRedirect(
	            HasFileName{t->normalizedFilePath, t->path});
	    !redirect.empty()) {
		t->redirect(loader, redirect);
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

	// filesparser.go:109-111 — capacity bookkeeping (atomics in Go).
	loader->totalFileCount++;
	if (t->libFile != nullptr) {
		loader->libFileCount++;
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
	// filesparser.go:128-130 — the virtual (content-mapped) name drives
	// the implied node format, not the canonical one.
	if (std::string virtualFileName = file->VirtualFileName();
	    !virtualFileName.empty()) {
		t->metadata.ImpliedNodeFormat = getImpliedNodeFormatForFile(
		    virtualFileName, t->metadata.PackageJsonType);
	}

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

	// filesparser.go:172-182 — a content-mapped file brings its
	// supplemental files into the program as separate files.
	if (const auto* supplementals = file->SupplementalSourceFiles()) {
		for (auto* supplemental : *supplementals) {
			auto* sub = newTask(supplemental->FileName());
			sub->file = supplemental;
			sub->isContentMapperSupplemental = true;
			sub->includeReason = loader->ip->newReason(
			    FileIncludeKind::ContentMapperSupplemental, t->path);
			t->subTasks.push_back(sub);
		}
	}
}

// filesparser.go:269 filesParser.start (single-threaded DFS)
// filesparser.go:269 filesParser.start — each task is queued on the work
// group; the task body runs under its path's parseTaskData mutex (Go:
// `data.mu`). The taskDataByPath table itself is SyncMap-equivalent via
// taskDataMapMu.
void filesParser::start(const std::vector<parseTask*>& tasks, int depth) {
	for (auto* task : tasks) {
		task->path = loader->toPath(task->normalizedFilePath);
		std::unique_lock<std::mutex> mapLock(taskDataMapMu);
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
		mapLock.unlock();

		wg->Queue([this, task, data, loaded, depth] {
			std::lock_guard<std::mutex> dataLock(data->mu);

			bool startSubtasks = false;
			if (loaded) {
				if (auto it = data->tasks.find(task->normalizedFilePath);
				    it != data->tasks.end()) {
					task->loadedTask = it->second;
				} else {
					data->tasks[task->normalizedFilePath] = task;
					// new task for file name — load subtasks if there
					// was loading for any other casing
					startSubtasks = data->startedSubTasks;
				}
			}

			// Propagate PackageId to data if we have one and data
			// doesn't yet
			if (data->PackageId.Name.empty() &&
			    !task->PackageId.Name.empty()) {
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
				return;
			}

			for (auto& [name, taskByFileName] : data->tasks) {
				bool loadSubTasks = startSubtasks;
				if (!taskByFileName->loaded) {
					load(taskByFileName);
					if (taskByFileName->redirectedParseTask !=
					    nullptr) {
						loadSubTasks = true;
						data->startedSubTasks = true;
					}
				}
				if (!taskByFileName->startedSubTasksTask &&
				    loadSubTasks) {
					taskByFileName->startedSubTasksTask = true;
					start(taskByFileName->subTasks,
					      data->lowestDepth);
				}
			}
		});
	}
}

filesParser::~filesParser() = default;

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
	// filesparser.go:371-378 — per-parseTaskData casing sets so a
	// deduplicated file is recorded once per casing (double-recording
	// would double-release the parse-cache entry on snapshot disposal).
	std::unordered_map<parseTaskData*, collections::Set<std::string>>
	    recordedDuplicates;
	auto recordDuplicate = [&](SourceFile* f) {
		// filesparser.go:408-415 / :454-461 — the parsed-but-deduplicated
		// file for the snapshot's parse-cache release bookkeeping.
		// SourceFile::Hash is tsc::Uint128 (ast.h); DuplicateSourceFile
		// stores xxh3::Uint128 — same 128-bit value, different field
		// names.
		duplicateSourceFiles.push_back(
		    std::make_shared<DuplicateSourceFile>(DuplicateSourceFile{
		        f->ParseOptions(), f->ContentMapperParseOptions(),
		        xxh3::Uint128{f->Hash.hi, f->Hash.lo}, f->ScriptKind,
		        f->ContentMapper(), f->IsContentMapperFailureStub()}));
	};

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
				// filesparser.go:398-417 — a second casing of a file that
				// already parsed is a duplicate the snapshot must release.
				if (task->file != nullptr &&
				    checkedName != task->normalizedFilePath) {
					auto& dups = recordedDuplicates[data];
					dups.Add(checkedName);
					if (dups.AddIfAbsent(task->normalizedFilePath)) {
						recordDuplicate(task->file);
					}
				}
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

			// filesparser.go:440-445 — replay the resolution traces
			// recorded during this task's parse.
			for (auto& trace : task->typeResolutionsTrace) {
				loader->host->Trace(
				    trace.Message,
				    diagArgsToStrings(trace.Args));
			}
			for (auto& trace : task->resolutionsTrace) {
				loader->host->Trace(
				    trace.Message,
				    diagArgsToStrings(trace.Args));
			}

			SourceFile* file = task->file;
			if (dedupePackages && !data->PackageId.Name.empty()) {
				if (auto pkgIt = packageIdToSourceFile.find(
				        data->PackageId);
				    pkgIt != packageIdToSourceFile.end()) {
					SourceFile* packageIdFile = pkgIt->second;
					// Package deduplication keeps the first package
					// instance in the program, but the file was parsed
					// and acquired — snapshot disposal must release the
					// extra owner (filesparser.go:450-461).
					if (file != nullptr) {
						recordDuplicate(file);
					}
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
				// filesparser.go:490-495 — when the program can't use
				// project-reference sources, remember which source a
				// redirected dts parse produced so emit can attribute it.
				if (!loader->opts->canUseProjectReferenceSource()) {
					outputFileToProjectReferenceSource
					    [task->redirectedParseTask->path] =
					        task->normalizedFilePath;
				}
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

// fileloader.go:152 processAllProgramFiles — project-reference tasks +
// resolver + root tasks + lib set + ATD task, then the parser run and
// collection.
void filesLoader::processAllProgramFiles(
    const std::vector<std::string>& rootFileNames, bool singleThreaded) {
	// fileloader.go:180 — must run before the resolver is created so
	// ResolverOptions.Host can be the dts-faking host installed by
	// projectReferenceParser::initMapper.
	addProjectReferenceTasks(singleThreaded);

	// fileloader.go:181-190 — resolver construction (moved out of the
	// SimpleProgram ctor to match Go's ordering).
	module::ResolverOptions resolverOptions{
	    .Host = projectReferenceFileMapper->host,
	    .CompilerOptions = compilerOptions,
	    .TypingsLocation = opts->TypingsLocation,
	    .ProjectName = opts->ProjectName,
	    .ExtraExtensions = opts->Config != nullptr
	                           ? opts->Config->ContentMapperExtensions()
	                           : std::vector<std::string>{},
	    .PackageJsonCache = {},
	};
	if (opts->CreateModuleResolver != nullptr) {
		resolverOwned.reset(opts->CreateModuleResolver(resolverOptions));
	} else {
		resolverOwned.reset(module::NewResolver(resolverOptions));
	}
	resolver = resolverOwned.get();

	// fileloader.go:194 — `defer opts.Tracing.Push(PhaseProgram,
	// "processRootFiles", {"count"}, false)()`.
	tracing::TraceScope traceProcessRootFiles(
	    tracing, tracing::PhaseProgram, "processRootFiles",
	    [&] { return tracing::TraceArgs{{"count", (int)rootFileNames.size()}}; },
	    false);
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

	parser->singleThreaded = singleThreaded;
	parser->getProcessedFiles(rootTasks);
}

// filesparser.go:330 getProcessedFiles — parse, DFS collect, then the
// lib-sort / allFiles / redirect-index / libResolution tail.
void filesParser::getProcessedFiles(
    const std::vector<parseTask*>& tasks) {
	// filesparser.go:327-328 — parse() queues every task on the work
	// group and blocks until it drains (parallel unless singleThreaded).
	wg.reset(tsc::newWorkGroup(singleThreaded));
	start(tasks, 0);
	wg->RunAndWait();

	// fileloader.go:223-224 — the mapper's loader and host links are
	// severed once parsing ends so reuse can detect a stale loader and a
	// host that may have been swapped for the faking host.
	loader->projectReferenceFileMapper->loader = nullptr;
	loader->projectReferenceFileMapper->host = nullptr;

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
		// filesparser.go:560-562 — replay the lib resolution's trace.
		for (auto& trace : value.trace) {
			loader->host->Trace(trace.Message,
			                    diagArgsToStrings(trace.Args));
		}
	}
}

}  // namespace tsc::compiler
