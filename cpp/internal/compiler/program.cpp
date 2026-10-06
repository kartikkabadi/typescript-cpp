// --- program.go + outputpaths/commonsourcedirectory.go + emitter.go
// (sourceFileMayBeEmitted) — program slice ---
//
// Port notes:
//  - Go runs file parsing/binding/checking through workgroups; this port is
//    single-threaded and produces identical observable state.
//  - Project references, content mappers, incremental (.tsbuildinfo), and
//    emit are out of scope — Go-equivalent no-ops or TSC_UNREACHABLE where a
//    branch can't be reached from `tsc --noEmit <files>`.
//  - The checker's per-file check walker isn't ported yet, so checker
//    diagnostics are empty (faithful for this slice's output).

#include <cstdio>
#include "internal/compiler/program.h"
#include "internal/binder/binder.h"
#include "internal/compiler/checkerpool.h"
#include "internal/compiler/emitter.h"
#include "internal/module/util.h" // === slice: ls-autoimport ===
#include "internal/modulespecifiers/types.h" // === slice: ls-autoimport ===
#include "internal/diagnostics/messages_generated.h"
#include "internal/json/json.h"
#include "internal/outputpaths/outputpaths.h"
#include "internal/scanner/scanner.h"
#include "internal/core/utilities.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>

namespace tsc::compiler {

namespace {

// ===========================================================================
// core stringers — modulekind_stringer_generated.go /
// scripttarget_stringer_generated.go / compileroptions.go JsxEmit.String
// ===========================================================================

std::string moduleKindString(ModuleKind i) {
	static const char* name0 =
	    "NoneCommonJSAMDUMDSystemES2015ES2020ES2022";
	static const uint8_t index0[] = {0, 4, 12, 15, 18, 24, 30, 36, 42};
	static const char* name1 = "ESNextNode16Node18Node20";
	static const uint8_t index1[] = {0, 6, 12, 18, 24};
	static const char* name2 = "NodeNextPreserve";
	static const uint8_t index2[] = {0, 8, 16};
	int v = static_cast<int>(i);
	if (0 <= v && v <= 7)
		return std::string(name0 + index0[v],
		                   static_cast<size_t>(index0[v + 1] - index0[v]));
	if (99 <= v && v <= 102) {
		v -= 99;
		return std::string(name1 + index1[v],
		                   static_cast<size_t>(index1[v + 1] - index1[v]));
	}
	if (199 <= v && v <= 200) {
		v -= 199;
		return std::string(name2 + index2[v],
		                   static_cast<size_t>(index2[v + 1] - index2[v]));
	}
	return "ModuleKind(" + std::to_string(v) + ")";
}

std::string scriptTargetStringForOptions(ScriptTarget i) {
	static const char* name0 =
	    "NoneES5ES2015ES2016ES2017ES2018ES2019ES2020ES2021ES2022ES2023ES2024"
	    "ES2025ES2026";
	static const uint8_t index0[] = {0, 4, 7, 13, 19, 25, 31, 37, 43,
	                                 49, 55, 61, 67, 73, 79};
	static const char* name1 = "ESNextJSON";
	static const uint8_t index1[] = {0, 6, 10};
	int v = static_cast<int>(i);
	if (0 <= v && v <= 13)
		return std::string(name0 + index0[v],
		                   static_cast<size_t>(index0[v + 1] - index0[v]));
	if (99 <= v && v <= 100) {
		v -= 99;
		return std::string(name1 + index1[v],
		                   static_cast<size_t>(index1[v + 1] - index1[v]));
	}
	return "ScriptTarget(" + std::to_string(v) + ")";
}

std::string moduleResolutionKindString(ModuleResolutionKind m) {
	switch (m) {
		case ModuleResolutionKind::Unknown:
			TSC_UNREACHABLE(
			    "ModuleResolutionKind.Unknown — ported with the program slice");
		case ModuleResolutionKind::Classic: return "Classic";
		case ModuleResolutionKind::Node10: return "Node10";
		case ModuleResolutionKind::Node16: return "Node16";
		case ModuleResolutionKind::NodeNext: return "NodeNext";
		case ModuleResolutionKind::Bundler: return "Bundler";
	}
	TSC_UNREACHABLE(
	    "unhandled case in moduleResolutionKindString — ported with the "
	    "program slice");
}

std::string jsxEmitString(JsxEmit j) {
	switch (j) {
		case JsxEmit::None:
			TSC_UNREACHABLE(
			    "JsxEmit.None — ported with the program slice");
		case JsxEmit::Preserve: return "preserve";
		case JsxEmit::ReactNative: return "react-native";
		case JsxEmit::React: return "react";
		case JsxEmit::ReactJSX: return "react-jsx";
		case JsxEmit::ReactJSXDev: return "react-jsxdev";
	}
	TSC_UNREACHABLE(
	    "unhandled case in jsxEmitString — ported with the program slice");
}

// program.go: hasZeroOrOneAsteriskCharacter
bool hasZeroOrOneAsteriskCharacter(std::string_view str) {
	bool seenAsterisk = false;
	for (char ch : str) {
		if (ch == '*') {
			if (!seenAsterisk) {
				seenAsterisk = true;
			} else {
				return false;
			}
		}
	}
	return true;
}

// program.go
bool moduleResolutionSupportsPackageJsonExportsAndImports(
    ModuleResolutionKind moduleResolution) {
	return (moduleResolution >= ModuleResolutionKind::Node16 &&
	        moduleResolution <= ModuleResolutionKind::NodeNext) ||
	       moduleResolution == ModuleResolutionKind::Bundler;
}

bool emitModuleKindIsNonNodeESM(ModuleKind moduleKind) {
	return moduleKind >= ModuleKind::ES2015 &&
	       moduleKind <= ModuleKind::ESNext;
}

// program.go: isCommentOrBlankLine
bool isCommentOrBlankLine(std::string_view text, size_t pos) {
	while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
		pos++;
	}
	return pos == text.size() ||
	       (pos < text.size() &&
	        (text[pos] == '\r' || text[pos] == '\n')) ||
	       (pos + 1 < text.size() && text[pos] == '/' &&
	        text[pos + 1] == '/');
}

// Arena for diagnostic clones produced by compactAndMergeRelatedInfos —
// diagnostics are never freed within a run, mirroring Go's GC.
Diagnostic* cloneDiagnostic(Diagnostic* d) {
	static std::deque<Diagnostic> arena;
	return &arena.emplace_back(*d);
}

// program.go:2373 plainJSErrors — diagnostics still reported for plain JS
// (binder grammar errors + a few others; JS type errors are filtered out).
bool isPlainJSError(int32_t code) {
	static const std::unordered_set<int32_t> s = {
	    Cannot_redeclare_block_scoped_variable_0->code,
	    A_module_cannot_have_multiple_default_exports->code,
	    Another_export_default_is_here->code,
	    The_first_export_default_is_here->code,
	    Identifier_expected_0_is_a_reserved_word_at_the_top_level_of_a_module
	        ->code,
	    Identifier_expected_0_is_a_reserved_word_in_strict_mode_Modules_are_automatically_in_strict_mode
	        ->code,
	    Identifier_expected_0_is_a_reserved_word_that_cannot_be_used_here
	        ->code,
	    X_constructor_is_a_reserved_word->code,
	    X_delete_cannot_be_called_on_an_identifier_in_strict_mode->code,
	    Code_contained_in_a_class_is_evaluated_in_JavaScript_s_strict_mode_which_does_not_allow_this_use_of_0_For_more_information_see_https_Colon_Slash_Slashdeveloper_mozilla_org_Slashen_US_Slashdocs_SlashWeb_SlashJavaScript_SlashReference_SlashStrict_mode
	        ->code,
	    Invalid_use_of_0_Modules_are_automatically_in_strict_mode->code,
	    Invalid_use_of_0_in_strict_mode->code,
	    A_label_is_not_allowed_here->code,
	    X_with_statements_are_not_allowed_in_strict_mode->code,
	    A_break_statement_can_only_be_used_within_an_enclosing_iteration_or_switch_statement
	        ->code,
	    A_break_statement_can_only_jump_to_a_label_of_an_enclosing_statement
	        ->code,
	    A_class_declaration_without_the_default_modifier_must_have_a_name
	        ->code,
	    A_class_member_cannot_have_the_0_keyword->code,
	    A_comma_expression_is_not_allowed_in_a_computed_property_name->code,
	    A_continue_statement_can_only_be_used_within_an_enclosing_iteration_statement
	        ->code,
	    A_continue_statement_can_only_jump_to_a_label_of_an_enclosing_iteration_statement
	        ->code,
	    A_default_clause_cannot_appear_more_than_once_in_a_switch_statement
	        ->code,
	    A_default_export_must_be_at_the_top_level_of_a_file_or_module_declaration
	        ->code,
	    A_definite_assignment_assertion_is_not_permitted_in_this_context->code,
	    A_destructuring_declaration_must_have_an_initializer->code,
	    A_get_accessor_cannot_have_parameters->code,
	    A_rest_element_cannot_contain_a_binding_pattern->code,
	    A_rest_element_cannot_have_a_property_name->code,
	    A_rest_element_cannot_have_an_initializer->code,
	    A_rest_element_must_be_last_in_a_destructuring_pattern->code,
	    A_rest_parameter_cannot_have_an_initializer->code,
	    A_rest_parameter_must_be_last_in_a_parameter_list->code,
	    A_rest_parameter_or_binding_pattern_may_not_have_a_trailing_comma
	        ->code,
	    A_return_statement_cannot_be_used_inside_a_class_static_block->code,
	    A_set_accessor_cannot_have_rest_parameter->code,
	    A_set_accessor_must_have_exactly_one_parameter->code,
	    An_export_declaration_can_only_be_used_at_the_top_level_of_a_module
	        ->code,
	    An_export_declaration_cannot_have_modifiers->code,
	    An_import_declaration_can_only_be_used_at_the_top_level_of_a_module
	        ->code,
	    An_import_declaration_cannot_have_modifiers->code,
	    An_object_member_cannot_be_declared_optional->code,
	    Argument_of_dynamic_import_cannot_be_spread_element->code,
	    Cannot_assign_to_private_method_0_Private_methods_are_not_writable
	        ->code,
	    Cannot_redeclare_identifier_0_in_catch_clause->code,
	    Catch_clause_variable_cannot_have_an_initializer->code,
	    Class_decorators_can_t_be_used_with_static_private_identifier_Consider_removing_the_experimental_decorator
	        ->code,
	    Classes_can_only_extend_a_single_class->code,
	    Classes_may_not_have_a_field_named_constructor->code,
	    Did_you_mean_to_use_a_Colon_An_can_only_follow_a_property_name_when_the_containing_object_literal_is_part_of_a_destructuring_pattern
	        ->code,
	    Duplicate_label_0->code,
	    Dynamic_imports_can_only_accept_a_module_specifier_and_an_optional_set_of_attributes_as_arguments
	        ->code,
	    X_for_await_loops_cannot_be_used_inside_a_class_static_block->code,
	    JSX_attributes_must_only_be_assigned_a_non_empty_expression->code,
	    JSX_elements_cannot_have_multiple_attributes_with_the_same_name->code,
	    JSX_expressions_may_not_use_the_comma_operator_Did_you_mean_to_write_an_array
	        ->code,
	    JSX_property_access_expressions_cannot_include_JSX_namespace_names
	        ->code,
	    Jump_target_cannot_cross_function_boundary->code,
	    Line_terminator_not_permitted_before_arrow->code,
	    Modifiers_cannot_appear_here->code,
	    Only_a_single_variable_declaration_is_allowed_in_a_for_in_statement
	        ->code,
	    Only_a_single_variable_declaration_is_allowed_in_a_for_of_statement
	        ->code,
	    Private_identifiers_are_not_allowed_outside_class_bodies->code,
	    Private_identifiers_are_only_allowed_in_class_bodies_and_may_only_be_used_as_part_of_a_class_member_declaration_property_access_or_on_the_left_hand_side_of_an_in_expression
	        ->code,
	    Property_0_is_not_accessible_outside_class_1_because_it_has_a_private_identifier
	        ->code,
	    Tagged_template_expressions_are_not_permitted_in_an_optional_chain
	        ->code,
	    The_left_hand_side_of_a_for_of_statement_may_not_be_async->code,
	    The_variable_declaration_of_a_for_in_statement_cannot_have_an_initializer
	        ->code,
	    The_variable_declaration_of_a_for_of_statement_cannot_have_an_initializer
	        ->code,
	    Trailing_comma_not_allowed->code,
	    Variable_declaration_list_cannot_be_empty->code,
	    X_0_and_1_operations_cannot_be_mixed_without_parentheses->code,
	    X_0_expected->code,
	    X_0_is_not_a_valid_meta_property_for_keyword_1_Did_you_mean_2->code,
	    X_0_list_cannot_be_empty->code,
	    X_0_modifier_already_seen->code,
	    X_0_modifier_cannot_appear_on_a_constructor_declaration->code,
	    X_0_modifier_cannot_appear_on_a_module_or_namespace_element->code,
	    X_0_modifier_cannot_appear_on_a_parameter->code,
	    X_0_modifier_cannot_appear_on_class_elements_of_this_kind->code,
	    X_0_modifier_cannot_be_used_here->code,
	    X_0_modifier_must_precede_1_modifier->code,
	    X_0_declarations_can_only_be_declared_inside_a_block->code,
	    X_0_declarations_must_be_initialized->code,
	    X_extends_clause_already_seen->code,
	    X_let_is_not_allowed_to_be_used_as_a_name_in_let_or_const_declarations
	        ->code,
	    Class_constructor_may_not_be_a_generator->code,
	    Class_constructor_may_not_be_an_accessor->code,
	    X_await_expressions_are_only_allowed_within_async_functions_and_at_the_top_levels_of_modules
	        ->code,
	    X_await_using_statements_are_only_allowed_within_async_functions_and_at_the_top_levels_of_modules
	        ->code,
	    Private_field_0_must_be_declared_in_an_enclosing_class->code,
	    // Type errors
	    This_condition_will_always_return_0_since_JavaScript_compares_objects_by_reference_not_value
	        ->code,
	};
	return s.count(code) != 0;
}

}  // namespace

// ===========================================================================
// SimpleProgram — program.go
// ===========================================================================

// program.go:285 NewProgram — processAllProgramFiles + checker pool (lazy
// single checker here) + verifyCompilerOptions.
SimpleProgram::SimpleProgram(CompilerHost* host_,
                             const CompilerOptions& opts,
                             std::vector<std::string> rootFileNames,
                             bool skipModuleResolution_)
	: SimpleProgram(host_, opts, std::move(rootFileNames),
	                nullptr /*config*/, skipModuleResolution_) {}

SimpleProgram::SimpleProgram(CompilerHost* host_,
                             tsoptions::ParsedCommandLine* config,
                             bool skipModuleResolution_)
	: SimpleProgram(host_, *config->CompilerOptions(), config->FileNames(),
	                config, skipModuleResolution_) {}

SimpleProgram::SimpleProgram(CompilerHost* host_,
                             const CompilerOptions& opts,
                             std::vector<std::string> rootFileNames,
                             tsoptions::ParsedCommandLine* config,
                             bool skipModuleResolution_,
                             tracing::Tracing* tracing_)
	: options(opts), skipModuleResolution(skipModuleResolution_),
	  tr_(tracing_) {
	host = host_;
	host->compilerOptions = &options;

	// === slice: incremental ===
	// opts.Config — the program's ParsedCommandLine (program.go NewProgram
	// receives the caller's). Borrowed when constructed from a config;
	// otherwise synthesize a bare one from options+fileNames.
	commandLine_ = config;
	if (commandLine_ == nullptr) {
		commandLineOwned_.reset(tsoptions::NewParsedCommandLine(
		    &options, rootFileNames, {}, comparePathsOptions()));
		commandLine_ = commandLineOwned_.get();
	}
	// === end slice: incremental ===

	// program.go:285 NewProgram — seed the ProgramOptions the file
	// loader, mapper and reuse machinery read (loader.opts). The
	// options/factories fields this ctor has no caller wiring for stay
	// zero-valued.
	opts_.Host = host;
	opts_.Config = commandLine_;
	opts_.Tracing = tr_;

	// fileLoader{} setup (processAllProgramFiles body, fileloader.go:152)
	filesLoader loader;
	filesParser parser;
	includeProcessor_.processingDiagArena->clear();
	loader.opts = &opts_;
	loader.host = host;
	loader.compilerOptions = &options;
	loader.parser = &parser;
	loader.program = this;
	loader.ip = &includeProcessor_;
	loader.skipModuleResolution = skipModuleResolution;
	loader.tracing = tr_;
	loader.defaultLibraryPath = tspath::getNormalizedAbsolutePath(
	    host->DefaultLibraryPath(), host->GetCurrentDirectory());
	// fileloader.go:168 — the host FS decides case sensitivity (a VFS may be
	// case-insensitive; hard-coding true desyncs filesByPath keys from
	// SimpleProgram::toPath lookups).
	loader.useCaseSensitiveFileNames = host->FS()->UseCaseSensitiveFileNames();
	// fileloader.go:178 — extensions the configured content mappers claim.
	loader.contentMapperExtensions = commandLine_->ContentMapperExtensions();
	loader.supportedExtensions =
	    tsoptions::getSupportedExtensions(&options, {});
	loader.supportedExtensionsWithJsonIfResolveJsonModule =
	    tsoptions::getSupportedExtensionsWithJsonIfResolveJsonModule(
	        &options, loader.supportedExtensions);
	int maxNodeModuleJsDepth = 0;
	if (options.MaxNodeModuleJsDepth != nullptr) {
		maxNodeModuleJsDepth = *options.MaxNodeModuleJsDepth;
	}
	parser.loader = &loader;
	parser.maxDepth = maxNodeModuleJsDepth;

	// fileloader.go:152 — the loader builds the project-reference
	// mapper and the resolver inside processAllProgramFiles (the
	// resolver's Host may be the mapper's dts-faking host).
	loader.processAllProgramFiles(rootFileNames, SingleThreaded());

	// Move collected state into the program (processedFiles — the
	// filesparser.go:565 literal).
	finishedProcessing = true;
	resolver_ = std::move(loader.resolverOwned);
	projectReferenceFileMapper_ =
	    std::move(loader.projectReferenceFileMapper);
	duplicateSourceFiles = std::move(parser.duplicateSourceFiles);
	outputFileToProjectReferenceSource =
	    std::move(parser.outputFileToProjectReferenceSource);
	files = std::move(parser.files);
	filesByPath = std::move(parser.filesByPath);
	resolvedModules = std::move(parser.resolvedModules);
	typeResolutionsInFile = std::move(parser.typeResolutionsInFile);
	sourceFileMetaDatas = std::move(parser.sourceFileMetaDatas);
	jsxRuntimeImportSpecifiers =
	    std::move(parser.jsxRuntimeImportSpecifiers);
	importHelpersImportSpecifiers =
	    std::move(parser.importHelpersImportSpecifiers);
	// Keep the synthetic import specifier nodes (created in
	// filesParser::factory) alive for the program's lifetime.
	syntheticImportArena =
	    std::make_shared<Arena>(std::move(parser.factory.arena()));
	sourceFilesFoundSearchingNodeModules =
	    std::move(parser.sourceFilesFoundSearchingNodeModules);
	libFiles = std::move(parser.libFiles);
	// filesparser.go:565 — Go's GC keeps the LibFile objects (owned by the
	// loader's pathForLibFileCache) alive for the program's lifetime; move
	// the owning cache so libFiles' values don't dangle.
	pathForLibFileCache = std::move(loader.pathForLibFileCache);
	missingFiles = std::move(parser.missingFiles);
	redirectTargetsMap = std::move(parser.redirectTargetsMap);
	redirectFilesByPath = std::move(parser.redirectFilesByPath);
	fileNameList = std::move(rootFileNames);
	// filesparser.go:584 — loader's content-mapper failure diagnostics.
	contentMapperDiagnostics = std::move(loader.contentMapperDiagnostics);
	// filesparser.go:585 — moduleResolutionError (empty: the C++
	// resolver interface has no error channel).
	moduleResolutionError_ = loader.moduleResolutionError;

	// program.go:447 — NewProgram installs the checker pool before
	// verifyCompilerOptions (the Go initCheckerPool call site).
	initCheckerPool();
	verifyCompilerOptions();
	collectContentMapperOptionDiagnostics();
}

