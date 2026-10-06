// Port of small enums from tsc/internal/core (languagevariant.go, scriptkind.go,
// compileroptions.go bits needed by the front end).
#pragma once

#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/tspath/tspath.h"

// === slice: api === — forward decls for encoding/json integration used by
// the api slice's proto marshalers.
namespace tsc::json { class Decoder; class Encoder; }

namespace tsc {

enum class LanguageVariant : int32_t {
	Standard = 0,
	JSX = 1,
};

enum class ScriptKind : int32_t {
	Unknown = 0,
	JS = 1,
	JSX = 2,
	TS = 3,
	TSX = 4,
	JSON = 6,
	Deferred = 7,
};

// core.go: GetScriptKindFromFileName / EnsureScriptKindFromFileName
inline ScriptKind getScriptKindFromFileName(std::string_view fileName) {
	auto dotPos = fileName.rfind('.');
	if (dotPos != std::string_view::npos) {
		std::string ext(fileName.substr(dotPos));
		for (auto& c : ext)
			c = static_cast<char>(std::tolower((unsigned char)c));
		if (ext == ".js" || ext == ".cjs" || ext == ".mjs")
			return ScriptKind::JS;
		if (ext == ".jsx") return ScriptKind::JSX;
		if (ext == ".ts" || ext == ".cts" || ext == ".mts")
			return ScriptKind::TS;
		if (ext == ".tsx") return ScriptKind::TSX;
		if (ext == ".json") return ScriptKind::JSON;
	}
	return ScriptKind::Unknown;
}

inline ScriptKind ensureScriptKindFromFileName(std::string_view fileName) {
	if (auto kind = getScriptKindFromFileName(fileName);
	    kind != ScriptKind::Unknown) {
		return kind;
	}
	return ScriptKind::TS;
}

enum class ScriptTarget : int32_t {
	None = 0,
	ES5 = 1,
	ES2015 = 2,
	ES2016 = 3,
	ES2017 = 4,
	ES2018 = 5,
	ES2019 = 6,
	ES2020 = 7,
	ES2021 = 8,
	ES2022 = 9,
	ES2023 = 10,
	ES2024 = 11,
	ES2025 = 12,
	ES2026 = 13,
	ESNext = 99,
	JSON = 100,
	Latest = ESNext,
	LatestStandard = ES2026,
};

enum class Tristate : int32_t {
	// tristate.go — Go order; zero value is Unknown so zero-init options
	// match the oracle.
	Unknown = 0,
	False = 1,
	True = 2,
};

inline constexpr bool tristateIsTrue(Tristate t) { return t == Tristate::True; }
inline constexpr bool tristateIsFalse(Tristate t) { return t == Tristate::False; }
inline constexpr bool tristateIsTrueOrUnknown(Tristate t) { return t != Tristate::False; }
inline constexpr bool tristateIsFalseOrUnknown(Tristate t) { return t != Tristate::True; }

// === slice: api ===
// Tristate.UnmarshalJSON (tristate.go:42) / Tristate.MarshalJSON (tristate.go:52)
// — ADL hooks picked up by json::unmarshalDecode/marshalInto.
std::string unmarshalJSONFrom(json::Decoder& dec, Tristate* out);
std::string marshalJSONTo(json::Encoder& enc, const Tristate& v);

enum class ModuleKind : int32_t {
	None = 0,
	CommonJS = 1,
	AMD = 2,    // Deprecated
	UMD = 3,    // Deprecated
	System = 4, // Deprecated
	// ES module kinds are contiguous: ES2015 (earliest) .. ESNext (last).
	ES2015 = 5,
	ES2020 = 6,
	ES2022 = 7,
	ESNext = 99,
	// Node16+ is an amalgam of commonjs and es2022+.
	Node16 = 100,
	Node18 = 101,
	Node20 = 102,
	NodeNext = 199,
	Preserve = 200, // Emit as written
};

inline constexpr bool moduleKindIsNonNodeESM(ModuleKind k) {
	return k >= ModuleKind::ES2015 && k <= ModuleKind::ESNext;
}
inline constexpr bool moduleKindSupportsImportAttributes(ModuleKind k) {
	return (k >= ModuleKind::Node18 && k <= ModuleKind::NodeNext) ||
		   k == ModuleKind::Preserve || k == ModuleKind::ESNext;
}

enum class ModuleResolutionKind : int32_t {
	Unknown = 0,
	Classic = 1, // Deprecated
	Node10 = 2,  // Deprecated
	Node16 = 3,
	NodeNext = 99,  // Not Node16: compiled code can distinguish "Next" reliably
	Bundler = 100,
};

enum class ModuleDetectionKind : int32_t {
	None = 0,
	Auto = 1,
	Legacy = 2,
	Force = 3,
};

enum class JsxEmit : int32_t {
	None = 0,
	Preserve = 1,
	React = 2,
	ReactNative = 3,
	ReactJSX = 4,
	ReactJSXDev = 5,
};

enum class NewLineKind : int32_t {
	// === slice: tsoptions === — numbered to match Go NewLineKind (compileroptions.go:485).
	None = 0,
	CarriageReturnLineFeed = 1,
	LineFeed = 2,
};

enum class EmitFlags : uint32_t;

using ResolutionMode = ModuleKind; // ModuleKindNone | ModuleKindCommonJS | ModuleKindESNext
inline constexpr ResolutionMode ResolutionModeNone = ModuleKind::None;
inline constexpr ResolutionMode ResolutionModeCommonJS = ModuleKind::CommonJS;
inline constexpr ResolutionMode ResolutionModeESM = ModuleKind::ESNext;

// CompilerOptions — port of tsc/internal/core/compileroptions.go.
struct PluginImport {
	std::string name;

