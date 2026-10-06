// Session / snapshotData / checkerSetup / apiBuildSystem — session.go,
// module_resolution.go, server.go.
//
// State layout mirrors Go field-for-field. sync.RWMutex → std::shared_mutex,
// sync.Mutex → std::mutex, sync.Map → collections::SyncMap, sync.Once →
// std::once_flag, atomic.Uint64 → std::atomic<uint64_t>.
//
// Go `(any, error)` handler results are modeled by ResultValue: pre-marshaled
// JSON text or RawBinary bytes (the msgpack `result.(RawBinary)` type
// assertion). Go resolver errors — `ResolveModuleName*`'s third return — have
// no error slot in module::Resolver's C++ signature, so they surface as
// thrown runtime_errors (same treatment as Go panics).
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "internal/api/encoder/encoder.h"
#include "internal/api/proto.h"
#include "internal/api/protocol_msgpack.h"
#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/execute/build/build.h"
#include "internal/execute/tsc/compile.h"
#include "internal/gostd/gostd.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/module/resolver.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/module/staticresolver.h"
#include "internal/module/types.h"
#include "internal/pprof/pprof.h"
#include "internal/project/project.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc {
namespace ls { struct LanguageService; }
namespace lsconv { struct LSPLineMap; }
namespace printer { struct Printer; }
} // namespace tsc

namespace tsc::api {

// execute/build was ported at tsc::execute::build (Go execute/build);
// alias for the api handlers below.
namespace build = tsc::execute::build;


// moduleResolverFactory — module_resolution.go. Declared in
// module_resolution.h; fwd-declared here for Session's factory method.
struct moduleResolverFactory;
struct projectRegistryData;
class Session;
struct apiBuildSystem;

// ResultValue — a handler's `(any, error)` result before the conn marshals
// it. `data` holds either pre-marshaled JSON (isRaw=false) or RawBinary
// msgpack payload bytes (isRaw=true).
struct ResultValue {
	bool isRaw = false;
	std::string data;
};

// snapshotData holds the per-snapshot state including the snapshot itself
// and symbol/type registries scoped to this snapshot.
// Multiple clients may hold references to the same snapshot via ref counting;
// the registries are cleaned up when refCount reaches zero.
struct snapshotData {
	project::Snapshot* snapshot = nullptr;
	std::shared_ptr<vfs::FS> fileSystem;
	int refCount = 0;

	collections::Set<tspath::Path> openProjects;
	collections::Set<tspath::Path> openFiles;

	// Symbol IDs come from ast.GetSymbolId, a global atomic counter, so the same
	// Symbol pointer always has the same unique ID across all projects in the
	// snapshot. Symbols are registered snapshot-wide to ensure identity semantics:
	// querying the same symbol from two different projects returns the same handle.
	std::unordered_map<SymbolID, Symbol*> symbolRegistry;
	std::shared_mutex symbolRegistryMu;

	// symbolCanonicalProjects records, for each registered symbol, the project it was
	// first observed in. Because symbols are shared snapshot-wide (binder symbols are
	// attached to source files, which can be shared across projects), lookups that need
	// a project context (e.g. member/export ordering, node handle resolution) but don't
	// receive one from the caller default to this canonical project. First-writer wins so
	// the choice is stable. Guarded by symbolRegistryMu.
	std::unordered_map<SymbolID, project::ID> symbolCanonicalProjects;

	std::unordered_map<project::ID, std::unique_ptr<projectRegistryData>> projectRegistries;
	std::shared_mutex projectRegistriesMu;

	// getProgram looks up a program from a project handle within this snapshot.
	std::pair<compiler::SimpleProgram*, gostd::Error> getProgram(
	    const project::ID& projectHandle);
	// getProject looks up a project from a project handle within this snapshot.
	std::pair<project::Project*, gostd::Error> getProject(
	    const project::ID& projectHandle);
	// nodeHandleFrom creates an index-based node handle (index.kind.path), building a node index table
	// for the file on-demand if needed.
	NodeHandle nodeHandleFrom(Node* node);
	// getOrCreateProjectRegistry returns the registry for the given project, creating it if needed.
	projectRegistryData* getOrCreateProjectRegistry(const project::ID& projectID);
	// newSymbolResponse registers a symbol in the snapshot's registry and returns the response.
	std::unique_ptr<SymbolResponse> newSymbolResponse(
	    Symbol* symbol, const project::ID& canonicalProject);
	// registerSymbol registers a symbol in the snapshot's registry and returns its handle along with
	// its canonical project.
	std::pair<SymbolID, project::ID> registerSymbol(
	    Symbol* symbol, const project::ID& canonicalProject);
	// newTypeResponse registers a type in the project's registry and returns the response.
	std::unique_ptr<TypeResponse> newTypeResponse(
	    const project::ID& projectID, checker::Type* t, checker::Checker* c);
	TypeID registerType(const project::ID& projectID, checker::Type* t);
	// resolveSymbolHandle resolves a symbol handle within the snapshot's registry.
	std::pair<Symbol*, gostd::Error> resolveSymbolHandle(SymbolID handle);
	// resolveTypeHandle resolves a type handle within the project's registry.
	std::pair<checker::Type*, gostd::Error> resolveTypeHandle(
	    const project::ID& projectID, TypeID handle);
	// resolveSignatureHandle resolves a signature handle within the project's registry.
	std::pair<checker::Signature*, gostd::Error> resolveSignatureHandle(
	    const project::ID& projectID, SignatureID handle);
	// newSignatureResponse registers a signature in the project's registry and returns the response.
	std::unique_ptr<SignatureResponse> newSignatureResponse(
	    const project::ID& projectID, checker::Signature* sig);
	SignatureID registerSignature(const project::ID& projectID,
	                              checker::Signature* sig);
	std::pair<Node*, gostd::Error> resolveNodeHandle(
	    compiler::SimpleProgram* program, const NodeHandle& handle);
};

// moduleResolverRegistration — module_resolution.go.
struct moduleResolverRegistration {
	ModuleResolverID id;
	// Go field `compilerOptions *core.CompilerOptions` — the registration owns
	// a copy (params are request-scoped).
	std::unique_ptr<CompilerOptions> compilerOptions;
	std::unique_ptr<module::StaticResolutions> resolutions;
	std::string resolveModuleNameCallback;
};

// programResolutionContext — module_resolution.go.
struct programResolutionContext {
	module::ResolverOptions options;
	std::unordered_map<ModuleResolverID, std::shared_ptr<module::Resolver>>
	    resolvers;
	std::mutex mu;