// program.go:570 BindSourceFiles — per-file bind tasks on a WorkGroup;
// ast::OnceFlag makes each file bind exactly once across callers.
void SimpleProgram::BindSourceFiles() {
	std::unique_ptr<workGroup> wg(newWorkGroup(SingleThreaded()));
	for (auto* file : files) {
		if (!file->isBound.load(std::memory_order_relaxed)) {
			wg->Queue([this, file] {
				// program.go:578 — `defer tr.Push(..., "bindSourceFile", ...)`.
				tracing::TraceScope tracePop(
				    tr_, tracing::PhaseBind, "bindSourceFile",
				    tracing::TraceArgs{{"path", std::string(file->Path())}},
				    true);
				bindSourceFile(file);
			});
		}
	}
	wg->RunAndWait();
}

// getChecker — the built-in pool's non-exclusive first checker (Go's
// getCheckerNonExclusive; equivalent to the old lazy single checker).
// Under an external (project) pool the legacy lazy checker_ is kept for
// callers that cannot carry the pool's release func.
checker::Checker* SimpleProgram::getChecker() {
	if (compilerCheckerPool_ != nullptr) {
		return compilerCheckerPool_->getCheckerNonExclusive().first;
	}
	if (!checker_) {
		checker_ = std::make_unique<checker::Checker>();
		// checkerpool.go createCheckers / checker.go:916 — Go passes the
		// tracer into NewChecker so it is set BEFORE the intrinsic types are
		// created; otherwise types 1..N would never be RecordType'd.
		if (tr_ != nullptr) {
			checker_->tracer = checker::newTracer(tr_, 0);
		}
		checker_->init(this);
	}
	return checker_.get();
}

// program.go:618 GetResolvedModule — lookup only (resolutions were recorded
// by the file loader).
module::ResolvedModule* SimpleProgram::getResolvedModuleByPath(
    const tspath::Path& path, const std::string& moduleReference,
    ResolutionMode mode) {
	auto it = resolvedModules.find(path);
	if (it != resolvedModules.end()) {
		auto it2 = it->second.find(
		    module::ModeAwareCacheKey{moduleReference, mode});
		if (it2 != it->second.end()) {
			return it2->second;
		}
	}
	return nullptr;
}

// checker.Program::GetResolvedModule — adapts module::ResolvedModule to the
// checker's minimal ResolvedModule value type.
std::optional<checker::ResolvedModule> SimpleProgram::GetResolvedModule(
    SourceFile* file, const std::string& moduleReference,
    ResolutionMode mode) {
	module::ResolvedModule* rm =
	    getResolvedModuleByPath(file->Path(), moduleReference, mode);
	if (rm == nullptr) {
		return std::nullopt;
	}
	checker::ResolvedModule out;
	out.resolved = rm->IsResolved();
	out.resolvedFileName = rm->ResolvedFileName;
	out.resolvedUsingTsExtension = rm->ResolvedUsingTsExtension;
	out.isExternalLibraryImport = rm->IsExternalLibraryImport;
	out.extension = rm->Extension;
	out.alternateResult = rm->AlternateResult;
	out.packageId = checker::PackageId{rm->PackageId.Name};
	out.resolvedUsingExtraExtensions = rm->ResolvedUsingExtraExtensions;
	return out;
}

// === slice: modulespecifiers ===
// program.go GetNearestAncestorDirectoryWithPackageJson.
std::string SimpleProgram::GetNearestAncestorDirectoryWithPackageJson(
    const std::string& dirname) {
	auto scoped = resolver_->GetPackageScopeForPath(dirname);
	if (scoped && scoped->Exists()) {
		return scoped->PackageDirectory;
	}
	return "";
}

// program.go GetPackageJsonInfo.
std::shared_ptr<packagejson::InfoCacheEntry>
SimpleProgram::GetPackageJsonInfo(const std::string& pkgJsonPath) {
	auto directory = tspath::getDirectoryPath(pkgJsonPath);
	auto scoped = resolver_->GetPackageScopeForPath(directory);
	if (scoped && scoped->Exists() && scoped->PackageDirectory == directory) {
		return scoped;
	}
	return nullptr;
}
// === end slice: modulespecifiers ===

module::ResolvedModule* SimpleProgram::GetResolvedModuleFromModuleSpecifier(
    SourceFile* file, Node* moduleSpecifier) {
	if (!isStringLiteralLike(moduleSpecifier)) {
		TSC_UNREACHABLE(
		    "moduleSpecifier must be a StringLiteralLike — program slice");
	}
	ResolutionMode mode = GetModeForUsageLocation(file, moduleSpecifier);
	return getResolvedModuleByPath(file->Path(),
	                               std::string(moduleSpecifier->text()),
	                               mode);
}

module::ResolvedTypeReferenceDirective*
SimpleProgram::GetResolvedTypeReferenceDirective(
    SourceFile* file, const std::string& typeDirectiveName,
    ResolutionMode mode) {
	auto it = typeResolutionsInFile.find(file->Path());
	if (it != typeResolutionsInFile.end()) {
		auto it2 = it->second.find(
		    module::ModeAwareCacheKey{typeDirectiveName, mode});
		if (it2 != it->second.end()) {
			return it2->second;
		}
	}
	return nullptr;
}

// program.go:667 collectDiagnostics — collects diagnostics from a single file
// or all files. If sourceFile is non-nil, returns diagnostics for just that
// file. If sourceFile is nil, returns diagnostics for all files in the program.
std::vector<Diagnostic*> SimpleProgram::collectDiagnostics(
    SourceFile* sourceFile, bool concurrent,
    const std::function<std::vector<Diagnostic*>(SourceFile*)>& collect) {
	std::vector<Diagnostic*> result;
	if (sourceFile != nullptr) {
		result = collect(sourceFile);
	} else {
		for (auto& diags :
		     collectDiagnosticsFromFiles(files, concurrent, collect)) {
			result.insert(result.end(), diags.begin(), diags.end());
		}
	}
	return filterAndSortDiagnostics(result);
}

// program.go:678 collectDiagnosticsFromFiles — per-file collect on a
// WorkGroup; diagnostics are written into per-file slots so the concatenated
// order matches program file order.
std::vector<std::vector<Diagnostic*>>
SimpleProgram::collectDiagnosticsFromFiles(
    const std::vector<SourceFile*>& sourceFiles, bool concurrent,
    const std::function<std::vector<Diagnostic*>(SourceFile*)>& collect) {
	std::vector<std::vector<Diagnostic*>> diagnostics(sourceFiles.size());
	std::unique_ptr<workGroup> wg(
	    newWorkGroup(!concurrent || SingleThreaded()));
	for (int i = 0; i < static_cast<int>(sourceFiles.size()); i++) {
		wg->Queue([i, &diagnostics, &sourceFiles, &collect] {
			diagnostics[i] = collect(sourceFiles[i]);
		});
	}
	wg->RunAndWait();
	return diagnostics;
}

// program.go:695 collectCheckerDiagnostics — collects diagnostics from a
// single file or all files using a callback that receives the checker for
// each file. When the checker pool supports grouped iteration (compiler
// pool), files are grouped by checker and processed in parallel with one
// task per checker, reducing contention and improving cache locality.
// Otherwise, falls back to per-file concurrent collection.
std::vector<Diagnostic*> SimpleProgram::collectCheckerDiagnostics(
    SourceFile* sourceFile,
    const std::function<std::vector<Diagnostic*>(checker::Checker*,
                                               SourceFile*)>& collect) {
	if (sourceFile != nullptr) {
		if (SkipTypeChecking(sourceFile, false)) {
			return {};
		}
		auto [c, done] = GetTypeCheckerForFileExclusive(sourceFile);
		auto result = collect(c, sourceFile);
		done();
		return filterAndSortDiagnostics(std::move(result));
	}
	std::vector<Diagnostic*> result;
	for (auto& diags :
	     collectCheckerDiagnosticsFromFiles(files, collect)) {
		result.insert(result.end(), diags.begin(), diags.end());
	}
	return filterAndSortDiagnostics(std::move(result));
}

// program.go:727 collectCheckerDiagnosticsFromFiles — grouped-by-checker
// iteration on the built-in pool, per-file exclusive checkout otherwise.
std::vector<std::vector<Diagnostic*>>
SimpleProgram::collectCheckerDiagnosticsFromFiles(
    const std::vector<SourceFile*>& sourceFiles,
    const std::function<std::vector<Diagnostic*>(checker::Checker*,
                                               SourceFile*)>& collect) {
	std::vector<std::vector<Diagnostic*>> diagnostics(sourceFiles.size());
	if (compilerCheckerPool_ != nullptr) {
		compilerCheckerPool_->forEachCheckerGroupDo(
		    gostd::Context{}, sourceFiles, SingleThreaded(),
		    [&](checker::Checker* c, int fileIndex, SourceFile* file) {
			    diagnostics[fileIndex] = collect(c, file);
		    });
	} else {
		std::unique_ptr<workGroup> wg(newWorkGroup(SingleThreaded()));
		for (int i = 0; i < static_cast<int>(sourceFiles.size()); i++) {
			if (SkipTypeChecking(sourceFiles[i], false)) {
				continue;
			}
			wg->Queue([this, i, &diagnostics, &sourceFiles, &collect] {
				auto [c, done] = checkerPool_->GetChecker(
				    gostd::Context{}, sourceFiles[i]);
				diagnostics[i] = collect(c, sourceFiles[i]);
				done();
			});
		}
		wg->RunAndWait();
	}
	return diagnostics;
}

// program.go:743 GetSyntacticDiagnostics
std::vector<Diagnostic*> SimpleProgram::GetSyntacticDiagnostics(
    SourceFile* sourceFile) {
	return collectDiagnostics(
	    sourceFile, false /*concurrent*/,
	    [this](SourceFile* file) -> std::vector<Diagnostic*> {
		    std::vector<Diagnostic*> diags = file->diagnostics;
		    diags.insert(diags.end(), file->jsDiagnostics.begin(),
		                 file->jsDiagnostics.end());
		    if (isSourceFileJS(file) &&
		        !isCheckJSEnabledForFile(file, &options)) {
			    auto extra =
			        getAdditionalJSSyntacticDiagnostics(file, &options);
			    diags.insert(diags.end(), extra.begin(), extra.end());
		    }
		    return diags;
	    });
}

// program.go:761 getAdditionalJSSyntacticDiagnostics — parameter decorators
// in JS files that won't be checked.
std::vector<Diagnostic*> getAdditionalJSSyntacticDiagnostics(
    SourceFile* file, const CompilerOptions* options) {
	if (options->ExperimentalDecorators == Tristate::True) {
		return {};
	}
	std::vector<Diagnostic*> diags;
	std::function<bool(Node*)> walk = [&](Node* node) -> bool {
		if ((node->subtreeFacts() & SubtreeContainsDecorators) == 0) {
			return false;
		}
		if (node->kind == Kind::Parameter && hasDecorators(node)) {
			Node* decorator = nullptr;
			for (Node* m : node->modifierNodes()) {
				if (isDecorator(m)) {
					decorator = m;
					break;
				}
			}
			if (decorator != nullptr) {
				diags.push_back(newDiagnostic(
				    file, decorator->loc,
				    Decorators_are_not_valid_here));
			}
		}
		node->forEachChild(
		    [&](Node* child) -> bool { return walk(child); });
		return false;
	};
	file->forEachChild([&](Node* child) -> bool { return walk(child); });
	return diags;
}

// program.go:786 GetBindDiagnostics
std::vector<Diagnostic*> SimpleProgram::GetBindDiagnostics(
    SourceFile* sourceFile) {
	if (sourceFile != nullptr) {
		bindSourceFile(sourceFile);
	} else {
		BindSourceFiles();
	}
	return collectDiagnostics(
	    sourceFile, false /*concurrent*/,
	    [](SourceFile* file) -> std::vector<Diagnostic*> {
		    return file->bindDiagnostics;
	    });
}

// program.go:804 GetSemanticDiagnostics
std::vector<Diagnostic*> SimpleProgram::GetSemanticDiagnostics(
    SourceFile* sourceFile) {
	return collectCheckerDiagnostics(
	    sourceFile,
	    [this](checker::Checker* c, SourceFile* file)
	        -> std::vector<Diagnostic*> {
		    return getSemanticDiagnosticsWithChecker(c, file);
	    });
}

// program.go:828 collectContentMapperOptionDiagnostics
void SimpleProgram::collectContentMapperOptionDiagnostics() {
	contentmapper::Project* project = ContentMapperProject();
	if (project == nullptr) {
		return;
	}
	for (const contentmapper::OptionDiagnostic& diagnostic :
	     project->Diagnostics()) {
		auto [file, loc] =
		    tsoptions::GetContentMapperOptionDiagnosticLocation(
		        commandLine_, diagnostic.Mapper, diagnostic.Path);
		contentMapperOptionDiagnostics.push_back(newExternalDiagnostic(
		    file, loc, diagnostic.Source, DiagnosticCategory::Error,
		    diagnostic.Code, diagnostic.MessageText));
	}
}

// program.go:826 GetProgramDiagnostics
std::vector<Diagnostic*> SimpleProgram::GetProgramDiagnostics() {
	// programDiagnostics + contentMapperDiagnostics +
	// contentMapperOptionDiagnostics + includeProcessor globals
	std::vector<Diagnostic*> all = programDiagnostics;
	all.insert(all.end(), contentMapperDiagnostics.begin(),
	           contentMapperDiagnostics.end());
	all.insert(all.end(), contentMapperOptionDiagnostics.begin(),
	           contentMapperOptionDiagnostics.end());
	auto ipGlobals =
	    includeProcessor_.getDiagnostics(this)->GetGlobalDiagnostics();
	all.insert(all.end(), ipGlobals.begin(), ipGlobals.end());
	return sortAndDeduplicateDiagnostics(std::move(all));
}

// program.go:840 GetIncludeProcessorDiagnostics
std::vector<Diagnostic*> SimpleProgram::GetIncludeProcessorDiagnostics(
    SourceFile* sourceFile) {
	if (SkipTypeChecking(sourceFile, false)) {
		return {};
	}
	auto [filtered, _] = getDiagnosticsWithPrecedingDirectives(
	    sourceFile, includeProcessor_.getDiagnostics(this)
	                    ->GetDiagnosticsForFile(sourceFile));
	return filtered;
}

// program.go:847 SkipTypeChecking
bool SimpleProgram::SkipTypeChecking(SourceFile* sourceFile,
                                     bool ignoreNoCheck) {
	return (!ignoreNoCheck && options.NoCheck == Tristate::True) ||
	       (options.SkipLibCheck == Tristate::True &&
	        sourceFile->IsDeclarationFile) ||
	       (options.SkipDefaultLibCheck == Tristate::True &&
	        IsSourceFileDefaultLibrary(sourceFile->Path())) ||
	       IsSourceFromProjectReference(sourceFile->Path()) ||
	       !canIncludeBindAndCheckDiagnostics(sourceFile);
}

