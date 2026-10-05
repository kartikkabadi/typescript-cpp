// Port of tsc/internal/tsoptions/declscompiler.go — compiler option
// declaration tables + optionsHaveChanges helpers (reflection replaced by the
// compilerOptionFieldInfos table).
#include "internal/tsoptions/tsoptions.h"

#include "internal/diagnostics/messages_generated.h"

namespace tsc::tsoptions {

static const std::vector<const CommandLineOption*> commonOptionsWithBuildData()
{
	static const std::vector<const CommandLineOption*> v = [] {
		//******* commonOptionsWithBuild *******
		static const CommandLineOption o_help{
		    .Name = "help", 
		    .ShortName = "h", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = true, 
		    .IsCommandLineOnly = true, 
		    .Category = Command_line_Options, 
		    .Description = Print_this_message, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_help_1{
		    .Name = "help", 
		    .ShortName = "?", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .IsCommandLineOnly = true, 
		    .Category = Command_line_Options, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_watch{
		    .Name = "watch", 
		    .ShortName = "w", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = true, 
		    .IsCommandLineOnly = true, 
		    .Category = Command_line_Options, 
		    .Description = Watch_input_files, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_preserveWatchOutput{
		    .Name = "preserveWatchOutput", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = false, 
		    .Category = Output_Formatting, 
		    .Description = Disable_wiping_the_console_in_watch_mode, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_listFiles{
		    .Name = "listFiles", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Compiler_Diagnostics, 
		    .Description = Print_all_of_the_files_read_during_the_compilation, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_explainFiles{
		    .Name = "explainFiles", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Compiler_Diagnostics, 
		    .Description = Print_files_read_during_the_compilation_including_why_it_was_included, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_listEmittedFiles{
		    .Name = "listEmittedFiles", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Compiler_Diagnostics, 
		    .Description = Print_the_names_of_emitted_files_after_a_compilation, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_pretty{
		    .Name = "pretty", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Output_Formatting, 
		    .Description = Enable_color_and_formatting_in_TypeScript_s_output_to_make_compiler_errors_easier_to_read, 
		    .DefaultValueDescription = true, 
		};
		static const CommandLineOption o_traceResolution{
		    .Name = "traceResolution", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Compiler_Diagnostics, 
		    .Description = Log_paths_used_during_the_moduleResolution_process, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_diagnostics{
		    .Name = "diagnostics", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Compiler_Diagnostics, 
		    .Description = Output_compiler_performance_information_after_building, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_extendedDiagnostics{
		    .Name = "extendedDiagnostics", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Compiler_Diagnostics, 
		    .Description = Output_more_detailed_compiler_performance_information_after_building, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_generateCpuProfile{
		    .Name = "generateCpuProfile", 
		    .Kind = CommandLineOptionTypeString, 
		    .IsFilePath = true, 
		    .Category = Compiler_Diagnostics, 
		    .Description = Emit_a_v8_CPU_profile_of_the_compiler_run_for_debugging, 
		    .DefaultValueDescription = std::string("profile.cpuprofile"), 
		};
		static const CommandLineOption o_generateTrace{
		    .Name = "generateTrace", 
		    .Kind = CommandLineOptionTypeString, 
		    .IsFilePath = true, 
		    .Category = Compiler_Diagnostics, 
		    .Description = Generates_an_event_trace_and_a_list_of_types, 
		};
		static const CommandLineOption o_incremental{
		    .Name = "incremental", 
		    .ShortName = "i", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Projects, 
		    .Description = Save_tsbuildinfo_files_to_allow_for_incremental_compilation_of_projects, 
		    .transpileOptionValue = Tristate::Unknown, 
		    .DefaultValueDescription = X_false_unless_composite_is_set, 
		};
		static const CommandLineOption o_declaration{
		    .Name = "declaration", 
		    .ShortName = "d", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Emit, 
		    .transpileOptionValue = Tristate::Unknown, 
		    .Description = Generate_d_ts_files_from_TypeScript_and_JavaScript_files_in_your_project, 
		    .DefaultValueDescription = X_false_unless_composite_is_set, 
		};
		static const CommandLineOption o_declarationMap{
		    .Name = "declarationMap", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Emit, 
		    .DefaultValueDescription = false, 
		    .Description = Create_sourcemaps_for_d_ts_files, 
		};
		static const CommandLineOption o_emitDeclarationOnly{
		    .Name = "emitDeclarationOnly", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Emit, 
		    .Description = Only_output_d_ts_files_and_not_JavaScript_files, 
		    .transpileOptionValue = Tristate::Unknown, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_sourceMap{
		    .Name = "sourceMap", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Emit, 
		    .DefaultValueDescription = false, 
		    .Description = Create_source_map_files_for_emitted_JavaScript_files, 
		};
		static const CommandLineOption o_inlineSourceMap{
		    .Name = "inlineSourceMap", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .Description = Include_sourcemap_files_inside_the_emitted_JavaScript, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noCheck{
		    .Name = "noCheck", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = false, 
		    .Category = Compiler_Diagnostics, 
		    .Description = Disable_full_type_checking_only_critical_parse_and_emit_errors_will_be_reported, 
		    .transpileOptionValue = Tristate::True, 
		    .DefaultValueDescription = false, 
		    // Not setting affectsSemanticDiagnostics or affectsBuildInfo because we dont want all diagnostics to go away, its handled in builder
		};
		static const CommandLineOption o_deduplicatePackages{
		    .Name = "deduplicatePackages", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Type_Checking, 
		    .Description = Deduplicate_packages_with_the_same_name_and_version, 
		    .DefaultValueDescription = true, 
		    .AffectsProgramStructure = true, 
		};
		static const CommandLineOption o_noEmit{
		    .Name = "noEmit", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Emit, 
		    .Description = Disable_emitting_files_from_a_compilation, 
		    .transpileOptionValue = Tristate::Unknown, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_assumeChangesOnlyAffectDirectDependencies{
		    .Name = "assumeChangesOnlyAffectDirectDependencies", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Watch_and_Build_Modes, 
		    .Description = Have_recompiles_in_projects_that_use_incremental_and_watch_mode_assume_that_changes_within_a_file_will_only_affect_files_directly_depending_on_it, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_locale{
		    .Name = "locale", 
		    .Kind = CommandLineOptionTypeString, 
		    .Category = Command_line_Options, 
		    .IsCommandLineOnly = true, 
		    .Description = Set_the_language_of_the_messaging_from_TypeScript_This_does_not_affect_emit, 
		    .DefaultValueDescription = Platform_specific, 
		    .extraValidation_ = extraValidationLocale, 
		};
		static const CommandLineOption o_quiet{
		    .Name = "quiet", 
		    .ShortName = "q", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Command_line_Options, 
		    .Description = Do_not_print_diagnostics, 
		};
		static const CommandLineOption o_singleThreaded{
		    .Name = "singleThreaded", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Command_line_Options, 
		    .Description = Run_in_single_threaded_mode, 
		};
		static const CommandLineOption o_pprofDir{
		    .Name = "pprofDir", 
		    .Kind = CommandLineOptionTypeString, 
		    .IsFilePath = true, 
		    .Category = Command_line_Options, 
		    .Description = Generate_pprof_CPU_Slashmemory_profiles_to_the_given_directory, 
		};
		static const CommandLineOption o_checkers{
		    .Name = "checkers", 
		    .Kind = CommandLineOptionTypeNumber, 
		    .Category = Command_line_Options, 
		    .Description = Set_the_number_of_checkers_per_project, 
		    .DefaultValueDescription = X_4_unless_singleThreaded_is_passed, 
		    .minValue = 1, 
		};
		static const CommandLineOption o_runExternalCode{
		    .Name = "runExternalCode", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Command_line_Options, 
		    .IsCommandLineOnly = true, 
		    .Description = Allow_loading_external_content_mapper_plugins_that_execute_code_during_compilation, 
		    .DefaultValueDescription = false, 
		};
		return std::vector<const CommandLineOption*>{
			&o_help,
			&o_help_1,
			&o_watch,
			&o_preserveWatchOutput,
			&o_listFiles,
			&o_explainFiles,
			&o_listEmittedFiles,
			&o_pretty,
			&o_traceResolution,
			&o_diagnostics,
			&o_extendedDiagnostics,
			&o_generateCpuProfile,
			&o_generateTrace,
			&o_incremental,
			&o_declaration,
			&o_declarationMap,
			&o_emitDeclarationOnly,
			&o_sourceMap,
			&o_inlineSourceMap,
			&o_noCheck,
			&o_deduplicatePackages,
			&o_noEmit,
			&o_assumeChangesOnlyAffectDirectDependencies,
			&o_locale,
			&o_quiet,
			&o_singleThreaded,
			&o_pprofDir,
			&o_checkers,
			&o_runExternalCode,
		};
	}();
	return v;
}

const std::vector<const CommandLineOption*>& commonOptionsWithBuild()
{
	return commonOptionsWithBuildData();
}

static const std::vector<const CommandLineOption*> optionsForCompilerData()
{
	static const std::vector<const CommandLineOption*> v = [] {
		//******* compilerOptions not common with --build *******
		// CommandLine only options
		static const CommandLineOption o_all{
		    .Name = "all", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Command_line_Options, 
		    .Description = Show_all_compiler_options, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_version{
		    .Name = "version", 
		    .ShortName = "v", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Command_line_Options, 
		    .Description = Print_the_compiler_s_version, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_init{
		    .Name = "init", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Command_line_Options, 
		    .Description = Initializes_a_TypeScript_project_and_creates_a_tsconfig_json_file, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_project{
		    .Name = "project", 
		    .ShortName = "p", 
		    .Kind = CommandLineOptionTypeString, 
		    .IsFilePath = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Command_line_Options, 
		    .Description = Compile_the_project_given_the_path_to_its_configuration_file_or_to_a_folder_with_a_tsconfig_json, 
		};
		static const CommandLineOption o_showConfig{
		    .Name = "showConfig", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Command_line_Options, 
		    .IsCommandLineOnly = true, 
		    .Description = Print_the_final_configuration_instead_of_building, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_listFilesOnly{
		    .Name = "listFilesOnly", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Command_line_Options, 
		    .IsCommandLineOnly = true, 
		    .Description = Print_names_of_files_that_are_part_of_the_compilation_and_then_stop_processing, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_ignoreConfig{
		    .Name = "ignoreConfig", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Command_line_Options, 
		    .IsCommandLineOnly = true, 
		    .Description = Ignore_the_tsconfig_found_and_build_with_commandline_options_and_files, 
		    .DefaultValueDescription = false, 
		};
		// Basic
		// targetOptionDeclaration,
		static const CommandLineOption o_target{
		    .Name = "target", 
		    .ShortName = "t", 
		    .Kind = CommandLineOptionTypeEnum, // targetOptionMap
		    .AffectsSourceFile = true, 
		    .AffectsModuleResolution = true, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Language_and_Environment, 
		    .Description = Set_the_JavaScript_language_version_for_emitted_JavaScript_and_include_compatible_library_declarations, 
		    .DefaultValueDescription = ScriptTarget::LatestStandard, 
		};
		// moduleOptionDeclaration,
		static const CommandLineOption o_module{
		    .Name = "module", 
		    .ShortName = "m", 
		    .Kind = CommandLineOptionTypeEnum, // moduleOptionMap
		    .AffectsModuleResolution = true, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Modules, 
		    .Description = Specify_what_module_code_is_generated, 
		    .DefaultValueDescription = Tristate::Unknown, 
		};
		static const CommandLineOption o_lib{
		    .Name = "lib", 
		    .Kind = CommandLineOptionTypeList, 
		    // elements: &CommandLineOption{
		    // 	name:                    "lib",
		    // 	kind:                   CommandLineOptionTypeEnum, // libMap,
		    // 	defaultValueDescription: core.TSUnknown,
		    // },
		    .AffectsProgramStructure = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Language_and_Environment, 
		    .Description = Specify_a_set_of_bundled_library_declaration_files_that_describe_the_target_runtime_environment, 
		    .transpileOptionValue = Tristate::Unknown, 
		};
		static const CommandLineOption o_allowJs{
		    .Name = "allowJs", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .allowJsFlag = true, 
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = JavaScript_Support, 
		    .Description = Allow_JavaScript_files_to_be_a_part_of_your_program_Use_the_checkJs_option_to_get_errors_from_these_files, 
		    .DefaultValueDescription = X_false_unless_checkJs_is_set, 
		};
		static const CommandLineOption o_checkJs{
		    .Name = "checkJs", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsModuleResolution = true, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = JavaScript_Support, 
		    .Description = Enable_error_reporting_in_type_checked_JavaScript_files, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_jsx{
		    .Name = "jsx", 
		    .Kind = CommandLineOptionTypeEnum, // jsxOptionMap,
		    .AffectsSourceFile = true, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .AffectsModuleResolution = true, 
		    // The checker emits an error when it sees JSX but this option is not set in compilerOptions.
		    // This is effectively a semantic error, so mark this option as affecting semantic diagnostics
		    // so we know to refresh errors when this option is changed.
		    .AffectsSemanticDiagnostics = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Language_and_Environment, 
		    .Description = Specify_what_JSX_code_is_generated, 
		    .DefaultValueDescription = Tristate::Unknown, 
		};
		static const CommandLineOption o_outFile{
		    .Name = "outFile", 
		    .Kind = CommandLineOptionTypeString, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .AffectsDeclarationPath = true, 
		    .IsFilePath = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Emit, 
		    .Description = Specify_a_file_that_bundles_all_outputs_into_one_JavaScript_file_If_declaration_is_true_also_designates_a_file_that_bundles_all_d_ts_output, 
		    .transpileOptionValue = Tristate::Unknown, 
		};
		static const CommandLineOption o_outDir{
		    .Name = "outDir", 
		    .Kind = CommandLineOptionTypeString, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .AffectsDeclarationPath = true, 
		    .IsFilePath = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Emit, 
		    .Description = Specify_an_output_folder_for_all_emitted_files, 
		};
		static const CommandLineOption o_rootDir{
		    .Name = "rootDir", 
		    .Kind = CommandLineOptionTypeString, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .AffectsDeclarationPath = true, 
		    .IsFilePath = true, 
		    .Category = Modules, 
		    .Description = Specify_the_root_folder_within_your_source_files, 
		    .DefaultValueDescription = Computed_from_the_list_of_input_files, 
		};
		static const CommandLineOption o_composite{
		    .Name = "composite", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true, 
		    .IsTSConfigOnly = true, 
		    .Category = Projects, 
		    .transpileOptionValue = Tristate::Unknown, 
		    .DefaultValueDescription = false, 
		    .Description = Enable_constraints_that_allow_a_TypeScript_project_to_be_used_with_project_references, 
		};
		static const CommandLineOption o_tsBuildInfoFile{
		    .Name = "tsBuildInfoFile", 
		    .Kind = CommandLineOptionTypeString, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .IsFilePath = true, 
		    .Category = Projects, 
		    .transpileOptionValue = Tristate::Unknown, 
		    .DefaultValueDescription = std::string(".tsbuildinfo"), 
		    .Description = Specify_the_path_to_tsbuildinfo_incremental_compilation_file, 
		};
		static const CommandLineOption o_removeComments{
		    .Name = "removeComments", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Emit, 
		    .DefaultValueDescription = false, 
		    .Description = Disable_emitting_comments, 
		};
		static const CommandLineOption o_importHelpers{
		    .Name = "importHelpers", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .AffectsSourceFile = true, 
		    .Category = Emit, 
		    .Description = Allow_importing_helper_functions_from_tslib_once_per_project_instead_of_including_them_per_file, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_downlevelIteration{
		    .Name = "downlevelIteration", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .Description = Emit_more_compliant_but_verbose_and_less_performant_JavaScript_for_iteration, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_isolatedModules{
		    .Name = "isolatedModules", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Interop_Constraints, 
		    .Description = Ensure_that_each_file_can_be_safely_transpiled_without_relying_on_other_imports, 
		    .transpileOptionValue = Tristate::True, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_verbatimModuleSyntax{
		    .Name = "verbatimModuleSyntax", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Interop_Constraints, 
		    .Description = Do_not_transform_or_elide_any_imports_or_exports_not_marked_as_type_only_ensuring_they_are_written_in_the_output_file_s_format_based_on_the_module_setting, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_isolatedDeclarations{
		    .Name = "isolatedDeclarations", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Interop_Constraints, 
		    .Description = Require_sufficient_annotation_on_exports_so_other_tools_can_trivially_generate_declaration_files, 
		    .DefaultValueDescription = false, 
		    .AffectsBuildInfo = true, 
		    .AffectsSemanticDiagnostics = true, 
		};
		static const CommandLineOption o_erasableSyntaxOnly{
		    .Name = "erasableSyntaxOnly", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Interop_Constraints, 
		    .Description = Do_not_allow_runtime_constructs_that_are_not_part_of_ECMAScript, 
		    .DefaultValueDescription = false, 
		    .AffectsBuildInfo = true, 
		    .AffectsSemanticDiagnostics = true, 
		};
		static const CommandLineOption o_libReplacement{
		    .Name = "libReplacement", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsProgramStructure = true, 
		    .Category = Language_and_Environment, 
		    .Description = Enable_lib_replacement, 
		    .DefaultValueDescription = false, 
		};
		// Strict Type Checks
		static const CommandLineOption o_strict{
		    .Name = "strict", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    // Though this affects semantic diagnostics, affectsSemanticDiagnostics is not set here
		    // The value of each strictFlag depends on own strictFlag value or this and never accessed directly.
		    // But we need to store `strict` in builf info, even though it won't be examined directly, so that the
		    // flags it controls (e.g. `strictNullChecks`) will be retrieved correctly
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Type_Checking, 
		    .Description = Enable_all_strict_type_checking_options, 
		    .DefaultValueDescription = true, 
		};
		static const CommandLineOption o_noImplicitAny{
		    .Name = "noImplicitAny", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .strictFlag = true, 
		    .Category = Type_Checking, 
		    .Description = Enable_error_reporting_for_expressions_and_declarations_with_an_implied_any_type, 
		    .DefaultValueDescription = X_true_unless_strict_is_false, 
		};
		static const CommandLineOption o_strictNullChecks{
		    .Name = "strictNullChecks", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .strictFlag = true, 
		    .Category = Type_Checking, 
		    .Description = When_type_checking_take_into_account_null_and_undefined, 
		    .DefaultValueDescription = X_true_unless_strict_is_false, 
		};
		static const CommandLineOption o_strictFunctionTypes{
		    .Name = "strictFunctionTypes", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .strictFlag = true, 
		    .Category = Type_Checking, 
		    .Description = When_assigning_functions_check_to_ensure_parameters_and_the_return_values_are_subtype_compatible, 
		    .DefaultValueDescription = X_true_unless_strict_is_false, 
		};
		static const CommandLineOption o_strictBindCallApply{
		    .Name = "strictBindCallApply", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .strictFlag = true, 
		    .Category = Type_Checking, 
		    .Description = Check_that_the_arguments_for_bind_call_and_apply_methods_match_the_original_function, 
		    .DefaultValueDescription = X_true_unless_strict_is_false, 
		};
		static const CommandLineOption o_strictPropertyInitialization{
		    .Name = "strictPropertyInitialization", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .strictFlag = true, 
		    .Category = Type_Checking, 
		    .Description = Check_for_class_properties_that_are_declared_but_not_set_in_the_constructor, 
		    .DefaultValueDescription = X_true_unless_strict_is_false, 
		};
		static const CommandLineOption o_strictBuiltinIteratorReturn{
		    .Name = "strictBuiltinIteratorReturn", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .strictFlag = true, 
		    .Category = Type_Checking, 
		    .Description = Built_in_iterators_are_instantiated_with_a_TReturn_type_of_undefined_instead_of_any, 
		    .DefaultValueDescription = X_true_unless_strict_is_false, 
		};
		static const CommandLineOption o_noImplicitThis{
		    .Name = "noImplicitThis", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .strictFlag = true, 
		    .Category = Type_Checking, 
		    .Description = Enable_error_reporting_when_this_is_given_the_type_any, 
		    .DefaultValueDescription = X_true_unless_strict_is_false, 
		};
		static const CommandLineOption o_useUnknownInCatchVariables{
		    .Name = "useUnknownInCatchVariables", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .strictFlag = true, 
		    .Category = Type_Checking, 
		    .Description = Default_catch_clause_variables_as_unknown_instead_of_any, 
		    .DefaultValueDescription = X_true_unless_strict_is_false, 
		};
		static const CommandLineOption o_alwaysStrict{
		    .Name = "alwaysStrict", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSourceFile = true, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Ensure_use_strict_is_always_emitted, 
		    .DefaultValueDescription = true, 
		};
		static const CommandLineOption o_stableTypeOrdering{
		    .Name = "stableTypeOrdering", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Ensure_types_are_ordered_stably_and_deterministically_across_compilations, 
		    .DefaultValueDescription = true, 
		};
		// Additional Checks
		static const CommandLineOption o_noUnusedLocals{
		    .Name = "noUnusedLocals", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Enable_error_reporting_when_local_variables_aren_t_read, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noUnusedParameters{
		    .Name = "noUnusedParameters", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Raise_an_error_when_a_function_parameter_isn_t_read, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_exactOptionalPropertyTypes{
		    .Name = "exactOptionalPropertyTypes", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Interpret_optional_property_types_as_written_rather_than_adding_undefined, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noImplicitReturns{
		    .Name = "noImplicitReturns", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Enable_error_reporting_for_codepaths_that_do_not_explicitly_return_in_a_function, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noFallthroughCasesInSwitch{
		    .Name = "noFallthroughCasesInSwitch", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsBindDiagnostics = true, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Enable_error_reporting_for_fallthrough_cases_in_switch_statements, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noUncheckedIndexedAccess{
		    .Name = "noUncheckedIndexedAccess", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Add_undefined_to_a_type_when_accessed_using_an_index, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noImplicitOverride{
		    .Name = "noImplicitOverride", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Ensure_overriding_members_in_derived_classes_are_marked_with_an_override_modifier, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noPropertyAccessFromIndexSignature{
		    .Name = "noPropertyAccessFromIndexSignature", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = false, 
		    .Category = Type_Checking, 
		    .Description = Enforces_using_indexed_accessors_for_keys_declared_using_an_indexed_type, 
		    .DefaultValueDescription = false, 
		};
		// Module Resolution
		static const CommandLineOption o_moduleResolution{
		    .Name = "moduleResolution", 
		    .Kind = CommandLineOptionTypeEnum, 
		    //    new Map(Object.entries({
		    //         // N.B. The first entry specifies the value shown in `tsc --init`
		    //         node10: ModuleResolutionKind.Node10,
		    //         node: ModuleResolutionKind.Node10,
		    //         classic: ModuleResolutionKind.Classic,
		    //         node16: ModuleResolutionKind.Node16,
		    //         nodenext: ModuleResolutionKind.NodeNext,
		    //         bundler: ModuleResolutionKind.Bundler,
		    //     })),
		    .AffectsModuleResolution = true, 
		    .Category = Modules, 
		    .Description = Specify_how_TypeScript_looks_up_a_file_from_a_given_module_specifier, 
		    .DefaultValueDescription = X_nodenext_if_module_is_nodenext_node16_if_module_is_node16_or_node18_otherwise_bundler, 
		};
		static const CommandLineOption o_baseUrl{
		    .Name = "baseUrl", 
		    .Kind = CommandLineOptionTypeString, 
		    .AffectsModuleResolution = true, 
		    .IsFilePath = true, 
		    .Category = Modules, 
		    .Description = Specify_the_base_directory_to_resolve_non_relative_module_names, 
		};
		static const CommandLineOption o_paths{
		    // this option can only be specified in tsconfig.json
		    // use type = object to copy the value as-is
		    .Name = "paths", 
		    .Kind = CommandLineOptionTypeObject, 
		    .AffectsModuleResolution = true, 
		    .allowConfigDirTemplateSubstitution = true, 
		    .IsTSConfigOnly = true, 
		    .Category = Modules, 
		    .Description = Specify_a_set_of_entries_that_re_map_imports_to_additional_lookup_locations, 
		    .transpileOptionValue = Tristate::Unknown, 
		};
		static const CommandLineOption o_rootDirs{
		    // this option can only be specified in tsconfig.json
		    // use type = object to copy the value as-is
		    .Name = "rootDirs", 
		    .Kind = CommandLineOptionTypeList, 
		    .IsTSConfigOnly = true, 
		    .AffectsModuleResolution = true, 
		    .allowConfigDirTemplateSubstitution = true, 
		    .Category = Modules, 
		    .Description = Allow_multiple_folders_to_be_treated_as_one_when_resolving_modules, 
		    .transpileOptionValue = Tristate::Unknown, 
		    .DefaultValueDescription = Computed_from_the_list_of_input_files, 
		};
		static const CommandLineOption o_typeRoots{
		    .Name = "typeRoots", 
		    .Kind = CommandLineOptionTypeList, 
		    .AffectsModuleResolution = true, 
		    .allowConfigDirTemplateSubstitution = true, 
		    .Category = Modules, 
		    .Description = Specify_multiple_folders_that_act_like_Slashnode_modules_Slash_types, 
		};
		static const CommandLineOption o_types{
		    .Name = "types", 
		    .Kind = CommandLineOptionTypeList, 
		    .AffectsProgramStructure = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Modules, 
		    .Description = Specify_type_package_names_to_be_included_without_being_referenced_in_a_source_file, 
		    .transpileOptionValue = Tristate::Unknown, 
		};
		static const CommandLineOption o_allowSyntheticDefaultImports{
		    .Name = "allowSyntheticDefaultImports", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Interop_Constraints, 
		    .Description = Allow_import_x_from_y_when_a_module_doesn_t_have_a_default_export, 
		    .DefaultValueDescription = true, 
		};
		static const CommandLineOption o_esModuleInterop{
		    .Name = "esModuleInterop", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .ShowInSimplifiedHelpView = true, 
		    .Category = Interop_Constraints, 
		    .Description = Emit_additional_JavaScript_to_ease_support_for_importing_CommonJS_modules_This_enables_allowSyntheticDefaultImports_for_type_compatibility, 
		    .DefaultValueDescription = true, 
		};
		static const CommandLineOption o_preserveSymlinks{
		    .Name = "preserveSymlinks", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Interop_Constraints, 
		    .Description = Disable_resolving_symlinks_to_their_realpath_This_correlates_to_the_same_flag_in_node, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_allowUmdGlobalAccess{
		    .Name = "allowUmdGlobalAccess", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Modules, 
		    .Description = Allow_accessing_UMD_globals_from_modules, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_moduleSuffixes{
		    .Name = "moduleSuffixes", 
		    .Kind = CommandLineOptionTypeList, 
		    .listPreserveFalsyValues = true, 
		    .AffectsModuleResolution = true, 
		    .Category = Modules, 
		    .Description = List_of_file_name_suffixes_to_search_when_resolving_a_module, 
		};
		static const CommandLineOption o_allowImportingTsExtensions{
		    .Name = "allowImportingTsExtensions", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Modules, 
		    .Description = Allow_imports_to_include_TypeScript_file_extensions_Requires_moduleResolution_bundler_and_either_noEmit_or_emitDeclarationOnly_to_be_set, 
		    .DefaultValueDescription = false, 
		    .transpileOptionValue = Tristate::Unknown, 
		};
		static const CommandLineOption o_rewriteRelativeImportExtensions{
		    .Name = "rewriteRelativeImportExtensions", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Modules, 
		    .Description = Rewrite_ts_tsx_mts_and_cts_file_extensions_in_relative_import_paths_to_their_JavaScript_equivalent_in_output_files, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_resolvePackageJsonExports{
		    .Name = "resolvePackageJsonExports", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsModuleResolution = true, 
		    .Category = Modules, 
		    .Description = Use_the_package_json_exports_field_when_resolving_package_imports, 
		    .DefaultValueDescription = X_true_when_moduleResolution_is_node16_nodenext_or_bundler_otherwise_false, 
		};
		static const CommandLineOption o_resolvePackageJsonImports{
		    .Name = "resolvePackageJsonImports", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsModuleResolution = true, 
		    .Category = Modules, 
		    .Description = Use_the_package_json_imports_field_when_resolving_imports, 
		    .DefaultValueDescription = X_true_when_moduleResolution_is_node16_nodenext_or_bundler_otherwise_false, 
		};
		static const CommandLineOption o_customConditions{
		    .Name = "customConditions", 
		    .Kind = CommandLineOptionTypeList, 
		    .AffectsModuleResolution = true, 
		    .Category = Modules, 
		    .Description = Conditions_to_set_in_addition_to_the_resolver_specific_defaults_when_resolving_imports, 
		};
		static const CommandLineOption o_noUncheckedSideEffectImports{
		    .Name = "noUncheckedSideEffectImports", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Modules, 
		    .Description = Check_side_effect_imports, 
		    .DefaultValueDescription = true, 
		};
		// Source Maps
		static const CommandLineOption o_sourceRoot{
		    .Name = "sourceRoot", 
		    .Kind = CommandLineOptionTypeString, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .Description = Specify_the_root_path_for_debuggers_to_find_the_reference_source_code, 
		};
		static const CommandLineOption o_mapRoot{
		    .Name = "mapRoot", 
		    .Kind = CommandLineOptionTypeString, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .Description = Specify_the_location_where_debugger_should_locate_map_files_instead_of_generated_locations, 
		};
		static const CommandLineOption o_inlineSources{
		    .Name = "inlineSources", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .Description = Include_source_code_in_the_sourcemaps_inside_the_emitted_JavaScript, 
		    .DefaultValueDescription = false, 
		};
		// Experimental
		static const CommandLineOption o_experimentalDecorators{
		    .Name = "experimentalDecorators", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Language_and_Environment, 
		    .Description = Enable_experimental_support_for_legacy_experimental_decorators, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_emitDecoratorMetadata{
		    .Name = "emitDecoratorMetadata", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Language_and_Environment, 
		    .Description = Emit_design_type_metadata_for_decorated_declarations_in_source_files, 
		    .DefaultValueDescription = false, 
		};
		// Advanced
		static const CommandLineOption o_jsxFactory{
		    .Name = "jsxFactory", 
		    .Kind = CommandLineOptionTypeString, 
		    .Category = Language_and_Environment, 
		    .Description = Specify_the_JSX_factory_function_used_when_targeting_React_JSX_emit_e_g_React_createElement_or_h, 
		    .DefaultValueDescription = std::string("`React.createElement`"), 
		};
		static const CommandLineOption o_jsxFragmentFactory{
		    .Name = "jsxFragmentFactory", 
		    .Kind = CommandLineOptionTypeString, 
		    .Category = Language_and_Environment, 
		    .Description = Specify_the_JSX_Fragment_reference_used_for_fragments_when_targeting_React_JSX_emit_e_g_React_Fragment_or_Fragment, 
		    .DefaultValueDescription = std::string("React.Fragment"), 
		};
		static const CommandLineOption o_jsxImportSource{
		    .Name = "jsxImportSource", 
		    .Kind = CommandLineOptionTypeString, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .AffectsModuleResolution = true, 
		    .AffectsSourceFile = true, 
		    .Category = Language_and_Environment, 
		    .Description = Specify_module_specifier_used_to_import_the_JSX_factory_functions_when_using_jsx_Colon_react_jsx_Asterisk, 
		    .DefaultValueDescription = std::string("react"), 
		};
		static const CommandLineOption o_resolveJsonModule{
		    .Name = "resolveJsonModule", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsModuleResolution = true, 
		    .Category = Modules, 
		    .Description = Enable_importing_json_files, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_allowArbitraryExtensions{
		    .Name = "allowArbitraryExtensions", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsProgramStructure = true, 
		    .Category = Modules, 
		    .Description = Enable_importing_files_with_any_extension_provided_a_declaration_file_is_present, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_reactNamespace{
		    .Name = "reactNamespace", 
		    .Kind = CommandLineOptionTypeString, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Language_and_Environment, 
		    .Description = Specify_the_object_invoked_for_createElement_This_only_applies_when_targeting_react_JSX_emit, 
		    .DefaultValueDescription = std::string("`React`"), 
		};
		static const CommandLineOption o_skipDefaultLibCheck{
		    .Name = "skipDefaultLibCheck", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    // We need to store these to determine whether `lib` files need to be rechecked
		    .AffectsBuildInfo = true, 
		    .Category = Completeness, 
		    .Description = Skip_type_checking_d_ts_files_that_are_included_with_TypeScript, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_emitBOM{
		    .Name = "emitBOM", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .Description = Emit_a_UTF_8_Byte_Order_Mark_BOM_in_the_beginning_of_output_files, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_newLine{
		    .Name = "newLine", 
		    .Kind = CommandLineOptionTypeEnum, // newLineOptionMap,
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .Description = Set_the_newline_character_for_emitting_files, 
		    .DefaultValueDescription = std::string("lf"), 
		};
		static const CommandLineOption o_noErrorTruncation{
		    .Name = "noErrorTruncation", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Output_Formatting, 
		    .Description = Disable_truncating_types_in_error_messages, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noLib{
		    .Name = "noLib", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .Category = Language_and_Environment, 
		    .AffectsProgramStructure = true, 
		    .Description = Disable_including_any_library_files_including_the_default_lib_d_ts, 
		    // We are not returning a sourceFile for lib file when asked by the program,
		    // so pass --noLib to avoid reporting a file not found error.
		    .transpileOptionValue = Tristate::True, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noResolve{
		    .Name = "noResolve", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsModuleResolution = true, 
		    .Category = Modules, 
		    .Description = Disallow_import_s_require_s_or_reference_s_from_expanding_the_number_of_files_TypeScript_should_add_to_a_project, 
		    // We are not doing a full typecheck, we are not resolving the whole context,
		    // so pass --noResolve to avoid reporting missing file errors.
		    .transpileOptionValue = Tristate::True, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_stripInternal{
		    .Name = "stripInternal", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .Description = Disable_emitting_declarations_that_have_internal_in_their_JSDoc_comments, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_disableSizeLimit{
		    .Name = "disableSizeLimit", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsProgramStructure = true, 
		    .Category = Editor_Support, 
		    .Description = Remove_the_20mb_cap_on_total_source_code_size_for_JavaScript_files_in_the_TypeScript_language_server, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_disableSourceOfProjectReferenceRedirect{
		    .Name = "disableSourceOfProjectReferenceRedirect", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .IsTSConfigOnly = true, 
		    .Category = Projects, 
		    .Description = Disable_preferring_source_files_instead_of_declaration_files_when_referencing_composite_projects, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_disableSolutionSearching{
		    .Name = "disableSolutionSearching", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .IsTSConfigOnly = true, 
		    .Category = Projects, 
		    .Description = Opt_a_project_out_of_multi_project_reference_checking_when_editing, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_disableReferencedProjectLoad{
		    .Name = "disableReferencedProjectLoad", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .IsTSConfigOnly = true, 
		    .Category = Projects, 
		    .Description = Reduce_the_number_of_projects_loaded_automatically_by_TypeScript, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noEmitHelpers{
		    .Name = "noEmitHelpers", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .Description = Disable_generating_custom_helper_functions_like_extends_in_compiled_output, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_noEmitOnError{
		    .Name = "noEmitOnError", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .transpileOptionValue = Tristate::Unknown, 
		    .Description = Disable_emitting_files_if_any_type_checking_errors_are_reported, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_preserveConstEnums{
		    .Name = "preserveConstEnums", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Emit, 
		    .Description = Disable_erasing_const_enum_declarations_in_generated_code, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_declarationDir{
		    .Name = "declarationDir", 
		    .Kind = CommandLineOptionTypeString, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .AffectsDeclarationPath = true, 
		    .IsFilePath = true, 
		    .Category = Emit, 
		    .transpileOptionValue = Tristate::Unknown, 
		    .Description = Specify_the_output_directory_for_generated_declaration_files, 
		};
		static const CommandLineOption o_skipLibCheck{
		    .Name = "skipLibCheck", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    // We need to store these to determine whether `lib` files need to be rechecked
		    .AffectsBuildInfo = true, 
		    .Category = Completeness, 
		    .Description = Skip_type_checking_all_d_ts_files, 
		    .DefaultValueDescription = false, 
		};
		static const CommandLineOption o_allowUnusedLabels{
		    .Name = "allowUnusedLabels", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsBindDiagnostics = true, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Disable_error_reporting_for_unused_labels, 
		    .DefaultValueDescription = Tristate::Unknown, 
		};
		static const CommandLineOption o_allowUnreachableCode{
		    .Name = "allowUnreachableCode", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsBindDiagnostics = true, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Type_Checking, 
		    .Description = Disable_error_reporting_for_unreachable_code, 
		    .DefaultValueDescription = Tristate::Unknown, 
		};
		static const CommandLineOption o_forceConsistentCasingInFileNames{
		    .Name = "forceConsistentCasingInFileNames", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsModuleResolution = true, 
		    .Category = Interop_Constraints, 
		    .Description = Ensure_that_casing_is_correct_in_imports, 
		    .DefaultValueDescription = true, 
		};
		static const CommandLineOption o_maxNodeModuleJsDepth{
		    .Name = "maxNodeModuleJsDepth", 
		    .Kind = CommandLineOptionTypeNumber, 
		    .AffectsModuleResolution = true, 
		    .Category = JavaScript_Support, 
		    .Description = Specify_the_maximum_folder_depth_used_for_checking_JavaScript_files_from_node_modules_Only_applicable_with_allowJs, 
		    .DefaultValueDescription = 0, 
		};
		static const CommandLineOption o_useDefineForClassFields{
		    .Name = "useDefineForClassFields", 
		    .Kind = CommandLineOptionTypeBoolean, 
		    .AffectsSemanticDiagnostics = true, 
		    .AffectsEmit = true, 
		    .AffectsBuildInfo = true, 
		    .Category = Language_and_Environment, 
		    .Description = Emit_ECMAScript_standard_compliant_class_fields, 
		    .DefaultValueDescription = X_true_for_ES2022_and_above_including_ESNext, 
		};
		static const CommandLineOption o_plugins{
		    // A list of plugins to load in the language service
		    .Name = "plugins", 
		    .Kind = CommandLineOptionTypeList, 
		    .IsTSConfigOnly = true, 
		    .Description = Specify_a_list_of_language_service_plugins_to_include, 
		    .Category = Editor_Support, 
		};
		static const CommandLineOption o_moduleDetection{
		    .Name = "moduleDetection", 
		    .Kind = CommandLineOptionTypeEnum, 
		    .AffectsSourceFile = true, 
		    .AffectsModuleResolution = true, 
		    .Description = Control_what_method_is_used_to_detect_module_format_JS_files, 
		    .Category = Language_and_Environment, 
		    .DefaultValueDescription = X_auto_Colon_Treat_files_with_imports_exports_import_meta_jsx_with_jsx_Colon_react_jsx_or_esm_format_with_module_Colon_node16_as_modules, 
		};
		static const CommandLineOption o_ignoreDeprecations{
		    .Name = "ignoreDeprecations", 
		    .Kind = CommandLineOptionTypeString, 
		    .DefaultValueDescription = Tristate::Unknown, 
		};
		return std::vector<const CommandLineOption*>{
			&o_all,
			&o_version,
			&o_init,
			&o_project,
			&o_showConfig,
			&o_listFilesOnly,
			&o_ignoreConfig,
			&o_target,
			&o_module,
			&o_lib,
			&o_allowJs,
			&o_checkJs,
			&o_jsx,
			&o_outFile,
			&o_outDir,
			&o_rootDir,
			&o_composite,
			&o_tsBuildInfoFile,
			&o_removeComments,
			&o_importHelpers,
			&o_downlevelIteration,
			&o_isolatedModules,
			&o_verbatimModuleSyntax,
			&o_isolatedDeclarations,
			&o_erasableSyntaxOnly,
			&o_libReplacement,
			&o_strict,
			&o_noImplicitAny,
			&o_strictNullChecks,
			&o_strictFunctionTypes,
			&o_strictBindCallApply,
			&o_strictPropertyInitialization,
			&o_strictBuiltinIteratorReturn,
			&o_noImplicitThis,
			&o_useUnknownInCatchVariables,
			&o_alwaysStrict,
			&o_stableTypeOrdering,
			&o_noUnusedLocals,
			&o_noUnusedParameters,
			&o_exactOptionalPropertyTypes,
			&o_noImplicitReturns,
			&o_noFallthroughCasesInSwitch,
			&o_noUncheckedIndexedAccess,
			&o_noImplicitOverride,
			&o_noPropertyAccessFromIndexSignature,
			&o_moduleResolution,
			&o_baseUrl,
			&o_paths,
			&o_rootDirs,
			&o_typeRoots,
			&o_types,
			&o_allowSyntheticDefaultImports,
			&o_esModuleInterop,
			&o_preserveSymlinks,
			&o_allowUmdGlobalAccess,
			&o_moduleSuffixes,
			&o_allowImportingTsExtensions,
			&o_rewriteRelativeImportExtensions,
			&o_resolvePackageJsonExports,
			&o_resolvePackageJsonImports,
			&o_customConditions,
			&o_noUncheckedSideEffectImports,
			&o_sourceRoot,
			&o_mapRoot,
			&o_inlineSources,
			&o_experimentalDecorators,
			&o_emitDecoratorMetadata,
			&o_jsxFactory,
			&o_jsxFragmentFactory,
			&o_jsxImportSource,
			&o_resolveJsonModule,
			&o_allowArbitraryExtensions,
			&o_reactNamespace,
			&o_skipDefaultLibCheck,
			&o_emitBOM,
			&o_newLine,
			&o_noErrorTruncation,
			&o_noLib,
			&o_noResolve,
			&o_stripInternal,
			&o_disableSizeLimit,
			&o_disableSourceOfProjectReferenceRedirect,
			&o_disableSolutionSearching,
			&o_disableReferencedProjectLoad,
			&o_noEmitHelpers,
			&o_noEmitOnError,
			&o_preserveConstEnums,
			&o_declarationDir,
			&o_skipLibCheck,
			&o_allowUnusedLabels,
			&o_allowUnreachableCode,
			&o_forceConsistentCasingInFileNames,
			&o_maxNodeModuleJsDepth,
			&o_useDefineForClassFields,
			&o_plugins,
			&o_moduleDetection,
			&o_ignoreDeprecations,
		};
	}();
	return v;
}

const std::vector<const CommandLineOption*>& optionsForCompiler()
{
	return optionsForCompilerData();
}


// OptionsDeclarations — declscompiler.go:11. slices.Concat.
const std::vector<const CommandLineOption*>& OptionsDeclarations() {
	static const std::vector<const CommandLineOption*> v = [] {
		std::vector<const CommandLineOption*> v = commonOptionsWithBuild();
		v.insert(v.end(), optionsForCompiler().begin(),
		         optionsForCompiler().end());
		return v;
	}();
	return v;
}

// compilerOptionFieldInfos — implicit-slice replacement for Go's
// optionsType reflect.TypeFor[core.CompilerOptions](). Declared order
// matches the Go struct so field positions line up.
const std::vector<compilerOptionFieldInfo>& compilerOptionFieldInfos() {
	static const std::vector<compilerOptionFieldInfo> v{
	{"AllowJs", "allowJs",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->AllowJs; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->AllowJs = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->AllowJs == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->AllowJs = Tristate::Unknown; }},
	{"AllowArbitraryExtensions", "allowArbitraryExtensions",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->AllowArbitraryExtensions; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->AllowArbitraryExtensions = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->AllowArbitraryExtensions == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->AllowArbitraryExtensions = Tristate::Unknown; }},
	{"AllowImportingTsExtensions", "allowImportingTsExtensions",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->AllowImportingTsExtensions; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->AllowImportingTsExtensions = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->AllowImportingTsExtensions == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->AllowImportingTsExtensions = Tristate::Unknown; }},
	{"AllowNonTsExtensions", "allowNonTsExtensions",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->AllowNonTsExtensions; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->AllowNonTsExtensions = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->AllowNonTsExtensions == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->AllowNonTsExtensions = Tristate::Unknown; }},
	{"AllowUmdGlobalAccess", "allowUmdGlobalAccess",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->AllowUmdGlobalAccess; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->AllowUmdGlobalAccess = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->AllowUmdGlobalAccess == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->AllowUmdGlobalAccess = Tristate::Unknown; }},
	{"AllowUnreachableCode", "allowUnreachableCode",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->AllowUnreachableCode; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->AllowUnreachableCode = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->AllowUnreachableCode == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->AllowUnreachableCode = Tristate::Unknown; }},
	{"AllowUnusedLabels", "allowUnusedLabels",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->AllowUnusedLabels; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->AllowUnusedLabels = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->AllowUnusedLabels == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->AllowUnusedLabels = Tristate::Unknown; }},
	{"AssumeChangesOnlyAffectDirectDependencies", "assumeChangesOnlyAffectDirectDependencies",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->AssumeChangesOnlyAffectDirectDependencies; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->AssumeChangesOnlyAffectDirectDependencies = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->AssumeChangesOnlyAffectDirectDependencies == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->AssumeChangesOnlyAffectDirectDependencies = Tristate::Unknown; }},
	{"CheckJs", "checkJs",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->CheckJs; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->CheckJs = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->CheckJs == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->CheckJs = Tristate::Unknown; }},
	{"CustomConditions", "customConditions",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return JsonStrList(o->CustomConditions); },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->CustomConditions = std::get<JsonStrList>(v.v); },
	 [](const CompilerOptions* o) { return o->CustomConditions.empty(); },
	 [](CompilerOptions* o) { o->CustomConditions.clear(); }},
	{"Composite", "composite",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Composite; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Composite = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Composite == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Composite = Tristate::Unknown; }},
	{"EmitDeclarationOnly", "emitDeclarationOnly",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->EmitDeclarationOnly; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->EmitDeclarationOnly = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->EmitDeclarationOnly == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->EmitDeclarationOnly = Tristate::Unknown; }},
	{"EmitBOM", "emitBOM",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->EmitBOM; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->EmitBOM = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->EmitBOM == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->EmitBOM = Tristate::Unknown; }},
	{"EmitDecoratorMetadata", "emitDecoratorMetadata",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->EmitDecoratorMetadata; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->EmitDecoratorMetadata = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->EmitDecoratorMetadata == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->EmitDecoratorMetadata = Tristate::Unknown; }},
	{"Declaration", "declaration",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Declaration; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Declaration = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Declaration == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Declaration = Tristate::Unknown; }},
	{"DeclarationDir", "declarationDir",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->DeclarationDir; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->DeclarationDir = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->DeclarationDir.empty(); },
	 [](CompilerOptions* o) { o->DeclarationDir.clear(); }},
	{"DeclarationMap", "declarationMap",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->DeclarationMap; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->DeclarationMap = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->DeclarationMap == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->DeclarationMap = Tristate::Unknown; }},
	{"DeduplicatePackages", "deduplicatePackages",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->DeduplicatePackages; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->DeduplicatePackages = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->DeduplicatePackages == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->DeduplicatePackages = Tristate::Unknown; }},
	{"DisableSizeLimit", "disableSizeLimit",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->DisableSizeLimit; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->DisableSizeLimit = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->DisableSizeLimit == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->DisableSizeLimit = Tristate::Unknown; }},
	{"DisableSourceOfProjectReferenceRedirect", "disableSourceOfProjectReferenceRedirect",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->DisableSourceOfProjectReferenceRedirect; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->DisableSourceOfProjectReferenceRedirect = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->DisableSourceOfProjectReferenceRedirect == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->DisableSourceOfProjectReferenceRedirect = Tristate::Unknown; }},
	{"DisableSolutionSearching", "disableSolutionSearching",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->DisableSolutionSearching; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->DisableSolutionSearching = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->DisableSolutionSearching == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->DisableSolutionSearching = Tristate::Unknown; }},
	{"DisableReferencedProjectLoad", "disableReferencedProjectLoad",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->DisableReferencedProjectLoad; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->DisableReferencedProjectLoad = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->DisableReferencedProjectLoad == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->DisableReferencedProjectLoad = Tristate::Unknown; }},
	{"ErasableSyntaxOnly", "erasableSyntaxOnly",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ErasableSyntaxOnly; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ErasableSyntaxOnly = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ErasableSyntaxOnly == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ErasableSyntaxOnly = Tristate::Unknown; }},
	{"ExactOptionalPropertyTypes", "exactOptionalPropertyTypes",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ExactOptionalPropertyTypes; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ExactOptionalPropertyTypes = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ExactOptionalPropertyTypes == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ExactOptionalPropertyTypes = Tristate::Unknown; }},
	{"ExperimentalDecorators", "experimentalDecorators",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ExperimentalDecorators; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ExperimentalDecorators = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ExperimentalDecorators == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ExperimentalDecorators = Tristate::Unknown; }},
	{"ForceConsistentCasingInFileNames", "forceConsistentCasingInFileNames",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ForceConsistentCasingInFileNames; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ForceConsistentCasingInFileNames = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ForceConsistentCasingInFileNames == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ForceConsistentCasingInFileNames = Tristate::Unknown; }},
	{"IsolatedModules", "isolatedModules",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->IsolatedModules; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->IsolatedModules = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->IsolatedModules == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->IsolatedModules = Tristate::Unknown; }},
	{"IsolatedDeclarations", "isolatedDeclarations",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->IsolatedDeclarations; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->IsolatedDeclarations = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->IsolatedDeclarations == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->IsolatedDeclarations = Tristate::Unknown; }},
	{"IgnoreConfig", "ignoreConfig",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->IgnoreConfig; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->IgnoreConfig = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->IgnoreConfig == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->IgnoreConfig = Tristate::Unknown; }},
	{"IgnoreDeprecations", "ignoreDeprecations",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->IgnoreDeprecations; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->IgnoreDeprecations = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->IgnoreDeprecations.empty(); },
	 [](CompilerOptions* o) { o->IgnoreDeprecations.clear(); }},
	{"ImportHelpers", "importHelpers",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ImportHelpers; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ImportHelpers = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ImportHelpers == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ImportHelpers = Tristate::Unknown; }},
	{"InlineSourceMap", "inlineSourceMap",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->InlineSourceMap; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->InlineSourceMap = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->InlineSourceMap == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->InlineSourceMap = Tristate::Unknown; }},
	{"InlineSources", "inlineSources",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->InlineSources; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->InlineSources = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->InlineSources == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->InlineSources = Tristate::Unknown; }},
	{"Init", "init",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Init; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Init = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Init == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Init = Tristate::Unknown; }},
	{"Incremental", "incremental",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Incremental; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Incremental = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Incremental == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Incremental = Tristate::Unknown; }},
	{"Jsx", "jsx",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Jsx; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Jsx = static_cast<JsxEmit>(std::get<int64_t>(v.v)); },
	 [](const CompilerOptions* o) { return o->Jsx == JsxEmit::None || static_cast<int32_t>(o->Jsx) == 0; },
	 [](CompilerOptions* o) { o->Jsx = JsxEmit(0); }},
	{"JsxFactory", "jsxFactory",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->JsxFactory; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->JsxFactory = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->JsxFactory.empty(); },
	 [](CompilerOptions* o) { o->JsxFactory.clear(); }},
	{"JsxFragmentFactory", "jsxFragmentFactory",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->JsxFragmentFactory; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->JsxFragmentFactory = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->JsxFragmentFactory.empty(); },
	 [](CompilerOptions* o) { o->JsxFragmentFactory.clear(); }},
	{"JsxImportSource", "jsxImportSource",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->JsxImportSource; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->JsxImportSource = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->JsxImportSource.empty(); },
	 [](CompilerOptions* o) { o->JsxImportSource.clear(); }},
	{"Lib", "lib",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return JsonStrList(o->Lib); },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Lib = std::get<JsonStrList>(v.v); },
	 [](const CompilerOptions* o) { return o->Lib.empty(); },
	 [](CompilerOptions* o) { o->Lib.clear(); }},
	{"LibReplacement", "libReplacement",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->LibReplacement; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->LibReplacement = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->LibReplacement == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->LibReplacement = Tristate::Unknown; }},
	{"Locale", "locale",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Locale; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Locale = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->Locale.empty(); },
	 [](CompilerOptions* o) { o->Locale.clear(); }},
	{"MapRoot", "mapRoot",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->MapRoot; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->MapRoot = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->MapRoot.empty(); },
	 [](CompilerOptions* o) { o->MapRoot.clear(); }},
	{"Module", "module",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Module; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Module = static_cast<ModuleKind>(std::get<int64_t>(v.v)); },
	 [](const CompilerOptions* o) { return o->Module == ModuleKind::None || static_cast<int32_t>(o->Module) == 0; },
	 [](CompilerOptions* o) { o->Module = ModuleKind(0); }},
	{"ModuleResolution", "moduleResolution",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ModuleResolution; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ModuleResolution = static_cast<ModuleResolutionKind>(std::get<int64_t>(v.v)); },
	 [](const CompilerOptions* o) { return o->ModuleResolution == ModuleResolutionKind::Unknown || static_cast<int32_t>(o->ModuleResolution) == 0; },
	 [](CompilerOptions* o) { o->ModuleResolution = ModuleResolutionKind(0); }},
	{"ModuleSuffixes", "moduleSuffixes",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return JsonStrList(o->ModuleSuffixes); },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ModuleSuffixes = std::get<JsonStrList>(v.v); },
	 [](const CompilerOptions* o) { return o->ModuleSuffixes.empty(); },
	 [](CompilerOptions* o) { o->ModuleSuffixes.clear(); }},
	{"ModuleDetection", "moduleDetection",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ModuleDetection; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ModuleDetection = static_cast<ModuleDetectionKind>(std::get<int64_t>(v.v)); },
	 [](const CompilerOptions* o) { return o->ModuleDetection == ModuleDetectionKind::None || static_cast<int32_t>(o->ModuleDetection) == 0; },
	 [](CompilerOptions* o) { o->ModuleDetection = ModuleDetectionKind(0); }},
	{"NewLine", "newLine",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NewLine; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NewLine = static_cast<NewLineKind>(std::get<int64_t>(v.v)); },
	 [](const CompilerOptions* o) { return o->NewLine == NewLineKind::None || static_cast<int32_t>(o->NewLine) == 0; },
	 [](CompilerOptions* o) { o->NewLine = NewLineKind(0); }},
	{"NoEmit", "noEmit",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoEmit; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoEmit = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoEmit == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoEmit = Tristate::Unknown; }},
	{"NoCheck", "noCheck",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoCheck; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoCheck = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoCheck == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoCheck = Tristate::Unknown; }},
	{"NoErrorTruncation", "noErrorTruncation",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoErrorTruncation; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoErrorTruncation = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoErrorTruncation == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoErrorTruncation = Tristate::Unknown; }},
	{"NoFallthroughCasesInSwitch", "noFallthroughCasesInSwitch",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoFallthroughCasesInSwitch; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoFallthroughCasesInSwitch = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoFallthroughCasesInSwitch == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoFallthroughCasesInSwitch = Tristate::Unknown; }},
	{"NoImplicitAny", "noImplicitAny",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoImplicitAny; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoImplicitAny = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoImplicitAny == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoImplicitAny = Tristate::Unknown; }},
	{"NoImplicitThis", "noImplicitThis",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoImplicitThis; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoImplicitThis = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoImplicitThis == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoImplicitThis = Tristate::Unknown; }},
	{"NoImplicitReturns", "noImplicitReturns",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoImplicitReturns; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoImplicitReturns = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoImplicitReturns == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoImplicitReturns = Tristate::Unknown; }},
	{"NoEmitHelpers", "noEmitHelpers",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoEmitHelpers; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoEmitHelpers = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoEmitHelpers == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoEmitHelpers = Tristate::Unknown; }},
	{"NoLib", "noLib",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoLib; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoLib = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoLib == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoLib = Tristate::Unknown; }},
	{"NoPropertyAccessFromIndexSignature", "noPropertyAccessFromIndexSignature",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoPropertyAccessFromIndexSignature; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoPropertyAccessFromIndexSignature = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoPropertyAccessFromIndexSignature == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoPropertyAccessFromIndexSignature = Tristate::Unknown; }},
	{"NoUncheckedIndexedAccess", "noUncheckedIndexedAccess",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoUncheckedIndexedAccess; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoUncheckedIndexedAccess = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoUncheckedIndexedAccess == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoUncheckedIndexedAccess = Tristate::Unknown; }},
	{"NoEmitOnError", "noEmitOnError",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoEmitOnError; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoEmitOnError = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoEmitOnError == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoEmitOnError = Tristate::Unknown; }},
	{"NoUnusedLocals", "noUnusedLocals",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoUnusedLocals; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoUnusedLocals = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoUnusedLocals == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoUnusedLocals = Tristate::Unknown; }},
	{"NoUnusedParameters", "noUnusedParameters",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoUnusedParameters; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoUnusedParameters = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoUnusedParameters == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoUnusedParameters = Tristate::Unknown; }},
	{"NoResolve", "noResolve",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoResolve; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoResolve = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoResolve == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoResolve = Tristate::Unknown; }},
	{"NoImplicitOverride", "noImplicitOverride",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoImplicitOverride; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoImplicitOverride = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoImplicitOverride == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoImplicitOverride = Tristate::Unknown; }},
	{"NoUncheckedSideEffectImports", "noUncheckedSideEffectImports",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoUncheckedSideEffectImports; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoUncheckedSideEffectImports = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoUncheckedSideEffectImports == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoUncheckedSideEffectImports = Tristate::Unknown; }},
	{"OutDir", "outDir",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->OutDir; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->OutDir = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->OutDir.empty(); },
	 [](CompilerOptions* o) { o->OutDir.clear(); }},
	{"Plugins", "plugins",
	 [](const CompilerOptions* o) -> CompilerOptionsValue {
			JsonArray arr;
			arr.reserve(o->Plugins.size());
			for (const auto& p : o->Plugins) {
				auto m = std::make_shared<JsonObject>(1);
				m->Set("name", p.name);
				arr.emplace_back(m);
			}
			return arr;
		},
	 [](CompilerOptions* o, const CompilerOptionsValue& v) {
			o->Plugins.clear();
			if (auto* p = std::get_if<JsonArray>(&v.v)) {
				for (const auto& item : *p) {
					PluginImport pi;
					if (auto* mp = std::get_if<JsonObjectPtr>(&item.v)) {
						auto nameV = (*mp)->GetOrZero("name");
						if (auto* n =
						        std::get_if<std::string>(&nameV.v)) {
							pi.name = *n;
						}
					}
					o->Plugins.push_back(std::move(pi));
				}
			}
		},
	 [](const CompilerOptions* o) { return o->Plugins.empty(); },
	 [](CompilerOptions* o) { o->Plugins.clear(); }},
	{"PreserveConstEnums", "preserveConstEnums",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->PreserveConstEnums; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->PreserveConstEnums = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->PreserveConstEnums == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->PreserveConstEnums = Tristate::Unknown; }},
	{"PreserveSymlinks", "preserveSymlinks",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->PreserveSymlinks; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->PreserveSymlinks = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->PreserveSymlinks == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->PreserveSymlinks = Tristate::Unknown; }},
	{"Project", "project",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Project; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Project = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->Project.empty(); },
	 [](CompilerOptions* o) { o->Project.clear(); }},
	{"ResolveJsonModule", "resolveJsonModule",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ResolveJsonModule; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ResolveJsonModule = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ResolveJsonModule == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ResolveJsonModule = Tristate::Unknown; }},
	{"ResolvePackageJsonExports", "resolvePackageJsonExports",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ResolvePackageJsonExports; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ResolvePackageJsonExports = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ResolvePackageJsonExports == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ResolvePackageJsonExports = Tristate::Unknown; }},
	{"ResolvePackageJsonImports", "resolvePackageJsonImports",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ResolvePackageJsonImports; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ResolvePackageJsonImports = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ResolvePackageJsonImports == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ResolvePackageJsonImports = Tristate::Unknown; }},
	{"RemoveComments", "removeComments",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->RemoveComments; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->RemoveComments = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->RemoveComments == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->RemoveComments = Tristate::Unknown; }},
	{"RewriteRelativeImportExtensions", "rewriteRelativeImportExtensions",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->RewriteRelativeImportExtensions; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->RewriteRelativeImportExtensions = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->RewriteRelativeImportExtensions == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->RewriteRelativeImportExtensions = Tristate::Unknown; }},
	{"ReactNamespace", "reactNamespace",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ReactNamespace; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ReactNamespace = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->ReactNamespace.empty(); },
	 [](CompilerOptions* o) { o->ReactNamespace.clear(); }},
	{"RootDir", "rootDir",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->RootDir; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->RootDir = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->RootDir.empty(); },
	 [](CompilerOptions* o) { o->RootDir.clear(); }},
	{"RootDirs", "rootDirs",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return JsonStrList(o->RootDirs); },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->RootDirs = std::get<JsonStrList>(v.v); },
	 [](const CompilerOptions* o) { return o->RootDirs.empty(); },
	 [](CompilerOptions* o) { o->RootDirs.clear(); }},
	{"SkipLibCheck", "skipLibCheck",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->SkipLibCheck; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->SkipLibCheck = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->SkipLibCheck == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->SkipLibCheck = Tristate::Unknown; }},
	{"StableTypeOrdering", "stableTypeOrdering",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->StableTypeOrdering; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->StableTypeOrdering = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->StableTypeOrdering == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->StableTypeOrdering = Tristate::Unknown; }},
	{"Strict", "strict",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Strict; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Strict = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Strict == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Strict = Tristate::Unknown; }},
	{"StrictBindCallApply", "strictBindCallApply",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->StrictBindCallApply; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->StrictBindCallApply = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->StrictBindCallApply == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->StrictBindCallApply = Tristate::Unknown; }},
	{"StrictBuiltinIteratorReturn", "strictBuiltinIteratorReturn",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->StrictBuiltinIteratorReturn; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->StrictBuiltinIteratorReturn = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->StrictBuiltinIteratorReturn == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->StrictBuiltinIteratorReturn = Tristate::Unknown; }},
	{"StrictFunctionTypes", "strictFunctionTypes",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->StrictFunctionTypes; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->StrictFunctionTypes = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->StrictFunctionTypes == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->StrictFunctionTypes = Tristate::Unknown; }},
	{"StrictNullChecks", "strictNullChecks",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->StrictNullChecks; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->StrictNullChecks = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->StrictNullChecks == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->StrictNullChecks = Tristate::Unknown; }},
	{"StrictPropertyInitialization", "strictPropertyInitialization",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->StrictPropertyInitialization; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->StrictPropertyInitialization = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->StrictPropertyInitialization == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->StrictPropertyInitialization = Tristate::Unknown; }},
	{"StripInternal", "stripInternal",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->StripInternal; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->StripInternal = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->StripInternal == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->StripInternal = Tristate::Unknown; }},
	{"SkipDefaultLibCheck", "skipDefaultLibCheck",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->SkipDefaultLibCheck; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->SkipDefaultLibCheck = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->SkipDefaultLibCheck == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->SkipDefaultLibCheck = Tristate::Unknown; }},
	{"SourceMap", "sourceMap",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->SourceMap; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->SourceMap = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->SourceMap == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->SourceMap = Tristate::Unknown; }},
	{"SourceRoot", "sourceRoot",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->SourceRoot; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->SourceRoot = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->SourceRoot.empty(); },
	 [](CompilerOptions* o) { o->SourceRoot.clear(); }},
	{"SuppressOutputPathCheck", "suppressOutputPathCheck",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->SuppressOutputPathCheck; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->SuppressOutputPathCheck = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->SuppressOutputPathCheck == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->SuppressOutputPathCheck = Tristate::Unknown; }},
	{"Target", "target",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Target; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Target = static_cast<ScriptTarget>(std::get<int64_t>(v.v)); },
	 [](const CompilerOptions* o) { return o->Target == ScriptTarget::None || static_cast<int32_t>(o->Target) == 0; },
	 [](CompilerOptions* o) { o->Target = ScriptTarget(0); }},
	{"TraceResolution", "traceResolution",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->TraceResolution; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->TraceResolution = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->TraceResolution == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->TraceResolution = Tristate::Unknown; }},
	{"TsBuildInfoFile", "tsBuildInfoFile",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->TsBuildInfoFile; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->TsBuildInfoFile = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->TsBuildInfoFile.empty(); },
	 [](CompilerOptions* o) { o->TsBuildInfoFile.clear(); }},
	{"TypeRoots", "typeRoots",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return JsonStrList(o->TypeRoots); },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->TypeRoots = std::get<JsonStrList>(v.v); },
	 [](const CompilerOptions* o) { return o->TypeRoots.empty(); },
	 [](CompilerOptions* o) { o->TypeRoots.clear(); }},
	{"Types", "types",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return JsonStrList(o->Types); },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Types = std::get<JsonStrList>(v.v); },
	 [](const CompilerOptions* o) { return o->Types.empty(); },
	 [](CompilerOptions* o) { o->Types.clear(); }},
	{"UseDefineForClassFields", "useDefineForClassFields",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->UseDefineForClassFields; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->UseDefineForClassFields = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->UseDefineForClassFields == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->UseDefineForClassFields = Tristate::Unknown; }},
	{"UseUnknownInCatchVariables", "useUnknownInCatchVariables",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->UseUnknownInCatchVariables; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->UseUnknownInCatchVariables = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->UseUnknownInCatchVariables == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->UseUnknownInCatchVariables = Tristate::Unknown; }},
	{"VerbatimModuleSyntax", "verbatimModuleSyntax",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->VerbatimModuleSyntax; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->VerbatimModuleSyntax = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->VerbatimModuleSyntax == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->VerbatimModuleSyntax = Tristate::Unknown; }},
	{"MaxNodeModuleJsDepth", "maxNodeModuleJsDepth",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->MaxNodeModuleJsDepth != nullptr ? CompilerOptionsValue(int64_t(*o->MaxNodeModuleJsDepth)) : CompilerOptionsValue(); },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { if (auto* p = std::get_if<int64_t>(&v.v)) { if (o->MaxNodeModuleJsDepth == nullptr) { o->MaxNodeModuleJsDepth = new int(static_cast<int>(*p)); } else { *o->MaxNodeModuleJsDepth = static_cast<int>(*p); } } else { o->MaxNodeModuleJsDepth = nullptr; } },
	 [](const CompilerOptions* o) { return o->MaxNodeModuleJsDepth == nullptr; },
	 [](CompilerOptions* o) { o->MaxNodeModuleJsDepth = nullptr; }},
	{"AllowSyntheticDefaultImports", "allowSyntheticDefaultImports",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->AllowSyntheticDefaultImports; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->AllowSyntheticDefaultImports = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->AllowSyntheticDefaultImports == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->AllowSyntheticDefaultImports = Tristate::Unknown; }},
	{"AlwaysStrict", "alwaysStrict",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->AlwaysStrict; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->AlwaysStrict = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->AlwaysStrict == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->AlwaysStrict = Tristate::Unknown; }},
	{"BaseUrl", "baseUrl",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->BaseUrl; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->BaseUrl = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->BaseUrl.empty(); },
	 [](CompilerOptions* o) { o->BaseUrl.clear(); }},
	{"DownlevelIteration", "downlevelIteration",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->DownlevelIteration; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->DownlevelIteration = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->DownlevelIteration == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->DownlevelIteration = Tristate::Unknown; }},
	{"ESModuleInterop", "esModuleInterop",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ESModuleInterop; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ESModuleInterop = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ESModuleInterop == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ESModuleInterop = Tristate::Unknown; }},
	{"OutFile", "outFile",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->OutFile; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->OutFile = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->OutFile.empty(); },
	 [](CompilerOptions* o) { o->OutFile.clear(); }},
	{"ConfigFilePath", "configFilePath",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ConfigFilePath; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ConfigFilePath = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->ConfigFilePath.empty(); },
	 [](CompilerOptions* o) { o->ConfigFilePath.clear(); }},
	{"NoDtsResolution", "noDtsResolution",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoDtsResolution; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoDtsResolution = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoDtsResolution == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoDtsResolution = Tristate::Unknown; }},
	{"PathsBasePath", "pathsBasePath",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->PathsBasePath; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->PathsBasePath = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->PathsBasePath.empty(); },
	 [](CompilerOptions* o) { o->PathsBasePath.clear(); }},
	{"Diagnostics", "diagnostics",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Diagnostics; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Diagnostics = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Diagnostics == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Diagnostics = Tristate::Unknown; }},
	{"ExtendedDiagnostics", "extendedDiagnostics",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ExtendedDiagnostics; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ExtendedDiagnostics = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ExtendedDiagnostics == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ExtendedDiagnostics = Tristate::Unknown; }},
	{"GenerateCpuProfile", "generateCpuProfile",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->GenerateCpuProfile; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->GenerateCpuProfile = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->GenerateCpuProfile.empty(); },
	 [](CompilerOptions* o) { o->GenerateCpuProfile.clear(); }},
	{"GenerateTrace", "generateTrace",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->GenerateTrace; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->GenerateTrace = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->GenerateTrace.empty(); },
	 [](CompilerOptions* o) { o->GenerateTrace.clear(); }},
	{"ListEmittedFiles", "listEmittedFiles",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ListEmittedFiles; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ListEmittedFiles = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ListEmittedFiles == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ListEmittedFiles = Tristate::Unknown; }},
	{"ListFiles", "listFiles",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ListFiles; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ListFiles = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ListFiles == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ListFiles = Tristate::Unknown; }},
	{"ExplainFiles", "explainFiles",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ExplainFiles; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ExplainFiles = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ExplainFiles == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ExplainFiles = Tristate::Unknown; }},
	{"ListFilesOnly", "listFilesOnly",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ListFilesOnly; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ListFilesOnly = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ListFilesOnly == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ListFilesOnly = Tristate::Unknown; }},
	{"NoEmitForJsFiles", "noEmitForJsFiles",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->NoEmitForJsFiles; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->NoEmitForJsFiles = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->NoEmitForJsFiles == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->NoEmitForJsFiles = Tristate::Unknown; }},
	{"PreserveWatchOutput", "preserveWatchOutput",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->PreserveWatchOutput; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->PreserveWatchOutput = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->PreserveWatchOutput == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->PreserveWatchOutput = Tristate::Unknown; }},
	{"Pretty", "pretty",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Pretty; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Pretty = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Pretty == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Pretty = Tristate::Unknown; }},
	{"Version", "version",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Version; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Version = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Version == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Version = Tristate::Unknown; }},
	{"Watch", "watch",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Watch; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Watch = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Watch == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Watch = Tristate::Unknown; }},
	{"ShowConfig", "showConfig",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->ShowConfig; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->ShowConfig = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->ShowConfig == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->ShowConfig = Tristate::Unknown; }},
	{"Build", "build",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Build; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Build = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Build == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Build = Tristate::Unknown; }},
	{"Help", "help",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Help; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Help = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Help == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Help = Tristate::Unknown; }},
	{"All", "all",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->All; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->All = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->All == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->All = Tristate::Unknown; }},
	{"RunExternalCode", "runExternalCode",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->RunExternalCode; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->RunExternalCode = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->RunExternalCode == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->RunExternalCode = Tristate::Unknown; }},
	{"PprofDir", "pprofDir",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->PprofDir; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->PprofDir = std::get<std::string>(v.v); },
	 [](const CompilerOptions* o) { return o->PprofDir.empty(); },
	 [](CompilerOptions* o) { o->PprofDir.clear(); }},
	{"SingleThreaded", "singleThreaded",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->SingleThreaded; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->SingleThreaded = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->SingleThreaded == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->SingleThreaded = Tristate::Unknown; }},
	{"Quiet", "quiet",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Quiet; },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { o->Quiet = std::get<Tristate>(v.v); },
	 [](const CompilerOptions* o) { return o->Quiet == Tristate::Unknown; },
	 [](CompilerOptions* o) { o->Quiet = Tristate::Unknown; }},
	{"Checkers", "checkers",
	 [](const CompilerOptions* o) -> CompilerOptionsValue { return o->Checkers != nullptr ? CompilerOptionsValue(int64_t(*o->Checkers)) : CompilerOptionsValue(); },
	 [](CompilerOptions* o, const CompilerOptionsValue& v) { if (auto* p = std::get_if<int64_t>(&v.v)) { if (o->Checkers == nullptr) { o->Checkers = new int(static_cast<int>(*p)); } else { *o->Checkers = static_cast<int>(*p); } } else { o->Checkers = nullptr; } },
	 [](const CompilerOptions* o) { return o->Checkers == nullptr; },
	 [](CompilerOptions* o) { o->Checkers = nullptr; }},
	};
	return v;
}

