// proto — port of tsc/internal/api/proto.go.
//
// Struct bodies are generated mechanically from the Go json tags (see the
// per-struct `unmarshalField`/`marshalJSONTo` pairs). The encoding/json
// custom-method contracts (MarshalerTo/UnmarshalerFrom on DocumentIdentifier,
// EnsurePrograms, BatchRequestsResponse, DiagnosticResponse) are ported
// hand-in-hand with the generic field loops so embedding works like Go's.
#include "internal/api/proto.h"

#include <algorithm>
#include <cstring>
#include <typeinfo>

#include "internal/api/requestfilesystem/requestfilesystem.h"
#include "internal/core/spelling.h"
#include "internal/core/text.h"
#include "internal/format/format.h"
#include "internal/jsonrpc/jsonrpc.h"

namespace tsc::api {

// fieldIs — encoding/json tag matching: exact or Unicode-simple-folded equal.
bool fieldIs(std::string_view name, std::string_view tag) {
	return name == tag || utf8detail::equalFold(name, tag);
}

// readFields reads an object body: consumes '{' .. '}', dispatching each
// member name to readField; unmatched members are skipped by the callback.
// Mirrors the per-type UnmarshalJSONFrom loops.
std::string readFields(json::Decoder& dec,
                       const std::function<std::string(std::string_view, json::Decoder&)>& readField) {
	if (dec.peekKind() != '{') {
		return "json: cannot unmarshal non-object";
	}
	if (auto [t, e] = dec.readToken(); !e.empty()) return e;
	while (dec.peekKind() != '}') {
		auto [key, e] = dec.readToken();
		if (!e.empty()) return e;
		if (auto e2 = readField(key.string(), dec); !e2.empty()) return e2;
	}
	if (auto [t, e] = dec.readToken(); !e.empty()) return e;
	return {};
}

// isZeroVal — encoding/json's emptiness test for omitempty/omitzero members.
template <class T>
bool isZeroVal(const T& v) {
	if constexpr (requires { v.empty(); }) return v.empty();
	else if constexpr (std::is_same_v<T, bool>) return !v;
	else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) return v == T{};
	else if constexpr (std::is_pointer_v<T>) return v == nullptr;
	else return false;
}
template <class T>
bool isZeroVal(const std::optional<T>& v) { return !v.has_value(); }
template <class T>
bool isZeroVal(const std::shared_ptr<T>& v) { return !v; }

// objWriter accumulates `{name value ...}` writes on an Encoder; the first
// error sticks, like Go's early returns.
struct objWriter {
	json::Encoder& enc;
	std::string err;

	void begin() {
		if (err.empty()) err = enc.writeToken(json::BeginObject);
	}
	std::string end() {
		if (err.empty()) err = enc.writeToken(json::EndObject);
		return err;
	}
	void name(std::string_view n) {
		if (err.empty()) err = enc.writeValue(json::marshalString(n));
	}
	template <class T>
	void member(std::string_view n, const T& v) {
		if (err.empty()) name(n);
		if (err.empty()) err = json::marshalEncode(enc, v);
	}
	// memberRaw writes an already-encoded JSON value (Go `any`/`json.Value`).
	void memberRaw(std::string_view n, const json::Value& v) {
		if (err.empty()) name(n);
		if (err.empty()) err = enc.writeValue(v.empty() ? json::Value("null") : v);
	}
	void memberTristate(std::string_view n, Tristate v) {
		if (err.empty()) name(n);
		if (err.empty())
			err = enc.writeValue(v == Tristate::True   ? json::Value("true")
			                     : v == Tristate::False ? json::Value("false")
			                                            : json::Value("null"));
	}
	// memberPaths — `paths` is OrderedMap[string, []string] in Go but stored
	// as an ordered vector of pairs here; it marshals as a JSON object.
	void memberPaths(std::string_view n,
	                 const std::vector<std::pair<std::string, std::vector<std::string>>>& v) {
		if (err.empty()) name(n);
		if (err.empty()) err = enc.writeToken(json::BeginObject);
		for (auto& [k, vals] : v) {
			if (err.empty()) name(k);
			if (err.empty()) err = json::marshalEncode(enc, vals);
		}
		if (err.empty()) err = enc.writeToken(json::EndObject);
	}
};

// unmarshalPaths — decode a `paths` object into the ordered pair vector.
std::string unmarshalPaths(json::Decoder& dec,
                           std::vector<std::pair<std::string, std::vector<std::string>>>* out) {
	if (dec.peekKind() == 'n') {
		if (auto [t, e] = dec.readToken(); !e.empty()) return e;
		return {};
	}
	return readFields(dec, [&](std::string_view n, json::Decoder& d) -> std::string {
		std::vector<std::string> vals;
		if (auto e = json::unmarshalDecode(d, &vals); !e.empty()) return e;
		out->emplace_back(n, std::move(vals));
		return std::string{};
	});
}

}  // namespace tsc::api

// === slice: api === — core-type encoding/json hooks defined here.

namespace tsc {

// unmarshalJSONFrom — tristate.go:42 UnmarshalJSON: true→True, false→False,
// anything else → Unknown, never errors.
std::string unmarshalJSONFrom(json::Decoder& dec, Tristate* out) {
	auto [v, e] = dec.readValue();
	if (!e.empty()) return e;
	if (v == "true")
		*out = Tristate::True;
	else if (v == "false")
		*out = Tristate::False;
	else
		*out = Tristate::Unknown;
	return {};
}

// marshalJSONTo — tristate.go:52 MarshalJSON: True→true, False→false,
// Unknown→null.
std::string marshalJSONTo(json::Encoder& enc, const Tristate& v) {
	return enc.writeValue(v == Tristate::True   ? json::Value("true")
	                      : v == Tristate::False ? json::Value("false")
	                                             : json::Value("null"));
}

// PluginImport — compileroptions.go:29. `Name` is `json:"name"`.
std::string PluginImport::unmarshalJSONFrom(json::Decoder& dec) {
	return api::readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		if (api::fieldIs(n, "name")) return json::unmarshalDecode(d, &name);
		return d.skipValue();
	});
}
std::string PluginImport::marshalJSONTo(json::Encoder& enc) const {
	api::objWriter w{enc};
	w.begin();
	if (!name.empty()) w.member("name", name);
	return w.end();
}

// CompilerOptions field-name set — used by CreateBuildOrchestratorParams to
// route embedded *CompilerOptions members.
bool compilerOptionsFieldName(std::string_view n);
std::string unmarshalCompilerOptionsField(CompilerOptions* o, std::string_view n, json::Decoder& d);
bool buildOptionsFieldName(std::string_view n);
std::string unmarshalBuildOptionsField(BuildOptions* o, std::string_view n, json::Decoder& d);

// BuildOptions — buildoptions.go:11.
std::string BuildOptions::unmarshalJSONFrom(json::Decoder& dec) {
	return api::readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		if (buildOptionsFieldName(n)) return unmarshalBuildOptionsField(this, n, d);
		return d.skipValue();
	});
}
bool buildOptionsFieldName(std::string_view n) {
	return api::fieldIs(n, "dry") || api::fieldIs(n, "force") || api::fieldIs(n, "verbose") ||
	       api::fieldIs(n, "builders") || api::fieldIs(n, "stopBuildOnErrors") ||
	       api::fieldIs(n, "clean");
}
std::string unmarshalBuildOptionsField(BuildOptions* o, std::string_view n, json::Decoder& d) {
	if (api::fieldIs(n, "dry")) return json::unmarshalDecode(d, &o->Dry);
	if (api::fieldIs(n, "force")) return json::unmarshalDecode(d, &o->Force);
	if (api::fieldIs(n, "verbose")) return json::unmarshalDecode(d, &o->Verbose);
	if (api::fieldIs(n, "builders")) return json::unmarshalDecode(d, &o->Builders);
	if (api::fieldIs(n, "stopBuildOnErrors")) return json::unmarshalDecode(d, &o->StopBuildOnErrors);
	if (api::fieldIs(n, "clean")) return json::unmarshalDecode(d, &o->Clean);
	return "unreachable";
}
std::string BuildOptions::marshalJSONTo(json::Encoder& enc) const {
	api::objWriter w{enc};
	w.begin();
	if (Dry != Tristate::Unknown) w.memberTristate("dry", Dry);
	if (Force != Tristate::Unknown) w.memberTristate("force", Force);
	if (Verbose != Tristate::Unknown) w.memberTristate("verbose", Verbose);
	if (Builders) w.member("builders", *Builders);
	if (StopBuildOnErrors != Tristate::Unknown) w.memberTristate("stopBuildOnErrors", StopBuildOnErrors);
	if (Clean != Tristate::Unknown) w.memberTristate("clean", Clean);
	return w.end();
}

// ProjectReference — types.go project reference.
std::string ProjectReference::unmarshalJSONFrom(json::Decoder& dec) {
	return api::readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		if (api::fieldIs(n, "path")) return json::unmarshalDecode(d, &Path);
		if (api::fieldIs(n, "originalPath")) return json::unmarshalDecode(d, &OriginalPath);
		if (api::fieldIs(n, "circular")) return json::unmarshalDecode(d, &Circular);
		return d.skipValue();
	});
}
std::string ProjectReference::marshalJSONTo(json::Encoder& enc) const {
	api::objWriter w{enc};
	w.begin();
	w.member("path", Path);
	if (!OriginalPath.empty()) w.member("originalPath", OriginalPath);
	if (Circular) w.member("circular", Circular);
	return w.end();
}

// TypeAcquisition — typeacquisition.go.
std::string TypeAcquisition::unmarshalJSONFrom(json::Decoder& dec) {
	return api::readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		if (api::fieldIs(n, "enable")) return json::unmarshalDecode(d, &Enable);
		if (api::fieldIs(n, "include")) return json::unmarshalDecode(d, &Include);
		if (api::fieldIs(n, "exclude")) return json::unmarshalDecode(d, &Exclude);
		if (api::fieldIs(n, "disableFilenameBasedTypeAcquisition"))
			return json::unmarshalDecode(d, &DisableFilenameBasedTypeAcquisition);
		return d.skipValue();
	});
}
std::string TypeAcquisition::marshalJSONTo(json::Encoder& enc) const {
	api::objWriter w{enc};
	w.begin();
	if (Enable != Tristate::Unknown) w.memberTristate("enable", Enable);
	if (!Include.empty()) w.member("include", Include);
	if (!Exclude.empty()) w.member("exclude", Exclude);
	if (DisableFilenameBasedTypeAcquisition != Tristate::Unknown)
		w.memberTristate("disableFilenameBasedTypeAcquisition", DisableFilenameBasedTypeAcquisition);
	return w.end();
}


}  // namespace tsc

