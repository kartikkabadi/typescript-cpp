// Port of tsc/internal/tsoptions/parsinghelpers.go — any→typed-option
// conversions, optionParser impls, the giant parseCompilerOptions switch, and
// mergeCompilerOptions.
#include "internal/tsoptions/tsoptions.h"

#include <algorithm>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

// ParseTristate — parsinghelpers.go:16.
Tristate ParseTristate(const CompilerOptionsValue& value) {
	if (value.isNil()) {
		return Tristate::Unknown;
	}
	if (auto* t = value.get<Tristate>()) {
		return *t;
	}
	if (value.isBool() && value.asBool()) {
		return Tristate::True;
	}
	return Tristate::False;
}

// ParseStringArray — parsinghelpers.go:30. Only the []any arm qualifies in
// Go; a []string value type-asserts to nil.
std::vector<std::string> ParseStringArray(const CompilerOptionsValue& value) {
	if (auto* arr = value.get<JsonArray>()) {
		std::vector<std::string> result;
		result.reserve(arr->size());
		for (const auto& v : *arr) {
			if (auto* str = v.get<std::string>()) {
				result.push_back(*str);
			}
		}
		return result;
	}
	// JsonStrList is our homogeneous []any-of-strings — Go type-asserts it
	// to the same result.
	if (auto* arr = value.get<JsonStrList>()) {
		return *arr;
	}
	return {};
}

// parseStringMap — parsinghelpers.go:46.
std::shared_ptr<collections::OrderedMap<std::string, std::vector<std::string>>>
parseStringMap(const CompilerOptionsValue& value) {
	if (auto* mp = value.get<JsonObjectPtr>()) {
		auto result = std::make_shared<collections::OrderedMap<
		    std::string, std::vector<std::string>>>((*mp)->Size());
		for (const auto& k : (*mp)->Keys()) {
			result->Set(k, ParseStringArray(*(*mp)->Get(k).first));
		}
		return result;
	}
	return nullptr;
}

// ParseString — parsinghelpers.go:57.
std::string ParseString(const CompilerOptionsValue& value) {
	if (auto* str = value.get<std::string>()) {
		return *str;
	}
	return "";
}

// parseNumber — parsinghelpers.go:64.
int* parseNumber(const CompilerOptionsValue& value) {
	if (auto* num = value.get<int64_t>()) {
		return new int(static_cast<int>(*num));
	}
	if (auto* num = value.get<double>()) {
		return new int(static_cast<int>(*num));
	}
	return nullptr;
}

// parseProjectReference — parsinghelpers.go:83.
projectReferenceParseResult* parseProjectReference(
    const CompilerOptionsValue& json) {
	if (auto* v = json.get<JsonObjectPtr>()) {
		auto* result = new projectReferenceParseResult{};
		if (auto value = (*v)->Get("path"); value.second) {
			result->hasPath = true;
			if (auto* path = value.first->get<std::string>()) {
				result->reference.Path = *path;
				result->pathValid = true;
			}
		}
		if (auto value = (*v)->Get("circular"); value.second) {
			result->hasCircular = true;
			if (auto* circular = value.first->get<bool>()) {
				result->reference.Circular = *circular;
				result->circularValid = true;
			}
		}
		return result;
	}
	return nullptr;
}

// parseStringArrayStrict — parsinghelpers.go:145.
std::pair<std::vector<std::string>, bool> parseStringArrayStrict(
    const CompilerOptionsValue& value) {
	auto* arr = value.get<JsonArray>();
	if (arr == nullptr) {
		return {{}, false};
	}
	std::vector<std::string> result;
	result.reserve(arr->size());
	for (const auto& v : *arr) {
		auto* str = v.get<std::string>();
		if (str == nullptr) {
			return {{}, false};
		}
		result.push_back(*str);
	}
	return {result, true};
}

// parseContentMapper — parsinghelpers.go:105.
std::pair<contentmapper::Mapper*, std::vector<Diagnostic*>> parseContentMapper(
    const CompilerOptionsValue& value) {
	auto* v = value.get<JsonObjectPtr>();
	if (v == nullptr) {
		return {nullptr, {}};
	}
	std::vector<Diagnostic*> errors;
	auto* mapper = new contentmapper::Mapper{};
	if (auto pkg = (*v)->Get("package"); pkg.second) {
		if (auto* str = pkg.first->get<std::string>();
		    str != nullptr && !str->empty()) {
			mapper->Definition.Package = *str;
		} else {
			errors.push_back(newCompilerDiagnostic(
			    Compiler_option_0_requires_a_value_of_type_1,
			    {"contentMapper.package", "string"}));
		}
	} else {
		errors.push_back(newCompilerDiagnostic(
		    Compiler_option_0_requires_a_value_of_type_1,
		    {"contentMapper.package", "string"}));
	}
	if (auto extensions = (*v)->Get("extensions"); extensions.second) {
		if (auto strs = parseStringArrayStrict(*extensions.first);
		    strs.second) {
			mapper->Definition.Extensions = std::move(strs.first);
		} else {
			errors.push_back(newCompilerDiagnostic(
			    Compiler_option_0_requires_a_value_of_type_1,
			    {"contentMapper.extensions", "string[]"}));
		}
	} else {
		errors.push_back(newCompilerDiagnostic(
		    Compiler_option_0_requires_a_value_of_type_1,
		    {"contentMapper.extensions", "string[]"}));
	}
	if (auto options = (*v)->Get("options"); options.second) {
		if (!options.first->isObject()) {
			errors.push_back(newCompilerDiagnostic(
			    Compiler_option_0_requires_a_value_of_type_1,
			    {"contentMapper.options", "object"}));
		} else {
			mapper->Definition.Options = jsonMarshal(*options.first);
		}
	}
	if (!errors.empty()) {
		return {nullptr, errors};
	}
	return {mapper, errors};
}