	// === slice: api ===
	// encoding/json Marshal/Unmarshal (Name is `json:"name"`).
	std::string unmarshalJSONFrom(json::Decoder& dec);
	std::string marshalJSONTo(json::Encoder& enc) const;
};

struct CompilerOptions {
	Tristate AllowJs{};
	Tristate AllowArbitraryExtensions{};
	Tristate AllowImportingTsExtensions{};
	Tristate AllowNonTsExtensions{};
	Tristate AllowUmdGlobalAccess{};
	Tristate AllowUnreachableCode{};
	Tristate AllowUnusedLabels{};
	Tristate AssumeChangesOnlyAffectDirectDependencies{};
	Tristate CheckJs{};
	std::vector<std::string> CustomConditions;
	Tristate Composite{};
	Tristate EmitDeclarationOnly{};
	Tristate EmitBOM{};
	Tristate EmitDecoratorMetadata{};
	Tristate Declaration{};
	std::string DeclarationDir;
	Tristate DeclarationMap{};
	Tristate DeduplicatePackages{};
	Tristate DisableSizeLimit{};
	Tristate DisableSourceOfProjectReferenceRedirect{};
	Tristate DisableSolutionSearching{};
	Tristate DisableReferencedProjectLoad{};
	Tristate ErasableSyntaxOnly{};
	Tristate ExactOptionalPropertyTypes{};
	Tristate ExperimentalDecorators{};
	Tristate ForceConsistentCasingInFileNames{};
	Tristate IsolatedModules{};
	Tristate IsolatedDeclarations{};
	Tristate IgnoreConfig{};
	std::string IgnoreDeprecations;
	Tristate ImportHelpers{};
	Tristate InlineSourceMap{};
	Tristate InlineSources{};
	Tristate Init{};
	Tristate Incremental{};
	JsxEmit Jsx{};
	std::string JsxFactory;
	std::string JsxFragmentFactory;
	std::string JsxImportSource;
	std::vector<std::string> Lib;
	Tristate LibReplacement{};
	std::string Locale;
	std::string MapRoot;
	ModuleKind Module{};
	ModuleResolutionKind ModuleResolution{};
	std::vector<std::string> ModuleSuffixes;
	ModuleDetectionKind ModuleDetection{};
	NewLineKind NewLine{};
	Tristate NoEmit{};
	Tristate NoCheck{};
	Tristate NoErrorTruncation{};
	Tristate NoFallthroughCasesInSwitch{};
	Tristate NoImplicitAny{};
	Tristate NoImplicitThis{};
	Tristate NoImplicitReturns{};
	Tristate NoEmitHelpers{};
	Tristate NoLib{};
	Tristate NoPropertyAccessFromIndexSignature{};
	Tristate NoUncheckedIndexedAccess{};
	Tristate NoEmitOnError{};
	Tristate NoUnusedLocals{};
	Tristate NoUnusedParameters{};
	Tristate NoResolve{};
	Tristate NoImplicitOverride{};
	Tristate NoUncheckedSideEffectImports{};
	std::string OutDir;
	std::vector<std::pair<std::string, std::vector<std::string>>> Paths;
	std::vector<PluginImport> Plugins;
	Tristate PreserveConstEnums{};
	Tristate PreserveSymlinks{};
	std::string Project;
	Tristate ResolveJsonModule{};
	Tristate ResolvePackageJsonExports{};
	Tristate ResolvePackageJsonImports{};
	Tristate RemoveComments{};
	Tristate RewriteRelativeImportExtensions{};
	std::string ReactNamespace;
	std::string RootDir;
	std::vector<std::string> RootDirs;
	Tristate SkipLibCheck{};
	Tristate StableTypeOrdering{};
	Tristate Strict{};
	Tristate StrictBindCallApply{};
	Tristate StrictBuiltinIteratorReturn{};
	Tristate StrictFunctionTypes{};
	Tristate StrictNullChecks{};
	Tristate StrictPropertyInitialization{};
	Tristate StripInternal{};
	Tristate SkipDefaultLibCheck{};
	Tristate SourceMap{};
	std::string SourceRoot;
	Tristate SuppressOutputPathCheck{};
	ScriptTarget Target{};
	Tristate TraceResolution{};
	std::string TsBuildInfoFile;
	std::vector<std::string> TypeRoots;
	std::vector<std::string> Types;
	Tristate UseDefineForClassFields{};
	Tristate UseUnknownInCatchVariables{};
	Tristate VerbatimModuleSyntax{};
	int* MaxNodeModuleJsDepth{};