// program.go:856 canIncludeBindAndCheckDiagnostics
bool SimpleProgram::canIncludeBindAndCheckDiagnostics(
    SourceFile* sourceFile) {
	if (sourceFile->CheckJsDirective != nullptr &&
	    !sourceFile->CheckJsDirective->Enabled) {
		return false;
	}

	if (sourceFile->ScriptKind == ScriptKind::TS ||
	    sourceFile->ScriptKind == ScriptKind::TSX) {
		return true;
	}

	bool isJS = sourceFile->ScriptKind == ScriptKind::JS ||
	            sourceFile->ScriptKind == ScriptKind::JSX;
	bool isCheckJS =
	    isJS && isCheckJSEnabledForFile(sourceFile, &options);
	bool isPlainJS = isPlainJSFile(sourceFile, options.CheckJs);

	return isPlainJS || isCheckJS;
}

// program.go:1498 getSemanticDiagnosticsWithChecker
std::vector<Diagnostic*> SimpleProgram::getSemanticDiagnosticsWithChecker(
    checker::Checker* fileChecker, SourceFile* sourceFile) {
	auto first = filterNoEmitSemanticDiagnostics(
	    getBindAndCheckDiagnosticsWithChecker(fileChecker, sourceFile),
	    &options);
	auto second = GetIncludeProcessorDiagnostics(sourceFile);
	first.insert(first.end(), second.begin(), second.end());
	return first;
}

// program.go:1505 FilterNoEmitSemanticDiagnostics
std::vector<Diagnostic*> filterNoEmitSemanticDiagnostics(
    std::vector<Diagnostic*> diags, const CompilerOptions* options) {
	if (options->NoEmit != Tristate::True) {
		return diags;
	}
	std::vector<Diagnostic*> out;
	for (auto* d : diags) {
		if (!d->SkippedOnNoEmit()) {
			out.push_back(d);
		}
	}
	return out;
}

// program.go:1490 getBindAndCheckDiagnosticsWithChecker
// includeDeferredGlobals is used by execute/incremental: defers global
// diagnostics until the file's check, then appends the delta.
std::vector<Diagnostic*>
SimpleProgram::getBindAndCheckDiagnosticsWithChecker(
    checker::Checker* fileChecker, SourceFile* sourceFile,
    bool includeDeferredGlobals) {
	if (SkipTypeChecking(sourceFile, false)) {
		return {};
	}
	// Checker creation forces binding, so bind diagnostics will be
	// populated.
	std::vector<Diagnostic*> previousGlobals;
	if (includeDeferredGlobals) {
		previousGlobals = fileChecker->GetGlobalDiagnostics();
	}

	std::vector<Diagnostic*> diags = sourceFile->bindDiagnostics;
	auto checkerDiags = fileChecker->GetDiagnostics(sourceFile);
	diags.insert(diags.end(), checkerDiags.begin(), checkerDiags.end());

	if (includeDeferredGlobals) {
		if (fileChecker->WasCanceled()) {
			return {};
		}
		auto currentGlobals = fileChecker->GetGlobalDiagnostics();
		if (currentGlobals.size() > previousGlobals.size()) {
			for (auto* diagnostic : currentGlobals) {
				if (!std::binary_search(
				        previousGlobals.begin(), previousGlobals.end(),
				        diagnostic, [](Diagnostic* a, Diagnostic* b) {
					        return CompareDiagnostics(a, b) < 0;
				        })) {
					diags.push_back(diagnostic);
				}
			}
		}
	}

	bool isPlainJS = isPlainJSFile(sourceFile, options.CheckJs);
	if (isPlainJS) {
		std::vector<Diagnostic*> kept;
		for (auto* d : diags) {
			if (isPlainJSError(d->Code())) {
				kept.push_back(d);
			}
		}
		diags = std::move(kept);
	} else {
		bool isJS = sourceFile->ScriptKind == ScriptKind::JS ||
		            sourceFile->ScriptKind == ScriptKind::JSX;
		bool isCheckJS =
		    isJS && isCheckJSEnabledForFile(sourceFile, &options);
		if (isCheckJS) {
			diags.insert(diags.end(), sourceFile->jsdocDiagnostics.begin(),
			             sourceFile->jsdocDiagnostics.end());
		}
	}

	auto [filtered, directivesByLine] =
	    getDiagnosticsWithPrecedingDirectives(sourceFile, std::move(diags));
	for (auto& [line, directive] : directivesByLine) {
		if (directive.Kind == CommentDirectiveKind::ExpectError) {
			filtered.push_back(newDiagnostic(
			    sourceFile, directive.Loc,
			    Unused_ts_expect_error_directive));
		}
	}
	// applyContentMapperDiagnosticDirectives — content mappers not in scope;
	// files have no DiagnosticDirectives in this mode.
	return filtered;
}

// program.go:1597 getDiagnosticsWithPrecedingDirectives
std::pair<std::vector<Diagnostic*>, std::unordered_map<int, CommentDirective>>
SimpleProgram::getDiagnosticsWithPrecedingDirectives(
    SourceFile* sourceFile, std::vector<Diagnostic*> diags) {
	if (sourceFile->CommentDirectives.empty()) {
		return {diags, {}};
	}
	std::unordered_map<int, CommentDirective> directivesByLine;
	for (auto& directive : sourceFile->CommentDirectives) {
		int line = getECMALineOfPosition(sourceFile,
		                                          directive.Loc.pos());
		directivesByLine[line] = directive;
	}
	auto lineStarts = getECMALineStarts(sourceFile);
	std::vector<Diagnostic*> filtered;
	filtered.reserve(diags.size());
	for (auto* diagnostic : diags) {
		bool ignoreDiagnostic = false;
		if (diagnostic->File() != sourceFile) {
			filtered.push_back(diagnostic);
			continue;
		}
		for (int line = computeLineOfPosition(lineStarts,
		                                               diagnostic->Pos()) -
		                1;
		     line >= 0; line--) {
			if (auto it = directivesByLine.find(line);
			    it != directivesByLine.end()) {
				ignoreDiagnostic = true;
				it->second.Kind = CommentDirectiveKind::Ignore;
				break;
			}
			if (!isCommentOrBlankLine(sourceFile->text,
			                          static_cast<size_t>(
			                              lineStarts[line]))) {
				break;
			}
		}
		if (!ignoreDiagnostic) {
			filtered.push_back(diagnostic);
		}
	}
	return {filtered, directivesByLine};
}

static std::vector<Diagnostic*> compactAndMergeRelatedInfosImpl(
    std::vector<Diagnostic*> diagnostics);

// program.go:1651 SortAndDeduplicateDiagnostics
std::vector<Diagnostic*> sortAndDeduplicateDiagnostics(
    std::vector<Diagnostic*> diagnostics) {
	std::sort(diagnostics.begin(), diagnostics.end(),
	          [](Diagnostic* a, Diagnostic* b) {
		          return CompareDiagnostics(a, b) < 0;
	          });
	return compactAndMergeRelatedInfosImpl(std::move(diagnostics));
}

// program.go:1659 compactAndMergeRelatedInfos
static std::vector<Diagnostic*> compactAndMergeRelatedInfosImpl(
    std::vector<Diagnostic*> diagnostics) {
	if (diagnostics.size() < 2) {
		return diagnostics;
	}
	size_t i = 0, j = 0;
	while (i < diagnostics.size()) {
		Diagnostic* d = diagnostics[i];
		size_t n = 1;
		while (i + n < diagnostics.size() &&
		       EqualDiagnosticsNoRelatedInfo(d, diagnostics[i + n])) {
			n++;
		}
		if (n > 1) {
			std::vector<Diagnostic*> relatedInfos;
			for (size_t k = 0; k < n; k++) {
				auto& ri = diagnostics[i + k]->RelatedInformation();
				relatedInfos.insert(relatedInfos.end(), ri.begin(),
				                    ri.end());
			}
			if (!relatedInfos.empty()) {
				std::sort(relatedInfos.begin(), relatedInfos.end(),
				          [](Diagnostic* a, Diagnostic* b) {
					          return CompareDiagnostics(a, b) < 0;
				          });
				std::vector<Diagnostic*> uniq;
				for (auto* ri : relatedInfos) {
					if (uniq.empty() ||
					    !EqualDiagnostics(uniq.back(), ri)) {
						uniq.push_back(ri);
					}
				}
				d = cloneDiagnostic(d)->SetRelatedInfo(std::move(uniq));
			}
		}
		diagnostics[j] = d;
		i += n;
		j++;
	}
	diagnostics.resize(j);
	return diagnostics;
}

// program.go:694 filterAndSortDiagnostics — SpanMap is always null here (no
// content mappers), so the filter keeps everything.
std::vector<Diagnostic*> filterAndSortDiagnostics(
    std::vector<Diagnostic*> diags) {
	return sortAndDeduplicateDiagnostics(std::move(diags));
}

// program.go:1455 GetGlobalDiagnostics — direct collection on the
// built-in pool; external pools accumulate globals incrementally.
std::vector<Diagnostic*> SimpleProgram::GetGlobalDiagnostics() {
	if (files.empty()) {
		return {};
	}
	if (compilerCheckerPool_ != nullptr) {
		return compilerCheckerPool_->GetGlobalDiagnostics();
	}
	// For external pools (project system), global diagnostics are collected
	// incrementally as checkers are used, not via a bulk query.
	return {};
}

// program.go:1467 GetDeclarationDiagnostics
std::vector<Diagnostic*> SimpleProgram::GetDeclarationDiagnostics(
    SourceFile* sourceFile) {
	return collectDiagnostics(
	    sourceFile, true /*concurrent*/,
	    [this](SourceFile* file) -> std::vector<Diagnostic*> {
		    return getDeclarationDiagnosticsForFile(file);
	    });
}

// program.go:1618 getDeclarationDiagnosticsForFile — memoized via the
// declarationDiagnosticCache SyncMap so the declaration file is never
// emitted twice.
std::vector<Diagnostic*> SimpleProgram::getDeclarationDiagnosticsForFile(
    SourceFile* sourceFile) {
	if (sourceFile->IsDeclarationFile) {
		return {};
	}
	if (auto [cached, ok] = declarationDiagnosticCache.Load(sourceFile);
	    ok) {
		return cached;
	}
	auto [eh, done] = newEmitHost(this, sourceFile);
	auto diags = getDeclarationDiagnostics(eh.get(), sourceFile);
	diags = declarationDiagnosticCache.LoadOrStore(sourceFile, diags).first;
	done();
	return diags;
}

// --- program.go: file/path accessors ---

tspath::Path SimpleProgram::toPath(const std::string& fileName) const {
	return tspath::toPath(fileName, host->GetCurrentDirectory(),
	                      host->UseCaseSensitiveFileNames());
}

tspath::ComparePathsOptions SimpleProgram::comparePathsOptions() const {
	return {host->UseCaseSensitiveFileNames(), host->GetCurrentDirectory()};
}

SourceFile* SimpleProgram::GetSourceFile(const std::string& fileName) {
	return GetSourceFileByPath(toPath(fileName));
}

// program.go:2076 GetSourceFileForResolvedModule — on a miss, retry
// through the project-reference redirect.
SourceFile* SimpleProgram::GetSourceFileForResolvedModule(
    const std::string& fileName) {
	SourceFile* file = GetSourceFile(fileName);
	if (file == nullptr) {
		if (std::string filename = GetParseFileRedirect(fileName);
		    !filename.empty()) {
			return GetSourceFile(filename);
		}
	}
	return file;
}

bool SimpleProgram::FileExists(const std::string& fileName) {
	return host->FileExists(fileName);
}

std::string SimpleProgram::GetCurrentDirectory() {
	return host->GetCurrentDirectory();
}

// program.go:231 UseCaseSensitiveFileNames — the host's fs decides.
bool SimpleProgram::UseCaseSensitiveFileNames() {
	return host->FS()->UseCaseSensitiveFileNames();
}

const std::vector<const FileIncludeReason*>* SimpleProgram::GetIncludeReasons(
    const tspath::Path& path) const {
	auto it = includeProcessor_.fileIncludeReasons.find(path);
	if (it == includeProcessor_.fileIncludeReasons.end()) {
		return nullptr;
	}
	return &it->second;
}

// program.go: GetLibFileFromReference
SourceFile* SimpleProgram::GetLibFileFromReference(const FileReference* ref) {
	auto [path, ok] = tsoptions::getLibFileName(ref->FileName);
	if (!ok) {
		return nullptr;
	}
	return GetSourceFileByPath(tspath::Path(path));
}

// program.go: IsSourceFileDefaultLibrary / IsLibFile
bool SimpleProgram::IsSourceFileDefaultLibrary(
    const tspath::Path& path) const {
	return libFiles.find(path) != libFiles.end();
}

bool SimpleProgram::IsLibFile(SourceFile* file) const {
	auto it = libFiles.find(file->Path());
	return it != libFiles.end() &&
	       it->second->name == file->fileName.substr(
	           file->fileName.rfind('/') + 1);
}

// program.go: IsSourceFileFromExternalLibrary
bool SimpleProgram::IsSourceFileFromExternalLibrary(SourceFile* file) const {
	return sourceFilesFoundSearchingNodeModules.count(file->Path()) != 0;
}

// program.go:194 IsSourceFromProjectReference — mapper delegate.
bool SimpleProgram::IsSourceFromProjectReference(
    const tspath::Path& path) const {
	return projectReferenceFileMapper_ != nullptr &&
	       projectReferenceFileMapper_->isSourceFromProjectReference(
	           path);
}

// === slice: project ===

// program.go — the tsoptions.ParsedCommandLine that satisfies both
// checker::RedirectInfo and checker::ProjectReference gets one cached
// adapter per config (stable deque storage — Go's GC shares the single
// interface value).
SimpleProgram::parsedCommandLineAdapter* SimpleProgram::adapterFor(
    tsoptions::ParsedCommandLine* ref) {
	if (ref == nullptr) {
		return nullptr;
	}
	auto it = parsedCommandLineAdapterIndex_.find(ref);
	if (it != parsedCommandLineAdapterIndex_.end()) {
		return it->second;
	}
	parsedCommandLineAdapters_.push_back(parsedCommandLineAdapter{});
	parsedCommandLineAdapters_.back().ref = ref;
	parsedCommandLineAdapters_.back().redirectInfo.ref = ref;
	parsedCommandLineAdapters_.back().projectRef.ref = ref;
	auto* adapter = &parsedCommandLineAdapters_.back();
	parsedCommandLineAdapterIndex_[ref] = adapter;
	return adapter;
}

// tsoptions -> checker SourceOutputAndProjectReference materialization;
// the resolved ParsedCommandLine rides through the shared adapter so the
// checker's ProjectReference* stays stable for a given config.
checker::SourceOutputAndProjectReference* SimpleProgram::toCheckerRef(
    tsoptions::SourceOutputAndProjectReference* ref) {
	if (ref == nullptr) {
		return nullptr;
	}
	auto it = checkerRefCache_.find(ref);
	if (it != checkerRefCache_.end()) {
		return &it->second;
	}
	auto* resolved = ref->Resolved != nullptr
	                     ? &adapterFor(ref->Resolved)->projectRef
	                     : nullptr;
	auto [insIt, _] = checkerRefCache_.emplace(
	    ref,
	    checker::SourceOutputAndProjectReference{
	        ref->Source, ref->OutputDts, resolved});
	return &insIt->second;
}

// program.go:181 GetSourceOfProjectReferenceIfOutputIncluded — the
// output->source map only carries entries when
// !canUseProjectReferenceSource(); otherwise the name is returned
// unchanged.
std::string SimpleProgram::GetSourceOfProjectReferenceIfOutputIncluded(
    const HasFileName& file) {
	auto it = outputFileToProjectReferenceSource.find(file.Path());
	if (it != outputFileToProjectReferenceSource.end()) {
		return it->second;
	}
	return file.FileName();
}

// program.go:189 GetProjectReferenceFromSource — checker.Program
// override (mapper delegate through the tsoptions->checker adapter).
checker::SourceOutputAndProjectReference*
SimpleProgram::GetProjectReferenceFromSource(const tspath::Path& path) {
	return toCheckerRef(
	    projectReferenceFileMapper_->getProjectReferenceFromSource(
	        path));
}

// program.go:198 GetProjectReferenceFromOutputDts — checker.Program
// override.
const checker::SourceOutputAndProjectReference*
SimpleProgram::GetProjectReferenceFromOutputDts(const std::string& path) {
	return toCheckerRef(
	    projectReferenceFileMapper_->getProjectReferenceFromOutputDts(
	        tspath::Path(path)));
}

// program.go:206 GetRedirectForResolution — checker.Program override;
// the resolved config adapts into checker::RedirectInfo.
checker::RedirectInfo* SimpleProgram::GetRedirectForResolution(
    SourceFile* file) {
	auto* adapter = adapterFor(
	    projectReferenceFileMapper_
	        ->getRedirectParsedCommandLineForResolution(
	            HasFileName{file->FileName(), file->Path()}));
	return adapter != nullptr ? &adapter->redirectInfo : nullptr;
}

// program.go:202 GetResolvedProjectReferenceFor.
std::pair<tsoptions::ParsedCommandLine*, bool>
SimpleProgram::GetResolvedProjectReferenceFor(const tspath::Path& path) {
	return projectReferenceFileMapper_->getResolvedReferenceFor(path);
}

// === end slice: project ===

const SourceFileMetaData& SimpleProgram::GetSourceFileMetaData(
    const tspath::Path& path) const {
	static const SourceFileMetaData empty{};
	auto it = sourceFileMetaDatas.find(path);
	return it != sourceFileMetaDatas.end() ? it->second : empty;
}

// --- emit-format helpers (program.go:1750+) ---

ModuleKind SimpleProgram::GetEmitModuleFormatOfFile(SourceFile* sourceFile) {
	return getEmitModuleFormatOfFileWorker(sourceFile->FileName(), &options,
	                                     GetSourceFileMetaData(
	                                         sourceFile->Path()));
}