// parseJsonToStringKey — parsinghelpers.go:161.
JsonObjectPtr parseJsonToStringKey(const CompilerOptionsValue& json) {
	auto result = std::make_shared<JsonObject>(6);
	if (auto* m = json.get<JsonObjectPtr>()) {
		if (auto v = (*m)->Get("include"); v.second) {
			result->Set("include", *v.first);
		}
		if (auto v = (*m)->Get("exclude"); v.second) {
			result->Set("exclude", *v.first);
		}
		if (auto v = (*m)->Get("files"); v.second) {
			result->Set("files", *v.first);
		}
		if (auto v = (*m)->Get("references"); v.second) {
			result->Set("references", *v.first);
		}
		if (auto v = (*m)->Get("contentMappers"); v.second) {
			result->Set("contentMappers", *v.first);
		}
		if (auto v = (*m)->Get("extends"); v.second) {
			if (auto* str = v.first->get<std::string>()) {
				result->Set("extends",
				            JsonArray{CompilerOptionsValue(*str)});
			}
			result->Set("extends", *v.first);
		}
		if (auto v = (*m)->Get("compilerOptions"); v.second) {
			result->Set("compilerOptions", *v.first);
		}
		if (auto v = (*m)->Get("excludes"); v.second) {
			result->Set("excludes", *v.first);
		}
		if (auto v = (*m)->Get("typeAcquisition"); v.second) {
			result->Set("typeAcquisition", *v.first);
		}
	}
	return result;
}

// optionParser impls — parsinghelpers.go:204-266.
std::vector<Diagnostic*> compilerOptionsParser::ParseOption(
    std::string_view key, const CompilerOptionsValue& value) {
	return ParseCompilerOptions(key, value, CompilerOptions);
}
const DiagnosticMessage* compilerOptionsParser::UnknownOptionDiagnostic()
    const {
	return extraKeyDiagnostics("compilerOptions");
}
const DiagnosticMessage* compilerOptionsParser::UnknownDidYouMeanDiagnostic()
    const {
	return extraKeyDidYouMeanDiagnostics("compilerOptions");
}
std::vector<Diagnostic*> typeAcquisitionParser::ParseOption(
    std::string_view key, const CompilerOptionsValue& value) {
	return ParseTypeAcquisition(key, value, TypeAcquisition);
}
const DiagnosticMessage* typeAcquisitionParser::UnknownOptionDiagnostic()
    const {
	return extraKeyDiagnostics("typeAcquisition");
}
const DiagnosticMessage*
typeAcquisitionParser::UnknownDidYouMeanDiagnostic() const {
	return extraKeyDidYouMeanDiagnostics("typeAcquisition");
}
std::vector<Diagnostic*> buildOptionsParser::ParseOption(
    std::string_view key, const CompilerOptionsValue& value) {
	return ParseBuildOptions(key, value, BuildOptions);
}
const DiagnosticMessage* buildOptionsParser::UnknownOptionDiagnostic() const {
	return extraKeyDiagnostics("buildOptions");
}
const DiagnosticMessage* buildOptionsParser::UnknownDidYouMeanDiagnostic()
    const {
	return extraKeyDidYouMeanDiagnostics("buildOptions");
}

// ParseCompilerOptions — parsinghelpers.go:268.
std::vector<Diagnostic*> ParseCompilerOptions(
    std::string_view key, const CompilerOptionsValue& value,
    CompilerOptions* allOptions) {
	if (value.isNil()) {
		return {};
	}
	if (allOptions == nullptr) {
		return {};
	}
	parseCompilerOptions(key, value, allOptions);
	return {};
}

