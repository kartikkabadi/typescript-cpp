// proto.h — declarations for tsc/internal/api/proto.go.
//
// The api package's protocol types. C++ notes:
//   - Go `any` / json.Value fields become json::Value (raw JSON text); a
//     marshaled-any field stores the pre-encoded bytes and marshals verbatim.
//   - `nonnil:"true"` tags mark slices that must always emit ([] not null).
//   - Params structs implement unmarshalJSONFrom(json::Decoder&); responses
//     implement marshalJSONTo(json::Encoder&) — the same contracts Go's
//     json.UnmarshalerFrom/MarshalerTo give encoding/json.
#pragma once

#include <any>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/checker/types.h"
#include "internal/checker/checker.h"
#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/execute/tsc/compile.h"
#include "internal/execute/tsc/statistics.h"
#include "internal/jsnum/jsnum.h"
#include "internal/json/json.h"
#include "internal/locale/locale.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/module/types.h"
#include "internal/packagejson/packagejson.h"
#include "internal/project/parsecache.h"
#include "internal/project/project.h"
#include "internal/scanner/scanner.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace tsc::api {

inline const gostd::Error ErrInvalidRequest = gostd::newError("api: invalid request");
inline const gostd::Error ErrClientError = gostd::newError("api: client error");

using Method = std::string;

using SnapshotID = uint64_t;
using ModuleResolverID = uint64_t;
using SourceFileLeaseID = uint64_t;
using BuildOrchestratorID = uint64_t;
using SymbolID = uint64_t;
using TypeID = uint32_t;
using SignatureID = uint64_t;
using NodeHandle = std::string;

// nextBuildOrchestratorId / NewBuildOrchestratorID — proto.go:42-46.
BuildOrchestratorID NewBuildOrchestratorID();

// SymbolHandle / TypeHandle / SignatureHandle — proto.go:48-58.
SymbolID SymbolHandle(Symbol* symbol);
TypeID TypeHandle(checker::Type* t);
SignatureID SignatureHandle(checker::Signature* sig);

// Forward decls for params/response structs used before their definitions.
struct EnsurePrograms;
struct CreateSnapshotProgramParams;
struct ReconfigureSnapshotProgramParams;
struct LanguageServerSnapshotChanges;
struct ModuleResolutionEntry;
struct DiagnosticResponse;
struct PackageId;
namespace requestfilesystem {
struct RequestFileSystem;
struct RequestDirectoryEntries;
struct RequestSymlink;
}  // namespace requestfilesystem

inline const Method MethodRelease = "release";
inline const Method MethodReleaseSourceFile = "releaseSourceFile";
inline const Method MethodRetainSourceFile = "retainSourceFile";
inline const Method MethodGetCachedSourceFile = "getCachedSourceFile";

inline const Method MethodBatchRequests = "batchRequests";
inline const Method MethodInitialize = "initialize";
inline const Method MethodCreateSnapshot = "createSnapshot";
inline const Method MethodUpdateSnapshot = "updateSnapshot";
inline const Method MethodGetCurrentLanguageServerSnapshot = "getCurrentLanguageServerSnapshot";
inline const Method MethodCreateBuildOrchestrator = "createBuildOrchestrator";
inline const Method MethodDisposeBuildOrchestrator = "disposeBuildOrchestrator";
inline const Method MethodBuild = "build";
inline const Method MethodBuildReferences = "buildReferences";
inline const Method MethodCleanBuild = "cleanBuild";
inline const Method MethodCleanReferences = "cleanReferences";
inline const Method MethodCreateModuleResolver = "createModuleResolver";
inline const Method MethodReleaseModuleResolver = "releaseModuleResolver";
inline const Method MethodResolveModuleName = "resolveModuleName";
inline const Method MethodParseCommandLine = "parseCommandLine";
inline const Method MethodReadConfigFile = "readConfigFile";
inline const Method MethodParseJsonConfigFile = "parseJsonConfigFileContent";
inline const Method MethodParseConfigFile = "parseConfigFile";
inline const Method MethodCreateSourceFile = "createSourceFile";
inline const Method MethodCreateSourceFileFromFile = "createSourceFileFromFile";
inline const Method MethodTranspileModule = "transpileModule";
inline const Method MethodTranspileModuleFromFile = "transpileModuleFromFile";
inline const Method MethodTranspileDeclaration = "transpileDeclaration";
inline const Method MethodTranspileDeclarationFromFile = "transpileDeclarationFromFile";
inline const Method MethodGetDefaultProjectForFile = "getDefaultProjectForFile";
inline const Method MethodGetSymbolAtPosition = "getSymbolAtPosition";
inline const Method MethodGetSymbolsAtPositions = "getSymbolsAtPositions";
inline const Method MethodGetSymbolAtLocation = "getSymbolAtLocation";
inline const Method MethodGetSymbolsAtLocations = "getSymbolsAtLocations";
inline const Method MethodGetSymbolOfSourceFile = "getSymbolOfSourceFile";
inline const Method MethodGetSymbolsOfSourceFiles = "getSymbolsOfSourceFiles";
inline const Method MethodGetTypeOfSymbol = "getTypeOfSymbol";
inline const Method MethodGetTypesOfSymbols = "getTypesOfSymbols";
inline const Method MethodGetDeclaredTypeOfSymbol = "getDeclaredTypeOfSymbol";
inline const Method MethodGetNonMissingTypeOfSymbol = "getNonMissingTypeOfSymbol";
inline const Method MethodGetSourceFile = "getSourceFile";
inline const Method MethodGetSourceFileNames = "getSourceFileNames";
inline const Method MethodGetSourceFileMetadata = "getSourceFileMetadata";
inline const Method MethodGetModeForUsageLocation = "getModeForUsageLocation";
inline const Method MethodGetModeForResolutionAtIndex = "getModeForResolutionAtIndex";
inline const Method MethodGetResolvedModule = "getResolvedModule";
inline const Method MethodGetResolvedModuleFromModuleSpecifier = "getResolvedModuleFromModuleSpecifier";
inline const Method MethodGetResolvedTypeReferenceDirective = "getResolvedTypeReferenceDirective";
inline const Method MethodGetResolvedTypeReferenceDirectiveFromReference = "getResolvedTypeReferenceDirectiveFromTypeReferenceDirective";
inline const Method MethodGetConfigFileNames = "getConfigFileNames";
inline const Method MethodGetConfigSourceFile = "getConfigSourceFile";
inline const Method MethodResolveName = "resolveName";
inline const Method MethodGetSymbolsInScope = "getSymbolsInScope";
inline const Method MethodGetSignaturesOfType = "getSignaturesOfType";
inline const Method MethodGetResolvedSignature = "getResolvedSignature";
inline const Method MethodGetTypeAtLocation = "getTypeAtLocation";
inline const Method MethodGetTypeAtLocations = "getTypeAtLocations";
inline const Method MethodGetTypeAtPosition = "getTypeAtPosition";
inline const Method MethodGetTypesAtPositions = "getTypesAtPositions";

