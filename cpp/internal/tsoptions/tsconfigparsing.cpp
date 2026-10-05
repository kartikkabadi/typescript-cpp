// Port of tsc/internal/tsoptions/tsconfigparsing.go.
#include "internal/tsoptions/tsoptions.h"

#include <algorithm>
#include <cstring>

#include "internal/ast/ast.h"
#include "internal/ast/nodes_generated.h"
#include "internal/collections/collections.h"
#include "internal/core/spelling.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/jsnum/jsnum.h"
#include "internal/module/resolver.h"
#include "internal/module/types.h"
#include "internal/module/vfsmatch.h"
#include "internal/parser/parser.h"
#include "internal/scanner/scanner.h"
#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

namespace {

// core.Find — returns the first element matching f or nullptr.
template <typename T, typename F>
T* findIn(const std::vector<T*>& v, F&& f) {
	for (T* e : v) {
		if (f(e)) return e;
	}
	return nullptr;
}

// core.InsertSorted — binary insert preserving order.
template <typename T>
void insertSorted(std::vector<T>& v, T element) {
	auto it = std::lower_bound(v.begin(), v.end(), element);
	v.insert(it, std::move(element));
}

// isDoubleQuotedString — tsconfigparsing.go:814. (file-local in Go;
// renamed to avoid colliding with tsc::isDoubleQuotedString in parser.h)
bool isDoubleQuotedStringLocal(Node* node) { return isStringLiteral(node); }

// Forward decls — file-local helpers (Go package-private), mutually
// recursive in places; defs appear later in this TU.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>> convertJsonOption(
    const CommandLineOption* opt, const CompilerOptionsValue& value,
    std::string_view basePath, PropertyAssignment* propertyAssignment,
    Node* valueExpression, SourceFile* sourceFile);
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
convertPropertyValueToJson(SourceFile* sourceFile, Node* valueExpression,
                           const CommandLineOption* option, bool returnValue,
                           jsonConversionNotifier* notifier);
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>> convertToJson(
    SourceFile* sourceFile, Node* rootExpression, bool returnValue,
    jsonConversionNotifier* notifier);
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
convertConfigFileToObject(SourceFile* sourceFile,
                          jsonConversionNotifier* notifier);
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>> convertToObject(
    SourceFile* sourceFile);
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
convertObjectLiteralExpressionToJson(
    SourceFile* sourceFile, bool returnValue, Node* node,
    const CommandLineOption* objectOption,
    jsonConversionNotifier* notifier);
std::pair<std::vector<std::string>, std::vector<Diagnostic*>>
getExtendsConfigPathOrArray(const CompilerOptionsValue& value,
                            ParseConfigHost* host, std::string_view basePath,
                            std::string_view configFileName,
                            PropertyAssignment* propertyAssignment,
                            Node* valueExpression, SourceFile* sourceFile);
std::pair<std::string, std::vector<Diagnostic*>> getExtendsConfigPath(
    std::string_view extendedConfigIn, ParseConfigHost* host,
    std::string_view basePath, Node* valueExpression,
    SourceFile* sourceFile);
std::string directoryOfCombinedPath(std::string_view fileName,
                                    std::string_view basePath);
CompilerOptions* getDefaultCompilerOptions(std::string_view configFileName);
TypeAcquisition* getDefaultTypeAcquisition(std::string_view configFileName);
std::pair<CompilerOptions*, std::vector<Diagnostic*>>
convertCompilerOptionsFromJsonWorker(const CompilerOptionsValue& jsonOptions,
                                     std::string_view basePath,
                                     std::string_view configFileName);
std::pair<TypeAcquisition*, std::vector<Diagnostic*>>
convertTypeAcquisitionFromJsonWorker(const CompilerOptionsValue& jsonOptions,
                                     std::string_view basePath,
                                     std::string_view configFileName);
std::pair<parsedTsconfig*, std::vector<Diagnostic*>> parseOwnConfigOfJson(
    JsonObjectPtr json, ParseConfigHost* host, std::string_view basePath,
    std::string_view configFileName);
ParsedCommandLine* parseJsonConfigFileContentWorker(
    JsonObjectPtr json, TsConfigSourceFile* sourceFile, ParseConfigHost* host,
    std::string_view basePath, CompilerOptions* existingOptions,
    const JsonObjectPtr& existingOptionsRaw, std::string_view configFileName,
    const std::vector<tspath::Path>& resolutionStack,
    ExtendedConfigCache* extendedConfigCache);
bool isStringValue(const CompilerOptionsValue& value);
std::string stringifyJson(const CompilerOptionsValue& value);
std::string jsonEscape(std::string_view s);
std::pair<std::vector<std::string>, std::vector<Diagnostic*>> validateSpecs(
    const CompilerOptionsValue& specs, bool disallowTrailingRecursion,
    SourceFile* jsonSourceFile, std::string_view specKey);
Diagnostic* createDiagnosticAtProjectReferenceProperty(
    TsConfigSourceFile* sourceFile, int index, std::string_view propertyName,
    const DiagnosticMessage* message, std::vector<std::string> args);
Node* getContentMapperSyntax(SourceFile* sourceFile, int index,
                             std::string_view subKey);
Node* getContentMappersKeySyntax(SourceFile* sourceFile);
Node* getContentMapperExtensionSyntax(SourceFile* sourceFile, int index,
                                      std::string_view ext);
Diagnostic* setContentMapperDiagnosticLocation(Diagnostic* diagnostic,
                                               SourceFile* sourceFile,
                                               Node* node);
std::optional<std::vector<std::string>>
getSubstitutedStringArrayWithConfigDirTemplate(
    const std::vector<std::string>& list, std::string_view basePath);
void handleOptionConfigDirTemplateSubstitution(
    CompilerOptions* compilerOptions, std::string_view basePath);
std::pair<optionParser*, std::vector<Diagnostic*>>
convertOptionsFromJsonImpl(const CommandLineOptionNameMap& optionsNameMap,
                           const CompilerOptionsValue& jsonOptions,
                           std::string_view basePath, optionParser* result);

}  // namespace

// The root-level option decls — tsconfigparsing.go:36-92. They are static
// const objects (pointer identity matters: Go compares
// `option == extendsOptionDeclaration`).

const CommandLineOption& compilerOptionsDeclaration() {
	static const CommandLineOption o{
	    .Name = "compilerOptions",
	    .Kind = CommandLineOptionTypeObject,
	};
	return o;
}
const CommandLineOption& compileOnSaveCommandLineOption() {
	static const CommandLineOption o{
	    .Name = "compileOnSave",
	    .Kind = CommandLineOptionTypeBoolean,
	    .DefaultValueDescription = CompilerOptionsValue(false),
	};
	return o;
}
const CommandLineOption& extendsOptionDeclaration() {
	static const CommandLineOption element{
	    .Name = "extends",
	    .Kind = CommandLineOptionTypeString,
	};
	static const CommandLineOptionNameMap elementMap =
	    commandLineOptionsToMap({&element});
	static const CommandLineOption o{
	    .Name = "extends",
	    .Kind = CommandLineOptionTypeListOrElement,
	    .Category = File_Management,
	    .ElementOptions = elementMap,
	};
	return o;
}

const CommandLineOption& tsconfigRootOptionsMap() {
	static const CommandLineOption referencesOption{
	    .Name = "references",
	    .Kind = CommandLineOptionTypeList,  // should be a list of projectReference
	};
	static const CommandLineOption contentMappersOption{
	    .Name = "contentMappers",
	    .Kind = CommandLineOptionTypeList,  // list of content mapper objects
	};
	static const CommandLineOption filesOption{
	    .Name = "files",
	    .Kind = CommandLineOptionTypeList,
	};
	static const CommandLineOption includeOption{
	    .Name = "include",
	    .Kind = CommandLineOptionTypeList,
	};
	static const CommandLineOption excludeOption{
	    .Name = "exclude",
	    .Kind = CommandLineOptionTypeList,
	};
	static const CommandLineOptionNameMap elementMap =
	    commandLineOptionsToMap(
	        {&compilerOptionsDeclaration(), &typeAcquisitionDeclaration(),
	         &extendsOptionDeclaration(), &referencesOption,
	         &contentMappersOption, &filesOption, &includeOption,
	         &excludeOption, &compileOnSaveCommandLineOption()});
	static const CommandLineOption o{
	    .Name = "undefined",  // should never be needed since this is root
	    .Kind = CommandLineOptionTypeObject,
	    .ElementOptions = elementMap,
	};
	return o;
}

// CommandLineCompilerOptionsMap — tsconfigparsing.go:624.
const CommandLineOptionNameMap& CommandLineCompilerOptionsMap() {
	static const CommandLineOptionNameMap m =
	    commandLineOptionsToMap(OptionsDeclarations());
	return m;
}

// matchesExclude — tsconfigparsing.go:108.
bool configFileSpecs::matchesExclude(
    std::string_view fileName,
    const tspath::ComparePathsOptions& comparePathsOptions) const {
	if (validatedExcludeSpecs.empty()) {
		return false;
	}
	module::vfsmatch::SpecMatcher* excludeMatcher =
	    module::vfsmatch::NewSpecMatcher(
	        validatedExcludeSpecs, comparePathsOptions.currentDirectory,
	        module::vfsmatch::Usage::Exclude,
	        comparePathsOptions.useCaseSensitiveFileNames);
	if (excludeMatcher == nullptr) {
		return false;
	}
	if (excludeMatcher->MatchString(fileName)) {
		return true;
	}
	if (!tspath::hasExtension(fileName)) {
		if (excludeMatcher->MatchString(
		        tspath::ensureTrailingDirectorySeparator(fileName))) {
			return true;
		}
	}
	return false;
}

// getMatchedIncludeSpec — tsconfigparsing.go:127.
std::string configFileSpecs::getMatchedIncludeSpec(
    std::string_view fileName,
    const tspath::ComparePathsOptions& comparePathsOptions) const {
	if (validatedIncludeSpecs.empty()) {
		return "";
	}
	for (size_t index = 0; index < validatedIncludeSpecs.size(); index++) {
		const auto& spec = validatedIncludeSpecs[index];
		module::vfsmatch::SpecMatcher* includeMatcher =
		    module::vfsmatch::NewSpecMatcher(
		        {spec}, comparePathsOptions.currentDirectory,
		        module::vfsmatch::Usage::Files,
		        comparePathsOptions.useCaseSensitiveFileNames);
		if (includeMatcher != nullptr && includeMatcher->MatchString(fileName)) {
			return validatedIncludeSpecsBeforeSubstitution[index];
		}
	}
	return "";
}

// getMatchedFileSpec — tsconfigparsing.go:140.
std::string configFileSpecs::getMatchedFileSpec(
    std::string_view fileName,
    const tspath::ComparePathsOptions& comparePathsOptions) const {
	if (validatedFilesSpec.empty()) {
		return "";
	}
	tspath::Path filePath = tspath::toPath(
	    fileName, comparePathsOptions.currentDirectory,
	    comparePathsOptions.useCaseSensitiveFileNames);
	for (size_t index = 0; index < validatedFilesSpec.size(); index++) {
		const auto& spec = validatedFilesSpec[index];
		if (tspath::toPath(spec, comparePathsOptions.currentDirectory,
		                   comparePathsOptions.useCaseSensitiveFileNames) ==
		    filePath) {
			return validatedFilesSpecBeforeSubstitution[index];
		}
	}
	return "";
}

// ExtendedFileNames — tsconfigparsing.go:163.
std::vector<std::string> ExtendedConfigCacheEntry::ExtendedFileNames() const {
	if (extendedResult != nullptr) {
		return extendedResult->ExtendedSourceFiles;
	}
	return {};
}

// NewTsconfigSourceFileFromFilePath — tsconfigparsing.go:296.
TsConfigSourceFile* NewTsconfigSourceFileFromFilePath(
    std::string_view configFileName, const tspath::Path& configPath,
    std::string_view configSourceText) {
	SourceFile* sourceFile = tsc::parseSourceFile(
	    SourceFileParseOptions{
	        .FileName = std::string(configFileName),
	        .Path = configPath,
	    },
	    configSourceText, ScriptKind::JSON);
	return new TsConfigSourceFile{
	    .SourceFile = sourceFile,
	};
}

