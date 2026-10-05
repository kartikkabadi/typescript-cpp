// Port of tsc/internal/tsoptions/declsbuild.go — build-mode option decls.
#include "internal/tsoptions/tsoptions.h"

#include "internal/diagnostics/messages_generated.h"

namespace tsc::tsoptions {

// TscBuildOption — declsbuild.go:9. OptionsForBuild[0] shares this object so
// pointer equality with &TscBuildOption() holds like Go's &TscBuildOption.
const CommandLineOption& TscBuildOption() {
	static const CommandLineOption o{
	    .Name = "build",
	    .ShortName = "b",
	    .Kind = CommandLineOptionTypeBoolean,
	    .Description =
	        Build_one_or_more_projects_and_their_dependencies_if_out_of_date,
	    .DefaultValueDescription = false,
	    .ShowInSimplifiedHelpView = true,
	    .Category = Command_line_Options,
	};
	return o;
}

const std::vector<const CommandLineOption*>& OptionsForBuild() {
	static const std::vector<const CommandLineOption*> v = [] {
		static const CommandLineOption verbose{
		    .Name = "verbose",
		    .ShortName = "v",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Enable_verbose_logging,
		    .DefaultValueDescription = false,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption dry{
		    .Name = "dry",
		    .ShortName = "d",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description =
		        Show_what_would_be_built_or_deleted_if_specified_with_clean,
		    .DefaultValueDescription = false,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption force{
		    .Name = "force",
		    .ShortName = "f",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description =
		        Build_all_projects_including_those_that_appear_to_be_up_to_date,
		    .DefaultValueDescription = false,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption clean{
		    .Name = "clean",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description = Delete_the_outputs_of_all_projects,
		    .DefaultValueDescription = false,
		    .Category = Command_line_Options,
		};
		static const CommandLineOption builders{
		    .Name = "builders",
		    .Kind = CommandLineOptionTypeNumber,
		    .Description =
		        Set_the_number_of_projects_to_build_concurrently,
		    .DefaultValueDescription =
		        X_4_unless_singleThreaded_is_passed,
		    .Category = Command_line_Options,
		    .minValue = 1,
		};
		static const CommandLineOption stopBuildOnErrors{
		    .Name = "stopBuildOnErrors",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description =
		        Skip_building_downstream_projects_on_error_in_upstream_project,
		    .DefaultValueDescription = false,
		    .Category = Command_line_Options,
		};
		return std::vector<const CommandLineOption*>{
			&TscBuildOption(),
			&verbose,
			&dry,
			&force,
			&clean,
			&builders,
			&stopBuildOnErrors,
		};
	}();
	return v;
}

// BuildOpts — declsbuild.go:69. slices.Concat(commonOptionsWithBuild,
// OptionsForBuild).
const std::vector<const CommandLineOption*>& BuildOpts() {
	static const std::vector<const CommandLineOption*> v = [] {
		std::vector<const CommandLineOption*> v = commonOptionsWithBuild();
		v.insert(v.end(), OptionsForBuild().begin(), OptionsForBuild().end());
		return v;
	}();
	return v;
}

}  // namespace tsc::tsoptions