namespace tsc {

// CompilerOptions::unmarshalJSONFrom / marshalJSONTo — compileroptions.go tagged fields.

bool compilerOptionsFieldName(std::string_view n) {
	return api::fieldIs(n, "allowJs") ||
	       api::fieldIs(n, "allowArbitraryExtensions") ||
	       api::fieldIs(n, "allowImportingTsExtensions") ||
	       api::fieldIs(n, "allowNonTsExtensions") ||
	       api::fieldIs(n, "allowUmdGlobalAccess") ||
	       api::fieldIs(n, "allowUnreachableCode") ||
	       api::fieldIs(n, "allowUnusedLabels") ||
	       api::fieldIs(n, "assumeChangesOnlyAffectDirectDependencies") ||
	       api::fieldIs(n, "checkJs") ||
	       api::fieldIs(n, "customConditions") ||
	       api::fieldIs(n, "composite") ||
	       api::fieldIs(n, "emitDeclarationOnly") ||
	       api::fieldIs(n, "emitBOM") ||
	       api::fieldIs(n, "emitDecoratorMetadata") ||
	       api::fieldIs(n, "declaration") ||
	       api::fieldIs(n, "declarationDir") ||
	       api::fieldIs(n, "declarationMap") ||
	       api::fieldIs(n, "deduplicatePackages") ||
	       api::fieldIs(n, "disableSizeLimit") ||
	       api::fieldIs(n, "disableSourceOfProjectReferenceRedirect") ||
	       api::fieldIs(n, "disableSolutionSearching") ||
	       api::fieldIs(n, "disableReferencedProjectLoad") ||
	       api::fieldIs(n, "erasableSyntaxOnly") ||
	       api::fieldIs(n, "exactOptionalPropertyTypes") ||
	       api::fieldIs(n, "experimentalDecorators") ||
	       api::fieldIs(n, "forceConsistentCasingInFileNames") ||
	       api::fieldIs(n, "isolatedModules") ||
	       api::fieldIs(n, "isolatedDeclarations") ||
	       api::fieldIs(n, "ignoreConfig") ||
	       api::fieldIs(n, "ignoreDeprecations") ||
	       api::fieldIs(n, "importHelpers") ||
	       api::fieldIs(n, "inlineSourceMap") ||
	       api::fieldIs(n, "inlineSources") ||
	       api::fieldIs(n, "init") ||
	       api::fieldIs(n, "incremental") ||
	       api::fieldIs(n, "jsx") ||
	       api::fieldIs(n, "jsxFactory") ||
	       api::fieldIs(n, "jsxFragmentFactory") ||
	       api::fieldIs(n, "jsxImportSource") ||
	       api::fieldIs(n, "lib") ||
	       api::fieldIs(n, "libReplacement") ||
	       api::fieldIs(n, "locale") ||
	       api::fieldIs(n, "mapRoot") ||
	       api::fieldIs(n, "module") ||
	       api::fieldIs(n, "moduleResolution") ||
	       api::fieldIs(n, "moduleSuffixes") ||
	       api::fieldIs(n, "moduleDetection") ||
	       api::fieldIs(n, "newLine") ||
	       api::fieldIs(n, "noEmit") ||
	       api::fieldIs(n, "noCheck") ||
	       api::fieldIs(n, "noErrorTruncation") ||
	       api::fieldIs(n, "noFallthroughCasesInSwitch") ||
	       api::fieldIs(n, "noImplicitAny") ||
	       api::fieldIs(n, "noImplicitThis") ||
	       api::fieldIs(n, "noImplicitReturns") ||
	       api::fieldIs(n, "noEmitHelpers") ||
	       api::fieldIs(n, "noLib") ||
	       api::fieldIs(n, "noPropertyAccessFromIndexSignature") ||
	       api::fieldIs(n, "noUncheckedIndexedAccess") ||
	       api::fieldIs(n, "noEmitOnError") ||
	       api::fieldIs(n, "noUnusedLocals") ||
	       api::fieldIs(n, "noUnusedParameters") ||
	       api::fieldIs(n, "noResolve") ||
	       api::fieldIs(n, "noImplicitOverride") ||
	       api::fieldIs(n, "noUncheckedSideEffectImports") ||
	       api::fieldIs(n, "outDir") ||
	       api::fieldIs(n, "plugins") ||
	       api::fieldIs(n, "preserveConstEnums") ||
	       api::fieldIs(n, "preserveSymlinks") ||
	       api::fieldIs(n, "project") ||
	       api::fieldIs(n, "resolveJsonModule") ||
	       api::fieldIs(n, "resolvePackageJsonExports") ||
	       api::fieldIs(n, "resolvePackageJsonImports") ||
	       api::fieldIs(n, "removeComments") ||
	       api::fieldIs(n, "rewriteRelativeImportExtensions") ||
	       api::fieldIs(n, "reactNamespace") ||
	       api::fieldIs(n, "rootDir") ||
	       api::fieldIs(n, "rootDirs") ||
	       api::fieldIs(n, "skipLibCheck") ||
	       api::fieldIs(n, "stableTypeOrdering") ||
	       api::fieldIs(n, "strict") ||
	       api::fieldIs(n, "strictBindCallApply") ||
	       api::fieldIs(n, "strictBuiltinIteratorReturn") ||
	       api::fieldIs(n, "strictFunctionTypes") ||
	       api::fieldIs(n, "strictNullChecks") ||
	       api::fieldIs(n, "strictPropertyInitialization") ||
	       api::fieldIs(n, "stripInternal") ||
	       api::fieldIs(n, "skipDefaultLibCheck") ||
	       api::fieldIs(n, "sourceMap") ||
	       api::fieldIs(n, "sourceRoot") ||
	       api::fieldIs(n, "suppressOutputPathCheck") ||
	       api::fieldIs(n, "target") ||
	       api::fieldIs(n, "traceResolution") ||
	       api::fieldIs(n, "tsBuildInfoFile") ||
	       api::fieldIs(n, "typeRoots") ||
	       api::fieldIs(n, "types") ||
	       api::fieldIs(n, "useDefineForClassFields") ||
	       api::fieldIs(n, "useUnknownInCatchVariables") ||
	       api::fieldIs(n, "verbatimModuleSyntax") ||
	       api::fieldIs(n, "maxNodeModuleJsDepth") ||
	       api::fieldIs(n, "allowSyntheticDefaultImports") ||
	       api::fieldIs(n, "alwaysStrict") ||
	       api::fieldIs(n, "baseUrl") ||
	       api::fieldIs(n, "downlevelIteration") ||
	       api::fieldIs(n, "esModuleInterop") ||
	       api::fieldIs(n, "outFile") ||
	       api::fieldIs(n, "configFilePath") ||
	       api::fieldIs(n, "noDtsResolution") ||
	       api::fieldIs(n, "pathsBasePath") ||
	       api::fieldIs(n, "diagnostics") ||
	       api::fieldIs(n, "extendedDiagnostics") ||
	       api::fieldIs(n, "generateCpuProfile") ||
	       api::fieldIs(n, "generateTrace") ||
	       api::fieldIs(n, "listEmittedFiles") ||
	       api::fieldIs(n, "listFiles") ||
	       api::fieldIs(n, "explainFiles") ||
	       api::fieldIs(n, "listFilesOnly") ||
	       api::fieldIs(n, "noEmitForJsFiles") ||
	       api::fieldIs(n, "preserveWatchOutput") ||
	       api::fieldIs(n, "pretty") ||
	       api::fieldIs(n, "version") ||
	       api::fieldIs(n, "watch") ||
	       api::fieldIs(n, "showConfig") ||
	       api::fieldIs(n, "build") ||
	       api::fieldIs(n, "help") ||
	       api::fieldIs(n, "all") ||
	       api::fieldIs(n, "runExternalCode") ||
	       api::fieldIs(n, "pprofDir") ||
	       api::fieldIs(n, "singleThreaded") ||
	       api::fieldIs(n, "quiet") ||
	       api::fieldIs(n, "checkers");
}

std::string unmarshalCompilerOptionsField(CompilerOptions* o, std::string_view n, json::Decoder& d) {
	if (api::fieldIs(n, "allowJs")) return json::unmarshalDecode(d, &o->AllowJs);
	if (api::fieldIs(n, "allowArbitraryExtensions")) return json::unmarshalDecode(d, &o->AllowArbitraryExtensions);
	if (api::fieldIs(n, "allowImportingTsExtensions")) return json::unmarshalDecode(d, &o->AllowImportingTsExtensions);
	if (api::fieldIs(n, "allowNonTsExtensions")) return json::unmarshalDecode(d, &o->AllowNonTsExtensions);
	if (api::fieldIs(n, "allowUmdGlobalAccess")) return json::unmarshalDecode(d, &o->AllowUmdGlobalAccess);
	if (api::fieldIs(n, "allowUnreachableCode")) return json::unmarshalDecode(d, &o->AllowUnreachableCode);
	if (api::fieldIs(n, "allowUnusedLabels")) return json::unmarshalDecode(d, &o->AllowUnusedLabels);
	if (api::fieldIs(n, "assumeChangesOnlyAffectDirectDependencies")) return json::unmarshalDecode(d, &o->AssumeChangesOnlyAffectDirectDependencies);
	if (api::fieldIs(n, "checkJs")) return json::unmarshalDecode(d, &o->CheckJs);
	if (api::fieldIs(n, "customConditions")) return json::unmarshalDecode(d, &o->CustomConditions);
	if (api::fieldIs(n, "composite")) return json::unmarshalDecode(d, &o->Composite);
	if (api::fieldIs(n, "emitDeclarationOnly")) return json::unmarshalDecode(d, &o->EmitDeclarationOnly);
	if (api::fieldIs(n, "emitBOM")) return json::unmarshalDecode(d, &o->EmitBOM);
	if (api::fieldIs(n, "emitDecoratorMetadata")) return json::unmarshalDecode(d, &o->EmitDecoratorMetadata);
	if (api::fieldIs(n, "declaration")) return json::unmarshalDecode(d, &o->Declaration);
	if (api::fieldIs(n, "declarationDir")) return json::unmarshalDecode(d, &o->DeclarationDir);
	if (api::fieldIs(n, "declarationMap")) return json::unmarshalDecode(d, &o->DeclarationMap);
	if (api::fieldIs(n, "deduplicatePackages")) return json::unmarshalDecode(d, &o->DeduplicatePackages);
	if (api::fieldIs(n, "disableSizeLimit")) return json::unmarshalDecode(d, &o->DisableSizeLimit);
	if (api::fieldIs(n, "disableSourceOfProjectReferenceRedirect")) return json::unmarshalDecode(d, &o->DisableSourceOfProjectReferenceRedirect);
	if (api::fieldIs(n, "disableSolutionSearching")) return json::unmarshalDecode(d, &o->DisableSolutionSearching);
	if (api::fieldIs(n, "disableReferencedProjectLoad")) return json::unmarshalDecode(d, &o->DisableReferencedProjectLoad);
	if (api::fieldIs(n, "erasableSyntaxOnly")) return json::unmarshalDecode(d, &o->ErasableSyntaxOnly);
	if (api::fieldIs(n, "exactOptionalPropertyTypes")) return json::unmarshalDecode(d, &o->ExactOptionalPropertyTypes);
	if (api::fieldIs(n, "experimentalDecorators")) return json::unmarshalDecode(d, &o->ExperimentalDecorators);
	if (api::fieldIs(n, "forceConsistentCasingInFileNames")) return json::unmarshalDecode(d, &o->ForceConsistentCasingInFileNames);
	if (api::fieldIs(n, "isolatedModules")) return json::unmarshalDecode(d, &o->IsolatedModules);
	if (api::fieldIs(n, "isolatedDeclarations")) return json::unmarshalDecode(d, &o->IsolatedDeclarations);
	if (api::fieldIs(n, "ignoreConfig")) return json::unmarshalDecode(d, &o->IgnoreConfig);
	if (api::fieldIs(n, "ignoreDeprecations")) return json::unmarshalDecode(d, &o->IgnoreDeprecations);
	if (api::fieldIs(n, "importHelpers")) return json::unmarshalDecode(d, &o->ImportHelpers);
	if (api::fieldIs(n, "inlineSourceMap")) return json::unmarshalDecode(d, &o->InlineSourceMap);
	if (api::fieldIs(n, "inlineSources")) return json::unmarshalDecode(d, &o->InlineSources);
	if (api::fieldIs(n, "init")) return json::unmarshalDecode(d, &o->Init);
	if (api::fieldIs(n, "incremental")) return json::unmarshalDecode(d, &o->Incremental);
	if (api::fieldIs(n, "jsx")) return json::unmarshalDecode(d, &o->Jsx);
	if (api::fieldIs(n, "jsxFactory")) return json::unmarshalDecode(d, &o->JsxFactory);
	if (api::fieldIs(n, "jsxFragmentFactory")) return json::unmarshalDecode(d, &o->JsxFragmentFactory);
	if (api::fieldIs(n, "jsxImportSource")) return json::unmarshalDecode(d, &o->JsxImportSource);
	if (api::fieldIs(n, "lib")) return json::unmarshalDecode(d, &o->Lib);
	if (api::fieldIs(n, "libReplacement")) return json::unmarshalDecode(d, &o->LibReplacement);
	if (api::fieldIs(n, "locale")) return json::unmarshalDecode(d, &o->Locale);
	if (api::fieldIs(n, "mapRoot")) return json::unmarshalDecode(d, &o->MapRoot);
	if (api::fieldIs(n, "module")) return json::unmarshalDecode(d, &o->Module);
	if (api::fieldIs(n, "moduleResolution")) return json::unmarshalDecode(d, &o->ModuleResolution);
	if (api::fieldIs(n, "moduleSuffixes")) return json::unmarshalDecode(d, &o->ModuleSuffixes);
	if (api::fieldIs(n, "moduleDetection")) return json::unmarshalDecode(d, &o->ModuleDetection);
	if (api::fieldIs(n, "newLine")) return json::unmarshalDecode(d, &o->NewLine);
	if (api::fieldIs(n, "noEmit")) return json::unmarshalDecode(d, &o->NoEmit);
	if (api::fieldIs(n, "noCheck")) return json::unmarshalDecode(d, &o->NoCheck);
	if (api::fieldIs(n, "noErrorTruncation")) return json::unmarshalDecode(d, &o->NoErrorTruncation);
	if (api::fieldIs(n, "noFallthroughCasesInSwitch")) return json::unmarshalDecode(d, &o->NoFallthroughCasesInSwitch);
	if (api::fieldIs(n, "noImplicitAny")) return json::unmarshalDecode(d, &o->NoImplicitAny);
	if (api::fieldIs(n, "noImplicitThis")) return json::unmarshalDecode(d, &o->NoImplicitThis);
	if (api::fieldIs(n, "noImplicitReturns")) return json::unmarshalDecode(d, &o->NoImplicitReturns);
	if (api::fieldIs(n, "noEmitHelpers")) return json::unmarshalDecode(d, &o->NoEmitHelpers);
	if (api::fieldIs(n, "noLib")) return json::unmarshalDecode(d, &o->NoLib);
	if (api::fieldIs(n, "noPropertyAccessFromIndexSignature")) return json::unmarshalDecode(d, &o->NoPropertyAccessFromIndexSignature);
	if (api::fieldIs(n, "noUncheckedIndexedAccess")) return json::unmarshalDecode(d, &o->NoUncheckedIndexedAccess);
	if (api::fieldIs(n, "noEmitOnError")) return json::unmarshalDecode(d, &o->NoEmitOnError);
	if (api::fieldIs(n, "noUnusedLocals")) return json::unmarshalDecode(d, &o->NoUnusedLocals);
	if (api::fieldIs(n, "noUnusedParameters")) return json::unmarshalDecode(d, &o->NoUnusedParameters);
	if (api::fieldIs(n, "noResolve")) return json::unmarshalDecode(d, &o->NoResolve);
	if (api::fieldIs(n, "noImplicitOverride")) return json::unmarshalDecode(d, &o->NoImplicitOverride);
	if (api::fieldIs(n, "noUncheckedSideEffectImports")) return json::unmarshalDecode(d, &o->NoUncheckedSideEffectImports);
	if (api::fieldIs(n, "outDir")) return json::unmarshalDecode(d, &o->OutDir);
	if (api::fieldIs(n, "plugins")) return json::unmarshalDecode(d, &o->Plugins);
	if (api::fieldIs(n, "preserveConstEnums")) return json::unmarshalDecode(d, &o->PreserveConstEnums);
	if (api::fieldIs(n, "preserveSymlinks")) return json::unmarshalDecode(d, &o->PreserveSymlinks);
	if (api::fieldIs(n, "project")) return json::unmarshalDecode(d, &o->Project);
	if (api::fieldIs(n, "resolveJsonModule")) return json::unmarshalDecode(d, &o->ResolveJsonModule);
	if (api::fieldIs(n, "resolvePackageJsonExports")) return json::unmarshalDecode(d, &o->ResolvePackageJsonExports);
	if (api::fieldIs(n, "resolvePackageJsonImports")) return json::unmarshalDecode(d, &o->ResolvePackageJsonImports);
	if (api::fieldIs(n, "removeComments")) return json::unmarshalDecode(d, &o->RemoveComments);
	if (api::fieldIs(n, "rewriteRelativeImportExtensions")) return json::unmarshalDecode(d, &o->RewriteRelativeImportExtensions);
	if (api::fieldIs(n, "reactNamespace")) return json::unmarshalDecode(d, &o->ReactNamespace);
	if (api::fieldIs(n, "rootDir")) return json::unmarshalDecode(d, &o->RootDir);
	if (api::fieldIs(n, "rootDirs")) return json::unmarshalDecode(d, &o->RootDirs);
	if (api::fieldIs(n, "skipLibCheck")) return json::unmarshalDecode(d, &o->SkipLibCheck);
	if (api::fieldIs(n, "stableTypeOrdering")) return json::unmarshalDecode(d, &o->StableTypeOrdering);
	if (api::fieldIs(n, "strict")) return json::unmarshalDecode(d, &o->Strict);
	if (api::fieldIs(n, "strictBindCallApply")) return json::unmarshalDecode(d, &o->StrictBindCallApply);
	if (api::fieldIs(n, "strictBuiltinIteratorReturn")) return json::unmarshalDecode(d, &o->StrictBuiltinIteratorReturn);
	if (api::fieldIs(n, "strictFunctionTypes")) return json::unmarshalDecode(d, &o->StrictFunctionTypes);
	if (api::fieldIs(n, "strictNullChecks")) return json::unmarshalDecode(d, &o->StrictNullChecks);
	if (api::fieldIs(n, "strictPropertyInitialization")) return json::unmarshalDecode(d, &o->StrictPropertyInitialization);
	if (api::fieldIs(n, "stripInternal")) return json::unmarshalDecode(d, &o->StripInternal);
	if (api::fieldIs(n, "skipDefaultLibCheck")) return json::unmarshalDecode(d, &o->SkipDefaultLibCheck);
	if (api::fieldIs(n, "sourceMap")) return json::unmarshalDecode(d, &o->SourceMap);
	if (api::fieldIs(n, "sourceRoot")) return json::unmarshalDecode(d, &o->SourceRoot);
	if (api::fieldIs(n, "suppressOutputPathCheck")) return json::unmarshalDecode(d, &o->SuppressOutputPathCheck);
	if (api::fieldIs(n, "target")) return json::unmarshalDecode(d, &o->Target);
	if (api::fieldIs(n, "traceResolution")) return json::unmarshalDecode(d, &o->TraceResolution);
	if (api::fieldIs(n, "tsBuildInfoFile")) return json::unmarshalDecode(d, &o->TsBuildInfoFile);
	if (api::fieldIs(n, "typeRoots")) return json::unmarshalDecode(d, &o->TypeRoots);
	if (api::fieldIs(n, "types")) {
		if (auto e = json::unmarshalDecode(d, &o->Types); !e.empty()) return e;
		o->TypesWasSet = true;
		return {};
	}
	if (api::fieldIs(n, "useDefineForClassFields")) return json::unmarshalDecode(d, &o->UseDefineForClassFields);
	if (api::fieldIs(n, "useUnknownInCatchVariables")) return json::unmarshalDecode(d, &o->UseUnknownInCatchVariables);
	if (api::fieldIs(n, "verbatimModuleSyntax")) return json::unmarshalDecode(d, &o->VerbatimModuleSyntax);
	if (api::fieldIs(n, "maxNodeModuleJsDepth")) return json::unmarshalDecode(d, &o->MaxNodeModuleJsDepth);
	if (api::fieldIs(n, "allowSyntheticDefaultImports")) return json::unmarshalDecode(d, &o->AllowSyntheticDefaultImports);
	if (api::fieldIs(n, "alwaysStrict")) return json::unmarshalDecode(d, &o->AlwaysStrict);
	if (api::fieldIs(n, "baseUrl")) return json::unmarshalDecode(d, &o->BaseUrl);
	if (api::fieldIs(n, "downlevelIteration")) return json::unmarshalDecode(d, &o->DownlevelIteration);
	if (api::fieldIs(n, "esModuleInterop")) return json::unmarshalDecode(d, &o->ESModuleInterop);
	if (api::fieldIs(n, "outFile")) return json::unmarshalDecode(d, &o->OutFile);
	if (api::fieldIs(n, "configFilePath")) return json::unmarshalDecode(d, &o->ConfigFilePath);
	if (api::fieldIs(n, "noDtsResolution")) return json::unmarshalDecode(d, &o->NoDtsResolution);
	if (api::fieldIs(n, "pathsBasePath")) return json::unmarshalDecode(d, &o->PathsBasePath);
	if (api::fieldIs(n, "diagnostics")) return json::unmarshalDecode(d, &o->Diagnostics);
	if (api::fieldIs(n, "extendedDiagnostics")) return json::unmarshalDecode(d, &o->ExtendedDiagnostics);
	if (api::fieldIs(n, "generateCpuProfile")) return json::unmarshalDecode(d, &o->GenerateCpuProfile);
	if (api::fieldIs(n, "generateTrace")) return json::unmarshalDecode(d, &o->GenerateTrace);
	if (api::fieldIs(n, "listEmittedFiles")) return json::unmarshalDecode(d, &o->ListEmittedFiles);
	if (api::fieldIs(n, "listFiles")) return json::unmarshalDecode(d, &o->ListFiles);
	if (api::fieldIs(n, "explainFiles")) return json::unmarshalDecode(d, &o->ExplainFiles);
	if (api::fieldIs(n, "listFilesOnly")) return json::unmarshalDecode(d, &o->ListFilesOnly);
	if (api::fieldIs(n, "noEmitForJsFiles")) return json::unmarshalDecode(d, &o->NoEmitForJsFiles);
	if (api::fieldIs(n, "preserveWatchOutput")) return json::unmarshalDecode(d, &o->PreserveWatchOutput);
	if (api::fieldIs(n, "pretty")) return json::unmarshalDecode(d, &o->Pretty);
	if (api::fieldIs(n, "version")) return json::unmarshalDecode(d, &o->Version);
	if (api::fieldIs(n, "watch")) return json::unmarshalDecode(d, &o->Watch);
	if (api::fieldIs(n, "showConfig")) return json::unmarshalDecode(d, &o->ShowConfig);
	if (api::fieldIs(n, "build")) return json::unmarshalDecode(d, &o->Build);
	if (api::fieldIs(n, "help")) return json::unmarshalDecode(d, &o->Help);
	if (api::fieldIs(n, "all")) return json::unmarshalDecode(d, &o->All);
	if (api::fieldIs(n, "runExternalCode")) return json::unmarshalDecode(d, &o->RunExternalCode);
	if (api::fieldIs(n, "pprofDir")) return json::unmarshalDecode(d, &o->PprofDir);
	if (api::fieldIs(n, "singleThreaded")) return json::unmarshalDecode(d, &o->SingleThreaded);
	if (api::fieldIs(n, "quiet")) return json::unmarshalDecode(d, &o->Quiet);
	if (api::fieldIs(n, "checkers")) return json::unmarshalDecode(d, &o->Checkers);
	return "unreachable";
}

std::string CompilerOptions::unmarshalJSONFrom(json::Decoder& dec) {
	return api::readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		if (compilerOptionsFieldName(n)) return unmarshalCompilerOptionsField(this, n, d);
		return d.skipValue();
	});
}