namespace {

// parseOwnConfigOfJsonSourceFile — tsconfigparsing.go:178.
std::pair<parsedTsconfig*, std::vector<Diagnostic*>>
parseOwnConfigOfJsonSourceFile(SourceFile* sourceFile, ParseConfigHost* host,
                               std::string_view basePath,
                               std::string_view configFileName) {
	CompilerOptions* compilerOptions =
	    getDefaultCompilerOptions(configFileName);
	TypeAcquisition* typeAcquisition =
	    getDefaultTypeAcquisition(configFileName);
	CompilerOptionsValue extendedConfigPath;
	std::vector<Node*> rootCompilerOptions;
	std::vector<Diagnostic*> errors;

	jsonConversionNotifier notifier;
	notifier.rootOptions = &tsconfigRootOptionsMap();
	notifier.onPropertySet =
	    [&compilerOptions, &typeAcquisition, &extendedConfigPath,
	     &rootCompilerOptions, sourceFile, host, basePath,
	     configFileName](
	        std::string_view keyText, const CompilerOptionsValue& valueIn,
	        PropertyAssignment* propertyAssignment,
	        const CommandLineOption* parentOption,
	        const CommandLineOption* option)
	    -> std::pair<CompilerOptionsValue, std::vector<Diagnostic*>> {
		// Ensure value is verified except for extends which is handled in its
		// own way for error reporting
		CompilerOptionsValue value = valueIn;
		std::vector<Diagnostic*> propertySetErrors;
		if (option != nullptr && option != &extendsOptionDeclaration()) {
			auto [v, e] = convertJsonOption(
			    option, value, basePath, propertyAssignment,
			    propertyAssignment->Initializer, sourceFile);
			value = v;
			propertySetErrors.insert(propertySetErrors.end(), e.begin(),
			                         e.end());
		}
		if (parentOption != nullptr && parentOption->Name != "undefined" &&
		    !value.isNil()) {
			if (option != nullptr && !option->Name.empty()) {
				std::vector<Diagnostic*> parseDiagnostics;
				if (parentOption->Name == "compilerOptions") {
					parseDiagnostics = ParseCompilerOptions(
					    option->Name, value, compilerOptions);
				} else if (parentOption->Name == "typeAcquisition") {
					parseDiagnostics = ParseTypeAcquisition(
					    option->Name, value, typeAcquisition);
				}
				propertySetErrors.insert(propertySetErrors.end(),
				                         parseDiagnostics.begin(),
				                         parseDiagnostics.end());
			} else if (!keyText.empty() && extraKeyDiagnostics(parentOption->Name) != nullptr) {
				const DiagnosticMessage* unknownNameDiag =
				    extraKeyDiagnostics(parentOption->Name);
				if (!parentOption->ElementOptions.m.empty()) {
					const CommandLineOption* possibleOption =
					    parentOption->ElementOptions.Get(keyText);
					if (possibleOption == nullptr) {
						possibleOption = parentOption->ElementOptions
						                     .GetSpellingSuggestion(
						                         keyText);
					}
					if (possibleOption != nullptr && possibleOption->Name != keyText) {
						propertySetErrors.push_back(
						    CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
						        sourceFile, propertyAssignment->name,
						        extraKeyDidYouMeanDiagnostics(
						            parentOption->Name),
						        {std::string(keyText),
						         possibleOption->Name}));
					} else {
						propertySetErrors.push_back(
						    createUnknownOptionError(
						        keyText, unknownNameDiag,
						        "" /*unknownOptionErrorText*/,
						        propertyAssignment->name, sourceFile,
						        nullptr /*alternateMode*/,
						        nullptr /*unknownDidYouMeanDiagnostic*/,
						        {} /*optionsNameMap*/));
					}
				} else {
					// errors = append(errors, ast.NewCompilerDiagnostic(diagnostics.Unknown_compiler_option_0_Did_you_mean_1, keyText, core.FindKey(parentOption.ElementOptions, keyText)))
				}
			}
		} else if (parentOption == &tsconfigRootOptionsMap()) {
			if (option == &extendsOptionDeclaration()) {
				auto [configPath, err] = getExtendsConfigPathOrArray(
				    value, host, basePath, configFileName,
				    propertyAssignment, propertyAssignment->Initializer,
				    sourceFile);
				extendedConfigPath = CompilerOptionsValue(
				    JsonStrList(std::move(configPath)));
				propertySetErrors.insert(propertySetErrors.end(),
				                         err.begin(), err.end());
			} else if (option == nullptr) {
				if (keyText == "excludes") {
					propertySetErrors.push_back(
					    CreateDiagnosticForNodeInSourceFile(
					        sourceFile, propertyAssignment->name,
					        Unknown_option_excludes_Did_you_mean_exclude));
				}
				if (findIn(optionsForCompiler(),
				           [&](const CommandLineOption* o) {
					           return o->Name == keyText;
				           }) != nullptr) {
					rootCompilerOptions.push_back(
					    propertyAssignment->name);
				}
			}
		}
		return {value, propertySetErrors};
	};

	auto [json, err] = convertConfigFileToObject(sourceFile, &notifier);
	errors.insert(errors.end(), err.begin(), err.end());
	if (auto* jsonObject = json.get<JsonObjectPtr>();
	    !rootCompilerOptions.empty() && jsonObject != nullptr &&
	    !(*jsonObject)->Has("compilerOptions")) {
		errors.push_back(CreateDiagnosticForNodeInSourceFile(
		    sourceFile, rootCompilerOptions[0],
		    X_0_should_be_set_inside_the_compilerOptions_object_of_the_config_json_file,
		    {getTextOfPropertyName(rootCompilerOptions[0])}));
	}
	return {new parsedTsconfig{
	            .raw = json,
	            .options = compilerOptions,
	            .typeAcquisition = typeAcquisition,
	            .extendedConfigPath = extendedConfigPath,
	        },
	        errors};
}

// convertConfigFileToObject — tsconfigparsing.go:311.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
convertConfigFileToObject(SourceFile* sourceFile,
                          jsonConversionNotifier* notifier) {
	Node* rootExpression = nullptr;
	if (sourceFile->Statements != nullptr &&
	    !sourceFile->Statements->nodes.empty()) {
		rootExpression =
		    sourceFile->Statements->nodes[0]->expression();
	}
	if (rootExpression != nullptr && rootExpression->kind != Kind::ObjectLiteralExpression) {
		std::string baseFileName = "tsconfig.json";
		if (tspath::getBaseFileName(sourceFile->FileName()) ==
		    "jsconfig.json") {
			baseFileName = "jsconfig.json";
		}
		std::vector<Diagnostic*> errors = {
		    CreateDiagnosticForNodeInSourceFile(
		        sourceFile, rootExpression,
		        The_root_value_of_a_0_file_must_be_an_object,
		        {baseFileName})};
		// Last-ditch error recovery. Somewhat useful because the JSON parser
		// will recover from some parse errors by synthesizing a top-level
		// array literal expression. There's a reasonable chance the first
		// element of that array is a well-formed configuration object, made
		// into an array element by stray characters.
		if (isArrayLiteralExpression(rootExpression)) {
			Node* firstObject = nullptr;
			for (Node* e : rootExpression->elements()) {
				if (isObjectLiteralExpression(e)) {
					firstObject = e;
					break;
				}
			}
			if (firstObject != nullptr) {
				return convertToJson(sourceFile, firstObject,
				                     true /*returnValue*/, notifier);
			}
		}
		return {CompilerOptionsValue(std::make_shared<JsonObject>()),
		        errors};
	}
	return convertToJson(sourceFile, rootExpression, true, notifier);
}

// isCompilerOptionsValue — tsconfigparsing.go:341.
bool isCompilerOptionsValue(const CommandLineOption* option,
                            const CompilerOptionsValue& value) {
	if (option != nullptr) {
		if (value.isNil()) {
			return !option->DisallowNullOrUndefined();
		}
		if (option->Kind == CommandLineOptionTypeList) {
			return std::holds_alternative<JsonArray>(value.v) ||
			       std::holds_alternative<JsonStrList>(value.v);
		}
		if (option->Kind == CommandLineOptionTypeListOrElement) {
			if (std::holds_alternative<JsonArray>(value.v) ||
			    std::holds_alternative<JsonStrList>(value.v)) {
				return true;
			}
			return isCompilerOptionsValue(option->Elements(), value);
		}
		if (option->Kind == CommandLineOptionTypeString) {
			return std::holds_alternative<std::string>(value.v);
		}
		if (option->Kind == CommandLineOptionTypeBoolean) {
			return std::holds_alternative<bool>(value.v);
		}
		if (option->Kind == CommandLineOptionTypeNumber) {
			return std::holds_alternative<double>(value.v);
		}
		if (option->Kind == CommandLineOptionTypeObject) {
			return std::holds_alternative<JsonObjectPtr>(value.v);
		}
		if (option->Kind == CommandLineOptionTypeEnum && std::holds_alternative<std::string>(value.v)) {
			return true;
		}
	}
	return false;
}

}  // namespace

// validateJsonOptionValue — tsconfigparsing.go:375.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
validateJsonOptionValue(const CommandLineOption* opt,
                        const CompilerOptionsValue& val,
                        Node* valueExpression, SourceFile* sourceFile) {
	if (val.isNil()) {
		return {CompilerOptionsValue(), {}};
	}

	std::vector<Diagnostic*> errors;

	if (opt->extraValidation_ == extraValidationSpec) {
		if (const DiagnosticMessage* diag =
		        specToDiagnostic(val.asString(), false);
		    diag != nullptr) {
			errors.push_back(
			    CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
			        sourceFile, valueExpression, diag));
		}
	} else if (opt->extraValidation_ == extraValidationLocale) {
		if (!locale::parse(val.asString()).second) {
			errors.push_back(
			    CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
			        sourceFile, valueExpression,
			        Locale_must_be_an_IETF_BCP_47_language_tag_Examples_Colon_0_1,
			        {"en", "ja-jp"}));
		}
	}

	if (!errors.empty()) {
		return {CompilerOptionsValue(), errors};
	}
	return {val, {}};
}

namespace {

// convertJsonOptionOfListType — tsconfigparsing.go:404.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
convertJsonOptionOfListType(const CommandLineOption* option,
                            const CompilerOptionsValue& values,
                            std::string_view basePath,
                            PropertyAssignment* propertyAssignment,
                            Node* valueExpression, SourceFile* sourceFile) {
	Node* expression = nullptr;
	std::vector<Diagnostic*> errors;
	if (const auto* arr = std::get_if<JsonArray>(&values.v)) {
		JsonArray mappedValues;
		mappedValues.reserve(arr->size());
		for (size_t index = 0; index < arr->size(); index++) {
			const auto& v = (*arr)[index];
			if (valueExpression != nullptr) {
				auto elements = valueExpression->elements();
				expression =
				    index < elements.size() ? elements[index] : nullptr;
			}
			auto [result, err] = convertJsonOption(
			    option->Elements(), v, basePath, propertyAssignment,
			    expression, sourceFile);
			errors.insert(errors.end(), err.begin(), err.end());
			mappedValues.push_back(result);
		}
		JsonArray filteredValues = mappedValues;
		if (!option->listPreserveFalsyValues) {
			JsonArray filtered;
			for (const auto& v : mappedValues) {
				// v != nil && v != false && v != 0 && v != ""
				bool falsy = v.isNil() ||
				             (std::get_if<bool>(&v.v) != nullptr &&
				              !*std::get_if<bool>(&v.v)) ||
				             (std::get_if<double>(&v.v) != nullptr &&
				              *std::get_if<double>(&v.v) == 0) ||
				             (std::get_if<int64_t>(&v.v) != nullptr &&
				              *std::get_if<int64_t>(&v.v) == 0) ||
				             (std::get_if<std::string>(&v.v) != nullptr && std::get_if<std::string>(&v.v)->empty());
				if (!falsy) {
					filtered.push_back(v);
				}
			}
			filteredValues = std::move(filtered);
		}
		return {filteredValues, errors};
	}
	return {CompilerOptionsValue(), errors};
}

// startsWithConfigDirTemplate — tsconfigparsing.go:436.
bool startsWithConfigDirTemplate(const CompilerOptionsValue& value) {
	const auto* str = std::get_if<std::string>(&value.v);
	if (str == nullptr) {
		return false;
	}
	return tspath::toFileNameLowerCase(*str).starts_with(
	    tspath::toFileNameLowerCase(configDirTemplate));
}
// normalizeNonListOptionValue — tsconfigparsing.go:444.
CompilerOptionsValue normalizeNonListOptionValue(
    const CommandLineOption* option, std::string_view basePath,
    const CompilerOptionsValue& value) {
	if (option->IsFilePath) {
		std::string v =
		    tspath::normalizeSlashes(value.asString());
		if (!startsWithConfigDirTemplate(v)) {
			v = tspath::getNormalizedAbsolutePath(v, basePath);
		}
		if (v.empty()) {
			v = ".";
		}
		return v;
	}
	return value;
}

// convertJsonOption — tsconfigparsing.go:457.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>> convertJsonOption(
    const CommandLineOption* opt, const CompilerOptionsValue& value,
    std::string_view basePath, PropertyAssignment* propertyAssignment,
    Node* valueExpression, SourceFile* sourceFile) {
	if (opt->IsCommandLineOnly) {
		Node* nodeValue = nullptr;
		if (propertyAssignment != nullptr) {
			nodeValue = propertyAssignment->name;
		}
		if (sourceFile == nullptr && nodeValue == nullptr) {
			return {CompilerOptionsValue(),
			        {newCompilerDiagnostic(
			            Option_0_can_only_be_specified_on_command_line,
			            {opt->Name})}};
		}
		return {CompilerOptionsValue(),
		        {CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
		            sourceFile, nodeValue,
		            Option_0_can_only_be_specified_on_command_line,
		            {opt->Name})}};
	}
	if (isCompilerOptionsValue(opt, value)) {
		if (opt->Kind == CommandLineOptionTypeList) {
			return convertJsonOptionOfListType(
			    opt, value, basePath, propertyAssignment, valueExpression,
			    sourceFile);  // as ArrayLiteralExpression | undefined
		}
		if (opt->Kind == CommandLineOptionTypeListOrElement) {
			if (std::holds_alternative<JsonArray>(value.v) ||
			    std::holds_alternative<JsonStrList>(value.v)) {
				return convertJsonOptionOfListType(
				    opt, value, basePath, propertyAssignment,
				    valueExpression, sourceFile);
			}
			return convertJsonOption(opt->Elements(), value, basePath,
			                         propertyAssignment, valueExpression,
			                         sourceFile);
		}
		if (opt->Kind == CommandLineOptionTypeEnum) {
			if (value.isNil()) {
				return {CompilerOptionsValue(), {}};
			}
			return convertJsonOptionOfEnumType(opt, value.asString(),
			                                   valueExpression, sourceFile);
		}

		auto [validatedValue, errors] = validateJsonOptionValue(
		    opt, value, valueExpression, sourceFile);
		if (!errors.empty() || validatedValue.isNil()) {
			return {validatedValue, errors};
		}
		return {normalizeNonListOptionValue(opt, basePath, validatedValue),
		        errors};
	}
	return {CompilerOptionsValue(),
	        {CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
	            sourceFile, valueExpression,
	            Compiler_option_0_requires_a_value_of_type_1,
	            {opt->Name, getCompilerOptionValueTypeString(opt)})}};
}

// directoryOfCombinedPath — tsconfigparsing.go:694.
std::string directoryOfCombinedPath(std::string_view fileName,
                                    std::string_view basePath) {
	// Use the `getNormalizedAbsolutePath` function to avoid canonicalizing
	// the path, as it must remain noncanonical until consistent casing
	// errors are reported
	return tspath::getDirectoryPath(
	    tspath::getNormalizedAbsolutePath(fileName, basePath));
}