// parseCompilerOptions — parsinghelpers.go:279.
bool parseCompilerOptions(std::string_view key,
                          const CompilerOptionsValue& value,
                          CompilerOptions* allOptions) {
	const CommandLineOption* option =
	    CommandLineCompilerOptionsMap().Get(key);
	if (option != nullptr) {
		key = option->Name;
	}
	if (false) {
	} else if (key == "allowJs") {
		allOptions->AllowJs = ParseTristate(value);
	} else if (key == "allowImportingTsExtensions") {
		allOptions->AllowImportingTsExtensions = ParseTristate(value);
	} else if (key == "allowSyntheticDefaultImports") {
		allOptions->AllowSyntheticDefaultImports = ParseTristate(value);
	} else if (key == "allowNonTsExtensions") {
		allOptions->AllowNonTsExtensions = ParseTristate(value);
	} else if (key == "allowUmdGlobalAccess") {
		allOptions->AllowUmdGlobalAccess = ParseTristate(value);
	} else if (key == "allowUnreachableCode") {
		allOptions->AllowUnreachableCode = ParseTristate(value);
	} else if (key == "allowUnusedLabels") {
		allOptions->AllowUnusedLabels = ParseTristate(value);
	} else if (key == "allowArbitraryExtensions") {
		allOptions->AllowArbitraryExtensions = ParseTristate(value);
	} else if (key == "alwaysStrict") {
		allOptions->AlwaysStrict = ParseTristate(value);
	} else if (key == "assumeChangesOnlyAffectDirectDependencies") {
		allOptions->AssumeChangesOnlyAffectDirectDependencies = ParseTristate(value);
	} else if (key == "baseUrl") {
		allOptions->BaseUrl = ParseString(value);
	} else if (key == "build") {
		allOptions->Build = ParseTristate(value);
	} else if (key == "checkJs") {
		allOptions->CheckJs = ParseTristate(value);
	} else if (key == "customConditions") {
		allOptions->CustomConditions = ParseStringArray(value);
	} else if (key == "composite") {
		allOptions->Composite = ParseTristate(value);
	} else if (key == "declarationDir") {
		allOptions->DeclarationDir = ParseString(value);
	} else if (key == "deduplicatePackages") {
		allOptions->DeduplicatePackages = ParseTristate(value);
	} else if (key == "diagnostics") {
		allOptions->Diagnostics = ParseTristate(value);
	} else if (key == "disableSizeLimit") {
		allOptions->DisableSizeLimit = ParseTristate(value);
	} else if (key == "disableSourceOfProjectReferenceRedirect") {
		allOptions->DisableSourceOfProjectReferenceRedirect = ParseTristate(value);
	} else if (key == "disableSolutionSearching") {
		allOptions->DisableSolutionSearching = ParseTristate(value);
	} else if (key == "disableReferencedProjectLoad") {
		allOptions->DisableReferencedProjectLoad = ParseTristate(value);
	} else if (key == "declarationMap") {
		allOptions->DeclarationMap = ParseTristate(value);
	} else if (key == "declaration") {
		allOptions->Declaration = ParseTristate(value);
	} else if (key == "downlevelIteration") {
		allOptions->DownlevelIteration = ParseTristate(value);
	} else if (key == "erasableSyntaxOnly") {
		allOptions->ErasableSyntaxOnly = ParseTristate(value);
	} else if (key == "emitDeclarationOnly") {
		allOptions->EmitDeclarationOnly = ParseTristate(value);
	} else if (key == "extendedDiagnostics") {
		allOptions->ExtendedDiagnostics = ParseTristate(value);
	} else if (key == "emitDecoratorMetadata") {
		allOptions->EmitDecoratorMetadata = ParseTristate(value);
	} else if (key == "emitBOM") {
		allOptions->EmitBOM = ParseTristate(value);
	} else if (key == "esModuleInterop") {
		allOptions->ESModuleInterop = ParseTristate(value);
	} else if (key == "exactOptionalPropertyTypes") {
		allOptions->ExactOptionalPropertyTypes = ParseTristate(value);
	} else if (key == "explainFiles") {
		allOptions->ExplainFiles = ParseTristate(value);
	} else if (key == "experimentalDecorators") {
		allOptions->ExperimentalDecorators = ParseTristate(value);
	} else if (key == "forceConsistentCasingInFileNames") {
		allOptions->ForceConsistentCasingInFileNames = ParseTristate(value);
	} else if (key == "generateCpuProfile") {
		allOptions->GenerateCpuProfile = ParseString(value);
	} else if (key == "generateTrace") {
		allOptions->GenerateTrace = ParseString(value);
	} else if (key == "isolatedModules") {
		allOptions->IsolatedModules = ParseTristate(value);
	} else if (key == "ignoreConfig") {
		allOptions->IgnoreConfig = ParseTristate(value);
	} else if (key == "ignoreDeprecations") {
		allOptions->IgnoreDeprecations = ParseString(value);
	} else if (key == "importHelpers") {
		allOptions->ImportHelpers = ParseTristate(value);
	} else if (key == "incremental") {
		allOptions->Incremental = ParseTristate(value);
	} else if (key == "init") {
		allOptions->Init = ParseTristate(value);
	} else if (key == "inlineSourceMap") {
		allOptions->InlineSourceMap = ParseTristate(value);
	} else if (key == "inlineSources") {
		allOptions->InlineSources = ParseTristate(value);
	} else if (key == "isolatedDeclarations") {
		allOptions->IsolatedDeclarations = ParseTristate(value);
	} else if (key == "jsx") {
		allOptions->Jsx = floatOrInt32ToFlag<JsxEmit>(value);
	} else if (key == "jsxFactory") {
		allOptions->JsxFactory = ParseString(value);
	} else if (key == "jsxFragmentFactory") {
		allOptions->JsxFragmentFactory = ParseString(value);
	} else if (key == "jsxImportSource") {
		allOptions->JsxImportSource = ParseString(value);
	} else if (key == "libReplacement") {
		allOptions->LibReplacement = ParseTristate(value);
	} else if (key == "listEmittedFiles") {
		allOptions->ListEmittedFiles = ParseTristate(value);
	} else if (key == "listFiles") {
		allOptions->ListFiles = ParseTristate(value);
	} else if (key == "listFilesOnly") {
		allOptions->ListFilesOnly = ParseTristate(value);
	} else if (key == "locale") {
		allOptions->Locale = ParseString(value);
	} else if (key == "mapRoot") {
		allOptions->MapRoot = ParseString(value);
	} else if (key == "module") {
		allOptions->Module = floatOrInt32ToFlag<ModuleKind>(value);
	} else if (key == "moduleDetectionKind") {
		allOptions->ModuleDetection = floatOrInt32ToFlag<ModuleDetectionKind>(value);
	} else if (key == "moduleResolution") {
		allOptions->ModuleResolution = floatOrInt32ToFlag<ModuleResolutionKind>(value);
	} else if (key == "moduleSuffixes") {
		allOptions->ModuleSuffixes = ParseStringArray(value);
	} else if (key == "moduleDetection") {
		allOptions->ModuleDetection = floatOrInt32ToFlag<ModuleDetectionKind>(value);
	} else if (key == "noCheck") {
		allOptions->NoCheck = ParseTristate(value);
	} else if (key == "noFallthroughCasesInSwitch") {
		allOptions->NoFallthroughCasesInSwitch = ParseTristate(value);
	} else if (key == "noEmitForJsFiles") {
		allOptions->NoEmitForJsFiles = ParseTristate(value);
	} else if (key == "noErrorTruncation") {
		allOptions->NoErrorTruncation = ParseTristate(value);
	} else if (key == "noImplicitAny") {
		allOptions->NoImplicitAny = ParseTristate(value);
	} else if (key == "noImplicitThis") {
		allOptions->NoImplicitThis = ParseTristate(value);
	} else if (key == "noLib") {
		allOptions->NoLib = ParseTristate(value);
	} else if (key == "noPropertyAccessFromIndexSignature") {
		allOptions->NoPropertyAccessFromIndexSignature = ParseTristate(value);
	} else if (key == "noUncheckedIndexedAccess") {
		allOptions->NoUncheckedIndexedAccess = ParseTristate(value);
	} else if (key == "noEmitHelpers") {
		allOptions->NoEmitHelpers = ParseTristate(value);
	} else if (key == "noEmitOnError") {
		allOptions->NoEmitOnError = ParseTristate(value);
	} else if (key == "noImplicitReturns") {
		allOptions->NoImplicitReturns = ParseTristate(value);
	} else if (key == "noUnusedLocals") {
		allOptions->NoUnusedLocals = ParseTristate(value);
	} else if (key == "noUnusedParameters") {
		allOptions->NoUnusedParameters = ParseTristate(value);
	} else if (key == "noImplicitOverride") {
		allOptions->NoImplicitOverride = ParseTristate(value);
	} else if (key == "noUncheckedSideEffectImports") {
		allOptions->NoUncheckedSideEffectImports = ParseTristate(value);
	} else if (key == "outFile") {
		allOptions->OutFile = ParseString(value);
	} else if (key == "noResolve") {
		allOptions->NoResolve = ParseTristate(value);
	} else if (key == "preserveWatchOutput") {
		allOptions->PreserveWatchOutput = ParseTristate(value);
	} else if (key == "preserveConstEnums") {
		allOptions->PreserveConstEnums = ParseTristate(value);
	} else if (key == "preserveSymlinks") {
		allOptions->PreserveSymlinks = ParseTristate(value);
	} else if (key == "project") {
		allOptions->Project = ParseString(value);
	} else if (key == "pretty") {
		allOptions->Pretty = ParseTristate(value);
	} else if (key == "resolveJsonModule") {
		allOptions->ResolveJsonModule = ParseTristate(value);
	} else if (key == "resolvePackageJsonExports") {
		allOptions->ResolvePackageJsonExports = ParseTristate(value);
	} else if (key == "resolvePackageJsonImports") {
		allOptions->ResolvePackageJsonImports = ParseTristate(value);
	} else if (key == "reactNamespace") {
		allOptions->ReactNamespace = ParseString(value);
	} else if (key == "rewriteRelativeImportExtensions") {
		allOptions->RewriteRelativeImportExtensions = ParseTristate(value);
	} else if (key == "rootDir") {
		allOptions->RootDir = ParseString(value);
	} else if (key == "rootDirs") {
		allOptions->RootDirs = ParseStringArray(value);
	} else if (key == "removeComments") {
		allOptions->RemoveComments = ParseTristate(value);
	} else if (key == "stableTypeOrdering") {
		allOptions->StableTypeOrdering = ParseTristate(value);
	} else if (key == "strict") {
		allOptions->Strict = ParseTristate(value);
	} else if (key == "strictBindCallApply") {
		allOptions->StrictBindCallApply = ParseTristate(value);
	} else if (key == "strictBuiltinIteratorReturn") {
		allOptions->StrictBuiltinIteratorReturn = ParseTristate(value);
	} else if (key == "strictFunctionTypes") {
		allOptions->StrictFunctionTypes = ParseTristate(value);
	} else if (key == "strictNullChecks") {
		allOptions->StrictNullChecks = ParseTristate(value);
	} else if (key == "strictPropertyInitialization") {
		allOptions->StrictPropertyInitialization = ParseTristate(value);
	} else if (key == "skipDefaultLibCheck") {
		allOptions->SkipDefaultLibCheck = ParseTristate(value);
	} else if (key == "sourceMap") {
		allOptions->SourceMap = ParseTristate(value);
	} else if (key == "sourceRoot") {
		allOptions->SourceRoot = ParseString(value);
	} else if (key == "stripInternal") {
		allOptions->StripInternal = ParseTristate(value);
	} else if (key == "suppressOutputPathCheck") {
		allOptions->SuppressOutputPathCheck = ParseTristate(value);
	} else if (key == "target") {
		allOptions->Target = floatOrInt32ToFlag<ScriptTarget>(value);
	} else if (key == "traceResolution") {
		allOptions->TraceResolution = ParseTristate(value);
	} else if (key == "tsBuildInfoFile") {
		allOptions->TsBuildInfoFile = ParseString(value);
	} else if (key == "typeRoots") {
		allOptions->TypeRoots = ParseStringArray(value);
	} else if (key == "types") {
		allOptions->Types = ParseStringArray(value);
		allOptions->TypesWasSet = true;
	} else if (key == "useDefineForClassFields") {
		allOptions->UseDefineForClassFields = ParseTristate(value);
	} else if (key == "useUnknownInCatchVariables") {
		allOptions->UseUnknownInCatchVariables = ParseTristate(value);
	} else if (key == "verbatimModuleSyntax") {
		allOptions->VerbatimModuleSyntax = ParseTristate(value);
	} else if (key == "version") {
		allOptions->Version = ParseTristate(value);
	} else if (key == "help") {
		allOptions->Help = ParseTristate(value);
	} else if (key == "all") {
		allOptions->All = ParseTristate(value);
	} else if (key == "maxNodeModuleJsDepth") {
		allOptions->MaxNodeModuleJsDepth = parseNumber(value);
	} else if (key == "skipLibCheck") {
		allOptions->SkipLibCheck = ParseTristate(value);
	} else if (key == "noEmit") {
		allOptions->NoEmit = ParseTristate(value);
	} else if (key == "showConfig") {
		allOptions->ShowConfig = ParseTristate(value);
	} else if (key == "configFilePath") {
		allOptions->ConfigFilePath = ParseString(value);
	} else if (key == "noDtsResolution") {
		allOptions->NoDtsResolution = ParseTristate(value);
	} else if (key == "pathsBasePath") {
		allOptions->PathsBasePath = ParseString(value);
	} else if (key == "outDir") {
		allOptions->OutDir = ParseString(value);
	} else if (key == "newLine") {
		allOptions->NewLine = floatOrInt32ToFlag<NewLineKind>(value);
	} else if (key == "watch") {
		allOptions->Watch = ParseTristate(value);
	} else if (key == "pprofDir") {
		allOptions->PprofDir = ParseString(value);
	} else if (key == "singleThreaded") {
		allOptions->SingleThreaded = ParseTristate(value);
	} else if (key == "quiet") {
		allOptions->Quiet = ParseTristate(value);
	} else if (key == "checkers") {
		allOptions->Checkers = parseNumber(value);
	} else if (key == "runExternalCode") {
		allOptions->RunExternalCode = ParseTristate(value);
	} else if (key == "lib") {
		if (value.isStrList()) {
			allOptions->Lib = value.asStrList();
		} else {
			allOptions->Lib = ParseStringArray(value);
		}
	} else if (key == "paths") {
		if (auto m = parseStringMap(value)) {
			allOptions->Paths.clear();
			for (const auto& k : m->Keys()) {
				allOptions->Paths.emplace_back(k, *m->Get(k).first);
			}
		} else {
			allOptions->Paths.clear();
		}
	} else if (key == "plugins") {
		// Native TypeScript does not load plugins; retain them only so tools can report the incompatibility.
		if (auto* plugins = value.get<JsonArray>()) {
			allOptions->Plugins.clear();
			allOptions->Plugins.reserve(plugins->size());
			for (const auto& plugin : *plugins) {
				if (auto* pluginMap = plugin.get<JsonObjectPtr>()) {
					allOptions->Plugins.push_back(PluginImport{
					    ParseString((*pluginMap)->GetOrZero("name"))});
				} else {
					allOptions->Plugins.push_back(PluginImport{});
				}
			}
		}
	} else {
		// different than any key above
		return false;
	}
	return true;
}

