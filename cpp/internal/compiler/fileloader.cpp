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
static const DiagnosticMessage* contentMapperProjectErrorDiagnostic(
    const gostd::Error& err) {
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
			    file, label, contentMapperProjectErrorDiagnostic(err));
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
static Diagnostic* contentMapperProjectDiagnostic(const gostd::Error& err) {
	if (gostd::errorAs<contentmapper::InitializeError*>(err) != nullptr) {
		return contentMapperInitializationDiagnostic("" /*label*/, err);
	}
	return tsoptions::newCompilerDiagnostic(
	    contentMapperProjectErrorDiagnostic(err));
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
			std::string moduleName = entry->text();
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