// getExtendsConfigPathOrArray — tsconfigparsing.go:504.
std::pair<std::vector<std::string>, std::vector<Diagnostic*>>
getExtendsConfigPathOrArray(const CompilerOptionsValue& value,
                            ParseConfigHost* host, std::string_view basePath,
                            std::string_view configFileName,
                            PropertyAssignment* propertyAssignment,
                            Node* valueExpression, SourceFile* sourceFile) {
	std::vector<std::string> extendedConfigPathArray;
	std::string newBase(basePath);
	if (!configFileName.empty()) {
		newBase = directoryOfCombinedPath(configFileName, basePath);
	}
	if (value.isNil()) {
		auto [v, errors] = convertJsonOption(
		    &extendsOptionDeclaration(), value, basePath, propertyAssignment,
		    valueExpression, sourceFile);
		return {extendedConfigPathArray, errors};
	}
	if (std::holds_alternative<std::string>(value.v)) {
		auto [val, err] = getExtendsConfigPath(
		    value.asString(), host, newBase, valueExpression, sourceFile);
		if (!val.empty()) {
			extendedConfigPathArray.push_back(val);
		}
		return {extendedConfigPathArray, err};
	}
	std::vector<Diagnostic*> errors;
	if (std::holds_alternative<JsonArray>(value.v)) {
		const auto& arr = std::get<JsonArray>(value.v);
		for (size_t index = 0; index < arr.size(); index++) {
			const auto& fileName = arr[index];
			Node* expression = nullptr;
			if (valueExpression != nullptr) {
				auto elements = valueExpression->elements();
				expression =
				    index < elements.size() ? elements[index] : nullptr;
			}
			if (std::holds_alternative<std::string>(fileName.v)) {
				auto [val, err] = getExtendsConfigPath(
				    fileName.asString(), host, newBase, expression,
				    sourceFile);
				if (!val.empty()) {
					extendedConfigPathArray.push_back(val);
				}
				errors.insert(errors.end(), err.begin(), err.end());
			} else {
				auto [v, err] = convertJsonOption(
				    extendsOptionDeclaration().Elements(), value,
				    basePath, propertyAssignment, expression,
				    sourceFile);
				errors.insert(errors.end(), err.begin(), err.end());
			}
		}
	} else {
		auto [v, errs] = convertJsonOption(
		    &extendsOptionDeclaration(), value, basePath, propertyAssignment,
		    valueExpression, sourceFile);
		errors = errs;
	}
	return {extendedConfigPathArray, errors};
}

// getExtendsConfigPath — tsconfigparsing.go:553.
std::pair<std::string, std::vector<Diagnostic*>> getExtendsConfigPath(
    std::string_view extendedConfigIn, ParseConfigHost* host,
    std::string_view basePath, Node* valueExpression,
    SourceFile* sourceFile) {
	std::string extendedConfig = tspath::normalizeSlashes(extendedConfigIn);
	std::vector<Diagnostic*> errors;
	SourceFile* errorFile = nullptr;
	if (sourceFile != nullptr) {
		errorFile = sourceFile;
	}
	if (tspath::isRootedDiskPath(extendedConfig) ||
	    extendedConfig.starts_with("./") || extendedConfig.starts_with("../")) {
		std::string extendedConfigPath = tspath::getNormalizedAbsolutePath(
		    extendedConfig, basePath);
		if (!host->FS()->FileExists(extendedConfigPath) &&
		    !extendedConfigPath.ends_with(tspath::extensionJson)) {
			extendedConfigPath += std::string(tspath::extensionJson);
			if (!host->FS()->FileExists(extendedConfigPath)) {
				errors.push_back(
				    CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
				        errorFile, valueExpression, File_0_not_found,
				        {extendedConfig}));
				return {"", errors};
			}
		}
		return {extendedConfigPath, errors};
	}
	// If the path isn't a rooted or relative path, resolve like a module
	if (auto resolved = module::ResolveConfig(
	        extendedConfig,
	        tspath::combinePaths(basePath, {"tsconfig.json"}), host);
	    resolved->IsResolved()) {
		return {resolved->ResolvedFileName, errors};
	}
	if (extendedConfig.empty()) {
		errors.push_back(
		    CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
		        errorFile, valueExpression,
		        Compiler_option_0_cannot_be_given_an_empty_string,
		        {"extends"}));
	} else {
		errors.push_back(
		    CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
		        errorFile, valueExpression, File_0_not_found,
		        {extendedConfig}));
	}
	return {"", errors};
}

// convertArrayLiteralExpressionToJson — tsconfigparsing.go:664.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
convertArrayLiteralExpressionToJson(SourceFile* sourceFile,
                                    const std::vector<Node*>& elements,
                                    const CommandLineOption* elementOption,
                                    bool returnValue) {
	if (!returnValue) {
		for (Node* element : elements) {
			convertPropertyValueToJson(sourceFile, element, elementOption,
			                           returnValue, nullptr);
		}
		return {CompilerOptionsValue(), {}};
	}
	// Filter out invalid values
	if (elements.empty()) {
		// Always return an empty array, even if elements is nil.
		// The parser will produce nil slices instead of allocating empty
		// ones.
		return {JsonArray{}, {}};
	}
	std::vector<Diagnostic*> errors;
	JsonArray value;
	for (Node* element : elements) {
		auto [convertedValue, err] = convertPropertyValueToJson(
		    sourceFile, element, elementOption, returnValue, nullptr);
		errors.insert(errors.end(), err.begin(), err.end());
		if (!convertedValue.isNil()) {
			value.push_back(convertedValue);
		}
	}
	return {value, errors};
}

// convertObjectLiteralExpressionToJson — tsconfigparsing.go:742.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
convertObjectLiteralExpressionToJson(SourceFile* sourceFile, bool returnValue,
                                     Node* node,
                                     const CommandLineOption* objectOption,
                                     jsonConversionNotifier* notifier) {
	JsonObjectPtr result;
	if (returnValue) {
		result = std::make_shared<JsonObject>();
	}
	std::vector<Diagnostic*> errors;
	for (Node* element : node->properties()) {
		if (element->kind != Kind::PropertyAssignment) {
			errors.push_back(newDiagnostic(
			    sourceFile, element->loc,
			    Property_assignment_expected));
			continue;
		}

		if (Node* token = element->questionToken(); token != nullptr) {
			errors.push_back(newDiagnostic(
			    sourceFile, token->loc,
			    The_0_modifier_can_only_be_used_in_TypeScript_files,
			    {"?"}));
		}
		std::string textOfKey;
		if (!isComputedNonLiteralName(element->name())) {
			tryGetTextOfPropertyName(element->name(), textOfKey);
		}
		std::string keyText = textOfKey;
		const CommandLineOption* option = nullptr;
		if (!keyText.empty() && objectOption != nullptr &&
		    !objectOption->ElementOptions.m.empty()) {
			option = objectOption->ElementOptions.Get(keyText);
			if (option != nullptr && option->Name != keyText) {
				option = nullptr;
			}
		}
		auto* pa = element->as<PropertyAssignment>();
		auto [value, err] = convertPropertyValueToJson(
		    sourceFile, pa->Initializer, option, returnValue, notifier);
		errors.insert(errors.end(), err.begin(), err.end());
		if (!keyText.empty()) {
			if (returnValue) {
				result->Set(keyText, value);
			}
			// Notify key value set, if user asked for it
			if (notifier != nullptr) {
				auto [v2, err2] = notifier->onPropertySet(
				    keyText, value, pa, objectOption, option);
				errors.insert(errors.end(), err2.begin(), err2.end());
			}
		}
	}
	return {result, errors};
}

// convertToJson — tsconfigparsing.go:794.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>> convertToJson(
    SourceFile* sourceFile, Node* rootExpression, bool returnValue,
    jsonConversionNotifier* notifier) {
	if (rootExpression == nullptr) {
		if (returnValue) {
			return {CompilerOptionsValue(std::monostate{}), {}};
		}
		return {CompilerOptionsValue(), {}};
	}
	const CommandLineOption* rootOptions = nullptr;
	if (notifier != nullptr) {
		rootOptions = notifier->rootOptions;
	}
	return convertPropertyValueToJson(sourceFile, rootExpression, rootOptions,
	                                  returnValue, notifier);
}

// convertPropertyValueToJson — tsconfigparsing.go:818.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
convertPropertyValueToJson(SourceFile* sourceFile, Node* valueExpression,
                           const CommandLineOption* option, bool returnValue,
                           jsonConversionNotifier* notifier) {
	switch (valueExpression->kind) {
	case Kind::TrueKeyword:
		return {true, {}};
	case Kind::FalseKeyword:
		return {false, {}};
	case Kind::NullKeyword:  // todo: how to manage null
		return {CompilerOptionsValue(), {}};
	case Kind::StringLiteral:
		if (!isDoubleQuotedStringLocal(valueExpression)) {
			return {valueExpression->text(),
			        {newDiagnostic(
			            sourceFile, valueExpression->loc,
			            String_literal_with_double_quotes_expected)}};
		}
		return {valueExpression->text(), {}};
	case Kind::NumericLiteral:
		return {tsc::numberFromString(valueExpression->text()).v, {}};
	case Kind::PrefixUnaryExpression: {
		auto* pue = valueExpression->as<PrefixUnaryExpression>();
		if (pue->Operator != Kind::MinusToken ||
		    pue->Operand->kind != Kind::NumericLiteral) {
			break;  // not valid JSON syntax
		}
		return {-tsc::numberFromString(pue->Operand->text()).v, {}};
	}
	case Kind::ObjectLiteralExpression:
		// Currently having element option declaration in the tsconfig with
		// type "object" determines if it needs
		// onSetValidOptionKeyValueInParent callback or not. At moment there
		// are only "compilerOptions", "typeAcquisition" and "typingOptions"
		// that satisfies it and need it to modify options set in them (for
		// normalizing file paths) vs what we set in the json. If need arises,
		// we can modify this interface and callbacks as needed
		return convertObjectLiteralExpressionToJson(
		    sourceFile, returnValue, valueExpression, option, notifier);
	case Kind::ArrayLiteralExpression: {
		return convertArrayLiteralExpressionToJson(
		    sourceFile, valueExpression->elements(), option, returnValue);
	}
	default:
		break;
	}
	// Not in expected format
	if (option != nullptr) {
		return {CompilerOptionsValue(),
		        {newDiagnostic(
		            sourceFile, valueExpression->loc,
		            Compiler_option_0_requires_a_value_of_type_1,
		            {option->Name,
		             getCompilerOptionValueTypeString(option)})}};
	}
	return {CompilerOptionsValue(),
	        {newDiagnostic(
	            sourceFile, valueExpression->loc,
	            Property_value_can_only_be_string_literal_numeric_literal_true_false_null_object_literal_or_array_literal)}};
}

// convertToObject — tsconfigparsing.go:919.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>> convertToObject(
    SourceFile* sourceFile) {
	Node* rootExpression = nullptr;
	if (sourceFile->Statements != nullptr &&
	    !sourceFile->Statements->nodes.empty()) {
		rootExpression =
		    sourceFile->Statements->nodes[0]->expression();
	}
	return convertToJson(sourceFile, rootExpression,
	                     true /*returnValue*/, nullptr /*notifier*/);
}

// normalizeJsonValue — tsconfigparsing.go:882.
CompilerOptionsValue normalizeJsonValue(const CompilerOptionsValue& value) {
	if (auto* p = std::get_if<JsonObjectPtr>(&value.v)) {
		auto obj = *p;
		for (const auto& key : obj->Keys()) {
			auto child = obj->Get(key);
			obj->Set(key, normalizeJsonValue(*child.first));
		}
		return value;
	}
	if (auto* m = std::get_if<JsonGoMapPtr>(&value.v)) {
		auto result = std::make_shared<JsonObject>();
		std::vector<std::string> keys;
		for (const auto& [k, v] : **m) keys.push_back(k);
		std::sort(keys.begin(), keys.end());
		for (const auto& key : keys) {
			result->Set(key, normalizeJsonValue((**m)[key]));
		}
		return CompilerOptionsValue(result);
	}
	if (auto* arr = std::get_if<JsonArray>(&value.v)) {
		JsonArray result(arr->size());
		for (size_t i = 0; i < arr->size(); i++) {
			result[i] = normalizeJsonValue((*arr)[i]);
		}
		return CompilerOptionsValue(std::move(result));
	}
	if (auto* sl = std::get_if<JsonStrList>(&value.v)) {
		// reflect.Slice branch — []string converts to []any.
		JsonArray result(sl->size());
		for (size_t i = 0; i < sl->size(); i++) {
			result[i] = CompilerOptionsValue((*sl)[i]);
		}
		return CompilerOptionsValue(std::move(result));
	}
	return value;
}

// getDefaultCompilerOptions — tsconfigparsing.go:927.
CompilerOptions* getDefaultCompilerOptions(std::string_view configFileName) {
	auto* options = new CompilerOptions{};
	if (!configFileName.empty() && tspath::getBaseFileName(configFileName) == "jsconfig.json") {
		options = new CompilerOptions{
		    .AllowJs = Tristate::True,
		    .MaxNodeModuleJsDepth = new int(2),
		    .SkipLibCheck = Tristate::True,
		    .NoEmit = Tristate::True,
		};
	}
	return options;
}

// getDefaultTypeAcquisition — tsconfigparsing.go:941.
TypeAcquisition* getDefaultTypeAcquisition(std::string_view configFileName) {
	auto* options = new TypeAcquisition{};
	if (!configFileName.empty() && tspath::getBaseFileName(configFileName) == "jsconfig.json") {
		options->Enable = Tristate::True;
	}
	return options;
}

// convertCompilerOptionsFromJsonWorker — tsconfigparsing.go:949.
std::pair<CompilerOptions*, std::vector<Diagnostic*>>
convertCompilerOptionsFromJsonWorker(const CompilerOptionsValue& jsonOptions,
                                     std::string_view basePath,
                                     std::string_view configFileName) {
	CompilerOptions* options = getDefaultCompilerOptions(configFileName);
	auto [p, errors] = convertOptionsFromJson(
	    CommandLineCompilerOptionsMap(), jsonOptions, basePath,
	    new compilerOptionsParser(options));
	if (!configFileName.empty()) {
		options->ConfigFilePath =
		    tspath::normalizeSlashes(configFileName);
	}
	return {options, errors};
}

// convertTypeAcquisitionFromJsonWorker — tsconfigparsing.go:958.
std::pair<TypeAcquisition*, std::vector<Diagnostic*>>
convertTypeAcquisitionFromJsonWorker(const CompilerOptionsValue& jsonOptions,
                                     std::string_view basePath,
                                     std::string_view configFileName) {
	TypeAcquisition* options = getDefaultTypeAcquisition(configFileName);
	auto [p, errors] = convertOptionsFromJson(
	    typeAcquisitionDeclaration().ElementOptions, jsonOptions, basePath,
	    new typeAcquisitionParser(options));
	return {options, errors};
}

}  // namespace