// ParseTypeAcquisition — parsinghelpers.go:606.
std::vector<Diagnostic*> ParseTypeAcquisition(
    std::string_view key, const CompilerOptionsValue& value,
    TypeAcquisition* allOptions) {
	if (value.isNil()) {
		return {};
	}
	if (allOptions == nullptr) {
		return {};
	}
	if (key == "enable") {
		allOptions->Enable = ParseTristate(value);
	} else if (key == "include") {
		allOptions->Include = ParseStringArray(value);
	} else if (key == "exclude") {
		allOptions->Exclude = ParseStringArray(value);
	} else if (key == "disableFilenameBasedTypeAcquisition") {
		allOptions->DisableFilenameBasedTypeAcquisition = ParseTristate(value);
	}
	return {};
}

// ParseBuildOptions — parsinghelpers.go:626.
std::vector<Diagnostic*> ParseBuildOptions(
    std::string_view key, const CompilerOptionsValue& value,
    BuildOptions* allOptions) {
	if (value.isNil()) {
		return {};
	}
	if (allOptions == nullptr) {
		return {};
	}
	const CommandLineOption* option = BuildNameMap().Get(key);
	if (option != nullptr) {
		key = option->Name;
	}
	if (key == "clean") {
		allOptions->Clean = ParseTristate(value);
	} else if (key == "dry") {
		allOptions->Dry = ParseTristate(value);
	} else if (key == "force") {
		allOptions->Force = ParseTristate(value);
	} else if (key == "builders") {
		allOptions->Builders = parseNumber(value);
	} else if (key == "stopBuildOnErrors") {
		allOptions->StopBuildOnErrors = ParseTristate(value);
	} else if (key == "verbose") {
		allOptions->Verbose = ParseTristate(value);
	}
	return {};
}

