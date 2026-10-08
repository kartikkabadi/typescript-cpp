// Port of tsc/internal/tsoptions/commandlineoption.go — CommandLineOption
// kind constants live in tsoptions.h; this TU has the accessor methods and the
// commandLineOptionElements/EnumMap/Deprecated tables.
#include "internal/tsoptions/tsoptions.h"

#include <unordered_map>

namespace tsc::tsoptions {

namespace {

// CommandLineOption.Elements() — commandlineoption.go:104.
const std::unordered_map<std::string, const CommandLineOption*>& commandLineOptionElements() {
	static const std::unordered_map<std::string, const CommandLineOption*> table = [] {
		static const CommandLineOption lib{
			.Name = "lib",
			.Kind = CommandLineOptionTypeEnum,  // libMap,
			.DefaultValueDescription = Tristate::Unknown,
		};
		static const CommandLineOption rootDirs{
			.Name = "rootDirs",
			.Kind = CommandLineOptionTypeString,
			.IsFilePath = true,
		};
		static const CommandLineOption typeRoots{
			.Name = "typeRoots",
			.Kind = CommandLineOptionTypeString,
			.IsFilePath = true,
		};
		static const CommandLineOption types{
			.Name = "types",
			.Kind = CommandLineOptionTypeString,
		};
		static const CommandLineOption moduleSuffixes{
			.Name = "moduleSuffixes",
			.Kind = CommandLineOptionTypeString,
		};
		static const CommandLineOption customConditions{
			.Name = "condition",
			.Kind = CommandLineOptionTypeString,
		};
		static const CommandLineOption plugins{
			.Name = "plugin",
			.Kind = CommandLineOptionTypeObject,
		};
		// For tsconfig root options
		static const CommandLineOption references{
			.Name = "references",
			.Kind = CommandLineOptionTypeObject,
		};
		static const CommandLineOption contentMappers{
			.Name = "contentMappers",
			.Kind = CommandLineOptionTypeObject,
		};
		static const CommandLineOption files{
			.Name = "files",
			.Kind = CommandLineOptionTypeString,
		};
		static const CommandLineOption include{
			.Name = "include",
			.Kind = CommandLineOptionTypeString,
		};
		static const CommandLineOption exclude{
			.Name = "exclude",
			.Kind = CommandLineOptionTypeString,
		};
		static const CommandLineOption extends{
			.Name = "extends",
			.Kind = CommandLineOptionTypeString,
		};
		// For Watch options
		static const CommandLineOption excludeDirectories{
			.Name = "excludeDirectory",
			.Kind = CommandLineOptionTypeString,
			.IsFilePath = true,
			.extraValidation_ = extraValidationSpec,
		};
		static const CommandLineOption excludeFiles{
			.Name = "excludeFile",
			.Kind = CommandLineOptionTypeString,
			.IsFilePath = true,
			.extraValidation_ = extraValidationSpec,
		};
		// Test infra options
		static const CommandLineOption libFiles{
			.Name = "libFiles",
			.Kind = CommandLineOptionTypeString,
		};
		return std::unordered_map<std::string, const CommandLineOption*>{
			{"lib", &lib},
			{"rootDirs", &rootDirs},
			{"typeRoots", &typeRoots},
			{"types", &types},
			{"moduleSuffixes", &moduleSuffixes},
			{"customConditions", &customConditions},
			{"plugins", &plugins},
			{"references", &references},
			{"contentMappers", &contentMappers},
			{"files", &files},
			{"include", &include},
			{"exclude", &exclude},
			{"extends", &extends},
			{"excludeDirectories", &excludeDirectories},
			{"excludeFiles", &excludeFiles},
			{"libFiles", &libFiles},
		};
	}();
	return table;
}

// CommandLineOption.EnumMap() — commandlineoption.go:183.
const std::unordered_map<std::string, const JsonObject*>& commandLineOptionEnumMap() {
	static const std::unordered_map<std::string, const JsonObject*> table = [] {
		return std::unordered_map<std::string, const JsonObject*>{
			{"lib", &libEnumMap()},
			{"moduleResolution", &moduleResolutionOptionMap()},
			{"module", &moduleOptionMap()},
			{"target", &targetOptionMap()},
			{"moduleDetection", &moduleDetectionOptionMap()},
			{"jsx", &jsxOptionMap()},
			{"newLine", &newLineOptionMap()},
		};
	}();
	return table;
}

// CommandLineOption.DeprecatedKeys() — commandlineoption.go:197.
const std::unordered_map<std::string, const collections::Set<std::string>*>& commandLineOptionDeprecated() {
	static const std::unordered_map<std::string, const collections::Set<std::string>*> table = [] {
		static const collections::Set<std::string> module = [] {
			collections::Set<std::string> s;
			s.Add("none");
			s.Add("amd");
			s.Add("system");
			s.Add("umd");
			return s;
		}();
		static const collections::Set<std::string> moduleResolution = [] {
			collections::Set<std::string> s;
			s.Add("node");
			s.Add("classic");
			s.Add("node10");
			return s;
		}();
		static const collections::Set<std::string> target = [] {
			collections::Set<std::string> s;
			s.Add("es5");
			return s;
		}();
		return std::unordered_map<std::string, const collections::Set<std::string>*>{
			{"module", &module},
			{"moduleResolution", &moduleResolution},
			{"target", &target},
		};
	}();
	return table;
}

}  // namespace

const collections::Set<std::string>* CommandLineOption::DeprecatedKeys() const {
	if (Kind != CommandLineOptionTypeEnum) {
		return nullptr;
	}
	auto it = commandLineOptionDeprecated().find(Name);
	return it == commandLineOptionDeprecated().end() ? nullptr : it->second;
}

const JsonObject* CommandLineOption::EnumMap() const {
	if (Kind != CommandLineOptionTypeEnum) {
		return nullptr;
	}
	auto it = commandLineOptionEnumMap().find(Name);
	return it == commandLineOptionEnumMap().end() ? nullptr : it->second;
}

const CommandLineOption* CommandLineOption::Elements() const {
	if (Kind != CommandLineOptionTypeList && Kind != CommandLineOptionTypeListOrElement) {
		return nullptr;
	}
	auto it = commandLineOptionElements().find(Name);
	return it == commandLineOptionElements().end() ? nullptr : it->second;
}

}  // namespace tsc::tsoptions
