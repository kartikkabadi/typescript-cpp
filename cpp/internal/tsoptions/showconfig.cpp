// Port of tsc/internal/tsoptions/showconfig.go.
#include "internal/tsoptions/tsoptions.h"

#include "internal/diagnostics/messages_generated.h"
#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

namespace {

// impliedOption — showconfig.go:23. `compute` wraps a typed getter so it can
// be stored as func(*core.CompilerOptions) any (computeFn — showconfig.go:15).
struct impliedOption {
	std::string_view name;
	std::vector<std::string_view> dependencies;
	CompilerOptionsValue (*compute)(const CompilerOptions*);
};

template <typename F>
CompilerOptionsValue (*computeFn(F fn))(const CompilerOptions*) {
	return fn;
}

// impliedOptions — showconfig.go:35.
const std::vector<impliedOption>& impliedOptions() {
	static const std::vector<impliedOption> v = {
	    {.name = "Module",
	     .dependencies = {"Target"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetEmitModuleKind();
	     }},
	    {.name = "ModuleResolution",
	     .dependencies = {"Module", "Target"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetModuleResolutionKind();
	     }},
	    {.name = "ModuleDetection",
	     .dependencies = {"Module", "Target"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetEmitModuleDetectionKind();
	     }},
	    {.name = "IsolatedModules",
	     .dependencies = {"VerbatimModuleSyntax"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetIsolatedModules();
	     }},
	    {.name = "PreserveConstEnums",
	     .dependencies = {"IsolatedModules", "VerbatimModuleSyntax"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->ShouldPreserveConstEnums();
	     }},
	    {.name = "Declaration",
	     .dependencies = {"Composite"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetEmitDeclarations();
	     }},
	    {.name = "DeclarationMap",
	     .dependencies = {"Declaration", "Composite"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetAreDeclarationMapsEnabled();
	     }},
	    {.name = "Incremental",
	     .dependencies = {"Composite"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->IsIncremental();
	     }},
	    {.name = "UseDefineForClassFields",
	     .dependencies = {"Target", "Module"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetUseDefineForClassFields();
	     }},
	    {.name = "ResolvePackageJsonExports",
	     .dependencies = {"ModuleResolution", "Module", "Target"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetResolvePackageJsonExports();
	     }},
	    {.name = "ResolvePackageJsonImports",
	     .dependencies = {"ModuleResolution", "ResolvePackageJsonExports",
	                     "Module", "Target"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetResolvePackageJsonImports();
	     }},
	    {.name = "ResolveJsonModule",
	     .dependencies = {"ModuleResolution", "Module", "Target"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetResolveJsonModule();
	     }},
	    {.name = "AllowJs",
	     .dependencies = {"CheckJs"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetAllowJS();
	     }},
	    {.name = "AllowImportingTsExtensions",
	     .dependencies = {"RewriteRelativeImportExtensions"},
	     .compute =
	         +[](const CompilerOptions* o) -> CompilerOptionsValue {
	         return o->GetAllowImportingTsExtensions();
	     }},
	};
	return v;
}

// getNameOfCompilerOptionValue — showconfig.go:153.
std::string getNameOfCompilerOptionValue(const CompilerOptionsValue& value,
                                         const JsonObject* enumMap) {
	for (const auto& k : enumMap->Keys()) {
		if (auto v = enumMap->Get(k); v.second && v.first->v == value.v) {
			return k;
		}
	}
	return "";
}

// serializeEnumValue — showconfig.go:280.
std::string serializeEnumValue(const CompilerOptionsValue& value,
                               const JsonObject* enumMap) {
	// The enum maps store values as core.ModuleKind, core.ScriptTarget, etc.
	// But those are all int32 underneath. We need to compare by the
	// underlying int32 value (the int64 arm here — see CompilerOptionsValue).
	if (auto* intVal = std::get_if<int64_t>(&value.v)) {
		for (const auto& k : enumMap->Keys()) {
			if (auto v = enumMap->Get(k); v.second) {
				if (auto* ev = std::get_if<int64_t>(&v.first->v);
				    ev != nullptr && *ev == *intVal) {
					return k;
				}
			}
		}
	}
	// Fallback: direct comparison
	return getNameOfCompilerOptionValue(value, enumMap);
}

// serializeImpliedOptionValue — showconfig.go:365.
CompilerOptionsValue serializeImpliedOptionValue(
    const CommandLineOption* optionDecl, const CompilerOptionsValue& value) {
	if (value.isNil()) {
		return CompilerOptionsValue();
	}
	const JsonObject* enumMap = optionDecl->EnumMap();
	if (enumMap != nullptr) {
		std::string s = serializeEnumValue(value, enumMap);
		if (!s.empty()) {
			return s;
		}
		return CompilerOptionsValue();
	}
	if (auto* b = std::get_if<bool>(&value.v)) {
		return *b;
	}
	if (auto* t = std::get_if<Tristate>(&value.v)) {
		if (*t == Tristate::True) {
			return true;
		}
		if (*t == Tristate::False) {
			return false;
		}
		return CompilerOptionsValue();
	}
	return value;
}

// anyDependencyProvided — showconfig.go:352.
bool anyDependencyProvided(
    const std::vector<std::string_view>& dependencies,
    const std::unordered_map<std::string, bool>& provided) {
	for (const auto& dep : dependencies) {
		const CommandLineOption* depDecl =
		    CommandLineCompilerOptionsMap().Get(dep);
		if (depDecl != nullptr) {
			if (auto it = provided.find(depDecl->Name);
			    it != provided.end() && it->second) {
				return true;
			}
		}
	}
	return false;
}

// addImpliedOptions — showconfig.go:300.
void addImpliedOptions(JsonObjectPtr optionMap, const CompilerOptions* options,
                       std::string_view /*configFilePath*/,
                       const tspath::ComparePathsOptions& /*comparePaths*/) {
	// Build the set of explicitly provided option JSON names (e.g.,
	// "module", "target").
	std::unordered_map<std::string, bool> provided;
	for (const auto& k : optionMap->Keys()) {
		provided[k] = true;
	}

	CompilerOptions* defaultOpts = new CompilerOptions{};

	for (const auto& entry : impliedOptions()) {
		// Get the option declaration for this implied option (using
		// case-insensitive lookup).
		const CommandLineOption* optionDecl =
		    CommandLineCompilerOptionsMap().Get(entry.name);
		if (optionDecl == nullptr) {
			continue;
		}

		// Skip if this option is already explicitly provided.
		if (auto it = provided.find(optionDecl->Name);
		    it != provided.end() && it->second) {
			continue;
		}

		// Check if any direct dependency is in the provided set.
		// This mirrors TypeScript's optionDependsOn check.
		if (!anyDependencyProvided(entry.dependencies, provided)) {
			continue;
		}

		// Compute the effective value with current options and the default
		// value with empty options.
		CompilerOptionsValue implied = entry.compute(options);
		CompilerOptionsValue defaultVal = entry.compute(defaultOpts);

		// If the implied value equals the default, this option doesn't add
		// useful information.
		if (implied.v == defaultVal.v) {
			continue;
		}

		// Serialize the implied value and add it to the option map.
		CompilerOptionsValue serialized =
		    serializeImpliedOptionValue(optionDecl, implied);
		if (serialized.isNil()) {
			continue;
		}
		optionMap->Set(optionDecl->Name, serialized);
	}
}

// filterSameAsDefaultInclude — showconfig.go:141 (file-local; def below).
std::vector<std::string> filterSameAsDefaultInclude(
    const std::vector<std::string>& specs);

}  // namespace