	std::shared_ptr<module::Resolver> resolverFor(
	    moduleResolverRegistration* registration);
};

// projectRegistryData holds per-project type and signature registries.
// Types and signatures use per-checker sequential IDs, so the same local ID
// can appear in multiple projects. Separate maps per project prevent collisions
// and allow clean teardown when a project is removed.
struct projectRegistryData {
	std::unordered_map<TypeID, checker::Type*> typeRegistry;
	std::shared_mutex typeRegistryMu;

	std::unordered_map<SignatureID, checker::Signature*> signatureRegistry;
	std::shared_mutex signatureRegistryMu;
};

// batchResponsePage — session.go.
struct batchResponsePage {
	std::vector<json::Value> encodedResponses;
};

// checkerSetup holds the common context needed by handlers that require a type checker.
struct checkerSetup {
	snapshotData* sd = nullptr;
	compiler::SimpleProgram* program = nullptr;
	checker::Checker* checker = nullptr;
	std::function<void()> done;
	project::ID projectID;

	std::unique_ptr<SymbolResponse> newSymbolResponse(Symbol* sym);
	std::unique_ptr<SignatureResponse> newSignatureResponse(
	    checker::Signature* sig);
	std::unique_ptr<IndexInfoResponse> newIndexInfoResponse(
	    checker::IndexInfo* info);
	std::pair<checker::Type*, gostd::Error> resolveTypeHandle(TypeID id);
	std::pair<Symbol*, gostd::Error> resolveSymbolHandle(SymbolID id);
	std::pair<checker::Signature*, gostd::Error> resolveSignatureHandle(
	    SignatureID id);
	// resolveLocation resolves an optional location, given either as a node handle or as a
	// file and position. Returns nil when neither is provided.
	std::pair<Node*, gostd::Error> resolveLocation(
	    const NodeHandle& handle, const DocumentIdentifier* file,
	    const uint32_t* position);
};

// snapshotOpenState — session.go.
struct snapshotOpenState {
	collections::Set<tspath::Path> openProjects;
	collections::Set<tspath::Path> openFiles;
};

// languageServerSnapshotUpdate — session.go.
struct languageServerSnapshotUpdate {
	std::unique_ptr<project::APISnapshotRequest> request;
	snapshotOpenState openState;

	void commit(Session* s, project::Snapshot* snapshot);
};

// SessionOptions configures an API session.
struct SessionOptions {
	// UseBinaryResponses enables binary responses for msgpack protocol.
	bool UseBinaryResponses = false;
};

// Session represents an API session that provides programmatic access
// to TypeScript language services through the LSP server.
// It implements the Handler interface to process incoming API requests.
// The session supports multiple active snapshots, each with their own
// symbol and type registries for maintaining object identity.
//
// module::ResolutionHost base: Go `s` is passed as the resolution host in
// module_resolution.go (Host: s) — FS()/GetCurrentDirectory() back it.
class Session : public ipc::Handler, public module::ResolutionHost {
public:
	std::string id;
	project::SnapshotHost* snapshotHost = nullptr;
	bool ownsSnapshotHost = false;
	std::function<gostd::Context(gostd::Context)> withLocale;
	project::Session* projectSession = nullptr;

	std::once_flag closeOnce;

	// This is set to true when using MessagePackProtocol.
	bool useBinaryResponses = false;
	collections::SyncMap<std::string, batchResponsePage> batchResponsePages;
	std::atomic<uint64_t> nextBatchResponsePageID{0};

	// snapshots maps snapshot handles to their data. Each snapshot has its own
	// symbol/type registries.
	//
	// snapshotsMu guards the snapshots map. It is held only for
	// short, map-bounded critical sections, never across slow work like a project
	// snapshot update or checker queries. Read handlers (getSnapshotData and the
	// language-service handlers built on it) take it for reading; handleRelease and
	// snapshot creation bookkeeping takes it for writing. This is what
	// lets queries against an existing snapshot run concurrently with the building of
	// the next one.
	std::unordered_map<SnapshotID, std::unique_ptr<snapshotData>> snapshots;
	std::shared_mutex snapshotsMu;

	// openProjects, openFiles, and createdPrograms are the canonical LSP-state resources
	// owned by this API client. Guarded by languageServerUpdateMu.
	collections::Set<tspath::Path> openProjects;
	collections::Set<tspath::Path> openFiles;
	collections::Set<project::SyntheticProjectID> createdPrograms;

	std::mutex languageServerUpdateMu;

	std::unordered_map<BuildOrchestratorID, std::unique_ptr<build::Orchestrator>>
	    buildOrchestrators;
	std::mutex buildMu;

