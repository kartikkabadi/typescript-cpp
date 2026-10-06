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

#include "internal/compiler/program.h"
#include "internal/binder/binder.h"
#include "internal/compiler/emitter.h"
#include "internal/module/util.h" // === slice: ls-autoimport ===
#include "internal/modulespecifiers/types.h" // === slice: ls-autoimport ===
#include "internal/diagnostics/messages_generated.h"
#include "internal/outputpaths/outputpaths.h"
#include "internal/scanner/scanner.h"

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
                             bool skipModuleResolution_)
	: options(opts), skipModuleResolution(skipModuleResolution_) {
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

	// fileLoader{} setup (processAllProgramFiles body, fileloader.go:152)
	filesLoader loader;
	filesParser parser;
	includeProcessor_.processingDiagArena.clear();
	loader.host = host;
	loader.compilerOptions = &options;
	loader.parser = &parser;
	loader.program = this;
	loader.ip = &includeProcessor_;
	loader.skipModuleResolution = skipModuleResolution;
	loader.defaultLibraryPath = tspath::getNormalizedAbsolutePath(
	    host->DefaultLibraryPath(), host->GetCurrentDirectory());
	loader.useCaseSensitiveFileNames = true; // POSIX FS — vfs UseCaseSensitiveFileNames
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

	// module.NewResolver(ResolverOptions{...}) — no project reference
	// redirects, no typingsLocation/ProjectName for the CLI path.
	module::ResolverOptions resolverOptions;
	resolverOptions.Host = host;
	resolverOptions.CompilerOptions = &options;
	resolverOptions.TypingsLocation = "";
	resolverOptions.ProjectName = "";
	resolverOptions.ExtraExtensions = {};
	resolver_ = std::make_unique<module::DefaultResolver>(resolverOptions);
	loader.resolver = resolver_.get();

	loader.processAllProgramFiles(rootFileNames);

	// Move collected state into the program (processedFiles).
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
	syntheticImportArena = std::move(parser.factory.arena());
	sourceFilesFoundSearchingNodeModules =
	    std::move(parser.sourceFilesFoundSearchingNodeModules);
	libFiles = std::move(parser.libFiles);
	missingFiles = std::move(parser.missingFiles);
	redirectTargetsMap = std::move(parser.redirectTargetsMap);
	redirectFilesByPath = std::move(parser.redirectFilesByPath);
	fileNameList = std::move(rootFileNames);
	// filesparser.go:584 — loader's content-mapper failure diagnostics.
	contentMapperDiagnostics = std::move(loader.contentMapperDiagnostics);

	// initCheckerPool — lazily: getChecker() materializes the single
	// checker.
	verifyCompilerOptions();
	collectContentMapperOptionDiagnostics();
}

// program.go:570 BindSourceFiles (single-threaded)
void SimpleProgram::BindSourceFiles() {
	for (auto* file : files) {
		if (!file->isBound.load(std::memory_order_relaxed)) {
			bindSourceFile(file);
		}
	}
	bindDone_ = true;
}

