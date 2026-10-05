#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <type_traits>
#include <variant>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/module/types.h"
#include "internal/parser/parser.h" // getErrorSpanForNode
#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

// --- Lib tables (enummaps.go: LibMap/Libs/LibFilesSet/targetToLibMap) ---

// LibMap — insertion order preserved (Libs is the ordered key list, NOT sorted).
inline const std::vector<std::pair<std::string_view, std::string_view>> libMapEntries = {
	// JavaScript only
	{"es5", "lib.es5.d.ts"},
	{"es6", "lib.es2015.d.ts"},
	{"es2015", "lib.es2015.d.ts"},
	{"es7", "lib.es2016.d.ts"},
	{"es2016", "lib.es2016.d.ts"},
	{"es2017", "lib.es2017.d.ts"},
	{"es2018", "lib.es2018.d.ts"},
	{"es2019", "lib.es2019.d.ts"},
	{"es2020", "lib.es2020.d.ts"},
	{"es2021", "lib.es2021.d.ts"},
	{"es2022", "lib.es2022.d.ts"},
	{"es2023", "lib.es2023.d.ts"},
	{"es2024", "lib.es2024.d.ts"},
	{"es2025", "lib.es2025.d.ts"},
	{"es2026", "lib.es2026.d.ts"},
	{"esnext", "lib.esnext.d.ts"},
	// Host only
	{"dom", "lib.dom.d.ts"},
	{"dom.iterable", "lib.dom.iterable.d.ts"},
	{"dom.asynciterable", "lib.dom.asynciterable.d.ts"},
	{"webworker", "lib.webworker.d.ts"},
	{"webworker.importscripts", "lib.webworker.importscripts.d.ts"},
	{"webworker.iterable", "lib.webworker.iterable.d.ts"},
	{"webworker.asynciterable", "lib.webworker.asynciterable.d.ts"},
	{"scripthost", "lib.scripthost.d.ts"},
	// ES2015 and later By-feature options
	{"es2015.core", "lib.es2015.core.d.ts"},
	{"es2015.collection", "lib.es2015.collection.d.ts"},
	{"es2015.generator", "lib.es2015.generator.d.ts"},
	{"es2015.iterable", "lib.es2015.iterable.d.ts"},
	{"es2015.promise", "lib.es2015.promise.d.ts"},
	{"es2015.proxy", "lib.es2015.proxy.d.ts"},
	{"es2015.reflect", "lib.es2015.reflect.d.ts"},
	{"es2015.symbol", "lib.es2015.symbol.d.ts"},
	{"es2015.symbol.wellknown", "lib.es2015.symbol.wellknown.d.ts"},
	{"es2016.array.include", "lib.es2016.array.include.d.ts"},
	{"es2016.intl", "lib.es2016.intl.d.ts"},
	{"es2017.arraybuffer", "lib.es2017.arraybuffer.d.ts"},
	{"es2017.date", "lib.es2017.date.d.ts"},
	{"es2017.object", "lib.es2017.object.d.ts"},
	{"es2017.sharedmemory", "lib.es2017.sharedmemory.d.ts"},
	{"es2017.string", "lib.es2017.string.d.ts"},
	{"es2017.intl", "lib.es2017.intl.d.ts"},
	{"es2017.typedarrays", "lib.es2017.typedarrays.d.ts"},
	{"es2018.asyncgenerator", "lib.es2018.asyncgenerator.d.ts"},
	{"es2018.asynciterable", "lib.es2018.asynciterable.d.ts"},
	{"es2018.intl", "lib.es2018.intl.d.ts"},
	{"es2018.promise", "lib.es2018.promise.d.ts"},
	{"es2018.regexp", "lib.es2018.regexp.d.ts"},
	{"es2019.array", "lib.es2019.array.d.ts"},
	{"es2019.object", "lib.es2019.object.d.ts"},
	{"es2019.string", "lib.es2019.string.d.ts"},
	{"es2019.symbol", "lib.es2019.symbol.d.ts"},
	{"es2019.intl", "lib.es2019.intl.d.ts"},
	{"es2020.bigint", "lib.es2020.bigint.d.ts"},
	{"es2020.date", "lib.es2020.date.d.ts"},
	{"es2020.promise", "lib.es2020.promise.d.ts"},
	{"es2020.sharedmemory", "lib.es2020.sharedmemory.d.ts"},
	{"es2020.string", "lib.es2020.string.d.ts"},
	{"es2020.symbol.wellknown", "lib.es2020.symbol.wellknown.d.ts"},
	{"es2020.intl", "lib.es2020.intl.d.ts"},
	{"es2020.number", "lib.es2020.number.d.ts"},
	{"es2021.promise", "lib.es2021.promise.d.ts"},
	{"es2021.string", "lib.es2021.string.d.ts"},
	{"es2021.weakref", "lib.es2021.weakref.d.ts"},
	{"es2021.intl", "lib.es2021.intl.d.ts"},
	{"es2022.array", "lib.es2022.array.d.ts"},
	{"es2022.error", "lib.es2022.error.d.ts"},
	{"es2022.intl", "lib.es2022.intl.d.ts"},
	{"es2022.object", "lib.es2022.object.d.ts"},
	{"es2022.string", "lib.es2022.string.d.ts"},
	{"es2022.regexp", "lib.es2022.regexp.d.ts"},
	{"es2023.array", "lib.es2023.array.d.ts"},
	{"es2023.collection", "lib.es2023.collection.d.ts"},
	{"es2023.intl", "lib.es2023.intl.d.ts"},
	{"es2024.arraybuffer", "lib.es2024.arraybuffer.d.ts"},
	{"es2024.collection", "lib.es2024.collection.d.ts"},
	{"es2024.object", "lib.es2024.object.d.ts"},
	{"es2024.promise", "lib.es2024.promise.d.ts"},
	{"es2024.regexp", "lib.es2024.regexp.d.ts"},
	{"es2024.sharedmemory", "lib.es2024.sharedmemory.d.ts"},
	{"es2024.string", "lib.es2024.string.d.ts"},
	{"es2025.collection", "lib.es2025.collection.d.ts"},
	{"es2025.float16", "lib.es2025.float16.d.ts"},
	{"es2025.intl", "lib.es2025.intl.d.ts"},
	{"es2025.iterator", "lib.es2025.iterator.d.ts"},
	{"es2025.promise", "lib.es2025.promise.d.ts"},
	{"es2025.regexp", "lib.es2025.regexp.d.ts"},
	{"es2026.array", "lib.es2026.array.d.ts"},
	{"es2026.collection", "lib.es2026.collection.d.ts"},
	{"es2026.error", "lib.es2026.error.d.ts"},
	{"es2026.iterator", "lib.es2026.iterator.d.ts"},
	{"es2026.json", "lib.es2026.json.d.ts"},
	{"es2026.math", "lib.es2026.math.d.ts"},
	{"es2026.typedarrays", "lib.es2026.typedarrays.d.ts"},
	// Fallback for backward compatibility
	{"esnext.asynciterable", "lib.es2018.asynciterable.d.ts"},
	{"esnext.symbol", "lib.es2019.symbol.d.ts"},
	{"esnext.bigint", "lib.es2020.bigint.d.ts"},
	{"esnext.weakref", "lib.es2021.weakref.d.ts"},
	{"esnext.object", "lib.es2024.object.d.ts"},
	{"esnext.regexp", "lib.es2024.regexp.d.ts"},
	{"esnext.string", "lib.es2024.string.d.ts"},
	{"esnext.float16", "lib.es2025.float16.d.ts"},
	{"esnext.promise", "lib.es2025.promise.d.ts"},
	{"esnext.array", "lib.es2026.array.d.ts"},
	{"esnext.collection", "lib.es2026.collection.d.ts"},
	{"esnext.error", "lib.es2026.error.d.ts"},
	{"esnext.iterator", "lib.es2026.iterator.d.ts"},
	{"esnext.typedarrays", "lib.es2026.typedarrays.d.ts"},
	// ESNext By-feature options
	{"esnext.date", "lib.esnext.date.d.ts"},
	{"esnext.decorators", "lib.esnext.decorators.d.ts"},
	{"esnext.disposable", "lib.esnext.disposable.d.ts"},
	{"esnext.intl", "lib.esnext.intl.d.ts"},
	{"esnext.sharedmemory", "lib.esnext.sharedmemory.d.ts"},
	{"esnext.temporal", "lib.esnext.temporal.d.ts"},
	// Decorators
	{"decorators", "lib.decorators.d.ts"},
	{"decorators.legacy", "lib.decorators.legacy.d.ts"},
};