// convertOptionsFromJsonImpl — tsconfigparsing.go:634.
// (Not generic: optionParser here is the Go interface; the C++ body uses the
// virtual optionParser.)
std::pair<optionParser*, std::vector<Diagnostic*>>
convertOptionsFromJsonImpl(const CommandLineOptionNameMap& optionsNameMap,
                           const CompilerOptionsValue& jsonOptions,
                           std::string_view basePath, optionParser* result) {
	if (jsonOptions.isNil()) {
		return {result, {}};
	}
	auto* jsonMap = std::get_if<JsonObjectPtr>(&jsonOptions.v);
	if (jsonMap == nullptr) {
		// !!! probably should be an error
		return {result, {}};
	}
	std::vector<Diagnostic*> errors;
	for (const auto& key : (*jsonMap)->Keys()) {
		auto valueV = (*jsonMap)->Get(key);
		const auto& value = *valueV.first;
		const CommandLineOption* opt = optionsNameMap.Get(key);
		if (opt != nullptr && opt->Name != key) {
			// Case-insensitive match found but exact case doesn't match -
			// provide "did you mean" suggestion
			errors.push_back(
			    CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
			        nullptr, nullptr,
			        result->UnknownDidYouMeanDiagnostic(),
			        {key, opt->Name}));
			continue;
		}
		if (opt == nullptr) {
			errors.push_back(createUnknownOptionError(
			    key, result->UnknownOptionDiagnostic(), "", nullptr,
			    nullptr, nullptr,
			    result->UnknownDidYouMeanDiagnostic(), optionsNameMap));
			continue;
		}

		auto [convertJson, err] =
		    convertJsonOption(opt, value, basePath, nullptr, nullptr,
		                      nullptr);
		errors.insert(errors.end(), err.begin(), err.end());
		auto compilerOptionsErr = result->ParseOption(key, convertJson);
		errors.insert(errors.end(), compilerOptionsErr.begin(),
		              compilerOptionsErr.end());
	}
	return {result, errors};
}

namespace {

// parseOwnConfigOfJson — tsconfigparsing.go:964.
std::pair<parsedTsconfig*, std::vector<Diagnostic*>> parseOwnConfigOfJson(
    JsonObjectPtr json, ParseConfigHost* host, std::string_view basePath,
    std::string_view configFileName) {
	std::vector<Diagnostic*> errors;
	if (json->Has("excludes")) {
		errors.push_back(newCompilerDiagnostic(
		    Unknown_option_excludes_Did_you_mean_exclude));
	}
	auto [options, err] = convertCompilerOptionsFromJsonWorker(
	    json->GetOrZero("compilerOptions"), basePath, configFileName);
	auto [typeAcquisition, err2] = convertTypeAcquisitionFromJsonWorker(
	    json->GetOrZero("typeAcquisition"), basePath, configFileName);
	errors.insert(errors.end(), err.begin(), err.end());
	errors.insert(errors.end(), err2.begin(), err2.end());
	if (auto compileOnSave = json->Get("compileOnSave");
	    compileOnSave.second) {
		auto [converted, compileOnSaveErrors] = convertJsonOption(
		    &compileOnSaveCommandLineOption(), *compileOnSave.first,
		    basePath, nullptr, nullptr, nullptr);
		errors.insert(errors.end(), compileOnSaveErrors.begin(),
		              compileOnSaveErrors.end());
		json->Set("compileOnSave", converted);
	}
	std::vector<std::string> extendedConfigPath;
	auto extends = json->GetOrZero("extends");
	if (!extends.isNil() &&
	    !(std::get_if<std::string>(&extends.v) != nullptr && std::get_if<std::string>(&extends.v)->empty())) {
		auto [p, err3] = getExtendsConfigPathOrArray(
		    extends, host, basePath, configFileName, nullptr, nullptr,
		    nullptr);
		extendedConfigPath = p;
		errors.insert(errors.end(), err3.begin(), err3.end());
	}
	auto* parsedConfig = new parsedTsconfig{
	    .raw = CompilerOptionsValue(json),
	    .options = options,
	    .typeAcquisition = typeAcquisition,
	    .extendedConfigPath = CompilerOptionsValue(JsonStrList(
	        extendedConfigPath)),
	};
	return {parsedConfig, errors};
}

}  // namespace

// readJsonConfigFile — tsconfigparsing.go:996.
std::pair<TsConfigSourceFile*, std::vector<Diagnostic*>> readJsonConfigFile(
    std::string_view fileName, const tspath::Path& path,
    const std::function<std::pair<std::string, bool>(std::string_view)>& readFile) {
	auto [text, diagnostic] = tryReadFile(fileName, readFile, {});
	if (!text.empty()) {
		return {new TsConfigSourceFile{
		            .SourceFile = tsc::parseSourceFile(
		                SourceFileParseOptions{
		                    .FileName = std::string(fileName),
		                    .Path = path,
		                },
		                text, ScriptKind::JSON),
		        },
		        diagnostic};
	}
	auto* factory = new NodeFactory{};
	auto* file = new TsConfigSourceFile{
	    .SourceFile =
	        factory
	            ->newSourceFile(
	                SourceFileParseOptions{
	                    .FileName = std::string(fileName),
	                    .Path = path,
	                },
	                "", factory->newNodeList({}),
	                factory->newToken(Kind::EndOfFile))
	            ->as<SourceFile>(),
	};
	file->SourceFile->diagnostics = diagnostic;
	return {file, diagnostic};
}

// getExtendedConfig — tsconfigparsing.go:1015.
std::pair<parsedTsconfig*, std::vector<Diagnostic*>> getExtendedConfig(
    TsConfigSourceFile* sourceFile, std::string_view extendedConfigFileName,
    ParseConfigHost* host,
    const std::vector<tspath::Path>& resolutionStack,
    ExtendedConfigCache* extendedConfigCache, extendsResult* result) {
	std::vector<Diagnostic*> errors;
	tspath::Path extendedConfigPath = tspath::toPath(
	    extendedConfigFileName, host->GetCurrentDirectory(),
	    host->FS()->UseCaseSensitiveFileNames());

	ExtendedConfigCacheEntry* cacheEntry = nullptr;
	// Bypass the cache when we detect a cycle in the resolution stack.
	// The cache locks entries during parsing, and a cycle would cause the
	// same goroutine to re-lock the same entry, resulting in a deadlock.
	// Let parseConfig handle the circularity error via its own resolution
	// stack check.
	if (extendedConfigCache != nullptr && std::find(resolutionStack.begin(), resolutionStack.end(),
	              extendedConfigPath) == resolutionStack.end()) {
		cacheEntry = extendedConfigCache->GetExtendedConfig(
		    extendedConfigFileName, extendedConfigPath, resolutionStack,
		    host);
	} else {
		cacheEntry = ParseExtendedConfig(extendedConfigFileName,
		                                 extendedConfigPath, resolutionStack,
		                                 host, extendedConfigCache);
	}

	if (!cacheEntry->errors.empty()) {
		errors.insert(errors.end(), cacheEntry->errors.begin(),
		              cacheEntry->errors.end());
	}

	if (cacheEntry->extendedResult != nullptr) {
		if (sourceFile != nullptr) {
			result->extendedSourceFiles.Add(
			    cacheEntry->extendedResult->SourceFile->FileName());
			for (const auto& extendedSourceFile :
			     cacheEntry->extendedResult->ExtendedSourceFiles) {
				result->extendedSourceFiles.Add(extendedSourceFile);
			}
		}
	}
	return {cacheEntry->extendedConfig, errors};
}

// ParseExtendedConfig — tsconfigparsing.go:1052.
ExtendedConfigCacheEntry* ParseExtendedConfig(
    std::string_view fileName, const tspath::Path& path,
    const std::vector<tspath::Path>& resolutionStack, ParseConfigHost* host,
    ExtendedConfigCache* extendedConfigCache) {
	auto [extendedResult, readErrors] = readJsonConfigFile(
	    fileName, path,
	    [host](std::string_view f) -> std::pair<std::string, bool> {
		    auto r = host->FS()->ReadFile(f);
		    return {r.value_or(""), r.has_value()};
	    });
	auto* entry = new ExtendedConfigCacheEntry{
	    .extendedResult = extendedResult,
	};

	if (!readErrors.empty()) {
		entry->errors = readErrors;
		return entry;
	}

	if (auto parseDiagnostics = extendedResult->SourceFile->diagnostics;
	    !parseDiagnostics.empty()) {
		entry->errors = parseDiagnostics;
		return entry;
	}

	auto [extendedConfig, parseErrors] = parseConfig(
	    nullptr, extendedResult, host,
	    tspath::getDirectoryPath(fileName), tspath::getBaseFileName(fileName),
	    resolutionStack, extendedConfigCache);
	entry->extendedConfig = extendedConfig;
	entry->errors = parseErrors;
	return entry;
}

// parseConfig — tsconfigparsing.go:1082.
// parseConfig just extracts options/include/exclude/files out of a config
// file. It does not resolve the included files.
std::pair<parsedTsconfig*, std::vector<Diagnostic*>> parseConfig(
    JsonObjectPtr json, TsConfigSourceFile* sourceFile, ParseConfigHost* host,
    std::string_view basePathIn, std::string_view configFileName,
    const std::vector<tspath::Path>& resolutionStackIn,
    ExtendedConfigCache* extendedConfigCache) {
	std::string basePath = tspath::normalizeSlashes(basePathIn);
	tspath::Path resolvedPath = tspath::toPath(
	    configFileName, basePath, host->FS()->UseCaseSensitiveFileNames());
	std::vector<Diagnostic*> errors;
	std::vector<tspath::Path> resolutionStack = resolutionStackIn;
	if (std::find(resolutionStack.begin(), resolutionStack.end(),
	              resolvedPath) != resolutionStack.end()) {
		parsedTsconfig* result;
		errors.push_back(newCompilerDiagnostic(
		    Circularity_detected_while_resolving_configuration_Colon_0));
		if (json == nullptr || json->Size() == 0) {
			result = new parsedTsconfig{
			    .raw = json ? CompilerOptionsValue(json)
			                : CompilerOptionsValue(),
			};
		} else {
			auto [rawResult, err] =
			    convertToObject(sourceFile->SourceFile);
			errors.insert(errors.end(), err.begin(), err.end());
			result = new parsedTsconfig{.raw = rawResult};
		}
		return {result, errors};
	}

	parsedTsconfig* ownConfig;
	std::vector<Diagnostic*> err;
	if (json != nullptr) {
		auto [oc, e] = parseOwnConfigOfJson(json, host, basePath,
		                                    configFileName);
		ownConfig = oc;
		err = e;
	} else {
		auto [oc, e] = parseOwnConfigOfJsonSourceFile(
		    tsconfigToSourceFile(sourceFile), host, basePath,
		    configFileName);
		ownConfig = oc;
		err = e;
	}
	errors.insert(errors.end(), err.begin(), err.end());
	if (ownConfig->options != nullptr && !ownConfig->options->Paths.empty()) {
		// If we end up needing to resolve relative paths from 'paths'
		// relative to the config file location, we'll need to know where that
		// config file was. Since 'paths' can be inherited from an extended
		// config in another directory, we wouldn't know which directory to
		// use unless we store it here.
		ownConfig->options->PathsBasePath = basePath;
	}

	auto applyExtendedConfig = [&](extendsResult* result,
	                               const std::string& extendedConfigPath) {
		auto [extendedConfig, extendedErrors] = getExtendedConfig(
		    sourceFile, extendedConfigPath, host, resolutionStack,
		    extendedConfigCache, result);
		errors.insert(errors.end(), extendedErrors.begin(),
		              extendedErrors.end());
		if (extendedConfig != nullptr && extendedConfig->options != nullptr) {
			const CompilerOptionsValue& extendsRaw =
			    extendedConfig->raw;
			std::string relativeDifference;
			auto setPropertyValue = [&](const std::string& propertyName) {
				auto* ownRaw =
				    std::get_if<JsonObjectPtr>(&ownConfig->raw.v);
				if (ownRaw != nullptr &&
				    (*ownRaw)->Has(propertyName)) {
					return;
				}
				if (propertyName == "include" ||
				    propertyName == "exclude" ||
				    propertyName == "files") {
					auto* rawMap =
					    std::get_if<JsonObjectPtr>(&extendsRaw.v);
					if (rawMap != nullptr &&
					    (*rawMap)->Has(propertyName)) {
						auto sliceV = (*rawMap)->GetOrZero(propertyName);
						if (auto* slice =
						        std::get_if<JsonArray>(&sliceV.v);
						    slice != nullptr) {
							JsonArray value2;
							for (const auto& path : *slice) {
								auto* pathStr =
								    std::get_if<std::string>(&path.v);
								if (pathStr == nullptr) {
									value2.push_back(path);
									continue;
								}
								if (startsWithConfigDirTemplate(
								        path) ||
								    tspath::isRootedDiskPath(*pathStr)) {
									value2.emplace_back(*pathStr);
								} else {
									if (relativeDifference.empty()) {
										tspath::ComparePathsOptions t{
										    .useCaseSensitiveFileNames =
										        host->FS()
										            ->UseCaseSensitiveFileNames(),
										    .currentDirectory = basePath,
										};
										relativeDifference =
										    convertToRelativePath(
										        tspath::getDirectoryPath(
										            extendedConfigPath),
										        t);
									}
									value2.emplace_back(
									    tspath::combinePaths(
									        relativeDifference,
									        {*pathStr}));
								}
							}
							if (propertyName == "include") {
								result->include = value2;
							} else if (propertyName == "exclude") {
								result->exclude = value2;
							} else if (propertyName == "files") {
								result->files = value2;
							}
						}
					}
				}
			};

			setPropertyValue("include");
			setPropertyValue("exclude");
			setPropertyValue("files");
			if (auto* extendedRawMap =
			        std::get_if<JsonObjectPtr>(&extendsRaw.v);
			    extendedRawMap != nullptr &&
			    (*extendedRawMap)->Has("contentMappers")) {
				auto cm =
				    (*extendedRawMap)->GetOrZero("contentMappers");
				if (auto* arr = std::get_if<JsonArray>(&cm.v)) {
					result->contentMappers = *arr;
				}
			}
			if (auto* extendedRawMap =
			        std::get_if<JsonObjectPtr>(&extendsRaw.v);
			    extendedRawMap != nullptr &&
			    (*extendedRawMap)->Has("compileOnSave")) {
				auto cs = (*extendedRawMap)->GetOrZero("compileOnSave");
				if (auto* b = std::get_if<bool>(&cs.v)) {
					result->compileOnSave = *b;
				}
			}
			mergeCompilerOptions(result->options,
			                     extendedConfig->options, extendsRaw);
		}
	};

	if (!ownConfig->extendedConfigPath.isNil()) {
		// copy the resolution stack so it is never reused between branches
		// in potential diamond-problem scenarios.
		resolutionStack.push_back(resolvedPath);
		auto* result = new extendsResult{
		    .options = new CompilerOptions{},
		};
		if (auto* s = std::get_if<std::string>(
		        &ownConfig->extendedConfigPath.v);
		    s != nullptr) {
			applyExtendedConfig(result, *s);
		} else if (auto* configPath = std::get_if<JsonStrList>(
		               &ownConfig->extendedConfigPath.v);
		           configPath != nullptr) {
			for (const auto& extendedConfigPath : *configPath) {
				applyExtendedConfig(result, extendedConfigPath);
			}
		}
		auto* ownRaw = std::get_if<JsonObjectPtr>(&ownConfig->raw.v);
		if (result->include.has_value() && ownRaw != nullptr) {
			(*ownRaw)->Set("include", *result->include);
		}
		if (result->exclude.has_value() && ownRaw != nullptr) {
			(*ownRaw)->Set("exclude", *result->exclude);
		}
		if (result->files.has_value() && ownRaw != nullptr) {
			(*ownRaw)->Set("files", *result->files);
		}
		if (result->contentMappers.has_value() && ownRaw != nullptr &&
		    !(*ownRaw)->Has("contentMappers")) {
			(*ownRaw)
			    ->Set("contentMappers", *result->contentMappers);
		}
		if (result->compileOnSave && ownRaw != nullptr &&
		    !(*ownRaw)->Has("compileOnSave")) {
			(*ownRaw)->Set("compileOnSave", result->compileOnSave);
		}
		if (sourceFile != nullptr) {
			for (const auto& extendedSourceFile :
			     result->extendedSourceFiles.Keys()) {
				insertSorted(sourceFile->ExtendedSourceFiles,
				             extendedSourceFile);
			}
		}
		ownConfig->options = mergeCompilerOptions(
		    result->options, ownConfig->options, ownConfig->raw);
	}
	return {ownConfig, errors};
}