std::string CompilerOptions::marshalJSONTo(json::Encoder& enc) const {
	api::objWriter w{enc};
	w.begin();
	if (AllowJs != Tristate::Unknown) w.memberTristate("allowJs", AllowJs);
	if (AllowArbitraryExtensions != Tristate::Unknown) w.memberTristate("allowArbitraryExtensions", AllowArbitraryExtensions);
	if (AllowImportingTsExtensions != Tristate::Unknown) w.memberTristate("allowImportingTsExtensions", AllowImportingTsExtensions);
	if (AllowNonTsExtensions != Tristate::Unknown) w.memberTristate("allowNonTsExtensions", AllowNonTsExtensions);
	if (AllowUmdGlobalAccess != Tristate::Unknown) w.memberTristate("allowUmdGlobalAccess", AllowUmdGlobalAccess);
	if (AllowUnreachableCode != Tristate::Unknown) w.memberTristate("allowUnreachableCode", AllowUnreachableCode);
	if (AllowUnusedLabels != Tristate::Unknown) w.memberTristate("allowUnusedLabels", AllowUnusedLabels);
	if (AssumeChangesOnlyAffectDirectDependencies != Tristate::Unknown) w.memberTristate("assumeChangesOnlyAffectDirectDependencies", AssumeChangesOnlyAffectDirectDependencies);
	if (CheckJs != Tristate::Unknown) w.memberTristate("checkJs", CheckJs);
	if (!api::isZeroVal(CustomConditions)) w.member("customConditions", CustomConditions);
	if (Composite != Tristate::Unknown) w.memberTristate("composite", Composite);
	if (EmitDeclarationOnly != Tristate::Unknown) w.memberTristate("emitDeclarationOnly", EmitDeclarationOnly);
	if (EmitBOM != Tristate::Unknown) w.memberTristate("emitBOM", EmitBOM);
	if (EmitDecoratorMetadata != Tristate::Unknown) w.memberTristate("emitDecoratorMetadata", EmitDecoratorMetadata);
	if (Declaration != Tristate::Unknown) w.memberTristate("declaration", Declaration);
	if (!api::isZeroVal(DeclarationDir)) w.member("declarationDir", DeclarationDir);
	if (DeclarationMap != Tristate::Unknown) w.memberTristate("declarationMap", DeclarationMap);
	if (DeduplicatePackages != Tristate::Unknown) w.memberTristate("deduplicatePackages", DeduplicatePackages);
	if (DisableSizeLimit != Tristate::Unknown) w.memberTristate("disableSizeLimit", DisableSizeLimit);
	if (DisableSourceOfProjectReferenceRedirect != Tristate::Unknown) w.memberTristate("disableSourceOfProjectReferenceRedirect", DisableSourceOfProjectReferenceRedirect);
	if (DisableSolutionSearching != Tristate::Unknown) w.memberTristate("disableSolutionSearching", DisableSolutionSearching);
	if (DisableReferencedProjectLoad != Tristate::Unknown) w.memberTristate("disableReferencedProjectLoad", DisableReferencedProjectLoad);
	if (ErasableSyntaxOnly != Tristate::Unknown) w.memberTristate("erasableSyntaxOnly", ErasableSyntaxOnly);
	if (ExactOptionalPropertyTypes != Tristate::Unknown) w.memberTristate("exactOptionalPropertyTypes", ExactOptionalPropertyTypes);
	if (ExperimentalDecorators != Tristate::Unknown) w.memberTristate("experimentalDecorators", ExperimentalDecorators);
	if (ForceConsistentCasingInFileNames != Tristate::Unknown) w.memberTristate("forceConsistentCasingInFileNames", ForceConsistentCasingInFileNames);
	if (IsolatedModules != Tristate::Unknown) w.memberTristate("isolatedModules", IsolatedModules);
	if (IsolatedDeclarations != Tristate::Unknown) w.memberTristate("isolatedDeclarations", IsolatedDeclarations);
	if (IgnoreConfig != Tristate::Unknown) w.memberTristate("ignoreConfig", IgnoreConfig);
	if (!api::isZeroVal(IgnoreDeprecations)) w.member("ignoreDeprecations", IgnoreDeprecations);
	if (ImportHelpers != Tristate::Unknown) w.memberTristate("importHelpers", ImportHelpers);
	if (InlineSourceMap != Tristate::Unknown) w.memberTristate("inlineSourceMap", InlineSourceMap);
	if (InlineSources != Tristate::Unknown) w.memberTristate("inlineSources", InlineSources);
	if (Init != Tristate::Unknown) w.memberTristate("init", Init);
	if (Incremental != Tristate::Unknown) w.memberTristate("incremental", Incremental);
	if (!api::isZeroVal(Jsx)) w.member("jsx", Jsx);
	if (!api::isZeroVal(JsxFactory)) w.member("jsxFactory", JsxFactory);
	if (!api::isZeroVal(JsxFragmentFactory)) w.member("jsxFragmentFactory", JsxFragmentFactory);
	if (!api::isZeroVal(JsxImportSource)) w.member("jsxImportSource", JsxImportSource);
	if (!api::isZeroVal(Lib)) w.member("lib", Lib);
	if (LibReplacement != Tristate::Unknown) w.memberTristate("libReplacement", LibReplacement);
	if (!api::isZeroVal(Locale)) w.member("locale", Locale);
	if (!api::isZeroVal(MapRoot)) w.member("mapRoot", MapRoot);
	if (!api::isZeroVal(Module)) w.member("module", Module);
	if (!api::isZeroVal(ModuleResolution)) w.member("moduleResolution", ModuleResolution);
	if (!api::isZeroVal(ModuleSuffixes)) w.member("moduleSuffixes", ModuleSuffixes);
	if (!api::isZeroVal(ModuleDetection)) w.member("moduleDetection", ModuleDetection);
	if (!api::isZeroVal(NewLine)) w.member("newLine", NewLine);
	if (NoEmit != Tristate::Unknown) w.memberTristate("noEmit", NoEmit);
	if (NoCheck != Tristate::Unknown) w.memberTristate("noCheck", NoCheck);
	if (NoErrorTruncation != Tristate::Unknown) w.memberTristate("noErrorTruncation", NoErrorTruncation);
	if (NoFallthroughCasesInSwitch != Tristate::Unknown) w.memberTristate("noFallthroughCasesInSwitch", NoFallthroughCasesInSwitch);
	if (NoImplicitAny != Tristate::Unknown) w.memberTristate("noImplicitAny", NoImplicitAny);
	if (NoImplicitThis != Tristate::Unknown) w.memberTristate("noImplicitThis", NoImplicitThis);
	if (NoImplicitReturns != Tristate::Unknown) w.memberTristate("noImplicitReturns", NoImplicitReturns);
	if (NoEmitHelpers != Tristate::Unknown) w.memberTristate("noEmitHelpers", NoEmitHelpers);
	if (NoLib != Tristate::Unknown) w.memberTristate("noLib", NoLib);
	if (NoPropertyAccessFromIndexSignature != Tristate::Unknown) w.memberTristate("noPropertyAccessFromIndexSignature", NoPropertyAccessFromIndexSignature);
	if (NoUncheckedIndexedAccess != Tristate::Unknown) w.memberTristate("noUncheckedIndexedAccess", NoUncheckedIndexedAccess);
	if (NoEmitOnError != Tristate::Unknown) w.memberTristate("noEmitOnError", NoEmitOnError);
	if (NoUnusedLocals != Tristate::Unknown) w.memberTristate("noUnusedLocals", NoUnusedLocals);
	if (NoUnusedParameters != Tristate::Unknown) w.memberTristate("noUnusedParameters", NoUnusedParameters);
	if (NoResolve != Tristate::Unknown) w.memberTristate("noResolve", NoResolve);
	if (NoImplicitOverride != Tristate::Unknown) w.memberTristate("noImplicitOverride", NoImplicitOverride);
	if (NoUncheckedSideEffectImports != Tristate::Unknown) w.memberTristate("noUncheckedSideEffectImports", NoUncheckedSideEffectImports);
	if (!api::isZeroVal(OutDir)) w.member("outDir", OutDir);
	if (!Paths.empty()) w.memberPaths("paths", Paths);
	if (!api::isZeroVal(Plugins)) w.member("plugins", Plugins);
	if (PreserveConstEnums != Tristate::Unknown) w.memberTristate("preserveConstEnums", PreserveConstEnums);
	if (PreserveSymlinks != Tristate::Unknown) w.memberTristate("preserveSymlinks", PreserveSymlinks);
	if (!api::isZeroVal(Project)) w.member("project", Project);
	if (ResolveJsonModule != Tristate::Unknown) w.memberTristate("resolveJsonModule", ResolveJsonModule);
	if (ResolvePackageJsonExports != Tristate::Unknown) w.memberTristate("resolvePackageJsonExports", ResolvePackageJsonExports);
	if (ResolvePackageJsonImports != Tristate::Unknown) w.memberTristate("resolvePackageJsonImports", ResolvePackageJsonImports);
	if (RemoveComments != Tristate::Unknown) w.memberTristate("removeComments", RemoveComments);
	if (RewriteRelativeImportExtensions != Tristate::Unknown) w.memberTristate("rewriteRelativeImportExtensions", RewriteRelativeImportExtensions);
	if (!api::isZeroVal(ReactNamespace)) w.member("reactNamespace", ReactNamespace);
	if (!api::isZeroVal(RootDir)) w.member("rootDir", RootDir);
	if (!api::isZeroVal(RootDirs)) w.member("rootDirs", RootDirs);
	if (SkipLibCheck != Tristate::Unknown) w.memberTristate("skipLibCheck", SkipLibCheck);
	if (StableTypeOrdering != Tristate::Unknown) w.memberTristate("stableTypeOrdering", StableTypeOrdering);
	if (Strict != Tristate::Unknown) w.memberTristate("strict", Strict);
	if (StrictBindCallApply != Tristate::Unknown) w.memberTristate("strictBindCallApply", StrictBindCallApply);
	if (StrictBuiltinIteratorReturn != Tristate::Unknown) w.memberTristate("strictBuiltinIteratorReturn", StrictBuiltinIteratorReturn);
	if (StrictFunctionTypes != Tristate::Unknown) w.memberTristate("strictFunctionTypes", StrictFunctionTypes);
	if (StrictNullChecks != Tristate::Unknown) w.memberTristate("strictNullChecks", StrictNullChecks);
	if (StrictPropertyInitialization != Tristate::Unknown) w.memberTristate("strictPropertyInitialization", StrictPropertyInitialization);
	if (StripInternal != Tristate::Unknown) w.memberTristate("stripInternal", StripInternal);
	if (SkipDefaultLibCheck != Tristate::Unknown) w.memberTristate("skipDefaultLibCheck", SkipDefaultLibCheck);
	if (SourceMap != Tristate::Unknown) w.memberTristate("sourceMap", SourceMap);
	if (!api::isZeroVal(SourceRoot)) w.member("sourceRoot", SourceRoot);
	if (SuppressOutputPathCheck != Tristate::Unknown) w.memberTristate("suppressOutputPathCheck", SuppressOutputPathCheck);
	if (!api::isZeroVal(Target)) w.member("target", Target);
	if (TraceResolution != Tristate::Unknown) w.memberTristate("traceResolution", TraceResolution);
	if (!api::isZeroVal(TsBuildInfoFile)) w.member("tsBuildInfoFile", TsBuildInfoFile);
	if (!api::isZeroVal(TypeRoots)) w.member("typeRoots", TypeRoots);
	// Go omits only a nil slice: an explicitly-set empty `types` emits [].
	if (TypesWasSet || !api::isZeroVal(Types)) w.member("types", Types);
	if (UseDefineForClassFields != Tristate::Unknown) w.memberTristate("useDefineForClassFields", UseDefineForClassFields);
	if (UseUnknownInCatchVariables != Tristate::Unknown) w.memberTristate("useUnknownInCatchVariables", UseUnknownInCatchVariables);
	if (VerbatimModuleSyntax != Tristate::Unknown) w.memberTristate("verbatimModuleSyntax", VerbatimModuleSyntax);
	if (!api::isZeroVal(MaxNodeModuleJsDepth)) w.member("maxNodeModuleJsDepth", MaxNodeModuleJsDepth);
	if (AllowSyntheticDefaultImports != Tristate::Unknown) w.memberTristate("allowSyntheticDefaultImports", AllowSyntheticDefaultImports);
	if (AlwaysStrict != Tristate::Unknown) w.memberTristate("alwaysStrict", AlwaysStrict);
	if (!api::isZeroVal(BaseUrl)) w.member("baseUrl", BaseUrl);
	if (DownlevelIteration != Tristate::Unknown) w.memberTristate("downlevelIteration", DownlevelIteration);
	if (ESModuleInterop != Tristate::Unknown) w.memberTristate("esModuleInterop", ESModuleInterop);
	if (!api::isZeroVal(OutFile)) w.member("outFile", OutFile);
	if (!api::isZeroVal(ConfigFilePath)) w.member("configFilePath", ConfigFilePath);
	if (NoDtsResolution != Tristate::Unknown) w.memberTristate("noDtsResolution", NoDtsResolution);
	if (!api::isZeroVal(PathsBasePath)) w.member("pathsBasePath", PathsBasePath);
	if (Diagnostics != Tristate::Unknown) w.memberTristate("diagnostics", Diagnostics);
	if (ExtendedDiagnostics != Tristate::Unknown) w.memberTristate("extendedDiagnostics", ExtendedDiagnostics);
	if (!api::isZeroVal(GenerateCpuProfile)) w.member("generateCpuProfile", GenerateCpuProfile);
	if (!api::isZeroVal(GenerateTrace)) w.member("generateTrace", GenerateTrace);
	if (ListEmittedFiles != Tristate::Unknown) w.memberTristate("listEmittedFiles", ListEmittedFiles);
	if (ListFiles != Tristate::Unknown) w.memberTristate("listFiles", ListFiles);
	if (ExplainFiles != Tristate::Unknown) w.memberTristate("explainFiles", ExplainFiles);
	if (ListFilesOnly != Tristate::Unknown) w.memberTristate("listFilesOnly", ListFilesOnly);
	if (NoEmitForJsFiles != Tristate::Unknown) w.memberTristate("noEmitForJsFiles", NoEmitForJsFiles);
	if (PreserveWatchOutput != Tristate::Unknown) w.memberTristate("preserveWatchOutput", PreserveWatchOutput);
	if (Pretty != Tristate::Unknown) w.memberTristate("pretty", Pretty);
	if (Version != Tristate::Unknown) w.memberTristate("version", Version);
	if (Watch != Tristate::Unknown) w.memberTristate("watch", Watch);
	if (ShowConfig != Tristate::Unknown) w.memberTristate("showConfig", ShowConfig);
	if (Build != Tristate::Unknown) w.memberTristate("build", Build);
	if (Help != Tristate::Unknown) w.memberTristate("help", Help);
	if (All != Tristate::Unknown) w.memberTristate("all", All);
	if (RunExternalCode != Tristate::Unknown) w.memberTristate("runExternalCode", RunExternalCode);
	if (!api::isZeroVal(PprofDir)) w.member("pprofDir", PprofDir);
	if (SingleThreaded != Tristate::Unknown) w.memberTristate("singleThreaded", SingleThreaded);
	if (Quiet != Tristate::Unknown) w.memberTristate("quiet", Quiet);
	if (!api::isZeroVal(Checkers)) w.member("checkers", Checkers);
	return w.end();
}
}  // namespace tsc

namespace tsc::api {

// DocumentIdentifier::unmarshalJSONFrom — proto.go:299. A plain string is a
// file name; an object carries `uri`.
std::pair<bool, std::string> DocumentIdentifier::unmarshalField(std::string_view, json::Decoder&) {
	return {false, {}};
}

std::string DocumentIdentifier::unmarshalJSONFrom(json::Decoder& dec) {
	auto [tok, err] = dec.readToken();
	if (!err.empty()) return err;
	switch (tok.kind()) {
	case '"':
		FileName = tok.string();
		return {};
	case '{': {
		// Read the object fields
		while (dec.peekKind() != '}') {
			auto [key, e] = dec.readToken();
			if (!e.empty()) return e;
			bool isURI = key.string() == "uri";
			auto [val, e2] = dec.readToken();
			if (!e2.empty()) return e2;
			if (isURI) URI = lsproto::DocumentUri(val.string());
		}
		// Consume the closing brace
		if (auto [t, e] = dec.readToken(); !e.empty()) return e;
		return {};
	}
	default:
		return std::string("DocumentIdentifier: expected string or object, got ") +
		       json::kindString(tok.kind());
	}
}

// DocumentIdentifier::marshalJSONTo — proto.go:284 field tags
// (`fileName,omitempty`, `uri,omitempty`); Go has no custom marshaler, so
// the wire form is the tagged object.
std::string DocumentIdentifier::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (!FileName.empty()) w.member("fileName", FileName);
	if (!URI.empty()) w.member("uri", URI);
	return w.end();
}