inline const std::vector<std::string_view> Libs = [] {
	std::vector<std::string_view> v;
	v.reserve(libMapEntries.size());
	for (const auto& e : libMapEntries) v.push_back(e.first);
	return v;
}();

inline const std::unordered_map<std::string_view, std::string_view> LibMap =
    [] {
	    std::unordered_map<std::string_view, std::string_view> m;
	    for (const auto& e : libMapEntries) m.emplace(e.first, e.second);
	    return m;
    }();

inline const std::unordered_set<std::string_view> LibFilesSet = [] {
	std::unordered_set<std::string_view> s;
	for (const auto& e : libMapEntries) s.insert(e.second);
	return s;
}();

// enummaps.go: GetLibFileName — checks if libName is a valid lib name or
// file name and converts it to the filename if needed.
inline std::pair<std::string_view, bool> getLibFileName(std::string_view libName) {
	std::string lowered = tspath::toFileNameLowerCase(libName);
	if (LibFilesSet.count(lowered)) {
		// Return a view into the interned string set for storage stability.
		return {*LibFilesSet.find(lowered), true};
	}
	auto it = LibMap.find(lowered);
	if (it == LibMap.end()) {
		return {"", false};
	}
	return {it->second, true};
}

// enummaps.go: targetToLibMap
inline const std::unordered_map<ScriptTarget, std::string_view> targetToLibMap = {
	{ScriptTarget::ESNext, "lib.esnext.full.d.ts"},
	{ScriptTarget::ES2026, "lib.es2026.full.d.ts"},
	{ScriptTarget::ES2025, "lib.es2025.full.d.ts"},
	{ScriptTarget::ES2024, "lib.es2024.full.d.ts"},
	{ScriptTarget::ES2023, "lib.es2023.full.d.ts"},
	{ScriptTarget::ES2022, "lib.es2022.full.d.ts"},
	{ScriptTarget::ES2021, "lib.es2021.full.d.ts"},
	{ScriptTarget::ES2020, "lib.es2020.full.d.ts"},
	{ScriptTarget::ES2019, "lib.es2019.full.d.ts"},
	{ScriptTarget::ES2018, "lib.es2018.full.d.ts"},
	{ScriptTarget::ES2017, "lib.es2017.full.d.ts"},
	{ScriptTarget::ES2016, "lib.es2016.full.d.ts"},
	{ScriptTarget::ES2015, "lib.es6.d.ts"}, // We don't use lib.es2015.full.d.ts due to breaking change.
};

// enummaps.go: GetDefaultLibFileName
inline std::string_view getDefaultLibFileName(const CompilerOptions* options) {
	auto it = targetToLibMap.find(options->GetEmitScriptTarget());
	if (it == targetToLibMap.end()) {
		return "lib.d.ts";
	}
	return it->second;
}

// --- CommandLineProgram file spec helpers (commandLineProgram.go) ---

// GetSupportedExtensions — tsconfigparsing.go:2026.
inline std::vector<std::vector<std::string_view>> getSupportedExtensions(
    const CompilerOptions* options,
    const std::vector<std::string_view>& extraFileExtensions) {
	bool needJSExtensions = options->GetAllowJS();
	const auto& builtins = needJSExtensions
	                           ? tspath::allSupportedExtensions
	                           : tspath::supportedTSExtensions;
	if (extraFileExtensions.empty()) {
		return builtins;
	}
	// flatBuiltins membership check
	std::unordered_set<std::string_view> flatBuiltins;
	for (const auto& group : builtins)
		for (auto ext : group) flatBuiltins.insert(ext);
	std::vector<std::vector<std::string_view>> extra;
	for (auto ext : extraFileExtensions) {
		if (!flatBuiltins.count(ext)) {
			extra.push_back({ext});
		}
	}
	if (extra.empty()) {
		return builtins;
	}
	auto result = builtins;
	for (auto& g : extra) result.push_back(g);
	return result;
}

inline std::vector<std::vector<std::string_view>>
getSupportedExtensionsWithJsonIfResolveJsonModule(
    const CompilerOptions* options,
    const std::vector<std::vector<std::string_view>>& supportedExtensions) {
	auto extensions = supportedExtensions;
	if (options->GetResolveJsonModule()) {
		extensions.push_back({tspath::extensionJson});
	}
	return extensions;
}

inline std::vector<std::string_view> supportedExtensionsFlat(
    const std::vector<std::vector<std::string_view>>& extensions) {
	std::vector<std::string_view> flat;
	for (const auto& group : extensions) {
		for (auto ext : group) flat.push_back(ext);
	}
	return flat;
}

// --- errors.go: CreateDiagnosticForNodeInSourceFile ---

// NewCompilerDiagnostic equivalent — a file-less diagnostic.
inline Diagnostic* newCompilerDiagnostic(const DiagnosticMessage* message,
                                         const std::vector<std::string>& args = {}) {
	return newDiagnostic(nullptr, TextRange::undefined(), message, args);
}

inline Diagnostic* createDiagnosticForNodeInSourceFile(SourceFile* sourceFile,
                                                     Node* node,
                                                     const DiagnosticMessage* message,
                                                     std::vector<std::string> args = {}) {
	TextRange span = getErrorSpanForNode(sourceFile->text, node);
	return newDiagnostic(sourceFile, span, message, args);
}

inline Diagnostic* createDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
    SourceFile* sourceFile, Node* node, const DiagnosticMessage* message,
    std::vector<std::string> args = {}) {
	if (node != nullptr) {
		return createDiagnosticForNodeInSourceFile(sourceFile, node, message, args);
	}
	return newCompilerDiagnostic(message, args);
}

// ============================================================================
// === slice: tsoptions ===
// Port of tsc/internal/tsoptions — option declarations, command-line parsing,
// tsconfig parsing and --showConfig serialization.
// ============================================================================

// ---------------------------------------------------------------------------
// commandlineoption.go — CompilerOptionsValue is Go `any`: a JSON-ish tagged
// union carrying every value that can flow through option parsing.
// ---------------------------------------------------------------------------

struct CompilerOptionsValue;

using JsonStrList = std::vector<std::string>;   // []string
using JsonArray = std::vector<CompilerOptionsValue>;  // []any
// *collections.OrderedMap[string, any]
using JsonObject =
    collections::OrderedMap<std::string, CompilerOptionsValue>;
using JsonObjectPtr = std::shared_ptr<JsonObject>;
// map[string]any — only reachable through external API callers passing
// unmarshalled JSON; normalizeJsonValue converts it to JsonObject.
using JsonGoMap = std::unordered_map<std::string, CompilerOptionsValue>;
using JsonGoMapPtr = std::shared_ptr<JsonGoMap>;

struct CompilerOptionsValue {
	std::variant<std::monostate,            // nil (and struct{}{} — never read)
	             bool,                      // bool
	             int64_t,                   // int, and all int32 enum kinds
	             double,                    // float64
	             Tristate,                  // core.Tristate
	             std::string,               // string
	             const DiagnosticMessage*,  // *diagnostics.Message
	             JsonStrList,               // []string
	             JsonArray,                 // []any
	             JsonObjectPtr,             // *OrderedMap[string, any]
	             JsonGoMapPtr>              // map[string]any
	    v;