	// Deprecated: Do not use outside of options parsing and validation.
	Tristate AllowSyntheticDefaultImports{};
	Tristate AlwaysStrict{};
	std::string BaseUrl;
	Tristate DownlevelIteration{};
	Tristate ESModuleInterop{};
	std::string OutFile;

	// Internal fields
	std::string ConfigFilePath;
	Tristate NoDtsResolution{};
	std::string PathsBasePath;
	Tristate Diagnostics{};
	Tristate ExtendedDiagnostics{};
	std::string GenerateCpuProfile;
	std::string GenerateTrace;
	Tristate ListEmittedFiles{};
	Tristate ListFiles{};
	Tristate ExplainFiles{};
	Tristate ListFilesOnly{};
	Tristate NoEmitForJsFiles{};
	Tristate PreserveWatchOutput{};
	Tristate Pretty{};
	Tristate Version{};
	Tristate Watch{};
	Tristate ShowConfig{};
	Tristate Build{};
	Tristate Help{};
	Tristate All{};
	Tristate RunExternalCode{};
	std::string PprofDir;
	Tristate SingleThreaded{};
	Tristate Quiet{};
	int* Checkers{};

	ScriptTarget GetEmitScriptTarget() const {
		if (Target != ScriptTarget::None) {
			return Target;
		}
		return ScriptTarget::LatestStandard;
	}

	ModuleKind GetEmitModuleKind() const {
		if (Module != ModuleKind::None) {
			return Module;
		}
		ScriptTarget target = GetEmitScriptTarget();
		if (target == ScriptTarget::ESNext) {
			return ModuleKind::ESNext;
		}
		if (target >= ScriptTarget::ES2022) {
			return ModuleKind::ES2022;
		}
		if (target >= ScriptTarget::ES2020) {
			return ModuleKind::ES2020;
		}
		if (target >= ScriptTarget::ES2015) {
			return ModuleKind::ES2015;
		}
		return ModuleKind::CommonJS;
	}

	ModuleResolutionKind GetModuleResolutionKind() const {
		switch (ModuleResolution) {
		case ModuleResolutionKind::Unknown:
		case ModuleResolutionKind::Classic:
		case ModuleResolutionKind::Node10: {
			switch (GetEmitModuleKind()) {
			case ModuleKind::Node16:
			case ModuleKind::Node18:
			case ModuleKind::Node20:
				return ModuleResolutionKind::Node16;
			case ModuleKind::NodeNext:
				return ModuleResolutionKind::NodeNext;
			default:
				return ModuleResolutionKind::Bundler;
			}
		}
		default:
			return ModuleResolution;
		}
	}

	ModuleDetectionKind GetEmitModuleDetectionKind() const {
		if (ModuleDetection != ModuleDetectionKind::None) {
			return ModuleDetection;
		}
		ModuleKind moduleKind = GetEmitModuleKind();
		if (moduleKind >= ModuleKind::Node16 && moduleKind <= ModuleKind::NodeNext) {
			return ModuleDetectionKind::Force;
		}
		return ModuleDetectionKind::Auto;
	}