// EnsurePrograms::unmarshalJSONFrom — proto.go:399. `true` or an array of
// project IDs.
std::pair<bool, std::string> EnsurePrograms::unmarshalField(std::string_view, json::Decoder&) {
	return {false, {}};
}

// EnsurePrograms::marshalJSONTo — proto.go:398 has no custom marshaler, so
// Go emits the default field names `All` and `Projects`.
std::string EnsurePrograms::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("All", All);
	w.member("Projects", Projects);
	return w.end();
}

std::string EnsurePrograms::unmarshalJSONFrom(json::Decoder& dec) {
	auto [value, err] = dec.readValue();
	if (!err.empty()) return err;
	if (value == "true") {
		All = true;
		return {};
	}
	if (value.empty() || value[0] != '[') {
		return "ensurePrograms must be true or an array of project IDs";
	}
	return json::unmarshal(value, &Projects);
}

// unmarshalField — FileNotifications.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> FileNotifications::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "invalidateAll")) return {true, json::unmarshalDecode(d, &InvalidateAll)};
	if (fieldIs(n, "changed")) return {true, json::unmarshalDecode(d, &Changed)};
	if (fieldIs(n, "created")) return {true, json::unmarshalDecode(d, &Created)};
	if (fieldIs(n, "deleted")) return {true, json::unmarshalDecode(d, &Deleted)};
	return {false, {}};
}

std::string FileNotifications::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — SnapshotRequestChangesParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> SnapshotRequestChangesParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "openProjects")) return {true, json::unmarshalDecode(d, &OpenProjects)};
	if (fieldIs(n, "closeProjects")) return {true, json::unmarshalDecode(d, &CloseProjects)};
	if (fieldIs(n, "openFiles")) return {true, json::unmarshalDecode(d, &OpenFiles)};
	if (fieldIs(n, "closeFiles")) return {true, json::unmarshalDecode(d, &CloseFiles)};
	if (fieldIs(n, "createPrograms")) return {true, json::unmarshalDecode(d, &CreatePrograms)};
	if (fieldIs(n, "reconfigurePrograms")) return {true, json::unmarshalDecode(d, &ReconfigurePrograms)};
	if (fieldIs(n, "removePrograms")) return {true, json::unmarshalDecode(d, &RemovePrograms)};
	if (fieldIs(n, "ensurePrograms")) return {true, json::unmarshalDecode(d, &EnsurePrograms)};
	return {false, {}};
}

std::string SnapshotRequestChangesParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CreateSnapshotParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CreateSnapshotParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "userPreferences")) return {true, json::unmarshalDecode(d, &UserPreferences)};
	if (fieldIs(n, "prepareAutoImports")) return {true, json::unmarshalDecode(d, &PrepareAutoImports)};
	if (fieldIs(n, "fileNotifications")) return {true, json::unmarshalDecode(d, &FileNotifications)};
	if (fieldIs(n, "fileSystem")) return {true, json::unmarshalDecode(d, &FileSystem)};
	return SnapshotRequestChangesParams::unmarshalField(n, d);
	return {false, {}};
}

std::string CreateSnapshotParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CreateSnapshotProgramParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CreateSnapshotProgramParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "rootFiles")) return {true, json::unmarshalDecode(d, &RootFiles)};
	if (fieldIs(n, "compilerOptions")) return {true, json::unmarshalDecode(d, &CompilerOptions)};
	if (fieldIs(n, "options")) return {true, json::unmarshalDecode(d, &Options)};
	return {false, {}};
}

std::string CreateSnapshotProgramParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ReconfigureSnapshotProgramParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ReconfigureSnapshotProgramParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "id")) return {true, json::unmarshalDecode(d, &Id)};
	if (fieldIs(n, "rootFiles")) return {true, json::unmarshalDecode(d, &RootFiles)};
	if (fieldIs(n, "compilerOptions")) return {true, json::unmarshalDecode(d, &CompilerOptions)};
	if (fieldIs(n, "options")) return {true, json::unmarshalDecode(d, &Options)};
	return {false, {}};
}

std::string ReconfigureSnapshotProgramParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — UpdateSnapshotParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> UpdateSnapshotParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "changes")) return {true, json::unmarshalDecode(d, &Changes)};
	return {false, {}};
}

std::string UpdateSnapshotParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetCurrentLanguageServerSnapshotParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetCurrentLanguageServerSnapshotParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "baseSnapshot")) return {true, json::unmarshalDecode(d, &BaseSnapshot)};
	if (fieldIs(n, "changes")) return {true, json::unmarshalDecode(d, &Changes)};
	return {false, {}};
}

std::string GetCurrentLanguageServerSnapshotParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — LanguageServerSnapshotChanges.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> LanguageServerSnapshotChanges::unmarshalField(std::string_view n, json::Decoder& d) {
	return SnapshotRequestChangesParams::unmarshalField(n, d);
	return {false, {}};
}

std::string LanguageServerSnapshotChanges::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CreateProgramOptions.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CreateProgramOptions::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "projectReferences")) return {true, json::unmarshalDecode(d, &ProjectReferences)};
	if (fieldIs(n, "configFileParsingDiagnostics")) return {true, json::unmarshalDecode(d, &ConfigFileParsingDiagnostics)};
	if (fieldIs(n, "moduleResolver")) return {true, json::unmarshalDecode(d, &ModuleResolver)};
	return {false, {}};
}

std::string CreateProgramOptions::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ModuleResolutionSpec.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ModuleResolutionSpec::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "fallback")) return {true, json::unmarshalDecode(d, &Fallback)};
	if (fieldIs(n, "entries")) return {true, json::unmarshalDecode(d, &Entries)};
	return {false, {}};
}

std::string ModuleResolutionSpec::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ModuleResolutionEntry.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ModuleResolutionEntry::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "moduleName")) return {true, json::unmarshalDecode(d, &ModuleName)};
	if (fieldIs(n, "containingDirectory")) return {true, json::unmarshalDecode(d, &ContainingDirectory)};
	if (fieldIs(n, "resolutionMode")) return {true, json::unmarshalDecode(d, &ResolutionMode)};
	if (fieldIs(n, "result")) return {true, json::unmarshalDecode(d, &Result)};
	return {false, {}};
}

std::string ModuleResolutionEntry::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — StaticModuleResolution.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> StaticModuleResolution::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "resolvedFileName")) return {true, json::unmarshalDecode(d, &ResolvedFileName)};
	if (fieldIs(n, "originalPath")) return {true, json::unmarshalDecode(d, &OriginalPath)};
	if (fieldIs(n, "packageId")) return {true, json::unmarshalDecode(d, &PackageID)};
	return {false, {}};
}

std::string StaticModuleResolution::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CreateModuleResolverParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CreateModuleResolverParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "compilerOptions")) return {true, json::unmarshalDecode(d, &CompilerOptions)};
	if (fieldIs(n, "moduleResolutions")) return {true, json::unmarshalDecode(d, &ModuleResolutions)};
	if (fieldIs(n, "resolveModuleNameCallback")) return {true, json::unmarshalDecode(d, &ResolveModuleNameCallback)};
	return {false, {}};
}

std::string CreateModuleResolverParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ReleaseModuleResolverParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ReleaseModuleResolverParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "resolver")) return {true, json::unmarshalDecode(d, &Resolver)};
	return {false, {}};
}

std::string ReleaseModuleResolverParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ResolveModuleNameParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ResolveModuleNameParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "inProgressSnapshot")) return {true, json::unmarshalDecode(d, &InProgressSnapshot)};
	if (fieldIs(n, "resolver")) return {true, json::unmarshalDecode(d, &Resolver)};
	if (fieldIs(n, "moduleName")) return {true, json::unmarshalDecode(d, &ModuleName)};
	if (fieldIs(n, "containingDirectory")) return {true, json::unmarshalDecode(d, &ContainingDirectory)};
	if (fieldIs(n, "resolutionMode")) return {true, json::unmarshalDecode(d, &ResolutionMode)};
	return {false, {}};
}

std::string ResolveModuleNameParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ResolveModuleNameCallbackParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ResolveModuleNameCallbackParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "moduleName")) return {true, json::unmarshalDecode(d, &ModuleName)};
	if (fieldIs(n, "containingDirectory")) return {true, json::unmarshalDecode(d, &ContainingDirectory)};
	if (fieldIs(n, "resolutionMode")) return {true, json::unmarshalDecode(d, &ResolutionMode)};
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "inProgressSnapshot")) return {true, json::unmarshalDecode(d, &InProgressSnapshot)};
	return {false, {}};
}

std::string ResolveModuleNameCallbackParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ParseConfigFileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ParseConfigFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	return {false, {}};
}

std::string ParseConfigFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ParseCommandLineParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ParseCommandLineParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "commandLine")) return {true, json::unmarshalDecode(d, &CommandLine)};
	return {false, {}};
}

std::string ParseCommandLineParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ReadConfigFileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ReadConfigFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	return {false, {}};
}

std::string ReadConfigFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ParseJsonConfigFileContentParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ParseJsonConfigFileContentParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "json")) return {true, json::unmarshalDecode(d, &JSON)};
	if (fieldIs(n, "configDirectory")) return {true, json::unmarshalDecode(d, &ConfigDirectory)};
	if (fieldIs(n, "configFileName")) return {true, json::unmarshalDecode(d, &ConfigFileName)};
	return {false, {}};
}

std::string ParseJsonConfigFileContentParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — TranspileOptions.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> TranspileOptions::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "compilerOptions")) return {true, json::unmarshalDecode(d, &CompilerOptions)};
	if (fieldIs(n, "fileName")) return {true, json::unmarshalDecode(d, &FileName)};
	if (fieldIs(n, "reportDiagnostics")) return {true, json::unmarshalDecode(d, &ReportDiagnostics)};
	return {false, {}};
}

std::string TranspileOptions::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CreateSourceFileOptions.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CreateSourceFileOptions::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "scriptKind")) return {true, json::unmarshalDecode(d, &ScriptKind)};
	return {false, {}};
}

std::string CreateSourceFileOptions::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CreateSourceFileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CreateSourceFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "fileName")) return {true, json::unmarshalDecode(d, &FileName)};
	if (fieldIs(n, "sourceText")) return {true, json::unmarshalDecode(d, &SourceText)};
	if (fieldIs(n, "options")) return {true, json::unmarshalDecode(d, &Options)};
	return {false, {}};
}

std::string CreateSourceFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CreateSourceFileFromFileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CreateSourceFileFromFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "fileName")) return {true, json::unmarshalDecode(d, &FileName)};
	if (fieldIs(n, "options")) return {true, json::unmarshalDecode(d, &Options)};
	return {false, {}};
}

std::string CreateSourceFileFromFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — TranspileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> TranspileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "input")) return {true, json::unmarshalDecode(d, &Input)};
	if (fieldIs(n, "options")) return {true, json::unmarshalDecode(d, &Options)};
	return {false, {}};
}

std::string TranspileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — TranspileFromFileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> TranspileFromFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "fileName")) return {true, json::unmarshalDecode(d, &FileName)};
	if (fieldIs(n, "options")) return {true, json::unmarshalDecode(d, &Options)};
	return {false, {}};
}

std::string TranspileFromFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — BatchRequestsParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> BatchRequestsParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "requests")) return {true, json::unmarshalDecode(d, &Requests)};
	if (fieldIs(n, "continuationToken")) return {true, json::unmarshalDecode(d, &ContinuationToken)};
	if (fieldIs(n, "maxResponseBytesPerPage")) return {true, json::unmarshalDecode(d, &MaxResponseBytesPerPage)};
	return {false, {}};
}

std::string BatchRequestsParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — BatchRequest.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> BatchRequest::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "method")) return {true, json::unmarshalDecode(d, &Method)};
	if (fieldIs(n, "params")) return {true, json::unmarshalDecode(d, &Params)};
	return {false, {}};
}

std::string BatchRequest::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ReleaseParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ReleaseParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	return {false, {}};
}

std::string ReleaseParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ReleaseSourceFileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ReleaseSourceFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "lease")) return {true, json::unmarshalDecode(d, &Lease)};
	return {false, {}};
}

std::string ReleaseSourceFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — SourceFileDescriptor member decode.
std::pair<bool, std::string> SourceFileDescriptor::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "fileName")) return {true, json::unmarshalDecode(d, &FileName)};
	if (fieldIs(n, "path")) return {true, json::unmarshalDecode(d, &Path)};
	if (fieldIs(n, "contentHash")) return {true, json::unmarshalDecode(d, &ContentHash)};
	if (fieldIs(n, "parseOptionsKey")) return {true, json::unmarshalDecode(d, &ParseOptionsKey)};
	if (fieldIs(n, "scriptKind")) return {true, json::unmarshalDecode(d, &ScriptKind)};
	if (fieldIs(n, "nodeId")) return {true, json::unmarshalDecode(d, &NodeID)};
	return {false, {}};
}

std::string SourceFileDescriptor::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

std::string SourceFileDescriptor::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("fileName", FileName);
	w.member("path", Path);
	w.member("contentHash", ContentHash);
	w.member("parseOptionsKey", ParseOptionsKey);
	w.member("scriptKind", ScriptKind);
	w.member("nodeId", NodeID);
	return w.end();
}

// unmarshalField — RetainSourceFileParams member decode.
std::pair<bool, std::string> RetainSourceFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	return {false, {}};
}

std::string RetainSourceFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

std::string RetainSourceFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("file", File);
	return w.end();
}

std::string RetainSourceFileResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("lease", Lease);
	return w.end();
}

// unmarshalField — GetCachedSourceFileParams member decode.
std::pair<bool, std::string> GetCachedSourceFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	return {false, {}};
}

std::string GetCachedSourceFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

std::string GetCachedSourceFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("file", File);
	return w.end();
}

// unmarshalField — GetSymbolOfDeclarationParams member decode.
std::pair<bool, std::string> GetSymbolOfDeclarationParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "index")) return {true, json::unmarshalDecode(d, &Index)};
	return {false, {}};
}

std::string GetSymbolOfDeclarationParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

std::string GetSymbolOfDeclarationParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("file", File);
	w.member("index", Index);
	return w.end();
}

// unmarshalField — SymbolReference member decode.
std::pair<bool, std::string> SymbolReference::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "kind")) return {true, json::unmarshalDecode(d, &Kind)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "id")) return {true, json::unmarshalDecode(d, &Id)};
	return {false, {}};
}

std::string SymbolReference::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

std::string SymbolReference::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("kind", Kind);
	if (File) w.member("file", *File);
	if (!api::isZeroVal(Snapshot)) w.member("snapshot", Snapshot);
	if (!Project.empty()) w.member("project", Project);
	w.member("id", Id);
	return w.end();
}

std::string CompactSymbolReference::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("id", Id);
	if (!File.empty()) w.member("file", File);
	return w.end();
}

// unmarshalField — ProfileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ProfileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "dir")) return {true, json::unmarshalDecode(d, &Dir)};
	return {false, {}};
}

std::string ProfileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CreateBuildOrchestratorParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CreateBuildOrchestratorParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "rootNames")) return {true, json::unmarshalDecode(d, &RootNames)};
	if (fieldIs(n, "cwd")) return {true, json::unmarshalDecode(d, &Cwd)};
	// The embedded *BuildOptions/*CompilerOptions carry explicit json tags, so
	// encoding/json treats them as nested object members (no promotion).
	if (fieldIs(n, "buildOptions")) return {true, json::unmarshalDecode(d, &BuildOptions)};
	if (fieldIs(n, "compilerOptions")) return {true, json::unmarshalDecode(d, &CompilerOptions)};
	return {false, {}};
}

std::string CreateBuildOrchestratorParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — DisposeBuildOrchestratorParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> DisposeBuildOrchestratorParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "buildOrchestratorID")) return {true, json::unmarshalDecode(d, &BuildOrchestratorID)};
	return {false, {}};
}

std::string DisposeBuildOrchestratorParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — BuildParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> BuildParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "buildOrchestratorID")) return {true, json::unmarshalDecode(d, &BuildOrchestratorID)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	return {false, {}};
}

std::string BuildParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CleanBuildParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CleanBuildParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "buildOrchestratorID")) return {true, json::unmarshalDecode(d, &BuildOrchestratorID)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	return {false, {}};
}