ResolutionMode SimpleProgram::GetEmitSyntaxForUsageLocation(
    SourceFile* sourceFile, Node* usageLocation) {
	return getEmitSyntaxForUsageLocationWorker(
	    sourceFile->FileName(),
	    sourceFileMetaDatas[sourceFile->Path()], usageLocation, &options);
}

ModuleKind SimpleProgram::GetImpliedNodeFormatForEmit(
    SourceFile* sourceFile) {
	return getImpliedNodeFormatForEmitWorker(
	    sourceFile->FileName(), options.GetEmitModuleKind(),
	    GetSourceFileMetaData(sourceFile->Path()));
}

ResolutionMode SimpleProgram::GetModeForUsageLocation(SourceFile* file,
                                                    Node* location) {
	return getModeForUsageLocation(
	    file->FileName(), sourceFileMetaDatas[file->Path()], location,
	    &options);
}

// program.go:1759 GetModeForResolutionAtIndex
ResolutionMode SimpleProgram::GetModeForResolutionAtIndex(
    SourceFile* sourceFile, int index) {
	const auto& imports = sourceFile->imports;
	if (index < static_cast<int>(imports.size())) {
		return GetModeForUsageLocation(sourceFile, imports[index]);
	}
	index -= static_cast<int>(imports.size());
	for (auto* augmentation : sourceFile->ModuleAugmentations) {
		if (augmentation->kind == Kind::StringLiteral) {
			if (index == 0) {
				return GetModeForUsageLocation(sourceFile, augmentation);
			}
			index--;
		}
	}
	TSC_UNREACHABLE(
	    "resolution index out of range — ported with the program slice");
}

ResolutionMode SimpleProgram::GetDefaultResolutionModeForFile(
    SourceFile* file) {
	return getDefaultResolutionModeForFile(
	    file->FileName(), sourceFileMetaDatas[file->Path()], &options);
}

// program.go:1791 checkSourceFilesBelongToPath
bool SimpleProgram::checkSourceFilesBelongToPath(
    const std::vector<std::string>& sourceFiles,
    const std::string& rootDirectory) {
	bool allFilesBelongToPath = true;
	for (auto& file : sourceFiles) {
		std::string absoluteSourceFilePath = tspath::getCanonicalFileName(
		    tspath::getNormalizedAbsolutePath(file, GetCurrentDirectory()),
		    UseCaseSensitiveFileNames());
		if (!tspath::containsPath(rootDirectory, file,
		                          comparePathsOptions())) {
			includeProcessor_.addProcessingDiagnostic(
			    includeProcessor_.newProcessingDiagnostic(
			        processingDiagnosticKind::ExplainingFileInclude,
			        includeExplainingDiagnostic{
			            tspath::Path(absoluteSourceFilePath), nullptr,
			            
			                File_0_is_not_under_rootDir_1_rootDir_is_expected_to_contain_all_source_files,
			            {file, rootDirectory}}));
			allFilesBelongToPath = false;
		}
	}
	return allFilesBelongToPath;
}

// program.go:1799 CommonSourceDirectory — memoized via sync.Once; emit
// tasks call it from parallel workers.
std::string SimpleProgram::CommonSourceDirectory() {
	commonSourceDirectoryOnce_.run([this] {
		// files() closure: emitted file names
		auto filesFn = [this]() -> std::vector<std::string> {
			std::vector<std::string> emittedFiles;
			for (auto* file : this->files) {
				if (sourceFileMayBeEmitted(file, this, false, false) &&
				    !file->IsDeclarationFile) {
					emittedFiles.push_back(file->FileName());
				}
			}
			return emittedFiles;
		};
		commonSourceDirectory_ = outputpaths::GetCommonSourceDirectory(
		    Options(), filesFn, GetCurrentDirectory(),
		    UseCaseSensitiveFileNames(),
		    [this](const std::vector<std::string>& sourceFiles,
		           std::string_view rootDirectory) {
			    return checkSourceFilesBelongToPath(
			        sourceFiles, std::string(rootDirectory));
		    });
	});
	return *commonSourceDirectory_;
}

// program.go:1390 IsEmitBlocked / :1384 blockEmittingOfFile
bool SimpleProgram::IsEmitBlocked(const std::string& emitFileName) const {
	return hasEmitBlockingDiagnostics->contains(toPath(emitFileName));
}

void SimpleProgram::blockEmittingOfFile(const std::string& emitFileName,
                                        Diagnostic* diag) {
	hasEmitBlockingDiagnostics->insert(toPath(emitFileName));
	programDiagnostics.push_back(diag);
}

// program.go:875 getSourceFilesToEmit — memoized for the nil-target case
// (Go sync.Once).
std::vector<SourceFile*> SimpleProgram::getSourceFilesToEmit(
    const std::vector<SourceFile*>* targetSourceFiles, bool forceDtsEmit,
    bool forceJsEmit) {
	if (targetSourceFiles == nullptr && !forceDtsEmit && !forceJsEmit) {
		if (!sourceFilesToEmitComputed_) {
			sourceFilesToEmitComputed_ = true;
			sourceFilesToEmit_ =
			    compiler::getSourceFilesToEmit(this, nullptr, false, false);
		}
		return sourceFilesToEmit_;
	}
	return compiler::getSourceFilesToEmit(this, targetSourceFiles,
	                                      forceDtsEmit, forceJsEmit);
}

// program.go:242 GetSourceFileFromReference
SourceFile* SimpleProgram::GetSourceFileFromReference(SourceFile* origin,
                                                    FileReference* ref) {
	std::string fileName = tspath::resolvePath(
	    tspath::getDirectoryPath(origin->FileName()), {ref->FileName});
	auto* supportedExtensionsBase = tsoptions::GetSupportedExtensions(
	    &options, {}); // CommandLine().ContentMapperExtensions() — none here
	auto* supportedExtensions =
	    tsoptions::GetSupportedExtensionsWithJsonIfResolveJsonModule(
	        &options, supportedExtensionsBase);
	bool allowNonTsExtensions =
	    options.AllowNonTsExtensions == Tristate::True;
	if (tspath::hasExtension(fileName)) {
		if (!allowNonTsExtensions) {
			std::string canonicalFileName = tspath::getCanonicalFileName(
			    fileName, UseCaseSensitiveFileNames());
			bool supported = false;
			for (const auto& group : *supportedExtensions) {
				if (tspath::fileExtensionIsOneOf(canonicalFileName, group)) {
					supported = true;
					break;
				}
			}
			if (!supported) {
				return nullptr; // unsupported extensions are forced to fail
			}
		}

		return GetSourceFileForResolvedModule(fileName);
	}
	if (allowNonTsExtensions) {
		auto* extensionless = GetSourceFileForResolvedModule(fileName);
		if (extensionless != nullptr) {
			return extensionless;
		}
	}

	// Only try adding extensions from the first supported group (which
	// should be .ts/.tsx/.d.ts)
	for (const auto& ext : (*supportedExtensions)[0]) {
		auto* result = GetSourceFileForResolvedModule(
		    fileName + std::string(ext));
		if (result != nullptr) {
			return result;
		}
	}
	return nullptr;
}

bool SimpleProgram::SourceFileMayBeEmitted(SourceFile* sourceFile,
                                           bool forceDtsEmit) {
	return sourceFileMayBeEmitted(sourceFile, this, forceDtsEmit, false);
}

// program.go:1867 Emit — Go runs file emits through a WorkGroup; this port
// emits sequentially (identical observable results).
EmitResult* SimpleProgram::Emit(EmitOptions* options) {
	// program.go:1868 — `defer tr.Push(PhaseEmit, "emit", nil, true)()`.
	tracing::TraceScope emitTraceGuard(tr_, tracing::PhaseEmit, "emit", {},
	                                   true);

	if (!options->ForceEmit &&
	    options->EmitOnly != EmitOnly::EmitOnlyBuilderSignature) {
		// Go passes options.TargetSourceFiles (nil when unset); an empty
		// vector is our nil, so map empty back to nullptr here.
		auto* result = HandleNoEmitOptions(
		    this,
		    options->TargetSourceFiles.empty() ? nullptr
		                                       : &options->TargetSourceFiles,
		    nullptr);
		if (result != nullptr) {
			return result;
		}
	}

	const std::string newLine =
	    Options()->NewLine == NewLineKind::CarriageReturnLineFeed
	        ? "\r\n"
	        : "\n";
	// program.go:1885 writerPool — sync.Pool of EmitTextWriter.
	struct emitTextWriterPool {
		std::mutex mu;
		std::vector<std::unique_ptr<printer::EmitTextWriter>> pool;
		const std::string* newLine;
		printer::EmitTextWriter* Get() {
			std::lock_guard<std::mutex> lock(mu);
			if (!pool.empty()) {
				auto* w = pool.back().release();
				pool.pop_back();
				return w;
			}
			return printer::NewTextWriter(*newLine, 0);
		}
		void Put(printer::EmitTextWriter* w) {
			std::lock_guard<std::mutex> lock(mu);
			pool.emplace_back(w);
		}
	};
	emitTextWriterPool writerPool{.newLine = &newLine};
	std::unique_ptr<workGroup> wg(newWorkGroup(SingleThreaded()));
	std::vector<std::unique_ptr<emitter>> emitters;
	bool forceDtsEmit =
	    options->EmitOnly == EmitOnly::EmitOnlyBuilderSignature ||
	    (options->ForceEmit && options->EmitOnly == EmitOnly::EmitOnlyDts);
	bool forceJsEmit =
	    options->ForceEmit && options->EmitOnly == EmitOnly::EmitOnlyJs;
	auto sourceFiles = getSourceFilesToEmit(
	    options->TargetSourceFiles.empty() ? nullptr
	                                       : &options->TargetSourceFiles,
	    forceDtsEmit, forceJsEmit);

	for (auto* sourceFile : sourceFiles) {
		auto* e = emitters.emplace_back(new emitter).get();
		e->sourceFile = sourceFile;
		e->emitOnly = options->EmitOnly;
		e->forceEmit = options->ForceEmit;
		e->writeFile = options->WriteFile;
		e->tr = tr_; // program.go:1903

		wg->Queue([this, e, sourceFile, &writerPool, forceDtsEmit,
		          forceJsEmit, options] {
			auto [host, done] = newEmitHost(this, sourceFile);
			e->host = host.get();

			// take an unused writer
			std::unique_ptr<printer::EmitTextWriter> writer(
			    writerPool.Get());
			writer->Clear();

			// attach writer and perform emit
			e->writer = writer.get();
			e->paths = outputpaths::GetOutputPathsFor(
			    sourceFile, e->host->Options(), e->host,
			    outputpaths::ForceEmitPaths{
			        .Dts = forceDtsEmit,
			        .Js = forceJsEmit,
			        .DeclarationMap =
			            options->ForceEmit &&
			            options->EmitOnly == EmitOnly::EmitOnlyDts,
			    });
			e->emit();
			e->writer = nullptr;

			// put the writer back in the pool
			writerPool.Put(writer.release());

			done(); // Go: `defer done()` — release the checker last
		});
	}

	// wait for emit to complete
	wg->RunAndWait();

	// collect results from emit, preserving input order
	std::vector<EmitResult*> results;
	results.reserve(emitters.size());
	for (auto& e : emitters) {
		results.push_back(&e->emitResult);
	}
	return CombineEmitResults(results);
}

// program.go:1941 CombineEmitResults
EmitResult* CombineEmitResults(const std::vector<EmitResult*>& results) {
	auto* result = new EmitResult{};
	for (auto* emitResult : results) {
		if (emitResult == nullptr) {
			continue; // Skip nil results
		}
		if (emitResult->EmitSkipped) {
			result->EmitSkipped = true;
		}
		result->Diagnostics.insert(result->Diagnostics.end(),
		                         emitResult->Diagnostics.begin(),
		                         emitResult->Diagnostics.end());
		result->EmittedFiles.insert(result->EmittedFiles.end(),
		                          emitResult->EmittedFiles.begin(),
		                          emitResult->EmittedFiles.end());
		if (!emitResult->SourceMaps.empty()) {
			result->SourceMaps.insert(result->SourceMaps.end(),
			                          emitResult->SourceMaps.begin(),
			                          emitResult->SourceMaps.end());
		}
	}
	return result;
}

// program.go:1966 HandleNoEmitOptions — program is a ProgramLike (Go);
// emitBuildInfo nullptr == Go nil.
EmitResult* HandleNoEmitOptions(
    ProgramLike* program, const std::vector<SourceFile*>* files,
    const std::function<EmitResult*()>& emitBuildInfo) {
	if (program->Options()->NoEmit != Tristate::True) {
		if (program->Options()->NoEmitOnError != Tristate::True) {
			return nullptr; // NoEmit is false and NoEmitOnError is also
			                // false, so we can proceed with normal emit
		}

		auto diagnostics = getDiagnosticsOfAnyProgram(
		    program, files == nullptr ? std::vector<SourceFile*>{} : *files,
		    true);
		if (diagnostics.empty()) {
			return nullptr; // NoEmitOnError is enabled, but no
			                // diagnostics were found, so we can proceed
			                // with emitting
		}
		auto* result = new EmitResult{};
		result->Diagnostics = diagnostics;
		result->EmitSkipped = true;
		return result;
	}
	if (files != nullptr) {
		auto* result = new EmitResult{};
		result->EmitSkipped = true;
		return result;
	}
	if (emitBuildInfo != nullptr) {
		auto* result = emitBuildInfo();
		if (result != nullptr) {
			return result;
		}
	}
	return new EmitResult{};
}