// Symbol sub-property methods
inline const Method MethodGetParentOfSymbol = "getParentOfSymbol";
inline const Method MethodGetMembersOfSymbol = "getMembersOfSymbol";
inline const Method MethodGetExportsOfSymbol = "getExportsOfSymbol";
inline const Method MethodGetExportSymbolOfSymbol = "getExportSymbolOfSymbol";

// Type sub-property methods
inline const Method MethodGetSymbolOfType = "getSymbolOfType";
inline const Method MethodGetTargetOfType = "getTargetOfType";
inline const Method MethodGetFreshTypeOfType = "getFreshTypeOfType";
inline const Method MethodGetRegularTypeOfType = "getRegularTypeOfType";
inline const Method MethodGetTypesOfType = "getTypesOfType";
inline const Method MethodGetTypeParametersOfType = "getTypeParametersOfType";
inline const Method MethodGetOuterTypeParametersOfType = "getOuterTypeParametersOfType";
inline const Method MethodGetLocalTypeParametersOfType = "getLocalTypeParametersOfType";
inline const Method MethodGetThisTypeOfType = "getThisTypeOfType";
inline const Method MethodGetAliasTypeArgumentsOfType = "getAliasTypeArgumentsOfType";
inline const Method MethodGetAliasSymbolOfType = "getAliasSymbolOfType";
inline const Method MethodGetObjectTypeOfType = "getObjectTypeOfType";
inline const Method MethodGetIndexTypeOfType = "getIndexTypeOfType";
inline const Method MethodGetCheckTypeOfType = "getCheckTypeOfType";
inline const Method MethodGetExtendsTypeOfType = "getExtendsTypeOfType";
inline const Method MethodGetBaseTypeOfType = "getBaseTypeOfType";
inline const Method MethodGetConstraintOfType = "getConstraintOfType";
inline const Method MethodGetTypeParameterOfMappedType = "getTypeParameterOfMappedType";
inline const Method MethodGetConstraintTypeOfMappedType = "getConstraintTypeOfMappedType";
inline const Method MethodGetNameTypeOfMappedType = "getNameTypeOfMappedType";
inline const Method MethodGetTemplateTypeOfMappedType = "getTemplateTypeOfMappedType";

// Signature sub-property methods
inline const Method MethodGetTypeParametersOfSignature = "getTypeParametersOfSignature";
inline const Method MethodGetParametersOfSignature = "getParametersOfSignature";
inline const Method MethodGetThisParameterOfSignature = "getThisParameterOfSignature";
inline const Method MethodGetTargetOfSignature = "getTargetOfSignature";

// Checker methods
inline const Method MethodGetContextualType = "getContextualType";
inline const Method MethodGetContextualTypeForArgument = "getContextualTypeForArgument";
inline const Method MethodGetAwaitedType = "getAwaitedType";
inline const Method MethodGetBaseTypeOfLiteralType = "getBaseTypeOfLiteralType";
inline const Method MethodGetNonNullableType = "getNonNullableType";
inline const Method MethodGetTypeFromTypeNode = "getTypeFromTypeNode";
inline const Method MethodGetWidenedType = "getWidenedType";
inline const Method MethodGetParameterType = "getParameterType";
inline const Method MethodGetTypeParameterAtPosition = "getTypeParameterAtPosition";
inline const Method MethodIsArrayLikeType = "isArrayLikeType";
inline const Method MethodIsTypeAssignableTo = "isTypeAssignableTo";
inline const Method MethodGetShorthandAssignmentValueSymbol = "getShorthandAssignmentValueSymbol";
inline const Method MethodGetTypeOfSymbolAtLocation = "getTypeOfSymbolAtLocation";
inline const Method MethodTypeToTypeNode = "typeToTypeNode";
inline const Method MethodSignatureToSignatureDeclaration = "signatureToSignatureDeclaration";
inline const Method MethodTypeToString = "typeToString";
inline const Method MethodIsContextSensitive = "isContextSensitive";
inline const Method MethodGetReturnTypeOfSignature = "getReturnTypeOfSignature";
inline const Method MethodGetRestTypeOfSignature = "getRestTypeOfSignature";
inline const Method MethodGetTypePredicateOfSignature = "getTypePredicateOfSignature";
inline const Method MethodGetBaseTypes = "getBaseTypes";
inline const Method MethodGetPropertiesOfType = "getPropertiesOfType";
inline const Method MethodGetApparentPropertiesOfType = "getApparentPropertiesOfType";
inline const Method MethodGetApparentType = "getApparentType";
inline const Method MethodGetReducedType = "getReducedType";
inline const Method MethodGetPropertyOfType = "getPropertyOfType";
inline const Method MethodGetTypeOfPropertyOfType = "getTypeOfPropertyOfType";
inline const Method MethodGetIndexInfoOfType = "getIndexInfoOfType";
inline const Method MethodGetIndexInfosOfType = "getIndexInfosOfType";
inline const Method MethodGetConstraintOfTypeParameter = "getConstraintOfTypeParameter";
inline const Method MethodGetDefaultFromTypeParameter = "getDefaultFromTypeParameter";
inline const Method MethodGetBaseConstraintOfType = "getBaseConstraintOfType";
inline const Method MethodGetTypeArguments = "getTypeArguments";
inline const Method MethodGetImportAdderEdits = "getImportAdderEdits";
inline const Method MethodGetTrueTypeOfConditionalType = "getTrueTypeOfConditionalType";
inline const Method MethodGetFalseTypeOfConditionalType = "getFalseTypeOfConditionalType";
inline const Method MethodGetConstantValue = "getConstantValue";
inline const Method MethodGetSignatureFromDeclaration = "getSignatureFromDeclaration";
inline const Method MethodGetExportSpecifierLocalTarget = "getExportSpecifierLocalTargetSymbol";
inline const Method MethodGetAliasedSymbol = "getAliasedSymbol";
inline const Method MethodGetImmediateAliasedSymbol = "getImmediateAliasedSymbol";
inline const Method MethodGetTargetSymbol = "getTargetSymbol";
inline const Method MethodGetExportSymbolOfSymbolForChecker = "getExportSymbolOfSymbolForChecker";
inline const Method MethodGetFullyQualifiedName = "getFullyQualifiedName";
inline const Method MethodGetExportsOfModule = "getExportsOfModule";
inline const Method MethodGetMemberInModuleExports = "getMemberInModuleExports";
inline const Method MethodGetJSDocTags = "getJsDocTags";
inline const Method MethodGetDocumentationComment = "getDocumentationComment";
inline const Method MethodIsArrayType = "isArrayType";
inline const Method MethodIsReadonlySymbol = "isReadonlySymbol";

// Reference methods
inline const Method MethodGetReferencesToSymbolInFile = "getReferencesToSymbolInFile";
inline const Method MethodGetReferencedSymbolsForNode = "getReferencedSymbolsForNode";
inline const Method MethodGetSignatureUsages = "getSignatureUsages";