namespace {

// isStringValue — tsconfigparsing.go:1226.
bool isStringValue(const CompilerOptionsValue& value) {
	return std::holds_alternative<std::string>(value.v);
}

// stringifyJson — minimal encoding/json marshal for the no-inputs
// diagnostic. OrderedMap keeps insertion order (encoding/json uses map
// iteration which sorts keys — Go's OrderedMap preserves insertion order
// here, matching Go semantics for these inputs).
std::string stringifyJson(const CompilerOptionsValue& value) {
	std::string out;
	if (value.isNil()) {
		return "null";
	}
	if (auto* b = std::get_if<bool>(&value.v)) {
		return *b ? "true" : "false";
	}
	if (auto* d = std::get_if<double>(&value.v)) {
		return tsc::Number(*d).string();
	}
	if (auto* i = std::get_if<int64_t>(&value.v)) {
		return std::to_string(*i);
	}
	if (auto* s = std::get_if<std::string>(&value.v)) {
		return "\"" + jsonEscape(*s) + "\"";
	}
	if (auto* arr = std::get_if<JsonArray>(&value.v)) {
		out = "[";
		for (size_t i = 0; i < arr->size(); i++) {
			if (i) out += ",";
			out += stringifyJson((*arr)[i]);
		}
		out += "]";
		return out;
	}
	if (auto* sl = std::get_if<JsonStrList>(&value.v)) {
		out = "[";
		for (size_t i = 0; i < sl->size(); i++) {
			if (i) out += ",";
			out += "\"" + jsonEscape((*sl)[i]) + "\"";
		}
		out += "]";
		return out;
	}
	if (auto* o = std::get_if<JsonObjectPtr>(&value.v)) {
		out = "{";
		bool first = true;
		for (const auto& k : (*o)->Keys()) {
			if (!first) out += ",";
			first = false;
			out += "\"" + jsonEscape(k) +
			       "\":" + stringifyJson(*(*o)->Get(k).first);
		}
		out += "}";
		return out;
	}
	if (auto* m = std::get_if<JsonGoMapPtr>(&value.v)) {
		std::vector<std::string> keys;
		for (const auto& [k, v] : **m) keys.push_back(k);
		std::sort(keys.begin(), keys.end());
		out = "{";
		bool first = true;
		for (const auto& k : keys) {
			if (!first) out += ",";
			first = false;
			out += "\"" + jsonEscape(k) + "\":" +
			       stringifyJson((**m)[k]);
		}
		out += "}";
		return out;
	}
	return "null";
}

std::string jsonEscape(std::string_view s) {
	std::string out;
	for (char c : s) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		case '<': out += "\\u003c"; break;
		case '>': out += "\\u003e"; break;
		case '&': out += "\\u0026"; break;
		default:
			if ((unsigned char)c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof buf, "\\u%04x", c);
				out += buf;
			} else {
				out += c;
			}
		}
	}
	return out;
}

// canJsonReportNoInputFiles — tsconfigparsing.go:1536.
bool canJsonReportNoInputFiles(const JsonObjectPtr& rawConfig) {
	bool filesExists = rawConfig->Has("files");
	bool referencesExists = rawConfig->Has("references");
	return !filesExists && !referencesExists;
}

// shouldReportNoInputFiles — tsconfigparsing.go:1542.
bool shouldReportNoInputFiles(
    const std::vector<std::string>& fileNames,
    bool canJsonReportNoInputFilesV,
    const std::vector<tspath::Path>& resolutionStack) {
	return fileNames.empty() && canJsonReportNoInputFilesV && resolutionStack.empty();
}

}  // namespace

// ParseJsonSourceFileConfigFileContent — tsconfigparsing.go:726.
ParsedCommandLine* ParseJsonSourceFileConfigFileContent(
    TsConfigSourceFile* sourceFile, ParseConfigHost* host,
    std::string_view basePath, CompilerOptions* existingOptions,
    const JsonObjectPtr& existingOptionsRaw, std::string_view configFileName,
    const std::vector<tspath::Path>& resolutionStack,
    ExtendedConfigCache* extendedConfigCache) {
	// tracing?.push(tracing.Phase.Parse, "parseJsonSourceFileConfigFileContent", { path: sourceFile.fileName });
	ParsedCommandLine* result = parseJsonConfigFileContentWorker(
	    nullptr /*json*/, sourceFile, host, basePath, existingOptions,
	    existingOptionsRaw, configFileName, resolutionStack,
	    extendedConfigCache);
	// tracing?.pop();
	return result;
}

// ParseJsonConfigFileContent — tsconfigparsing.go:872.
ParsedCommandLine* ParseJsonConfigFileContent(
    const CompilerOptionsValue& json, ParseConfigHost* host,
    std::string_view basePath, CompilerOptions* existingOptions,
    std::string_view configFileName,
    const std::vector<tspath::Path>& resolutionStack,
    ExtendedConfigCache* extendedConfigCache) {
	CompilerOptionsValue normalized = normalizeJsonValue(json);
	JsonObjectPtr jsonObject;
	if (auto* o = std::get_if<JsonObjectPtr>(&normalized.v)) {
		jsonObject = *o;
	} else {
		jsonObject = std::make_shared<JsonObject>();
	}
	ParsedCommandLine* result = parseJsonConfigFileContentWorker(
	    jsonObject, nullptr /*sourceFile*/, host, basePath, existingOptions,
	    nullptr /*existingOptionsRaw*/, configFileName, resolutionStack,
	    extendedConfigCache);
	return result;
}

// ParseConfigFileTextToJson — tsconfigparsing.go:703.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
ParseConfigFileTextToJson(std::string_view fileName, const tspath::Path& path,
                          std::string_view jsonText) {
	SourceFile* jsonSourceFile = tsc::parseSourceFile(
	    SourceFileParseOptions{
	        .FileName = std::string(fileName),
	        .Path = path,
	    },
	    jsonText, ScriptKind::JSON);
	auto [config, errors] = convertConfigFileToObject(
	    jsonSourceFile, /*notifier*/ nullptr);
	if (!jsonSourceFile->diagnostics.empty()) {
		return {config, {jsonSourceFile->diagnostics[0]}};
	}
	return {config, errors};
}