	bool GetResolvePackageJsonExports() const {
		return tristateIsTrueOrUnknown(ResolvePackageJsonExports);
	}
	bool GetResolvePackageJsonImports() const {
		return tristateIsTrueOrUnknown(ResolvePackageJsonImports);
	}
	bool GetAllowImportingTsExtensions() const {
		return tristateIsTrue(AllowImportingTsExtensions) ||
			   tristateIsTrue(RewriteRelativeImportExtensions);
	}
	bool GetResolveJsonModule() const {
		if (ResolveJsonModule != Tristate::Unknown) {
			return ResolveJsonModule == Tristate::True;
		}
		switch (GetEmitModuleKind()) {
		case ModuleKind::Node20:
		case ModuleKind::NodeNext:
			return true;
		default:;
		}
		return GetModuleResolutionKind() == ModuleResolutionKind::Bundler;
	}
	bool ShouldPreserveConstEnums() const {
		return PreserveConstEnums == Tristate::True || GetIsolatedModules();
	}
	bool GetAllowJS() const {
		if (AllowJs != Tristate::Unknown) {
			return AllowJs == Tristate::True;
		}
		return CheckJs == Tristate::True;
	}
	// compileroptions.go: AllowImportingTsExtensionsFrom — the tspath check
	// lives in the caller so this header doesn't depend on tspath.
	bool AllowImportingTsExtensionsFrom(bool isDeclarationFile) const {
		return GetAllowImportingTsExtensions() || isDeclarationFile;
	}
	bool DefaultIfUnknown(Tristate value, Tristate defaultValue) const {
		return tristateIsTrue(value != Tristate::Unknown ? value
		                                                : defaultValue);
	}
	bool GetJSXTransformEnabled() const {
		return Jsx == JsxEmit::React || Jsx == JsxEmit::ReactJSX || Jsx == JsxEmit::ReactJSXDev;
	}
	bool GetStrictOptionValue(Tristate value) const {
		if (value != Tristate::Unknown) {
			return value == Tristate::True;
		}
		return Strict != Tristate::False;
	}
	bool GetIsolatedModules() const {
		return IsolatedModules == Tristate::True || VerbatimModuleSyntax == Tristate::True;
	}
	bool IsIncremental() const {
		return tristateIsTrue(Incremental) || tristateIsTrue(Composite);
	}
	// === slice: testutil ===
	// Clone — compileroptions.go:183. Shallow copy like Go's field copy.
	CompilerOptions* Clone() const { return new CompilerOptions(*this); }
	bool GetEmitStandardClassFields() const {
		return UseDefineForClassFields != Tristate::False &&
			   GetEmitScriptTarget() >= ScriptTarget::ES2022;
	}
	bool GetUseDefineForClassFields() const {
		if (UseDefineForClassFields == Tristate::Unknown) {
			return GetEmitScriptTarget() >= ScriptTarget::ES2022;
		}
		return UseDefineForClassFields == Tristate::True;
	}
	bool GetEmitDeclarations() const {
		return tristateIsTrue(Declaration) || tristateIsTrue(Composite);
	}
	// compileroptions.go:305 GetEffectiveTypeRoots
	std::pair<std::vector<std::string>, bool> GetEffectiveTypeRoots(
	    const std::string& currentDirectory) const {
		if (!TypeRoots.empty()) {
			return {TypeRoots, true};
		}
		std::string baseDir;
		if (!ConfigFilePath.empty()) {
			baseDir = tspath::getDirectoryPath(ConfigFilePath);
		} else {
			baseDir = currentDirectory;
			if (baseDir.empty()) {
				// Go panics here; unreachable for `tsc --noEmit <file>`.
			}
		}
		std::vector<std::string> typeRoots;
		tspath::forEachAncestorDirectory<bool>(
		    baseDir, [&](std::string_view dir) -> std::pair<bool, bool> {
			    typeRoots.push_back(
			        tspath::combinePaths(dir,
			                             {"node_modules", "@types"}));
			    return {false, false};
		    });
		return {typeRoots, false};
	}
	bool GetAreDeclarationMapsEnabled() const {
		return DeclarationMap == Tristate::True && GetEmitDeclarations();
	}
	bool HasJsonModuleEmitEnabled() const {
		switch (GetEmitModuleKind()) {
		case ModuleKind::System:
		case ModuleKind::UMD:
			return false;
		default:;
		}
		return true;
	}
	bool UsesWildcardTypes() const {
		for (auto& t : Types) {
			if (t == "*") {
				return true;
			}
		}
		return false;
	}