std::string CleanBuildParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetDefaultProjectForFileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetDefaultProjectForFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	return {false, {}};
}

std::string GetDefaultProjectForFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSymbolAtPositionParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSymbolAtPositionParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "position")) return {true, json::unmarshalDecode(d, &Position)};
	return {false, {}};
}

std::string GetSymbolAtPositionParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSymbolsAtPositionsParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSymbolsAtPositionsParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "positions")) return {true, json::unmarshalDecode(d, &Positions)};
	return {false, {}};
}

std::string GetSymbolsAtPositionsParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSymbolOfSourceFileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSymbolOfSourceFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	return {false, {}};
}

std::string GetSymbolOfSourceFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSymbolsOfSourceFilesParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSymbolsOfSourceFilesParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "files")) return {true, json::unmarshalDecode(d, &Files)};
	return {false, {}};
}

std::string GetSymbolsOfSourceFilesParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSymbolAtLocationParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSymbolAtLocationParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	return {false, {}};
}

std::string GetSymbolAtLocationParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSymbolsAtLocationsParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSymbolsAtLocationsParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "locations")) return {true, json::unmarshalDecode(d, &Locations)};
	return {false, {}};
}

std::string GetSymbolsAtLocationsParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetTypeOfSymbolParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetTypeOfSymbolParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "symbol")) return {true, json::unmarshalDecode(d, &Symbol)};
	return {false, {}};
}

std::string GetTypeOfSymbolParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetTypesOfSymbolsParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetTypesOfSymbolsParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "symbols")) return {true, json::unmarshalDecode(d, &Symbols)};
	return {false, {}};
}

std::string GetTypesOfSymbolsParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSourceFileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSourceFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	return {false, {}};
}

std::string GetSourceFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSourceFileNamesParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSourceFileNamesParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	return {false, {}};
}

std::string GetSourceFileNamesParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetModeForUsageLocationParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetModeForUsageLocationParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "usage")) return {true, json::unmarshalDecode(d, &Usage)};
	return {false, {}};
}

std::string GetModeForUsageLocationParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetModeForResolutionAtIndexParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetModeForResolutionAtIndexParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "index")) return {true, json::unmarshalDecode(d, &Index)};
	return {false, {}};
}

std::string GetModeForResolutionAtIndexParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetResolvedModuleParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetResolvedModuleParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "moduleName")) return {true, json::unmarshalDecode(d, &ModuleName)};
	if (fieldIs(n, "mode")) return {true, json::unmarshalDecode(d, &Mode)};
	return {false, {}};
}

std::string GetResolvedModuleParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetResolvedModuleFromModuleSpecifierParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetResolvedModuleFromModuleSpecifierParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "moduleSpecifier")) return {true, json::unmarshalDecode(d, &ModuleSpecifier)};
	if (fieldIs(n, "sourceFile")) return {true, json::unmarshalDecode(d, &SourceFile)};
	return {false, {}};
}

std::string GetResolvedModuleFromModuleSpecifierParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetResolvedTypeReferenceDirectiveParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetResolvedTypeReferenceDirectiveParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "typeDirectiveName")) return {true, json::unmarshalDecode(d, &TypeDirectiveName)};
	if (fieldIs(n, "mode")) return {true, json::unmarshalDecode(d, &Mode)};
	return {false, {}};
}

std::string GetResolvedTypeReferenceDirectiveParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetResolvedTypeReferenceDirectiveFromReferenceParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetResolvedTypeReferenceDirectiveFromReferenceParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "sourceFile")) return {true, json::unmarshalDecode(d, &SourceFile)};
	if (fieldIs(n, "typeDirectiveName")) return {true, json::unmarshalDecode(d, &TypeDirectiveName)};
	if (fieldIs(n, "resolutionMode")) return {true, json::unmarshalDecode(d, &ResolutionMode)};
	return {false, {}};
}

std::string GetResolvedTypeReferenceDirectiveFromReferenceParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — PackageId.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> PackageId::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "name")) return {true, json::unmarshalDecode(d, &Name)};
	if (fieldIs(n, "subModuleName")) return {true, json::unmarshalDecode(d, &SubModuleName)};
	if (fieldIs(n, "version")) return {true, json::unmarshalDecode(d, &Version)};
	if (fieldIs(n, "peerDependencies")) return {true, json::unmarshalDecode(d, &PeerDependencies)};
	return {false, {}};
}

std::string PackageId::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ResolveNameParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ResolveNameParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "name")) return {true, json::unmarshalDecode(d, &Name)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "position")) return {true, json::unmarshalDecode(d, &Position)};
	if (fieldIs(n, "meaning")) return {true, json::unmarshalDecode(d, &Meaning)};
	if (fieldIs(n, "excludeGlobals")) return {true, json::unmarshalDecode(d, &ExcludeGlobals)};
	return {false, {}};
}

std::string ResolveNameParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSymbolsInScopeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSymbolsInScopeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "position")) return {true, json::unmarshalDecode(d, &Position)};
	if (fieldIs(n, "meaning")) return {true, json::unmarshalDecode(d, &Meaning)};
	return {false, {}};
}

std::string GetSymbolsInScopeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetTypePropertyParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetTypePropertyParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "objectId")) return {true, json::unmarshalDecode(d, &Type)};
	return {false, {}};
}

std::string GetTypePropertyParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSymbolPropertyParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSymbolPropertyParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "symbol")) return {true, json::unmarshalDecode(d, &Symbol)};
	return {false, {}};
}

std::string GetSymbolPropertyParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSignaturePropertyParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSignaturePropertyParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "objectId")) return {true, json::unmarshalDecode(d, &Signature)};
	return {false, {}};
}

std::string GetSignaturePropertyParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetContextualTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetContextualTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	return {false, {}};
}

std::string GetContextualTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetContextualTypeForArgumentParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetContextualTypeForArgumentParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	if (fieldIs(n, "index")) return {true, json::unmarshalDecode(d, &Index)};
	return {false, {}};
}

std::string GetContextualTypeForArgumentParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetTypeOfSymbolAtLocationParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetTypeOfSymbolAtLocationParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "symbol")) return {true, json::unmarshalDecode(d, &Symbol)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	return {false, {}};
}

std::string GetTypeOfSymbolAtLocationParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetReferencesToSymbolInFileParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetReferencesToSymbolInFileParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "symbol")) return {true, json::unmarshalDecode(d, &Symbol)};
	return {false, {}};
}

std::string GetReferencesToSymbolInFileParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetReferencedSymbolsForNodeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetReferencedSymbolsForNodeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "node")) return {true, json::unmarshalDecode(d, &Node)};
	if (fieldIs(n, "position")) return {true, json::unmarshalDecode(d, &Position)};
	return {false, {}};
}

std::string GetReferencedSymbolsForNodeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSignatureUsagesParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSignatureUsagesParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "signatureDecl")) return {true, json::unmarshalDecode(d, &SignatureDecl)};
	return {false, {}};
}

std::string GetSignatureUsagesParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetCompletionsAtPositionParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetCompletionsAtPositionParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "position")) return {true, json::unmarshalDecode(d, &Position)};
	if (fieldIs(n, "triggerCharacter")) return {true, json::unmarshalDecode(d, &TriggerCharacter)};
	if (fieldIs(n, "includeSymbol")) return {true, json::unmarshalDecode(d, &IncludeSymbol)};
	return {false, {}};
}

std::string GetCompletionsAtPositionParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetIntrinsicTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetIntrinsicTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	return {false, {}};
}

std::string GetIntrinsicTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetBaseTypeOfLiteralTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetBaseTypeOfLiteralTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "type")) return {true, json::unmarshalDecode(d, &Type)};
	return {false, {}};
}

std::string GetBaseTypeOfLiteralTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetNonNullableTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetNonNullableTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "type")) return {true, json::unmarshalDecode(d, &Type)};
	return {false, {}};
}

std::string GetNonNullableTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetTypeFromTypeNodeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetTypeFromTypeNodeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	return {false, {}};
}

std::string GetTypeFromTypeNodeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetWidenedTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetWidenedTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "type")) return {true, json::unmarshalDecode(d, &Type)};
	return {false, {}};
}

std::string GetWidenedTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetParameterTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetParameterTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "signature")) return {true, json::unmarshalDecode(d, &Signature)};
	if (fieldIs(n, "index")) return {true, json::unmarshalDecode(d, &Index)};
	return {false, {}};
}

std::string GetParameterTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — IsArrayLikeTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> IsArrayLikeTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "type")) return {true, json::unmarshalDecode(d, &Type)};
	return {false, {}};
}

std::string IsArrayLikeTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — IsTypeAssignableToParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> IsTypeAssignableToParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "source")) return {true, json::unmarshalDecode(d, &Source)};
	if (fieldIs(n, "target")) return {true, json::unmarshalDecode(d, &Target)};
	return {false, {}};
}

std::string IsTypeAssignableToParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetSignaturesOfTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetSignaturesOfTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "type")) return {true, json::unmarshalDecode(d, &Type)};
	if (fieldIs(n, "kind")) return {true, json::unmarshalDecode(d, &Kind)};
	return {false, {}};
}

std::string GetSignaturesOfTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetResolvedSignatureParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetResolvedSignatureParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	return {false, {}};
}

std::string GetResolvedSignatureParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetTypeAtLocationParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetTypeAtLocationParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	return {false, {}};
}

std::string GetTypeAtLocationParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetTypeAtLocationsParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetTypeAtLocationsParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "locations")) return {true, json::unmarshalDecode(d, &Locations)};
	return {false, {}};
}

std::string GetTypeAtLocationsParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetTypeAtPositionParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetTypeAtPositionParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "position")) return {true, json::unmarshalDecode(d, &Position)};
	return {false, {}};
}

std::string GetTypeAtPositionParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetTypesAtPositionsParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetTypesAtPositionsParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "positions")) return {true, json::unmarshalDecode(d, &Positions)};
	return {false, {}};
}

std::string GetTypesAtPositionsParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — ImportAdderAction.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> ImportAdderAction::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "kind")) return {true, json::unmarshalDecode(d, &Kind)};
	if (fieldIs(n, "symbol")) return {true, json::unmarshalDecode(d, &Symbol)};
	if (fieldIs(n, "isValidTypeOnlyUseSite")) return {true, json::unmarshalDecode(d, &IsValidTypeOnlyUseSite)};
	return {false, {}};
}

std::string ImportAdderAction::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetImportAdderEditsParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetImportAdderEditsParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "actions")) return {true, json::unmarshalDecode(d, &Actions)};
	return {false, {}};
}

std::string GetImportAdderEditsParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — TypeToTypeNodeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> TypeToTypeNodeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "type")) return {true, json::unmarshalDecode(d, &Type)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	if (fieldIs(n, "flags")) return {true, json::unmarshalDecode(d, &Flags)};
	return {false, {}};
}

std::string TypeToTypeNodeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — SignatureToSignatureDeclarationParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> SignatureToSignatureDeclarationParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "signature")) return {true, json::unmarshalDecode(d, &Signature)};
	if (fieldIs(n, "kind")) return {true, json::unmarshalDecode(d, &Kind)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	if (fieldIs(n, "flags")) return {true, json::unmarshalDecode(d, &Flags)};
	return {false, {}};
}

std::string SignatureToSignatureDeclarationParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — PrintNodeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> PrintNodeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "data")) return {true, json::unmarshalDecode(d, &Data)};
	if (fieldIs(n, "preserveSourceNewlines")) return {true, json::unmarshalDecode(d, &PreserveSourceNewlines)};
	if (fieldIs(n, "neverAsciiEscape")) return {true, json::unmarshalDecode(d, &NeverAsciiEscape)};
	if (fieldIs(n, "terminateUnterminatedLiterals")) return {true, json::unmarshalDecode(d, &TerminateUnterminatedLiterals)};
	return {false, {}};
}

std::string PrintNodeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — EmitParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> EmitParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "emitOnly")) return {true, json::unmarshalDecode(d, &EmitOnly)};
	return {false, {}};
}

std::string EmitParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — SelectedFilesEmitParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> SelectedFilesEmitParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "files")) return {true, json::unmarshalDecode(d, &Files)};
	return {false, {}};
}

std::string SelectedFilesEmitParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — FormatNodeForInsertionParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> FormatNodeForInsertionParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "file")) return {true, json::unmarshalDecode(d, &File)};
	if (fieldIs(n, "position")) return {true, json::unmarshalDecode(d, &Position)};
	if (fieldIs(n, "data")) return {true, json::unmarshalDecode(d, &Data)};
	return {false, {}};
}

std::string FormatNodeForInsertionParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CheckerTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CheckerTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "type")) return {true, json::unmarshalDecode(d, &Type)};
	return {false, {}};
}

std::string CheckerTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetPropertyOfTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetPropertyOfTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "type")) return {true, json::unmarshalDecode(d, &Type)};
	if (fieldIs(n, "name")) return {true, json::unmarshalDecode(d, &Name)};
	return {false, {}};
}

std::string GetPropertyOfTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetIndexInfoOfTypeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetIndexInfoOfTypeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "type")) return {true, json::unmarshalDecode(d, &Type)};
	if (fieldIs(n, "kind")) return {true, json::unmarshalDecode(d, &Kind)};
	return {false, {}};
}

std::string GetIndexInfoOfTypeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetMemberInModuleExportsParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetMemberInModuleExportsParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "symbol")) return {true, json::unmarshalDecode(d, &Symbol)};
	if (fieldIs(n, "name")) return {true, json::unmarshalDecode(d, &Name)};
	return {false, {}};
}

std::string GetMemberInModuleExportsParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CheckerNodeParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CheckerNodeParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "location")) return {true, json::unmarshalDecode(d, &Location)};
	return {false, {}};
}

std::string CheckerNodeParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CheckerSymbolParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CheckerSymbolParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "symbol")) return {true, json::unmarshalDecode(d, &Symbol)};
	return {false, {}};
}

std::string CheckerSymbolParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — CheckerSignatureParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> CheckerSignatureParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "signature")) return {true, json::unmarshalDecode(d, &Signature)};
	return {false, {}};
}

std::string CheckerSignatureParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetDiagnosticsParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetDiagnosticsParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	if (fieldIs(n, "files")) return {true, json::unmarshalDecode(d, &Files)};
	return {false, {}};
}

std::string GetDiagnosticsParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — GetProjectDiagnosticsParams.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> GetProjectDiagnosticsParams::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "snapshot")) return {true, json::unmarshalDecode(d, &Snapshot)};
	if (fieldIs(n, "project")) return {true, json::unmarshalDecode(d, &Project)};
	return {false, {}};
}

std::string GetProjectDiagnosticsParams::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — DiagnosticPositionResponse.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> DiagnosticPositionResponse::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "line")) return {true, json::unmarshalDecode(d, &Line)};
	if (fieldIs(n, "character")) return {true, json::unmarshalDecode(d, &Character)};
	return {false, {}};
}

std::string DiagnosticPositionResponse::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// unmarshalField — DiagnosticSourceLineResponse.go member decode; returns {false, ""} for unmatched names.
std::pair<bool, std::string> DiagnosticSourceLineResponse::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "line")) return {true, json::unmarshalDecode(d, &Line)};
	if (fieldIs(n, "text")) return {true, json::unmarshalDecode(d, &Text)};
	return {false, {}};
}

std::string DiagnosticSourceLineResponse::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}

// === marshalJSONTo bodies — proto.go response/result structs ===

// BatchRequestsResponse::marshalJSONTo — proto.go:841. Writes already-encoded
// responses verbatim when populated (batch pagination), else encodes Responses.
std::string BatchRequestsResponse::marshalJSONTo(json::Encoder& enc) const {
	if (auto e = enc.writeToken(json::BeginObject); !e.empty()) return e;
	if (auto e = enc.writeValue(json::marshalString("responses")); !e.empty()) return e;
	if (auto e = enc.writeToken(json::BeginArray); !e.empty()) return e;
	if (!encodedResponses.empty()) {
		for (auto& response : encodedResponses) {
			if (auto e = enc.writeValue(response); !e.empty()) return e;
		}
	} else {
		for (size_t i = 0; i < Responses.size(); i++) {
			if (auto e = json::marshalEncode(enc, &Responses[i]); !e.empty()) return e;
		}
	}
	if (auto e = enc.writeToken(json::EndArray); !e.empty()) return e;
	if (!ContinuationToken.empty()) {
		if (auto e = enc.writeValue(json::marshalString("continuationToken")); !e.empty()) return e;
		if (auto e = json::marshalEncode(enc, ContinuationToken); !e.empty()) return e;
	}
	return enc.writeToken(json::EndObject);
}

