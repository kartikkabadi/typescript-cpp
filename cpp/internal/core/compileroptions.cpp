// compileroptions.go — CompilerOptions methods that need tspath.
// === slice: module ===

#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc {

// compileroptions.go — GetEffectiveTypeRoots.
std::pair<std::vector<std::string>, bool> CompilerOptions::GetEffectiveTypeRoots(
    std::string_view currentDirectory) const {
	if (!TypeRoots.empty()) {
		// Note: Go distinguishes nil vs empty TypeRoots; C++ cannot.
		return {TypeRoots, true};
	}
	std::string baseDir;
	if (!ConfigFilePath.empty()) {
		baseDir = tspath::getDirectoryPath(ConfigFilePath);
	} else {
		baseDir = std::string{currentDirectory};
		if (baseDir.empty()) {
			// panic: cannot get effective type roots without a config file
			// path or current directory
			tscUnreachable("cannot get effective type roots without a config "
			               "file path or current directory");
		}
	}

	std::vector<std::string> typeRoots;
	tspath::forEachAncestorDirectory<bool>(
	    baseDir, [&](std::string_view dir) -> std::pair<bool, bool> {
		    typeRoots.emplace_back(
		        tspath::combinePaths(dir, {"node_modules", "@types"}));
		    return {false, false};
	    });
	return {typeRoots, false};
}

// compileroptions.go — GetPathsBasePath.
std::string CompilerOptions::GetPathsBasePath(
    std::string_view currentDirectory) const {
	if (Paths.empty()) {
		return "";
	}
	if (!PathsBasePath.empty()) {
		return PathsBasePath;
	}
	return std::string{currentDirectory};
}

}  // namespace tsc

namespace tsc {

// === slice: project ===
// reflect.DeepEqual helpers shared by execute/watcher.cpp and the
// project collection builder (moved out of watcher.cpp's anonymous
// namespace so both slices use one definition).

// intPtrEqual — DeepEqual on *int: nil-vs-nil or equal pointees.
bool intPtrEqual(const int* a, const int* b) {
	if (a == nullptr || b == nullptr) {
		return a == b;
	}
	return *a == *b;
}

// pathsEqual — CompilerOptions.Paths: ordered [key, []values] pairs.
bool pathsEqual(
    const std::vector<std::pair<std::string, std::vector<std::string>>>& a,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& b) {
	return a == b;
}

// pluginsEqual — CompilerOptions.Plugins ([]PluginImport{name}).
bool pluginsEqual(const std::vector<PluginImport>& a,
                  const std::vector<PluginImport>& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		if (a[i].name != b[i].name) {
			return false;
		}
	}
	return true;
}
// compilerOptionsDeepEqual — reflect.DeepEqual on *core.CompilerOptions:
// every field of the struct, in declaration order.
bool CompilerOptions::Equals(const CompilerOptions* other) const {
	return compilerOptionsDeepEqual(this, other);
}