// ==== verifyCompilerOptions ====  program.go:866+
void SimpleProgram::verifyCompilerOptions() {
	// program.go:883 — memoized config-file syntax accessors. Without a
	// config file they all evaluate to nullptr, matching Go's
	// ForEachTsConfigPropArray(nil)/ForEachPropertyAssignment(nil) no-ops.
	SourceFile* sourceFile_ = nullptr;
	bool sourceFileComputed = false;
	auto sourceFile = [&]() -> SourceFile* {
		if (!sourceFileComputed) {
			sourceFileComputed = true;
			if (opts_.Config != nullptr && opts_.Config->ConfigFile != nullptr) {
				sourceFile_ = opts_.Config->ConfigFile->SourceFile;
			}
		}
		return sourceFile_;
	};

	std::string configFilePath_;
	bool configFilePathComputed = false;
	auto configFilePath = [&]() -> const std::string& {
		if (!configFilePathComputed) {
			configFilePathComputed = true;
			if (SourceFile* file = sourceFile(); file != nullptr) {
				configFilePath_ = file->FileName();
			}
		}
		return configFilePath_;
	};

	PropertyAssignment* compilerOptionsProperty_ = nullptr;
	bool compilerOptionsPropertyComputed = false;
	auto getCompilerOptionsPropertySyntax = [&]() -> PropertyAssignment* {
		if (!compilerOptionsPropertyComputed) {
			compilerOptionsPropertyComputed = true;
			compilerOptionsProperty_ =
			    tsoptions::ForEachTsConfigPropArray<PropertyAssignment>(
			        sourceFile(), "compilerOptions",
			        [](PropertyAssignment* prop) { return prop; });
		}
		return compilerOptionsProperty_;
	};

	ObjectLiteralExpression* compilerOptionsObjectLiteral_ = nullptr;
	bool compilerOptionsObjectLiteralComputed = false;
	auto getCompilerOptionsObjectLiteralSyntax =
	    [&]() -> ObjectLiteralExpression* {
		if (!compilerOptionsObjectLiteralComputed) {
			compilerOptionsObjectLiteralComputed = true;
			PropertyAssignment* compilerOptionsProperty =
			    getCompilerOptionsPropertySyntax();
			if (compilerOptionsProperty != nullptr &&
			    compilerOptionsProperty->Initializer != nullptr &&
			    isObjectLiteralExpression(
			        compilerOptionsProperty->Initializer)) {
				compilerOptionsObjectLiteral_ =
				    compilerOptionsProperty->Initializer
				        ->as<ObjectLiteralExpression>();
			}
		}
		return compilerOptionsObjectLiteral_;
	};

	auto createCompilerOptionsDiagnostic =
	    [&](const DiagnosticMessage* message,
	        std::vector<std::string> args = {}) -> Diagnostic* {
		PropertyAssignment* compilerOptionsProperty =
		    getCompilerOptionsPropertySyntax();
		Diagnostic* diag;
		if (compilerOptionsProperty != nullptr) {
			diag = tsoptions::createDiagnosticForNodeInSourceFile(
			    sourceFile(), compilerOptionsProperty->name, message,
			    std::move(args));
		} else {
			diag = tsoptions::newCompilerDiagnostic(message,
			                                        std::move(args));
		}
		programDiagnostics.push_back(diag);
		return diag;
	};

	// createOptionDiagnosticInObjectLiteralSyntax — program.go:918.
	auto createOptionDiagnosticInObjectLiteralSyntax =
	    [&](ObjectLiteralExpression* objectLiteral, bool onKey,
	        const std::string& key1, const std::string& key2,
	        const DiagnosticMessage* message,
	        std::vector<std::string> args = {}) -> Diagnostic* {
		Diagnostic* diag = tsoptions::ForEachPropertyAssignment<Diagnostic>(
		    objectLiteral, key1,
		    [&](PropertyAssignment* property) -> Diagnostic* {
			    return tsoptions::createDiagnosticForNodeInSourceFile(
			        sourceFile(),
			        onKey ? property->name : property->Initializer, message,
			        args);
		    },
		    key2);
		if (diag != nullptr) {
			programDiagnostics.push_back(diag);
		}
		return diag;
	};

	auto createDiagnosticForOption =
	    [&](bool onKey, const std::string& option1, const std::string& option2,
	        const DiagnosticMessage* message,
	        std::vector<std::string> args = {}) -> Diagnostic* {
		Diagnostic* diag = createOptionDiagnosticInObjectLiteralSyntax(
		    getCompilerOptionsObjectLiteralSyntax(), onKey, option1,
		    option2, message, args);
		if (diag == nullptr) {
			diag = createCompilerOptionsDiagnostic(message,
			                                     std::move(args));
		}
		return diag;
	};

	auto createDiagnosticForOptionName =
	    [&](const DiagnosticMessage* message, const std::string& option1,
	        const std::string& option2,
	        std::vector<std::string> args = {}) {
		std::vector<std::string> newArgs;
		newArgs.reserve(args.size() + 2);
		newArgs.push_back(option1);
		newArgs.push_back(option2);
		for (auto& a : args)
			newArgs.push_back(std::move(a));
		createDiagnosticForOption(true, option1, option2, message,
		                          std::move(newArgs));
	};

	auto createOptionValueDiagnostic =
	    [&](const std::string& option1, const DiagnosticMessage* message,
	        std::vector<std::string> args = {}) {
		createDiagnosticForOption(false, option1, "", message,
		                          std::move(args));
	};

	auto createRemovedOptionDiagnostic =
	    [&](const std::string& name, const std::string& value,
	        const std::string& useInstead) {
		const DiagnosticMessage* message;
		std::vector<std::string> args;
		if (value.empty()) {
			message = 
			    Option_0_has_been_removed_Please_remove_it_from_your_configuration;
			args = {name};
		} else {
			message = 
			    Option_0_1_has_been_removed_Please_remove_it_from_your_configuration;
			args = {name, value};
		}
		auto* diag = createDiagnosticForOption(value.empty(), name, "",
		                                       message, args);
		if (!useInstead.empty()) {
			diag->AddMessageChain(tsoptions::newCompilerDiagnostic(
			    Use_0_instead, {useInstead}));
		}
	};

	// Removed in TS7

	if (!options.BaseUrl.empty()) {
		// BaseUrl will have been turned absolute by this point.
		std::string useInstead;
		if (!configFilePath().empty()) {
			std::string relative = tspath::getRelativePathFromFile(
			    configFilePath(), options.BaseUrl, comparePathsOptions());
			if (!(relative.starts_with("./") ||
			      relative.starts_with("../"))) {
				relative = "./" + relative;
			}
			std::string suggestion =
			    tspath::combinePaths(relative, {"*"});
			useInstead = "\"paths\": {\"*\": [" +
			           std::string(json::marshalString(suggestion)) +
			           "]}";
		}
		createRemovedOptionDiagnostic("baseUrl", "", useInstead);
	}

	if (!options.OutFile.empty()) {
		createRemovedOptionDiagnostic("outFile", "", "");
	}

	if (options.Target == ScriptTarget::ES5) {
		createRemovedOptionDiagnostic("target", "ES5", "");
	}

	if (options.Module == ModuleKind::AMD) {
		createRemovedOptionDiagnostic("module", "AMD", "");
	}
	if (options.Module == ModuleKind::System) {
		createRemovedOptionDiagnostic("module", "System", "");
	}
	if (options.Module == ModuleKind::UMD) {
		createRemovedOptionDiagnostic("module", "UMD", "");
	}

	if (options.ModuleResolution == ModuleResolutionKind::Classic) {
		createRemovedOptionDiagnostic("moduleResolution", "Classic", "");
	}

	if (options.AlwaysStrict == Tristate::False) {
		createRemovedOptionDiagnostic("alwaysStrict", "false", "");
	}

	if (options.ESModuleInterop == Tristate::False) {
		createRemovedOptionDiagnostic("esModuleInterop", "false", "");
	}

	if (options.AllowSyntheticDefaultImports == Tristate::False) {
		createRemovedOptionDiagnostic("allowSyntheticDefaultImports",
		                              "false", "");
	}

	if (options.ModuleResolution == ModuleResolutionKind::Node10) {
		createRemovedOptionDiagnostic("moduleResolution", "node10", "");
	}

	if (options.DownlevelIteration != Tristate::Unknown) {
		createRemovedOptionDiagnostic("downlevelIteration", "", "");
	}

	if (options.StrictPropertyInitialization == Tristate::True &&
	    !options.GetStrictOptionValue(options.StrictNullChecks)) {
		createDiagnosticForOptionName(
		    
		        Option_0_cannot_be_specified_without_specifying_option_1,
		    "strictPropertyInitialization", "strictNullChecks");
	}
	if (options.ExactOptionalPropertyTypes == Tristate::True &&
	    !options.GetStrictOptionValue(options.StrictNullChecks)) {
		createDiagnosticForOptionName(
		    
		        Option_0_cannot_be_specified_without_specifying_option_1,
		    "exactOptionalPropertyTypes", "strictNullChecks");
	}

	if (options.IsolatedDeclarations == Tristate::True) {
		if (options.GetAllowJS()) {
			createDiagnosticForOptionName(
			    Option_0_cannot_be_specified_with_option_1,
			    "allowJs", "isolatedDeclarations");
		}
		if (!options.GetEmitDeclarations()) {
			createDiagnosticForOptionName(
			    
			        Option_0_cannot_be_specified_without_specifying_option_1_or_option_2,
			    "isolatedDeclarations", "declaration", {"composite"});
		}
	}

	if (options.InlineSourceMap == Tristate::True) {
		if (options.SourceMap == Tristate::True) {
			createDiagnosticForOptionName(
			    Option_0_cannot_be_specified_with_option_1,
			    "sourceMap", "inlineSourceMap");
		}
		if (!options.MapRoot.empty()) {
			createDiagnosticForOptionName(
			    Option_0_cannot_be_specified_with_option_1,
			    "mapRoot", "inlineSourceMap");
		}
	}

	if (options.Composite == Tristate::True) {
		if (options.Declaration == Tristate::False) {
			createDiagnosticForOptionName(
			    
			        Composite_projects_may_not_disable_declaration_emit,
			    "declaration", "");
		}
		if (options.Incremental == Tristate::False) {
			createDiagnosticForOptionName(
			    
			        Composite_projects_may_not_disable_incremental_compilation,
			    "declaration", "");
		}
	}

	if (options.TsBuildInfoFile.empty() &&
	    options.Incremental == Tristate::True &&
	    options.ConfigFilePath.empty()) {
		createCompilerOptionsDiagnostic(
		    
		        Option_incremental_is_only_valid_with_a_known_configuration_file_like_tsconfig_json_or_when_tsBuildInfoFile_is_explicitly_provided);
	}

	verifyProjectReferences();

	if (options.Composite == Tristate::True) {
		std::unordered_set<tspath::Path> rootPaths;
		for (auto& fileName : fileNameList) {
			rootPaths.insert(toPath(fileName));
		}
		for (auto* file : files) {
			tspath::Path rootPath = file->Path();
			// CanonicalSourceFile — no content mappers → file itself.
			if (sourceFileMayBeEmitted(file, this, false, false) &&
			    !rootPaths.count(rootPath)) {
				includeProcessor_.addProcessingDiagnostic(
				    includeProcessor_.newProcessingDiagnostic(
				        processingDiagnosticKind::ExplainingFileInclude,
				        includeExplainingDiagnostic{
				            file->Path(), nullptr,
				            
				                File_0_is_not_listed_within_the_file_list_of_project_1_Projects_must_list_all_files_or_use_an_include_pattern,
				            {file->FileName(), ""}}));
			}
		}
	}

	// forEachOptionPathsSyntax / createDiagnosticForOptionPaths /
	// createDiagnosticForOptionPathKeyValue — program.go:1097-1134.
	auto forEachOptionPathsSyntax =
	    [&](const std::function<Diagnostic*(PropertyAssignment*)>& callback)
	    -> Diagnostic* {
		return tsoptions::ForEachPropertyAssignment<Diagnostic>(
		    getCompilerOptionsObjectLiteralSyntax(), "paths", callback);
	};

	auto createDiagnosticForOptionPaths =
	    [&](bool onKey, const std::string& key,
	        const DiagnosticMessage* message,
	        std::vector<std::string> args = {}) -> Diagnostic* {
		Diagnostic* diag = forEachOptionPathsSyntax(
		    [&](PropertyAssignment* pathProp) -> Diagnostic* {
			    if (isObjectLiteralExpression(pathProp->Initializer)) {
				    return createOptionDiagnosticInObjectLiteralSyntax(
				        pathProp->Initializer
				            ->as<ObjectLiteralExpression>(),
				        onKey, key, "", message, args);
			    }
			    return nullptr;
		    });
		if (diag == nullptr) {
			diag = createCompilerOptionsDiagnostic(message,
			                                     std::move(args));
		}
		return diag;
	};

	auto createDiagnosticForOptionPathKeyValue =
	    [&](const std::string& key, int valueIndex,
	        const DiagnosticMessage* message,
	        std::vector<std::string> args = {}) -> Diagnostic* {
		Diagnostic* diag = forEachOptionPathsSyntax(
		    [&](PropertyAssignment* pathProp) -> Diagnostic* {
			    if (isObjectLiteralExpression(pathProp->Initializer)) {
				    return tsoptions::
				        ForEachPropertyAssignment<Diagnostic>(
				            pathProp->Initializer
				                ->as<ObjectLiteralExpression>(),
				            key,
				            [&](PropertyAssignment* keyProps)
				                -> Diagnostic* {
					            Node* initializer =
					                keyProps->Initializer;
					            if (isArrayLiteralExpression(
					                    initializer)) {
						            NodeList* elements =
						                initializer
						                    ->as<ArrayLiteralExpression>()
						                    ->Elements;
						            if (elements != nullptr &&
						                valueIndex >= 0 &&
						                static_cast<size_t>(
						                    valueIndex) <
						                    elements->nodes.size()) {
							            Diagnostic* diag =
							                tsoptions::
							                    createDiagnosticForNodeInSourceFile(
							                        sourceFile(),
							                        elements
							                            ->nodes
							                                [valueIndex],
							                        message,
							                        args);
							            programDiagnostics
							                .push_back(diag);
							            return diag;
						            }
					            }
					            return nullptr;
				            });
			    }
			    return nullptr;
		    });
		if (diag == nullptr) {
			diag = createCompilerOptionsDiagnostic(message,
			                                     std::move(args));
		}
		return diag;
	};

	for (auto& [key, value] : options.Paths) {
		if (!hasZeroOrOneAsteriskCharacter(key)) {
			createDiagnosticForOptionPaths(
			    true, key,
			    
			        Pattern_0_can_have_at_most_one_Asterisk_character,
			    {key});
		}
		if (value.empty()) {
			// Go distinguishes nil ("should be an array") from empty
			// ("shouldn't be an empty array"); both collapse to empty here,
			// and tsconfig-driven paths can't occur without a config file.
			createDiagnosticForOptionPaths(
			    false, key,
			    
			        Substitutions_for_pattern_0_shouldn_t_be_an_empty_array,
			    {key});
		}
		for (size_t i = 0; i < value.size(); i++) {
			const std::string& subst = value[i];
			if (!hasZeroOrOneAsteriskCharacter(subst)) {
				createDiagnosticForOptionPathKeyValue(
				    key, static_cast<int>(i),
				    
				        Substitution_0_in_pattern_1_can_have_at_most_one_Asterisk_character,
				    {subst, key});
			}
			if (!tspath::pathIsRelative(subst) &&
			    !tspath::pathIsAbsolute(subst)) {
				createDiagnosticForOptionPathKeyValue(
				    key, static_cast<int>(i),
				    
				        Non_relative_paths_are_not_allowed_Did_you_forget_a_leading_Slash,
				    {});
			}
		}
	}

	if (options.SourceMap != Tristate::True &&
	    options.InlineSourceMap != Tristate::True) {
		if (options.InlineSources == Tristate::True) {
			createDiagnosticForOptionName(
			    
			        Option_0_can_only_be_used_when_either_option_inlineSourceMap_or_option_sourceMap_is_provided,
			    "inlineSources", "");
		}
		if (!options.SourceRoot.empty()) {
			createDiagnosticForOptionName(
			    
			        Option_0_can_only_be_used_when_either_option_inlineSourceMap_or_option_sourceMap_is_provided,
			    "sourceRoot", "");
		}
	}

	if (!options.MapRoot.empty() &&
	    !(options.SourceMap == Tristate::True ||
	      options.DeclarationMap == Tristate::True)) {
		createDiagnosticForOptionName(
		    
		        Option_0_cannot_be_specified_without_specifying_option_1_or_option_2,
		    "mapRoot", "sourceMap", {"declarationMap"});
	}

	if (!options.DeclarationDir.empty()) {
		if (!options.GetEmitDeclarations()) {
			createDiagnosticForOptionName(
			    
			        Option_0_cannot_be_specified_without_specifying_option_1_or_option_2,
			    "declarationDir", "declaration", {"composite"});
		}
	}

	if (options.DeclarationMap == Tristate::True &&
	    !options.GetEmitDeclarations()) {
		createDiagnosticForOptionName(
		    
		        Option_0_cannot_be_specified_without_specifying_option_1_or_option_2,
		    "declarationMap", "declaration", {"composite"});
	}

	if (!options.Lib.empty() && options.NoLib == Tristate::True) {
		createDiagnosticForOptionName(
		    Option_0_cannot_be_specified_with_option_1, "lib",
		    "noLib");
	}

	if (options.IsolatedModules == Tristate::True ||
	    options.VerbatimModuleSyntax == Tristate::True) {
		if (options.PreserveConstEnums == Tristate::False) {
			createDiagnosticForOptionName(
			    
			        Option_preserveConstEnums_cannot_be_disabled_when_0_is_enabled,
			    options.VerbatimModuleSyntax == Tristate::True
			        ? "verbatimModuleSyntax"
			        : "isolatedModules",
			    "preserveConstEnums");
		}
	}

	if (!options.OutDir.empty() || !options.RootDir.empty() ||
	    !options.SourceRoot.empty() || !options.MapRoot.empty() ||
	    (options.GetEmitDeclarations() &&
	     !options.DeclarationDir.empty())) {
		std::string dir = CommonSourceDirectory();
		bool anyRooted = false;
		for (auto* f : files) {
			if (tspath::getRootLength(f->FileName()) > 1) {
				anyRooted = true;
				break;
			}
		}
		if (!options.OutDir.empty() && dir.empty() && anyRooted) {
			createDiagnosticForOptionName(
			    
			        Cannot_find_the_common_subdirectory_path_for_the_input_files,
			    "outDir", "");
		}
	}

	// The common-source-dir-vs-rootDir check requires emit helpers
	// (outputpaths.GetComputedCommonSourceDirectory over emitted files);
	// unreachable when NoEmit is true, which it always is here.
	if (options.NoEmit != Tristate::True &&
	    options.Composite != Tristate::True && options.RootDir.empty() &&
	    !options.ConfigFilePath.empty() &&
	    (!options.OutDir.empty() ||
	     (options.GetEmitDeclarations() &&
	      !options.DeclarationDir.empty()) ||
	     !options.OutFile.empty())) {
		std::string dir = CommonSourceDirectory();
		std::vector<std::string> emittedFiles;
		for (auto* file : files) {
			if (!file->IsDeclarationFile &&
			    sourceFileMayBeEmitted(file, this, false, false)) {
				emittedFiles.push_back(file->FileName());
			}
		}
		std::string dir59 = outputpaths::GetComputedCommonSourceDirectory(
		    emittedFiles, GetCurrentDirectory(),
		    UseCaseSensitiveFileNames());
		if (!dir59.empty() &&
		    tspath::getCanonicalFileName(dir,
		                                 UseCaseSensitiveFileNames()) !=
		        tspath::getCanonicalFileName(dir59,
		                                     UseCaseSensitiveFileNames())) {
			std::string option1;
			if (!options.OutFile.empty()) {
				option1 = "outFile";
			} else if (!options.OutDir.empty()) {
				option1 = "outDir";
			} else {
				option1 = "declarationDir";
			}
			std::string option2;
			if (options.OutFile.empty() && !options.OutDir.empty()) {
				option2 = "declarationDir";
			}
			Diagnostic* diag = createDiagnosticForOption(
			    true, option1, option2,
			    
			        The_common_source_directory_of_0_is_1_The_rootDir_setting_must_be_explicitly_set_to_this_or_another_path_to_adjust_your_output_s_file_layout,
			    {std::string(tspath::getBaseFileName(
			         options.ConfigFilePath)),
			     tspath::getRelativePathFromFile(
			         options.ConfigFilePath, dir59,
			         comparePathsOptions())});
			diag->AddMessageChain(tsoptions::newCompilerDiagnostic(
			    
			        Visit_https_Colon_Slash_Slashaka_ms_Slashts6_for_migration_information));
		}
	}

	if (options.CheckJs == Tristate::True && !options.GetAllowJS()) {
		createDiagnosticForOptionName(
		    Option_0_cannot_be_specified_with_option_1,
		    "checkJs", "allowJs");
	}

	if (options.EmitDeclarationOnly == Tristate::True) {
		if (!options.GetEmitDeclarations()) {
			createDiagnosticForOptionName(
			    
			        Option_0_cannot_be_specified_without_specifying_option_1_or_option_2,
			    "emitDeclarationOnly", "declaration", {"composite"});
		}
	}

	if (options.EmitDecoratorMetadata == Tristate::True &&
	    options.ExperimentalDecorators != Tristate::True) {
		createDiagnosticForOptionName(
		    Option_0_cannot_be_specified_with_option_1,
		    "emitDecoratorMetadata", "experimentalDecorators");
	}

	if (!options.JsxFactory.empty()) {
		if (!options.ReactNamespace.empty()) {
			createDiagnosticForOptionName(
			    Option_0_cannot_be_specified_with_option_1,
			    "reactNamespace", "jsxFactory");
		}
		if (options.Jsx == JsxEmit::ReactJSX ||
		    options.Jsx == JsxEmit::ReactJSXDev) {
			createDiagnosticForOptionName(
			    Option_0_cannot_be_specified_when_option_jsx_is_1,
			    "jsxFactory", {jsxEmitString(options.Jsx)});
		}
		if (parseIsolatedEntityName(options.JsxFactory) ==
		    nullptr) {
			createOptionValueDiagnostic(
			    "jsxFactory",
			    
			        Invalid_value_for_jsxFactory_0_is_not_a_valid_identifier_or_qualified_name,
			    {options.JsxFactory});
		}
	} else if (!options.ReactNamespace.empty() &&
	           !isIdentifierText(options.ReactNamespace, LanguageVariant::Standard)) {
		createOptionValueDiagnostic(
		    "reactNamespace",
		    
		        Invalid_value_for_reactNamespace_0_is_not_a_valid_identifier,
		    {options.ReactNamespace});
	}

	if (!options.JsxFragmentFactory.empty()) {
		if (options.JsxFactory.empty()) {
			createDiagnosticForOptionName(
			    
			        Option_0_cannot_be_specified_without_specifying_option_1,
			    "jsxFragmentFactory", "jsxFactory");
		}
		if (options.Jsx == JsxEmit::ReactJSX ||
		    options.Jsx == JsxEmit::ReactJSXDev) {
			createDiagnosticForOptionName(
			    Option_0_cannot_be_specified_when_option_jsx_is_1,
			    "jsxFragmentFactory", {jsxEmitString(options.Jsx)});
		}
		if (parseIsolatedEntityName(options.JsxFragmentFactory) ==
		    nullptr) {
			createOptionValueDiagnostic(
			    "jsxFragmentFactory",
			    
			        Invalid_value_for_jsxFragmentFactory_0_is_not_a_valid_identifier_or_qualified_name,
			    {options.JsxFragmentFactory});
		}
	}

	if (!options.ReactNamespace.empty()) {
		if (options.Jsx == JsxEmit::ReactJSX ||
		    options.Jsx == JsxEmit::ReactJSXDev) {
			createDiagnosticForOptionName(
			    Option_0_cannot_be_specified_when_option_jsx_is_1,
			    "reactNamespace", {jsxEmitString(options.Jsx)});
		}
	}

	if (!options.JsxImportSource.empty()) {
		if (options.Jsx == JsxEmit::React) {
			createDiagnosticForOptionName(
			    Option_0_cannot_be_specified_when_option_jsx_is_1,
			    "jsxImportSource", {jsxEmitString(options.Jsx)});
		}
	}

	ModuleKind moduleKind = options.GetEmitModuleKind();

	if (options.AllowImportingTsExtensions == Tristate::True &&
	    !(options.NoEmit == Tristate::True ||
	      options.EmitDeclarationOnly == Tristate::True ||
	      options.RewriteRelativeImportExtensions == Tristate::True)) {
		createOptionValueDiagnostic(
		    "allowImportingTsExtensions",
		    
		        Option_allowImportingTsExtensions_can_only_be_used_when_one_of_noEmit_emitDeclarationOnly_or_rewriteRelativeImportExtensions_is_set);
	}

	ModuleResolutionKind moduleResolution =
	    options.GetModuleResolutionKind();
	if (options.ResolvePackageJsonExports == Tristate::True &&
	    !moduleResolutionSupportsPackageJsonExportsAndImports(
	        moduleResolution)) {
		createDiagnosticForOptionName(
		    
		        Option_0_can_only_be_used_when_moduleResolution_is_set_to_node16_nodenext_or_bundler,
		    "resolvePackageJsonExports", "");
	}
	if (options.ResolvePackageJsonImports == Tristate::True &&
	    !moduleResolutionSupportsPackageJsonExportsAndImports(
	        moduleResolution)) {
		createDiagnosticForOptionName(
		    
		        Option_0_can_only_be_used_when_moduleResolution_is_set_to_node16_nodenext_or_bundler,
		    "resolvePackageJsonImports", "");
	}
	if (!options.CustomConditions.empty() &&
	    !moduleResolutionSupportsPackageJsonExportsAndImports(
	        moduleResolution)) {
		createDiagnosticForOptionName(
		    
		        Option_0_can_only_be_used_when_moduleResolution_is_set_to_node16_nodenext_or_bundler,
		    "customConditions", "");
	}

	if (moduleResolution == ModuleResolutionKind::Bundler &&
	    !emitModuleKindIsNonNodeESM(moduleKind) &&
	    moduleKind != ModuleKind::Preserve &&
	    moduleKind != ModuleKind::CommonJS) {
		createOptionValueDiagnostic(
		    "moduleResolution",
		    
		        Option_0_can_only_be_used_when_module_is_set_to_preserve_commonjs_or_es2015_or_later,
		    {"bundler"});
	}

	if (ModuleKind::Node16 <= moduleKind &&
	    moduleKind <= ModuleKind::NodeNext &&
	    !(ModuleResolutionKind::Node16 <= moduleResolution &&
	      moduleResolution <= ModuleResolutionKind::NodeNext)) {
		std::string moduleKindName = moduleKindString(moduleKind);
		std::string moduleResolutionName;
		// core.ModuleKindToModuleResolutionKind map
		if (moduleKind == ModuleKind::Node16) {
			moduleResolutionName =
			    moduleResolutionKindString(ModuleResolutionKind::Node16);
		} else if (moduleKind == ModuleKind::NodeNext) {
			moduleResolutionName = moduleResolutionKindString(
			    ModuleResolutionKind::NodeNext);
		} else {
			moduleResolutionName = "Node16";
		}
		createOptionValueDiagnostic(
		    "moduleResolution",
		    
		        Option_moduleResolution_must_be_set_to_0_or_left_unspecified_when_option_module_is_set_to_1,
		    {moduleResolutionName, moduleKindName});
	} else if (ModuleResolutionKind::Node16 <= moduleResolution &&
	           moduleResolution <= ModuleResolutionKind::NodeNext &&
	           !(ModuleKind::Node16 <= moduleKind &&
	             moduleKind <= ModuleKind::NodeNext)) {
		std::string moduleResolutionName =
		    moduleResolutionKindString(moduleResolution);
		createOptionValueDiagnostic(
		    "module",
		    
		        Option_module_must_be_set_to_0_when_option_moduleResolution_is_set_to_1,
		    {moduleResolutionName, moduleResolutionName});
	}

	// If the emit is enabled make sure that every output file is unique and
	// not overwriting any of the input files
	if (options.NoEmit != Tristate::True &&
	    options.SuppressOutputPathCheck != Tristate::True) {
		std::unordered_set<std::string> emitFilesSeen;

		// Verify that all the emit files are unique and don't overwrite
		// input files
		auto verifyEmitFilePath = [&](const std::string& emitFileName) {
			if (!emitFileName.empty()) {
				tspath::Path emitFilePath = toPath(emitFileName);
				// Report error if the output overwrites input file
				if (filesByPath.count(emitFilePath)) {
					Diagnostic* diag = tsoptions::newCompilerDiagnostic(
					    Cannot_write_file_0_because_it_would_overwrite_input_file,
					    {emitFileName});
					if (options.ConfigFilePath.empty()) {
						// The program is from either an inferred project or
						// an external project
						diag->AddMessageChain(tsoptions::newCompilerDiagnostic(
						    Adding_a_tsconfig_json_file_will_help_organize_projects_that_contain_both_TypeScript_and_JavaScript_files_Learn_more_at_https_Colon_Slash_Slashaka_ms_Slashtsconfig));
					}
					blockEmittingOfFile(emitFileName, diag);
				}

				std::string emitFileKey;
				if (!UseCaseSensitiveFileNames()) {
					emitFileKey = tspath::toFileNameLowerCase(
					    std::string(emitFilePath));
				} else {
					emitFileKey = std::string(emitFilePath);
				}

				// Report error if multiple files write into same file
				if (emitFilesSeen.count(emitFileKey)) {
					// Already seen the same emit file - report error
					blockEmittingOfFile(
					    emitFileName,
					    tsoptions::newCompilerDiagnostic(
					        Cannot_write_file_0_because_it_would_be_overwritten_by_multiple_input_files,
					        {emitFileName}));
				} else {
					emitFilesSeen.insert(emitFileKey);
				}
			}
		};

		outputpaths::ForEachEmittedFile(
		    this, &options,
		    [&](outputpaths::OutputPaths* emitFileNames,
		        SourceFile* /*sourceFile*/) {
			    verifyEmitFilePath(emitFileNames->jsFilePath);
			    verifyEmitFilePath(emitFileNames->sourceMapFilePath);
			    verifyEmitFilePath(emitFileNames->declarationFilePath);
			    verifyEmitFilePath(emitFileNames->declarationMapPath);
			    return false;
		    },
		    getSourceFilesToEmit(nullptr, false, false), false);
		verifyEmitFilePath(
		    outputpaths::GetBuildInfoFileName(&options,
		                                      comparePathsOptions()));
	}
}