// DiagnosticResponse — proto.go:1890. Marshals all fields (Pos/End/Code/Category
// always); unmarshal is a flat tagged-field loop (used for round-trip tests and
// inbound related-information decoding).
std::pair<bool, std::string> DiagnosticResponse::unmarshalField(std::string_view n, json::Decoder& d) {
	if (fieldIs(n, "fileName")) return {true, json::unmarshalDecode(d, &FileName)};
	if (fieldIs(n, "pos")) return {true, json::unmarshalDecode(d, &Pos)};
	if (fieldIs(n, "end")) return {true, json::unmarshalDecode(d, &End)};
	if (fieldIs(n, "startPosition")) return {true, json::unmarshalDecode(d, &StartPosition)};
	if (fieldIs(n, "endPosition")) return {true, json::unmarshalDecode(d, &EndPosition)};
	if (fieldIs(n, "sourceLines")) return {true, json::unmarshalDecode(d, &SourceLines)};
	if (fieldIs(n, "code")) return {true, json::unmarshalDecode(d, &Code)};
	if (fieldIs(n, "category")) return {true, json::unmarshalDecode(d, &Category)};
	if (fieldIs(n, "source")) return {true, json::unmarshalDecode(d, &Source)};
	if (fieldIs(n, "text")) return {true, json::unmarshalDecode(d, &Text)};
	if (fieldIs(n, "reportsUnnecessary")) return {true, json::unmarshalDecode(d, &ReportsUnnecessary)};
	if (fieldIs(n, "reportsDeprecated")) return {true, json::unmarshalDecode(d, &ReportsDeprecated)};
	if (fieldIs(n, "messageChain")) return {true, json::unmarshalDecode(d, &MessageChain)};
	if (fieldIs(n, "relatedInformation")) return {true, json::unmarshalDecode(d, &RelatedInformation)};
	return {false, {}};
}
std::string DiagnosticResponse::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		auto [handled, err] = unmarshalField(n, d);
		if (handled) return err;
		return d.skipValue();
	});
}
std::string InitializeResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("useCaseSensitiveFileNames", UseCaseSensitiveFileNames);
	w.member("currentDirectory", CurrentDirectory);
	return w.end();
}

std::string FileNotifications::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (InvalidateAll) w.member("invalidateAll", InvalidateAll);
	if (!Changed.empty()) w.member("changed", Changed);
	if (!Created.empty()) w.member("created", Created);
	if (!Deleted.empty()) w.member("deleted", Deleted);
	return w.end();
}

std::string SnapshotRequestChangesParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (!OpenProjects.empty()) w.member("openProjects", OpenProjects);
	if (!CloseProjects.empty()) w.member("closeProjects", CloseProjects);
	if (OpenFiles && !OpenFiles->empty()) w.member("openFiles", *OpenFiles);
	if (!CloseFiles.empty()) w.member("closeFiles", CloseFiles);
	if (CreatePrograms && !CreatePrograms->empty()) w.member("createPrograms", *CreatePrograms);
	if (!ReconfigurePrograms.empty()) w.member("reconfigurePrograms", ReconfigurePrograms);
	if (!RemovePrograms.empty()) w.member("removePrograms", RemovePrograms);
	if (EnsurePrograms) w.member("ensurePrograms", EnsurePrograms);
	return w.end();
}

std::string CreateSnapshotParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (UserPreferences) w.member("userPreferences", UserPreferences);
	if (PrepareAutoImports) w.member("prepareAutoImports", PrepareAutoImports);
	if (FileNotifications) w.member("fileNotifications", FileNotifications);
	if (FileSystem) w.member("fileSystem", FileSystem);
	return w.end();
}

std::string CreateSnapshotProgramParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("rootFiles", RootFiles);
	w.member("compilerOptions", CompilerOptions);
	if (Options) w.member("options", Options);
	return w.end();
}

std::string ReconfigureSnapshotProgramParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("id", Id);
	w.member("rootFiles", RootFiles);
	w.member("compilerOptions", CompilerOptions);
	if (Options) w.member("options", Options);
	return w.end();
}

std::string UpdateSnapshotParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	if (Changes) w.member("changes", Changes);
	return w.end();
}

std::string GetCurrentLanguageServerSnapshotParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (!api::isZeroVal(BaseSnapshot)) w.member("baseSnapshot", BaseSnapshot);
	if (Changes) w.member("changes", Changes);
	return w.end();
}

std::string LanguageServerSnapshotChanges::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	return w.end();
}

std::string CreateProgramOptions::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (!ProjectReferences.empty()) w.member("projectReferences", ProjectReferences);
	if (!ConfigFileParsingDiagnostics.empty()) w.member("configFileParsingDiagnostics", ConfigFileParsingDiagnostics);
	if (!api::isZeroVal(ModuleResolver)) w.member("moduleResolver", ModuleResolver);
	return w.end();
}

std::string ModuleResolutionSpec::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("fallback", Fallback);
	w.member("entries", Entries);
	return w.end();
}

std::string ModuleResolutionEntry::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("moduleName", ModuleName);
	if (ContainingDirectory) w.member("containingDirectory", ContainingDirectory);
	if (ResolutionMode) w.member("resolutionMode", ResolutionMode);
	w.member("result", Result);
	return w.end();
}

std::string StaticModuleResolution::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (ResolvedFileName) w.member("resolvedFileName", ResolvedFileName);
	if (OriginalPath) w.member("originalPath", OriginalPath);
	if (PackageID) w.member("packageId", PackageID);
	return w.end();
}

std::string CreateModuleResolverParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("compilerOptions", CompilerOptions);
	if (ModuleResolutions) w.member("moduleResolutions", ModuleResolutions);
	if (!ResolveModuleNameCallback.empty()) w.member("resolveModuleNameCallback", ResolveModuleNameCallback);
	return w.end();
}

std::string ReleaseModuleResolverParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("resolver", Resolver);
	return w.end();
}

std::string ResolveModuleNameParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (!api::isZeroVal(Snapshot)) w.member("snapshot", Snapshot);
	if (!api::isZeroVal(InProgressSnapshot)) w.member("inProgressSnapshot", InProgressSnapshot);
	w.member("resolver", Resolver);
	w.member("moduleName", ModuleName);
	w.member("containingDirectory", ContainingDirectory);
	if (ResolutionMode) w.member("resolutionMode", ResolutionMode);
	return w.end();
}

std::string ResolveModuleNameCallbackParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("moduleName", ModuleName);
	w.member("containingDirectory", ContainingDirectory);
	if (ResolutionMode) w.member("resolutionMode", ResolutionMode);
	if (Snapshot) w.member("snapshot", Snapshot);
	if (InProgressSnapshot) w.member("inProgressSnapshot", InProgressSnapshot);
	return w.end();
}

std::string ResolveModuleNameResult::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (ResolvedModule) w.member("resolvedModule", ResolvedModule);
	if (!Trace.empty()) w.member("trace", Trace);
	return w.end();
}

std::string ProjectFileChanges::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (!ChangedFiles.empty()) w.member("changedFiles", ChangedFiles);
	if (!DeletedFiles.empty()) w.member("deletedFiles", DeletedFiles);
	return w.end();
}

std::string SnapshotChanges::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (!ChangedProjects.empty()) w.member("changedProjects", ChangedProjects);
	if (!RemovedProjects.empty()) w.member("removedProjects", RemovedProjects);
	return w.end();
}

std::string CreateSnapshotResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("projects", Projects);
	if (Changes) w.member("changes", Changes);
	w.member("operation", Operation);
	return w.end();
}

std::string SnapshotOperationResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (CreatedPrograms) w.member("createdPrograms", CreatedPrograms);
	if (OpenedFiles) w.member("openedFiles", OpenedFiles);
	return w.end();
}

std::string OpenedFileOperationResult::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("project", Project);
	return w.end();
}

std::string ParseConfigFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("file", File);
	return w.end();
}

std::string ParseCommandLineParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("commandLine", CommandLine);
	return w.end();
}

std::string ReadConfigFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("file", File);
	return w.end();
}

std::string ParseJsonConfigFileContentParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("json", JSON);
	if (ConfigDirectory) w.member("configDirectory", ConfigDirectory);
	if (ConfigFileName) w.member("configFileName", ConfigFileName);
	return w.end();
}

std::string TranspileOptions::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (CompilerOptions) w.member("compilerOptions", CompilerOptions);
	if (!FileName.empty()) w.member("fileName", FileName);
	if (ReportDiagnostics) w.member("reportDiagnostics", ReportDiagnostics);
	return w.end();
}

std::string CreateSourceFileOptions::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (!api::isZeroVal(ScriptKind)) w.member("scriptKind", ScriptKind);
	return w.end();
}

std::string CreateSourceFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("fileName", FileName);
	w.member("sourceText", SourceText);
	w.member("options", Options);
	return w.end();
}

std::string CreateSourceFileFromFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("fileName", FileName);
	w.member("options", Options);
	return w.end();
}

std::string TranspileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("input", Input);
	w.member("options", Options);
	return w.end();
}

std::string TranspileFromFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("fileName", FileName);
	w.member("options", Options);
	return w.end();
}

std::string TranspileOutputResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("outputText", OutputText);
	if (!Diagnostics.empty()) w.member("diagnostics", Diagnostics);
	if (!SourceMapText.empty()) w.member("sourceMapText", SourceMapText);
	return w.end();
}

std::string BatchRequestsParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("requests", Requests);
	if (!ContinuationToken.empty()) w.member("continuationToken", ContinuationToken);
	if (!api::isZeroVal(MaxResponseBytesPerPage)) w.member("maxResponseBytesPerPage", MaxResponseBytesPerPage);
	return w.end();
}

std::string BatchRequest::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("method", Method);
	if (!Params.empty()) w.memberRaw("params", Params);
	return w.end();
}

std::string BatchResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("method", Method);
	w.memberRaw("result", Result);
	if (!Error.empty()) w.member("error", Error);
	return w.end();
}

std::string ReleaseParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	return w.end();
}

std::string ReleaseSourceFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("lease", Lease);
	return w.end();
}

std::string ProfileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("dir", Dir);
	return w.end();
}

std::string ProfileResult::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("file", File);
	return w.end();
}

std::string CreateBuildOrchestratorParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("rootNames", RootNames);
	if (!Cwd.empty()) w.member("cwd", Cwd);
	return w.end();
}

std::string CreateBuildOrchestratorResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("buildOrchestratorID", BuildOrchestratorID);
	return w.end();
}

std::string DisposeBuildOrchestratorParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("buildOrchestratorID", BuildOrchestratorID);
	return w.end();
}

std::string BuildParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("buildOrchestratorID", BuildOrchestratorID);
	if (!Project.empty()) w.member("project", Project);
	return w.end();
}

std::string BuildResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("status", Status);
	if (!Diagnostics.empty()) w.member("diagnostics", Diagnostics);
	w.member("statistics", Statistics);
	return w.end();
}

std::string CleanBuildParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("buildOrchestratorID", BuildOrchestratorID);
	if (!Project.empty()) w.member("project", Project);
	return w.end();
}

std::string CleanBuildResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("status", Status);
	if (!Diagnostics.empty()) w.member("diagnostics", Diagnostics);
	w.member("statistics", Statistics);
	if (!FilesDeleted.empty()) w.member("filesDeleted", FilesDeleted);
	return w.end();
}

std::string ConfigFileResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("fileNames", FileNames);
	w.member("options", Options);
	if (BuildOptions) w.member("buildOptions", BuildOptions);
	if (!ProjectReferences.empty()) w.member("projectReferences", ProjectReferences);
	if (TypeAcquisition) w.member("typeAcquisition", TypeAcquisition);
	if (CompileOnSave) w.member("compileOnSave", CompileOnSave);
	if (!Raw.empty()) w.memberRaw("raw", Raw);
	w.member("errors", Errors);
	return w.end();
}

std::string ReadConfigFileResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.memberRaw("config", Config);
	if (Error) w.member("error", Error);
	return w.end();
}

std::string GetDefaultProjectForFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("file", File);
	return w.end();
}

std::string ProjectResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("id", Id);
	w.member("configFileName", ConfigFileName);
	w.member("currentDirectory", CurrentDirectory);
	w.member("dirty", Dirty);
	w.member("parsedCommandLine", ParsedCommandLine);
	w.member("rootFiles", RootFiles);
	w.member("compilerOptions", CompilerOptions);
	return w.end();
}

std::string GetSymbolAtPositionParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("position", Position);
	return w.end();
}

std::string GetSymbolsAtPositionsParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("positions", Positions);
	return w.end();
}

std::string GetSymbolOfSourceFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	return w.end();
}

std::string GetSymbolsOfSourceFilesParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("files", Files);
	return w.end();
}

std::string GetSymbolAtLocationParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("location", Location);
	return w.end();
}

std::string GetSymbolsAtLocationsParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("locations", Locations);
	return w.end();
}

std::string SymbolResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("reference", Reference);
	w.member("name", Name);
	w.member("flags", Flags);
	w.member("checkFlags", CheckFlags);
	if (!Declarations.empty()) w.member("declarations", Declarations);
	if (!ValueDeclaration.empty()) w.member("valueDeclaration", ValueDeclaration);
	if (Parent) w.member("parent", *Parent);
	if (ExportSymbol) w.member("exportSymbol", *ExportSymbol);
	return w.end();
}

std::string GetTypeOfSymbolParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("symbol", Symbol);
	return w.end();
}

std::string GetTypesOfSymbolsParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("symbols", Symbols);
	return w.end();
}

std::string TypeResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("id", Id);
	w.member("flags", Flags);
	if (!api::isZeroVal(ObjectFlags)) w.member("objectFlags", ObjectFlags);
	if (IsTupleType) w.member("isTupleType", IsTupleType);
	w.memberRaw("value", Value);
	if (!api::isZeroVal(Target)) w.member("target", Target);
	if (!TypeParameters.empty()) w.member("typeParameters", TypeParameters);
	if (!OuterTypeParameters.empty()) w.member("outerTypeParameters", OuterTypeParameters);
	if (!LocalTypeParameters.empty()) w.member("localTypeParameters", LocalTypeParameters);
	if (!ElementFlags.empty()) w.member("elementFlags", ElementFlags);
	if (FixedLength) w.member("fixedLength", FixedLength);
	if (TupleReadonly) w.member("readonly", TupleReadonly);
	if (!LabeledElementDeclarations.empty()) w.member("labeledElementDeclarations", LabeledElementDeclarations);
	if (!api::isZeroVal(ObjectType)) w.member("objectType", ObjectType);
	if (!api::isZeroVal(IndexType)) w.member("indexType", IndexType);
	if (!api::isZeroVal(CheckType)) w.member("checkType", CheckType);
	if (!api::isZeroVal(ExtendsType)) w.member("extendsType", ExtendsType);
	if (!api::isZeroVal(BaseType)) w.member("baseType", BaseType);
	if (!api::isZeroVal(SubstConstraint)) w.member("substConstraint", SubstConstraint);
	if (!api::isZeroVal(TypeParameter)) w.member("typeParameter", TypeParameter);
	if (!api::isZeroVal(ConstraintType)) w.member("constraintType", ConstraintType);
	if (!api::isZeroVal(NameType)) w.member("nameType", NameType);
	if (!api::isZeroVal(TemplateType)) w.member("templateType", TemplateType);
	if (!Texts.empty()) w.member("texts", Texts);
	if (!api::isZeroVal(FreshType)) w.member("freshType", FreshType);
	if (!api::isZeroVal(RegularType)) w.member("regularType", RegularType);
	if (IsThisType) w.member("isThisType", IsThisType);
	if (!api::isZeroVal(ThisType)) w.member("thisType", ThisType);
	if (!IntrinsicName.empty()) w.member("intrinsicName", IntrinsicName);
	if (!AliasTypeArguments.empty()) w.member("aliasTypeArguments", AliasTypeArguments);
	if (AliasSymbol) w.member("aliasSymbol", *AliasSymbol);
	if (Symbol) w.member("symbol", *Symbol);
	return w.end();
}

std::string ConstantValueResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("isNumber", IsNumber);
	w.memberRaw("value", Value);
	return w.end();
}

std::string SignatureResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("id", Id);
	w.member("flags", Flags);
	if (!Declaration.empty()) w.member("declaration", Declaration);
	if (!TypeParameters.empty()) w.member("typeParameters", TypeParameters);
	if (!Parameters.empty()) w.member("parameters", Parameters);
	if (ThisParameter) w.member("thisParameter", *ThisParameter);
	if (!api::isZeroVal(Target)) w.member("target", Target);
	return w.end();
}

std::string GetSourceFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	return w.end();
}

std::string GetSourceFileNamesParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	return w.end();
}

std::string GetModeForUsageLocationParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("usage", Usage);
	return w.end();
}

std::string GetModeForResolutionAtIndexParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("index", Index);
	return w.end();
}

std::string GetResolvedModuleParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("moduleName", ModuleName);
	w.member("mode", Mode);
	return w.end();
}

std::string GetResolvedModuleFromModuleSpecifierParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("moduleSpecifier", ModuleSpecifier);
	if (SourceFile) w.member("sourceFile", SourceFile);
	return w.end();
}

std::string GetResolvedTypeReferenceDirectiveParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("typeDirectiveName", TypeDirectiveName);
	w.member("mode", Mode);
	return w.end();
}