	// === slice: module ===
	// compileroptions.go — implementations live in core/compileroptions.cpp
	// since they need tspath.
	// GetEffectiveTypeRoots — (result, fromConfig).
	std::pair<std::vector<std::string>, bool> GetEffectiveTypeRoots(
	    std::string_view currentDirectory) const;
	// GetPathsBasePath — "" when Paths is unset.
	std::string GetPathsBasePath(std::string_view currentDirectory) const;

	// === slice: api ===
	// encoding/json Marshal/Unmarshal over the `json:`-tagged fields
	// (all fields except the noCopy marker; internal fields are tagged and
	// marshaled too, matching encoding/json). Implemented in api/proto.cpp.
	std::string unmarshalJSONFrom(json::Decoder& dec);
	std::string marshalJSONTo(json::Encoder& enc) const;
};

// === slice: tsoptions ===

// watchoptions.go
enum class WatchFileKind : int32_t {
	None = 0,
	FixedPollingInterval = 1,
	PriorityPollingInterval = 2,
	DynamicPriorityPolling = 3,
	FixedChunkSizePolling = 4,
	UseFsEvents = 5,
	UseFsEventsOnParentDirectory = 6,
};

enum class WatchDirectoryKind : int32_t {
	None = 0,
	UseFsEvents = 1,
	FixedPollingInterval = 2,
	DynamicPriorityPolling = 3,
	FixedChunkSizePolling = 4,
};

enum class PollingKind : int32_t {
	None = 0,
	FixedInterval = 1,
	PriorityInterval = 2,
	DynamicPriority = 3,
	FixedChunkSize = 4,
};

struct WatchOptions {
	int* Interval{};
	WatchFileKind FileKind{};
	WatchDirectoryKind DirectoryKind{};
	PollingKind FallbackPolling{};
	Tristate SyncWatchDir{};
	std::vector<std::string> ExcludeDir;
	std::vector<std::string> ExcludeFiles;

	// WatchInterval — watchoptions.go. Default 2000ms.
	int64_t WatchInterval() const {
		int64_t watchInterval = 2000;
		if (Interval != nullptr) {
			watchInterval = *Interval;
		}
		return watchInterval;
	}
};

// typeacquisition.go
struct TypeAcquisition {
	Tristate Enable{};
	std::vector<std::string> Include;
	std::vector<std::string> Exclude;
	Tristate DisableFilenameBasedTypeAcquisition{};

	bool Equals(const TypeAcquisition* other) const {
		if (this == other) {
			return true;
		}
		if (other == nullptr) {
			return false;
		}
		return Enable == other->Enable && Include == other->Include &&
			   Exclude == other->Exclude &&
			   DisableFilenameBasedTypeAcquisition ==
				   other->DisableFilenameBasedTypeAcquisition;
	}

	// === slice: api ===
	// encoding/json Marshal/Unmarshal over the tagged fields (api/proto.cpp).
	std::string unmarshalJSONFrom(json::Decoder& dec);
	std::string marshalJSONTo(json::Encoder& enc) const;
};

// buildoptions.go
struct BuildOptions {
	Tristate Dry{};
	Tristate Force{};
	Tristate Verbose{};
	int* Builders{};
	Tristate StopBuildOnErrors{};

	// Internal fields
	Tristate Clean{};

	// === slice: api ===
	// encoding/json Marshal/Unmarshal over the tagged fields (api/proto.cpp).
	std::string unmarshalJSONFrom(json::Decoder& dec);
	std::string marshalJSONTo(json::Encoder& enc) const;
};

// projectreference.go
struct ProjectReference {
	// Path is a normalized path on disk.
	tspath::Path Path;
	// OriginalPath is the path as it was originally written.
	std::string OriginalPath;
	// Circular indicates that this reference is intended to form a circularity.
	bool Circular = false;