// ConvertToTSConfig — showconfig.go:64.
TSConfig* ConvertToTSConfig(ParsedCommandLine* configParseResult,
                            std::string_view configFileName) {
	if (configFileName.empty()) {
		configFileName = "tsconfig.json";
	}
	std::string normalizedConfigPath = tspath::getNormalizedAbsolutePath(
	    configFileName, configParseResult->GetCurrentDirectory());
	tspath::ComparePathsOptions comparePathsOptions{
	    .useCaseSensitiveFileNames =
	        configParseResult->UseCaseSensitiveFileNames(),
	    .currentDirectory = configParseResult->GetCurrentDirectory(),
	};

	// Build the list of all resolved files as relative paths from the config
	// file.
	std::vector<std::string> files;
	for (const auto& f : configParseResult->FileNames()) {
		std::string normalizedFilePath = tspath::getNormalizedAbsolutePath(
		    f, configParseResult->GetCurrentDirectory());
		std::string relativePath = tspath::getRelativePathFromFile(
		    normalizedConfigPath, normalizedFilePath, comparePathsOptions);
		files.push_back(relativePath);
	}

	// Serialize compiler options
	JsonObjectPtr optionMap = serializeCompilerOptions(
	    configParseResult->CompilerOptions(), normalizedConfigPath,
	    comparePathsOptions);

	// Remove command-line-only options from the output
	for (const char* name : {"showConfig", "configFile", "configFilePath",
	                        "help", "init", "listFilesOnly",
	                        "listEmittedFiles", "project", "build",
	                        "version"}) {
		optionMap->Delete(name);
	}

	// Add implied compiler options (options that are derived from explicitly
	// set options, such as moduleResolution implied by module, or
	// useDefineForClassFields implied by target). This mirrors TypeScript's
	// convertToTSConfig computedOptions logic.
	addImpliedOptions(optionMap, configParseResult->CompilerOptions(),
	                  normalizedConfigPath, comparePathsOptions);

	auto* config = new TSConfig{
	    .CompilerOptions = optionMap,
	};

	// Add references
	if (auto refs = configParseResult->ProjectReferences(); !refs.empty()) {
		JsonArray references;
		for (auto* r : refs) {
			auto ref = std::make_shared<JsonObject>();
			ref->Set("path", r->OriginalPath);
			if (r->Circular) {
				ref->Set("circular", true);
			}
			references.emplace_back(ref);
		}
		config->References = references;
	}

	// Add files
	if (!files.empty()) {
		config->Files = files;
	}

	// Add include/exclude from configFileSpecs
	if (configParseResult->ConfigFile != nullptr && configParseResult->ConfigFile->configFileSpecs != nullptr) {
		configFileSpecs* specs =
		    configParseResult->ConfigFile->configFileSpecs;
		std::vector<std::string> include =
		    filterSameAsDefaultInclude(specs->validatedIncludeSpecs);
		if (!include.empty()) {
			config->Include = include;
		}
		config->Exclude = specs->validatedExcludeSpecs;
	}

	// Add compileOnSave
	if (configParseResult->CompileOnSave != nullptr &&
	    *configParseResult->CompileOnSave) {
		config->CompileOnSave = std::make_shared<bool>(true);
	}

	return config;
}