std::string GetResolvedTypeReferenceDirectiveFromReferenceParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("sourceFile", SourceFile);
	w.member("typeDirectiveName", TypeDirectiveName);
	w.member("resolutionMode", ResolutionMode);
	return w.end();
}

std::string PackageId::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("name", Name);
	w.member("subModuleName", SubModuleName);
	w.member("version", Version);
	w.member("peerDependencies", PeerDependencies);
	return w.end();
}

std::string ResolvedModule::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("resolvedFileName", ResolvedFileName);
	if (!OriginalPath.empty()) w.member("originalPath", OriginalPath);
	w.member("extension", Extension);
	if (ResolvedUsingTsExtension) w.member("resolvedUsingTsExtension", ResolvedUsingTsExtension);
	if (ResolvedUsingExtraExtensions) w.member("resolvedUsingExtraExtensions", ResolvedUsingExtraExtensions);
	if (PackageId) w.member("packageId", PackageId);
	if (IsExternalLibraryImport) w.member("isExternalLibraryImport", IsExternalLibraryImport);
	if (!AlternateResult.empty()) w.member("alternateResult", AlternateResult);
	return w.end();
}

std::string ResolvedTypeReferenceDirective::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("primary", Primary);
	w.member("resolvedFileName", ResolvedFileName);
	if (!OriginalPath.empty()) w.member("originalPath", OriginalPath);
	if (PackageId) w.member("packageId", PackageId);
	if (IsExternalLibraryImport) w.member("isExternalLibraryImport", IsExternalLibraryImport);
	return w.end();
}

std::string SourceFileMetadata::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("isDefaultLibrary", IsDefaultLibrary);
	w.member("isFromExternalLibrary", IsFromExternalLibrary);
	w.member("packageJsonType", PackageJsonType);
	w.member("packageJsonDirectory", PackageJsonDirectory);
	w.member("impliedNodeFormat", ImpliedNodeFormat);
	return w.end();
}

std::string ResolveNameParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("name", Name);
	if (!Location.empty()) w.member("location", Location);
	if (File) w.member("file", File);
	if (Position) w.member("position", Position);
	w.member("meaning", Meaning);
	if (ExcludeGlobals) w.member("excludeGlobals", ExcludeGlobals);
	return w.end();
}

std::string GetSymbolsInScopeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	if (!Location.empty()) w.member("location", Location);
	if (File) w.member("file", File);
	if (Position) w.member("position", Position);
	w.member("meaning", Meaning);
	return w.end();
}

std::string GetTypePropertyParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("objectId", Type);
	return w.end();
}

std::string GetSymbolPropertyParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("symbol", Symbol);
	return w.end();
}

std::string GetSignaturePropertyParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("objectId", Signature);
	return w.end();
}

std::string GetContextualTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("location", Location);
	return w.end();
}

std::string GetContextualTypeForArgumentParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("location", Location);
	w.member("index", Index);
	return w.end();
}

std::string GetTypeOfSymbolAtLocationParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("symbol", Symbol);
	w.member("location", Location);
	return w.end();
}

std::string GetReferencesToSymbolInFileParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("symbol", Symbol);
	return w.end();
}

std::string GetReferencedSymbolsForNodeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("node", Node);
	w.member("position", Position);
	return w.end();
}

std::string ReferencedSymbolEntry::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("definition", Definition);
	if (Symbol) w.member("symbol", Symbol);
	w.member("references", References);
	return w.end();
}

std::string GetSignatureUsagesParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("signatureDecl", SignatureDecl);
	return w.end();
}

std::string SignatureUsageResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("name", Name);
	if (!Call.empty()) w.member("call", Call);
	return w.end();
}

std::string GetCompletionsAtPositionParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("position", Position);
	if (TriggerCharacter) w.member("triggerCharacter", TriggerCharacter);
	if (IncludeSymbol) w.member("includeSymbol", IncludeSymbol);
	return w.end();
}

std::string CompletionEntryLabelDetailsResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (Detail) w.member("detail", Detail);
	if (Description) w.member("description", Description);
	return w.end();
}

std::string CompletionEntryResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("name", Name);
	if (!api::isZeroVal(Kind)) w.member("kind", Kind);
	if (SortText) w.member("sortText", SortText);
	if (InsertText) w.member("insertText", InsertText);
	if (FilterText) w.member("filterText", FilterText);
	if (Detail) w.member("detail", Detail);
	if (LabelDetails) w.member("labelDetails", LabelDetails);
	if (Symbol) w.member("symbol", Symbol);
	return w.end();
}

std::string CompletionInfoResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("isIncomplete", IsIncomplete);
	w.member("entries", Entries);
	return w.end();
}

std::string GetIntrinsicTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	return w.end();
}

std::string WellKnownSymbolsResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("unknown", Unknown);
	w.member("undefined", Undefined);
	w.member("arguments", Arguments);
	return w.end();
}

std::string WellKnownSignaturesResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("unknown", Unknown);
	return w.end();
}

std::string GetBaseTypeOfLiteralTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("type", Type);
	return w.end();
}

std::string GetNonNullableTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("type", Type);
	return w.end();
}

std::string GetTypeFromTypeNodeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("location", Location);
	return w.end();
}

std::string GetWidenedTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("type", Type);
	return w.end();
}

std::string GetParameterTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("signature", Signature);
	w.member("index", Index);
	return w.end();
}

std::string IsArrayLikeTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("type", Type);
	return w.end();
}

std::string IsTypeAssignableToParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("source", Source);
	w.member("target", Target);
	return w.end();
}

std::string GetSignaturesOfTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("type", Type);
	w.member("kind", Kind);
	return w.end();
}

std::string GetResolvedSignatureParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("location", Location);
	return w.end();
}

std::string GetTypeAtLocationParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("location", Location);
	return w.end();
}

std::string GetTypeAtLocationsParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("locations", Locations);
	return w.end();
}

std::string GetTypeAtPositionParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("position", Position);
	return w.end();
}

std::string GetTypesAtPositionsParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("positions", Positions);
	return w.end();
}

std::string ImportAdderAction::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("kind", Kind);
	if (!api::isZeroVal(Symbol)) w.member("symbol", Symbol);
	if (IsValidTypeOnlyUseSite) w.member("isValidTypeOnlyUseSite", IsValidTypeOnlyUseSite);
	return w.end();
}

std::string GetImportAdderEditsParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("actions", Actions);
	return w.end();
}

std::string TextEdit::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("pos", Pos);
	w.member("end", End);
	w.member("newText", NewText);
	return w.end();
}

std::string TypeToTypeNodeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("type", Type);
	if (!Location.empty()) w.member("location", Location);
	if (!api::isZeroVal(Flags)) w.member("flags", Flags);
	return w.end();
}

std::string SignatureToSignatureDeclarationParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("signature", Signature);
	w.member("kind", Kind);
	if (!Location.empty()) w.member("location", Location);
	if (!api::isZeroVal(Flags)) w.member("flags", Flags);
	return w.end();
}

std::string PrintNodeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("data", Data);
	if (PreserveSourceNewlines) w.member("preserveSourceNewlines", PreserveSourceNewlines);
	if (NeverAsciiEscape) w.member("neverAsciiEscape", NeverAsciiEscape);
	if (TerminateUnterminatedLiterals) w.member("terminateUnterminatedLiterals", TerminateUnterminatedLiterals);
	return w.end();
}

std::string EmitParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	if (EmitOnly) w.member("emitOnly", EmitOnly);
	return w.end();
}

std::string SelectedFilesEmitParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("files", Files);
	return w.end();
}

std::string EmitResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("emitSkipped", EmitSkipped);
	w.member("diagnostics", Diagnostics);
	w.member("emittedFiles", EmittedFiles);
	w.member("emittedFilesContents", EmittedFilesContents);
	return w.end();
}

std::string EmitOutputFile::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("fileName", FileName);
	w.member("text", Text);
	if (SourceFileName) w.member("sourceFileName", SourceFileName);
	return w.end();
}

std::string EmitOutputResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("emitSkipped", EmitSkipped);
	w.member("diagnostics", Diagnostics);
	w.member("outputFiles", OutputFiles);
	return w.end();
}

std::string FormatNodeForInsertionParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("file", File);
	w.member("position", Position);
	w.member("data", Data);
	return w.end();
}

std::string CheckerTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("type", Type);
	return w.end();
}

std::string GetPropertyOfTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("type", Type);
	w.member("name", Name);
	return w.end();
}

std::string GetIndexInfoOfTypeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("type", Type);
	w.member("kind", Kind);
	return w.end();
}

std::string GetMemberInModuleExportsParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("symbol", Symbol);
	w.member("name", Name);
	return w.end();
}

std::string CheckerNodeParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("location", Location);
	return w.end();
}

std::string CheckerSymbolParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("symbol", Symbol);
	return w.end();
}

std::string JSDocTagInfo::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("name", Name);
	if (!Text.empty()) w.member("text", Text);
	return w.end();
}

std::string CheckerSignatureParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	w.member("signature", Signature);
	return w.end();
}

std::string TypePredicateResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("kind", Kind);
	w.member("parameterIndex", ParameterIndex);
	if (!ParameterName.empty()) w.member("parameterName", ParameterName);
	if (Type) w.member("type", Type);
	return w.end();
}

std::string IndexInfoResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("keyType", KeyType);
	w.member("valueType", ValueType);
	if (IsReadonly) w.member("isReadonly", IsReadonly);
	if (!Declaration.empty()) w.member("declaration", Declaration);
	return w.end();
}

std::string SourceFileResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("data", Data);
	return w.end();
}

std::string GetDiagnosticsParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	if (!Files.empty()) w.member("files", Files);
	return w.end();
}

std::string GetProjectDiagnosticsParams::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("snapshot", Snapshot);
	w.member("project", Project);
	return w.end();
}

std::string DiagnosticPositionResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("line", Line);
	w.member("character", Character);
	return w.end();
}

std::string DiagnosticSourceLineResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	w.member("line", Line);
	w.member("text", Text);
	return w.end();
}

}  // namespace tsc::api

namespace tsc::project {

// ID JSON round-trip — project.go ID marshals as a plain string.
std::string unmarshalJSONFrom(json::Decoder& dec, ID* out) {
	auto [t, err] = dec.readToken();
	if (!err.empty()) return err;
	if (t.k != '"') return "json: cannot unmarshal non-string into Go value of type string";
	std::string v;
	err = json::unmarshal(std::string_view(t.raw), &v);
	if (!err.empty()) return err;
	*out = ID(std::move(v));
	return {};
}
std::string marshalJSONTo(json::Encoder& enc, const ID& id) {
	return enc.writeValue(json::Value(json::marshalString(id)));
}

}  // namespace tsc::project

namespace tsc::packagejson {

// JSONValue::marshalJSONTo — jsonvalue.go:41. encoding/json marshals the
// exported Go fields: `{"Type":<n>,"Value":<any>}`.
std::string JSONValue::marshalJSONTo(json::Encoder& enc) const {
	api::objWriter w{enc};
	w.begin();
	w.member("Type", static_cast<int8_t>(type));
	if (auto e = enc.writeValue(json::marshalString("Value")); !e.empty()) return e;
	switch (type) {
	case JSONValueType::NotPresent:
	case JSONValueType::Null:
		if (auto e = enc.writeValue(json::Value("null")); !e.empty()) return e;
		break;
	case JSONValueType::String:
		if (auto e = enc.writeValue(json::Value(json::marshalString(str))); !e.empty()) return e;
		break;
	case JSONValueType::Number:
		if (auto e = enc.writeValue(json::Value(json::detail::goFloat(num))); !e.empty()) return e;
		break;
	case JSONValueType::Boolean:
		if (auto e = enc.writeValue(json::Value(json::marshalBool(boolean))); !e.empty()) return e;
		break;
	case JSONValueType::Array:
		if (auto e = json::marshalEncode(enc, array); !e.empty()) return e;
		break;
	case JSONValueType::Object:
		if (auto e = json::marshalEncode(enc, object); !e.empty()) return e;
		break;
	}
	return w.end();
}

}  // namespace tsc::packagejson

namespace tsc::execute::tsc {

// Statistics::marshalJSONTo — statistics.go:49; only the exported Go fields
// marshal (capitalized names, no tags).
std::string Statistics::marshalJSONTo(json::Encoder& enc) const {
	api::objWriter w{enc};
	w.begin();
	w.member("Projects", Projects);
	w.member("ProjectsBuilt", ProjectsBuilt);
	w.member("TimestampUpdates", TimestampUpdates);
	return w.end();
}

}  // namespace tsc::execute::tsc