// program.go:1394 verifyProjectReferences — walk the resolved
// references reporting missing configs, missing composite/noEmit
// settings and tsbuildinfo collisions.
void SimpleProgram::verifyProjectReferences() {
	std::string buildInfoFileName =
	    opts_.Config != nullptr &&
	            !tristateIsTrue(Options()->SuppressOutputPathCheck)
	        ? opts_.Config->GetBuildInfoFileName()
	        : std::string{};
	auto createDiagnosticForReference =
	    [&](tsoptions::ParsedCommandLine* config, int index,
	        const DiagnosticMessage* message,
	        std::vector<std::string> args) {
		    Diagnostic* diag = tsoptions::CreateDiagnosticAtReferenceSyntax(
		        config, index, message, args);
		    if (diag == nullptr) {
			    diag = tsoptions::newCompilerDiagnostic(message, args);
		    }
		    programDiagnostics.push_back(diag);
	    };

	RangeResolvedProjectReference(
	    [&](tspath::Path path, tsoptions::ParsedCommandLine* config,
	        tsoptions::ParsedCommandLine* parent, int index) -> bool {
		    ProjectReference* ref = parent->ProjectReferences()[index];
		    // !!! Deprecated in 5.0 and removed since 5.5
		    // verifyRemovedProjectReference(ref, parent, index);
		    if (config == nullptr) {
			    createDiagnosticForReference(
			        parent, index, File_0_not_found,
			        {std::string(ref->Path)});
			    return true;
		    }
		    const CompilerOptions* refOptions = config->CompilerOptions();
		    if (!tristateIsTrue(refOptions->Composite) ||
		        tristateIsTrue(refOptions->NoEmit)) {
			    if (!parent->FileNames().empty()) {
				    if (!tristateIsTrue(refOptions->Composite)) {
					    createDiagnosticForReference(
					        parent, index,
					        Referenced_project_0_must_have_setting_composite_Colon_true,
					        {std::string(ref->Path)});
				    }
				    if (tristateIsTrue(refOptions->NoEmit)) {
					    createDiagnosticForReference(
					        parent, index,
					        Referenced_project_0_may_not_disable_emit,
					        {std::string(ref->Path)});
				    }
			    }
		    }
		    if (!buildInfoFileName.empty() &&
		        buildInfoFileName == config->GetBuildInfoFileName()) {
			    createDiagnosticForReference(
			        parent, index,
			        Cannot_write_file_0_because_it_will_overwrite_tsbuildinfo_file_generated_by_referenced_project_1,
			        {buildInfoFileName, std::string(ref->Path)});
			    hasEmitBlockingDiagnostics->insert(toPath(buildInfoFileName));
		    }
		    return true;
	    });
}

// program.go:2010 GetDiagnosticsOfAnyProgram — generalized to ProgramLike
// so the incremental Program can drive it (execute/incremental slice).
std::vector<Diagnostic*> getDiagnosticsOfAnyProgram(
    ProgramLike* program, const std::vector<SourceFile*>& files,
    bool skipNoEmitCheckForDtsDiagnostics,
    std::function<std::vector<Diagnostic*>(SourceFile*)>
        getBindDiagnostics,
    std::function<std::vector<Diagnostic*>(SourceFile*)>
        getSemanticDiagnostics) {
	if (getBindDiagnostics == nullptr) {
		getBindDiagnostics = [program](SourceFile* f) {
			return program->GetBindDiagnostics(f);
		};
	}
	if (getSemanticDiagnostics == nullptr) {
		getSemanticDiagnostics = [program](SourceFile* f) {
			return program->GetSemanticDiagnostics(f);
		};
	}
	std::vector<Diagnostic*> allDiagnostics =
	    program->GetConfigFileParsingDiagnostics();
	size_t configFileParsingDiagnosticsLength = allDiagnostics.size();

	auto appendDiagnosticsForAllFiles =
	    [&](std::vector<Diagnostic*> diagnostics,
	        const std::function<std::vector<Diagnostic*>(SourceFile*)>&
	            getDiagnostics) {
		    if (files.empty()) {
			    auto d = getDiagnostics(nullptr);
			    diagnostics.insert(diagnostics.end(), d.begin(),
			                       d.end());
			    return diagnostics;
		    }
		    for (auto* file : files) {
			    auto d = getDiagnostics(file);
			    diagnostics.insert(diagnostics.end(), d.begin(),
			                       d.end());
		    }
		    return diagnostics;
	    };

	auto syntacticDiagnostics = appendDiagnosticsForAllFiles(
	    {}, [&](SourceFile* f) {
		    return program->GetSyntacticDiagnostics(f);
	    });
	// (no contentMapperDiagnostics — no mappers)
	allDiagnostics.insert(allDiagnostics.end(),
	                      syntacticDiagnostics.begin(),
	                      syntacticDiagnostics.end());

	if (allDiagnostics.size() == configFileParsingDiagnosticsLength) {
		auto progDiags = program->GetProgramDiagnostics();
		allDiagnostics.insert(allDiagnostics.end(), progDiags.begin(),
		                      progDiags.end());

		// Do binding early so we can track the time.
		appendDiagnosticsForAllFiles({}, getBindDiagnostics);

		if (program->Options()->ListFilesOnly != Tristate::True) {
			auto globals = program->GetGlobalDiagnostics();
			allDiagnostics.insert(allDiagnostics.end(), globals.begin(),
			                      globals.end());

			if (allDiagnostics.size() ==
			    configFileParsingDiagnosticsLength) {
				allDiagnostics = appendDiagnosticsForAllFiles(
				    allDiagnostics, getSemanticDiagnostics);
				// Late program check to get global diagnostics — Go gates
				// this on `program.(*Program)`, i.e. only for the
				// concrete (non-incremental) program.
				if (auto* concreteProgram =
				        dynamic_cast<SimpleProgram*>(program);
				    concreteProgram != nullptr) {
					auto globals2 = concreteProgram->GetGlobalDiagnostics();
					allDiagnostics.insert(allDiagnostics.end(),
					                      globals2.begin(),
					                      globals2.end());
				}
			}

			if ((skipNoEmitCheckForDtsDiagnostics ||
			     program->Options()->NoEmit == Tristate::True) &&
			    program->Options()->GetEmitDeclarations() &&
			    allDiagnostics.size() ==
			        configFileParsingDiagnosticsLength) {
				allDiagnostics = appendDiagnosticsForAllFiles(
				    allDiagnostics, [&](SourceFile* f) {
					    return program->GetDeclarationDiagnostics(f);
				    });
			}
		}
	}
	return allDiagnostics;
}

}  // namespace tsc::compiler

