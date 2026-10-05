// Port of tsc/internal/tsoptions/declswatch.go — OptionsForWatch.
#include "internal/tsoptions/tsoptions.h"

#include "internal/diagnostics/messages_generated.h"

namespace tsc::tsoptions {

const std::vector<const CommandLineOption*>& OptionsForWatch() {
	static const std::vector<const CommandLineOption*> v = [] {
		static const CommandLineOption watchInterval{
		    .Name = "watchInterval",
		    .Kind = CommandLineOptionTypeNumber,
		    .Category = Watch_and_Build_Modes,
		};
		static const CommandLineOption watchFile{
		    .Name = "watchFile",
		    .Kind = CommandLineOptionTypeEnum,
		    // new Map(Object.entries({
		    //     fixedpollinginterval: WatchFileKind.FixedPollingInterval,
		    //     prioritypollinginterval: WatchFileKind.PriorityPollingInterval,
		    //     dynamicprioritypolling: WatchFileKind.DynamicPriorityPolling,
		    //     fixedchunksizepolling: WatchFileKind.FixedChunkSizePolling,
		    //     usefsevents: WatchFileKind.UseFsEvents,
		    //     usefseventsonparentdirectory: WatchFileKind.UseFsEventsOnParentDirectory,
		    // })),
		    .Description =
		        Specify_how_the_TypeScript_watch_mode_works,
		    .DefaultValueDescription = WatchFileKind::UseFsEvents,
		    .Category = Watch_and_Build_Modes,
		};
		static const CommandLineOption watchDirectory{
		    .Name = "watchDirectory",
		    .Kind = CommandLineOptionTypeEnum,
		    // new Map(Object.entries({
		    //     usefsevents: WatchDirectoryKind.UseFsEvents,
		    //     fixedpollinginterval: WatchDirectoryKind.FixedPollingInterval,
		    //     dynamicprioritypolling: WatchDirectoryKind.DynamicPriorityPolling,
		    //     fixedchunksizepolling: WatchDirectoryKind.FixedChunkSizePolling,
		    // })),
		    .Description =
		        Specify_how_directories_are_watched_on_systems_that_lack_recursive_file_watching_functionality,
		    .DefaultValueDescription = WatchDirectoryKind::UseFsEvents,
		    .Category = Watch_and_Build_Modes,
		};
		static const CommandLineOption fallbackPolling{
		    .Name = "fallbackPolling",
		    .Kind = CommandLineOptionTypeEnum,
		    // new Map(Object.entries({
		    //     fixedinterval: PollingWatchKind.FixedInterval,
		    //     priorityinterval: PollingWatchKind.PriorityInterval,
		    //     dynamicpriority: PollingWatchKind.DynamicPriority,
		    //     fixedchunksize: PollingWatchKind.FixedChunkSize,
		    // })),
		    .Description =
		        Specify_what_approach_the_watcher_should_use_if_the_system_runs_out_of_native_file_watchers,
		    .DefaultValueDescription = PollingKind::PriorityInterval,
		    .Category = Watch_and_Build_Modes,
		};
		static const CommandLineOption synchronousWatchDirectory{
		    .Name = "synchronousWatchDirectory",
		    .Kind = CommandLineOptionTypeBoolean,
		    .Description =
		        Synchronously_call_callbacks_and_update_the_state_of_directory_watchers_on_platforms_that_don_t_support_recursive_watching_natively,
		    .DefaultValueDescription = false,
		    .Category = Watch_and_Build_Modes,
		};
		static const CommandLineOption excludeDirectories{
		    .Name = "excludeDirectories",
		    .Kind = CommandLineOptionTypeList,
		    // element: {
		    //     Name: "excludeDirectory",
		    //     Kind: "string",
		    //     isFilePath: true,
		    //     extraValidation: specToDiagnostic,
		    // },
		    .Description =
		        Remove_a_list_of_directories_from_the_watch_process,
		    .Category = Watch_and_Build_Modes,
		    .allowConfigDirTemplateSubstitution = true,
		};
		static const CommandLineOption excludeFiles{
		    .Name = "excludeFiles",
		    .Kind = CommandLineOptionTypeList,
		    // element: {
		    //     Name: "excludeFile",
		    //     Kind: "string",
		    //     isFilePath: true,
		    //     extraValidation: specToDiagnostic,
		    // },
		    .Description =
		        Remove_a_list_of_files_from_the_watch_mode_s_processing,
		    .Category = Watch_and_Build_Modes,
		    .allowConfigDirTemplateSubstitution = true,
		};
		return std::vector<const CommandLineOption*>{
			&watchInterval,
			&watchFile,
			&watchDirectory,
			&fallbackPolling,
			&synchronousWatchDirectory,
			&excludeDirectories,
			&excludeFiles,
		};
	}();
	return v;
}

}  // namespace tsc::tsoptions