namespace tsc::api {

// jsonValueToAny — proto.go:764.
tsoptions::CompilerOptionsValue jsonValueToAny(
    const packagejson::JSONValue& value) {
	switch (value.type) {
	case packagejson::JSONValueType::NotPresent:
	case packagejson::JSONValueType::Null:
		return tsoptions::CompilerOptionsValue{};
	case packagejson::JSONValueType::String:
		return tsoptions::CompilerOptionsValue(value.str);
	case packagejson::JSONValueType::Number:
		return tsoptions::CompilerOptionsValue(value.num);
	case packagejson::JSONValueType::Boolean:
		return tsoptions::CompilerOptionsValue(value.boolean);
	case packagejson::JSONValueType::Array: {
		const std::vector<packagejson::JSONValue>* array = value.AsArray();
		tsoptions::JsonArray result(array->size());
		for (size_t i = 0; i < array->size(); ++i) {
			result[i] = jsonValueToAny((*array)[i]);
		}
		return tsoptions::CompilerOptionsValue(std::move(result));
	}
	case packagejson::JSONValueType::Object: {
		const collections::OrderedMap<std::string, packagejson::JSONValue>* object = value.AsObject();
		tsoptions::JsonObjectPtr result = std::make_shared<
		    collections::OrderedMap<std::string, tsoptions::CompilerOptionsValue>>(
		    object->Size());
		for (const auto& key : object->Keys()) {
			result->Set(key, jsonValueToAny(*object->Get(key).first));
		}
		return tsoptions::CompilerOptionsValue(std::move(result));
	}
	default:
		TSC_UNREACHABLE((std::string("unexpected JSON value type ") +
		                 packagejson::JSONValueTypeString(value.type))
		                    .c_str());
	}
}

// literalValueToJSON — proto.go:1294. Returns the marshaled JSON text for Go
// `any` (a literal's constant value): strings/numbers/bools marshal directly;
// +/-Infinity/NaN marshal as sentinel strings; PseudoBigInt marshals as a
// signed decimal string which the client decodes back into a real bigint.
json::Value literalValueToJSON(const checker::LiteralValue& value) {
	if (const auto* p = std::get_if<std::string>(&value)) {
		return json::marshalString(*p);
	}
	if (const auto* p = std::get_if<Number>(&value)) {
		if (p->isInf()) {
			return json::marshalString(p->v > 0 ? "+Infinity" : "-Infinity");
		}
		if (p->isNaN()) {
			return json::marshalString("NaN");
		}
		return json::detail::goFloat(p->v);
	}
	if (const auto* p = std::get_if<bool>(&value)) {
		return json::marshalBool(*p);
	}
	if (const auto* p = std::get_if<PseudoBigInt>(&value)) {
		// Encode bigint literals as a signed decimal string (e.g. "-123"); the
		// API client decodes this back into a real bigint. JSON has no bigint.
		return json::marshalString(p->string());
	}
	return "null";
}

// nextBuildOrchestratorId — proto.go:49.
static std::atomic<uint64_t> nextBuildOrchestratorId;


// NewBuildOrchestratorID — proto.go:51.
BuildOrchestratorID NewBuildOrchestratorID() {
	return BuildOrchestratorID(nextBuildOrchestratorId.fetch_add(1) + 1);
}

// SymbolHandle — proto.go:55.
SymbolID SymbolHandle(Symbol* symbol) {
	return SymbolID(getSymbolId(symbol));
}

// TypeHandle — proto.go:59.
TypeID TypeHandle(checker::Type* t) {
	return TypeID(t->id);
}

// SignatureHandle — proto.go:63.
SignatureID SignatureHandle(checker::Signature* sig) {
	return SignatureID(sig->id);
}

// ToFileName — proto.go:327.
std::string DocumentIdentifier::ToFileName() const {
	if (!URI.empty()) {
		return lsproto::documentUriFileName(URI);
	}
	return FileName;
}

// ToURI — proto.go:337. An explicitly provided URI is returned as-is; a file
// name is first normalized to an absolute path against cwd before being
// converted to a URI.
lsproto::DocumentUri DocumentIdentifier::ToURI(const std::string& cwd) const {
	if (!URI.empty()) {
		return URI;
	}
	return lsconv::FileNameToDocumentURI(
	    tspath::getNormalizedAbsolutePath(FileName, cwd));
}

// ToAbsoluteFileName — proto.go:344.
std::string DocumentIdentifier::ToAbsoluteFileName(const std::string& cwd) const {
	if (!URI.empty()) {
		return lsproto::documentUriFileName(URI);
	}
	return tspath::getNormalizedAbsolutePath(FileName, cwd);
}

// String — proto.go:351.
std::string DocumentIdentifier::String() const {
	if (!URI.empty()) {
		return URI;
	}
	return FileName;
}

// NewConfigFileResponse — proto.go:983.
std::shared_ptr<ConfigFileResponse> NewConfigFileResponse(
    tsoptions::ParsedCommandLine* parsedCommandLine) {
	if (parsedCommandLine == nullptr) {
		return nullptr;
	}
	std::optional<bool> compileOnSave;
	if (parsedCommandLine->CompileOnSave) {
		compileOnSave = *parsedCommandLine->CompileOnSave;
	} else if (const auto* rawConfig =
	               parsedCommandLine->Raw.get<tsoptions::JsonObjectPtr>()) {
		// Go calls rawConfig.GetOrZero on the (non-nil) OrderedMap; a typed-nil
		// map inside the `any` would panic, so the null check mirrors the
		// reachable path only.
		if (*rawConfig != nullptr) {
			if (const auto* b =
			        (*rawConfig)->GetOrZero("compileOnSave").get<bool>()) {
				compileOnSave = *b;
			}
		}
	}
	tsc::CompilerOptions* compilerOptions = parsedCommandLine->CompilerOptions();
	std::vector<std::shared_ptr<DiagnosticResponse>> errors =
	    NewDiagnosticResponses(parsedCommandLine->Errors);
	auto resp = std::make_shared<ConfigFileResponse>();
	resp->FileNames = parsedCommandLine->FileNames();
	resp->Options = compilerOptions;
	resp->ProjectReferences = parsedCommandLine->ProjectReferences();
	resp->TypeAcquisition = parsedCommandLine->TypeAcquisition();
	resp->CompileOnSave = compileOnSave;
	// `raw,omitempty`: Go's nil `any` omits the member; only marshal non-nil Raw.
	if (!parsedCommandLine->Raw.isNil()) {
		resp->Raw = json::Value(
		    tsoptions::jsonMarshal(parsedCommandLine->Raw));
	}
	resp->Errors = std::move(errors);
	return resp;
}

// NewProjectResponse — proto.go:1036.
std::shared_ptr<ProjectResponse> NewProjectResponse(project::Project* p) {
	if (p == nullptr || p->CommandLine == nullptr) {
		TSC_UNREACHABLE("NewProjectResponse called with unloaded project");
	}
	std::string configFileName;
	if (p->Kind == project::KindConfigured) {
		configFileName = p->ConfigFileName();
	}
	auto resp = std::make_shared<ProjectResponse>();
	resp->Id = p->ID();
	resp->ConfigFileName = std::move(configFileName);
	resp->CurrentDirectory = p->CurrentDirectory();
	resp->Dirty = p->IsDirty();
	resp->ParsedCommandLine = NewConfigFileResponse(p->CommandLine);
	resp->RootFiles = p->CommandLine->FileNames();
	resp->CompilerOptions = p->CommandLine->CompilerOptions();
	return resp;
}

// newTypeResponse — proto.go:1196.
std::shared_ptr<TypeResponse> newTypeResponse(checker::Type* t, TypeID id) {
	auto resp = std::make_shared<TypeResponse>();
	resp->Id = id;
	resp->Flags = uint32_t(t->flags);

	if (t->alias != nullptr) {
		resp->AliasTypeArguments = typeHandles(t->alias->TypeArguments());
	}

	checker::TypeFlags flags = t->flags;
	if ((flags & checker::TypeFlagsFreshable) != 0) {
		auto* lit = t->AsLiteralType();
		if ((flags & checker::TypeFlagsLiteral) != 0) {
			resp->Value = literalValueToJSON(lit->value);
		}
		if (lit->freshType != nullptr) {
			resp->FreshType = TypeHandle(lit->freshType);
		}
		if (lit->regularType != nullptr) {
			resp->RegularType = TypeHandle(lit->regularType);
		}
	} else if ((flags & checker::TypeFlagsObject) != 0) {
		resp->ObjectFlags = uint32_t(t->objectFlags);
		resp->IsTupleType = checker::IsTupleType(t);
		checker::ObjectFlags objectFlags = t->objectFlags;
		if ((objectFlags & checker::ObjectFlagsReference) != 0) {
			auto* ref = t->AsTypeReference();
			if (checker::IsTupleTypeTarget(t)) {
				auto* tuple = t->AsTupleType();
				// tuple.ElementFlags() — types.go:1092.
				std::vector<checker::ElementFlags> elementFlags(
				    tuple->elementInfos.size());
				for (size_t i = 0; i < tuple->elementInfos.size(); i++) {
					elementFlags[i] = tuple->elementInfos[i].flags;
				}
				resp->ElementFlags = std::move(elementFlags);
				resp->FixedLength = int64_t(tuple->fixedLength);
				resp->TupleReadonly = tuple->readonly;
			}
			if (checker::Type* target = ref->AsType()->Target();
			    target != nullptr) {
				resp->Target = TypeHandle(target);
			}
		}
		if ((objectFlags & checker::ObjectFlagsClassOrInterface) != 0) {
			auto* iface = t->AsInterfaceType();
			resp->TypeParameters =
			    typeHandles(checker::interfaceTypeTypeParameters(iface));
			resp->OuterTypeParameters =
			    typeHandles(checker::interfaceTypeOuterTypeParameters(iface));
			resp->LocalTypeParameters =
			    typeHandles(checker::interfaceTypeLocalTypeParameters(iface));
			if (iface->thisType != nullptr) {
				resp->ThisType = TypeHandle(iface->thisType);
			}
		}
	} else if ((flags & checker::TypeFlagsUnionOrIntersection) != 0) {
		// types omitted; fetched via separate request
	} else if ((flags & checker::TypeFlagsIndex) != 0) {
		resp->Target = TypeHandle(t->AsIndexType()->target);
	} else if ((flags & checker::TypeFlagsIndexedAccess) != 0) {
		auto* data = t->AsIndexedAccessType();
		resp->ObjectType = TypeHandle(data->objectType);
		resp->IndexType = TypeHandle(data->indexType);
	} else if ((flags & checker::TypeFlagsConditional) != 0) {
		auto* data = t->AsConditionalType();
		resp->CheckType = TypeHandle(data->checkType);
		resp->ExtendsType = TypeHandle(data->extendsType);
	} else if ((flags & checker::TypeFlagsSubstitution) != 0) {
		auto* data = t->AsSubstitutionType();
		resp->BaseType = TypeHandle(data->baseType);
		resp->SubstConstraint = TypeHandle(data->constraint);
	} else if ((flags & checker::TypeFlagsTemplateLiteral) != 0) {
		auto* tl = t->AsTemplateLiteralType();
		resp->Texts = tl->texts;
		// types omitted; fetched via separate request
	} else if ((flags & checker::TypeFlagsStringMapping) != 0) {
		resp->Target = TypeHandle(t->AsStringMappingType()->target);
	} else if ((flags & checker::TypeFlagsTypeParameter) != 0) {
		resp->IsThisType = t->AsTypeParameter()->isThisType;
	} else if ((flags & checker::TypeFlagsIntrinsic) != 0) {
		resp->IntrinsicName = t->AsIntrinsicType()->intrinsicName;
	}

	return resp;
}

// typeHandles — proto.go:1283.
std::vector<TypeID> typeHandles(const std::vector<checker::Type*>& types) {
	if (types.empty()) {
		return {};
	}
	std::vector<TypeID> handles(types.size());
	for (size_t i = 0; i < types.size(); i++) {
		handles[i] = TypeHandle(types[i]);
	}
	return handles;
}

// NewPackageId — proto.go:1398.
std::shared_ptr<PackageId> NewPackageId(const module::PackageId& packageID) {
	if (packageID.Name.empty()) {
		return nullptr;
	}
	auto resp = std::make_shared<PackageId>();
	resp->Name = packageID.Name;
	resp->SubModuleName = packageID.SubModuleName;
	resp->Version = packageID.Version;
	resp->PeerDependencies = packageID.PeerDependencies;
	return resp;
}

// FileLike-based replica of scanner.GetECMALineAndUTF16CharacterOfPosition —
// scanner.go:2684. The scanner helper takes a concrete SourceFile*, while the
// diagnostic layer only exposes a FileLike.
static std::pair<int, int> getECMALineAndUTF16CharacterOfPosition(
    const diagnosticwriter::FileLike* file, int pos) {
	const auto& lineMap = file->ecmaLineMap();
	int line = computeLineOfPosition(lineMap, pos);
	int character = utf16Len(
	    std::string_view(file->text()).substr(lineMap[line], pos - lineMap[line]));
	return {line, character};
}

// diagnosticSourceLines — proto.go:1915.
static std::vector<std::shared_ptr<DiagnosticSourceLineResponse>>
diagnosticSourceLines(const diagnosticwriter::FileLike* file, int firstLine,
                      int lastLine) {
	const auto& lineMap = file->ecmaLineMap();
	if (lineMap.empty()) {
		return {};
	}

	std::vector<int> lines;
	lines.reserve(std::min(lastLine - firstLine + 1, 4));
	if (lastLine - firstLine >= 4) {
		lines.push_back(firstLine);
		lines.push_back(firstLine + 1);
		lines.push_back(lastLine - 1);
		lines.push_back(lastLine);
	} else {
		for (int line = firstLine; line <= lastLine; line++) {
			lines.push_back(line);
		}
	}

	std::string_view text = file->text();
	std::vector<std::shared_ptr<DiagnosticSourceLineResponse>> result;
	result.reserve(lines.size());
	for (int line : lines) {
		size_t start = static_cast<size_t>(lineMap[line]);
		size_t end = text.size();
		if (line + 1 < static_cast<int>(lineMap.size())) {
			end = static_cast<size_t>(lineMap[line + 1]);
		}
		auto r = std::make_shared<DiagnosticSourceLineResponse>();
		r->Line = line;
		r->Text = std::string(text.substr(start, end - start));
		result.push_back(std::move(r));
	}
	return result;
}

// fileLikeSourceFile — the counterpart of Go's `file.(*ast.SourceFile)` type
// assertion in newDiagnosticResponse. ASTDiagnostic::file() returns the bare
// SourceFile FileLike only when resolve() doesn't use the original text and
// the (canonical) file name equals the file's own name; otherwise it returns
// an OriginalTextFile or RenamedFile wrapper (diagnosticwriter.go:61-80).
static SourceFile* fileLikeSourceFile(diagnosticwriter::ASTDiagnostic* d) {
	SourceFile* file = d->diagnostic()->file;
	if (file == nullptr || d->resolve().useOriginal) {
		return nullptr;
	}
	std::string_view fileName = file->FileName();
	if (SourceFile* canonical = file->CanonicalSourceFile();
	    canonical != nullptr) {
		fileName = canonical->FileName();
	}
	if (fileName != file->FileName()) {
		return nullptr;
	}
	return file;
}

// checkedASTDiagnostic — Go's `c.(*diagnosticwriter.ASTDiagnostic)`
// panic-on-failure type assertion. Children produced by
// ASTDiagnostic::messageChain/relatedInformation are always ASTDiagnostics.
static diagnosticwriter::ASTDiagnostic* checkedASTDiagnostic(
    diagnosticwriter::Diagnostic* d) {
	auto* a = dynamic_cast<diagnosticwriter::ASTDiagnostic*>(d);
	if (a == nullptr) {
		TSC_UNREACHABLE("non-ASTDiagnostic in diagnostic chain");
	}
	return a;
}

// NewDiagnosticResponse — proto.go:1944. Converts an ast.Diagnostic to a
// DiagnosticResponse.
std::shared_ptr<DiagnosticResponse> NewDiagnosticResponse(Diagnostic* d) {
	return newDiagnosticResponse(diagnosticwriter::wrapASTDiagnostic(d));
}

// newDiagnosticResponse — proto.go:1948.
std::shared_ptr<DiagnosticResponse> newDiagnosticResponse(
    diagnosticwriter::ASTDiagnostic* d) {
	const diagnosticwriter::FileLike* file = d->file();
	int pos = d->pos(), end = d->end();
	if (file != nullptr) {
		int len = static_cast<int>(file->text().size());
		pos = std::max(0, std::min(pos, len));
		end = std::max(pos, std::min(end, len));
	}
	auto resp = std::make_shared<DiagnosticResponse>();
	resp->Pos = pos;
	resp->End = end;
	resp->Code = d->code();
	resp->Category = d->category();
	resp->Source = std::string(d->source());
	resp->Text = d->localize(locale::Default);
	resp->ReportsUnnecessary = d->diagnostic()->ReportsUnnecessary();
	resp->ReportsDeprecated = d->diagnostic()->ReportsDeprecated();

	if (file != nullptr) {
		resp->FileName = std::string(file->fileName());
		if (SourceFile* sourceFile = fileLikeSourceFile(d);
		    sourceFile != nullptr) {
			PositionMap* positionMap = sourceFile->GetPositionMap();
			resp->Pos = positionMap->UTF8ToUTF16(pos);
			resp->End = positionMap->UTF8ToUTF16(end);
		} else {
			resp->Pos = utf16Len(file->text().substr(0, pos));
			resp->End = utf16Len(file->text().substr(0, end));
		}
		auto [startLine, startCharacter] =
		    getECMALineAndUTF16CharacterOfPosition(file, pos);
		auto [endLine, endCharacter] =
		    getECMALineAndUTF16CharacterOfPosition(file, end);
		resp->StartPosition = std::make_shared<DiagnosticPositionResponse>(
		    DiagnosticPositionResponse{startLine, startCharacter});
		resp->EndPosition = std::make_shared<DiagnosticPositionResponse>(
		    DiagnosticPositionResponse{endLine, endCharacter});
		resp->SourceLines = diagnosticSourceLines(file, startLine, endLine);
	}

	if (std::vector<diagnosticwriter::Diagnostic*> chain = d->messageChain();
	    !chain.empty()) {
		resp->MessageChain.resize(chain.size());
		for (size_t i = 0; i < chain.size(); i++) {
			resp->MessageChain[i] =
			    newDiagnosticResponse(checkedASTDiagnostic(chain[i]));
		}
	}

	if (std::vector<diagnosticwriter::Diagnostic*> related =
	        d->relatedInformation();
	    !related.empty()) {
		resp->RelatedInformation.resize(related.size());
		for (size_t i = 0; i < related.size(); i++) {
			resp->RelatedInformation[i] =
			    newDiagnosticResponse(checkedASTDiagnostic(related[i]));
		}
	}

	return resp;
}

// ToDiagnostic — proto.go:2000. Mirrors ast.NewDiagnosticFromText's literal:
// the message text is wrapped in an ad-hoc DiagnosticMessage and file is nil.
Diagnostic* DiagnosticResponse::ToDiagnostic() const {
	auto* d = new Diagnostic();
	d->loc = TextRange{static_cast<TextPos>(Pos), static_cast<TextPos>(End)};
	d->code = Code;
	d->category = Category;
	d->message = NewAdHocMessage(Text);
	d->messageChain =
	    tsc::Map(MessageChain, [](const std::shared_ptr<DiagnosticResponse>& d) {
		    return d->ToDiagnostic();
	    });
	d->relatedInformation = tsc::Map(
	    RelatedInformation, [](const std::shared_ptr<DiagnosticResponse>& d) {
		    return d->ToDiagnostic();
	    });
	d->reportsUnnecessary = ReportsUnnecessary;
	d->reportsDeprecated = ReportsDeprecated;
	return d;
}

// NewDiagnosticResponses — proto.go:2015. Converts a slice of ast.Diagnostics
// to DiagnosticResponses.
std::vector<std::shared_ptr<DiagnosticResponse>> NewDiagnosticResponses(
    std::span<Diagnostic* const> diags) {
	if (diags.empty()) {
		return {};
	}
	std::vector<std::shared_ptr<DiagnosticResponse>> result(diags.size());
	for (size_t i = 0; i < diags.size(); i++) {
		result[i] = NewDiagnosticResponse(diags[i]);
	}
	return result;
}

// DiagnosticResponse::marshalJSONTo — proto.go:1874; the struct has no custom
// marshaler in Go, so this is the tag-ordered field marshal (omitempty/omitzero
// per tag).
std::string DiagnosticResponse::marshalJSONTo(json::Encoder& enc) const {
	objWriter w{enc};
	w.begin();
	if (!FileName.empty()) w.member("fileName", FileName);
	w.member("pos", Pos);
	w.member("end", End);
	if (StartPosition) w.member("startPosition", StartPosition);
	if (EndPosition) w.member("endPosition", EndPosition);
	if (!SourceLines.empty()) w.member("sourceLines", SourceLines);
	w.member("code", Code);
	w.member("category", Category);
	if (!Source.empty()) w.member("source", Source);
	w.member("text", Text);
	if (ReportsUnnecessary) w.member("reportsUnnecessary", ReportsUnnecessary);
	if (ReportsDeprecated) w.member("reportsDeprecated", ReportsDeprecated);
	if (!MessageChain.empty()) w.member("messageChain", MessageChain);
	if (!RelatedInformation.empty())
		w.member("relatedInformation", RelatedInformation);
	return w.end();
}

}  // namespace tsc::api