	CompilerOptionsValue() = default;
	CompilerOptionsValue(std::monostate) {}
	CompilerOptionsValue(bool x) : v(x) {}
	CompilerOptionsValue(int x) : v(int64_t(x)) {}
	CompilerOptionsValue(int64_t x) : v(x) {}
	CompilerOptionsValue(double x) : v(x) {}
	CompilerOptionsValue(Tristate x) : v(x) {}
	CompilerOptionsValue(std::string x) : v(std::move(x)) {}
	CompilerOptionsValue(std::string_view x) : v(std::string(x)) {}
	CompilerOptionsValue(const char* x) : v(std::string(x)) {}
	CompilerOptionsValue(const DiagnosticMessage* x) : v(x) {}
	CompilerOptionsValue(JsonStrList x) : v(std::move(x)) {}
	CompilerOptionsValue(JsonArray x) : v(std::move(x)) {}
	CompilerOptionsValue(JsonObjectPtr x) : v(std::move(x)) {}
	CompilerOptionsValue(JsonGoMapPtr x) : v(std::move(x)) {}

	bool operator==(const CompilerOptionsValue&) const = default;
	// Go enum values flowing into `any` keep their dynamic enum type but
	// compare by the int32 underneath (reflect.CanInt); the int64 arm plays
	// that role here.
	template <typename E>
	    requires std::is_enum_v<E>
	CompilerOptionsValue(E x)
	    : v(int64_t(static_cast<std::underlying_type_t<E>>(x))) {}
	// enum-kind arms: stored as int64 like Go's reflect.Int() view.
	CompilerOptionsValue(ScriptTarget x) : v(int64_t(x)) {}
	CompilerOptionsValue(ModuleKind x) : v(int64_t(x)) {}
	CompilerOptionsValue(ModuleResolutionKind x) : v(int64_t(x)) {}
	CompilerOptionsValue(ModuleDetectionKind x) : v(int64_t(x)) {}
	CompilerOptionsValue(JsxEmit x) : v(int64_t(x)) {}
	CompilerOptionsValue(NewLineKind x) : v(int64_t(x)) {}
	CompilerOptionsValue(WatchFileKind x) : v(int64_t(x)) {}
	CompilerOptionsValue(WatchDirectoryKind x) : v(int64_t(x)) {}
	CompilerOptionsValue(PollingKind x) : v(int64_t(x)) {}

	bool isNil() const { return std::holds_alternative<std::monostate>(v); }

	template <typename T>
	const T* get() const {
		return std::get_if<T>(&v);
	}
	template <typename T>
	T* get() {
		return std::get_if<T>(&v);
	}
	bool isString() const { return get<std::string>() != nullptr; }
	const std::string& asString() const { return std::get<std::string>(v); }
	bool isBool() const { return get<bool>() != nullptr; }
	bool asBool() const { return std::get<bool>(v); }
	bool isInt() const { return get<int64_t>() != nullptr; }
	int64_t asInt() const { return std::get<int64_t>(v); }
	bool isDouble() const { return get<double>() != nullptr; }
	double asDouble() const { return std::get<double>(v); }
	bool isTristate() const { return get<Tristate>() != nullptr; }
	Tristate asTristate() const { return std::get<Tristate>(v); }
	bool isStrList() const { return get<JsonStrList>() != nullptr; }
	const JsonStrList& asStrList() const { return std::get<JsonStrList>(v); }
	JsonStrList& asStrList() { return std::get<JsonStrList>(v); }
	bool isArray() const { return get<JsonArray>() != nullptr; }
	const JsonArray& asArray() const { return std::get<JsonArray>(v); }
	JsonArray& asArray() { return std::get<JsonArray>(v); }
	bool isObject() const { return get<JsonObjectPtr>() != nullptr; }
	const JsonObjectPtr& asObject() const { return std::get<JsonObjectPtr>(v); }
	JsonObjectPtr& asObject() { return std::get<JsonObjectPtr>(v); }
	bool isGoMap() const { return get<JsonGoMapPtr>() != nullptr; }
	const JsonGoMapPtr& asGoMap() const { return std::get<JsonGoMapPtr>(v); }
	bool isMessage() const { return get<const DiagnosticMessage*>() != nullptr; }

	// reflect.Kind() == reflect.Slice — both Go slice spellings.
	bool isSliceKind() const { return isArray() || isStrList(); }
};

// deepEqual — reflect.DeepEqual over the variant arms (shared_ptr objects
// compare by content, matching Go's OrderedMap deep equality).
bool jsonDeepEqual(const CompilerOptionsValue& a, const CompilerOptionsValue& b);

// jsonMarshal — encoding/json Marshal of a CompilerOptionsValue (OrderedMap
// emits keys in insertion order per MarshalJSONTo).
std::string jsonMarshal(const CompilerOptionsValue& v);

// ---------------------------------------------------------------------------
// commandlineoption.go
// ---------------------------------------------------------------------------

using CommandLineOptionKind = std::string_view;
inline constexpr CommandLineOptionKind CommandLineOptionTypeString = "string";
inline constexpr CommandLineOptionKind CommandLineOptionTypeNumber = "number";
inline constexpr CommandLineOptionKind CommandLineOptionTypeBoolean =
    "boolean";
inline constexpr CommandLineOptionKind CommandLineOptionTypeObject = "object";
inline constexpr CommandLineOptionKind CommandLineOptionTypeList = "list";
inline constexpr CommandLineOptionKind CommandLineOptionTypeListOrElement =
    "listOrElement";
inline constexpr CommandLineOptionKind CommandLineOptionTypeEnum = "enum";

using extraValidation = std::string_view;
inline constexpr extraValidation extraValidationNone = "";
inline constexpr extraValidation extraValidationSpec = "spec";
inline constexpr extraValidation extraValidationLocale = "locale";

struct CommandLineOption;
// CommandLineOptionNameMap — tsconfigparsing.go:596.
struct CommandLineOptionNameMap {
	std::unordered_map<std::string, const CommandLineOption*> m;

	const CommandLineOption* Get(std::string_view name) const;
	const CommandLineOption* GetSpellingSuggestion(
	    std::string_view name) const;
};

struct CommandLineOption {
	std::string Name, ShortName;
	CommandLineOptionKind Kind;

	// used in parsing
	bool IsFilePath = false;
	bool IsTSConfigOnly = false;
	bool IsCommandLineOnly = false;

	// used in output
	const DiagnosticMessage* Description = nullptr;
	CompilerOptionsValue DefaultValueDescription;
	bool ShowInSimplifiedHelpView = false;

	// used in output in serializing and generate tsconfig
	const DiagnosticMessage* Category = nullptr;

	// What kind of extra validation `validateJsonOptionValue` should do
	extraValidation extraValidation_ = extraValidationNone;

	// checks that option with number type has value >= minValue
	int minValue = 0;

	// true or undefined
	// used for configDirTemplateSubstitutionOptions
	bool allowConfigDirTemplateSubstitution = false;

	// used for filter in compilerrunner
	bool AffectsDeclarationPath = false;
	bool AffectsProgramStructure = false;
	bool AffectsSemanticDiagnostics = false;
	bool AffectsBuildInfo = false;
	bool AffectsBindDiagnostics = false;
	bool AffectsSourceFile = false;
	bool AffectsModuleResolution = false;
	bool AffectsEmit = false;

	bool allowJsFlag = false;
	bool strictFlag = false;

	// used in transpileoptions worker
	// todo: revisit to see if this can be reduced to boolean
	Tristate transpileOptionValue{};

	// used for CommandLineOptionTypeList
	bool listPreserveFalsyValues = false;
	// used for compilerOptionsDeclaration
	CommandLineOptionNameMap ElementOptions;

	// EnumMap — commandlineoption.go:86. nullptr unless Kind == enum.
	const JsonObject* EnumMap() const;
	// DeprecatedKeys — commandlineoption.go:79.
	const collections::Set<std::string>* DeprecatedKeys() const;
	// Elements — commandlineoption.go:93. nullptr unless list/listOrElement.
	const CommandLineOption* Elements() const;
	// DisallowNullOrUndefined — commandlineoption.go:100.
	bool DisallowNullOrUndefined() const { return Name == "extends"; }
};

// ---------------------------------------------------------------------------
// The option declaration tables (decls*.go). Function-static tables so
// cross-TU init order is irrelevant.
// ---------------------------------------------------------------------------