// mergeCompilerOptions — parsinghelpers.go:658. reflect-driven field walk
// replaced by compilerOptionFieldInfos.
CompilerOptions* mergeCompilerOptions(
    CompilerOptions* targetOptions, const CompilerOptions* sourceOptions,
    const CompilerOptionsValue& rawSource) {
	if (sourceOptions == nullptr) {
		return targetOptions;
	}

	// Collect explicitly null field names from raw JSON
	collections::Set<std::string> explicitNullFields;
	if (!rawSource.isNil()) {
		if (auto* rawMap = rawSource.get<JsonObjectPtr>();
		    rawMap != nullptr && *rawMap != nullptr) {
			// Options are nested under "compilerOptions" in both
			// tsconfig.json and wrapped command line options
			if (auto compilerOptionsRaw = (*rawMap)->Get("compilerOptions");
			    compilerOptionsRaw.second) {
				if (auto* compilerOptionsMap =
				        compilerOptionsRaw.first->get<JsonObjectPtr>()) {
					for (const auto& key : (*compilerOptionsMap)->Keys()) {
						if ((*compilerOptionsMap)
						        ->Get(key)
						        .first->isNil()) {
							explicitNullFields.Add(key);
						}
					}
				}
			}
		}
	}

	// Do the merge, handling explicit nulls during the normal merge
	for (const auto& info : compilerOptionFieldInfos()) {
		// Get the JSON field name for this struct field and check if it's
		// explicitly null
		if (!info.jsonName.empty() && explicitNullFields.Has(std::string(info.jsonName))) {
			info.setZero(targetOptions);
			continue;
		}
		// Normal merge behavior: copy non-zero fields
		if (!info.isZero(sourceOptions)) {
			info.set(targetOptions, info.get(sourceOptions));
		}
	}
	return targetOptions;
}