namespace {

// filterSameAsDefaultInclude — showconfig.go:141.
std::vector<std::string> filterSameAsDefaultInclude(
    const std::vector<std::string>& specs) {
	if (specs.empty()) {
		return {};
	}
	if (specs.size() == 1 && specs[0] == defaultIncludeSpec) {
		return {};
	}
	return specs;
}

}  // namespace

// serializeCompilerOptions — showconfig.go:165.
JsonObjectPtr serializeCompilerOptions(
    const CompilerOptions* options, std::string_view configFilePath,
    const tspath::ComparePathsOptions& comparePathsOptions) {
	auto result = std::make_shared<JsonObject>();
	std::string configDir = tspath::getDirectoryPath(configFilePath);

	for (const compilerOptionFieldInfo& field :
	     compilerOptionFieldInfos()) {
		const CommandLineOption* optionDecl =
		    CommandLineCompilerOptionsMap().Get(field.name);
		if (optionDecl == nullptr) {
			continue;
		}

		// Skip command-line-only and output formatting options
		if (optionDecl->Category == Command_line_Options ||
		    optionDecl->Category == Output_Formatting) {
			continue;
		}

		// Skip zero values (unset options)
		if (field.isZero(options)) {
			continue;
		}

		std::string name = optionDecl->Name;
		CompilerOptionsValue value = field.get(options);

		const JsonObject* enumMap = optionDecl->EnumMap();
		if (enumMap != nullptr) {
			// Enum option - convert numeric value to string name
			std::string serialized = serializeEnumValue(value, enumMap);
			if (!serialized.empty()) {
				result->Set(name, serialized);
			}
			continue;
		}

		if (optionDecl->Kind == CommandLineOptionTypeListOrElement) {
			TSC_ASSERT(
			    false,
			    "listOrElement option should not reach serialization");
		} else if (optionDecl->Kind == CommandLineOptionTypeList) {
			const CommandLineOption* elem = optionDecl->Elements();
			if (elem != nullptr && elem->IsFilePath) {
				// List of file paths - make relative
				if (auto* strs = std::get_if<JsonStrList>(&value.v)) {
					std::vector<std::string> relPaths(strs->size());
					for (size_t j = 0; j < strs->size(); j++) {
						std::string absPath =
						    tspath::getNormalizedAbsolutePath(
						        (*strs)[j], configDir);
						relPaths[j] = tspath::getRelativePathFromFile(
						    configFilePath, absPath,
						    comparePathsOptions);
					}
					result->Set(name, JsonStrList(std::move(relPaths)));
					continue;
				}
			}
			if (elem != nullptr && elem->EnumMap() != nullptr) {
				// List of enum values (e.g., lib)
				const JsonObject* elemMap = elem->EnumMap();
				if (auto* strs = std::get_if<JsonStrList>(&value.v)) {
					std::vector<std::string> serialized;
					serialized.reserve(strs->size());
					for (const auto& s : *strs) {
						// lib values are already stored as the d.ts
						// filename, need to find original key
						std::string found =
						    getNameOfCompilerOptionValue(
						        CompilerOptionsValue(s), elemMap);
						if (!found.empty()) {
							serialized.push_back(found);
						} else {
							serialized.push_back(s);
						}
					}
					result->Set(name, JsonStrList(std::move(serialized)));
					continue;
				}
			}
			result->Set(name, value);
		} else if (optionDecl->Kind == CommandLineOptionTypeString) {
			if (optionDecl->IsFilePath) {
				// File path option - make relative to config
				if (auto* s = std::get_if<std::string>(&value.v);
				    s != nullptr && !s->empty()) {
					std::string absPath =
					    tspath::getNormalizedAbsolutePath(
					        *s, configDir);
					result->Set(
					    name, tspath::getRelativePathFromFile(
					              configFilePath, absPath,
					              comparePathsOptions));
					continue;
				}
			}
			result->Set(name, value);
		} else if (optionDecl->Kind == CommandLineOptionTypeBoolean) {
			if (auto* t = std::get_if<Tristate>(&value.v)) {
				if (*t == Tristate::True) {
					result->Set(name, true);
				} else if (*t == Tristate::False) {
					result->Set(name, false);
				}
			} else {
				result->Set(name, value);
			}
		} else if (optionDecl->Kind == CommandLineOptionTypeNumber) {
			result->Set(name, value);
		} else {
			result->Set(name, value);
		}
	}

	return result;
}

}  // namespace tsc::tsoptions