// declscompiler.go
const std::vector<const CommandLineOption*>& commonOptionsWithBuild();
const std::vector<const CommandLineOption*>& optionsForCompiler();
const std::vector<const CommandLineOption*>& OptionsDeclarations();
// declsbuild.go
const CommandLineOption& TscBuildOption();
const std::vector<const CommandLineOption*>& OptionsForBuild();
const std::vector<const CommandLineOption*>& BuildOpts();
// declswatch.go
const std::vector<const CommandLineOption*>& OptionsForWatch();
// declstypeacquisition.go
const CommandLineOption& typeAcquisitionDeclaration();
const std::vector<const CommandLineOption*>& typeAcquisitionDecls();
// tsconfigparsing.go
inline constexpr std::string_view configDirTemplate = "${configDir}";
inline constexpr std::string_view defaultIncludeSpec = "**/*";
const CommandLineOption& compilerOptionsDeclaration();
const CommandLineOption& compileOnSaveCommandLineOption();
const CommandLineOption& extendsOptionDeclaration();
const CommandLineOption& tsconfigRootOptionsMap();
// enummaps.go — the option enum maps hold their values as int64.
const JsonObject& libEnumMap();
const JsonObject& moduleResolutionOptionMap();
const JsonObject& targetOptionMap();
const JsonObject& moduleOptionMap();
const JsonObject& moduleDetectionOptionMap();
const JsonObject& jsxOptionMap();
const JsonObject& newLineOptionMap();
const JsonObject& watchFileEnumMap();
const JsonObject& watchDirectoryEnumMap();
const JsonObject& fallbackEnumMap();
// TargetToLibMap — enummaps.go:232 (table lives above as targetToLibMap).
inline const std::unordered_map<ScriptTarget, std::string_view>& TargetToLibMap() {
	return targetToLibMap;
}

// commandlineparser.go helper — builds {name, lowerName} → option.
CommandLineOptionNameMap commandLineOptionsToMap(
    const std::vector<const CommandLineOption*>& optDecls);

// ---------------------------------------------------------------------------
// namemap.go
// ---------------------------------------------------------------------------

struct NameMap {
	// optionsNames maps lowercase name → option (insertion-ordered).
	collections::OrderedMap<std::string, const CommandLineOption*> optionsNames;
	std::unordered_map<std::string, std::string> shortOptionNames;

	const CommandLineOption* Get(std::string_view name) const;
	const CommandLineOption* GetFromShort(std::string_view shortName) const;
	const CommandLineOption* GetOptionDeclarationFromName(
	    std::string_view optionName, bool allowShort) const;
};

const NameMap& CompilerNameMap();
const NameMap& BuildNameMap();
const NameMap& WatchNameMap();
// GetNameMapFromList — namemap.go:15.
std::shared_ptr<NameMap> GetNameMapFromList(
    const std::vector<const CommandLineOption*>& optDecls);

// ---------------------------------------------------------------------------
// diagnostics.go
// ---------------------------------------------------------------------------

struct AlternateModeDiagnostics;
struct DidYouMeanOptionsDiagnostics {
	AlternateModeDiagnostics* alternateMode = nullptr;
	const std::vector<const CommandLineOption*>* OptionDeclarations =
	    nullptr;
	const DiagnosticMessage* UnknownOptionDiagnostic = nullptr;
	const DiagnosticMessage* UnknownDidYouMeanDiagnostic = nullptr;
};

struct AlternateModeDiagnostics {
	const DiagnosticMessage* diagnostic = nullptr;
	const NameMap* optionsNameMap = nullptr;
};

struct ParseCommandLineWorkerDiagnostics {
	DidYouMeanOptionsDiagnostics didYouMean;
	const NameMap* optionsNameMap = nullptr;
	mutable std::once_flag optionsNameMapOnce;
	const DiagnosticMessage* OptionTypeMismatchDiagnostic = nullptr;
};

// getParseCommandLineWorkerDiagnostics — diagnostics.go:30.
std::unique_ptr<ParseCommandLineWorkerDiagnostics>
getParseCommandLineWorkerDiagnostics(
    const std::vector<const CommandLineOption*>& decls);
ParseCommandLineWorkerDiagnostics& CompilerOptionsDidYouMeanDiagnostics();
ParseCommandLineWorkerDiagnostics& watchOptionsDidYouMeanDiagnostics();
ParseCommandLineWorkerDiagnostics& buildOptionsDidYouMeanDiagnostics();

// ---------------------------------------------------------------------------
// parsedoptions.go / parsedcommandline.go / parsedbuildcommandline.go
// ---------------------------------------------------------------------------

namespace glob {  // dep-stub decls — owned by the glob slice
// glob.go — a compiled wildcard spec.
struct Glob {
	// Match — glob.go:215. dep-stubbed in wildcarddirectories' TU.
	bool Match(std::string_view input) const;
};
// glob.Parse — glob.go:47. Returns (glob, ok): ok=false on parse error.
std::pair<Glob*, bool> Parse(std::string_view pattern);
}  // namespace glob

namespace outputpaths {  // dep-stub decls — owned by the outputpaths slice
// outputpaths.go:9 — the methods ParsedCommandLine implements.
struct OutputPathsHost {
	virtual ~OutputPathsHost() = default;
	virtual std::string CommonSourceDirectory() = 0;
	virtual bool UseCaseSensitiveFileNames() = 0;
	virtual std::string GetCurrentDirectory() = 0;
};
// commonsourcedirectory.go:59
std::string GetCommonSourceDirectory(
    const CompilerOptions* options, const std::function<std::vector<std::string>()>& files,
    std::string_view currentDirectory, bool useCaseSensitiveFileNames,
    const std::function<bool(const std::vector<std::string>&, std::string_view)>&
        checkSourceFilesBelongToPath);
// outputpaths.go:108
std::string GetOutputDeclarationFileNameWorker(
    std::string_view inputFileName, const CompilerOptions* options,
    OutputPathsHost* host);
// outputpaths.go:81
std::string GetOutputJSFileName(std::string_view inputFileName,
                                const CompilerOptions* options,
                                OutputPathsHost* host);
// outputpaths.go:200
std::string GetSourceMapFilePath(std::string_view jsFilePath,
                                 const CompilerOptions* options);
// outputpaths.go:207
std::string GetBuildInfoFileName(
    const CompilerOptions* options,
    const tspath::ComparePathsOptions& comparePathsOptions);
}  // namespace outputpaths

namespace locale {  // dep-stub decls — owned by the locale slice
// locale.go — a BCP-47 language tag; zero value is Default.
struct Locale {
	std::string tag;
	std::string String() const { return tag; }
	bool operator==(const Locale&) const = default;
};
inline Locale Default{};
// locale.Parse — locale.go:36. dep-stubbed in tsconfigparsing' TU.
std::pair<Locale, bool> Parse(std::string_view localeStr);
}  // namespace locale

namespace contentmapper {  // minimal decls — contentmapper package types.
// contentmapper.go — types only (the package's Host machinery isn't ported
// here). Definition.Options is raw JSON (Go json.Value).
struct Definition {
	std::string Package;
	std::vector<std::string> Extensions;
	std::string Options;
};
struct Manifest {
	std::string Name;
	std::string Version;
	std::vector<std::string> Exec;
	std::vector<std::string> CompilerOptions;
	bool DynamicConfig = false;
};
struct Mapper {
	Definition Definition;
	Manifest Manifest;
	// PackageDirectory is the real path directory returned by package
	// resolution for package-based mappers.
	std::string PackageDirectory;
	// ContributionID is provided by an LSP client extension for inferred
	// project content mappers.
	std::string ContributionID;
};
// host.go:209
struct OptionPathSegment {
	std::string Property;
	int Index = 0;
	bool IsIndex = false;
};
}  // namespace contentmapper

struct ParsedOptions {
	CompilerOptions* CompilerOptions = nullptr;
	::tsc::WatchOptions* WatchOptions = nullptr;
	TypeAcquisition* TypeAcquisition = nullptr;

	std::vector<std::string> FileNames;
	std::vector<ProjectReference*> ProjectReferences;
	std::vector<contentmapper::Mapper*> ContentMappers;
};

struct ParsedCommandLine;