bool compilerOptionsDeepEqual(const CompilerOptions* a,
                              const CompilerOptions* b) {
	if (a == b) {
		return true;
	}
	if (a == nullptr || b == nullptr) {
		return false;
	}
	return a->AllowJs == b->AllowJs &&
	       a->AllowArbitraryExtensions == b->AllowArbitraryExtensions &&
	       a->AllowImportingTsExtensions == b->AllowImportingTsExtensions &&
	       a->AllowNonTsExtensions == b->AllowNonTsExtensions &&
	       a->AllowUmdGlobalAccess == b->AllowUmdGlobalAccess &&
	       a->AllowUnreachableCode == b->AllowUnreachableCode &&
	       a->AllowUnusedLabels == b->AllowUnusedLabels &&
	       a->AssumeChangesOnlyAffectDirectDependencies ==
	           b->AssumeChangesOnlyAffectDirectDependencies &&
	       a->CheckJs == b->CheckJs &&
	       a->CustomConditions == b->CustomConditions &&
	       a->Composite == b->Composite &&
	       a->EmitDeclarationOnly == b->EmitDeclarationOnly &&
	       a->EmitBOM == b->EmitBOM &&
	       a->EmitDecoratorMetadata == b->EmitDecoratorMetadata &&
	       a->Declaration == b->Declaration &&
	       a->DeclarationDir == b->DeclarationDir &&
	       a->DeclarationMap == b->DeclarationMap &&
	       a->DeduplicatePackages == b->DeduplicatePackages &&
	       a->DisableSizeLimit == b->DisableSizeLimit &&
	       a->DisableSourceOfProjectReferenceRedirect ==
	           b->DisableSourceOfProjectReferenceRedirect &&
	       a->DisableSolutionSearching == b->DisableSolutionSearching &&
	       a->DisableReferencedProjectLoad ==
	           b->DisableReferencedProjectLoad &&
	       a->ErasableSyntaxOnly == b->ErasableSyntaxOnly &&
	       a->ExactOptionalPropertyTypes == b->ExactOptionalPropertyTypes &&
	       a->ExperimentalDecorators == b->ExperimentalDecorators &&
	       a->ForceConsistentCasingInFileNames ==
	           b->ForceConsistentCasingInFileNames &&
	       a->IsolatedModules == b->IsolatedModules &&
	       a->IsolatedDeclarations == b->IsolatedDeclarations &&
	       a->IgnoreConfig == b->IgnoreConfig &&
	       a->IgnoreDeprecations == b->IgnoreDeprecations &&
	       a->ImportHelpers == b->ImportHelpers &&
	       a->InlineSourceMap == b->InlineSourceMap &&
	       a->InlineSources == b->InlineSources &&
	       a->Init == b->Init &&
	       a->Incremental == b->Incremental &&
	       a->Jsx == b->Jsx && a->JsxFactory == b->JsxFactory &&
	       a->JsxFragmentFactory == b->JsxFragmentFactory &&
	       a->JsxImportSource == b->JsxImportSource &&
	       a->Lib == b->Lib &&
	       a->LibReplacement == b->LibReplacement &&
	       a->Locale == b->Locale && a->MapRoot == b->MapRoot &&
	       a->Module == b->Module &&
	       a->ModuleResolution == b->ModuleResolution &&
	       a->ModuleSuffixes == b->ModuleSuffixes &&
	       a->ModuleDetection == b->ModuleDetection &&
	       a->NewLine == b->NewLine && a->NoEmit == b->NoEmit &&
	       a->NoCheck == b->NoCheck &&
	       a->NoErrorTruncation == b->NoErrorTruncation &&
	       a->NoFallthroughCasesInSwitch == b->NoFallthroughCasesInSwitch &&
	       a->NoImplicitAny == b->NoImplicitAny &&
	       a->NoImplicitThis == b->NoImplicitThis &&
	       a->NoImplicitReturns == b->NoImplicitReturns &&
	       a->NoEmitHelpers == b->NoEmitHelpers &&
	       a->NoLib == b->NoLib &&
	       a->NoPropertyAccessFromIndexSignature ==
	           b->NoPropertyAccessFromIndexSignature &&
	       a->NoUncheckedIndexedAccess == b->NoUncheckedIndexedAccess &&
	       a->NoEmitOnError == b->NoEmitOnError &&
	       a->NoUnusedLocals == b->NoUnusedLocals &&
	       a->NoUnusedParameters == b->NoUnusedParameters &&
	       a->NoResolve == b->NoResolve &&
	       a->NoImplicitOverride == b->NoImplicitOverride &&
	       a->NoUncheckedSideEffectImports ==
	           b->NoUncheckedSideEffectImports &&
	       a->OutDir == b->OutDir && pathsEqual(a->Paths, b->Paths) &&
	       pluginsEqual(a->Plugins, b->Plugins) &&
	       a->PreserveConstEnums == b->PreserveConstEnums &&
	       a->PreserveSymlinks == b->PreserveSymlinks &&
	       a->Project == b->Project &&
	       a->ResolveJsonModule == b->ResolveJsonModule &&
	       a->ResolvePackageJsonExports == b->ResolvePackageJsonExports &&
	       a->ResolvePackageJsonImports == b->ResolvePackageJsonImports &&
	       a->RemoveComments == b->RemoveComments &&
	       a->RewriteRelativeImportExtensions ==
	           b->RewriteRelativeImportExtensions &&
	       a->ReactNamespace == b->ReactNamespace &&
	       a->RootDir == b->RootDir && a->RootDirs == b->RootDirs &&
	       a->SkipLibCheck == b->SkipLibCheck &&
	       a->StableTypeOrdering == b->StableTypeOrdering &&
	       a->Strict == b->Strict &&
	       a->StrictBindCallApply == b->StrictBindCallApply &&
	       a->StrictBuiltinIteratorReturn ==
	           b->StrictBuiltinIteratorReturn &&
	       a->StrictFunctionTypes == b->StrictFunctionTypes &&
	       a->StrictNullChecks == b->StrictNullChecks &&
	       a->StrictPropertyInitialization ==
	           b->StrictPropertyInitialization &&
	       a->StripInternal == b->StripInternal &&
	       a->SkipDefaultLibCheck == b->SkipDefaultLibCheck &&
	       a->SourceMap == b->SourceMap &&
	       a->SourceRoot == b->SourceRoot &&
	       a->SuppressOutputPathCheck == b->SuppressOutputPathCheck &&
	       a->Target == b->Target &&
	       a->TraceResolution == b->TraceResolution &&
	       a->TsBuildInfoFile == b->TsBuildInfoFile &&
	       a->TypeRoots == b->TypeRoots && a->Types == b->Types &&
	       a->UseDefineForClassFields == b->UseDefineForClassFields &&
	       a->UseUnknownInCatchVariables == b->UseUnknownInCatchVariables &&
	       a->VerbatimModuleSyntax == b->VerbatimModuleSyntax &&
	       intPtrEqual(a->MaxNodeModuleJsDepth, b->MaxNodeModuleJsDepth) &&
	       a->AllowSyntheticDefaultImports ==
	           b->AllowSyntheticDefaultImports &&
	       a->AlwaysStrict == b->AlwaysStrict &&
	       a->BaseUrl == b->BaseUrl &&
	       a->DownlevelIteration == b->DownlevelIteration &&
	       a->ESModuleInterop == b->ESModuleInterop &&
	       a->OutFile == b->OutFile &&
	       a->ConfigFilePath == b->ConfigFilePath &&
	       a->NoDtsResolution == b->NoDtsResolution &&
	       a->PathsBasePath == b->PathsBasePath &&
	       a->Diagnostics == b->Diagnostics &&
	       a->ExtendedDiagnostics == b->ExtendedDiagnostics &&
	       a->GenerateCpuProfile == b->GenerateCpuProfile &&
	       a->GenerateTrace == b->GenerateTrace &&
	       a->ListEmittedFiles == b->ListEmittedFiles &&
	       a->ListFiles == b->ListFiles &&
	       a->ExplainFiles == b->ExplainFiles &&
	       a->ListFilesOnly == b->ListFilesOnly &&
	       a->NoEmitForJsFiles == b->NoEmitForJsFiles &&
	       a->PreserveWatchOutput == b->PreserveWatchOutput &&
	       a->Pretty == b->Pretty && a->Version == b->Version &&
	       a->Watch == b->Watch && a->ShowConfig == b->ShowConfig &&
	       a->Build == b->Build && a->Help == b->Help &&
	       a->All == b->All &&
	       a->RunExternalCode == b->RunExternalCode &&
	       a->PprofDir == b->PprofDir &&
	       a->SingleThreaded == b->SingleThreaded &&
	       a->Quiet == b->Quiet &&
	       intPtrEqual(a->Checkers, b->Checkers);
}
// === end slice: project ===
}  // namespace tsc