namespace {

// parseJsonConfigFileContentWorker — tsconfigparsing.go:1237.
ParsedCommandLine* parseJsonConfigFileContentWorker(
    JsonObjectPtr json, TsConfigSourceFile* sourceFile, ParseConfigHost* host,
    std::string_view basePath, CompilerOptions* existingOptions,
    const JsonObjectPtr& existingOptionsRaw, std::string_view configFileName,
    const std::vector<tspath::Path>& resolutionStack,
    ExtendedConfigCache* extendedConfigCache) {
	TSC_ASSERT(
	    (json == nullptr && sourceFile != nullptr) ||
	        (json != nullptr && sourceFile == nullptr),
	    "expected json and sourceFile to be mutually exclusive");

	std::string basePathForFileNames;
	if (!configFileName.empty()) {
		basePathForFileNames = tspath::normalizePath(
		    directoryOfCombinedPath(configFileName, basePath));
	} else {
		basePathForFileNames = tspath::normalizePath(basePath);
	}

	auto parsedConfigResult = parseConfig(
	    json, sourceFile, host, basePath, configFileName, resolutionStack,
	    extendedConfigCache);
	parsedTsconfig* parsedConfig = parsedConfigResult.first;
	std::vector<Diagnostic*>& errors = parsedConfigResult.second;
	mergeCompilerOptions(parsedConfig->options, existingOptions,
	                     CompilerOptionsValue(existingOptionsRaw));
	handleOptionConfigDirTemplateSubstitution(parsedConfig->options,
	                                          basePathForFileNames);
	JsonObjectPtr rawConfig = parseJsonToStringKey(parsedConfig->raw);
	if (!configFileName.empty() && parsedConfig->options != nullptr) {
		parsedConfig->options->ConfigFilePath =
		    tspath::normalizeSlashes(configFileName);
	}
	auto getPropFromRaw =
	    [&](const std::string& prop,
	        const std::function<bool(const CompilerOptionsValue&)>& validateElement,
	        std::string_view elementTypeName) -> propOfRaw {
		auto [value, exists] = rawConfig->Get(prop);
		if (exists && !value->isNil()) {
			if (std::holds_alternative<JsonArray>(value->v) ||
			    std::holds_alternative<JsonStrList>(value->v)) {
				CompilerOptionsValue result = rawConfig->GetOrZero(prop);
				if (auto* arr = std::get_if<JsonArray>(&result.v);
				    arr != nullptr) {
					if (sourceFile == nullptr) {
						bool every = true;
						for (const auto& el : *arr) {
							if (!validateElement(el)) {
								every = false;
								break;
							}
						}
						if (!every) {
							errors.push_back(newCompilerDiagnostic(
							    Compiler_option_0_requires_a_value_of_type_1,
							    {std::string(prop),
							     std::string(elementTypeName)}));
						}
					}
					return propOfRaw{.sliceValue = *arr};
				}
				if (auto* sl =
				        std::get_if<JsonStrList>(&result.v);
				    sl != nullptr) {
					JsonArray arr;
					for (const auto& s : *sl)
						arr.emplace_back(s);
					return propOfRaw{.sliceValue = arr};
				}
			} else if (sourceFile == nullptr) {
				errors.push_back(newCompilerDiagnostic(
				    Compiler_option_0_requires_a_value_of_type_1,
				    {std::string(prop), "Array"}));
				return propOfRaw{.sliceValue = {},
				                 .wrongValue = "not-array"};
			}
		}
		return propOfRaw{.sliceValue = {}, .wrongValue = "no-prop"};
	};
	propOfRaw referencesOfRaw = getPropFromRaw(
	    "references",
	    [](const CompilerOptionsValue& element) {
		    return std::holds_alternative<JsonObjectPtr>(element.v);
	    },
	    "object");
	propOfRaw fileSpecs =
	    getPropFromRaw("files", isStringValue, "string");
	if (fileSpecs.sliceValue.has_value() ||
	    fileSpecs.wrongValue.empty()) {
		bool hasZeroOrNoReferences = false;
		if (referencesOfRaw.wrongValue == "no-prop" ||
		    referencesOfRaw.wrongValue == "not-array" ||
		    (referencesOfRaw.sliceValue.has_value() && referencesOfRaw.sliceValue->empty())) {
			hasZeroOrNoReferences = true;
		}
		CompilerOptionsValue hasExtends = rawConfig->GetOrZero("extends");
		if (fileSpecs.sliceValue.has_value() && fileSpecs.sliceValue->empty() && hasZeroOrNoReferences && hasExtends.isNil()) {
			if (sourceFile != nullptr) {
				std::string fileName;
				if (!configFileName.empty()) {
					fileName = configFileName;
				} else {
					fileName = "tsconfig.json";
				}
				const DiagnosticMessage* diagnosticMessage =
				    The_files_list_in_config_file_0_is_empty;
				Node* nodeValue = ForEachTsConfigPropArray<Node>(
				    sourceFile->SourceFile, "files",
				    [](PropertyAssignment* property) -> Node* {
					    return property->Initializer;
				    });
				errors.push_back(CreateDiagnosticForNodeInSourceFile(
				    sourceFile->SourceFile, nodeValue,
				    diagnosticMessage, {fileName}));
			} else {
				errors.push_back(newCompilerDiagnostic(
				    The_files_list_in_config_file_0_is_empty,
				    {std::string(configFileName)}));
			}
		}
	}
	propOfRaw includeSpecs =
	    getPropFromRaw("include", isStringValue, "string");
	propOfRaw excludeSpecs =
	    getPropFromRaw("exclude", isStringValue, "string");
	bool isDefaultIncludeSpec = false;
	if (excludeSpecs.wrongValue == "no-prop" && parsedConfig->options != nullptr) {
		const std::string& outDir = parsedConfig->options->OutDir;
		const std::string& declarationDir =
		    parsedConfig->options->DeclarationDir;
		if (!outDir.empty() || !declarationDir.empty()) {
			JsonArray values;
			if (!outDir.empty()) {
				values.emplace_back(outDir);
			}
			if (!declarationDir.empty()) {
				values.emplace_back(declarationDir);
			}
			excludeSpecs = propOfRaw{.sliceValue = values};
		}
	}
	if (!fileSpecs.sliceValue.has_value() &&
	    !includeSpecs.sliceValue.has_value()) {
		includeSpecs = propOfRaw{
		    .sliceValue =
		        JsonArray{CompilerOptionsValue(
		            std::string(defaultIncludeSpec))}};
		isDefaultIncludeSpec = true;
	}
	std::vector<std::string> validatedIncludeSpecs;
	std::vector<std::string> validatedIncludeSpecsBeforeSubstitution;
	std::vector<std::string> validatedExcludeSpecs;
	std::vector<std::string> validatedFilesSpec;
	std::vector<std::string> validatedFilesSpecBeforeSubstitution;
	// The exclude spec list is converted into a regular expression, which
	// allows us to quickly test whether a file or directory should be
	// excluded before recursively traversing the file system.
	if (includeSpecs.sliceValue.has_value()) {
		auto [specs, err] = validateSpecs(
		    CompilerOptionsValue(*includeSpecs.sliceValue),
		    true /*disallowTrailingRecursion*/,
		    tsconfigToSourceFile(sourceFile), "include");
		validatedIncludeSpecsBeforeSubstitution = specs;
		errors.insert(errors.end(), err.begin(), err.end());
		if (auto sub = getSubstitutedStringArrayWithConfigDirTemplate(
		        validatedIncludeSpecsBeforeSubstitution,
		        basePathForFileNames);
		    sub.has_value()) {
			validatedIncludeSpecs = *sub;
		} else {
			validatedIncludeSpecs =
			    validatedIncludeSpecsBeforeSubstitution;
		}
	}
	if (excludeSpecs.sliceValue.has_value()) {
		auto [specs, err] = validateSpecs(
		    CompilerOptionsValue(*excludeSpecs.sliceValue),
		    false /*disallowTrailingRecursion*/,
		    tsconfigToSourceFile(sourceFile), "exclude");
		validatedExcludeSpecs = specs;
		errors.insert(errors.end(), err.begin(), err.end());
		if (auto sub = getSubstitutedStringArrayWithConfigDirTemplate(
		        validatedExcludeSpecs, basePathForFileNames);
		    sub.has_value()) {
			validatedExcludeSpecs = *sub;
		}
	}
	if (fileSpecs.sliceValue.has_value()) {
		for (const auto& spec : *fileSpecs.sliceValue) {
			if (auto* s = std::get_if<std::string>(&spec.v)) {
				validatedFilesSpecBeforeSubstitution.push_back(*s);
			}
		}
		if (auto sub = getSubstitutedStringArrayWithConfigDirTemplate(
		        validatedFilesSpecBeforeSubstitution,
		        basePathForFileNames);
		    sub.has_value()) {
			validatedFilesSpec = *sub;
		} else {
			validatedFilesSpec =
			    validatedFilesSpecBeforeSubstitution;
		}
	}
	configFileSpecs specValue{
	    .filesSpecs =
	        fileSpecs.sliceValue.has_value()
	            ? CompilerOptionsValue(*fileSpecs.sliceValue)
	            : CompilerOptionsValue(),
	    .includeSpecs =
	        includeSpecs.sliceValue.has_value()
	            ? CompilerOptionsValue(*includeSpecs.sliceValue)
	            : CompilerOptionsValue(),
	    .excludeSpecs =
	        excludeSpecs.sliceValue.has_value()
	            ? CompilerOptionsValue(*excludeSpecs.sliceValue)
	            : CompilerOptionsValue(),
	    .validatedFilesSpec = validatedFilesSpec,
	    .validatedIncludeSpecs = validatedIncludeSpecs,
	    .validatedExcludeSpecs = validatedExcludeSpecs,
	    .validatedFilesSpecBeforeSubstitution =
	        validatedFilesSpecBeforeSubstitution,
	    .validatedIncludeSpecsBeforeSubstitution =
	        validatedIncludeSpecsBeforeSubstitution,
	    .isDefaultIncludeSpec = isDefaultIncludeSpec,
	};

	if (sourceFile != nullptr) {
		sourceFile->configFileSpecs = new configFileSpecs(specValue);
	}

	SourceFile* contentMapperSourceFile = nullptr;
	if (sourceFile != nullptr) {
		contentMapperSourceFile = sourceFile->SourceFile;
	}
	std::vector<contentmapper::Mapper*> contentMappers;
	std::vector<int> contentMapperIndices;
	propOfRaw contentMappersOfRaw = getPropFromRaw(
	    "contentMappers",
	    [](const CompilerOptionsValue& element) {
		    return std::holds_alternative<JsonObjectPtr>(element.v);
	    },
	    "object");
	if (contentMappersOfRaw.sliceValue.has_value()) {
		for (size_t i = 0;
		     i < contentMappersOfRaw.sliceValue->size(); i++) {
			auto [mapper, mapperErrors] = parseContentMapper(
			    (*contentMappersOfRaw.sliceValue)[i]);
			for (auto* mapperError : mapperErrors) {
				errors.push_back(setContentMapperDiagnosticLocation(
				    mapperError, contentMapperSourceFile,
				    getContentMapperSyntax(
				        contentMapperSourceFile, (int)i, "")));
			}
			if (mapper != nullptr) {
				contentMappers.push_back(mapper);
				contentMapperIndices.push_back((int)i);
			}
		}
	}
	size_t totalContentMapperExtensions = 0;
	for (auto* mapper : contentMappers) {
		totalContentMapperExtensions +=
		    mapper->Definition.Extensions.size();
	}
	collections::Set<std::string> seenContentMapperExtensions;
	std::vector<std::string> contentMapperExtensions;
	std::vector<std::string_view> nativeExtensions;
	for (const auto& group : tspath::allSupportedExtensionsWithJson) {
		for (auto ext : group) nativeExtensions.push_back(ext);
	}
	auto canonicalExtension = [&](std::string_view extension) {
		return tspath::getCanonicalFileName(
		    extension, host->FS()->UseCaseSensitiveFileNames());
	};
	for (size_t j = 0; j < contentMappers.size(); j++) {
		auto* mapper = contentMappers[j];
		std::vector<std::string> validExtensions;
		for (const auto& ext : mapper->Definition.Extensions) {
			Node* extNode = getContentMapperExtensionSyntax(
			    contentMapperSourceFile, contentMapperIndices[j], ext);
			std::string canonicalExt = canonicalExtension(ext);
			if (!ext.starts_with('.')) {
				errors.push_back(setContentMapperDiagnosticLocation(
				    newCompilerDiagnostic(
				        Content_mapper_file_extension_0_must_begin_with_a,
				        {ext}),
				    contentMapperSourceFile, extNode));
			} else if (std::any_of(
			               nativeExtensions.begin(),
			               nativeExtensions.end(),
			               [&](std::string_view nativeExtension) {
				               return utf8detail::equalFold(nativeExtension, ext);
			               })) {
				errors.push_back(setContentMapperDiagnosticLocation(
				    newCompilerDiagnostic(
				        Content_mapper_file_extension_0_is_a_built_in_extension_and_cannot_be_registered_by_a_content_mapper,
				        {ext}),
				    contentMapperSourceFile, extNode));
			} else {
				if (seenContentMapperExtensions.Has(canonicalExt)) {
					errors.push_back(
					    setContentMapperDiagnosticLocation(
					        newCompilerDiagnostic(
					            Content_mapper_file_extension_0_is_registered_by_more_than_one_content_mapper,
					            {ext}),
					        contentMapperSourceFile, extNode));
				} else {
					seenContentMapperExtensions.Add(canonicalExt);
					contentMapperExtensions.push_back(ext);
					validExtensions.push_back(ext);
				}
			}
		}
		mapper->Definition.Extensions = validExtensions;
	}
	if (!contentMappers.empty() &&
	    !(parsedConfig->options != nullptr && parsedConfig->options->RunExternalCode == Tristate::True)) {
		errors.push_back(setContentMapperDiagnosticLocation(
		    newCompilerDiagnostic(
		        Content_mappers_require_the_runExternalCode_command_line_flag_to_be_enabled),
		    contentMapperSourceFile,
		    getContentMappersKeySyntax(contentMapperSourceFile)));
		// Without the flag the mappers are not trusted to run, so drop them
		// entirely: their extensions are not registered and their files are
		// not intercepted (they are treated as unknown foreign files).
		contentMappers.clear();
		contentMapperExtensions.clear();
	} else if (!contentMappers.empty()) {
		// Resolve each mapper's package.json now so its name, version, and
		// run command are available to everything downstream (diagnostics,
		// build-info staleness) without executing anything.
		std::string containingFile(configFileName);
		if (containingFile.empty()) {
			containingFile = tspath::combinePaths(
			    basePathForFileNames, {"tsconfig.json"});
		}
		std::vector<contentmapper::Mapper*> resolvedContentMappers;
		for (size_t j = 0; j < contentMappers.size(); j++) {
			auto* mapper = contentMappers[j];
			auto [manifest, packageDirectory, diagnostic] =
			    resolveContentMapperManifest(host, containingFile,
			                                 mapper->Definition.Package);
			mapper->PackageDirectory = packageDirectory;
			if (diagnostic != nullptr) {
				errors.push_back(setContentMapperDiagnosticLocation(
				    diagnostic, contentMapperSourceFile,
				    getContentMapperSyntax(
				        contentMapperSourceFile,
				        contentMapperIndices[j], "package")));
				continue;
			}
			mapper->Manifest = manifest;
			resolvedContentMappers.push_back(mapper);
		}
		contentMappers = resolvedContentMappers;
		contentMapperExtensions.clear();
		for (auto* mapper : contentMappers) {
			for (const auto& ext : mapper->Definition.Extensions) {
				contentMapperExtensions.push_back(ext);
			}
		}
	}

	auto getFileNames =
	    [&](std::string_view bp) -> std::pair<std::vector<std::string>, int> {
		CompilerOptions* parsedConfigOptions = parsedConfig->options;
		auto [fileNames, literalFileNamesLen] =
		    getFileNamesFromConfigSpecs(
		        specValue, bp, parsedConfigOptions, host->FS(),
		        contentMapperExtensions);
		if (shouldReportNoInputFiles(
		        fileNames, canJsonReportNoInputFiles(rawConfig),
		        resolutionStack)) {
			CompilerOptionsValue includeSpecsV =
			    specValue.includeSpecs.isNil()
			        ? CompilerOptionsValue(JsonArray{})
			        : specValue.includeSpecs;
			CompilerOptionsValue excludeSpecsV =
			    specValue.excludeSpecs.isNil()
			        ? CompilerOptionsValue(JsonArray{})
			        : specValue.excludeSpecs;
			errors.push_back(newCompilerDiagnostic(
			    No_inputs_were_found_in_config_file_0_Specified_include_paths_were_1_and_exclude_paths_were_2,
			    {std::string(configFileName),
			     stringifyJson(includeSpecsV),
			     stringifyJson(excludeSpecsV)}));
		}
		return {fileNames, literalFileNamesLen};
	};

	auto getProjectReferences =
	    [&](std::string_view bp) -> std::vector<ProjectReference*> {
		std::vector<ProjectReference*> projectReferences;
		propOfRaw newReferencesOfRaw = getPropFromRaw(
		    "references",
		    [](const CompilerOptionsValue& element) {
			    return std::holds_alternative<JsonObjectPtr>(
			        element.v);
		    },
		    "object");
		if (newReferencesOfRaw.sliceValue.has_value()) {
			projectReferences = {};
			for (size_t index = 0;
			     index < newReferencesOfRaw.sliceValue->size();
			     index++) {
				auto* ref = parseProjectReference(
				    (*newReferencesOfRaw.sliceValue)[index]);
				if (ref == nullptr) {
					continue;
				}
				if (!ref->hasPath || !ref->pathValid) {
					errors.push_back(
					    createDiagnosticAtProjectReferenceProperty(
					        sourceFile, (int)index, "path",
					        Compiler_option_0_requires_a_value_of_type_1,
					        {"reference.path", "string"}));
					continue;
				}
				if (ref->reference.Path.empty()) {
					errors.push_back(
					    createDiagnosticAtProjectReferenceProperty(
					        sourceFile, (int)index, "path",
					        Compiler_option_0_cannot_be_given_an_empty_string,
					        {"reference.path"}));
					continue;
				}
				if (ref->hasCircular && !ref->circularValid) {
					errors.push_back(
					    createDiagnosticAtProjectReferenceProperty(
					        sourceFile, (int)index, "circular",
					        Compiler_option_0_requires_a_value_of_type_1,
					        {"reference.circular", "boolean"}));
				}
				projectReferences.push_back(new ProjectReference{
				    .Path = tspath::getNormalizedAbsolutePath(
				        ref->reference.Path, bp),
				    .OriginalPath = ref->reference.Path,
				    .Circular = ref->reference.Circular,
				});
			}
		}
		return projectReferences;
	};

	auto [fileNames, literalFileNamesLen] =
	    getFileNames(basePathForFileNames);
	std::shared_ptr<bool> compileOnSave;
	if (auto* raw = std::get_if<JsonObjectPtr>(&parsedConfig->raw.v)) {
		auto v = (*raw)->GetOrZero("compileOnSave");
		if (auto* b = std::get_if<bool>(&v.v)) {
			compileOnSave = std::make_shared<bool>(*b);
		}
	}
	auto* result = new ParsedCommandLine{};
	result->ParsedConfig = new ParsedOptions{
	    .CompilerOptions = parsedConfig->options,
	    .WatchOptions = nullptr,
	    .TypeAcquisition = parsedConfig->typeAcquisition,
	    .FileNames = fileNames,
	    .ProjectReferences = getProjectReferences(basePathForFileNames),
	    .ContentMappers = contentMappers,
	};
	result->ConfigFile = sourceFile;
	result->Raw = parsedConfig->raw;
	result->Errors = errors;
	result->CompileOnSave = compileOnSave;
	result->comparePathsOptions = tspath::ComparePathsOptions{
	    .useCaseSensitiveFileNames =
	        host->FS()->UseCaseSensitiveFileNames(),
	    .currentDirectory = basePathForFileNames,
	};
	result->literalFileNamesLen = literalFileNamesLen;
	return result;
}

// validateSpecs — tsconfigparsing.go:1546.
std::pair<std::vector<std::string>, std::vector<Diagnostic*>> validateSpecs(
    const CompilerOptionsValue& specs, bool disallowTrailingRecursion,
    SourceFile* jsonSourceFile, std::string_view specKey) {
	auto createDiagnostic = [&](const DiagnosticMessage* message,
	                            const std::string& spec) -> Diagnostic* {
		StringLiteral* element = GetTsConfigPropArrayElementValue(
		    jsonSourceFile, specKey, spec);
		Node* node = nullptr;
		if (element != nullptr) {
			node = element->asNode();
		}
		return CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
		    jsonSourceFile, node, message, {spec});
	};
	std::vector<Diagnostic*> errors;
	std::vector<std::string> finalSpecs;
	const auto* arr = std::get_if<JsonArray>(&specs.v);
	const auto* sl = std::get_if<JsonStrList>(&specs.v);
	JsonArray arrStorage;
	if (arr == nullptr && sl != nullptr) {
		for (const auto& s : *sl) arrStorage.emplace_back(s);
		arr = &arrStorage;
	}
	for (const auto& value : *arr) {
		const auto* spec = std::get_if<std::string>(&value.v);
		if (spec == nullptr) {
			continue;
		}
		const DiagnosticMessage* diag =
		    specToDiagnostic(*spec, disallowTrailingRecursion);
		if (diag != nullptr) {
			errors.push_back(createDiagnostic(diag, *spec));
		} else {
			finalSpecs.push_back(*spec);
		}
	}
	return {finalSpecs, errors};
}

}  // namespace