// program.go: GetTypeChecker — lazy single checker.
checker::Checker* SimpleProgram::getChecker() {
	if (!checker_) {
		checker_ = std::make_unique<checker::Checker>();
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

// program.go:648 collectDiagnostics — single file or all files.
std::vector<Diagnostic*> SimpleProgram::collectDiagnostics(
    SourceFile* sourceFile,
    const std::function<std::vector<Diagnostic*>(SourceFile*)>& collect) {
	std::vector<Diagnostic*> result;
	if (sourceFile != nullptr) {
		result = collect(sourceFile);
	} else {
		for (auto* file : files) {
			auto d = collect(file);
			result.insert(result.end(), d.begin(), d.end());
		}
	}
	return filterAndSortDiagnostics(result);
}

// program.go:697 collectCheckerDiagnostics — same shape with SkipTypeChecking.
std::vector<Diagnostic*> SimpleProgram::collectCheckerDiagnostics(
    SourceFile* sourceFile,
    const std::function<std::vector<Diagnostic*>(SourceFile*)>& collect) {
	if (sourceFile != nullptr) {
		if (SkipTypeChecking(sourceFile, false)) {
			return {};
		}
		return filterAndSortDiagnostics(collect(sourceFile));
	}
	std::vector<Diagnostic*> result;
	for (auto* file : files) {
		if (SkipTypeChecking(file, false)) {
			continue;
		}
		auto d = collect(file);
		result.insert(result.end(), d.begin(), d.end());
	}
	return filterAndSortDiagnostics(result);
}

// program.go:743 GetSyntacticDiagnostics
std::vector<Diagnostic*> SimpleProgram::GetSyntacticDiagnostics(
    SourceFile* sourceFile) {
	return collectDiagnostics(
	    sourceFile, [this](SourceFile* file) -> std::vector<Diagnostic*> {
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
	    sourceFile, [](SourceFile* file) -> std::vector<Diagnostic*> {
		    return file->bindDiagnostics;
	    });
}

// program.go:804 GetSemanticDiagnostics
std::vector<Diagnostic*> SimpleProgram::GetSemanticDiagnostics(
    SourceFile* sourceFile) {
	return collectCheckerDiagnostics(
	    sourceFile,
	    [this](SourceFile* file) -> std::vector<Diagnostic*> {
		    return getSemanticDiagnosticsWithChecker(file);
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
    SourceFile* sourceFile) {
	auto first = filterNoEmitSemanticDiagnostics(
	    getBindAndCheckDiagnosticsWithChecker(sourceFile), &options);
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
    SourceFile* sourceFile, bool includeDeferredGlobals) {
	if (SkipTypeChecking(sourceFile, false)) {
		return {};
	}
	checker::Checker* fileChecker =
	    getChecker(); // checker creation forces binding

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

// program.go:1435 GetGlobalDiagnostics
std::vector<Diagnostic*> SimpleProgram::GetGlobalDiagnostics() {
	if (files.empty()) {
		return {};
	}
	// compilerCheckerPool.GetGlobalDiagnostics — our single checker's
	// collection (empty until the check walker lands).
	if (checker_ == nullptr) {
		return {};
	}
	return checker_->diagnostics.GetGlobalDiagnostics();
}

// program.go:1454 GetDeclarationDiagnostics
std::vector<Diagnostic*> SimpleProgram::GetDeclarationDiagnostics(
    SourceFile* sourceFile) {
	// Memoization is used in order to avoid emitting the declaration file
	// twice
	if (auto it = declarationDiagnosticCache.find(sourceFile);
	    it != declarationDiagnosticCache.end()) {
		return it->second;
	}

	auto [eh, done] = newEmitHost(this, sourceFile);
	auto diags = getDeclarationDiagnostics(eh.get(), sourceFile);
	done();
	declarationDiagnosticCache[sourceFile] = diags;
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

SourceFile* SimpleProgram::GetSourceFileForResolvedModule(
    const std::string& fileName) {
	SourceFile* file = GetSourceFile(fileName);
	// GetParseFileRedirect — project references not in scope → none.
	return file;
}

bool SimpleProgram::FileExists(const std::string& fileName) {
	return host->FileExists(fileName);
}

std::string SimpleProgram::GetCurrentDirectory() {
	return host->GetCurrentDirectory();
}

bool SimpleProgram::UseCaseSensitiveFileNames() {
	return true; // POSIX FS
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

// program.go: IsSourceFromProjectReference — none (no project references).
bool SimpleProgram::IsSourceFromProjectReference(
    const tspath::Path& path) const {
	return false;
}

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

// program.go:1799 CommonSourceDirectory (memoized).
std::string SimpleProgram::CommonSourceDirectory() {
	if (commonSourceDirectory_.has_value()) {
		return *commonSourceDirectory_;
	}
	// files() closure: emitted file names
	auto files = [this]() -> std::vector<std::string> {
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
	    Options(), files, GetCurrentDirectory(), UseCaseSensitiveFileNames(),
	    [this](const std::vector<std::string>& sourceFiles,
	           std::string_view rootDirectory) {
		    return checkSourceFilesBelongToPath(
		        sourceFiles, std::string(rootDirectory));
	    });
	return *commonSourceDirectory_;
}

// program.go:1390 IsEmitBlocked / :1384 blockEmittingOfFile
bool SimpleProgram::IsEmitBlocked(const std::string& emitFileName) const {
	return hasEmitBlockingDiagnostics.contains(toPath(emitFileName));
}

void SimpleProgram::blockEmittingOfFile(const std::string& emitFileName,
                                        Diagnostic* diag) {
	hasEmitBlockingDiagnostics.insert(toPath(emitFileName));
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
	std::unique_ptr<printer::EmitTextWriter> writer;
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

		auto [host, done] = newEmitHost(this, sourceFile);
		e->host = host.get();

		// take an unused writer
		if (writer == nullptr) {
			writer.reset(printer::NewTextWriter(newLine, 0));
		}
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
		done();
	}

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
	// Config-file syntax accessors: no config file in this mode, so
	// sourceFile()/getCompilerOptionsPropertySyntax()/
	// getCompilerOptionsObjectLiteralSyntax() are all nil — matching Go's
	// ForEachTsConfigPropArray(nil)/ForEachPropertyAssignment(nil) no-ops.

	auto createCompilerOptionsDiagnostic =
	    [&](const DiagnosticMessage* message,
	        std::vector<std::string> args = {}) -> Diagnostic* {
		// compilerOptionsProperty is nil (no config file) →
		// NewCompilerDiagnostic
		auto* diag = tsoptions::newCompilerDiagnostic(message,
		                                              std::move(args));
		programDiagnostics.push_back(diag);
		return diag;
	};

	// createOptionDiagnosticInObjectLiteralSyntax — objectLiteral is always
	// nil here, so it always returns nil (Go: ForEachPropertyAssignment(nil)
	// → nil).
	auto createOptionDiagnosticInObjectLiteralSyntax =
	    [&](void* objectLiteral, bool onKey, const std::string& key1,
	        const std::string& key2, const DiagnosticMessage* message,
	        std::vector<std::string> args = {}) -> Diagnostic* {
		return nullptr; // no config file
	};

	auto createDiagnosticForOption =
	    [&](bool onKey, const std::string& option1, const std::string& option2,
	        const DiagnosticMessage* message,
	        std::vector<std::string> args = {}) -> Diagnostic* {
		Diagnostic* diag = createOptionDiagnosticInObjectLiteralSyntax(
		    nullptr, onKey, option1, option2, message, args);
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
		std::string useInstead;
		// configFilePath() is "" without a config file → suggestion skipped.
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
	// createDiagnosticForOptionPathKeyValue — getCompilerOptionsObjectLiteralSyntax
	// is nil → ForEachPropertyAssignment(nil) → nil → falls through to
	// createCompilerOptionsDiagnostic.
	auto forEachOptionPathsSyntax =
	    [&](const std::function<Diagnostic*(void*)>& callback)
	    -> Diagnostic* { return nullptr; };

	auto createDiagnosticForOptionPaths =
	    [&](bool onKey, const std::string& key,
	        const DiagnosticMessage* message,
	        std::vector<std::string> args = {}) -> Diagnostic* {
		Diagnostic* diag = forEachOptionPathsSyntax(
		    [&](void* pathProp) -> Diagnostic* { return nullptr; });
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
		    [&](void* pathProp) -> Diagnostic* { return nullptr; });
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

// program.go:1398 verifyProjectReferences — RangeResolvedProjectReference
// iterates over resolved project references; there are none in this mode,
// so this is a faithful no-op.
void SimpleProgram::verifyProjectReferences() {}

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

// program.go GetPackagesMap — lazily-cached package name → bundles types.
const std::unordered_map<std::string, bool>&
SimpleProgram::GetPackagesMap() {
	if (!packagesMap.has_value()) {
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
	}
	return *packagesMap;
}

// === slice: incremental ===

// program.go:816 GetSuggestionDiagnostics.
std::vector<Diagnostic*> SimpleProgram::GetSuggestionDiagnostics(
    SourceFile* sourceFile) {
	return collectCheckerDiagnostics(sourceFile, [&](SourceFile* file) {
		// getSuggestionDiagnosticsWithChecker — program.go:1634.
		if (SkipTypeChecking(file, false)) {
			return std::vector<Diagnostic*>{};
		}
		return getChecker()->GetSuggestionDiagnostics(file);
	});
}

// program.go:616 GetTypeCheckerForFileExclusive — the Go checker pool
// hands out the file's dedicated checker; the single-checker port shares
// the one checker, so DoneForFile is a no-op.
std::pair<checker::Checker*, std::function<void()>>
SimpleProgram::GetTypeCheckerForFileExclusive(SourceFile* file) {
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

// program.go:804 GetSemanticDiagnosticsForIncremental —
// collectCheckerDiagnosticsFromFiles run sequentially (single checker):
// per-file bind+check with deferred globals, then filter+sort.
std::vector<std::pair<SourceFile*, std::vector<Diagnostic*>>>
SimpleProgram::GetSemanticDiagnosticsForIncremental(
    const std::vector<SourceFile*>& sourceFiles) {
	std::vector<std::pair<SourceFile*, std::vector<Diagnostic*>>> result;
	result.reserve(sourceFiles.size());
	for (auto* file : sourceFiles) {
		auto [fileChecker, done] = GetTypeCheckerForFileExclusive(file);
		auto diags = getBindAndCheckDiagnosticsWithChecker(
		    file, true /*includeDeferredGlobals*/);
		done();
		result.emplace_back(
		    file, filterAndSortDiagnostics(std::move(diags)));
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
	auto* p = new SimpleProgram(opts.Host,
	                            *opts.Config->ParsedConfig->CompilerOptions,
	                            opts.Config->ParsedConfig->FileNames,
	                            opts.Config, opts.SkipModuleResolution);
	p->opts_ = opts;
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

// program.go:332 ReuseProgram — the UpdateProgram single-file fast path.
// dep-stub — owned by compiler: the program-state replay machinery
// (processedFiles, lazyValue caches, updateFileIncludeProcessor,
// initCheckerPool) is not ported.
std::tuple<SimpleProgram*, SourceFile*, bool> SimpleProgram::ReuseProgram(
    const tspath::Path& changedFilePath, CompilerHost* newHost,
    const std::function<void*(SimpleProgram*)>& createCheckerPool,
    const std::function<module::Resolver*(const module::ResolverOptions&)>&
        createModuleResolver) {
	TSC_UNREACHABLE("SimpleProgram::ReuseProgram — owned by compiler");
}

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

// program.go:1706 SymbolCount — single checker port: files plus the lazy
// checker's symbol count when it exists.
int SimpleProgram::SymbolCount() const {
	int count = 0;
	for (auto* file : files) {
		count += file->SymbolCount;
	}
	if (checker_ != nullptr) {
		count += static_cast<int>(checker_->SymbolCount);
	}
	return count;
}

// program.go:1719 TypeCount — single checker.
uint32_t SimpleProgram::TypeCount() {
	return checker_ != nullptr ? checker_->TypeCount : 0;
}

// program.go:1727 InstantiationCount — single checker.
uint64_t SimpleProgram::InstantiationCount() {
	return checker_ != nullptr ? checker_->TotalInstantiationCount : 0;
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

// program.go:590 GetTypeChecker — single checker (the port has no checker
// pool; getChecker() is the pool-equivalent, and releasing it is a no-op).
std::pair<checker::Checker*, std::function<void()>>
SimpleProgram::GetTypeChecker(gostd::Context ctx) {
	(void)ctx;
	return {getChecker(), []() {}};
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

// program.go:597 ForEachCheckerParallel — Go iterates the
// compilerCheckerPool when non-nil. The port's equivalent is the single
// lazy checker covering all files, so the callback runs once.
void SimpleProgram::ForEachCheckerParallel(
    const std::function<void(int, checker::Checker*)>& cb) {
	cb(0, getChecker());
}

// === end slice: testrunner ===

// program.go:2226 collectPackageNames — lazy like Go's lazyValue.
SimpleProgram::packageNamesInfo* SimpleProgram::collectPackageNames() {
	if (packageNames_.has_value()) {
		return &*packageNames_;
	}
	packageNames_.emplace();
	auto& packageNames = *packageNames_;
	for (auto* file : files) {
		if (IsSourceFileDefaultLibrary(file->Path()) ||
		    IsSourceFileFromExternalLibrary(file) ||
		    file->FileName().find("/node_modules/") != std::string::npos) {
			// Checking for /node_modules/ is a little imprecise, but ATA
			// treats locally installed typings as root files, which would
			// not pass IsSourceFileFromExternalLibrary.
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
				if (rm2 != rmIt->second.end() && rm2->second != nullptr &&
				    rm2->second->IsResolved()) {
					module::ResolvedModule* resolvedModule = rm2->second;
					if (!resolvedModule->IsExternalLibraryImport) {
						continue;
					}
					// Priority order for getting package name:
					// 1. PackageId.Name (requires both name and version in
					//    package.json)
					std::string name = resolvedModule->PackageId.Name;
					if (name.empty()) {
						// 2. GetPackageScopeForPath - get name from
						//    package.json in the package directory
						auto packageScope = resolver_->GetPackageScopeForPath(
						    resolvedModule->ResolvedFileName);
						if (packageScope != nullptr &&
						    packageScope->Exists()) {
							if (auto [scopeName, ok] =
							        packageScope->Contents->Name.GetValue();
							    ok) {
								name = scopeName;
							}
						}
					}
					if (name.empty()) {
						// 3. GetPackageNameFromDirectory - extract from
						//    node_modules path
						name = modulespecifiers::GetPackageNameFromDirectory(
						    resolvedModule->ResolvedFileName);
					}
					// 4. If all fail, don't add empty string
					if (!name.empty()) {
						packageNames.resolved.Add(name);
						// Detect deep imports: subpath imports in packages
						// without exports. These are imports like
						// "lodash/fp" where the package has no exports map,
						// so auto-import can only find them via recursive
						// directory search.
						auto [_, rest] = module::ParsePackageName(imp->text());
						if (!rest.empty()) {
							if (auto scope =
							        resolver_->GetPackageScopeForPath(
							            resolvedModule->ResolvedFileName);
							    scope != nullptr && scope->Exists() &&
							    !scope->Contents->Exports.IsPresent()) {
								packageNames.deepImportPackages.Add(
								    module::GetPackageNameFromTypesPackageName(
								        name));
							}
						}
					}
					continue;
				}
			}
			packageNames.unresolved.Add(imp->text());
		}
	}
	return &packageNames;
}

// === end slice: ls-autoimport ===

}  // namespace tsc::compiler