	// === slice: api ===
	// encoding/json Marshal/Unmarshal over the tagged fields (api/proto.cpp).
	std::string unmarshalJSONFrom(json::Decoder& dec);
	std::string marshalJSONTo(json::Encoder& enc) const;
};

inline std::string ResolveConfigFileNameOfProjectReference(std::string_view path) {
	if (tspath::fileExtensionIs(path, tspath::extensionJson)) {
		return std::string{path};
	}
	return tspath::combinePaths(path, {"tsconfig.json"});
}

inline std::string ResolveProjectReferencePath(const ProjectReference& ref) {
	return ResolveConfigFileNameOfProjectReference(ref.Path);
}

// === slice: api ===
// core.go:80 — Map applies f to each element and returns the results.
template <class U, class T, class F>
std::vector<U> mapVec(const std::vector<T>& slice, F&& f) {
	std::vector<U> result;
	result.reserve(slice.size());
	for (const auto& t : slice) {
		result.push_back(f(t));
	}
	return result;
}

// binarysearch.go — BinarySearchUniqueFunc: binary search by compare function;
// returns (index, found) where element order is unique.
template <class E, class Cmp>
std::pair<int, bool> binarySearchUniqueFunc(const std::vector<E>& x, Cmp&& cmp) {
	int lo = 0, hi = static_cast<int>(x.size()) - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int c = cmp(mid, x[mid]);
		if (c < 0) {
			lo = mid + 1;
		} else if (c > 0) {
			hi = mid - 1;
		} else {
			return {mid, true};
		}
	}
	return {lo, false};
}
// core.go:36 — Filter returns elements for which f returns true. Returns the
// original vector unchanged when every element passes (Go aliasing of the
// backing array is not observable here; the result is a fresh vector).
template <class T, class F>
std::vector<T> Filter(const std::vector<T>& slice, F&& f) {
	bool allMatch = true;
	for (size_t i = 0; i < slice.size(); i++) {
		if (!f(slice[i])) {
			std::vector<T> result(slice.begin(), slice.begin() + i);
			for (size_t j = i + 1; j < slice.size(); j++) {
				if (f(slice[j])) {
					result.push_back(slice[j]);
				}
			}
			return result;
		}
	}
	return slice;
}

// core.go:775 — DiffMapsFunc compares two maps m1 and m2 and calls the
// provided callbacks for added, removed, and changed entries. Null
// std::function callbacks are skipped like Go nil funcs.
template <class K, class V1, class V2, class Eq>
void DiffMapsFunc(const std::unordered_map<K, V1>& m1,
                  const std::unordered_map<K, V2>& m2, Eq&& equalValues,
                  const std::function<void(K, V2)>& onAdded,
                  const std::function<void(K, V1)>& onRemoved,
                  const std::function<void(K, V1, V2)>& onChanged) {
	if (onAdded) {
		for (const auto& [k, v2] : m2) {
			if (m1.find(k) == m1.end()) {
				onAdded(k, v2);
			}
		}
	}
	if (!onChanged && !onRemoved) {
		return;
	}
	for (const auto& [k, v1] : m1) {
		auto it = m2.find(k);
		if (it != m2.end()) {
			if (onChanged && !equalValues(v1, it->second)) {
				onChanged(k, v1, it->second);
			}
		} else if (onRemoved) {
			onRemoved(k, v1);
		}
	}
}

// core.go:771 — DiffMaps with pointer equality for comparable Go values.
template <class K, class V>
void DiffMaps(const std::unordered_map<K, V>& m1,
              const std::unordered_map<K, V>& m2,
              const std::function<void(K, V)>& onAdded,
              const std::function<void(K, V)>& onRemoved,
              const std::function<void(K, V, V)>& onChanged) {
	DiffMapsFunc<K, V, V>(
	    m1, m2, [](const V& a, const V& b) { return a == b; }, onAdded,
	    onRemoved, onChanged);
}

// Pointer overloads — a nil map acts as empty (Go nil map arguments, e.g.
// session.go's computeSnapshotChanges diffing two programs' FilesByPath maps
// where either side may be nil).
template <class K, class V1, class V2, class Eq>
void DiffMapsFunc(const std::unordered_map<K, V1>* m1,
                  const std::unordered_map<K, V2>* m2, Eq&& equalValues,
                  const std::function<void(K, V2)>& onAdded,
                  const std::function<void(K, V1)>& onRemoved,
                  const std::function<void(K, V1, V2)>& onChanged) {
	static const std::unordered_map<K, V1> emptyM1;
	static const std::unordered_map<K, V2> emptyM2;
	DiffMapsFunc(m1 ? *m1 : emptyM1, m2 ? *m2 : emptyM2,
	             std::forward<Eq>(equalValues), onAdded, onRemoved, onChanged);
}

template <class K, class V>
void DiffMaps(const std::unordered_map<K, V>* m1,
              const std::unordered_map<K, V>* m2,
              const std::function<void(K, V)>& onAdded,
              const std::function<void(K, V)>& onRemoved,
              const std::function<void(K, V, V)>& onChanged) {
	DiffMapsFunc<K, V, V>(
	    m1, m2, [](const V& a, const V& b) { return a == b; }, onAdded,
	    onRemoved, onChanged);
}
// === end slice: api ===

}  // namespace tsc