struct SourceOutputAndProjectReference {
	std::string Source;
	std::string OutputDts;
	ParsedCommandLine* Resolved = nullptr;
};

struct configFileSpecs {
	CompilerOptionsValue filesSpecs;
	// Present to report errors (user specified specs), validatedIncludeSpecs
	// are used for file name matching
	CompilerOptionsValue includeSpecs;
	// Present to report errors (user specified specs), validatedExcludeSpecs
	// are used for file name matching
	CompilerOptionsValue excludeSpecs;
	std::vector<std::string> validatedFilesSpec;
	std::vector<std::string> validatedIncludeSpecs;
	std::vector<std::string> validatedExcludeSpecs;
	std::vector<std::string> validatedFilesSpecBeforeSubstitution;
	std::vector<std::string> validatedIncludeSpecsBeforeSubstitution;
	bool isDefaultIncludeSpec = false;

	// tsconfigparsing.go
	bool matchesExclude(
	    std::string_view fileName,
	    const tspath::ComparePathsOptions& comparePathsOptions) const;
	std::string getMatchedIncludeSpec(
	    std::string_view fileName,
	    const tspath::ComparePathsOptions& comparePathsOptions) const;
	std::string getMatchedFileSpec(
	    std::string_view fileName,
	    const tspath::ComparePathsOptions& comparePathsOptions) const;
};

struct TsConfigSourceFile {
	std::vector<std::string> ExtendedSourceFiles;
	struct configFileSpecs* configFileSpecs = nullptr;
	SourceFile* SourceFile = nullptr;
};

// tsconfigToSourceFile — tsconfigparsing.go:289.
inline SourceFile* tsconfigToSourceFile(const TsConfigSourceFile* f) {
	if (f == nullptr) {
		return nullptr;
	}
	return f->SourceFile;
}

struct ParsedCommandLine : module::ResolvedProjectReference,
                           outputpaths::OutputPathsHost {
	ParsedOptions* ParsedConfig = nullptr;

	TsConfigSourceFile* ConfigFile = nullptr;
	std::vector<Diagnostic*> Errors;
	CompilerOptionsValue Raw;
	std::shared_ptr<bool> CompileOnSave;

	tspath::ComparePathsOptions comparePathsOptions;

	mutable std::once_flag wildcardDirectoriesOnce;
	std::shared_ptr<std::unordered_map<std::string, bool>> wildcardDirectories;
	mutable std::once_flag includeGlobsOnce;
	std::shared_ptr<std::vector<glob::Glob*>> includeGlobs;

	mutable std::once_flag sourceAndOutputMapsOnce;
	std::shared_ptr<
	    std::unordered_map<tspath::Path, SourceOutputAndProjectReference*>>
	    sourceToProjectReference,
	    outputDtsToProjectReference;

	std::string commonSourceDirectory;
	mutable std::once_flag commonSourceDirectoryOnce;

	std::vector<std::string> resolvedProjectReferencePaths;
	mutable std::once_flag resolvedProjectReferencePathsOnce;

	int literalFileNamesLen = 0;
	std::shared_ptr<std::unordered_map<tspath::Path, std::string>>
	    fileNamesByPath;
	mutable std::once_flag fileNamesByPathOnce;

	locale::Locale locale_;
	mutable std::once_flag localeOnce;

	// parsedcommandline.go
	// (fileGlob, recursiveFileGlob) — parsedcommandline.go:31.
	std::pair<std::string, std::string> fileGlobPatterns();
	// module::ResolvedProjectReference overrides (const) + Go spellings.
	std::string ConfigName() const override;
	const tsc::CompilerOptions* CompilerOptions() const override {
		if (this == nullptr || ParsedConfig == nullptr) {
			return nullptr;
		}
		return ParsedConfig->CompilerOptions;
	}
	tsc::CompilerOptions* CompilerOptions() {
		if (this == nullptr || ParsedConfig == nullptr) {
			return nullptr;
		}
		return ParsedConfig->CompilerOptions;
	}
	const std::shared_ptr<std::unordered_map<tspath::Path,
	                                       SourceOutputAndProjectReference*>>&
	SourceToProjectReference() const {
		return sourceToProjectReference;
	}
	const std::shared_ptr<std::unordered_map<tspath::Path,
	                                       SourceOutputAndProjectReference*>>&
	OutputDtsToProjectReference() const {
		return outputDtsToProjectReference;
	}
	void ParseInputOutputNames();
	std::string CommonSourceDirectory() override;
	bool checkSourceFilesBelongToPath(const std::vector<std::string>& sourceFiles,
	                                  std::string_view rootDirectory);
	std::string GetCurrentDirectory() override {
		return comparePathsOptions.currentDirectory;
	}
	bool UseCaseSensitiveFileNames() override {
		return comparePathsOptions.useCaseSensitiveFileNames;
	}
	// iter.Seq2[string,string] — getOutputDeclarationAndSourceFileNames.
	void getOutputDeclarationAndSourceFileNames(
	    const std::function<bool(std::string_view dtsName,
	                             std::string_view inputName)>& yield);
	// iter.Seq[string] — GetOutputFileNames.
	void GetOutputFileNames(
	    const std::function<bool(std::string_view outputName)>& yield);
	std::string GetBuildInfoFileName();
	std::unordered_map<std::string, bool>* WildcardDirectories();
	std::vector<glob::Glob*>* WildcardDirectoryGlobs();
	std::vector<std::string> LiteralFileNames();
	void SetParsedOptions(ParsedOptions* o) { ParsedConfig = o; }
	void SetCompilerOptions(tsc::CompilerOptions* o) {
		ParsedConfig->CompilerOptions = o;
	}
	void SetTypeAcquisition(TypeAcquisition* o) {
		ParsedConfig->TypeAcquisition = o;
	}
	TypeAcquisition* TypeAcquisition() {
		return ParsedConfig->TypeAcquisition;
	}
	std::vector<std::string> FileNames() { return ParsedConfig->FileNames; }
	std::unordered_map<tspath::Path, std::string>* FileNamesByPath();
	std::vector<ProjectReference*> ProjectReferences() {
		return ParsedConfig->ProjectReferences;
	}
	std::vector<contentmapper::Mapper*> ContentMappers();
	std::vector<std::string> ContentMapperExtensions();
	contentmapper::Mapper* GetContentMapperForFileName(
	    std::string_view fileName);
	std::vector<std::string> ResolvedProjectReferencePaths();
	std::vector<std::string> ExtendedSourceFiles();
	std::vector<Diagnostic*> GetConfigFileParsingDiagnostics();
	bool PossiblyMatchesFileName(std::string_view fileName);
	bool PossiblyMatchesDirectoryName(const tspath::Path& directoryPath);
	std::string GetMatchedFileSpec(std::string_view fileName);
	std::pair<std::string, bool> GetMatchedIncludeSpec(
	    std::string_view fileName);
	ParsedCommandLine* ReloadFileNamesOfParsedCommandLine(
	    module::ResolutionHost* fs);
	locale::Locale Locale();

	// WithFileNames — parsedcommandline.go:93.
	ParsedCommandLine* WithFileNames(const std::vector<std::string>& fileNames);
};

// NewParsedCommandLine — parsedcommandline.go:77.
ParsedCommandLine* NewParsedCommandLine(
    tsc::CompilerOptions* compilerOptions,
    std::vector<std::string> rootFileNames,
    std::vector<ProjectReference*> projectReferences,
    tspath::ComparePathsOptions comparePathsOptions);

struct ParsedBuildCommandLine {
	BuildOptions* BuildOptions = nullptr;
	tsc::CompilerOptions* CompilerOptions = nullptr;
	::tsc::WatchOptions* WatchOptions = nullptr;
	std::vector<std::string> Projects;
	std::vector<Diagnostic*> Errors;
	CompilerOptionsValue Raw;

	tspath::ComparePathsOptions comparePathsOptions;

	std::vector<std::string> resolvedProjectPaths;
	mutable std::once_flag resolvedProjectPathsOnce;

	locale::Locale locale_;
	mutable std::once_flag localeOnce;