// specToDiagnostic — tsconfigparsing.go:1572.
const DiagnosticMessage* specToDiagnostic(std::string_view spec,
                                          bool disallowTrailingRecursion) {
	if (disallowTrailingRecursion && invalidTrailingRecursion(spec)) {
		return File_specification_cannot_end_in_a_recursive_directory_wildcard_Asterisk_Asterisk_Colon_0;
	}
	if (invalidDotDotAfterRecursiveWildcard(spec)) {
		return File_specification_cannot_contain_a_parent_directory_that_appears_after_a_recursive_directory_wildcard_Asterisk_Asterisk_Colon_0;
	}
	return nullptr;
}

// invalidTrailingRecursion — tsconfigparsing.go:1582.
bool invalidTrailingRecursion(std::string_view spec) {
	// Matches **, /**, **/, and /**/, but not a**b.
	// Strip optional trailing slash, then check if it ends with /** or is
	// just **
	if (spec.ends_with('/')) {
		spec.remove_suffix(1);
	}
	return spec == "**" || spec.ends_with("/**");
}

// invalidDotDotAfterRecursiveWildcard — tsconfigparsing.go:1589.
bool invalidDotDotAfterRecursiveWildcard(std::string_view s) {
	// We used to use the regex /(^|\/)\*\*\/(.*\/)?\.\.($|\/)/ to check for
	// this case, but in v8, that has polynomial performance because the
	// recursive wildcard match - **/ - can be matched in many arbitrary
	// positions when multiple are present, resulting in bad backtracking
	// (and we don't care which is matched - just that some /.. segment
	// comes after some **/ segment).
	int wildcardIndex;
	if (s.starts_with("**/")) {
		wildcardIndex = 0;
	} else {
		auto idx = s.find("/**/");
		wildcardIndex = idx == std::string_view::npos ? -1 : (int)idx;
	}
	if (wildcardIndex == -1) {
		return false;
	}
	size_t lastDotIndex;
	if (s.ends_with("/..")) {
		lastDotIndex = s.size();
	} else {
		lastDotIndex = s.rfind("/../");
		if (lastDotIndex == std::string_view::npos) {
			return false;
		}
	}
	return (int)lastDotIndex > wildcardIndex;
}

// GetTsConfigPropArrayElementValue — tsconfigparsing.go:1613.
StringLiteral* GetTsConfigPropArrayElementValue(
    SourceFile* tsConfigSourceFile, std::string_view propKey,
    std::string_view elementValue) {
	auto callback =
	    GetCallbackForFindingPropertyAssignmentByValue(elementValue);
	return ForEachTsConfigPropArray<StringLiteral>(
	    tsConfigSourceFile, propKey,
	    [&](PropertyAssignment* property) -> StringLiteral* {
		    if (Node* value = callback(property); value != nullptr) {
			    return value->as<StringLiteral>();
		    }
		    return nullptr;
	    });
}

// CreateDiagnosticAtReferenceSyntax — tsconfigparsing.go:1630.
Diagnostic* CreateDiagnosticAtReferenceSyntax(
    ParsedCommandLine* config, int index, const DiagnosticMessage* message,
    std::vector<std::string> args) {
	return ForEachTsConfigPropArray<Diagnostic>(
	    config->ConfigFile->SourceFile, "references",
	    [&](PropertyAssignment* property) -> Diagnostic* {
		    if (isArrayLiteralExpression(property->Initializer)) {
			    auto value = property->Initializer->elements();
			    if (value.size() > (size_t)index) {
				    return CreateDiagnosticForNodeInSourceFile(
				        config->ConfigFile->SourceFile, value[index],
				        message, args);
			    }
		    }
		    return nullptr;
	    });
}

namespace {

// createDiagnosticAtProjectReferenceProperty — tsconfigparsing.go:1642.
Diagnostic* createDiagnosticAtProjectReferenceProperty(
    TsConfigSourceFile* sourceFile, int index, std::string_view propertyName,
    const DiagnosticMessage* message, std::vector<std::string> args) {
	Node* node = nullptr;
	if (sourceFile != nullptr) {
		node = ForEachTsConfigPropArray<Node>(
		    sourceFile->SourceFile, "references",
		    [&](PropertyAssignment* property) -> Node* {
			    if (isArrayLiteralExpression(property->Initializer)) {
				    auto elements = property->Initializer->elements();
				    if (elements.size() > (size_t)index && isObjectLiteralExpression(elements[index])) {
					    if (Node* propertyNode =
					            ForEachPropertyAssignment<Node>(
					                elements[index]
					                    ->as<ObjectLiteralExpression>(),
					                propertyName,
					                [](PropertyAssignment* property)
					                    -> Node* {
						                return property->Initializer;
					                });
					        propertyNode != nullptr) {
						    return propertyNode;
					    }
					    return elements[index];
				    }
			    }
			    return nullptr;
		    });
	}
	return CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
	    tsconfigToSourceFile(sourceFile), node, message, args);
}

}  // namespace

// GetCallbackForFindingPropertyAssignmentByValue — tsconfigparsing.go:1663.
std::function<Node*(PropertyAssignment*)>
GetCallbackForFindingPropertyAssignmentByValue(std::string_view value) {
	std::string v(value);
	return [v](PropertyAssignment* property) -> Node* {
		if (isArrayLiteralExpression(property->Initializer)) {
			for (Node* element : property->Initializer->elements()) {
				if (isStringLiteral(element) && element->text() == v) {
					return element;
				}
			}
		}
		return nullptr;
	};
}

// GetOptionsSyntaxByArrayElementValue — tsconfigparsing.go:1674.
Node* GetOptionsSyntaxByArrayElementValue(
    ObjectLiteralExpression* objectLiteral, std::string_view propKey,
    std::string_view elementValue) {
	return ForEachPropertyAssignment<Node>(
	    objectLiteral, propKey,
	    GetCallbackForFindingPropertyAssignmentByValue(elementValue));
}

namespace {

// getContentMapperSyntax — tsconfigparsing.go:1682.
Node* getContentMapperSyntax(SourceFile* sourceFile, int index,
                             std::string_view subKey) {
	if (sourceFile == nullptr) {
		return nullptr;
	}
	return ForEachTsConfigPropArray<Node>(
	    sourceFile, "contentMappers",
	    [&](PropertyAssignment* property) -> Node* {
		    if (!isArrayLiteralExpression(property->Initializer)) {
			    return property->Initializer;
		    }
		    auto elements = property->Initializer->elements();
		    if (index < 0 || index >= (int)elements.size()) {
			    return property->Initializer;
		    }
		    Node* element = elements[index];
		    if (!subKey.empty() && isObjectLiteralExpression(element)) {
			    if (Node* node = ForEachPropertyAssignment<Node>(
			            element->as<ObjectLiteralExpression>(), subKey,
			            [](PropertyAssignment* property) -> Node* {
				            return property->Initializer;
			            });
			        node != nullptr) {
				    return node;
			    }
		    }
		    return element;
	    });
}

}  // namespace

// GetContentMapperOptionDiagnosticLocation — tsconfigparsing.go:1706.
std::pair<SourceFile*, TextRange> GetContentMapperOptionDiagnosticLocation(
    ParsedCommandLine* config, contentmapper::Mapper* mapper,
    const std::vector<contentmapper::OptionPathSegment>& path) {
	if (config == nullptr || config->ConfigFile == nullptr) {
		return {nullptr, TextRange{-1, -1}};
	}
	int index = -1;
	{
		auto mappers = config->ContentMappers();
		for (size_t i = 0; i < mappers.size(); i++) {
			if (mappers[i] == mapper) {
				index = (int)i;
				break;
			}
		}
	}
	Node* mapperNode = getContentMapperSyntax(
	    config->ConfigFile->SourceFile, index, "");
	Node* node = getContentMapperSyntax(config->ConfigFile->SourceFile,
	                                      index, "options");
	if (node == nullptr) {
		node = mapperNode;
	}
	for (const auto& segment : path) {
		Node* next = nullptr;
		if (segment.IsIndex && isArrayLiteralExpression(node)) {
			auto elements = node->elements();
			if ((size_t)segment.Index < elements.size()) {
				next = elements[segment.Index];
			}
		} else if (!segment.IsIndex && isObjectLiteralExpression(node)) {
			next = ForEachPropertyAssignment<Node>(
			    node->as<ObjectLiteralExpression>(), segment.Property,
			    [](PropertyAssignment* property) -> Node* {
				    return property->Initializer;
			    });
		}
		if (next == nullptr) {
			break;
		}
		node = next;
	}
	if (node == nullptr) {
		return {nullptr, TextRange{-1, -1}};
	}
	SourceFile* file = config->ConfigFile->SourceFile;
	return {file,
	        TextRange{tsc::skipTrivia(file->text, node->pos()),
	                  node->end()}};
}

namespace {

// getContentMappersKeySyntax — tsconfigparsing.go:1743.
Node* getContentMappersKeySyntax(SourceFile* sourceFile) {
	if (sourceFile == nullptr) {
		return nullptr;
	}
	return ForEachTsConfigPropArray<Node>(
	    sourceFile, "contentMappers",
	    [](PropertyAssignment* property) -> Node* {
		    return property->name;
	    });
}

// getContentMapperExtensionSyntax — tsconfigparsing.go:1754.
Node* getContentMapperExtensionSyntax(SourceFile* sourceFile, int index,
                                      std::string_view ext) {
	Node* node = getContentMapperSyntax(sourceFile, index, "extensions");
	if (node != nullptr && isArrayLiteralExpression(node)) {
		for (Node* element : node->elements()) {
			if (isStringLiteral(element) && element->text() == ext) {
				return element;
			}
		}
	}
	return node;
}

// setContentMapperDiagnosticLocation — tsconfigparsing.go:1769.
Diagnostic* setContentMapperDiagnosticLocation(Diagnostic* diagnostic,
                                               SourceFile* sourceFile,
                                               Node* node) {
	if (sourceFile != nullptr && node != nullptr) {
		diagnostic->SetFile(sourceFile);
		diagnostic->SetLocation(TextRange{
		    tsc::skipTrivia(sourceFile->text, node->pos()),
		    node->end()});
	}
	return diagnostic;
}

// getSubstitutedPathWithConfigDirTemplate — tsconfigparsing.go:1803.
std::string getSubstitutedPathWithConfigDirTemplate(std::string_view value,
                                                    std::string_view basePath) {
	std::string replaced(value);
	if (auto pos = replaced.find(configDirTemplate);
	    pos != std::string::npos) {
		replaced.replace(pos, configDirTemplate.size(), "./");
	}
	return tspath::getNormalizedAbsolutePath(replaced, basePath);
}

}  // namespace

// getTsConfigObjectLiteralExpression — tsconfigparsing.go:1793.
ObjectLiteralExpression* getTsConfigObjectLiteralExpression(
    SourceFile* tsConfigSourceFile) {
	if (tsConfigSourceFile != nullptr && tsConfigSourceFile->Statements != nullptr &&
	    !tsConfigSourceFile->Statements->nodes.empty()) {
		Node* expression =
		    tsConfigSourceFile->Statements->nodes[0]->expression();
		if (isObjectLiteralExpression(expression)) {
			return expression->as<ObjectLiteralExpression>();
		}
	}
	return nullptr;
}

namespace {

// getSubstitutedStringArrayWithConfigDirTemplate — tsconfigparsing.go:1807.
// Returns nullopt for Go's nil (no substitution needed).
std::optional<std::vector<std::string>>
getSubstitutedStringArrayWithConfigDirTemplate(
    const std::vector<std::string>& list, std::string_view basePath) {
	std::optional<std::vector<std::string>> result;
	for (size_t i = 0; i < list.size(); i++) {
		if (startsWithConfigDirTemplate(list[i])) {
			if (!result.has_value()) {
				result = list;
			}
			(*result)[i] = getSubstitutedPathWithConfigDirTemplate(
			    list[i], basePath);
		}
	}
	return result;
}

// handleOptionConfigDirTemplateSubstitution — tsconfigparsing.go:1823.
void handleOptionConfigDirTemplateSubstitution(
    CompilerOptions* compilerOptions, std::string_view basePath) {
	if (compilerOptions == nullptr) {
		return;
	}

	// !!! don't hardcode this; use options declarations?

	for (const auto& [k, v] : compilerOptions->Paths) {
		if (auto substitution =
		        getSubstitutedStringArrayWithConfigDirTemplate(v, basePath);
		    substitution.has_value()) {
			for (auto& [k2, v2] : compilerOptions->Paths) {
				if (k2 == k) {
					v2 = *substitution;
					break;
				}
			}
		}
	}

	if (auto rootDirs = getSubstitutedStringArrayWithConfigDirTemplate(
	        compilerOptions->RootDirs, basePath);
	    rootDirs.has_value()) {
		compilerOptions->RootDirs = *rootDirs;
	}
	if (auto typeRoots = getSubstitutedStringArrayWithConfigDirTemplate(
	        compilerOptions->TypeRoots, basePath);
	    typeRoots.has_value()) {
		compilerOptions->TypeRoots = *typeRoots;
	}
	if (startsWithConfigDirTemplate(compilerOptions->GenerateCpuProfile)) {
		compilerOptions->GenerateCpuProfile =
		    getSubstitutedPathWithConfigDirTemplate(
		        compilerOptions->GenerateCpuProfile, basePath);
	}
	if (startsWithConfigDirTemplate(compilerOptions->GenerateTrace)) {
		compilerOptions->GenerateTrace =
		    getSubstitutedPathWithConfigDirTemplate(
		        compilerOptions->GenerateTrace, basePath);
	}
	if (startsWithConfigDirTemplate(compilerOptions->OutFile)) {
		compilerOptions->OutFile = getSubstitutedPathWithConfigDirTemplate(
		    compilerOptions->OutFile, basePath);
	}
	if (startsWithConfigDirTemplate(compilerOptions->OutDir)) {
		compilerOptions->OutDir = getSubstitutedPathWithConfigDirTemplate(
		    compilerOptions->OutDir, basePath);
	}
	if (startsWithConfigDirTemplate(compilerOptions->RootDir)) {
		compilerOptions->RootDir = getSubstitutedPathWithConfigDirTemplate(
		    compilerOptions->RootDir, basePath);
	}
	if (startsWithConfigDirTemplate(compilerOptions->TsBuildInfoFile)) {
		compilerOptions->TsBuildInfoFile =
		    getSubstitutedPathWithConfigDirTemplate(
		        compilerOptions->TsBuildInfoFile, basePath);
	}
	if (startsWithConfigDirTemplate(compilerOptions->BaseUrl)) {
		compilerOptions->BaseUrl = getSubstitutedPathWithConfigDirTemplate(
		    compilerOptions->BaseUrl, basePath);
	}
	if (startsWithConfigDirTemplate(compilerOptions->DeclarationDir)) {
		compilerOptions->DeclarationDir =
		    getSubstitutedPathWithConfigDirTemplate(
		        compilerOptions->DeclarationDir, basePath);
	}
}

}  // namespace