	std::atomic<uint64_t> nextModuleResolverID{0};
	std::unordered_map<ModuleResolverID,
	                   std::shared_ptr<moduleResolverRegistration>>
	    moduleResolvers;
	std::shared_mutex moduleResolversMu;
	std::atomic<uint64_t> nextProgramResolutionContextID{0};
	std::unordered_map<uint64_t, std::shared_ptr<programResolutionContext>>
	    programResolutionContexts;
	std::shared_mutex programResolutionContextsMu;
	std::unordered_map<SourceFileLeaseID,
	                   std::shared_ptr<project::SourceFileLease>>
	    sourceFileLeases;
	std::mutex sourceFileLeasesMu;
	std::atomic<uint64_t> nextSourceFileLeaseID{0};
	std::shared_ptr<ipc::Conn> conn;

	pprof::CPUProfiler cpuProfiler;

	// --- module::ResolutionHost (Go `Host: s` in module_resolution.go) ---
	bool FileExists(std::string_view path) override {
		return FS()->FileExists(std::string(path));
	}
	bool DirectoryExists(std::string_view path) override {
		return FS()->DirectoryExists(std::string(path));
	}
	std::optional<std::string> ReadFile(std::string_view path) override {
		auto pair = FS()->ReadFile(std::string(path));
		if (!pair.second) return std::nullopt;
		return pair.first;
	}
	std::string Realpath(std::string_view path) override {
		return FS()->Realpath(std::string(path));
	}
	std::string GetCurrentDirectory() override;
	bool UseCaseSensitiveFileNames() override {
		return FS()->UseCaseSensitiveFileNames();
	}
	AccessibleEntries GetAccessibleEntries(std::string_view path) override {
		auto entries = FS()->GetAccessibleEntries(std::string(path));
		return {std::move(entries.files), std::move(entries.directories),
		        std::move(entries.symlinks)};
	}

	// --- ipc::Handler ---
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context ctx, std::string_view method,
	    const json::Value& params) override;
	gostd::Error HandleNotification(gostd::Context ctx, std::string_view method,
	                                const json::Value& params) override;

	// --- session.go ---
	std::string ID();
	void SetConnection(std::shared_ptr<ipc::Conn> conn);
	std::shared_ptr<vfs::FS> FS();
	std::string DefaultLibraryPath();
	bool useCaseSensitiveFileNames();

	std::pair<snapshotData*, gostd::Error> getSnapshotData(SnapshotID handle);
	std::pair<snapshotData*, gostd::Error> retainSnapshotData(SnapshotID handle);
	gostd::Error releaseSnapshot(SnapshotID handle);

	std::pair<checkerSetup, gostd::Error> setupChecker(
	    gostd::Context ctx, SnapshotID snapshot,
	    const project::ID& projectHandle);
	std::pair<ls::LanguageService*, gostd::Error> setupLanguageService(
	    project::Snapshot* snapshot, compiler::SimpleProgram* program,
	    const project::ID& projectHandle, const std::string& activeFile);

	// handleRequest — Session.HandleRequest (session.go:694); raw results
	// ride `ResultValue::isRaw` so batch/source-file handling can spot
	// RawBinary like Go's `result.(RawBinary)`.
	std::pair<ResultValue, gostd::Error> handleRequest(
	    gostd::Context ctx, const std::string& method, const json::Value& params);

	std::pair<std::unique_ptr<BatchRequestsResponse>, gostd::Error>
	handleBatchRequests(gostd::Context ctx, const BatchRequestsParams* params);
	std::pair<std::unique_ptr<BatchRequestsResponse>, gostd::Error>
	paginateBatchResponses(batchResponsePage& page,
	                       std::vector<BatchResponse>* responses,
	                       int64_t maxResponseBytesPerPage);
	BatchResponse handleBatchRequest(gostd::Context ctx,
	                                 const BatchRequest& request);

	std::pair<ResultValue, gostd::Error> handleStartCPUProfile(
	    gostd::Context ctx, const ProfileParams* params);
	std::pair<std::unique_ptr<ProfileResult>, gostd::Error> handleStopCPUProfile(
	    gostd::Context ctx);
	std::pair<std::unique_ptr<ProfileResult>, gostd::Error> handleSaveHeapProfile(
	    gostd::Context ctx, const ProfileParams* params);

	std::pair<std::unique_ptr<InitializeResponse>, gostd::Error>
	handleInitialize(gostd::Context ctx);
	std::pair<std::unique_ptr<CreateSnapshotResponse>, gostd::Error>
	handleCreateSnapshot(gostd::Context ctx, const CreateSnapshotParams* params);
	std::pair<std::unique_ptr<CreateSnapshotResponse>, gostd::Error>
	handleUpdateSnapshot(gostd::Context ctx, const UpdateSnapshotParams* params);
	std::pair<std::unique_ptr<project::APISnapshotRequest>, gostd::Error>
	toAPISnapshotRequest(gostd::Context ctx,
	                     const SnapshotRequestChangesParams* changes);
	std::pair<std::unique_ptr<languageServerSnapshotUpdate>, gostd::Error>
	toLanguageServerSnapshotUpdate(
	    gostd::Context ctx, const SnapshotRequestChangesParams* changes);
	snapshotOpenState reconcileSnapshotOpens(
	    project::APISnapshotRequest* apiRequest, snapshotOpenState base);
	void registerSnapshot(project::Snapshot* snapshot, snapshotOpenState openState,
	                      const std::shared_ptr<vfs::FS>& fileSystem);
	std::pair<std::unique_ptr<CreateSnapshotResponse>, gostd::Error>
	handleGetCurrentLanguageServerSnapshot(
	    gostd::Context ctx, const GetCurrentLanguageServerSnapshotParams* params);
	std::unique_ptr<CreateSnapshotResponse> createSnapshotResponse(
	    project::Snapshot* snapshot, project::Snapshot* base,
	    const SnapshotRequestChangesParams* request);
	std::unique_ptr<SnapshotOperationResponse> createSnapshotOperationResponse(
	    project::Snapshot* snapshot, const SnapshotRequestChangesParams* request);
	std::pair<ResultValue, gostd::Error> handleRelease(
	    gostd::Context ctx, const ReleaseParams* params);
	std::pair<std::unique_ptr<ProjectResponse>, gostd::Error>
	handleGetDefaultProjectForFile(
	    gostd::Context ctx, const GetDefaultProjectForFileParams* params);

