// Port of tsc/internal/contentmapper/contentmapper.go.
#include "internal/contentmapper/contentmapper.h"

#include <unordered_set>

namespace tsc::contentmapper {

// supportedVirtualExtensions — contentmapper.go:58.
static const std::unordered_set<std::string_view> supportedVirtualExtensions = {
	".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx", ".mts", ".cts", ".json",
};

// IsSupportedVirtualExtension — contentmapper.go:62.
bool IsSupportedVirtualExtension(std::string_view extension) {
	return supportedVirtualExtensions.contains(extension);
}

// Mapper.DiagnosticName — contentmapper.go:67.
// Mapper::Equals — contentmapper.go:99.
bool Mapper::Equals(const Mapper* other) const {
	if (this == other) {
		return true;
	}
	if (other == nullptr) {
		return false;
	}
	return Definition.Package == other->Definition.Package &&
	       Definition.Extensions == other->Definition.Extensions &&
	       Definition.Options == other->Definition.Options &&
	       Manifest.Name == other->Manifest.Name &&
	       Manifest.Version == other->Manifest.Version &&
	       Manifest.Exec == other->Manifest.Exec &&
	       Manifest.CompilerOptions == other->Manifest.CompilerOptions &&
	       Manifest.DynamicConfig == other->Manifest.DynamicConfig &&
	       PackageDirectory == other->PackageDirectory &&
	       ContributionID == other->ContributionID;
}

std::string Mapper::DiagnosticName() const {
	if (!Manifest.Name.empty()) {
		return Manifest.Name;
	}
	if (!Definition.Package.empty()) {
		return Definition.Package;
	}
	return ContributionID;
}

// Mapper.Identity — contentmapper.go:80.
std::string Mapper::Identity() const {
	if (!ContributionID.empty()) {
		return ContributionID + " (" + manifestIdentity() + ")";
	}
	return manifestIdentity();
}

// Mapper.manifestIdentity — contentmapper.go:87.
std::string Mapper::manifestIdentity() const {
	if (Manifest.Name.empty()) {
		return "";
	}
	if (Manifest.Version.empty()) {
		return Manifest.Name;
	}
	return Manifest.Name + "@" + Manifest.Version;
}

// Mapper.TransformIdentity — contentmapper.go:104.
xxh3::Uint128 Mapper::TransformIdentity(const CompilerOptions* options) const {
	auto [declared, err] = MarshalDeclaredOptions(options);
	std::string optionsJSON = "null";
	if (err == nullptr) {
		// json.Marshal(*OrderedMap[string, json.Value]) — object in insertion
		// order.
		std::vector<std::pair<std::string, json::Value>> members;
		for (const auto& name : declared.Keys()) {
			members.emplace_back(name, declared.GetOrZero(name));
		}
		optionsJSON = json::marshalObject(members);
	}
	const std::string identity = Identity();
	std::string buf;
	buf.reserve(identity.size() + 2 + Definition.Options.size() +
	            optionsJSON.size());
	buf += identity;
	buf += '\0';
	buf += Definition.Options;
	buf += '\0';
	buf += optionsJSON;
	return xxh3::hash128(buf);
}

namespace {

// One CompilerOptions field for MarshalDeclaredOptions: whether the field is
// zero (reflect.IsZero) and its json.Marshal output.
struct optionField {
	bool (*isZero)(const CompilerOptions*);
	json::Value (*marshal)(const CompilerOptions*);
};

json::Value marshalTristate(Tristate v) {
	switch (v) {
	case Tristate::True:
		return "true";
	case Tristate::False:
		return "false";
	default:
		return "null";
	}
}

json::Value marshalStringVec(const std::vector<std::string>& v) {
	std::vector<json::Value> items;
	items.reserve(v.size());
	for (const auto& s : v) {
		items.push_back(json::marshalString(s));
	}
	return json::marshalArray(items);
}

template <Tristate CompilerOptions::*M>
optionField tristateField() {
	return {[](const CompilerOptions* o) { return o->*M == Tristate::Unknown; },
	        [](const CompilerOptions* o) { return marshalTristate(o->*M); }};
}
template <std::string CompilerOptions::*M>
optionField stringField() {
	return {[](const CompilerOptions* o) { return (o->*M).empty(); },
	        [](const CompilerOptions* o) { return json::marshalString(o->*M); }};
}
template <std::vector<std::string> CompilerOptions::*M>
optionField stringVecField() {
	// reflect.IsZero on a []string is len==0 (nil or empty slice — the C++
	// vector cannot distinguish, which is equivalent for marshaling).
	return {[](const CompilerOptions* o) { return (o->*M).empty(); },
	        [](const CompilerOptions* o) { return marshalStringVec(o->*M); }};
}
template <typename T, T CompilerOptions::*M>
optionField enumField() {
	return {[](const CompilerOptions* o) { return o->*M == T(0); },
	        [](const CompilerOptions* o) {
	            return json::marshalInt64(static_cast<int32_t>(o->*M));
	        }};
}
template <int* CompilerOptions::*M>
optionField intPtrField() {
	return {[](const CompilerOptions* o) { return o->*M == nullptr; },
	        [](const CompilerOptions* o) {
	            return json::marshalInt64(*(o->*M));
	        }};
}
optionField pathsField() {
	return {
	    [](const CompilerOptions* o) { return o->Paths.empty(); },
	    [](const CompilerOptions* o) {
	        // *collections.OrderedMap[string, []string] → JSON object.
	        std::vector<std::pair<std::string, json::Value>> members;
	        members.reserve(o->Paths.size());
	        for (const auto& kv : o->Paths) {
	            members.emplace_back(kv.first, marshalStringVec(kv.second));
	        }
	        return json::marshalObject(members);
	    }};
}
optionField pluginsField() {
	return {
	    [](const CompilerOptions* o) { return o->Plugins.empty(); },
	    [](const CompilerOptions* o) {
	        std::vector<json::Value> items;
	        items.reserve(o->Plugins.size());
	        for (const auto& p : o->Plugins) {
	            items.push_back(json::marshalObject(
	                {{"name", json::marshalString(p.name)}}));
	        }
	        return json::marshalArray(items);
	    }};
}

// compilerOptionFields — contentmapper.go:145: each CompilerOptions option
// name (its json tag) mapped to its field accessor, in Go struct-field order
// (json.Marshal emits declaration order). Built once (sync.OnceValue).
const std::vector<std::pair<std::string_view, optionField>>&
compilerOptionFields() {
	static const auto* fields =
	    new std::vector<std::pair<std::string_view, optionField>>{
	    {"allowJs", tristateField<&CompilerOptions::AllowJs>()},
	    {"allowArbitraryExtensions", tristateField<&CompilerOptions::AllowArbitraryExtensions>()},
	    {"allowImportingTsExtensions", tristateField<&CompilerOptions::AllowImportingTsExtensions>()},
	    {"allowNonTsExtensions", tristateField<&CompilerOptions::AllowNonTsExtensions>()},
	    {"allowUmdGlobalAccess", tristateField<&CompilerOptions::AllowUmdGlobalAccess>()},
	    {"allowUnreachableCode", tristateField<&CompilerOptions::AllowUnreachableCode>()},
	    {"allowUnusedLabels", tristateField<&CompilerOptions::AllowUnusedLabels>()},
	    {"assumeChangesOnlyAffectDirectDependencies", tristateField<&CompilerOptions::AssumeChangesOnlyAffectDirectDependencies>()},
	    {"checkJs", tristateField<&CompilerOptions::CheckJs>()},
	    {"customConditions", stringVecField<&CompilerOptions::CustomConditions>()},
	    {"composite", tristateField<&CompilerOptions::Composite>()},
	    {"emitDeclarationOnly", tristateField<&CompilerOptions::EmitDeclarationOnly>()},
	    {"emitBOM", tristateField<&CompilerOptions::EmitBOM>()},
	    {"emitDecoratorMetadata", tristateField<&CompilerOptions::EmitDecoratorMetadata>()},
	    {"declaration", tristateField<&CompilerOptions::Declaration>()},
	    {"declarationDir", stringField<&CompilerOptions::DeclarationDir>()},
	    {"declarationMap", tristateField<&CompilerOptions::DeclarationMap>()},
	    {"deduplicatePackages", tristateField<&CompilerOptions::DeduplicatePackages>()},
	    {"disableSizeLimit", tristateField<&CompilerOptions::DisableSizeLimit>()},
	    {"disableSourceOfProjectReferenceRedirect", tristateField<&CompilerOptions::DisableSourceOfProjectReferenceRedirect>()},
	    {"disableSolutionSearching", tristateField<&CompilerOptions::DisableSolutionSearching>()},
	    {"disableReferencedProjectLoad", tristateField<&CompilerOptions::DisableReferencedProjectLoad>()},
	    {"erasableSyntaxOnly", tristateField<&CompilerOptions::ErasableSyntaxOnly>()},
	    {"exactOptionalPropertyTypes", tristateField<&CompilerOptions::ExactOptionalPropertyTypes>()},
	    {"experimentalDecorators", tristateField<&CompilerOptions::ExperimentalDecorators>()},
	    {"forceConsistentCasingInFileNames", tristateField<&CompilerOptions::ForceConsistentCasingInFileNames>()},
	    {"isolatedModules", tristateField<&CompilerOptions::IsolatedModules>()},
	    {"isolatedDeclarations", tristateField<&CompilerOptions::IsolatedDeclarations>()},
	    {"ignoreConfig", tristateField<&CompilerOptions::IgnoreConfig>()},
	    {"ignoreDeprecations", stringField<&CompilerOptions::IgnoreDeprecations>()},
	    {"importHelpers", tristateField<&CompilerOptions::ImportHelpers>()},
	    {"inlineSourceMap", tristateField<&CompilerOptions::InlineSourceMap>()},
	    {"inlineSources", tristateField<&CompilerOptions::InlineSources>()},
	    {"init", tristateField<&CompilerOptions::Init>()},
	    {"incremental", tristateField<&CompilerOptions::Incremental>()},
	    {"jsx", enumField<JsxEmit, &CompilerOptions::Jsx>()},
	    {"jsxFactory", stringField<&CompilerOptions::JsxFactory>()},
	    {"jsxFragmentFactory", stringField<&CompilerOptions::JsxFragmentFactory>()},
	    {"jsxImportSource", stringField<&CompilerOptions::JsxImportSource>()},
	    {"lib", stringVecField<&CompilerOptions::Lib>()},
	    {"libReplacement", tristateField<&CompilerOptions::LibReplacement>()},
	    {"locale", stringField<&CompilerOptions::Locale>()},
	    {"mapRoot", stringField<&CompilerOptions::MapRoot>()},
	    {"module", enumField<ModuleKind, &CompilerOptions::Module>()},
	    {"moduleResolution", enumField<ModuleResolutionKind, &CompilerOptions::ModuleResolution>()},
	    {"moduleSuffixes", stringVecField<&CompilerOptions::ModuleSuffixes>()},
	    {"moduleDetection", enumField<ModuleDetectionKind, &CompilerOptions::ModuleDetection>()},
	    {"newLine", enumField<NewLineKind, &CompilerOptions::NewLine>()},
	    {"noEmit", tristateField<&CompilerOptions::NoEmit>()},
	    {"noCheck", tristateField<&CompilerOptions::NoCheck>()},
	    {"noErrorTruncation", tristateField<&CompilerOptions::NoErrorTruncation>()},
	    {"noFallthroughCasesInSwitch", tristateField<&CompilerOptions::NoFallthroughCasesInSwitch>()},
	    {"noImplicitAny", tristateField<&CompilerOptions::NoImplicitAny>()},
	    {"noImplicitThis", tristateField<&CompilerOptions::NoImplicitThis>()},
	    {"noImplicitReturns", tristateField<&CompilerOptions::NoImplicitReturns>()},
	    {"noEmitHelpers", tristateField<&CompilerOptions::NoEmitHelpers>()},
	    {"noLib", tristateField<&CompilerOptions::NoLib>()},
	    {"noPropertyAccessFromIndexSignature", tristateField<&CompilerOptions::NoPropertyAccessFromIndexSignature>()},
	    {"noUncheckedIndexedAccess", tristateField<&CompilerOptions::NoUncheckedIndexedAccess>()},
	    {"noEmitOnError", tristateField<&CompilerOptions::NoEmitOnError>()},
	    {"noUnusedLocals", tristateField<&CompilerOptions::NoUnusedLocals>()},
	    {"noUnusedParameters", tristateField<&CompilerOptions::NoUnusedParameters>()},
	    {"noResolve", tristateField<&CompilerOptions::NoResolve>()},
	    {"noImplicitOverride", tristateField<&CompilerOptions::NoImplicitOverride>()},
	    {"noUncheckedSideEffectImports", tristateField<&CompilerOptions::NoUncheckedSideEffectImports>()},
	    {"outDir", stringField<&CompilerOptions::OutDir>()},
	    {"paths", pathsField()},
	    {"plugins", pluginsField()},
	    {"preserveConstEnums", tristateField<&CompilerOptions::PreserveConstEnums>()},
	    {"preserveSymlinks", tristateField<&CompilerOptions::PreserveSymlinks>()},
	    {"project", stringField<&CompilerOptions::Project>()},
	    {"resolveJsonModule", tristateField<&CompilerOptions::ResolveJsonModule>()},
	    {"resolvePackageJsonExports", tristateField<&CompilerOptions::ResolvePackageJsonExports>()},
	    {"resolvePackageJsonImports", tristateField<&CompilerOptions::ResolvePackageJsonImports>()},
	    {"removeComments", tristateField<&CompilerOptions::RemoveComments>()},
	    {"rewriteRelativeImportExtensions", tristateField<&CompilerOptions::RewriteRelativeImportExtensions>()},
	    {"reactNamespace", stringField<&CompilerOptions::ReactNamespace>()},
	    {"rootDir", stringField<&CompilerOptions::RootDir>()},
	    {"rootDirs", stringVecField<&CompilerOptions::RootDirs>()},
	    {"skipLibCheck", tristateField<&CompilerOptions::SkipLibCheck>()},
	    {"stableTypeOrdering", tristateField<&CompilerOptions::StableTypeOrdering>()},
	    {"strict", tristateField<&CompilerOptions::Strict>()},
	    {"strictBindCallApply", tristateField<&CompilerOptions::StrictBindCallApply>()},
	    {"strictBuiltinIteratorReturn", tristateField<&CompilerOptions::StrictBuiltinIteratorReturn>()},
	    {"strictFunctionTypes", tristateField<&CompilerOptions::StrictFunctionTypes>()},
	    {"strictNullChecks", tristateField<&CompilerOptions::StrictNullChecks>()},
	    {"strictPropertyInitialization", tristateField<&CompilerOptions::StrictPropertyInitialization>()},
	    {"stripInternal", tristateField<&CompilerOptions::StripInternal>()},
	    {"skipDefaultLibCheck", tristateField<&CompilerOptions::SkipDefaultLibCheck>()},
	    {"sourceMap", tristateField<&CompilerOptions::SourceMap>()},
	    {"sourceRoot", stringField<&CompilerOptions::SourceRoot>()},
	    {"suppressOutputPathCheck", tristateField<&CompilerOptions::SuppressOutputPathCheck>()},
	    {"target", enumField<ScriptTarget, &CompilerOptions::Target>()},
	    {"traceResolution", tristateField<&CompilerOptions::TraceResolution>()},
	    {"tsBuildInfoFile", stringField<&CompilerOptions::TsBuildInfoFile>()},
	    {"typeRoots", stringVecField<&CompilerOptions::TypeRoots>()},
	    {"types", stringVecField<&CompilerOptions::Types>()},
	    {"useDefineForClassFields", tristateField<&CompilerOptions::UseDefineForClassFields>()},
	    {"useUnknownInCatchVariables", tristateField<&CompilerOptions::UseUnknownInCatchVariables>()},
	    {"verbatimModuleSyntax", tristateField<&CompilerOptions::VerbatimModuleSyntax>()},
	    {"maxNodeModuleJsDepth", intPtrField<&CompilerOptions::MaxNodeModuleJsDepth>()},
	    {"allowSyntheticDefaultImports", tristateField<&CompilerOptions::AllowSyntheticDefaultImports>()},
	    {"alwaysStrict", tristateField<&CompilerOptions::AlwaysStrict>()},
	    {"baseUrl", stringField<&CompilerOptions::BaseUrl>()},
	    {"downlevelIteration", tristateField<&CompilerOptions::DownlevelIteration>()},
	    {"esModuleInterop", tristateField<&CompilerOptions::ESModuleInterop>()},
	    {"outFile", stringField<&CompilerOptions::OutFile>()},
	    {"configFilePath", stringField<&CompilerOptions::ConfigFilePath>()},
	    {"noDtsResolution", tristateField<&CompilerOptions::NoDtsResolution>()},
	    {"pathsBasePath", stringField<&CompilerOptions::PathsBasePath>()},
	    {"diagnostics", tristateField<&CompilerOptions::Diagnostics>()},
	    {"extendedDiagnostics", tristateField<&CompilerOptions::ExtendedDiagnostics>()},
	    {"generateCpuProfile", stringField<&CompilerOptions::GenerateCpuProfile>()},
	    {"generateTrace", stringField<&CompilerOptions::GenerateTrace>()},
	    {"listEmittedFiles", tristateField<&CompilerOptions::ListEmittedFiles>()},
	    {"listFiles", tristateField<&CompilerOptions::ListFiles>()},
	    {"explainFiles", tristateField<&CompilerOptions::ExplainFiles>()},
	    {"listFilesOnly", tristateField<&CompilerOptions::ListFilesOnly>()},
	    {"noEmitForJsFiles", tristateField<&CompilerOptions::NoEmitForJsFiles>()},
	    {"preserveWatchOutput", tristateField<&CompilerOptions::PreserveWatchOutput>()},
	    {"pretty", tristateField<&CompilerOptions::Pretty>()},
	    {"version", tristateField<&CompilerOptions::Version>()},
	    {"watch", tristateField<&CompilerOptions::Watch>()},
	    {"showConfig", tristateField<&CompilerOptions::ShowConfig>()},
	    {"build", tristateField<&CompilerOptions::Build>()},
	    {"help", tristateField<&CompilerOptions::Help>()},
	    {"all", tristateField<&CompilerOptions::All>()},
	    {"runExternalCode", tristateField<&CompilerOptions::RunExternalCode>()},
	    {"pprofDir", stringField<&CompilerOptions::PprofDir>()},
	    {"singleThreaded", tristateField<&CompilerOptions::SingleThreaded>()},
	    {"quiet", tristateField<&CompilerOptions::Quiet>()},
	    {"checkers", intPtrField<&CompilerOptions::Checkers>()},
	};
	return *fields;
}

} // namespace

// Mapper.MarshalDeclaredOptions — contentmapper.go:119.
std::pair<collections::OrderedMap<std::string, json::Value>, gostd::Error>
Mapper::MarshalDeclaredOptions(const CompilerOptions* options) const {
	collections::OrderedMap<std::string, json::Value> out;
	if (options == nullptr || Manifest.CompilerOptions.empty()) {
		return {out, nullptr};
	}
	const auto& fields = compilerOptionFields();
	for (const auto& name : Manifest.CompilerOptions) {
		const optionField* field = nullptr;
		for (const auto& [key, f] : fields) {
			if (key == name) {
				field = &f;
				break;
			}
		}
		if (field == nullptr) {
			continue;
		}
		if (field->isZero(options)) {
			continue;
		}
		out.Set(name, field->marshal(options));
	}
	return {out, nullptr};
}

namespace detail {

// json.Marshal(*CompilerOptions) — emits each non-zero field.
json::Value marshalCompilerOptions(const CompilerOptions* options) {
	if (options == nullptr) {
		return "null";
	}
	std::vector<std::pair<std::string, json::Value>> members;
	for (const auto& [name, field] : compilerOptionFields()) {
		if (field.isZero(options)) {
			continue;
		}
		members.emplace_back(name, field.marshal(options));
	}
	return json::marshalObject(members);
}

} // namespace detail

} // namespace tsc::contentmapper