	std::vector<std::string> ResolvedProjectPaths();
	std::string GetCurrentDirectory() {
		return comparePathsOptions.currentDirectory;
	}
	locale::Locale Locale();
};

// ---------------------------------------------------------------------------
// tsconfigparsing.go
// ---------------------------------------------------------------------------

struct parsedTsconfig {
	CompilerOptionsValue raw;
	tsc::CompilerOptions* options = nullptr;
	TypeAcquisition* typeAcquisition = nullptr;
	// Note that the case of the config path has not yet been normalized, as
	// no files have been imported into the project yet
	CompilerOptionsValue extendedConfigPath;
};

struct ExtendedConfigCacheEntry {
	TsConfigSourceFile* extendedResult = nullptr;
	parsedTsconfig* extendedConfig = nullptr;
	std::vector<Diagnostic*> errors;
	std::vector<std::string> ExtendedFileNames() const;
};

struct ExtendedConfigCache;
struct ParseConfigHost;
struct ExtendedConfigCache {
	virtual ~ExtendedConfigCache() = default;
	virtual ExtendedConfigCacheEntry* GetExtendedConfig(
	    std::string_view fileName, const tspath::Path& path,
	    const std::vector<tspath::Path>& resolutionStack,
	    ParseConfigHost* host) = 0;
};

// ParseConfigHost — tsconfigparsing.go:715. C++ folds vfs.FS into
// module::ResolutionHost; FS() returns `this` so `host->FS()->X()` mirrors
// the Go `host.FS().X()` shape.
struct ParseConfigHost : module::ResolutionHost {
	module::ResolutionHost* FS() { return this; }
};

// tsConfigOptions — tsconfigparsing.go:590.
struct tsConfigOptions {
	std::unordered_map<std::string, std::vector<std::string>> prop;
	std::vector<ProjectReference*> references;
	std::string notDefined;
};

struct projectReferenceParseResult {
	ProjectReference reference;
	bool hasPath = false;
	bool pathValid = false;
	bool hasCircular = false;
	bool circularValid = false;
};

// extendsResult — tsconfigparsing.go:26.
struct extendsResult {
	CompilerOptions* options = nullptr;
	// Go `[]any` fields — nil-vs-empty is observable (`result.include != nil`),
	// so they are optional<> rather than bare vectors.
	std::optional<JsonArray> include;
	std::optional<JsonArray> exclude;
	std::optional<JsonArray> files;
	std::optional<JsonArray> contentMappers;
	bool compileOnSave = false;
	collections::Set<std::string> extendedSourceFiles;
};

// propOfRaw — tsconfigparsing.go:1221.
struct propOfRaw {
	// []any in Go — nil vs empty matters (Optional not a JSON spec error).
	std::optional<JsonArray> sliceValue;
	std::string wrongValue;
};

// jsonConversionNotifier — tsconfigparsing.go:306.
struct jsonConversionNotifier {
	const CommandLineOption* rootOptions = nullptr;
	std::function<std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>(
	    std::string_view keyText, const CompilerOptionsValue& value,
	    PropertyAssignment* propertyAssignment,
	    const CommandLineOption* parentOption,
	    const CommandLineOption* option)>
	    onPropertySet;
};

// commandLineParser — commandlineparser.go:32. `fs` is vfs.FS →
// module::ResolutionHost (same C++ folding as ParseConfigHost).
struct commandLineParser {
	ParseCommandLineWorkerDiagnostics* workerDiagnostics = nullptr;
	std::shared_ptr<NameMap> optionsMap;
	module::ResolutionHost* fs = nullptr;
	std::string currentDirectory;
	JsonObjectPtr options;
	std::vector<std::string> fileNames;
	std::vector<Diagnostic*> errors;
	collections::Set<tspath::Path> responseFileStack;

	AlternateModeDiagnostics* AlternateMode() const;
	const std::vector<const CommandLineOption*>* OptionsDeclarations()
	    const;
	const DiagnosticMessage* UnknownOptionDiagnostic() const;
	const DiagnosticMessage* UnknownDidYouMeanDiagnostic() const;
	void parseStrings(const std::vector<std::string>& args);
	void parseResponseFile(std::string_view fileName);
	int parseOptionValue(const std::vector<std::string>& args, int i,
	                     const CommandLineOption* opt,
	                     const DiagnosticMessage* diag);
	std::pair<JsonArray, std::vector<Diagnostic*>> parseListTypeOption(
	    const CommandLineOption* opt, std::string_view value);
	// errors.go:39 — method spelling of createUnknownOptionError.
	Diagnostic* createUnknownOptionError(
	    std::string_view unknownOption,
	    std::string_view unknownOptionErrorText, Node* node,
	    SourceFile* sourceFile);
};

// optionParser — parsinghelpers.go:198. Go uses an interface satisfied by
// pointer receivers; C++ uses one abstract base.
struct optionParser {
	virtual ~optionParser() = default;
	virtual std::vector<Diagnostic*> ParseOption(
	    std::string_view key, const CompilerOptionsValue& value) = 0;
	virtual const DiagnosticMessage* UnknownOptionDiagnostic() const = 0;
	virtual const DiagnosticMessage* UnknownDidYouMeanDiagnostic() const = 0;
};
struct compilerOptionsParser : optionParser {
	tsc::CompilerOptions* CompilerOptions = nullptr;
	explicit compilerOptionsParser(tsc::CompilerOptions* o)
	    : CompilerOptions(o) {}
	std::vector<Diagnostic*> ParseOption(
	    std::string_view key, const CompilerOptionsValue& value) override;
	const DiagnosticMessage* UnknownOptionDiagnostic() const override;
	const DiagnosticMessage* UnknownDidYouMeanDiagnostic() const override;
};
struct watchOptionsParser : optionParser {
	::tsc::WatchOptions* WatchOptions = nullptr;
	explicit watchOptionsParser(::tsc::WatchOptions* o) : WatchOptions(o) {}
	std::vector<Diagnostic*> ParseOption(
	    std::string_view key, const CompilerOptionsValue& value) override;
	const DiagnosticMessage* UnknownOptionDiagnostic() const override;
	const DiagnosticMessage* UnknownDidYouMeanDiagnostic() const override;
};
struct typeAcquisitionParser : optionParser {
	::tsc::TypeAcquisition* TypeAcquisition = nullptr;
	explicit typeAcquisitionParser(::tsc::TypeAcquisition* o)
	    : TypeAcquisition(o) {}
	std::vector<Diagnostic*> ParseOption(
	    std::string_view key, const CompilerOptionsValue& value) override;
	const DiagnosticMessage* UnknownOptionDiagnostic() const override;
	const DiagnosticMessage* UnknownDidYouMeanDiagnostic() const override;
};
struct buildOptionsParser : optionParser {
	::tsc::BuildOptions* BuildOptions = nullptr;
	explicit buildOptionsParser(::tsc::BuildOptions* o) : BuildOptions(o) {}
	std::vector<Diagnostic*> ParseOption(
	    std::string_view key, const CompilerOptionsValue& value) override;
	const DiagnosticMessage* UnknownOptionDiagnostic() const override;
	const DiagnosticMessage* UnknownDidYouMeanDiagnostic() const override;
};

// TSConfig — showconfig.go:53.
struct TSConfig {
	JsonObjectPtr CompilerOptions;
	JsonArray References;
	std::vector<std::string> Files;
	std::vector<std::string> Include;
	std::vector<std::string> Exclude;
	std::shared_ptr<bool> CompileOnSave;
};

// ---------------------------------------------------------------------------
// Function decls — see the .cpp files for the ported bodies.
// ---------------------------------------------------------------------------

// errors.go
Diagnostic* createDiagnosticForInvalidEnumType(const CommandLineOption* opt,
                                             SourceFile* sourceFile,
                                             Node* node);
std::string formatEnumTypeKeys(const CommandLineOption* opt,
                               const std::vector<std::string>& keys);
