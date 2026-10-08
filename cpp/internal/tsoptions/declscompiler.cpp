// Port of tsc/internal/tsoptions/declscompiler.go — compiler option
// declaration tables + optionsHaveChanges helpers (reflection replaced by the
// compilerOptionFieldInfos table).
#include "internal/tsoptions/tsoptions.h"

#include "internal/diagnostics/messages_generated.h"

namespace tsc::tsoptions {

static const std::vector<const CommandLineOption*>& commonOptionsWithBuildData()
{
	static const std::vector<const CommandLineOption*> v = [] {
		//******* commonOptionsWithBuild *******
		static const CommandLineOption o_help{
		    .Name = "help",
		    .ShortName = "h",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsCommandLineOnly = true,
		    .Description = Print_this_message,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_help_1{
		    .Name = "help",
		    .ShortName = "?",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsCommandLineOnly = true,
		    .DefaultValueDescription = false,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_watch{
		    .Name = "watch",
		    .ShortName = "w",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsCommandLineOnly = true,
		    .Description = Watch_input_files,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_preserveWatchOutput{
		    .Name = "preserveWatchOutput",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_wiping_the_console_in_watch_mode,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = false,
		    .Category = Output_Formatting,
		};
		static const CommandLineOption o_listFiles{
		    .Name = "listFiles",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Print_all_of_the_files_read_during_the_compilation,
		    .DefaultValueDescription = false,
		    .Category = Compiler_Diagnostics,
		};
		static const CommandLineOption o_explainFiles{
		    .Name = "explainFiles",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Print_files_read_during_the_compilation_including_why_it_was_included,
		    .DefaultValueDescription = false,
		    .Category = Compiler_Diagnostics,
		};
		static const CommandLineOption o_listEmittedFiles{
		    .Name = "listEmittedFiles",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Print_the_names_of_emitted_files_after_a_compilation,
		    .DefaultValueDescription = false,
		    .Category = Compiler_Diagnostics,
		};
		static const CommandLineOption o_pretty{
		    .Name = "pretty",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_color_and_formatting_in_TypeScript_s_output_to_make_compiler_errors_easier_to_read,
		    .DefaultValueDescription = true,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Output_Formatting,
		};
		static const CommandLineOption o_traceResolution{
		    .Name = "traceResolution",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Log_paths_used_during_the_moduleResolution_process,
		    .DefaultValueDescription = false,
		    .Category = Compiler_Diagnostics,
		};
		static const CommandLineOption o_diagnostics{
		    .Name = "diagnostics",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Output_compiler_performance_information_after_building,
		    .DefaultValueDescription = false,
		    .Category = Compiler_Diagnostics,
		};
		static const CommandLineOption o_extendedDiagnostics{
		    .Name = "extendedDiagnostics",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Output_more_detailed_compiler_performance_information_after_building,
		    .DefaultValueDescription = false,
		    .Category = Compiler_Diagnostics,
		};
		static const CommandLineOption o_generateCpuProfile{
		    .Name = "generateCpuProfile",
		    .Kind = CommandLineOptionTypeString,
		    .IsFilePath = true,
		    .Description = Emit_a_v8_CPU_profile_of_the_compiler_run_for_debugging,
		    .DefaultValueDescription = std::string("profile.cpuprofile"),
		    .Category = Compiler_Diagnostics,
		};
		static const CommandLineOption o_generateTrace{
		    .Name = "generateTrace",
		    .Kind = CommandLineOptionTypeString,
		    .IsFilePath = true,
		    .Description = Generates_an_event_trace_and_a_list_of_types,
		    .Category = Compiler_Diagnostics,
		};
		static const CommandLineOption o_incremental{
		    .Name = "incremental",
		    .ShortName = "i",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Save_tsbuildinfo_files_to_allow_for_incremental_compilation_of_projects,
		    .DefaultValueDescription = X_false_unless_composite_is_set,
		    .Category = Projects,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_declaration{
		    .Name = "declaration",
		    .ShortName = "d",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Generate_d_ts_files_from_TypeScript_and_JavaScript_files_in_your_project,
		    .DefaultValueDescription = X_false_unless_composite_is_set,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Emit,
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_declarationMap{
		    .Name = "declarationMap",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Create_sourcemaps_for_d_ts_files,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Emit,
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_emitDeclarationOnly{
		    .Name = "emitDeclarationOnly",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Only_output_d_ts_files_and_not_JavaScript_files,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Emit,
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_sourceMap{
		    .Name = "sourceMap",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Create_source_map_files_for_emitted_JavaScript_files,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Emit,
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_inlineSourceMap{
		    .Name = "inlineSourceMap",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Include_sourcemap_files_inside_the_emitted_JavaScript,
		    .DefaultValueDescription = false,
		    .Category = Emit,
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_noCheck{
		    .Name = "noCheck",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_full_type_checking_only_critical_parse_and_emit_errors_will_be_reported,
		    // Not setting affectsSemanticDiagnostics or affectsBuildInfo because we dont want all diagnostics to go away, its handled in builder
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = false,
		    .Category = Compiler_Diagnostics,
		    .transpileOptionValue = Tristate::True,
		};
		static const CommandLineOption o_deduplicatePackages{
		    .Name = "deduplicatePackages",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Deduplicate_packages_with_the_same_name_and_version,
		    .DefaultValueDescription = true,
		    .Category = Type_Checking,
		    .AffectsProgramStructure = true,
		};
		static const CommandLineOption o_noEmit{
		    .Name = "noEmit",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_emitting_files_from_a_compilation,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Emit,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_assumeChangesOnlyAffectDirectDependencies{
		    .Name = "assumeChangesOnlyAffectDirectDependencies",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Have_recompiles_in_projects_that_use_incremental_and_watch_mode_assume_that_changes_within_a_file_will_only_affect_files_directly_depending_on_it,
		    .DefaultValueDescription = false,
		    .Category = Watch_and_Build_Modes,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_locale{
		    .Name = "locale",
		    .Kind = CommandLineOptionTypeString,
		    .IsCommandLineOnly = true,
		    .Description = Set_the_language_of_the_messaging_from_TypeScript_This_does_not_affect_emit,
		    .DefaultValueDescription = Platform_specific,
		    .Category = Command_line_Options,
		    .extraValidation_ = extraValidationLocale,
		};
		static const CommandLineOption o_quiet{
		    .Name = "quiet",
		    .ShortName = "q",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Do_not_print_diagnostics,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_singleThreaded{
		    .Name = "singleThreaded",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Run_in_single_threaded_mode,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_pprofDir{
		    .Name = "pprofDir",
		    .Kind = CommandLineOptionTypeString,
		    .IsFilePath = true,
		    .Description = Generate_pprof_CPU_Slashmemory_profiles_to_the_given_directory,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_checkers{
		    .Name = "checkers",
		    .Kind = CommandLineOptionTypeNumber,
		    .Description = Set_the_number_of_checkers_per_project,
		    .DefaultValueDescription = X_4_unless_singleThreaded_is_passed,
		    .Category = Command_line_Options,
		    .minValue = 1,
		};
		static const CommandLineOption o_runExternalCode{
		    .Name = "runExternalCode",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsCommandLineOnly = true,
		    .Description = Allow_loading_external_content_mapper_plugins_that_execute_code_during_compilation,
		    .DefaultValueDescription = false,
		    .Category = Command_line_Options,
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

static const std::vector<const CommandLineOption*>& optionsForCompilerData()
{
	static const std::vector<const CommandLineOption*> v = [] {
		//******* compilerOptions not common with --build *******
		// CommandLine only options
		static const CommandLineOption o_all{
		    .Name = "all",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Show_all_compiler_options,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_version{
		    .Name = "version",
		    .ShortName = "v",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Print_the_compiler_s_version,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_init{
		    .Name = "init",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Initializes_a_TypeScript_project_and_creates_a_tsconfig_json_file,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_project{
		    .Name = "project",
		    .ShortName = "p",
		    .Kind = CommandLineOptionTypeString,
		    .IsFilePath = true,
		    .Description = Compile_the_project_given_the_path_to_its_configuration_file_or_to_a_folder_with_a_tsconfig_json,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_showConfig{
		    .Name = "showConfig",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsCommandLineOnly = true,
		    .Description = Print_the_final_configuration_instead_of_building,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_listFilesOnly{
		    .Name = "listFilesOnly",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsCommandLineOnly = true,
		    .Description = Print_names_of_files_that_are_part_of_the_compilation_and_then_stop_processing,
		    .DefaultValueDescription = false,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption o_ignoreConfig{
		    .Name = "ignoreConfig",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsCommandLineOnly = true,
		    .Description = Ignore_the_tsconfig_found_and_build_with_commandline_options_and_files,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Command_line_Options,
		};
		// Basic
		// targetOptionDeclaration,
		static const CommandLineOption o_target{
		    .Name = "target",
		    .ShortName = "t",
		    .Kind = CommandLineOptionTypeEnum,
		    .Description = Set_the_JavaScript_language_version_for_emitted_JavaScript_and_include_compatible_library_declarations,
		    .DefaultValueDescription = ScriptTarget::LatestStandard,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Language_and_Environment,
		    .AffectsBuildInfo = true,
		    // targetOptionMap
		    .AffectsSourceFile = true,
		    .AffectsModuleResolution = true,
		    .AffectsEmit = true,
		};
		// moduleOptionDeclaration,
		static const CommandLineOption o_module{
		    .Name = "module",
		    .ShortName = "m",
		    .Kind = CommandLineOptionTypeEnum,
		    .Description = Specify_what_module_code_is_generated,
		    .DefaultValueDescription = Tristate::Unknown,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Modules,
		    .AffectsBuildInfo = true,
		    // moduleOptionMap
		    .AffectsModuleResolution = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_lib{
		    .Name = "lib",
		    .Kind = CommandLineOptionTypeList,
		    .Description = Specify_a_set_of_bundled_library_declaration_files_that_describe_the_target_runtime_environment,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Language_and_Environment,
		    // elements: &CommandLineOption{
		    // 	name:                    "lib",
		    // 	kind:                   CommandLineOptionTypeEnum, // libMap,
		    // 	defaultValueDescription: core.TSUnknown,
		    // },
		    .AffectsProgramStructure = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_allowJs{
		    .Name = "allowJs",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Allow_JavaScript_files_to_be_a_part_of_your_program_Use_the_checkJs_option_to_get_errors_from_these_files,
		    .DefaultValueDescription = X_false_unless_checkJs_is_set,
		    .ShowInSimplifiedHelpView = true,
		    .Category = JavaScript_Support,
		    .AffectsBuildInfo = true,
		    .allowJsFlag = true,
		};
		static const CommandLineOption o_checkJs{
		    .Name = "checkJs",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_error_reporting_in_type_checked_JavaScript_files,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = JavaScript_Support,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsModuleResolution = true,
		};
		static const CommandLineOption o_jsx{
		    .Name = "jsx",
		    .Kind = CommandLineOptionTypeEnum,
		    .Description = Specify_what_JSX_code_is_generated,
		    .DefaultValueDescription = Tristate::Unknown,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Language_and_Environment,
		    // The checker emits an error when it sees JSX but this option is not set in compilerOptions.
		    // This is effectively a semantic error, so mark this option as affecting semantic diagnostics
		    // so we know to refresh errors when this option is changed.
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    // jsxOptionMap,
		    .AffectsSourceFile = true,
		    .AffectsModuleResolution = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_outFile{
		    .Name = "outFile",
		    .Kind = CommandLineOptionTypeString,
		    .IsFilePath = true,
		    .Description = Specify_a_file_that_bundles_all_outputs_into_one_JavaScript_file_If_declaration_is_true_also_designates_a_file_that_bundles_all_d_ts_output,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Emit,
		    .AffectsDeclarationPath = true,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_outDir{
		    .Name = "outDir",
		    .Kind = CommandLineOptionTypeString,
		    .IsFilePath = true,
		    .Description = Specify_an_output_folder_for_all_emitted_files,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Emit,
		    .AffectsDeclarationPath = true,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_rootDir{
		    .Name = "rootDir",
		    .Kind = CommandLineOptionTypeString,
		    .IsFilePath = true,
		    .Description = Specify_the_root_folder_within_your_source_files,
		    .DefaultValueDescription = Computed_from_the_list_of_input_files,
		    .Category = Modules,
		    .AffectsDeclarationPath = true,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_composite{
		    .Name = "composite",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsTSConfigOnly = true,
		    .Description = Enable_constraints_that_allow_a_TypeScript_project_to_be_used_with_project_references,
		    .DefaultValueDescription = false,
		    .Category = Projects,
		    // Not setting affectsEmit because we calculate this flag might not affect full emit
		    .AffectsBuildInfo = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_tsBuildInfoFile{
		    .Name = "tsBuildInfoFile",
		    .Kind = CommandLineOptionTypeString,
		    .IsFilePath = true,
		    .Description = Specify_the_path_to_tsbuildinfo_incremental_compilation_file,
		    .DefaultValueDescription = std::string(".tsbuildinfo"),
		    .Category = Projects,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_removeComments{
		    .Name = "removeComments",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_emitting_comments,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_importHelpers{
		    .Name = "importHelpers",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Allow_importing_helper_functions_from_tslib_once_per_project_instead_of_including_them_per_file,
		    .DefaultValueDescription = false,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsSourceFile = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_downlevelIteration{
		    .Name = "downlevelIteration",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Emit_more_compliant_but_verbose_and_less_performant_JavaScript_for_iteration,
		    .DefaultValueDescription = false,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_isolatedModules{
		    .Name = "isolatedModules",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Ensure_that_each_file_can_be_safely_transpiled_without_relying_on_other_imports,
		    .DefaultValueDescription = false,
		    .Category = Interop_Constraints,
		    .transpileOptionValue = Tristate::True,
		};
		static const CommandLineOption o_verbatimModuleSyntax{
		    .Name = "verbatimModuleSyntax",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Do_not_transform_or_elide_any_imports_or_exports_not_marked_as_type_only_ensuring_they_are_written_in_the_output_file_s_format_based_on_the_module_setting,
		    .DefaultValueDescription = false,
		    .Category = Interop_Constraints,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_isolatedDeclarations{
		    .Name = "isolatedDeclarations",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Require_sufficient_annotation_on_exports_so_other_tools_can_trivially_generate_declaration_files,
		    .DefaultValueDescription = false,
		    .Category = Interop_Constraints,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_erasableSyntaxOnly{
		    .Name = "erasableSyntaxOnly",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Do_not_allow_runtime_constructs_that_are_not_part_of_ECMAScript,
		    .DefaultValueDescription = false,
		    .Category = Interop_Constraints,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_libReplacement{
		    .Name = "libReplacement",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_lib_replacement,
		    .DefaultValueDescription = false,
		    .Category = Language_and_Environment,
		    .AffectsProgramStructure = true,
		};
		// Strict Type Checks
		static const CommandLineOption o_strict{
		    .Name = "strict",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_all_strict_type_checking_options,
		    .DefaultValueDescription = true,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Type_Checking,
		    // Though this affects semantic diagnostics, affectsSemanticDiagnostics is not set here
		    // The value of each strictFlag depends on own strictFlag value or this and never accessed directly.
		    // But we need to store `strict` in builf info, even though it won't be examined directly, so that the
		    // flags it controls (e.g. `strictNullChecks`) will be retrieved correctly
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_noImplicitAny{
		    .Name = "noImplicitAny",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_error_reporting_for_expressions_and_declarations_with_an_implied_any_type,
		    .DefaultValueDescription = X_true_unless_strict_is_false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .strictFlag = true,
		};
		static const CommandLineOption o_strictNullChecks{
		    .Name = "strictNullChecks",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = When_type_checking_take_into_account_null_and_undefined,
		    .DefaultValueDescription = X_true_unless_strict_is_false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .strictFlag = true,
		};
		static const CommandLineOption o_strictFunctionTypes{
		    .Name = "strictFunctionTypes",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = When_assigning_functions_check_to_ensure_parameters_and_the_return_values_are_subtype_compatible,
		    .DefaultValueDescription = X_true_unless_strict_is_false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .strictFlag = true,
		};
		static const CommandLineOption o_strictBindCallApply{
		    .Name = "strictBindCallApply",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Check_that_the_arguments_for_bind_call_and_apply_methods_match_the_original_function,
		    .DefaultValueDescription = X_true_unless_strict_is_false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .strictFlag = true,
		};
		static const CommandLineOption o_strictPropertyInitialization{
		    .Name = "strictPropertyInitialization",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Check_for_class_properties_that_are_declared_but_not_set_in_the_constructor,
		    .DefaultValueDescription = X_true_unless_strict_is_false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .strictFlag = true,
		};
		static const CommandLineOption o_strictBuiltinIteratorReturn{
		    .Name = "strictBuiltinIteratorReturn",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Built_in_iterators_are_instantiated_with_a_TReturn_type_of_undefined_instead_of_any,
		    .DefaultValueDescription = X_true_unless_strict_is_false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .strictFlag = true,
		};
		static const CommandLineOption o_noImplicitThis{
		    .Name = "noImplicitThis",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_error_reporting_when_this_is_given_the_type_any,
		    .DefaultValueDescription = X_true_unless_strict_is_false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .strictFlag = true,
		};
		static const CommandLineOption o_useUnknownInCatchVariables{
		    .Name = "useUnknownInCatchVariables",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Default_catch_clause_variables_as_unknown_instead_of_any,
		    .DefaultValueDescription = X_true_unless_strict_is_false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .strictFlag = true,
		};
		static const CommandLineOption o_alwaysStrict{
		    .Name = "alwaysStrict",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Ensure_use_strict_is_always_emitted,
		    .DefaultValueDescription = true,
		    .Category = Type_Checking,
		    .AffectsBuildInfo = true,
		    .AffectsSourceFile = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_stableTypeOrdering{
		    .Name = "stableTypeOrdering",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Ensure_types_are_ordered_stably_and_deterministically_across_compilations,
		    .DefaultValueDescription = true,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		// Additional Checks
		static const CommandLineOption o_noUnusedLocals{
		    .Name = "noUnusedLocals",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_error_reporting_when_local_variables_aren_t_read,
		    .DefaultValueDescription = false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_noUnusedParameters{
		    .Name = "noUnusedParameters",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Raise_an_error_when_a_function_parameter_isn_t_read,
		    .DefaultValueDescription = false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_exactOptionalPropertyTypes{
		    .Name = "exactOptionalPropertyTypes",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Interpret_optional_property_types_as_written_rather_than_adding_undefined,
		    .DefaultValueDescription = false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_noImplicitReturns{
		    .Name = "noImplicitReturns",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_error_reporting_for_codepaths_that_do_not_explicitly_return_in_a_function,
		    .DefaultValueDescription = false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_noFallthroughCasesInSwitch{
		    .Name = "noFallthroughCasesInSwitch",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_error_reporting_for_fallthrough_cases_in_switch_statements,
		    .DefaultValueDescription = false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsBindDiagnostics = true,
		};
		static const CommandLineOption o_noUncheckedIndexedAccess{
		    .Name = "noUncheckedIndexedAccess",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Add_undefined_to_a_type_when_accessed_using_an_index,
		    .DefaultValueDescription = false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_noImplicitOverride{
		    .Name = "noImplicitOverride",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Ensure_overriding_members_in_derived_classes_are_marked_with_an_override_modifier,
		    .DefaultValueDescription = false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_noPropertyAccessFromIndexSignature{
		    .Name = "noPropertyAccessFromIndexSignature",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enforces_using_indexed_accessors_for_keys_declared_using_an_indexed_type,
		    .DefaultValueDescription = false,
		    .ShowInSimplifiedHelpView = false,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		// Module Resolution
		static const CommandLineOption o_moduleResolution{
		    .Name = "moduleResolution",
		    .Kind = CommandLineOptionTypeEnum,
		    .Description = Specify_how_TypeScript_looks_up_a_file_from_a_given_module_specifier,
		    .DefaultValueDescription = X_nodenext_if_module_is_nodenext_node16_if_module_is_node16_or_node18_otherwise_bundler,
		    .Category = Modules,
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
		};
		static const CommandLineOption o_baseUrl{
		    .Name = "baseUrl",
		    .Kind = CommandLineOptionTypeString,
		    .IsFilePath = true,
		    .Description = Specify_the_base_directory_to_resolve_non_relative_module_names,
		    .Category = Modules,
		    .AffectsModuleResolution = true,
		};
		static const CommandLineOption o_paths{
		    // this option can only be specified in tsconfig.json
		    // use type = object to copy the value as-is
		    .Name = "paths",
		    .Kind = CommandLineOptionTypeObject,
		    .IsTSConfigOnly = true,
		    .Description = Specify_a_set_of_entries_that_re_map_imports_to_additional_lookup_locations,
		    .Category = Modules,
		    .allowConfigDirTemplateSubstitution = true,
		    .AffectsModuleResolution = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_rootDirs{
		    // this option can only be specified in tsconfig.json
		    // use type = object to copy the value as-is
		    .Name = "rootDirs",
		    .Kind = CommandLineOptionTypeList,
		    .IsTSConfigOnly = true,
		    .Description = Allow_multiple_folders_to_be_treated_as_one_when_resolving_modules,
		    .DefaultValueDescription = Computed_from_the_list_of_input_files,
		    .Category = Modules,
		    .allowConfigDirTemplateSubstitution = true,
		    .AffectsModuleResolution = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_typeRoots{
		    .Name = "typeRoots",
		    .Kind = CommandLineOptionTypeList,
		    .Description = Specify_multiple_folders_that_act_like_Slashnode_modules_Slash_types,
		    .Category = Modules,
		    .allowConfigDirTemplateSubstitution = true,
		    .AffectsModuleResolution = true,
		};
		static const CommandLineOption o_types{
		    .Name = "types",
		    .Kind = CommandLineOptionTypeList,
		    .Description = Specify_type_package_names_to_be_included_without_being_referenced_in_a_source_file,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Modules,
		    .AffectsProgramStructure = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_allowSyntheticDefaultImports{
		    .Name = "allowSyntheticDefaultImports",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Allow_import_x_from_y_when_a_module_doesn_t_have_a_default_export,
		    .DefaultValueDescription = true,
		    .Category = Interop_Constraints,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_esModuleInterop{
		    .Name = "esModuleInterop",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Emit_additional_JavaScript_to_ease_support_for_importing_CommonJS_modules_This_enables_allowSyntheticDefaultImports_for_type_compatibility,
		    .DefaultValueDescription = true,
		    .ShowInSimplifiedHelpView = true,
		    .Category = Interop_Constraints,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_preserveSymlinks{
		    .Name = "preserveSymlinks",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_resolving_symlinks_to_their_realpath_This_correlates_to_the_same_flag_in_node,
		    .DefaultValueDescription = false,
		    .Category = Interop_Constraints,
		};
		static const CommandLineOption o_allowUmdGlobalAccess{
		    .Name = "allowUmdGlobalAccess",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Allow_accessing_UMD_globals_from_modules,
		    .DefaultValueDescription = false,
		    .Category = Modules,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_moduleSuffixes{
		    .Name = "moduleSuffixes",
		    .Kind = CommandLineOptionTypeList,
		    .Description = List_of_file_name_suffixes_to_search_when_resolving_a_module,
		    .Category = Modules,
		    .AffectsModuleResolution = true,
		    .listPreserveFalsyValues = true,
		};
		static const CommandLineOption o_allowImportingTsExtensions{
		    .Name = "allowImportingTsExtensions",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Allow_imports_to_include_TypeScript_file_extensions_Requires_moduleResolution_bundler_and_either_noEmit_or_emitDeclarationOnly_to_be_set,
		    .DefaultValueDescription = false,
		    .Category = Modules,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_rewriteRelativeImportExtensions{
		    .Name = "rewriteRelativeImportExtensions",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Rewrite_ts_tsx_mts_and_cts_file_extensions_in_relative_import_paths_to_their_JavaScript_equivalent_in_output_files,
		    .DefaultValueDescription = false,
		    .Category = Modules,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_resolvePackageJsonExports{
		    .Name = "resolvePackageJsonExports",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Use_the_package_json_exports_field_when_resolving_package_imports,
		    .DefaultValueDescription = X_true_when_moduleResolution_is_node16_nodenext_or_bundler_otherwise_false,
		    .Category = Modules,
		    .AffectsModuleResolution = true,
		};
		static const CommandLineOption o_resolvePackageJsonImports{
		    .Name = "resolvePackageJsonImports",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Use_the_package_json_imports_field_when_resolving_imports,
		    .DefaultValueDescription = X_true_when_moduleResolution_is_node16_nodenext_or_bundler_otherwise_false,
		    .Category = Modules,
		    .AffectsModuleResolution = true,
		};
		static const CommandLineOption o_customConditions{
		    .Name = "customConditions",
		    .Kind = CommandLineOptionTypeList,
		    .Description = Conditions_to_set_in_addition_to_the_resolver_specific_defaults_when_resolving_imports,
		    .Category = Modules,
		    .AffectsModuleResolution = true,
		};
		static const CommandLineOption o_noUncheckedSideEffectImports{
		    .Name = "noUncheckedSideEffectImports",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Check_side_effect_imports,
		    .DefaultValueDescription = true,
		    .Category = Modules,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		// Source Maps
		static const CommandLineOption o_sourceRoot{
		    .Name = "sourceRoot",
		    .Kind = CommandLineOptionTypeString,
		    .Description = Specify_the_root_path_for_debuggers_to_find_the_reference_source_code,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_mapRoot{
		    .Name = "mapRoot",
		    .Kind = CommandLineOptionTypeString,
		    .Description = Specify_the_location_where_debugger_should_locate_map_files_instead_of_generated_locations,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_inlineSources{
		    .Name = "inlineSources",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Include_source_code_in_the_sourcemaps_inside_the_emitted_JavaScript,
		    .DefaultValueDescription = false,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		// Experimental
		static const CommandLineOption o_experimentalDecorators{
		    .Name = "experimentalDecorators",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_experimental_support_for_legacy_experimental_decorators,
		    .DefaultValueDescription = false,
		    .Category = Language_and_Environment,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_emitDecoratorMetadata{
		    .Name = "emitDecoratorMetadata",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Emit_design_type_metadata_for_decorated_declarations_in_source_files,
		    .DefaultValueDescription = false,
		    .Category = Language_and_Environment,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		// Advanced
		static const CommandLineOption o_jsxFactory{
		    .Name = "jsxFactory",
		    .Kind = CommandLineOptionTypeString,
		    .Description = Specify_the_JSX_factory_function_used_when_targeting_React_JSX_emit_e_g_React_createElement_or_h,
		    .DefaultValueDescription = std::string("`React.createElement`"),
		    .Category = Language_and_Environment,
		};
		static const CommandLineOption o_jsxFragmentFactory{
		    .Name = "jsxFragmentFactory",
		    .Kind = CommandLineOptionTypeString,
		    .Description = Specify_the_JSX_Fragment_reference_used_for_fragments_when_targeting_React_JSX_emit_e_g_React_Fragment_or_Fragment,
		    .DefaultValueDescription = std::string("React.Fragment"),
		    .Category = Language_and_Environment,
		};
		static const CommandLineOption o_jsxImportSource{
		    .Name = "jsxImportSource",
		    .Kind = CommandLineOptionTypeString,
		    .Description = Specify_module_specifier_used_to_import_the_JSX_factory_functions_when_using_jsx_Colon_react_jsx_Asterisk,
		    .DefaultValueDescription = std::string("react"),
		    .Category = Language_and_Environment,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsSourceFile = true,
		    .AffectsModuleResolution = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_resolveJsonModule{
		    .Name = "resolveJsonModule",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_importing_json_files,
		    .DefaultValueDescription = false,
		    .Category = Modules,
		    .AffectsModuleResolution = true,
		};
		static const CommandLineOption o_allowArbitraryExtensions{
		    .Name = "allowArbitraryExtensions",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_importing_files_with_any_extension_provided_a_declaration_file_is_present,
		    .DefaultValueDescription = false,
		    .Category = Modules,
		    .AffectsProgramStructure = true,
		};
		static const CommandLineOption o_reactNamespace{
		    .Name = "reactNamespace",
		    .Kind = CommandLineOptionTypeString,
		    .Description = Specify_the_object_invoked_for_createElement_This_only_applies_when_targeting_react_JSX_emit,
		    .DefaultValueDescription = std::string("`React`"),
		    .Category = Language_and_Environment,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_skipDefaultLibCheck{
		    .Name = "skipDefaultLibCheck",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Skip_type_checking_d_ts_files_that_are_included_with_TypeScript,
		    .DefaultValueDescription = false,
		    .Category = Completeness,
		    // We need to store these to determine whether `lib` files need to be rechecked
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_emitBOM{
		    .Name = "emitBOM",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Emit_a_UTF_8_Byte_Order_Mark_BOM_in_the_beginning_of_output_files,
		    .DefaultValueDescription = false,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_newLine{
		    .Name = "newLine",
		    .Kind = CommandLineOptionTypeEnum,
		    .Description = Set_the_newline_character_for_emitting_files,
		    .DefaultValueDescription = std::string("lf"),
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    // newLineOptionMap,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_noErrorTruncation{
		    .Name = "noErrorTruncation",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_truncating_types_in_error_messages,
		    .DefaultValueDescription = false,
		    .Category = Output_Formatting,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_noLib{
		    .Name = "noLib",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_including_any_library_files_including_the_default_lib_d_ts,
		    .DefaultValueDescription = false,
		    .Category = Language_and_Environment,
		    .AffectsProgramStructure = true,
		    // We are not returning a sourceFile for lib file when asked by the program,
		    // so pass --noLib to avoid reporting a file not found error.
		    .transpileOptionValue = Tristate::True,
		};
		static const CommandLineOption o_noResolve{
		    .Name = "noResolve",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disallow_import_s_require_s_or_reference_s_from_expanding_the_number_of_files_TypeScript_should_add_to_a_project,
		    .DefaultValueDescription = false,
		    .Category = Modules,
		    .AffectsModuleResolution = true,
		    // We are not doing a full typecheck, we are not resolving the whole context,
		    // so pass --noResolve to avoid reporting missing file errors.
		    .transpileOptionValue = Tristate::True,
		};
		static const CommandLineOption o_stripInternal{
		    .Name = "stripInternal",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_emitting_declarations_that_have_internal_in_their_JSDoc_comments,
		    .DefaultValueDescription = false,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_disableSizeLimit{
		    .Name = "disableSizeLimit",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Remove_the_20mb_cap_on_total_source_code_size_for_JavaScript_files_in_the_TypeScript_language_server,
		    .DefaultValueDescription = false,
		    .Category = Editor_Support,
		    .AffectsProgramStructure = true,
		};
		static const CommandLineOption o_disableSourceOfProjectReferenceRedirect{
		    .Name = "disableSourceOfProjectReferenceRedirect",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsTSConfigOnly = true,
		    .Description = Disable_preferring_source_files_instead_of_declaration_files_when_referencing_composite_projects,
		    .DefaultValueDescription = false,
		    .Category = Projects,
		};
		static const CommandLineOption o_disableSolutionSearching{
		    .Name = "disableSolutionSearching",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsTSConfigOnly = true,
		    .Description = Opt_a_project_out_of_multi_project_reference_checking_when_editing,
		    .DefaultValueDescription = false,
		    .Category = Projects,
		};
		static const CommandLineOption o_disableReferencedProjectLoad{
		    .Name = "disableReferencedProjectLoad",
		    .Kind = CommandLineOptionTypeBoolean,
		    .IsTSConfigOnly = true,
		    .Description = Reduce_the_number_of_projects_loaded_automatically_by_TypeScript,
		    .DefaultValueDescription = false,
		    .Category = Projects,
		};
		static const CommandLineOption o_noEmitHelpers{
		    .Name = "noEmitHelpers",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_generating_custom_helper_functions_like_extends_in_compiled_output,
		    .DefaultValueDescription = false,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_noEmitOnError{
		    .Name = "noEmitOnError",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_emitting_files_if_any_type_checking_errors_are_reported,
		    .DefaultValueDescription = false,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_preserveConstEnums{
		    .Name = "preserveConstEnums",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_erasing_const_enum_declarations_in_generated_code,
		    .DefaultValueDescription = false,
		    .Category = Emit,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		};
		static const CommandLineOption o_declarationDir{
		    .Name = "declarationDir",
		    .Kind = CommandLineOptionTypeString,
		    .IsFilePath = true,
		    .Description = Specify_the_output_directory_for_generated_declaration_files,
		    .Category = Emit,
		    .AffectsDeclarationPath = true,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
		    .transpileOptionValue = Tristate::Unknown,
		};
		static const CommandLineOption o_skipLibCheck{
		    .Name = "skipLibCheck",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Skip_type_checking_all_d_ts_files,
		    .DefaultValueDescription = false,
		    .Category = Completeness,
		    // We need to store these to determine whether `lib` files need to be rechecked
		    .AffectsBuildInfo = true,
		};
		static const CommandLineOption o_allowUnusedLabels{
		    .Name = "allowUnusedLabels",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_error_reporting_for_unused_labels,
		    .DefaultValueDescription = Tristate::Unknown,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsBindDiagnostics = true,
		};
		static const CommandLineOption o_allowUnreachableCode{
		    .Name = "allowUnreachableCode",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Disable_error_reporting_for_unreachable_code,
		    .DefaultValueDescription = Tristate::Unknown,
		    .Category = Type_Checking,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsBindDiagnostics = true,
		};
		static const CommandLineOption o_forceConsistentCasingInFileNames{
		    .Name = "forceConsistentCasingInFileNames",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Ensure_that_casing_is_correct_in_imports,
		    .DefaultValueDescription = true,
		    .Category = Interop_Constraints,
		    .AffectsModuleResolution = true,
		};
		static const CommandLineOption o_maxNodeModuleJsDepth{
		    .Name = "maxNodeModuleJsDepth",
		    .Kind = CommandLineOptionTypeNumber,
		    .Description = Specify_the_maximum_folder_depth_used_for_checking_JavaScript_files_from_node_modules_Only_applicable_with_allowJs,
		    .DefaultValueDescription = 0,
		    .Category = JavaScript_Support,
		    .AffectsModuleResolution = true,
		};
		static const CommandLineOption o_useDefineForClassFields{
		    .Name = "useDefineForClassFields",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Emit_ECMAScript_standard_compliant_class_fields,
		    .DefaultValueDescription = X_true_for_ES2022_and_above_including_ESNext,
		    .Category = Language_and_Environment,
		    .AffectsSemanticDiagnostics = true,
		    .AffectsBuildInfo = true,
		    .AffectsEmit = true,
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
		    .Description = Control_what_method_is_used_to_detect_module_format_JS_files,
		    .DefaultValueDescription = X_auto_Colon_Treat_files_with_imports_exports_import_meta_jsx_with_jsx_Colon_react_jsx_or_esm_format_with_module_Colon_node16_as_modules,
		    .Category = Language_and_Environment,
		    .AffectsSourceFile = true,
		    .AffectsModuleResolution = true,
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
		auto opts = optionsForCompiler();
		v.insert(v.end(), opts.begin(), opts.end());
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
	// === slice: incremental ===
	{"Paths", "paths",
	 [](const CompilerOptions* o) -> CompilerOptionsValue {
			auto m = std::make_shared<JsonObject>(o->Paths.size());
			for (const auto& [key, subs] : o->Paths) {
				m->Set(key, subs);
			}
			return CompilerOptionsValue{JsonObjectPtr(m)};
		},
	 [](CompilerOptions* o, const CompilerOptionsValue& v) {
			o->Paths.clear();
			if (auto* mp = std::get_if<JsonObjectPtr>(&v.v)) {
				for (const auto& key : (*mp)->Keys()) {
					auto [value, ok] = (*mp)->Get(key);
					if (ok) {
						o->Paths.emplace_back(
						    key, ParseStringArray(*value));
					}
				}
			}
		},
	 [](const CompilerOptions* o) { return o->Paths.empty(); },
	 [](CompilerOptions* o) { o->Paths.clear(); }},
	// === end slice: incremental ===
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
	 [](CompilerOptions* o, const CompilerOptionsValue& v) {
		 o->Types = std::get<JsonStrList>(v.v);
		 o->TypesWasSet = true;
	 },
	 // Go IsZero on []string is a nil check: an explicitly-set empty
	 // `types` is NOT zero.
	 [](const CompilerOptions* o) { return !o->TypesWasSet; },
	 [](CompilerOptions* o) {
		 o->Types.clear();
		 o->TypesWasSet = false;
	 }},
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