	std::pair<std::unique_ptr<CreateBuildOrchestratorResponse>, gostd::Error>
	handleCreateBuildOrchestrator(gostd::Context ctx,
	                              const CreateBuildOrchestratorParams* params);
	std::pair<ResultValue, gostd::Error> handleDisposeBuildOrchestrator(
	    gostd::Context ctx, const DisposeBuildOrchestratorParams* params);
	std::pair<std::unique_ptr<BuildResponse>, gostd::Error> handleBuild(
	    gostd::Context ctx, const BuildParams* params);
	std::pair<std::unique_ptr<BuildResponse>, gostd::Error>
	handleBuildReferences(gostd::Context ctx, const BuildParams* params);
	std::pair<std::unique_ptr<CleanBuildResponse>, gostd::Error>
	handleCleanBuild(gostd::Context ctx, const CleanBuildParams* params);
	std::pair<std::unique_ptr<CleanBuildResponse>, gostd::Error>
	handleCleanReferences(gostd::Context ctx, const CleanBuildParams* params);
	apiBuildSystem* getBuildSys(const CreateBuildOrchestratorParams* params);

	std::pair<std::unique_ptr<ConfigFileResponse>, gostd::Error>
	handleParseCommandLine(gostd::Context ctx,
	                       const ParseCommandLineParams* params);
	std::pair<std::unique_ptr<ReadConfigFileResponse>, gostd::Error>
	handleReadConfigFile(gostd::Context ctx,
	                     const ReadConfigFileParams* params);
	std::pair<std::unique_ptr<ConfigFileResponse>, gostd::Error>
	handleParseJsonConfigFileContent(
	    gostd::Context ctx, const ParseJsonConfigFileContentParams* params);
	std::pair<std::unique_ptr<ConfigFileResponse>, gostd::Error>
	handleParseConfigFile(gostd::Context ctx,
	                      const ParseConfigFileParams* params);
	std::pair<std::unique_ptr<TranspileOutputResponse>, gostd::Error>
	handleTranspile(gostd::Context ctx, const TranspileParams* params,
	                bool declaration);
	std::pair<ResultValue, gostd::Error> handleCreateSourceFile(
	    gostd::Context ctx, const CreateSourceFileParams* params);
	std::pair<ResultValue, gostd::Error> handleCreateSourceFileFromFile(
	    gostd::Context ctx, const CreateSourceFileFromFileParams* params);
	std::pair<std::shared_ptr<project::SourceFileLease>, gostd::Error>
	createSourceFile(const std::string& fileName, const std::string& sourceText,
	                 const CreateSourceFileOptions& options);
	std::shared_ptr<project::SourceFileLease> acquireSourceFile(
	    SourceFileParseOptions options, const std::string& sourceText,
	    ScriptKind scriptKind);
	std::pair<ResultValue, gostd::Error> encodeLeasedSourceFile(
	    std::shared_ptr<project::SourceFileLease> lease);
	std::pair<ResultValue, gostd::Error> handleReleaseSourceFile(
	    const ReleaseSourceFileParams* params);
	void releaseSourceFileLeases();
	std::pair<std::unique_ptr<TranspileOutputResponse>, gostd::Error>
	handleTranspileFromFile(gostd::Context ctx,
	                       const TranspileFromFileParams* params,
	                       bool declaration);
	std::pair<ResultValue, gostd::Error> handleGetSourceFile(
	    gostd::Context ctx, const GetSourceFileParams* params);
	std::pair<std::vector<std::string>, gostd::Error>
	handleGetConfigFileNames(gostd::Context ctx,
	                         const GetProjectDiagnosticsParams* params);
	std::pair<ResultValue, gostd::Error> handleGetConfigSourceFile(
	    gostd::Context ctx, const GetSourceFileParams* params);
	std::pair<ResultValue, gostd::Error> encodeSourceFileResponse(
	    SourceFile* sourceFile);
	std::pair<std::vector<std::string>, gostd::Error>
	handleGetSourceFileNames(gostd::Context ctx,
	                         const GetSourceFileNamesParams* params);
	std::pair<std::unique_ptr<SourceFileMetadata>, gostd::Error>
	handleGetSourceFileMetadata(gostd::Context ctx,
	                            const GetSourceFileParams* params);