std::string getCompilerOptionValueTypeString(const CommandLineOption* option);
Diagnostic* createUnknownOptionError(
    std::string_view unknownOption,
    const DiagnosticMessage* unknownOptionDiagnostic,
    std::string_view unknownOptionErrorText, Node* node,
    SourceFile* sourceFile, AlternateModeDiagnostics* alternateMode,
    const DiagnosticMessage* unknownDidYouMeanDiagnostic,
    CommandLineOptionNameMap optionsNameMap);
// errors.go:92/96 — ported in errors.cpp (needs scanner::skipTrivia).
Diagnostic* CreateDiagnosticForNodeInSourceFile(
    SourceFile* sourceFile, Node* node, const DiagnosticMessage* message,
    std::vector<std::string> args = {});
Diagnostic* CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
    SourceFile* sourceFile, Node* node, const DiagnosticMessage* message,
    std::vector<std::string> args = {});
const DiagnosticMessage* extraKeyDiagnostics(std::string_view s);
const DiagnosticMessage* extraKeyDidYouMeanDiagnostics(std::string_view s);

// commandlineparser.go
commandLineParser* parseCommandLineWorker(
    ParseCommandLineWorkerDiagnostics* diagnostics,
    const std::vector<std::string>& commandLine, module::ResolutionHost* fs,
    std::string_view currentDirectory);
std::string getInputOptionName(std::string_view input);
std::pair<std::string, std::vector<Diagnostic*>> tryReadFile(
    std::string_view fileName,
    const std::function<std::pair<std::string, bool>(std::string_view)>&
        readFile,
    std::vector<Diagnostic*> errors);
std::pair<JsonArray, std::vector<Diagnostic*>> ParseListTypeOption(
    const CommandLineOption* opt, std::string_view value);
// validateJsonOptionValue — tsconfigparsing.go:375.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
validateJsonOptionValue(const CommandLineOption* opt,
                        const CompilerOptionsValue& val,
                        Node* valueExpression, SourceFile* sourceFile);
// convertJsonOptionOfEnumType — commandlineparser.go:392.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
convertJsonOptionOfEnumType(const CommandLineOption* opt,
                            std::string_view value, Node* valueExpression,
                            SourceFile* sourceFile);
ParsedCommandLine* ParseCommandLine(
    const std::vector<std::string>& commandLine, ParseConfigHost* host);
ParsedBuildCommandLine* ParseBuildCommandLine(
    const std::vector<std::string>& commandLine, ParseConfigHost* host);

// wildcarddirectories.go
struct wildcardDirectoryMatch {
	std::string Key;
	std::string Path;
	bool Recursive = false;
};
std::unordered_map<std::string, bool> getWildcardDirectories(
    const std::vector<std::string>& include,
    const std::vector<std::string>& exclude,
    const tspath::ComparePathsOptions& comparePathsOptions);
wildcardDirectoryMatch* getWildcardDirectoryFromSpec(
    std::string_view spec, bool useCaseSensitiveFileNames);

// tsconfigparsing.go
TsConfigSourceFile* NewTsconfigSourceFileFromFilePath(
    std::string_view configFileName, const tspath::Path& configPath,
    std::string_view configSourceText);
std::pair<parsedTsconfig*, std::vector<Diagnostic*>> parseConfig(
    JsonObjectPtr json, TsConfigSourceFile* sourceFile,
    ParseConfigHost* host, std::string_view basePath,
    std::string_view configFileName,
    const std::vector<tspath::Path>& resolutionStack,
    ExtendedConfigCache* extendedConfigCache);
ParsedCommandLine* ParseJsonSourceFileConfigFileContent(
    TsConfigSourceFile* sourceFile, ParseConfigHost* host,
    std::string_view basePath, CompilerOptions* existingOptions,
    const JsonObjectPtr& existingOptionsRaw,
    std::string_view configFileName,
    const std::vector<tspath::Path>& resolutionStack,
    ExtendedConfigCache* extendedConfigCache);
ParsedCommandLine* ParseJsonConfigFileContent(
    const CompilerOptionsValue& json, ParseConfigHost* host,
    std::string_view basePath, CompilerOptions* existingOptions,
    std::string_view configFileName,
    const std::vector<tspath::Path>& resolutionStack,
    ExtendedConfigCache* extendedConfigCache);
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
ParseConfigFileTextToJson(std::string_view fileName,
                          const tspath::Path& path,
                          std::string_view jsonText);
std::pair<TsConfigSourceFile*, std::vector<Diagnostic*>> readJsonConfigFile(
    std::string_view fileName, const tspath::Path& path,
    const std::function<std::pair<std::string, bool>(std::string_view)>&
        readFile);
std::pair<parsedTsconfig*, std::vector<Diagnostic*>> getExtendedConfig(
    TsConfigSourceFile* sourceFile,
    std::string_view extendedConfigFileName, ParseConfigHost* host,
    const std::vector<tspath::Path>& resolutionStack,
    ExtendedConfigCache* extendedConfigCache, extendsResult* result);
ExtendedConfigCacheEntry* ParseExtendedConfig(
    std::string_view fileName, const tspath::Path& path,
    const std::vector<tspath::Path>& resolutionStack,
    ParseConfigHost* host, ExtendedConfigCache* extendedConfigCache);
std::pair<std::vector<std::string>, int> getFileNamesFromConfigSpecs(
    const configFileSpecs& spec, std::string_view basePath,
    CompilerOptions* options, module::ResolutionHost* host,
    const std::vector<std::string>& extraExtensions);
bool hasFileWithHigherPriorityExtension(
    std::string_view file,
    const std::vector<std::vector<std::string_view>>& extensions,
    const std::function<bool(std::string_view)>& hasFile);
void removeWildcardFilesWithLowerPriorityExtension(
    std::string_view file,
    collections::OrderedMap<std::string, std::string>* wildcardFiles,
    const std::vector<std::vector<std::string_view>>& extensions,
    const std::function<std::string(std::string_view)>& keyMapper);
// GetSupportedExtensions — returns a pointer so the builtin identity check in
// GetSupportedExtensionsWithJsonIfResolveJsonModule (Go core.Same — backing
// array identity) stays faithful: builtins point at the tspath:: constants.
const std::vector<std::vector<std::string_view>>* GetSupportedExtensions(
    const CompilerOptions* compilerOptions,
    const std::vector<std::string>& extraExtensions);
const std::vector<std::vector<std::string_view>>*
GetSupportedExtensionsWithJsonIfResolveJsonModule(
    const CompilerOptions* compilerOptions,
    const std::vector<std::vector<std::string_view>>* supportedExtensions);
const DiagnosticMessage* specToDiagnostic(std::string_view spec,
                                          bool disallowTrailingRecursion);
bool invalidTrailingRecursion(std::string_view spec);
bool invalidDotDotAfterRecursiveWildcard(std::string_view spec);
Diagnostic* CreateDiagnosticAtReferenceSyntax(
    ParsedCommandLine* config, int index, const DiagnosticMessage* message,
    std::vector<std::string> args = {});
StringLiteral* GetTsConfigPropArrayElementValue(
    SourceFile* tsConfigSourceFile, std::string_view propKey,
    std::string_view elementValue);
std::function<Node*(PropertyAssignment*)>
GetCallbackForFindingPropertyAssignmentByValue(std::string_view value);
Node* GetOptionsSyntaxByArrayElementValue(
    ObjectLiteralExpression* objectLiteral, std::string_view propKey,
    std::string_view elementValue);
std::pair<SourceFile*, TextRange> GetContentMapperOptionDiagnosticLocation(
    ParsedCommandLine* config, contentmapper::Mapper* mapper,
    const std::vector<contentmapper::OptionPathSegment>& path);
ObjectLiteralExpression* getTsConfigObjectLiteralExpression(
    SourceFile* tsConfigSourceFile);
std::pair<ParsedCommandLine*, std::vector<Diagnostic*>>
GetParsedCommandLineOfConfigFile(
    std::string_view configFileName, CompilerOptions* options,
    const JsonObjectPtr& optionsRaw, ParseConfigHost* sys,
    ExtendedConfigCache* extendedConfigCache);
std::pair<ParsedCommandLine*, std::vector<Diagnostic*>>
GetParsedCommandLineOfConfigFilePath(
    std::string_view configFileName, const tspath::Path& path,
    CompilerOptions* options, const JsonObjectPtr& optionsRaw,
    ParseConfigHost* sys, ExtendedConfigCache* extendedConfigCache);