// Language service methods
inline const Method MethodGetCompletionsAtPosition = "getCompletionsAtPosition";

// Diagnostic methods
inline const Method MethodGetSyntacticDiagnostics = "getSyntacticDiagnostics";
inline const Method MethodGetBindDiagnostics = "getBindDiagnostics";
inline const Method MethodGetSemanticDiagnostics = "getSemanticDiagnostics";
inline const Method MethodGetSuggestionDiagnostics = "getSuggestionDiagnostics";
inline const Method MethodGetDeclarationDiagnostics = "getDeclarationDiagnostics";
inline const Method MethodGetProgramDiagnostics = "getProgramDiagnostics";
inline const Method MethodGetGlobalDiagnostics = "getGlobalDiagnostics";
inline const Method MethodGetConfigFileParsingDiagnostics = "getConfigFileParsingDiagnostics";
// Printer methods
inline const Method MethodPrintNode = "printNode";
inline const Method MethodFormatNodeForInsertion = "formatNodeForInsertion";
inline const Method MethodEmit = "emit";
inline const Method MethodEmitToString = "emitToString";
inline const Method MethodGetJavaScriptEmit = "getJavaScriptEmit";
inline const Method MethodGetDeclarationEmit = "getDeclarationEmit";

// Intrinsic type getters
inline const Method MethodGetAnyType = "getAnyType";
inline const Method MethodGetStringType = "getStringType";
inline const Method MethodGetNumberType = "getNumberType";
inline const Method MethodGetBooleanType = "getBooleanType";
inline const Method MethodGetVoidType = "getVoidType";
inline const Method MethodGetUndefinedType = "getUndefinedType";
inline const Method MethodGetNullType = "getNullType";
inline const Method MethodGetNeverType = "getNeverType";
inline const Method MethodGetUnknownType = "getUnknownType";
inline const Method MethodGetBigIntType = "getBigIntType";
inline const Method MethodGetESSymbolType = "getESSymbolType";
inline const Method MethodGetNonPrimitiveType = "getNonPrimitiveType";

// Well-known per-checker symbols
inline const Method MethodGetWellKnownSymbols = "getWellKnownSymbols";

// Well-known per-checker signatures
inline const Method MethodGetWellKnownSignatures = "getWellKnownSignatures";

// Profiling methods
inline const Method MethodStartCPUProfile = "startCPUProfile";
inline const Method MethodStopCPUProfile = "stopCPUProfile";
inline const Method MethodSaveHeapProfile = "saveHeapProfile";

// InitializeResponse is returned by the initialize method.
struct InitializeResponse {
    // UseCaseSensitiveFileNames indicates whether the host file system is case-sensitive.
    bool UseCaseSensitiveFileNames{};
    // CurrentDirectory is the server's current working directory.
    std::string CurrentDirectory;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// DocumentIdentifier identifies a document by either a file name (plain string) or a URI object.
// On the wire it is string | { uri: string }.
struct DocumentIdentifier {
    std::string FileName;
    lsproto::DocumentUri URI;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);

    // ToFileName — proto.go:327.
    std::string ToFileName() const;
    // ToURI returns the document URI for this identifier. An explicitly provided URI
    // is returned as-is; a file name is first normalized to an absolute path against
    // cwd before being converted to a URI.
    lsproto::DocumentUri ToURI(const std::string& cwd) const;
    std::string ToAbsoluteFileName(const std::string& cwd) const;
    // String — proto.go:352.
    std::string String() const;
};