// convertToOptionsWithAbsolutePaths — parsinghelpers.go:706.
JsonObjectPtr convertToOptionsWithAbsolutePaths(
    const JsonObjectPtr& optionsBase, const CommandLineOptionNameMap& optionMap,
    std::string_view cwd) {
	// !!! convert to options with absolute paths was previously done with
	// `CompilerOptions` object, but for ease of implementation, we do it
	// pre-conversion.
	// !!! Revisit this choice if/when refactoring when conversion is done in
	// tsconfig parsing
	if (optionsBase == nullptr) {
		return nullptr;
	}
	for (const auto& o : optionsBase->Keys()) {
		auto [result, ok] = ConvertOptionToAbsolutePath(
		    o, *optionsBase->Get(o).first, optionMap, cwd);
		if (ok) {
			optionsBase->Set(o, result);
		}
	}
	return optionsBase;
}

// ConvertOptionToAbsolutePath — parsinghelpers.go:721.
std::pair<CompilerOptionsValue, bool> ConvertOptionToAbsolutePath(
    std::string_view o, const CompilerOptionsValue& v,
    const CommandLineOptionNameMap& optionMap, std::string_view cwd) {
	const CommandLineOption* option = optionMap.Get(o);
	if (option == nullptr) {
		return {CompilerOptionsValue(), false};
	}
	if (option->Kind == CommandLineOptionTypeList) {
		if (option->Elements()->IsFilePath) {
			if (auto* arr = v.get<JsonStrList>()) {
				JsonStrList mapped;
				mapped.reserve(arr->size());
				for (const auto& item : *arr) {
					mapped.push_back(
					    tspath::getNormalizedAbsolutePath(item, cwd));
				}
				return {std::move(mapped), true};
			}
			if (auto* arr = v.get<JsonArray>()) {
				JsonArray mapped;
				mapped.reserve(arr->size());
				for (const auto& item : *arr) {
					if (auto* s = item.get<std::string>()) {
						mapped.emplace_back(
						    tspath::getNormalizedAbsolutePath(*s, cwd));
					} else {
						mapped.push_back(item);
					}
				}
				return {std::move(mapped), true};
			}
		}
	} else if (option->IsFilePath) {
		if (auto* value = v.get<std::string>()) {
			return {tspath::getNormalizedAbsolutePath(*value, cwd), true};
		}
	}
	return {CompilerOptionsValue(), false};
}