// CommandLineCompilerOptionsMap — tsconfigparsing.go:624.
const CommandLineOptionNameMap& CommandLineCompilerOptionsMap();

// ForEachPropertyAssignment — tsconfigparsing.go:1777.
template <typename T>
T* ForEachPropertyAssignment(ObjectLiteralExpression* objectLiteral,
                             std::string_view key,
                             const std::function<T*(PropertyAssignment*)>& callback,
                             std::string_view key2 = "") {
	if (objectLiteral != nullptr) {
		for (Node* property : objectLiteral->properties()) {
			if (!isPropertyAssignment(property)) {
				continue;
			}
			std::string propName;
			if (tryGetTextOfPropertyName(property->name(), propName)) {
				if (propName == key || (!key2.empty() && key2 == propName)) {
					return callback(property->as<PropertyAssignment>());
				}
			}
		}
	}
	return nullptr;
}

// ForEachTsConfigPropArray — tsconfigparsing.go:1623.
template <typename T>
T* ForEachTsConfigPropArray(SourceFile* tsConfigSourceFile,
                            std::string_view propKey,
                            const std::function<T*(PropertyAssignment*)>& callback) {
	if (tsConfigSourceFile != nullptr) {
		return ForEachPropertyAssignment<T>(
		    getTsConfigObjectLiteralExpression(tsConfigSourceFile), propKey,
		    callback);
	}
	return nullptr;
}

// parsinghelpers.go
Tristate ParseTristate(const CompilerOptionsValue& value);
std::vector<std::string> ParseStringArray(const CompilerOptionsValue& value);
// parseStringMap — returns nullptr for empty like Go's nil OrderedMap.
std::shared_ptr<collections::OrderedMap<std::string, std::vector<std::string>>>
parseStringMap(const CompilerOptionsValue& value);
std::string ParseString(const CompilerOptionsValue& value);
int* parseNumber(const CompilerOptionsValue& value);
projectReferenceParseResult* parseProjectReference(
    const CompilerOptionsValue& json);
std::pair<contentmapper::Mapper*, std::vector<Diagnostic*>> parseContentMapper(
    const CompilerOptionsValue& value);
// parseStringArrayStrict — (strings, ok).
std::pair<std::vector<std::string>, bool> parseStringArrayStrict(
    const CompilerOptionsValue& value);
// parseJsonToStringKey — *OrderedMap[string,any] or nullptr.
JsonObjectPtr parseJsonToStringKey(const CompilerOptionsValue& json);
// floatOrInt32ToFlag[T ~int32] — parsinghelpers.go:570. Enum values are
// stored in the int64 arm; raw JSON numbers land in the double arm. Any
// other arm is a Go type-assert panic — std::get throws.
template <typename T>
T floatOrInt32ToFlag(const CompilerOptionsValue& value) {
	if (auto* p = value.get<int64_t>()) {
		return static_cast<T>(*p);
	}
	return static_cast<T>(value.asDouble());
}
std::vector<Diagnostic*> ParseCompilerOptions(
    std::string_view key, const CompilerOptionsValue& value,
    CompilerOptions* allOptions);
// parseCompilerOptions — returns whether the key was found.
bool parseCompilerOptions(std::string_view key,
                          const CompilerOptionsValue& value,
                          CompilerOptions* allOptions);
std::vector<Diagnostic*> ParseWatchOptions(
    std::string_view key, const CompilerOptionsValue& value,
    ::tsc::WatchOptions* allOptions);
std::vector<Diagnostic*> ParseTypeAcquisition(
    std::string_view key, const CompilerOptionsValue& value,
    TypeAcquisition* allOptions);
std::vector<Diagnostic*> ParseBuildOptions(
    std::string_view key, const CompilerOptionsValue& value,
    BuildOptions* allOptions);
CompilerOptions* mergeCompilerOptions(
    CompilerOptions* targetOptions, const CompilerOptions* sourceOptions,
    const CompilerOptionsValue& rawSource);
// convertToOptionsWithAbsolutePaths — tsconfigparsing.go style OrderedMap passthrough.
JsonObjectPtr convertToOptionsWithAbsolutePaths(
    const JsonObjectPtr& optionsBase, const CommandLineOptionNameMap& optionMap,
    std::string_view cwd);
std::pair<CompilerOptionsValue, bool> ConvertOptionToAbsolutePath(
    std::string_view o, const CompilerOptionsValue& v,
    const CommandLineOptionNameMap& optionMap, std::string_view cwd);
// convertMapToOptions / convertOptionsFromJson — generic optionParser walk; header.
template <typename O>
O convertMapToOptions(const JsonObjectPtr& compilerOptions, O result) {
	// this assumes any `key`, `value` pair in `options` will have `value` already be the correct type. this function should no error handling
	if (compilerOptions) {
		for (const auto& key : compilerOptions->Keys()) {
			auto v = compilerOptions->Get(key);
			result->ParseOption(key, *v.first);
		}
	}
	return result;
}
// convertOptionsFromJsonImpl — the optionParser-independent body (defined
// in tsconfigparsing.cpp); the template wraps it for the concrete parser.
std::pair<optionParser*, std::vector<Diagnostic*>>
convertOptionsFromJsonImpl(const CommandLineOptionNameMap& optionsNameMap,
                           const CompilerOptionsValue& jsonOptions,
                           std::string_view basePath, optionParser* result);
// convertOptionsFromJson — tsconfigparsing.go:634.
template <typename O>
std::pair<O, std::vector<Diagnostic*>> convertOptionsFromJson(
    const CommandLineOptionNameMap& optionsNameMap,
    const CompilerOptionsValue& jsonOptions, std::string_view basePath,
    O result) {
	auto [p, errors] = convertOptionsFromJsonImpl(
	    optionsNameMap, jsonOptions, basePath, result);
	return {result, errors};
}

// declscompiler.go — reflection-backed comparisons.
bool optionsHaveChanges(
    const CompilerOptions* oldOptions, const CompilerOptions* newOptions,
    const std::function<bool(const CommandLineOption*)>& declFilter);
// ForEachCompilerOptionValue — fn signature takes the field VALUE not a flag.
bool ForEachCompilerOptionValue(
    const CompilerOptions* options,
    const std::function<bool(const CommandLineOption*)>& declFilter,
    const std::function<bool(const CommandLineOption*,
                             const CompilerOptionsValue&, int)>& fn);
bool CompilerOptionsAffectSemanticDiagnostics(
    const CompilerOptions* oldOptions, const CompilerOptions* newOptions);
bool CompilerOptionsAffectDeclarationPath(
    const CompilerOptions* oldOptions, const CompilerOptions* newOptions);
bool CompilerOptionsAffectEmit(const CompilerOptions* oldOptions,
                               const CompilerOptions* newOptions);

// showconfig.go
TSConfig* ConvertToTSConfig(ParsedCommandLine* configParseResult,
                            std::string_view configFileName);
JsonObjectPtr serializeCompilerOptions(
    const CompilerOptions* options, std::string_view configFilePath,
    const tspath::ComparePathsOptions& comparePathsOptions);

// contentmappers.go
std::tuple<contentmapper::Manifest, std::string, Diagnostic*>
resolveContentMapperManifest(ParseConfigHost* host,
                             std::string_view containingFile,
                             std::string_view packageName);

// implicit-slice helper — the hand-written field table standing in for Go's
// reflect-driven iteration over CompilerOptions (optionsType).
struct compilerOptionFieldInfo {
	std::string_view name;      // Go field name, e.g. "Module"
	std::string_view jsonName;  // json tag name, e.g. "module"
	CompilerOptionsValue (*get)(const CompilerOptions*);
	void (*set)(CompilerOptions*, const CompilerOptionsValue&);
	bool (*isZero)(const CompilerOptions*);
	void (*setZero)(CompilerOptions*);
};
const std::vector<compilerOptionFieldInfo>& compilerOptionFieldInfos();

}  // namespace tsc::tsoptions