// hasFileWithHigherPriorityExtension — tsconfigparsing.go:1875.
bool hasFileWithHigherPriorityExtension(
    std::string_view file,
    const std::vector<std::vector<std::string_view>>& extensions,
    const std::function<bool(std::string_view)>& hasFile) {
	std::vector<std::string_view> extensionGroup;
	for (const auto& group : extensions) {
		if (tspath::fileExtensionIsOneOf(file, group)) {
			for (auto e : group) extensionGroup.push_back(e);
		}
	}
	if (extensionGroup.empty()) {
		return false;
	}
	for (auto ext : extensionGroup) {
		// d.ts files match with .ts extension and with case sensitive
		// sorting the file order for same files with ts tsx and dts
		// extension is d.ts, .ts, .tsx in that order so we need to handle
		// tsx and dts of same same name case here and in remove files with
		// same extensions. So dont match .d.ts files with .ts extension
		if (tspath::fileExtensionIs(file, ext) &&
		    !(ext == tspath::extensionTs && tspath::fileExtensionIs(file, tspath::extensionDts))) {
			return false;
		}
		if (hasFile(tspath::changeExtension(file, ext))) {
			if (ext == tspath::extensionDts &&
			    (tspath::fileExtensionIs(file, tspath::extensionJs) ||
			     tspath::fileExtensionIs(file, tspath::extensionJsx))) {
				// LEGACY BEHAVIOR: An off-by-one bug somewhere in the
				// extension priority system for wildcard module loading
				// allowed declaration files to be loaded alongside their
				// js(x) counterparts. We regard this as generally
				// undesirable, but retain the behavior to prevent breakage.
				continue;
			}
			return true;
		}
	}
	return false;
}

// removeWildcardFilesWithLowerPriorityExtension — tsconfigparsing.go:1907.
// Removes files included via wildcard expansion with a lower extension
// priority that have already been included.
void removeWildcardFilesWithLowerPriorityExtension(
    std::string_view file,
    collections::OrderedMap<std::string, std::string>* wildcardFiles,
    const std::vector<std::vector<std::string_view>>& extensions,
    const std::function<std::string(std::string_view)>& keyMapper) {
	std::vector<std::string_view> extensionGroup;
	for (const auto& group : extensions) {
		if (tspath::fileExtensionIsOneOf(file, group)) {
			for (auto e : group) extensionGroup.push_back(e);
		}
	}
	if (extensionGroup.empty()) {
		return;
	}
	for (auto it = extensionGroup.rbegin(); it != extensionGroup.rend();
	     ++it) {
		std::string_view ext = *it;
		if (tspath::fileExtensionIs(file, ext)) {
			return;
		}
		std::string lowerPriorityPath =
		    keyMapper(tspath::changeExtension(file, ext));
		wildcardFiles->Delete(lowerPriorityPath);
	}
}

// getFileNamesFromConfigSpecs — tsconfigparsing.go:1934.
std::pair<std::vector<std::string>, int> getFileNamesFromConfigSpecs(
    const configFileSpecs& spec, std::string_view basePathIn,
    CompilerOptions* options, module::ResolutionHost* host,
    const std::vector<std::string>& extraExtensions) {
	std::string basePath = tspath::normalizePath(basePathIn);
	auto keyMappper = [&](std::string_view value) {
		return tspath::getCanonicalFileName(
		    value, host->UseCaseSensitiveFileNames());
	};
	// Literal file names (provided via the "files" array in tsconfig.json)
	// are stored in a file map with a possibly case insensitive key. We use
	// this map later when when including wildcard paths.
	collections::OrderedMap<std::string, std::string> literalFileMap;
	// Wildcard paths (provided via the "includes" array in tsconfig.json)
	// are stored in a file map with a possibly case insensitive key. We use
	// this map to store paths matched via wildcard, and to handle extension
	// priority.
	collections::OrderedMap<std::string, std::string> wildcardFileMap;
	// Wildcard paths of json files (provided via the "includes" array in
	// tsconfig.json) are stored in a file map with a possibly case
	// insensitive key. We use this map to store paths matched via wildcard
	// of *.json kind
	collections::OrderedMap<std::string, std::string> wildCardJsonFileMap;
	const auto& validatedFilesSpec = spec.validatedFilesSpec;
	const auto& validatedIncludeSpecs = spec.validatedIncludeSpecs;
	const auto& validatedExcludeSpecs = spec.validatedExcludeSpecs;
	// Rather than re-query this for each file and filespec, we query the
	// supported extensions once and store it on the expansion context.
	auto* supportedExtensions = GetSupportedExtensions(options,
	                                                   extraExtensions);
	auto* supportedExtensionsWithJsonIfResolveJsonModule =
	    GetSupportedExtensionsWithJsonIfResolveJsonModule(
	        options, supportedExtensions);
	// Literal files are always included verbatim. An "include" or "exclude"
	// specification cannot remove a literal file.
	for (const auto& fileName : validatedFilesSpec) {
		std::string file =
		    tspath::getNormalizedAbsolutePath(fileName, basePath);
		literalFileMap.Set(keyMappper(fileName), file);
	}

	module::vfsmatch::SpecMatcher* jsonOnlyIncludeMatchers = nullptr;
	if (!validatedIncludeSpecs.empty()) {
		std::vector<std::string_view> flatExtensions;
		for (const auto& group :
		     *supportedExtensionsWithJsonIfResolveJsonModule) {
			for (auto ext : group) flatExtensions.push_back(ext);
		}
		std::vector<std::string> files = module::vfsmatch::ReadDirectory(
		    host, basePath, basePath, flatExtensions, validatedExcludeSpecs,
		    validatedIncludeSpecs, module::vfsmatch::UnlimitedDepth);
		for (const auto& file : files) {
			if (tspath::fileExtensionIs(file, tspath::extensionJson)) {
				if (jsonOnlyIncludeMatchers == nullptr) {
					std::vector<std::string> includes;
					for (const auto& include :
					     validatedIncludeSpecs) {
						if (include.ends_with(
						        tspath::extensionJson)) {
							includes.push_back(include);
						}
					}
					jsonOnlyIncludeMatchers =
					    module::vfsmatch::NewSpecMatcher(
					        includes, basePath,
					        module::vfsmatch::Usage::Files,
					        host->UseCaseSensitiveFileNames());
				}
				int includeIndex = -1;
				if (jsonOnlyIncludeMatchers != nullptr) {
					includeIndex =
					    jsonOnlyIncludeMatchers->MatchIndex(file);
				}
				if (includeIndex != -1) {
					std::string key = keyMappper(file);
					if (!literalFileMap.Has(key) &&
					    !wildCardJsonFileMap.Has(key)) {
						wildCardJsonFileMap.Set(key, file);
					}
				}
				continue;
			}
			// If we have already included a literal or wildcard path with a
			// higher priority extension, we should skip this file.
			//
			// This handles cases where we may encounter both <file>.ts and
			// <file>.d.ts (or <file>.js if "allowJs" is enabled) in the
			// same directory when they are compilation outputs.
			if (hasFileWithHigherPriorityExtension(
			        file, *supportedExtensions,
			        [&](std::string_view fileName) {
				        std::string canonicalFileName =
				            keyMappper(fileName);
				        return literalFileMap.Has(canonicalFileName) ||
				               wildcardFileMap.Has(canonicalFileName);
			        })) {
				continue;
			}
			// We may have included a wildcard path with a lower priority
			// extension due to the user-defined order of entries in the
			// "include" array. If there is a lower priority extension in
			// the same directory, we should remove it.
			removeWildcardFilesWithLowerPriorityExtension(
			    file, &wildcardFileMap, *supportedExtensions,
			    keyMappper);
			std::string key = keyMappper(file);
			if (!literalFileMap.Has(key) && !wildcardFileMap.Has(key)) {
				wildcardFileMap.Set(key, file);
			}
		}
	}
	std::vector<std::string> files;
	files.reserve(literalFileMap.Size() + wildcardFileMap.Size() +
	              wildCardJsonFileMap.Size());
	for (const auto& k : literalFileMap.Keys()) {
		files.push_back(*literalFileMap.Get(k).first);
	}
	for (const auto& k : wildcardFileMap.Keys()) {
		files.push_back(*wildcardFileMap.Get(k).first);
	}
	for (const auto& k : wildCardJsonFileMap.Keys()) {
		files.push_back(*wildCardJsonFileMap.Get(k).first);
	}
	return {files, (int)literalFileMap.Size()};
}

// GetSupportedExtensions — tsconfigparsing.go:2026.
const std::vector<std::vector<std::string_view>>* GetSupportedExtensions(
    const CompilerOptions* compilerOptions,
    const std::vector<std::string>& extraExtensions) {
	bool needJSExtensions = compilerOptions->GetAllowJS();
	const std::vector<std::vector<std::string_view>>* builtins =
	    needJSExtensions ? &tspath::allSupportedExtensions
	                     : &tspath::supportedTSExtensions;
	if (extraExtensions.empty()) {
		return builtins;
	}
	std::unordered_set<std::string_view> flatBuiltins;
	for (const auto& group : *builtins)
		for (auto ext : group) flatBuiltins.insert(ext);
	std::vector<std::vector<std::string_view>> result;
	for (const auto& ext : extraExtensions) {
		if (!flatBuiltins.count(ext)) {
			result.push_back({ext});
		}
	}
	if (result.empty()) {
		return builtins;
	}
	auto* concat = new std::vector<std::vector<std::string_view>>(*builtins);
	for (auto& g : result) concat->push_back(std::move(g));
	return concat;
}

// GetSupportedExtensionsWithJsonIfResolveJsonModule —
// tsconfigparsing.go:2050.
const std::vector<std::vector<std::string_view>>*
GetSupportedExtensionsWithJsonIfResolveJsonModule(
    const CompilerOptions* compilerOptions,
    const std::vector<std::vector<std::string_view>>* supportedExtensions) {
	if (compilerOptions == nullptr ||
	    !compilerOptions->GetResolveJsonModule()) {
		return supportedExtensions;
	}
	// core.Same — backing-array identity.
	if (supportedExtensions == &tspath::allSupportedExtensions) {
		return &tspath::allSupportedExtensionsWithJson;
	}
	if (supportedExtensions == &tspath::supportedTSExtensions) {
		return &tspath::supportedTSExtensionsWithJson;
	}
	auto* concat = new std::vector<std::vector<std::string_view>>(
	    *supportedExtensions);
	concat->push_back({tspath::extensionJson});
	return concat;
}

// GetParsedCommandLineOfConfigFile — tsconfigparsing.go:2064.
// Reads the config file and reports errors.
std::pair<ParsedCommandLine*, std::vector<Diagnostic*>>
GetParsedCommandLineOfConfigFile(std::string_view configFileName,
                                 CompilerOptions* options,
                                 const JsonObjectPtr& optionsRaw,
                                 ParseConfigHost* sys,
                                 ExtendedConfigCache* extendedConfigCache) {
	std::string configFileNameAbs = tspath::getNormalizedAbsolutePath(
	    configFileName, sys->GetCurrentDirectory());
	return GetParsedCommandLineOfConfigFilePath(
	    configFileNameAbs,
	    tspath::toPath(configFileNameAbs, sys->GetCurrentDirectory(),
	                   sys->FS()->UseCaseSensitiveFileNames()),
	    options, optionsRaw, sys, extendedConfigCache);
}

// GetParsedCommandLineOfConfigFilePath — tsconfigparsing.go:2075.
std::pair<ParsedCommandLine*, std::vector<Diagnostic*>>
GetParsedCommandLineOfConfigFilePath(
    std::string_view configFileName, const tspath::Path& path,
    CompilerOptions* options, const JsonObjectPtr& optionsRaw,
    ParseConfigHost* sys, ExtendedConfigCache* extendedConfigCache) {
	std::vector<Diagnostic*> errors;
	auto [configFileText, errs] = tryReadFile(
	    configFileName,
	    [sys](std::string_view f) -> std::pair<std::string, bool> {
		    auto r = sys->FS()->ReadFile(f);
		    return {r.value_or(""), r.has_value()};
	    },
	    errors);
	errors = errs;
	if (!errors.empty()) {
		// these are unrecoverable errors--exit to report them as
		// diagnostics
		return {nullptr, errors};
	}

	TsConfigSourceFile* tsConfigSourceFile =
	    NewTsconfigSourceFileFromFilePath(configFileName, path,
	                                      configFileText);
	// tsConfigSourceFile.resolvedPath = tsConfigSourceFile.FileName()
	// tsConfigSourceFile.originalFileName = tsConfigSourceFile.FileName()
	return {ParseJsonSourceFileConfigFileContent(
	            tsConfigSourceFile, sys,
	            tspath::getDirectoryPath(configFileName), options,
	            optionsRaw, configFileName, {}, extendedConfigCache),
	        {}};
}

}  // namespace tsc::tsoptions

// === dep stubs — removed when owner slice lands ===

namespace tsc::tspath {
// convertToRelativePath — path.go:821. Owned by the tspath slice.
std::string convertToRelativePath(std::string_view absoluteOrRelativePath,
                                  const ComparePathsOptions& options) {
	TSC_UNREACHABLE("convertToRelativePath — tsoptions dep");
}
}  // namespace tsc::tspath