// optionsHaveChanges — declscompiler.go:1213.
bool optionsHaveChanges(
    const CompilerOptions* oldOptions, const CompilerOptions* newOptions,
    const std::function<bool(const CommandLineOption*)>& declFilter) {
	if (oldOptions == newOptions) {
		return false;
	}
	if (oldOptions == nullptr || newOptions == nullptr) {
		return true;
	}
	return ForEachCompilerOptionValue(
	    newOptions, declFilter,
	    [&](const CommandLineOption* option,
	        const CompilerOptionsValue& newValue, int i) -> bool {
		    const auto& info = compilerOptionFieldInfos()[i];
		    CompilerOptionsValue oldValue = info.get(oldOptions);
		    if (option->strictFlag) {
			    return oldOptions->GetStrictOptionValue(
			               std::get<Tristate>(oldValue.v)) !=
			           newOptions->GetStrictOptionValue(
			               std::get<Tristate>(newValue.v));
		    }
		    if (option->allowJsFlag) {
			    return oldOptions->GetAllowJS() != newOptions->GetAllowJS();
		    }
		    return !jsonDeepEqual(newValue, oldValue);
	    });
}

// ForEachCompilerOptionValue — declscompiler.go:1234.
bool ForEachCompilerOptionValue(
    const CompilerOptions* options,
    const std::function<bool(const CommandLineOption*)>& declFilter,
    const std::function<bool(const CommandLineOption*,
                             const CompilerOptionsValue&, int)>& fn) {
	const auto& infos = compilerOptionFieldInfos();
	for (int i = 0; i < (int)infos.size(); i++) {
		const auto& info = infos[i];
		const CommandLineOption* optionDeclaration =
		    CommandLineCompilerOptionsMap().Get(info.name);
		if (optionDeclaration != nullptr && declFilter(optionDeclaration)) {
			if (fn(optionDeclaration, info.get(options), i)) {
				return true;
			}
		}
	}
	return false;
}

// CompilerOptionsAffectSemanticDiagnostics — declscompiler.go:1250.
bool CompilerOptionsAffectSemanticDiagnostics(const CompilerOptions* oldOptions,
                                              const CompilerOptions* newOptions) {
	return optionsHaveChanges(
	    oldOptions, newOptions, [](const CommandLineOption* option) {
		    return option->AffectsSemanticDiagnostics;
	    });
}

// CompilerOptionsAffectDeclarationPath — declscompiler.go:1259.
bool CompilerOptionsAffectDeclarationPath(const CompilerOptions* oldOptions,
                                          const CompilerOptions* newOptions) {
	return optionsHaveChanges(
	    oldOptions, newOptions, [](const CommandLineOption* option) {
		    return option->AffectsDeclarationPath;
	    });
}

// CompilerOptionsAffectEmit — declscompiler.go:1268.
bool CompilerOptionsAffectEmit(const CompilerOptions* oldOptions,
                               const CompilerOptions* newOptions) {
	return optionsHaveChanges(
	    oldOptions, newOptions, [](const CommandLineOption* option) {
		    return option->AffectsEmit;
	    });
}

}  // namespace tsc::tsoptions