// FileNotifications describes changes to files that have occurred on the host
// file system, used to notify the session to reload cached files and reevaluate
// tsconfig.json `include` globs. Either InvalidateAll is true (discard all caches)
// or Changed/Created/Deleted list individual documents.
struct FileNotifications {
    bool InvalidateAll{};
    std::vector<DocumentIdentifier> Changed;
    std::vector<DocumentIdentifier> Created;
    std::vector<DocumentIdentifier> Deleted;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// SnapshotRequestChangesParams describes project, file, and program changes to apply
// while creating or updating a snapshot.
struct SnapshotRequestChangesParams {
    // OpenProjects lists tsconfig.json files to open/load in the new snapshot.
    std::vector<DocumentIdentifier> OpenProjects;
    // CloseProjects lists tsconfig.json files to release in the new snapshot.
    // A project is only unloaded once every API client that opened it closes it.
    std::vector<DocumentIdentifier> CloseProjects;
    // OpenFiles lists files to open in the new snapshot, mirroring LSP's
    // textDocument/didOpen. For each file, ancestor directories are searched for a
    // tsconfig that contains it; if found, that configured project is loaded and
    // becomes the file's default project. Otherwise the file is loaded into the
    // inferred project (e.g. a node_modules d.ts not in any project's import graph).
    // If a file cannot be loaded into any project, the request fails.
    // Go `[]DocumentIdentifier` — optional preserves nil-vs-empty, which
    // createSnapshotOperationResponse distinguishes (`request.OpenFiles != nil`).
    std::optional<std::vector<DocumentIdentifier>> OpenFiles;
    // CloseFiles lists files to release in the new snapshot. A file is only fully
    // closed once every API client that opened it closes it.
    std::vector<DocumentIdentifier> CloseFiles;
    // CreatePrograms describes synthetic programs to create in the snapshot.
    // optional for Go's nil-vs-empty distinction (session.go:4651).
    std::optional<std::vector<std::shared_ptr<CreateSnapshotProgramParams>>> CreatePrograms;
    // ReconfigurePrograms replaces the configuration of existing synthetic programs.
    std::vector<std::shared_ptr<ReconfigureSnapshotProgramParams>> ReconfigurePrograms;
    // RemovePrograms lists synthetic project handles to remove from the snapshot.
    std::vector<project::SyntheticProjectID> RemovePrograms;
    // EnsurePrograms identifies projects whose programs should be updated if dirty,
    // or all contained projects when true.
    std::shared_ptr<EnsurePrograms> EnsurePrograms;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct EnsurePrograms {
    bool All{};
    std::vector<project::ID> Projects;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// CreateSnapshotParams are the parameters for creating a new independent snapshot.
// (Go embeds SnapshotRequestChangesParams; C++ inherits.)
struct CreateSnapshotParams : SnapshotRequestChangesParams {
    // FileNotifications describes host file system changes to invalidate while creating the snapshot.
    std::shared_ptr<FileNotifications> FileNotifications;
    // FileSystem supplies file contents and directory listings for the new snapshot.
    // A full filesystem is canonical and total. A filesystem layer is checked
    // before falling back to the base snapshot or host filesystem.
    std::shared_ptr<requestfilesystem::RequestFileSystem> FileSystem;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct CreateProgramOptions;

struct CreateSnapshotProgramParams {
    std::vector<DocumentIdentifier> RootFiles;
    ::tsc::CompilerOptions CompilerOptions;
    std::shared_ptr<CreateProgramOptions> Options;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ReconfigureSnapshotProgramParams {
    project::SyntheticProjectID Id;
    std::vector<DocumentIdentifier> RootFiles;
    ::tsc::CompilerOptions CompilerOptions;
    std::shared_ptr<CreateProgramOptions> Options;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct UpdateSnapshotParams {
    SnapshotID Snapshot{};
    std::shared_ptr<CreateSnapshotParams> Changes;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetCurrentLanguageServerSnapshotParams {
    SnapshotID BaseSnapshot{};
    std::shared_ptr<LanguageServerSnapshotChanges> Changes;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// LanguageServerSnapshotChanges describes API-driven changes to adopt into the
// language server's canonical state.
struct LanguageServerSnapshotChanges : SnapshotRequestChangesParams {
	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct DiagnosticResponse;

struct CreateProgramOptions {
    std::vector<std::shared_ptr<::tsc::ProjectReference>> ProjectReferences;
    std::vector<std::shared_ptr<DiagnosticResponse>> ConfigFileParsingDiagnostics;
    ModuleResolverID ModuleResolver{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

using ModuleResolutionFallback = std::string;
using ResolutionMode = ::tsc::ModuleKind;

inline const ModuleResolutionFallback ModuleResolutionFallbackResolve = "resolve";
inline const ModuleResolutionFallback ModuleResolutionFallbackUnresolved = "unresolved";

struct ModuleResolutionSpec {
    ModuleResolutionFallback Fallback;
    std::vector<std::shared_ptr<ModuleResolutionEntry>> Entries; // nonnil

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct StaticModuleResolution;

struct ModuleResolutionEntry {
    std::string ModuleName;
    std::shared_ptr<DocumentIdentifier> ContainingDirectory;
    std::optional<api::ResolutionMode> ResolutionMode;
    std::shared_ptr<StaticModuleResolution> Result; // nonnil

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct PackageId;

struct StaticModuleResolution {
    std::shared_ptr<DocumentIdentifier> ResolvedFileName;
    std::shared_ptr<DocumentIdentifier> OriginalPath;
    std::shared_ptr<PackageId> PackageID;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct CreateModuleResolverParams {
    ::tsc::CompilerOptions CompilerOptions;
    std::shared_ptr<ModuleResolutionSpec> ModuleResolutions;
    std::string ResolveModuleNameCallback;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ReleaseModuleResolverParams {
    ModuleResolverID Resolver{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ResolveModuleNameParams {
    SnapshotID Snapshot{};
    uint64_t InProgressSnapshot{};
    ModuleResolverID Resolver{};
    std::string ModuleName;
    DocumentIdentifier ContainingDirectory;
    std::optional<api::ResolutionMode> ResolutionMode;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ResolveModuleNameCallbackParams {
    std::string ModuleName;
    std::string ContainingDirectory;
    std::optional<api::ResolutionMode> ResolutionMode;
    std::optional<SnapshotID> Snapshot;
    std::optional<uint64_t> InProgressSnapshot;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ResolvedModule;

struct ResolveModuleNameResult {
    std::shared_ptr<ResolvedModule> ResolvedModule;
    // Trace is provided when compilerOptions.traceResolution is true.
    std::vector<std::string> Trace;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// ProjectFileChanges describes what source files changed within a single project.
struct ProjectFileChanges {
    // ChangedFiles lists source file paths whose content differs.
    std::vector<tspath::Path> ChangedFiles;
    // DeletedFiles lists source file paths removed from the project's program.
    std::vector<tspath::Path> DeletedFiles;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// SnapshotChanges describes what changed between a response base and a new
// snapshot. Changes are reported per-project so clients
// can track cache refs at the (snapshot, project) level.
struct SnapshotChanges {
    // ChangedProjects maps project handles to the file changes within that project.
    // Projects not listed here (and not in RemovedProjects) are unchanged.
    std::map<project::ID, std::shared_ptr<ProjectFileChanges>> ChangedProjects;
    // RemovedProjects lists project handles that were present in the previous
    // snapshot but absent from the new one.
    std::vector<project::ID> RemovedProjects;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct ProjectResponse;
struct SnapshotOperationResponse;

// CreateSnapshotResponse is returned by createSnapshot.
struct CreateSnapshotResponse {
    // Snapshot is the handle for the newly created snapshot.
    SnapshotID Snapshot{};
    // Projects contains all projects when no response base was supplied, or only
    // projects added or replaced relative to that base.
    std::vector<std::shared_ptr<ProjectResponse>> Projects; // nonnil
    // Changes describes source file differences from the response base.
    std::shared_ptr<SnapshotChanges> Changes;
    // Operation describes results correlated with the request that produced the snapshot.
    std::shared_ptr<SnapshotOperationResponse> Operation; // nonnil

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct OpenedFileOperationResult;

struct SnapshotOperationResponse {
    std::optional<std::vector<project::SyntheticProjectID>> CreatedPrograms;
    std::optional<std::vector<std::shared_ptr<OpenedFileOperationResult>>> OpenedFiles;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct OpenedFileOperationResult {
    project::ID Project;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// unmarshalers — proto.go:569. Maps each Method to a decoder producing a
// shared_ptr<T> params object held in std::any (Go's `any` = `*T`).
std::pair<std::any, std::string> unmarshalPayload(std::string_view method, const json::Value& payload);

// unmarshallerFor — proto.go:2036.
template <class T>
std::pair<std::any, std::string> unmarshallerFor(const json::Value& data) {
    auto v = std::make_shared<T>();
    if (std::string err = json::unmarshal(data, v.get()); !err.empty()) {
        return {std::any{}, "failed to unmarshal " + std::string(typeid(T).name()) + ": " + err};
    }
    return {std::any(std::move(v)), {}};
}

// noParams — proto.go:2040.
inline std::pair<std::any, std::string> noParams(const json::Value&) {
    return {std::any{}, {}};
}

struct ParseConfigFileParams {
    DocumentIdentifier File;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ParseCommandLineParams {
    std::vector<std::string> CommandLine;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ReadConfigFileParams {
    DocumentIdentifier File;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ParseJsonConfigFileContentParams {
    packagejson::JSONValue JSON;
    std::optional<std::string> ConfigDirectory;
    std::shared_ptr<DocumentIdentifier> ConfigFileName;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// jsonValueToAny — proto.go:763. The C++ "any" for parsed tsconfig JSON is
// tsoptions::CompilerOptionsValue (JsonObjectPtr/JsonArray/scalars).
tsoptions::CompilerOptionsValue jsonValueToAny(const packagejson::JSONValue& value);

struct TranspileOptions {
    std::shared_ptr<::tsc::CompilerOptions> CompilerOptions;
    std::string FileName;
    bool ReportDiagnostics{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct CreateSourceFileOptions {
    ::tsc::ScriptKind ScriptKind{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct CreateSourceFileParams {
    std::string FileName;
    std::string SourceText;
    CreateSourceFileOptions Options;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct CreateSourceFileFromFileParams {
    std::string FileName;
    CreateSourceFileOptions Options;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct TranspileParams {
    std::string Input;
    TranspileOptions Options;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct TranspileFromFileParams {
    std::string FileName;
    TranspileOptions Options;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct TranspileOutputResponse {
    std::string OutputText;
    std::vector<std::shared_ptr<DiagnosticResponse>> Diagnostics;
    std::string SourceMapText;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct BatchRequest;

struct BatchRequestsParams {
    std::vector<BatchRequest> Requests;
    std::string ContinuationToken;
    int64_t MaxResponseBytesPerPage{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct BatchRequest {
    Method Method;
    json::Value Params;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct BatchResponse {
    Method Method;
    json::Value Result; // `json:"result"` — pre-marshaled; `any` in Go
    std::string Error;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct BatchRequestsResponse {
    std::vector<BatchResponse> Responses; // nonnil
    std::string ContinuationToken;
    std::vector<json::Value> encodedResponses; // proto.go:837 — pre-encoded pages

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// ReleaseParams are the parameters for the release method.
struct ReleaseParams {
    SnapshotID Snapshot{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ReleaseSourceFileParams {
    SourceFileLeaseID Lease{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// SourceFileDescriptor — proto.go:901. The complete identity of an ordinary
// cached source file.
struct SourceFileDescriptor {
    std::string FileName;
    tspath::Path Path;
    std::string ContentHash;
    std::string ParseOptionsKey;
    ScriptKind ScriptKind{};
    std::string NodeID;

    bool operator==(const SourceFileDescriptor& o) const = default;

    // parseCacheKey — session.go:2163. Validates the descriptor and rebuilds
    // the parse-cache key it names.
    std::pair<project::ParseCacheKey, gostd::Error> parseCacheKey() const;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct RetainSourceFileParams {
    SourceFileDescriptor File;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct RetainSourceFileResponse {
    SourceFileLeaseID Lease{};

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// GetCachedSourceFileParams address an ordinary cached source file by its
// complete identity, independent of any snapshot or lease.
struct GetCachedSourceFileParams {
    SourceFileDescriptor File;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ProfileParams {
    std::string Dir;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ProfileResult {
    std::string File;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct CreateBuildOrchestratorParams {
    std::vector<std::string> RootNames;
    std::string Cwd;
    // Only a subset of these options are exposed  the API
    // (Go embeds *core.BuildOptions and *core.CompilerOptions; JSON members are
    // promoted flat onto this object.)
    std::shared_ptr<::tsc::BuildOptions> BuildOptions;
    std::shared_ptr<::tsc::CompilerOptions> CompilerOptions;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct CreateBuildOrchestratorResponse {
    BuildOrchestratorID BuildOrchestratorID{};

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct DisposeBuildOrchestratorParams {
    BuildOrchestratorID BuildOrchestratorID{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct BuildParams {
    BuildOrchestratorID BuildOrchestratorID{};
    std::string Project;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct BuildResponse {
    ::tsc::execute::tsc::ExitStatus Status{};
    std::vector<std::shared_ptr<DiagnosticResponse>> Diagnostics;
    ::tsc::execute::tsc::Statistics Statistics;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct CleanBuildParams {
    BuildOrchestratorID BuildOrchestratorID{};
    std::string Project;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct CleanBuildResponse {
    ::tsc::execute::tsc::ExitStatus Status{};
    std::vector<std::shared_ptr<DiagnosticResponse>> Diagnostics;
    ::tsc::execute::tsc::Statistics Statistics;
    std::vector<std::string> FilesDeleted;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// BuildOrchestrator — proto.go:937.
struct BuildOrchestrator {
    std::function<::tsc::execute::tsc::ExitStatus(const std::string& project)> Build;
    std::function<::tsc::execute::tsc::ExitStatus(const std::string& project)> BuildReferences;
    std::function<::tsc::execute::tsc::ExitStatus(const std::string& project)> CleanReferences;
};

struct ConfigFileResponse {
    std::vector<std::string> FileNames; // nonnil
    ::tsc::CompilerOptions* Options = nullptr; // nonnil (borrowed — session snapshot owns)
    ::tsc::BuildOptions* BuildOptions = nullptr;
    std::vector<::tsc::ProjectReference*> ProjectReferences;
    ::tsc::TypeAcquisition* TypeAcquisition = nullptr;
    std::optional<bool> CompileOnSave;
    json::Value Raw; // `any` — pre-marshaled
    std::vector<std::shared_ptr<DiagnosticResponse>> Errors; // nonnil

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct ReadConfigFileResponse {
    json::Value Config; // `json:"config"` — pre-marshaled; `any` in Go
    std::shared_ptr<DiagnosticResponse> Error;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct GetDefaultProjectForFileParams {
    SnapshotID Snapshot{};
    DocumentIdentifier File;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct ProjectResponse {
    project::ID Id;
    std::string ConfigFileName;
    std::string CurrentDirectory;
    bool Dirty{};
    std::shared_ptr<ConfigFileResponse> ParsedCommandLine; // nonnil
    // Deprecated: Use parsedCommandLine.fileNames.
    std::vector<std::string> RootFiles; // nonnil
    // Deprecated: Use parsedCommandLine.options.
    ::tsc::CompilerOptions* CompilerOptions = nullptr; // nonnil (borrowed)

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// NewConfigFileResponse — proto.go:987. `parsedCommandLine` may be null.
std::shared_ptr<ConfigFileResponse> NewConfigFileResponse(tsoptions::ParsedCommandLine* parsedCommandLine);

// NewProjectResponse — proto.go:1035. Panics (TSC_UNREACHABLE) on unloaded project.
std::shared_ptr<ProjectResponse> NewProjectResponse(project::Project* p);

struct GetSymbolAtPositionParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    uint32_t Position{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetSymbolsAtPositionsParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    std::vector<uint32_t> Positions;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetSymbolOfSourceFileParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetSymbolsOfSourceFilesParams {
    SnapshotID Snapshot{};
    project::ID Project;
    std::vector<DocumentIdentifier> Files;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetSymbolAtLocationParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle Location;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetSymbolsAtLocationsParams {
    SnapshotID Snapshot{};
    project::ID Project;
    std::vector<NodeHandle> Locations;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// SymbolOwnerKind — proto.go:1129.
enum class SymbolOwnerKind : uint32_t {
    File = 0,
    Snapshot = 1,
};

// SymbolOwner — proto.go:1139. Embedded in SymbolReference.
struct SymbolOwner {
    SymbolOwnerKind Kind{};
    std::shared_ptr<SourceFileDescriptor> File; // omitempty
    SnapshotID Snapshot{}; // omitzero
    project::ID Project; // omitempty
};

// SymbolReference — proto.go:1147. Identifies a symbol and its
// server-resolvable owner.
struct SymbolReference : SymbolOwner {
    SymbolID Id{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// CompactSymbolReference — proto.go:1156. Identifies a cached symbol without
// repeating its owning file's full descriptor: File is the owning source
// file's node ID, or empty for a symbol owned by the response's snapshot.
struct CompactSymbolReference {
    SymbolID Id{};
    std::string File; // omitempty

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct SymbolResponse {
    SymbolReference Reference;
    std::string Name;
    uint32_t Flags{};
    uint32_t CheckFlags{};
    std::vector<NodeHandle> Declarations;
    NodeHandle ValueDeclaration;
    std::shared_ptr<CompactSymbolReference> Parent;
    std::shared_ptr<CompactSymbolReference> ExportSymbol;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct GetTypeOfSymbolParams {
    SnapshotID Snapshot{};
    project::ID Project;
    SymbolReference Symbol;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetTypesOfSymbolsParams {
    SnapshotID Snapshot{};
    project::ID Project;
    std::vector<SymbolReference> Symbols;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct TypeResponse {
    TypeID Id{};
    uint32_t Flags{};
    uint32_t ObjectFlags{};
    bool IsTupleType{};

    // Value is literal type data. BigInt literals are encoded as signed decimal
    // strings because JSON cannot represent bigint; absent values are null.
    json::Value Value; // `any` — pre-marshaled (literalValueToJSON)

    // ObjectType / TypeReference / StringMappingType / IndexType target
    TypeID Target{}; // omitzero

    // InterfaceType type parameters
    std::vector<TypeID> TypeParameters;
    std::vector<TypeID> OuterTypeParameters;
    std::vector<TypeID> LocalTypeParameters;

    // TupleType data
    std::vector<checker::ElementFlags> ElementFlags;
    std::optional<int64_t> FixedLength;
    std::optional<bool> TupleReadonly;
    std::vector<NodeHandle> LabeledElementDeclarations;

    // IndexedAccessType data
    TypeID ObjectType{}; // omitzero
    TypeID IndexType{}; // omitzero

    // ConditionalType data
    TypeID CheckType{}; // omitzero
    TypeID ExtendsType{}; // omitzero

    // SubstitutionType data
    TypeID BaseType{}; // omitzero
    TypeID SubstConstraint{}; // omitzero

    // MappedType data
    TypeID TypeParameter{}; // omitzero
    TypeID ConstraintType{}; // omitzero
    TypeID NameType{}; // omitzero
    TypeID TemplateType{}; // omitzero

    // TemplateLiteralType text segments
    std::vector<std::string> Texts;

    // FreshableType data (LiteralType and computed enum types)
    TypeID FreshType{}; // omitzero
    TypeID RegularType{}; // omitzero

    // TypeParameter data
    bool IsThisType{};

    // InterfaceType data
    TypeID ThisType{}; // omitzero

    // IntrinsicType data
    std::string IntrinsicName;

    // TypeAlias data
    std::vector<TypeID> AliasTypeArguments;
    std::shared_ptr<CompactSymbolReference> AliasSymbol; // omitempty

    // Symbol associated with structured types
    std::shared_ptr<CompactSymbolReference> Symbol; // omitempty

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// newTypeResponse — proto.go:1159.
std::shared_ptr<TypeResponse> newTypeResponse(checker::Type* t, TypeID id);
// typeHandles — proto.go:1240.
std::vector<TypeID> typeHandles(const std::vector<checker::Type*>& types);
// literalValueToJSON — proto.go:1251. Returns pre-marshaled JSON (Go `any`).
json::Value literalValueToJSON(const checker::LiteralValue& value);

struct ConstantValueResponse {
    bool IsNumber{};
    json::Value Value; // `any` — pre-marshaled

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct SignatureResponse {
    SignatureID Id{};
    uint32_t Flags{};
    NodeHandle Declaration;
    std::vector<TypeID> TypeParameters;
    std::vector<CompactSymbolReference> Parameters;
    std::shared_ptr<CompactSymbolReference> ThisParameter; // omitempty
    SignatureID Target{}; // omitzero

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct GetSourceFileParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetSourceFileNamesParams {
    SnapshotID Snapshot{};
    project::ID Project;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetModeForUsageLocationParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    NodeHandle Usage;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetModeForResolutionAtIndexParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    int64_t Index{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetResolvedModuleParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    std::string ModuleName;
    ::tsc::ResolutionMode Mode{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetResolvedModuleFromModuleSpecifierParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle ModuleSpecifier;
    std::shared_ptr<DocumentIdentifier> SourceFile;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetResolvedTypeReferenceDirectiveParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    std::string TypeDirectiveName;
    ::tsc::ResolutionMode Mode{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetResolvedTypeReferenceDirectiveFromReferenceParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier SourceFile;
    std::string TypeDirectiveName;
    ::tsc::ResolutionMode ResolutionMode{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct PackageId {
    std::string Name;
    std::string SubModuleName;
    std::string Version;
    std::string PeerDependencies;

    std::string marshalJSONTo(json::Encoder& enc) const;
	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// NewPackageId — proto.go:1403.
std::shared_ptr<PackageId> NewPackageId(const module::PackageId& packageID);

struct ResolvedModule {
    std::string ResolvedFileName;
    std::string OriginalPath;
    std::string Extension;
    bool ResolvedUsingTsExtension{};
    bool ResolvedUsingExtraExtensions{};
    std::shared_ptr<PackageId> PackageId;
    bool IsExternalLibraryImport{};
    std::string AlternateResult;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct ResolvedTypeReferenceDirective {
    bool Primary{};
    std::string ResolvedFileName;
    std::string OriginalPath;
    std::shared_ptr<PackageId> PackageId;
    bool IsExternalLibraryImport{};

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// SourceFileMetadata carries program-stored metadata about a single source file.
struct SourceFileMetadata {
    bool IsDefaultLibrary{};
    bool IsFromExternalLibrary{};
    std::string PackageJsonType;
    std::string PackageJsonDirectory;
    ::tsc::ResolutionMode ImpliedNodeFormat{};

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct ResolveNameParams {
    SnapshotID Snapshot{};
    project::ID Project;
    std::string Name;
    NodeHandle Location;       // Optional: node handle for location context
    std::shared_ptr<DocumentIdentifier> File; // Optional: file for location context (alternative to Location)
    std::optional<uint32_t> Position; // Optional: position in file for location context (with File)
    uint32_t Meaning{};        // SymbolFlags for what kind of symbol to find
    bool ExcludeGlobals{};     // Whether to exclude global symbols

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetSymbolsInScopeParams are parameters for getSymbolsInScope, which returns
// all symbols visible at a given location.
struct GetSymbolsInScopeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle Location;       // Optional: node handle for location context
    std::shared_ptr<DocumentIdentifier> File; // Optional: file for location context (alternative to Location)
    std::optional<uint32_t> Position; // Optional: position in file for location context (with File)
    uint32_t Meaning{};        // SymbolFlags for what kind of symbols to find

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetTypePropertyParams is used for all type sub-property endpoints.
struct GetTypePropertyParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Type{}; // `json:"objectId"`

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetSymbolPropertyParams is used for all symbol sub-property endpoints.
struct GetSymbolPropertyParams {
    SymbolReference Symbol;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetSignaturePropertyParams is used for all signature sub-property endpoints.
struct GetSignaturePropertyParams {
    SnapshotID Snapshot{};
    project::ID Project;
    SignatureID Signature{}; // `json:"objectId"`

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetContextualTypeParams returns the contextual type for a node.
struct GetContextualTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle Location;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetContextualTypeForArgumentParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle Location;
    int32_t Index{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetTypeOfSymbolAtLocationParams returns the narrowed type of a symbol at a specific location.
struct GetTypeOfSymbolAtLocationParams {
    SnapshotID Snapshot{};
    project::ID Project;
    SymbolReference Symbol;
    NodeHandle Location;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetReferencesToSymbolInFileParams are the parameters for the getReferencesToSymbolInFile method.
struct GetReferencesToSymbolInFileParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    SymbolReference Symbol;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetReferencedSymbolsForNodeParams are the parameters for the getReferencedSymbolsForNode method.
struct GetReferencedSymbolsForNodeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle Node;
    int64_t Position{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// ReferencedSymbolEntry represents a symbol definition and its references.
struct ReferencedSymbolEntry {
    NodeHandle Definition;
    std::shared_ptr<SymbolResponse> Symbol;
    std::vector<NodeHandle> References; // nonnil

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// GetSignatureUsagesParams are the parameters for the getSignatureUsages method.
struct GetSignatureUsagesParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle SignatureDecl;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// SignatureUsageResponse represents a single usage of a signature as a name-call pair.
struct SignatureUsageResponse {
    NodeHandle Name;
    NodeHandle Call;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// GetCompletionsAtPositionParams are the parameters for the getCompletionsAtPosition method.
struct GetCompletionsAtPositionParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    uint32_t Position{};
    std::optional<std::string> TriggerCharacter;
    bool IncludeSymbol{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// CompletionEntryLabelDetailsResponse holds additional label display text for a completion entry.
struct CompletionEntryLabelDetailsResponse {
    std::optional<std::string> Detail;
    std::optional<std::string> Description;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// CompletionEntryResponse represents a single completion item.
struct CompletionEntryResponse {
    std::string Name;
    uint32_t Kind{};
    std::optional<std::string> SortText;
    std::optional<std::string> InsertText;
    std::optional<std::string> FilterText;
    std::optional<std::string> Detail;
    std::shared_ptr<CompletionEntryLabelDetailsResponse> LabelDetails;
    std::shared_ptr<SymbolResponse> Symbol;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// CompletionInfoResponse wraps a list of completion entries.
struct CompletionInfoResponse {
    bool IsIncomplete{};
    std::vector<std::shared_ptr<CompletionEntryResponse>> Entries; // nonnil

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// GetIntrinsicTypeParams is used for intrinsic type getters (anyType, stringType, etc.).
struct GetIntrinsicTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// WellKnownSymbolsResponse carries the handle ids of the per-checker singleton
// symbols (unknown, undefined, arguments) so the client can identify them by id
// without a round-trip on every check.
struct WellKnownSymbolsResponse {
    SymbolID Unknown{};
    SymbolID Undefined{};
    SymbolID Arguments{};

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// WellKnownSignaturesResponse carries the handle id of the per-checker singleton
// unknown signature (the signature the checker yields when a call cannot be
// resolved) so the client can identify it by id without a round-trip on every check.
struct WellKnownSignaturesResponse {
    SignatureID Unknown{};

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// GetBaseTypeOfLiteralTypeParams returns the base type of a literal type.
struct GetBaseTypeOfLiteralTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Type{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetNonNullableTypeParams are the parameters for the getNonNullableType method.
struct GetNonNullableTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Type{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetTypeFromTypeNodeParams are the parameters for the getTypeFromTypeNode method.
struct GetTypeFromTypeNodeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle Location;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetWidenedTypeParams are the parameters for the getWidenedType method.
struct GetWidenedTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Type{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetParameterTypeParams are the parameters for the getParameterType method.
struct GetParameterTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    SignatureID Signature{};
    int32_t Index{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// IsArrayLikeTypeParams checks whether a type is array-like.
struct IsArrayLikeTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Type{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// IsTypeAssignableToParams checks assignability between two types.
struct IsTypeAssignableToParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Source{};
    TypeID Target{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetSignaturesOfTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Type{};
    int32_t Kind{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetResolvedSignatureParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle Location;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetTypeAtLocationParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle Location;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetTypeAtLocationsParams {
    SnapshotID Snapshot{};
    project::ID Project;
    std::vector<NodeHandle> Locations;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetTypeAtPositionParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    uint32_t Position{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetTypesAtPositionsParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    std::vector<uint32_t> Positions;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

using ImportAdderActionKind = std::string;

inline const ImportAdderActionKind ImportAdderActionKindImportSymbol = "importSymbol";

struct ImportAdderAction {
    ImportAdderActionKind Kind;
    std::shared_ptr<SymbolReference> Symbol; // omitempty
    std::optional<bool> IsValidTypeOnlyUseSite;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetImportAdderEditsParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;
    std::vector<ImportAdderAction> Actions;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct TextEdit {
    int64_t Pos{};
    int64_t End{};
    std::string NewText;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// TypeToTypeNodeParams are the parameters for the typeToTypeNode method.
struct TypeToTypeNodeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Type{};
    NodeHandle Location;
    int32_t Flags{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// SignatureToSignatureDeclarationParams are the parameters for the signatureToSignatureDeclaration method.
struct SignatureToSignatureDeclarationParams {
    SnapshotID Snapshot{};
    project::ID Project;
    SignatureID Signature{};
    int32_t Kind{};
    NodeHandle Location;
    int32_t Flags{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// PrintNodeParams are the parameters for the printNode method.
struct PrintNodeParams {
    std::string Data; // base64-encoded binary AST data
    bool PreserveSourceNewlines{};
    bool NeverAsciiEscape{};
    bool TerminateUnterminatedLiterals{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct EmitParams {
    SnapshotID Snapshot{};
    project::ID Project;
    std::optional<uint32_t> EmitOnly;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct SelectedFilesEmitParams {
    SnapshotID Snapshot{};
    project::ID Project;
    std::vector<DocumentIdentifier> Files;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct EmitResponse {
    bool EmitSkipped{};
    std::vector<std::shared_ptr<DiagnosticResponse>> Diagnostics; // nonnil
    std::vector<std::string> EmittedFiles; // nonnil
    // EmittedFilesContents contains contents parallel to EmittedFiles when the
    // source snapshot uses a full filesystem. It is empty for write-through emits.
    std::vector<std::string> EmittedFilesContents; // nonnil

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct EmitOutputFile {
    std::string FileName;
    std::string Text;
    std::optional<std::string> SourceFileName;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

struct EmitOutputResponse {
    bool EmitSkipped{};
    std::vector<std::shared_ptr<DiagnosticResponse>> Diagnostics; // nonnil
    std::vector<std::shared_ptr<EmitOutputFile>> OutputFiles; // nonnil

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// FormatNodeForInsertionParams are the parameters for the formatNodeForInsertion method.
struct FormatNodeForInsertionParams {
    SnapshotID Snapshot{};
    project::ID Project;
    DocumentIdentifier File;   // target file where the node will be inserted
    uint32_t Position{};       // UTF-16 code-unit offset of the insertion position in the target file
    std::string Data;          // base64-encoded binary AST data for the synthesized node

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// CheckerTypeParams are parameters for checker methods that operate on a type.
struct CheckerTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Type{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetPropertyOfTypeParams are parameters for getPropertyOfType (a named property of a type).
struct GetPropertyOfTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Type{};
    std::string Name;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct GetIndexInfoOfTypeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    TypeID Type{};
    int32_t Kind{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetMemberInModuleExportsParams are parameters for getMemberInModuleExports.
struct GetMemberInModuleExportsParams {
    SnapshotID Snapshot{};
    project::ID Project;
    SymbolReference Symbol;
    std::string Name;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// CheckerNodeParams are parameters for checker methods that operate on a node location.
struct CheckerNodeParams {
    SnapshotID Snapshot{};
    project::ID Project;
    NodeHandle Location;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// CheckerSymbolParams are parameters for checker methods that operate on a symbol.
struct CheckerSymbolParams {
    SnapshotID Snapshot{};
    project::ID Project;
    SymbolReference Symbol;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// JSDocTagInfo is a single JSDoc tag, mirroring Strada's JSDocTagInfo but with the tag text
// rendered as a plain string rather than SymbolDisplayPart[].
struct JSDocTagInfo {
    std::string Name;
    std::string Text;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// CheckerSignatureParams are parameters for checker methods that operate on a signature.
struct CheckerSignatureParams {
    SnapshotID Snapshot{};
    project::ID Project;
    SignatureID Signature{};

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// TypePredicateResponse is the response for getTypePredicateOfSignature.
struct TypePredicateResponse {
    int32_t Kind{};
    int32_t ParameterIndex{};
    std::string ParameterName;
    std::shared_ptr<TypeResponse> Type;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// IndexInfoResponse represents a single index signature.
struct IndexInfoResponse {
    TypeResponse KeyType;
    TypeResponse ValueType;
    bool IsReadonly{};
    NodeHandle Declaration;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// SourceFileResponse contains the binary-encoded AST data for a source file.
// The Data field is base64-encoded binary data in the encoder's format.
struct SourceFileResponse {
    // Data is the base64-encoded binary AST data in the encoder's format.
    std::string Data;

    std::string marshalJSONTo(json::Encoder& enc) const;
};

// GetDiagnosticsParams are parameters for per-file diagnostic methods.
struct GetDiagnosticsParams {
    SnapshotID Snapshot{};
    project::ID Project;
    std::vector<DocumentIdentifier> Files;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// GetProjectDiagnosticsParams are parameters for project-wide diagnostic methods.
struct GetProjectDiagnosticsParams {
    SnapshotID Snapshot{};
    project::ID Project;

	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct DiagnosticPositionResponse;
struct DiagnosticSourceLineResponse;

// DiagnosticResponse is the API response for a single diagnostic.
struct DiagnosticResponse {
    // FileName is the path of the file this diagnostic belongs to, if any.
    std::string FileName;
    // Pos is the start position of the diagnostic in the source file.
    int64_t Pos{};
    // End is the end position of the diagnostic in the source file.
    int64_t End{};
    // StartPosition is the zero-based line and UTF-16 character position of Pos.
    std::shared_ptr<DiagnosticPositionResponse> StartPosition;
    // EndPosition is the zero-based line and UTF-16 character position of End.
    std::shared_ptr<DiagnosticPositionResponse> EndPosition;
    // SourceLines contains the source lines needed to render this diagnostic with context.
    std::vector<std::shared_ptr<DiagnosticSourceLineResponse>> SourceLines;
    // Code is the diagnostic error code.
    int32_t Code{};
    // Category is the diagnostic category (error, warning, suggestion, message).
    DiagnosticCategory Category{};
    // Source is a custom diagnostic-code prefix. An empty value uses the default "TS".
    std::string Source;
    // Text is the localized diagnostic message text.
    std::string Text;
    // ReportsUnnecessary indicates this diagnostic highlights unnecessary code.
    bool ReportsUnnecessary{}; // omitzero
    // ReportsDeprecated indicates this diagnostic highlights deprecated code.
    bool ReportsDeprecated{}; // omitzero
    // MessageChain contains chained diagnostic messages, if any.
    std::vector<std::shared_ptr<DiagnosticResponse>> MessageChain;
    // RelatedInformation contains related diagnostic information, if any.
    std::vector<std::shared_ptr<DiagnosticResponse>> RelatedInformation;

    std::string marshalJSONTo(json::Encoder& enc) const;
	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string unmarshalJSONFrom(json::Decoder& dec);

    // ToDiagnostic — proto.go:1982.
    Diagnostic* ToDiagnostic() const;
};

struct DiagnosticPositionResponse {
    int64_t Line{};
    ::tsc::UTF16Offset Character{};

    std::string marshalJSONTo(json::Encoder& enc) const;
	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct DiagnosticSourceLineResponse {
    int64_t Line{};
    std::string Text;

    std::string marshalJSONTo(json::Encoder& enc) const;
	std::pair<bool, std::string> unmarshalField(std::string_view n, json::Decoder& d);
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// NewDiagnosticResponse converts an ast.Diagnostic to a DiagnosticResponse.
std::shared_ptr<DiagnosticResponse> NewDiagnosticResponse(Diagnostic* d);
// newDiagnosticResponse — proto.go:1957.
std::shared_ptr<DiagnosticResponse> newDiagnosticResponse(diagnosticwriter::ASTDiagnostic* d);
// NewDiagnosticResponses converts a slice of ast.Diagnostics to DiagnosticResponses.
std::vector<std::shared_ptr<DiagnosticResponse>> NewDiagnosticResponses(std::span<Diagnostic* const> diags);

// --- api-internal JSON helpers (proto.cpp) — shared by callbackfs.cpp ------
// fieldIs — case-folded JSON field-name match (encoding/json tag folding).
bool fieldIs(std::string_view name, std::string_view want);
// readFields — consumes a `{...}` object from dec, dispatching each member.
std::string readFields(json::Decoder& dec,
                       const std::function<std::string(std::string_view, json::Decoder&)>& readField);

} // namespace tsc::api