	std::pair<ResolutionMode, gostd::Error> handleGetModeForUsageLocation(
	    gostd::Context ctx, const GetModeForUsageLocationParams* params);
	std::pair<ResolutionMode, gostd::Error>
	handleGetModeForResolutionAtIndex(
	    gostd::Context ctx, const GetModeForResolutionAtIndexParams* params);
	std::pair<std::unique_ptr<ResolvedModule>, gostd::Error>
	handleGetResolvedModule(gostd::Context ctx,
	                       const GetResolvedModuleParams* params);
	std::pair<std::unique_ptr<ResolvedModule>, gostd::Error>
	handleGetResolvedModuleFromModuleSpecifier(
	    gostd::Context ctx,
	    const GetResolvedModuleFromModuleSpecifierParams* params);
	std::pair<std::unique_ptr<ResolvedTypeReferenceDirective>, gostd::Error>
	handleGetResolvedTypeReferenceDirective(
	    gostd::Context ctx, const GetResolvedTypeReferenceDirectiveParams* params);
	std::pair<std::unique_ptr<ResolvedTypeReferenceDirective>, gostd::Error>
	handleGetResolvedTypeReferenceDirectiveFromReference(
	    gostd::Context ctx,
	    const GetResolvedTypeReferenceDirectiveFromReferenceParams* params);

	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetSymbolAtPosition(gostd::Context ctx,
	                          const GetSymbolAtPositionParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetSymbolOfSourceFile(gostd::Context ctx,
	                            const GetSymbolOfSourceFileParams* params);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	handleGetSymbolsOfSourceFiles(gostd::Context ctx,
	                              const GetSymbolsOfSourceFilesParams* params);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	handleGetSymbolsAtPositions(gostd::Context ctx,
	                            const GetSymbolsAtPositionsParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetSymbolAtLocation(gostd::Context ctx,
	                          const GetSymbolAtLocationParams* params);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	handleGetSymbolsAtLocations(gostd::Context ctx,
	                            const GetSymbolsAtLocationsParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error> handleGetTypeOfSymbol(
	    gostd::Context ctx, const GetTypeOfSymbolParams* params);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetTypesOfSymbols(gostd::Context ctx,
	                        const GetTypesOfSymbolsParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetDeclaredTypeOfSymbol(gostd::Context ctx,
	                              const GetTypeOfSymbolParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetNonMissingTypeOfSymbol(gostd::Context ctx,
	                                const GetTypeOfSymbolParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error> handleResolveName(
	    gostd::Context ctx, const ResolveNameParams* params);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	handleGetSymbolsInScope(gostd::Context ctx,
	                        const GetSymbolsInScopeParams* params);
	std::pair<std::vector<std::unique_ptr<SignatureResponse>>, gostd::Error>
	handleGetSignaturesOfType(gostd::Context ctx,
	                          const GetSignaturesOfTypeParams* params);
	std::pair<std::unique_ptr<SignatureResponse>, gostd::Error>
	handleGetResolvedSignature(gostd::Context ctx,
	                           const GetResolvedSignatureParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetTypeAtLocation(gostd::Context ctx,
	                        const GetTypeAtLocationParams* params);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetTypeAtLocations(gostd::Context ctx,
	                         const GetTypeAtLocationsParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetTypeAtPosition(gostd::Context ctx,
	                        const GetTypeAtPositionParams* params);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetTypesAtPositions(gostd::Context ctx,
	                          const GetTypesAtPositionsParams* params);

	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetParentOfSymbol(gostd::Context ctx,
	                        const GetSymbolPropertyParams* params);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	handleGetMembersOfSymbol(gostd::Context ctx,
	                         const GetSymbolPropertyParams* params);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	handleGetExportsOfSymbol(gostd::Context ctx,
	                         const GetSymbolPropertyParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetExportSymbolOfSymbol(gostd::Context ctx,
	                              const GetSymbolPropertyParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetSymbolOfType(gostd::Context ctx,
	                      const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetTargetOfType(gostd::Context ctx,
	                      const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetFreshTypeOfType(gostd::Context ctx,
	                         const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetRegularTypeOfType(gostd::Context ctx,
	                           const GetTypePropertyParams* params);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetTypesOfType(gostd::Context ctx,
	                     const GetTypePropertyParams* params);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetTypeParametersOfType(gostd::Context ctx,
	                              const GetTypePropertyParams* params);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetOuterTypeParametersOfType(gostd::Context ctx,
	                                   const GetTypePropertyParams* params);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetLocalTypeParametersOfType(gostd::Context ctx,
	                                   const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetThisTypeOfType(gostd::Context ctx,
	                        const GetTypePropertyParams* params);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetAliasTypeArgumentsOfType(gostd::Context ctx,
	                                  const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetAliasSymbolOfType(gostd::Context ctx,
	                           const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetObjectTypeOfType(gostd::Context ctx,
	                          const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetIndexTypeOfType(gostd::Context ctx,
	                         const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetCheckTypeOfType(gostd::Context ctx,
	                         const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetExtendsTypeOfType(gostd::Context ctx,
	                           const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetBaseTypeOfType(gostd::Context ctx,
	                        const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetConstraintOfType(gostd::Context ctx,
	                          const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetTypeParameterOfMappedType(gostd::Context ctx,
	                                   const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetConstraintTypeOfMappedType(gostd::Context ctx,
	                                    const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetNameTypeOfMappedType(gostd::Context ctx,
	                              const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetTemplateTypeOfMappedType(gostd::Context ctx,
	                                  const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetTrueTypeOfConditionalType(gostd::Context ctx,
	                                   const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetFalseTypeOfConditionalType(gostd::Context ctx,
	                                    const GetTypePropertyParams* params);

	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetTypeParametersOfSignature(gostd::Context ctx,
	                                   const GetSignaturePropertyParams* params);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	handleGetParametersOfSignature(gostd::Context ctx,
	                               const GetSignaturePropertyParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetThisParameterOfSignature(gostd::Context ctx,
	                                  const GetSignaturePropertyParams* params);
	std::pair<std::unique_ptr<SignatureResponse>, gostd::Error>
	handleGetTargetOfSignature(gostd::Context ctx,
	                           const GetSignaturePropertyParams* params);

	std::pair<std::vector<std::unique_ptr<TextEdit>>, gostd::Error>
	handleGetImportAdderEdits(gostd::Context ctx,
	                          const GetImportAdderEditsParams* params);

	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	resolveTypePropertyOfType(
	    gostd::Context ctx, const GetTypePropertyParams* params,
	    const std::function<checker::Type*(checker::Type*)>& getter);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	resolveTypeArrayPropertyOfType(
	    gostd::Context ctx, const GetTypePropertyParams* params,
	    const std::function<std::vector<checker::Type*>(checker::Type*)>& getter);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	resolveSymbolPropertyOfType(
	    const GetTypePropertyParams* params,
	    const std::function<Symbol*(checker::Type*)>& getter);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	resolveSymbolPropertyOfSymbol(
	    const GetSymbolPropertyParams* params,
	    const std::function<Symbol*(Symbol*)>& getter);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	resolveSymbolTablePropertyOfSymbol(
	    gostd::Context ctx, const GetSymbolPropertyParams* params,
	    const std::function<const SymbolTable*(Symbol*)>& getter);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	resolveSymbolArrayPropertyOfSignature(
	    const GetSignaturePropertyParams* params,
	    const std::function<std::vector<Symbol*>(checker::Signature*)>&
	        getter);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	resolveSymbolPropertyOfSignature(
	    const GetSignaturePropertyParams* params,
	    const std::function<Symbol*(checker::Signature*)>& getter);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	resolveTypeArrayPropertyOfSignature(
	    gostd::Context ctx, const GetSignaturePropertyParams* params,
	    const std::function<std::vector<checker::Type*>(checker::Signature*)>&
	        getter);
	std::pair<std::unique_ptr<SignatureResponse>, gostd::Error>
	resolveSignaturePropertyOfSignature(
	    const GetSignaturePropertyParams* params,
	    const std::function<checker::Signature*(checker::Signature*)>& getter);

	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetContextualType(gostd::Context ctx,
	                        const GetContextualTypeParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetContextualTypeForArgument(
	    gostd::Context ctx, const GetContextualTypeForArgumentParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error> handleGetAwaitedType(
	    gostd::Context ctx, const CheckerTypeParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetBaseTypeOfLiteralType(
	    gostd::Context ctx, const GetBaseTypeOfLiteralTypeParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetNonNullableType(gostd::Context ctx,
	                         const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetTypeFromTypeNode(gostd::Context ctx,
	                          const GetTypeFromTypeNodeParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error> handleGetWidenedType(
	    gostd::Context ctx, const GetWidenedTypeParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetParameterType(gostd::Context ctx,
	                       const GetParameterTypeParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetTypeParameterAtPosition(gostd::Context ctx,
	                                 const GetParameterTypeParams* params);
	std::pair<bool, gostd::Error> handleIsArrayLikeType(
	    gostd::Context ctx, const IsArrayLikeTypeParams* params);
	std::pair<bool, gostd::Error> handleIsTypeAssignableTo(
	    gostd::Context ctx, const IsTypeAssignableToParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetShorthandAssignmentValueSymbol(
	    gostd::Context ctx, const GetTypeAtLocationParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetTypeOfSymbolAtLocation(
	    gostd::Context ctx, const GetTypeOfSymbolAtLocationParams* params);
	std::pair<ResultValue, gostd::Error> handleTypeToTypeNode(
	    gostd::Context ctx, const TypeToTypeNodeParams* params);
	std::pair<ResultValue, gostd::Error> handleSignatureToSignatureDeclaration(
	    gostd::Context ctx, const SignatureToSignatureDeclarationParams* params);
	std::pair<ResultValue, gostd::Error> handleTypeToString(
	    gostd::Context ctx, const TypeToTypeNodeParams* params);
	std::pair<std::string, gostd::Error> handlePrintNode(
	    gostd::Context ctx, const PrintNodeParams* params);

	std::pair<std::unique_ptr<EmitResponse>, gostd::Error> handleEmit(
	    gostd::Context ctx, const EmitParams* params);
	std::pair<std::unique_ptr<EmitOutputResponse>, gostd::Error>
	handleEmitToString(gostd::Context ctx, const EmitParams* params);
	std::pair<std::unique_ptr<EmitOutputResponse>, gostd::Error>
	handleSelectedFilesEmit(gostd::Context ctx,
	                        const SelectedFilesEmitParams* params,
	                        compiler::EmitOnly emitOnly);
	std::tuple<compiler::SimpleProgram*, compiler::EmitOptions, gostd::Error>
	getEmitOptions(const EmitParams* params);
	std::pair<compiler::SimpleProgram*, gostd::Error> getEmitProgram(
	    SnapshotID snapshot, const project::ID& projectID);

	std::pair<std::string, gostd::Error> handleFormatNodeForInsertion(
	    gostd::Context ctx, const FormatNodeForInsertionParams* params);

	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetIntrinsicType(
	    gostd::Context ctx, const GetIntrinsicTypeParams* params,
	    const std::function<checker::Type*(checker::Checker*)>& getter);
	std::pair<std::unique_ptr<WellKnownSymbolsResponse>, gostd::Error>
	handleGetWellKnownSymbols(gostd::Context ctx,
	                          const GetIntrinsicTypeParams* params);
	std::pair<std::unique_ptr<WellKnownSignaturesResponse>, gostd::Error>
	handleGetWellKnownSignatures(gostd::Context ctx,
	                             const GetIntrinsicTypeParams* params);
	std::pair<bool, gostd::Error> handleIsContextSensitive(
	    gostd::Context ctx, const GetContextualTypeParams* params);

	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetReturnTypeOfSignature(gostd::Context ctx,
	                               const GetSignaturePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetRestTypeOfSignature(gostd::Context ctx,
	                             const CheckerSignatureParams* params);
	std::pair<std::unique_ptr<TypePredicateResponse>, gostd::Error>
	handleGetTypePredicateOfSignature(gostd::Context ctx,
	                                  const CheckerSignatureParams* params);
	std::pair<bool, gostd::Error> handleIsArrayType(
	    gostd::Context ctx, const CheckerTypeParams* params);
	std::pair<bool, gostd::Error> handleIsReadonlySymbol(
	    gostd::Context ctx, const CheckerSymbolParams* params);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetBaseTypes(gostd::Context ctx, const CheckerTypeParams* params);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	handleGetPropertiesOfType(gostd::Context ctx,
	                          const CheckerTypeParams* params);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	handleGetApparentPropertiesOfType(gostd::Context ctx,
	                                  const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetApparentType(gostd::Context ctx,
	                      const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetReducedType(gostd::Context ctx,
	                     const GetTypePropertyParams* params);
	std::pair<std::vector<std::unique_ptr<IndexInfoResponse>>, gostd::Error>
	handleGetIndexInfosOfType(gostd::Context ctx,
	                          const CheckerTypeParams* params);
	std::tuple<checkerSetup, checker::Type*, checker::Type*, gostd::Error>
	resolveIndexInfoRequest(gostd::Context ctx,
	                        const GetIndexInfoOfTypeParams* params);
	std::pair<std::unique_ptr<IndexInfoResponse>, gostd::Error>
	handleGetIndexInfoOfType(gostd::Context ctx,
	                         const GetIndexInfoOfTypeParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetConstraintOfTypeParameter(gostd::Context ctx,
	                                   const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetDefaultFromTypeParameter(gostd::Context ctx,
	                                  const GetTypePropertyParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetBaseConstraintOfType(gostd::Context ctx,
	                              const CheckerTypeParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetPropertyOfType(gostd::Context ctx,
	                        const GetPropertyOfTypeParams* params);
	std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
	handleGetTypeOfPropertyOfType(gostd::Context ctx,
	                              const GetPropertyOfTypeParams* params);
	std::pair<std::unique_ptr<ConstantValueResponse>, gostd::Error>
	handleGetConstantValue(gostd::Context ctx,
	                       const CheckerNodeParams* params);
	std::pair<std::unique_ptr<SignatureResponse>, gostd::Error>
	handleGetSignatureFromDeclaration(gostd::Context ctx,
	                                  const CheckerNodeParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetExportSpecifierLocalTargetSymbol(gostd::Context ctx,
	                                          const CheckerNodeParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetAliasedSymbol(gostd::Context ctx,
	                       const CheckerSymbolParams* params);
	std::pair<std::string, gostd::Error> handleGetFullyQualifiedName(
	    gostd::Context ctx, const CheckerSymbolParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetImmediateAliasedSymbol(gostd::Context ctx,
	                                const CheckerSymbolParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleMethodGetTargetSymbol(gostd::Context ctx,
	                            const CheckerSymbolParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetExportSymbolOfSymbolForChecker(gostd::Context ctx,
	                                        const CheckerSymbolParams* params);
	std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
	handleGetExportsOfModule(gostd::Context ctx,
	                         const CheckerSymbolParams* params);
	std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
	handleGetMemberInModuleExports(
	    gostd::Context ctx, const GetMemberInModuleExportsParams* params);
	std::pair<std::vector<std::unique_ptr<JSDocTagInfo>>, gostd::Error>
	handleGetJSDocTags(gostd::Context ctx, const CheckerSymbolParams* params);
	std::pair<std::string, gostd::Error> handleGetDocumentationComment(
	    gostd::Context ctx, const CheckerSymbolParams* params);
	std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
	handleGetTypeArguments(gostd::Context ctx, const CheckerTypeParams* params);

	// --- module_resolution.go ---
	// Go name `moduleResolverFactory` — same as the type; the qualified
	// return type avoids the member-name lookup ambiguity.
	std::pair<std::unique_ptr<tsc::api::moduleResolverFactory>, gostd::Error>
	moduleResolverFactory(gostd::Context ctx,
	                      const CreateProgramOptions* options);
	uint64_t registerProgramResolutionContext(
	    std::shared_ptr<module::Resolver> resolver,
	    const module::ResolverOptions& options,
	    std::shared_ptr<moduleResolverRegistration> registration);
	void releaseProgramResolutionContext(uint64_t id);
	std::pair<ModuleResolverID, gostd::Error> handleCreateModuleResolver(
	    const CreateModuleResolverParams* params);
	std::pair<ResultValue, gostd::Error> handleReleaseModuleResolver(
	    const ReleaseModuleResolverParams* params);
	std::pair<std::unique_ptr<ResolveModuleNameResult>, gostd::Error>
	handleResolveModuleName(gostd::Context ctx,
	                        const ResolveModuleNameParams* params);

	// Close closes the session and releases all active snapshots,
	// regardless of their ref counts.
	void Close();
	void releaseLanguageServerRefs();
	tspath::Path toPath(const std::string& fileName);
	project::FileChangeSummary toFileChangeSummary(
	    const FileNotifications* changes);

	std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
	getDiagnostics(
	    gostd::Context ctx, const GetDiagnosticsParams* params,
	    const std::function<std::vector<Diagnostic*>(
	        compiler::SimpleProgram*, gostd::Context, SourceFile*)>& getter);
	std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
	handleGetSyntacticDiagnostics(gostd::Context ctx,
	                              const GetDiagnosticsParams* params);
	std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
	handleGetBindDiagnostics(gostd::Context ctx,
	                         const GetDiagnosticsParams* params);
	std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
	handleGetSemanticDiagnostics(gostd::Context ctx,
	                             const GetDiagnosticsParams* params);
	std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
	handleGetSuggestionDiagnostics(gostd::Context ctx,
	                               const GetDiagnosticsParams* params);
	std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
	handleGetDeclarationDiagnostics(gostd::Context ctx,
	                                const GetDiagnosticsParams* params);
	std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
	handleGetConfigFileParsingDiagnostics(
	    gostd::Context ctx, const GetProjectDiagnosticsParams* params);
	std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
	handleGetProgramDiagnostics(gostd::Context ctx,
	                            const GetProjectDiagnosticsParams* params);
	std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
	handleGetGlobalDiagnostics(gostd::Context ctx,
	                           const GetProjectDiagnosticsParams* params);

	std::pair<SourceFile*, gostd::Error> resolveOptionalSourceFile(
	    compiler::SimpleProgram* program, const DocumentIdentifier* file);
	std::pair<std::vector<NodeHandle>, gostd::Error>
	handleGetReferencesToSymbolInFile(
	    gostd::Context ctx, const GetReferencesToSymbolInFileParams* params);
	std::pair<std::vector<SignatureUsageResponse>, gostd::Error>
	handleGetSignatureUsages(gostd::Context ctx,
	                         const GetSignatureUsagesParams* params);
	std::pair<std::unique_ptr<CompletionInfoResponse>, gostd::Error>
	handleGetCompletionsAtPosition(
	    gostd::Context ctx, const GetCompletionsAtPositionParams* params);
	std::pair<std::vector<ReferencedSymbolEntry>, gostd::Error>
	handleGetReferencedSymbolsForNode(
	    gostd::Context ctx, const GetReferencedSymbolsForNodeParams* params);
};

// DefaultMaxResponseBytesPerPage leaves room for base64 expansion beneath V8's
// maximum string length while rounding down to an even decimal value.
inline constexpr int64_t DefaultMaxResponseBytesPerPage = 300'000'000;

// NewLSPSession creates a new API session with the given project session.
std::shared_ptr<Session> NewLSPSession(project::Session* projectSession,
                                      const SessionOptions* options);
// NewStandaloneSession creates an API session with an independently owned snapshot host.
std::shared_ptr<Session> NewStandaloneSession(project::SessionInit* init,
                                              const SessionOptions* options);

// snapshotHandle creates a snapshot handle from a snapshot's ID.
SnapshotID snapshotHandle(project::Snapshot* snapshot);
bool isSourceFileResponseMethod(Method method);
bool isValidCreateSourceFileScriptKind(ScriptKind scriptKind);
std::pair<std::unique_ptr<TranspileOutputResponse>, gostd::Error>
transpileOutput(gostd::Context ctx, const std::string& input,
                const TranspileOptions& options, bool declaration);
std::unique_ptr<ResolvedModule> newResolvedModuleResponse(
    module::ResolvedModule* resolution);
std::unique_ptr<ResolvedTypeReferenceDirective>
newResolvedTypeReferenceDirectiveResponse(
    module::ResolvedTypeReferenceDirective* resolution);
std::pair<Node*, gostd::Error> decodePrintNode(
    const std::string& encoded);
printer::Printer* newPrinter(const PrintNodeParams* params);
std::pair<std::unique_ptr<EmitOutputResponse>, gostd::Error> emitToOutput(
    gostd::Context ctx, compiler::SimpleProgram* program,
    compiler::EmitOptions options);
std::pair<compiler::EmitOnly, gostd::Error> getEmitOnly(const uint32_t* value);
std::pair<std::unique_ptr<compiler::EmitResult>, gostd::Error> emitProgram(
    gostd::Context ctx, compiler::SimpleProgram* program,
    const compiler::EmitOptions& options);
std::vector<std::shared_ptr<DiagnosticResponse>> nonNilDiagnostics(
    const std::vector<Diagnostic*>& diags);
std::vector<std::unique_ptr<TextEdit>> toAPITextEdits(
    SourceFile* sourceFile,
    const std::vector<lsp::lsproto::TextEdit*>& edits);
std::pair<int, bool> originalTextOffset(lsconv::LSPLineMap* lineMap,
                                        lsp::lsproto::Position position,
                                        int textLength);
std::unique_ptr<SnapshotChanges> computeSnapshotChanges(
    project::Snapshot* prev, project::Snapshot* next);
std::string formatSessionID(uint64_t id);

// --- module_resolution.go ---
std::pair<std::unique_ptr<module::StaticResolutions>, gostd::Error>
compileModuleResolutionSpec(
    const ModuleResolutionSpec* spec, const std::string& currentDirectory,
    bool useCaseSensitive);
std::unique_ptr<module::ResolvedModule> staticModuleResolutionToResolvedModule(
    const StaticModuleResolution* staticResolution,
    const std::string& currentDirectory);
std::vector<std::string> moduleResolutionTraceToStrings(
    const std::vector<module::DiagAndArgs>& trace);
gostd::Error moduleResolutionError(project::Snapshot* snapshot);

// apiBuildSystem — session.go. Wrapper for the API session for build orchestrator.
// Go's apiBuildSystem satisfies tsoptions.ParseConfigHost through its embedded
// vfs.FS; execute::tsc::System already inherits tsoptions::ParseConfigHost, so
// the ResolutionHost methods delegate to fs().
struct apiBuildSystem : public execute::tsc::System {
	Session* session;
	std::string currentDirectory;
	vfs::TimePoint start;

	apiBuildSystem(Session* session, std::string currentDirectory,
	               vfs::TimePoint start)
	    : session(session), currentDirectory(std::move(currentDirectory)),
	      start(start) {}

	std::ostream* Writer() override;
	std::ostream* ErrorWriter() override;
	std::shared_ptr<vfs::FS> fs() override;
	std::string DefaultLibraryPath() override;
	std::string GetCurrentDirectory() override;
	bool WriteOutputIsTTY() override;
	int GetWidthOfTerminal() override;
	std::pair<std::string, bool> GetEnvironmentVariable(
	    std::string_view name) override;
	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error> Spawn(
	    const std::vector<std::string>& command, const std::string& dir,
	    gostd::io::Writer* stderr_) override;
	vfs::TimePoint Now() override;
	gostd::Duration SinceStart() override;
};

} // namespace tsc::api