namespace tsc::compiler {

// program.go GetResolvedModules — flattened for the checker's slim view.
std::vector<checker::ResolvedModule> SimpleProgram::GetResolvedModules() {
	std::vector<checker::ResolvedModule> result;
	for (auto& [path, cache] : resolvedModules) {
		for (auto& [key, rm] : cache) {
			if (rm != nullptr) {
				checker::ResolvedModule out;
				out.resolved = rm->IsResolved();
				out.resolvedFileName = rm->ResolvedFileName;
				out.resolvedUsingTsExtension = rm->ResolvedUsingTsExtension;
				out.isExternalLibraryImport = rm->IsExternalLibraryImport;
				out.extension = rm->Extension;
				out.alternateResult = rm->AlternateResult;
				out.packageId = checker::PackageId{rm->PackageId.Name};
				out.resolvedUsingExtraExtensions =
				    rm->ResolvedUsingExtraExtensions;
				result.push_back(std::move(out));
			}
		}
	}
	return result;
}

// program.go GetPackagesMap — lazily-cached package name → bundles types;
// sync.Once like Go's packagesMapOnce (emit tasks race to populate it).
const std::unordered_map<std::string, bool>&
SimpleProgram::GetPackagesMap() {
	packagesMapOnce_.run([this] {
		packagesMap.emplace();
		for (auto& [path, resolvedModulesInFile] : resolvedModules) {
			for (auto& [key, mod] : resolvedModulesInFile) {
				if (mod != nullptr && !mod->PackageId.Name.empty()) {
					(*packagesMap)[mod->PackageId.Name] =
					    (*packagesMap)[mod->PackageId.Name] ||
					    mod->Extension == tspath::extensionDts;
				}
			}
		}
	});
	return *packagesMap;
}

// === slice: incremental ===

// program.go:816 GetSuggestionDiagnostics.
std::vector<Diagnostic*> SimpleProgram::GetSuggestionDiagnostics(
    SourceFile* sourceFile) {
	return collectCheckerDiagnostics(
	    sourceFile,
	    [this](checker::Checker* c, SourceFile* file)
	        -> std::vector<Diagnostic*> {
		    return getSuggestionDiagnosticsWithChecker(c, file);
	    });
}

// program.go:1634 getSuggestionDiagnosticsWithChecker.
std::vector<Diagnostic*>
SimpleProgram::getSuggestionDiagnosticsWithChecker(
    checker::Checker* fileChecker, SourceFile* sourceFile) {
	if (SkipTypeChecking(sourceFile, false)) {
		return {};
	}
	return fileChecker->GetSuggestionDiagnostics(sourceFile);
}

// program.go:616 GetTypeCheckerForFileExclusive — the file's checker,
// locked for the caller; `done` releases it. A null file takes the
// non-exclusive first checker (Go's GetChecker(ctx, nil) shape — the
// port keeps the previous shared-checker semantics for nil files).
std::pair<checker::Checker*, std::function<void()>>
SimpleProgram::GetTypeCheckerForFileExclusive(SourceFile* file) {
	if (compilerCheckerPool_ != nullptr) {
		if (file != nullptr) {
			return compilerCheckerPool_->getCheckerForFileExclusive(
			    gostd::Context{}, file);
		}
		return compilerCheckerPool_->getCheckerNonExclusive();
	}
	if (file != nullptr) {
		return checkerPool_->GetChecker(gostd::Context{}, file);
	}
	return {getChecker(), []() {}};
}

// program.go:607 GetTypeCheckerForFile — non-exclusive checkout of the
// file's checker on the built-in pool; external pools go through the
// CheckerPool interface (exclusive checkout + release).
std::pair<checker::Checker*, std::function<void()>>
SimpleProgram::getTypeCheckerForFileNonExclusive(SourceFile* file) {
	if (compilerCheckerPool_ != nullptr) {
		if (file != nullptr) {
			return compilerCheckerPool_->getCheckerForFileNonExclusive(file);
		}
		return compilerCheckerPool_->getCheckerNonExclusive();
	}
	if (file != nullptr) {
		return checkerPool_->GetChecker(gostd::Context{}, file);
	}
	return {getChecker(), []() {}};
}

// program.go:166 PackageJsonCacheEntries — delegates to the resolver's
// package-json scope cache.
void SimpleProgram::PackageJsonCacheEntries(
    const std::function<bool(
        tspath::Path, const std::shared_ptr<packagejson::InfoCacheEntry>&)>&
        f) {
	resolver_->PackageJsonCacheEntries(f);
}

// program.go:804 GetSemanticDiagnosticsForIncremental — includes newly
// discovered globals in each file's cached diagnostics and leaves
// noEmit filtering to the builder.
std::vector<std::pair<SourceFile*, std::vector<Diagnostic*>>>
SimpleProgram::GetSemanticDiagnosticsForIncremental(
    const std::vector<SourceFile*>& sourceFiles) {
	auto allDiags = collectCheckerDiagnosticsFromFiles(
	    sourceFiles,
	    [this](checker::Checker* c, SourceFile* file)
	        -> std::vector<Diagnostic*> {
		    return getBindAndCheckDiagnosticsWithChecker(
		        c, file, true /*includeDeferredGlobals*/);
	    });
	std::vector<std::pair<SourceFile*, std::vector<Diagnostic*>>> result;
	result.reserve(sourceFiles.size());
	for (int i = 0; i < static_cast<int>(sourceFiles.size()); i++) {
		result.emplace_back(
		    sourceFiles[i],
		    filterAndSortDiagnostics(std::move(allDiags[i])));
	}
	return result;
}

// === end slice: incremental ===

// === slice: execute-tsc ===

// program.go:285 NewProgram — PhaseProgram "createProgram" trace span, then
// the SimpleProgram ctor (which does the processAllProgramFiles-equivalent).
SimpleProgram* NewProgram(const ProgramOptions& opts) {
	std::function<void()> tracePop;
	if (opts.Tracing != nullptr) {
		// Go: defer Tracing.Push(...)() — the pop runs when NewProgram
		// returns, so the span covers the createProgram body.
		tracePop = opts.Tracing->Push(
		    tracing::PhaseProgram, "createProgram",
		    tracing::TraceArgs{
		        {"configFilePath",
		         opts.Config->ParsedConfig->CompilerOptions->ConfigFilePath}},
		    true);
	}
	// program.go: opts.Config — pass the caller's ParsedCommandLine through
	// to the program (borrowed); the impl ctor stores it in commandLine_ so
	// loader-time ContentMapperExtensions() sees the real config.
	{
		auto& o = *opts.Config->ParsedConfig->CompilerOptions;
	}
	auto* p = new SimpleProgram(opts.Host,
	                            *opts.Config->ParsedConfig->CompilerOptions,
	                            opts.Config->ParsedConfig->FileNames,
	                            opts.Config, opts.SkipModuleResolution,
	                            opts.Tracing);
	p->opts_ = opts;
	// === slice: project ===
	// program.go:299 NewProgram -> initCheckerPool: the pool is created
	// by opts.CreateCheckerPool when non-nil; when nil Go builds its own
	// checkerPool (checkerpool.go).
	p->initCheckerPool();
	// === end slice: project ===
	if (tracePop) {
		tracePop();
	}
	return p;
}

// program.go:2120 ExplainFiles.
void SimpleProgram::ExplainFiles(std::ostream& w,
                                 const locale::Locale& locale) {
	auto toRelativeFileName = [this](const std::string& fileName) {
		return tspath::getRelativePathFromDirectory(
		    GetCurrentDirectory(), fileName, comparePathsOptions());
	};
	auto localizeDiag = [&](Diagnostic* d) {
		// ast.Diagnostic.Localize — diagnostic.go:117.
		if (d->message == nullptr && !d->messageText.empty()) {
			return d->messageText;
		}
		return ::tsc::localize(locale, d->message,
		                     std::string(d->messageKey), d->messageArgs);
	};
	int filesExplained = 0;
	std::vector<SourceFile*> files = GetSourceFiles();
	int sourceFileIndex = 0;
	// explainFile — ast.HasFileName reduced to (FileName, Path) so
	// redirectsFile can share it.
	auto explainFile = [&](const std::string& fileName,
	                       const tspath::Path& path) {
		w << toRelativeFileName(fileName) << '\n';
		auto it = includeProcessor_.fileIncludeReasons.find(path);
		if (it != includeProcessor_.fileIncludeReasons.end()) {
			for (auto* reason : it->second) {
				auto* diag = reason->toDiagnostic(this, true);
				w << "   " << localizeDiag(diag) << '\n';
			}
		}
		for (auto* diag : includeProcessor_.explainRedirectAndImpliedFormat(
		         this, path,
		         [&](std::string_view name) {
			         return toRelativeFileName(std::string(name));
		         })) {
			w << "   " << localizeDiag(diag) << '\n';
		}
		filesExplained++;
	};

	auto explainSourceFiles = [&](int endIndex) {
		for (; filesExplained < endIndex;) {
			explainFile(files[sourceFileIndex]->FileName(),
			            files[sourceFileIndex]->Path());
			sourceFileIndex++;
		}
	};

	std::vector<redirectsFile> redirectFiles;
	redirectFiles.reserve(redirectFilesByPath.size());
	for (auto& [path, rf] : redirectFilesByPath) {
		redirectFiles.push_back(rf);
	}
	std::sort(redirectFiles.begin(), redirectFiles.end(),
	          [](const redirectsFile& a, const redirectsFile& b) {
		          return a.index < b.index;
	          });

	for (auto& redirectFile : redirectFiles) {
		// Explain all sourceFiles till we reach this redirectFile index
		explainSourceFiles(redirectFile.index);
		explainFile(redirectFile.fileName, redirectFile.path);
	}

	// Explain any remaining sourceFiles
	explainSourceFiles(static_cast<int>(files.size() + redirectFiles.size()));
}

// program.go:495-509 — the canReplaceFileInProgram equality helpers.
namespace {

// equalModuleSpecifiers — program.go:495.
bool equalModuleSpecifiers(Node* n1, Node* n2) {
	return n1->kind == n2->kind &&
	       (!isStringLiteral(n1) || n1->text() == n2->text());
}

// equalModuleAugmentationNames — program.go:499.
bool equalModuleAugmentationNames(Node* n1, Node* n2) {
	return n1->kind == n2->kind && n1->text() == n2->text();
}

// equalFileReferences — program.go:503.
bool equalFileReferences(FileReference* f1, FileReference* f2) {
	return f1->FileName == f2->FileName &&
	       f1->ResolutionMode == f2->ResolutionMode &&
	       f1->Preserve == f2->Preserve;
}

// equalCheckJSDirectives — program.go:507.
bool equalCheckJSDirectives(CheckJsDirective* d1, CheckJsDirective* d2) {
	return (d1 == nullptr && d2 == nullptr) ||
	       (d1 != nullptr && d2 != nullptr && d1->Enabled == d2->Enabled);
}

}  // namespace

// program.go:454 canReplaceFileInProgram.
bool SimpleProgram::canReplaceFileInProgram(SourceFile* file1,
                                            SourceFile* file2) {
	return file2 != nullptr &&
	       file1->ParseOptions() == file2->ParseOptions() &&
	       file1->ScriptKind == file2->ScriptKind &&
	       isExternalOrCommonJSModule(file1) ==
	           isExternalOrCommonJSModule(file2) &&
	       file1->UsesUriStyleNodeCoreModules ==
	           file2->UsesUriStyleNodeCoreModules &&
	       file1->imports.size() == file2->imports.size() &&
	       [&] {
		       for (size_t i = 0; i < file1->imports.size(); i++) {
			       auto* n1 = file1->imports[i];
			       auto* n2 = file2->imports[i];
			       if (!equalModuleSpecifiers(n1, n2) ||
			           GetModeForUsageLocation(file1, n1) !=
			               GetModeForUsageLocation(file2, n2)) {
				       return false;
			       }
		       }
		       return true;
	       }() &&
	       std::equal(file1->ModuleAugmentations.begin(),
	                  file1->ModuleAugmentations.end(),
	                  file2->ModuleAugmentations.begin(),
	                  file2->ModuleAugmentations.end(),
	                  equalModuleAugmentationNames) &&
	       file1->AmbientModuleNames == file2->AmbientModuleNames &&
	       std::equal(file1->ReferencedFiles.begin(),
	                  file1->ReferencedFiles.end(),
	                  file2->ReferencedFiles.begin(),
	                  file2->ReferencedFiles.end(),
	                  equalFileReferences) &&
	       std::equal(file1->TypeReferenceDirectives.begin(),
	                  file1->TypeReferenceDirectives.end(),
	                  file2->TypeReferenceDirectives.begin(),
	                  file2->TypeReferenceDirectives.end(),
	                  equalFileReferences) &&
	       std::equal(file1->LibReferenceDirectives.begin(),
	                  file1->LibReferenceDirectives.end(),
	                  file2->LibReferenceDirectives.begin(),
	                  file2->LibReferenceDirectives.end(),
	                  equalFileReferences) &&
	       equalCheckJSDirectives(file1->CheckJsDirective,
	                            file2->CheckJsDirective);
}

// program.go:472 needsImportHelpersImportSpecifier.
bool SimpleProgram::needsImportHelpersImportSpecifier(SourceFile* file) {
	auto [redirect, _] =
	    projectReferenceFileMapper_->getRedirectForResolution(
	        HasFileName{file->FileName(), file->Path()});
	const CompilerOptions* optionsForFile =
	    module::GetCompilerOptionsWithRedirect(
	        opts_.Config->CompilerOptions(), redirect);
	if (!tristateIsTrue(optionsForFile->ImportHelpers)) {
		return false;
	}
	bool isJavaScriptFile = isSourceFileJS(file);
	bool isExternalModuleFile = isExternalModule(file);
	if (!isJavaScriptFile &&
	    (file->IsDeclarationFile ||
	     (!optionsForFile->GetIsolatedModules() &&
	      !isExternalModuleFile))) {
		return false;
	}
	return true;
}

// program.go:486 jsxRuntimeImportSpecifier (member renamed
// jsxRuntimeImportSpecifierForFile — the field's map has the same name).
std::string SimpleProgram::jsxRuntimeImportSpecifierForFile(
    SourceFile* file) {
	if (!isSourceFileJS(file) && file->ScriptKind != ScriptKind::TSX) {
		return "";
	}
	auto [redirect, _] =
	    projectReferenceFileMapper_->getRedirectForResolution(
	        HasFileName{file->FileName(), file->Path()});
	const CompilerOptions* optionsForFile =
	    module::GetCompilerOptionsWithRedirect(
	        opts_.Config->CompilerOptions(), redirect);
	return getJSXRuntimeImport(
	    getJSXImplicitImportBase(optionsForFile, file), optionsForFile);
}

// program.go:332 ReuseProgram — the UpdateProgram single-file fast path.
// The &Program{...} literal (program.go:405-413) is an in-place field
// splice: the reuse ctor leaves all state zero, then the processedFiles
// fields + the named program-level fields copy over (Go's map fields are
// shared references — ours are value copies / shared_ptr where sharing
// is semantic).
std::tuple<SimpleProgram*, SourceFile*, bool> SimpleProgram::ReuseProgram(
    const tspath::Path& changedFilePath, CompilerHost* newHost,
    const std::function<CheckerPool*(SimpleProgram*)>& createCheckerPool,
    const std::function<module::Resolver*(const module::ResolverOptions&)>&
        createModuleResolver) {
	ProgramOptions newOpts = opts_;
	newOpts.Host = newHost;
	if (createCheckerPool) {
		newOpts.CreateCheckerPool = createCheckerPool;
	}
	if (createModuleResolver) {
		newOpts.CreateModuleResolver = createModuleResolver;
	}
	SourceFile* oldFile = filesByPath[changedFilePath];
	SourceFile* newFile = nullptr;
	std::vector<SourceFile*> oldSupplementalFiles;
	std::vector<SourceFile*> newSupplementalFiles;
	if (!oldFile->ContentMapper().empty()) {
		// Content-mapped files are produced by running an external
		// transform, which a plain reparse can't reproduce. Re-run the
		// transform through the host; any failure (or a missing file)
		// falls back to a full rebuild so the file loader's failure
		// policy runs.
		auto* mapper = newOpts.Config->GetContentMapperForFileName(
		    oldFile->FileName());
		auto [files, err] = newHost->GetContentMappedSourceFiles(
		    oldFile->ParseOptions(), mapper);
		newFile = files.Canonical;
		if (err) {
			return {nullptr, nullptr, false};
		}
		oldSupplementalFiles =
		    oldFile->SupplementalSourceFiles() != nullptr
		        ? *oldFile->SupplementalSourceFiles()
		        : std::vector<SourceFile*>{};
		newSupplementalFiles = files.Supplemental;
	} else {
		newFile = newHost->GetSourceFile(oldFile->ParseOptions());
	}

	// If this file is part of a package redirect group (same package
	// installed in multiple node_modules locations), we need to rebuild
	// the program because the redirect targets might need
	// recalculation.
	if (redirectFilesByPath.find(changedFilePath) !=
	        redirectFilesByPath.end() ||
	    redirectTargetsMap.find(changedFilePath) !=
	        redirectTargetsMap.end()) {
		return {nullptr, newFile, false};
	}

	if (!canReplaceFileInProgram(oldFile, newFile)) {
		return {nullptr, newFile, false};
	}
	// Cloning does not recompute synthetic helper or JSX-runtime import
	// bookkeeping. Fall back to a full build whenever either version
	// requires those imports.
	if (importHelpersImportSpecifiers[oldFile->Path()] != nullptr ||
	    needsImportHelpersImportSpecifier(newFile)) {
		return {nullptr, newFile, false};
	}
	if (jsxRuntimeImportSpecifiers.find(oldFile->Path()) !=
	        jsxRuntimeImportSpecifiers.end() ||
	    !jsxRuntimeImportSpecifierForFile(newFile).empty()) {
		return {nullptr, newFile, false};
	}
	if (oldSupplementalFiles.size() != newSupplementalFiles.size()) {
		return {nullptr, newFile, false};
	}
	for (size_t i = 0; i < oldSupplementalFiles.size(); i++) {
		auto* oldSupplemental = oldSupplementalFiles[i];
		auto* newSupplemental = newSupplementalFiles[i];
		if (oldSupplemental->Path() != newSupplemental->Path() ||
		    !canReplaceFileInProgram(oldSupplemental, newSupplemental)) {
			return {nullptr, newFile, false};
		}
		if (importHelpersImportSpecifiers[oldSupplemental->Path()] !=
		        nullptr ||
		    needsImportHelpersImportSpecifier(newSupplemental)) {
			return {nullptr, newFile, false};
		}
		if (jsxRuntimeImportSpecifiers.find(oldSupplemental->Path()) !=
		        jsxRuntimeImportSpecifiers.end() ||
		    !jsxRuntimeImportSpecifierForFile(newSupplemental)
		         .empty()) {
			return {nullptr, newFile, false};
		}
	}
	// TODO: reverify compiler options when config has changed?
	// program.go:405 &Program{...} — the reuse splice.
	auto* result = new SimpleProgram();
	result->opts_ = newOpts;
	// host / options / config — Go reads these off p.opts.Host /
	// p.opts.Config.CompilerOptions(); the C++ program stores them
	// separately, so wire the new host + carry the config.
	result->host = newHost;
	result->options = options;
	result->host->compilerOptions = &result->options;
	result->commandLine_ = newOpts.Config;
	result->tr_ = tr_;
	if (commandLineOwned_ != nullptr) {
		// This program synthesized its config — the reuse re-synthesizes
		// its own so it doesn't borrow storage the source program owns.
		result->commandLineOwned_.reset(
		    tsoptions::NewParsedCommandLine(
		        &result->options, result->fileNameList, {},
		        result->comparePathsOptions()));
		result->commandLine_ = result->commandLineOwned_.get();
	}
	result->skipModuleResolution = skipModuleResolution;
	result->usesUriStyleNodeCoreModules = usesUriStyleNodeCoreModules;
	result->programDiagnostics = programDiagnostics;
	result->hasEmitBlockingDiagnostics = hasEmitBlockingDiagnostics;
	result->contentMapperOptionDiagnostics =
	    contentMapperOptionDiagnostics;
	result->configFileParsingDiagnostics = configFileParsingDiagnostics;
	// processedFiles fields (fileloader.go:113-145) — the Go literal
	// copies the struct wholesale.
	result->resolver_ = resolver_;
	result->files = files;
	result->duplicateSourceFiles = duplicateSourceFiles;
	result->filesByPath = filesByPath;
	result->projectReferenceFileMapper_ = projectReferenceFileMapper_;
	result->missingFiles = missingFiles;
	result->resolvedModules = resolvedModules;
	result->typeResolutionsInFile = typeResolutionsInFile;
	result->sourceFileMetaDatas = sourceFileMetaDatas;
	result->jsxRuntimeImportSpecifiers = jsxRuntimeImportSpecifiers;
	result->importHelpersImportSpecifiers =
	    importHelpersImportSpecifiers;
	result->libFiles = libFiles;
	result->sourceFilesFoundSearchingNodeModules =
	    sourceFilesFoundSearchingNodeModules;
	result->outputFileToProjectReferenceSource =
	    outputFileToProjectReferenceSource;
	result->redirectTargetsMap = redirectTargetsMap;
	result->redirectFilesByPath = redirectFilesByPath;
	result->contentMapperDiagnostics = contentMapperDiagnostics;
	result->moduleResolutionError_ = moduleResolutionError_;
	result->finishedProcessing = finishedProcessing;
	result->fileNameList = fileNameList;
	result->syntheticImportArena = syntheticImportArena;
	// includeProcessor — program.go:408: the processedFiles embed copies
	// the *includeProcessor pointer, so the splice shares this program's
	// reasons (the GC owns them). updateFileIncludeProcessor
	// (program.go:431) then re-keys a fresh processor on the new program
	// keeping only the maps; the arenas are shared_ptr so the pointers the
	// maps hold stay valid while either program lives.
	result->includeProcessor_.fileIncludeReasons =
	    includeProcessor_.fileIncludeReasons;
	result->includeProcessor_.processingDiagnostics =
	    includeProcessor_.processingDiagnostics;
	result->includeProcessor_.reasonArena = includeProcessor_.reasonArena;
	result->includeProcessor_.processingDiagArena =
	    includeProcessor_.processingDiagArena;
	result->includeProcessor_.diagArena = includeProcessor_.diagArena;
	result->unresolvedImports.tryReuse(&unresolvedImports);
	result->knownSymlinks.tryReuse(&knownSymlinks);
	result->packageNames_.tryReuse(&packageNames_);
	result->initCheckerPool();
	auto indexIt =
	    std::find_if(result->files.begin(), result->files.end(),
	                 [newFile](SourceFile* file) {
		                 return file->Path() == newFile->Path();
	                 });
	auto index = indexIt - result->files.begin();
	result->files[index] = newFile;
	result->filesByPath[newFile->Path()] = newFile;
	if (!oldSupplementalFiles.empty()) {
		for (size_t i = 0; i < oldSupplementalFiles.size(); i++) {
			auto* oldSupplemental = oldSupplementalFiles[i];
			auto* newSupplemental = newSupplementalFiles[i];
			auto supIt = std::find_if(
			    result->files.begin(), result->files.end(),
			    [oldSupplemental](SourceFile* file) {
				    return file == oldSupplemental;
			    });
			auto supplementalIndex = supIt - result->files.begin();
			result->files[supplementalIndex] = newSupplemental;
			result->filesByPath[newSupplemental->Path()] =
			    newSupplemental;
		}
	}
	updateFileIncludeProcessor(result);
	return {result, newFile, true};
}

// program.go:435 initCheckerPool — installs opts_.CreateCheckerPool's
// pool when set, else the built-in pool (compilerCheckerPool_ set ONLY
// for the built-in pool, like Go program.go:443-445).
void SimpleProgram::initCheckerPool() {
	if (!finishedProcessing) {
		TSC_UNREACHABLE(
		    "Program must finish processing files before initializing "
		    "checker pool");
	}

	// Go runs this once in NewProgram; the port also calls it from the
	// SimpleProgram ctor (checkFile/emitFile paths) and from
	// program.go:299-equivalent NewProgram after opts_ are installed, so
	// the second call replaces a ctor-installed built-in pool with the
	// caller's CreateCheckerPool.
	compilerCheckerPool_ = nullptr;
	checkerPool_ = nullptr;
	ownedCheckerPool_.reset();

	if (opts_.CreateCheckerPool) {
		checkerPool_ = opts_.CreateCheckerPool(this);
	} else {
		auto* pool = newCheckerPoolWithTracing(this, opts_.Tracing);
		ownedCheckerPool_.reset(pool);
		checkerPool_ = pool;
		compilerCheckerPool_ = pool;
	}
}

// program.go:542 extractUnresolvedImports — the unresolvedImports
// lazyValue's compute step (set accumulates across all files).
collections::Set<std::string>* SimpleProgram::extractUnresolvedImports() {
	auto* unresolvedSet = new collections::Set<std::string>();
	for (auto* sourceFile : files) {
		for (auto& imp :
		     extractUnresolvedImportsFromSourceFile(sourceFile)) {
			unresolvedSet->Add(imp);
		}
	}
	return unresolvedSet;
}

// program.go:555 extractUnresolvedImportsFromSourceFile — non-relative
// specifiers whose resolution is missing or landed on a non-TS
// extension are "unresolved" for auto-import purposes.
std::vector<std::string>
SimpleProgram::extractUnresolvedImportsFromSourceFile(SourceFile* file) {
	std::vector<std::string> unresolvedImports;
	auto it = resolvedModules.find(file->Path());
	if (it != resolvedModules.end()) {
		for (auto& [cacheKey, resolution] : it->second) {
			bool resolved = resolution->IsResolved();
			if ((!resolved ||
			     !tspath::extensionIsOneOf(
			         resolution->Extension,
			         tspath::supportedTSExtensionsWithJsonFlat)) &&
			    !tspath::isExternalModuleNameRelative(cacheKey.Name)) {
				unresolvedImports.push_back(cacheKey.Name);
			}
		}
	}
	return unresolvedImports;
}

// program.go:2300 GetSymlinkCache — lazyValue[symlinks.KnownSymlinks]:
// resolutions' realpath bookkeeping plus a package.json dependency probe
// (records each runtime dep's original->resolved package.json pair).
symlinks::KnownSymlinks* SimpleProgram::GetSymlinkCache() {
	return knownSymlinks.getValue([this]() -> symlinks::KnownSymlinks* {
		auto* knownSymlinks = symlinks::NewKnownSymlink(
		    GetCurrentDirectory(), UseCaseSensitiveFileNames());

		// Resolved modules store realpath information when they're
		// resolved inside node_modules
		if (!resolvedModules.empty() || !typeResolutionsInFile.empty()) {
			knownSymlinks->SetSymlinksFromResolutions(
			    [this](const std::function<void(
			               module::ResolvedModule*, std::string_view,
			               ResolutionMode, tspath::Path)>& cb,
			           SourceFile* file) {
				    ForEachResolvedModule(cb, file);
			    },
			    [this](const std::function<void(
			               module::ResolvedTypeReferenceDirective*,
			               std::string_view, ResolutionMode,
			               tspath::Path)>& cb,
			           SourceFile* file) {
				    ForEachResolvedTypeReferenceDirective(cb,
				                                          file);
			    });
		}

		// Check other dependencies for symlinks
		collections::Set<tspath::Path> seenPackageJsons;
		for (auto& [filePath, meta] : sourceFileMetaDatas) {
			if (meta.PackageJsonDirectory.empty() ||
			    !SourceFileMayBeEmitted(
			        GetSourceFileByPath(filePath), false) ||
			    !seenPackageJsons.AddIfAbsent(
			        toPath(meta.PackageJsonDirectory))) {
				continue;
			}
			std::string packageJsonName = tspath::combinePaths(
			    meta.PackageJsonDirectory, {"package.json"});
			auto info = GetPackageJsonInfo(packageJsonName);
			if (info == nullptr || info->GetContents() == nullptr) {
				continue;
			}

			// GetRuntimeDependencyNames returns a Set by value; Keys()
			// returns a reference into it, so bind it to a named local
			// before iterating (the range-init does not extend the
			// temporary's lifetime).
			auto runtimeDeps =
			    info->GetContents()->GetRuntimeDependencyNames();
			for (auto& dep : runtimeDeps.Keys()) {
				// Skip work in common case: we already saved a
				// symlink for this package directory in the
				// node_modules adjacent to this package.json
				auto possibleDirectoryPath = toPath(
				    tspath::combinePaths(meta.PackageJsonDirectory,
				                         {"node_modules", dep}));
				if (knownSymlinks->HasDirectory(
				        possibleDirectoryPath)) {
					continue;
				}
				if (dep.rfind("@types", 0) != 0) {
					auto possibleTypesDirectoryPath = toPath(
					    tspath::combinePaths(
					        meta.PackageJsonDirectory,
					        {"node_modules",
					         module::GetTypesPackageName(dep)}));
					if (knownSymlinks->HasDirectory(
					        possibleTypesDirectoryPath)) {
						continue;
					}
				}

				auto packageResolution =
				    resolver_->ResolvePackageDirectory(
				        dep, packageJsonName,
				        ResolutionModeCommonJS, nullptr);
				if (packageResolution != nullptr &&
				    packageResolution->IsResolved() &&
				    !packageResolution->OriginalPath.empty()) {
					knownSymlinks->ProcessResolution(
					    tspath::combinePaths(
					        packageResolution->OriginalPath,
					        {"package.json"}),
					    tspath::combinePaths(
					        packageResolution->ResolvedFileName,
					        {"package.json"}));
				}
			}
		}
		return knownSymlinks;
	});
}

// program.go:305 UpdateProgram — ReuseProgram fast path, else a fresh
// program built from opts with the host/factories swapped.
std::tuple<SimpleProgram*, SourceFile*, bool> SimpleProgram::UpdateProgram(
    const tspath::Path& changedFilePath, CompilerHost* newHost,
    const std::function<CheckerPool*(SimpleProgram*)>& createCheckerPool,
    const std::function<module::Resolver*(const module::ResolverOptions&)>&
        createModuleResolver) {
	auto reuseResult = ReuseProgram(changedFilePath, newHost,
	                                createCheckerPool,
	                                createModuleResolver);
	if (std::get<2>(reuseResult)) {
		return reuseResult;
	}
	SourceFile* newFile = std::get<1>(reuseResult);
	ProgramOptions newOpts = opts_;
	newOpts.Host = newHost;
	if (createCheckerPool) {
		newOpts.CreateCheckerPool = createCheckerPool;
	}
	if (createModuleResolver) {
		newOpts.CreateModuleResolver = createModuleResolver;
	}
	return {NewProgram(newOpts), newFile, false};
}

// program.go:2095 HasSameFileNames — maps.EqualFunc over filesByPath
// (SourceFile FileName equality, casing-insensitive systems read the
// real name back) plus redirectFilesByPath equality.
bool SimpleProgram::HasSameFileNames(SimpleProgram* other) {
	if (filesByPath.size() != other->filesByPath.size() ||
	    redirectFilesByPath.size() != other->redirectFilesByPath.size()) {
		return false;
	}
	for (const auto& [path, a] : filesByPath) {
		auto it = other->filesByPath.find(path);
		if (it == other->filesByPath.end() ||
		    a->FileName() != it->second->FileName()) {
			return false;
		}
	}
	for (const auto& [path, a] : redirectFilesByPath) {
		auto it = other->redirectFilesByPath.find(path);
		if (it == other->redirectFilesByPath.end() ||
		    a.fileName != it->second.fileName) {
			return false;
		}
	}
	return true;
}
// === end slice: project ===

// program.go:1690 LineCount.
int SimpleProgram::LineCount() const {
	int count = 0;
	for (auto* file : files) {
		count += static_cast<int>(file->ecmaLineMap().size());
	}
	return count;
}

// program.go:1698 IdentifierCount.
int SimpleProgram::IdentifierCount() const {
	int count = 0;
	for (auto* file : files) {
		count += file->IdentifierCount;
	}
	return count;
}

// program.go:1706 SymbolCount — file symbols plus every checker's count
// via ForEachCheckerParallel.
int SimpleProgram::SymbolCount() const {
	int count = 0;
	for (auto* file : files) {
		count += file->SymbolCount;
	}
	std::atomic<uint32_t> val{static_cast<uint32_t>(count)};
	ForEachCheckerParallel([&val](int, checker::Checker* c) {
		val.fetch_add(c->SymbolCount);
	});
	return static_cast<int>(val.load());
}

// program.go:1719 TypeCount — summed across pooled checkers.
uint32_t SimpleProgram::TypeCount() {
	std::atomic<uint32_t> val{0};
	ForEachCheckerParallel([&val](int, checker::Checker* c) {
		val.fetch_add(c->TypeCount);
	});
	return val.load();
}

// program.go:1727 InstantiationCount — summed across pooled checkers.
uint64_t SimpleProgram::InstantiationCount() {
	std::atomic<uint32_t> val{0};
	ForEachCheckerParallel([&val](int, checker::Checker* c) {
		val.fetch_add(c->TotalInstantiationCount);
	});
	return val.load();
}

// === slice: ls-autoimport ===

// program.go:1785 IsGlobalTypingsFile.
bool SimpleProgram::IsGlobalTypingsFile(const std::string& fileName) const {
	if (!tspath::isDeclarationFileName(fileName)) {
		return false;
	}
	return tspath::containsPath(opts_.TypingsLocation, fileName,
	                            comparePathsOptions());
}

// program.go:590 GetTypeChecker — the built-in pool's non-exclusive
// first checker; an external pool checks one out via GetChecker.
std::pair<checker::Checker*, std::function<void()>>
SimpleProgram::GetTypeChecker(gostd::Context ctx) {
	if (compilerCheckerPool_ != nullptr) {
		return compilerCheckerPool_->getCheckerNonExclusive();
	}
	return checkerPool_->GetChecker(ctx, nullptr);
}

// === slice: testrunner ===

// program.go:517 GetContentMapper — returns the content mapper that
// produced the given source file, or nullptr if the file was not produced
// by a content mapper.
contentmapper::Mapper* SimpleProgram::GetContentMapper(SourceFile* file) {
	if (file->ContentMapper().empty()) {
		return nullptr;
	}
	auto* mapper =
	    commandLine_->GetContentMapperForFileName(file->FileName());
	if (mapper != nullptr && mapper->Identity() == file->ContentMapper()) {
		return mapper;
	}
	return nullptr;
}

// program.go:597 ForEachCheckerParallel — grouped parallel iteration
// over the built-in pool; no-op under an external pool, like Go.
void SimpleProgram::ForEachCheckerParallel(
    const std::function<void(int, checker::Checker*)>& cb) const {
	if (compilerCheckerPool_ != nullptr) {
		compilerCheckerPool_->forEachCheckerParallel(cb);
	}
}

// === end slice: testrunner ===

// program.go:2226 collectPackageNames — lazyValue[packageNamesInfo].
SimpleProgram::packageNamesInfo* SimpleProgram::collectPackageNames() {
	return packageNames_.getValue([this]() -> packageNamesInfo* {
		auto* packageNames = new packageNamesInfo{};
		for (auto* file : files) {
			if (IsSourceFileDefaultLibrary(file->Path()) ||
			    IsSourceFileFromExternalLibrary(file) ||
			    file->FileName().find("/node_modules/") !=
			        std::string::npos) {
				// Checking for /node_modules/ is a little imprecise, but
				// ATA treats locally installed typings as root files,
				// which would not pass
				// IsSourceFileFromExternalLibrary.
				continue;
			}
			for (auto* imp : file->imports) {
				if (tspath::isExternalModuleNameRelative(imp->text())) {
					continue;
				}
				auto rmIt = resolvedModules.find(file->Path());
				if (rmIt != resolvedModules.end()) {
					module::ModeAwareCacheKey key{
					    imp->text(), GetModeForUsageLocation(file, imp)};
					auto rm2 = rmIt->second.find(key);
					if (rm2 != rmIt->second.end() &&
					    rm2->second != nullptr &&
					    rm2->second->IsResolved()) {
						module::ResolvedModule* resolvedModule =
						    rm2->second;
						if (!resolvedModule->IsExternalLibraryImport) {
							continue;
						}
						// Priority order for getting package name:
						// 1. PackageId.Name (requires both name and
						//    version in package.json)
						std::string name = resolvedModule->PackageId.Name;
						if (name.empty()) {
							// 2. GetPackageScopeForPath - get name from
							//    package.json in the package directory
							auto packageScope =
							    resolver_->GetPackageScopeForPath(
							        resolvedModule->ResolvedFileName);
							if (packageScope != nullptr &&
							    packageScope->Exists()) {
								if (auto [scopeName, ok] =
								        packageScope->Contents->Name
								            .GetValue();
								    ok) {
									name = scopeName;
								}
							}
						}
						if (name.empty()) {
							// 3. GetPackageNameFromDirectory - extract
							//    from node_modules path
							name =
							    modulespecifiers::
							        GetPackageNameFromDirectory(
							            resolvedModule
							                ->ResolvedFileName);
						}
						// 4. If all fail, don't add empty string
						if (!name.empty()) {
							packageNames->resolved.Add(name);
							// Detect deep imports: subpath imports in
							// packages without exports. These are
							// imports like "lodash/fp" where the
							// package has no exports map, so
							// auto-import can only find them via
							// recursive directory search.
							auto [_, rest] =
							    module::ParsePackageName(imp->text());
							if (!rest.empty()) {
								if (auto scope =
								        resolver_
								            ->GetPackageScopeForPath(
								                resolvedModule
								                    ->ResolvedFileName);
								    scope != nullptr &&
								    scope->Exists() &&
								    !scope->Contents->Exports
								         .IsPresent()) {
									packageNames->deepImportPackages
									    .Add(
									        module::
									            GetPackageNameFromTypesPackageName(
									                name));
								}
							}
						}
						continue;
					}
				}
				packageNames->unresolved.Add(imp->text());
			}
		}
		return packageNames;
	});
}

// === end slice: ls-autoimport ===

}  // namespace tsc::compiler