// ---------------------------------------------------------------------------
// implicit-slice helpers — used by several TUs.
// ---------------------------------------------------------------------------

// jsonDeepEqual — reflect.DeepEqual across the variant arms.
bool jsonDeepEqual(const CompilerOptionsValue& a,
                   const CompilerOptionsValue& b) {
	if (a.v.index() != b.v.index()) {
		return false;
	}
	switch (a.v.index()) {
	case 0:  // monostate
		return true;
	case 1:
		return a.asBool() == b.asBool();
	case 2:
		return a.asInt() == b.asInt();
	case 3:
		return a.asDouble() == b.asDouble();
	case 4:
		return a.asTristate() == b.asTristate();
	case 5:
		return a.asString() == b.asString();
	case 6:
		return std::get<const DiagnosticMessage*>(a.v) ==
		       std::get<const DiagnosticMessage*>(b.v);
	case 7:
		return a.asStrList() == b.asStrList();
	case 8: {
		const auto& xa = a.asArray();
		const auto& xb = b.asArray();
		if (xa.size() != xb.size()) {
			return false;
		}
		for (size_t i = 0; i < xa.size(); i++) {
			if (!jsonDeepEqual(xa[i], xb[i])) {
				return false;
			}
		}
		return true;
	}
	case 9: {
		const auto& pa = a.asObject();
		const auto& pb = b.asObject();
		if (pa == nullptr || pb == nullptr) {
			return pa == pb;
		}
		if (pa->Size() != pb->Size()) {
			return false;
		}
		for (const auto& k : pa->Keys()) {
			auto vb = pb->Get(k);
			if (!vb.second || !jsonDeepEqual(*pa->Get(k).first, *vb.first)) {
				return false;
			}
		}
		return true;
	}
	case 10: {
		const auto& pa = a.asGoMap();
		const auto& pb = b.asGoMap();
		if (pa == nullptr || pb == nullptr) {
			return pa == pb;
		}
		if (pa->size() != pb->size()) {
			return false;
		}
		for (const auto& [k, va] : *pa) {
			auto it = pb->find(k);
			if (it == pb->end() || !jsonDeepEqual(va, it->second)) {
				return false;
			}
		}
		return true;
	}
	}
	return false;
}

// jsonMarshal — encoding/json Marshal of the variant. Objects emit keys in
// insertion order like OrderedMap.MarshalJSONTo.
std::string jsonMarshal(const CompilerOptionsValue& v) {
	if (v.isNil()) {
		return "null";
	}
	if (auto* p = v.get<bool>()) {
		return *p ? "true" : "false";
	}
	if (auto* p = v.get<int64_t>()) {
		return std::to_string(*p);
	}
	if (auto* p = v.get<double>()) {
		// encoding/json: integer-valued floats marshal without a fraction.
		if (*p == std::floor(*p) && std::abs(*p) < 1e15) {
			return std::to_string(static_cast<int64_t>(*p));
		}
		return std::to_string(*p);
	}
	if (auto* p = v.get<Tristate>()) {
		return std::to_string(static_cast<int64_t>(*p));
	}
	if (auto* p = v.get<const DiagnosticMessage*>()) {
		// *diagnostics.Message marshals as its message text.
		std::string out = "\"";
		for (char c : std::string_view((*p)->text)) {
			if (c == '"' || c == '\\') {
				out += '\\';
			}
			out += c;
		}
		out += "\"";
		return out;
	}
	if (auto* p = v.get<std::string>()) {
		std::string out = "\"";
		for (char c : *p) {
			switch (c) {
			case '"':
			case '\\':
				out += '\\';
				out += c;
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				out += c;
			}
		}
		out += "\"";
		return out;
	}
	if (auto* p = v.get<JsonStrList>()) {
		std::string out = "[";
		for (size_t i = 0; i < p->size(); i++) {
			if (i) {
				out += ",";
			}
			out += jsonMarshal((*p)[i]);
		}
		out += "]";
		return out;
	}
	if (auto* p = v.get<JsonArray>()) {
		std::string out = "[";
		for (size_t i = 0; i < p->size(); i++) {
			if (i) {
				out += ",";
			}
			out += jsonMarshal((*p)[i]);
		}
		out += "]";
		return out;
	}
	if (auto* p = v.get<JsonObjectPtr>()) {
		if (*p == nullptr) {
			return "null";
		}
		std::string out = "{";
		bool first = true;
		for (const auto& k : (*p)->Keys()) {
			if (!first) {
				out += ",";
			}
			first = false;
			out += jsonMarshal(CompilerOptionsValue(k));
			out += ":";
			out += jsonMarshal(*(*p)->Get(k).first);
		}
		out += "}";
		return out;
	}
	if (auto* p = v.get<JsonGoMapPtr>()) {
		if (*p == nullptr) {
			return "null";
		}
		// Go marshals map[string]any with keys in sorted order.
		std::vector<std::string> keys;
		keys.reserve((*p)->size());
		for (const auto& [k, unused] : **p) {
			(void)unused;
			keys.push_back(k);
		}
		std::sort(keys.begin(), keys.end());
		std::string out = "{";
		bool first = true;
		for (const auto& k : keys) {
			if (!first) {
				out += ",";
			}
			first = false;
			out += jsonMarshal(CompilerOptionsValue(k));
			out += ":";
			out += jsonMarshal((*p)->at(k));
		}
		out += "}";
		return out;
	}
	return "null";
}

// CompilerOptionsValue::marshalJSONTo — encoding/json Marshal of the variant
// through the stream encoder (honors MarshalIndentWrite indentation).
std::string CompilerOptionsValue::marshalJSONTo(json::Encoder& enc) const {
	if (v.valueless_by_exception() || std::holds_alternative<std::monostate>(v)) {
		return enc.writeValue("null");
	}
	if (auto* p = std::get_if<bool>(&v)) {
		return enc.writeValue(*p ? "true" : "false");
	}
	if (auto* p = std::get_if<int64_t>(&v)) {
		return enc.writeValue(std::to_string(*p));
	}
	if (auto* p = std::get_if<double>(&v)) {
		// encoding/json: integer-valued floats marshal without a fraction.
		if (*p == std::floor(*p) && std::abs(*p) < 1e15) {
			return enc.writeValue(std::to_string(static_cast<int64_t>(*p)));
		}
		return enc.writeValue(std::to_string(*p));
	}
	if (auto* p = std::get_if<Tristate>(&v)) {
		return enc.writeValue(std::to_string(static_cast<int64_t>(*p)));
	}
	if (auto* p = std::get_if<const DiagnosticMessage*>(&v)) {
		// *diagnostics.Message marshals as its message text.
		return enc.writeValue(
		    json::Value(json::marshalString(*p ? (*p)->text : "")));
	}
	if (auto* p = std::get_if<std::string>(&v)) {
		return enc.writeValue(json::Value(json::marshalString(*p)));
	}
	if (auto* p = std::get_if<JsonStrList>(&v)) {
		if (auto err = enc.writeToken(json::BeginArray); !err.empty()) {
			return err;
		}
		for (const auto& s : *p) {
			if (auto err = enc.writeValue(
			        json::Value(json::marshalString(s)));
			    !err.empty()) {
				return err;
			}
		}
		return enc.writeToken(json::EndArray);
	}
	if (auto* p = std::get_if<JsonArray>(&v)) {
		if (auto err = enc.writeToken(json::BeginArray); !err.empty()) {
			return err;
		}
		for (const auto& e : *p) {
			if (auto err = e.marshalJSONTo(enc); !err.empty()) {
				return err;
			}
		}
		return enc.writeToken(json::EndArray);
	}
	if (auto* p = std::get_if<JsonObjectPtr>(&v)) {
		if (*p == nullptr) {
			return enc.writeValue("null");
		}
		if (auto err = enc.writeToken(json::BeginObject); !err.empty()) {
			return err;
		}
		for (const auto& k : (*p)->Keys()) {
			if (auto err = enc.writeValue(
			        json::Value(json::marshalString(k)));
			    !err.empty()) {
				return err;
			}
			if (auto err = (*p)->Get(k).first->marshalJSONTo(enc);
			    !err.empty()) {
				return err;
			}
		}
		return enc.writeToken(json::EndObject);
	}
	if (auto* p = std::get_if<JsonGoMapPtr>(&v)) {
		if (*p == nullptr) {
			return enc.writeValue("null");
		}
		// Go marshals map[string]any with keys in sorted order.
		std::vector<std::string> keys;
		keys.reserve((*p)->size());
		for (const auto& [k, unused] : **p) {
			(void)unused;
			keys.push_back(k);
		}
		std::sort(keys.begin(), keys.end());
		if (auto err = enc.writeToken(json::BeginObject); !err.empty()) {
			return err;
		}
		for (const auto& k : keys) {
			if (auto err = enc.writeValue(
			        json::Value(json::marshalString(k)));
			    !err.empty()) {
				return err;
			}
			if (auto err = (*p)->at(k).marshalJSONTo(enc); !err.empty()) {
				return err;
			}
		}
		return enc.writeToken(json::EndObject);
	}
	return enc.writeValue("null");
}

}  // namespace tsc::tsoptions
