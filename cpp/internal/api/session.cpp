// session.cpp — tsc/internal/api/session.go (and its session-side helpers
// from module_resolution.go). API Session: request dispatch, snapshot
// lifecycle, symbol/type/signature registries, and all handler methods.
//
// Go defer → local `deferGuard` RAII objects (careful: Go deferred calls run
// LIFO at function exit; the guards below are only used where a single defer
// or exit-path ordering matches). Go `panic` in invariants → TSC_UNREACHABLE;
// `panic(recovered)` in handleBatchRequest → catch-all producing the panic
// error string.
//
// context.Context → gostd::Context. core.WithCheckerLifetime is a checker-pool
// lifetime slot: the C++ program exposes a single lazily-created checker
// (getChecker), so the Go ctx value has no equivalent — the CheckerLifetimeAPI
// call sites are noted in comments and ctx is passed through unchanged.

#include "internal/api/module_resolution.h"
#include "internal/api/session.h"

#include "internal/diagnostics/messages_generated.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <utility>

#include "internal/api/encoder/encoder.h"
#include "internal/api/requestfilesystem/requestfilesystem.h"
#include "internal/astnav/tokens.h"
#include "internal/format/format.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/nodebuilder/types.h"
#include "internal/packagejson/packagejson.h"
#include "internal/printer/printer.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/transpile/transpile.h"

namespace tsc::api {

namespace {

// base64Encode/base64Decode — Go base64.StdEncoding (session.go keeps source
// file payloads and PrintNode data base64'd on the wire).
inline std::string base64Encode(std::string_view data) {
	static constexpr char kTable[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	for (size_t i = 0; i < data.size(); i += 3) {
		uint32_t b = static_cast<uint8_t>(data[i]) << 16;
		bool two = i + 1 < data.size(), three = i + 2 < data.size();
		if (two) b |= static_cast<uint8_t>(data[i + 1]) << 8;
		if (three) b |= static_cast<uint8_t>(data[i + 2]);
		out += kTable[(b >> 18) & 63];
		out += kTable[(b >> 12) & 63];
		out += two ? kTable[(b >> 6) & 63] : '=';
		out += three ? kTable[b & 63] : '=';
	}
	return out;
}

inline bool base64Val(char c, uint8_t* v) {
	if (c >= 'A' && c <= 'Z') { *v = c - 'A'; return true; }
	if (c >= 'a' && c <= 'z') { *v = c - 'a' + 26; return true; }
	if (c >= '0' && c <= '9') { *v = c - '0' + 52; return true; }
	if (c == '+') { *v = 62; return true; }
	if (c == '/') { *v = 63; return true; }
	return false;
}

// base64Decode — base64.StdEncoding.DecodeString; {data, badByteIndex} with
// badByteIndex < 0 meaning success (Go's CorruptInputError index).
inline std::pair<std::string, int> base64Decode(std::string_view s) {
	if (s.size() % 4 != 0) {
		return {{}, int(s.size() - s.size() % 4)};
	}
	std::string out;
	out.reserve(s.size() / 4 * 3);
	for (size_t i = 0; i < s.size(); i += 4) {
		uint32_t b = 0;
		int pad = 0;
		for (int j = 0; j < 4; ++j) {
			char c = s[i + j];
			uint8_t v;
			if (c == '=') {
				if (j < 2) return {{}, int(i + j)};
				++pad;
				v = 0;
			} else if (pad || !base64Val(c, &v)) {
				return {{}, int(i + j)};
			}
			b = (b << 6) | v;
		}
		out += static_cast<char>((b >> 16) & 0xFF);
		if (pad < 2) out += static_cast<char>((b >> 8) & 0xFF);
		if (pad < 1) out += static_cast<char>(b & 0xFF);
	}
	return {out, -1};
}

// Overload for byte vectors (encoded node payloads).
inline std::string base64Encode(const std::vector<uint8_t>& data) {
	return base64Encode(std::string_view(
	    reinterpret_cast<const char*>(data.data()), data.size()));
}

// deferGuard — Go `defer f()` as RAII.
struct deferGuard {
	std::function<void()> f;
	~deferGuard() { if (f) f(); }
};

// marshalResult — `json.Marshal(result)` at the end of every handler result.
template <class T>
std::pair<ResultValue, gostd::Error> marshalResult(const T& result) {
	auto [data, err] = json::marshal(result);
	if (!err.empty()) return {ResultValue{}, gostd::newError(err)};
	return {ResultValue{false, std::move(data)}, nullptr};
}

// unmarshalParam — extracts the `*T` params object produced by
// unmarshalPayload (std::any holding shared_ptr<T>).
template <class T>
const T* unmarshalParam(std::any& parsed) {
	return std::any_cast<std::shared_ptr<T>>(parsed).get();
}

// snapshotHostParseConfigHost — adapts project::SnapshotHost to
// tsoptions::ParseConfigHost. Go's snapshotHost implements the tsconfig host
// iface; the C++ SnapshotHost exposes an FS() instead, so parse calls go
// through this adapter (fs methods delegate to the host FS; GetCurrentDirectory
// to the host).
struct snapshotHostParseConfigHost : tsoptions::ParseConfigHost {
	project::SnapshotHost* host;
	explicit snapshotHostParseConfigHost(project::SnapshotHost* h) : host(h) {}

	bool FileExists(std::string_view path) override {
		return host->FS()->FileExists(std::string(path));
	}
	bool DirectoryExists(std::string_view path) override {
		return host->FS()->DirectoryExists(std::string(path));
	}
	std::optional<std::string> ReadFile(std::string_view path) override {
		auto [data, ok] = host->FS()->ReadFile(std::string(path));
		if (!ok) return std::nullopt;
		return data;
	}
	std::string Realpath(std::string_view path) override {
		return host->FS()->Realpath(std::string(path));
	}
	bool UseCaseSensitiveFileNames() override {
		return host->FS()->UseCaseSensitiveFileNames();
	}
	std::string GetCurrentDirectory() override {
		return host->GetCurrentDirectory();
	}
	AccessibleEntries GetAccessibleEntries(std::string_view path) override {
		auto e = host->FS()->GetAccessibleEntries(std::string(path));
		return {std::move(e.files), std::move(e.directories),
		        std::move(e.symlinks)};
	}
};

} // namespace

std::atomic<uint64_t> sessionIDCounter{0};

// formatSessionID — session.go:4740.
std::string formatSessionID(uint64_t id) {
	return "api-session-" + std::to_string(id);
}

// snapshotHandle creates a snapshot handle from a snapshot's ID.
SnapshotID snapshotHandle(project::Snapshot* snapshot) {
	return SnapshotID(snapshot->ID());
}

// getProgram looks up a program from a project handle within this snapshot.
std::pair<compiler::SimpleProgram*, gostd::Error> snapshotData::getProgram(
    const project::ID& projectHandle) {
	auto [proj, err] = getProject(projectHandle);
	if (err) return {nullptr, err};

	compiler::SimpleProgram* program = proj->GetProgram();
	if (program == nullptr) {
		return {nullptr, gostd::errorf("%w: project has no program",
		                               {ErrClientError})};
	}

	return {program, nullptr};
}

// getProject looks up a project from a project handle within this snapshot.
std::pair<project::Project*, gostd::Error> snapshotData::getProject(
    const project::ID& projectHandle) {
	project::Project* proj =
	    snapshot->ProjectCollection->GetProject(projectHandle);
	if (proj == nullptr) {
		return {nullptr, gostd::errorf("%w: project %s not found",
		                               {ErrClientError, projectHandle.v})};
	}
	return {proj, nullptr};
}

// nodeHandleFrom creates an index-based node handle (index.kind.path), building a node index table
// for the file on-demand if needed.
NodeHandle snapshotData::nodeHandleFrom(Node* node) {
	SourceFile* sourceFile = getSourceFileOfNode(node);
	const std::string& path = sourceFile->Path();
	encoder::NodeIndexTable* table = encoder::GetNodeIndexTable(sourceFile);
	uint32_t idx = table->GetIndex(node);
	return NodeHandle(std::to_string(idx) + "." +
	                  std::to_string(static_cast<int>(node->kind)) + "." +
	                  path);
}

// getOrCreateProjectRegistry returns the registry for the given project, creating it if needed.
projectRegistryData* snapshotData::getOrCreateProjectRegistry(
    const project::ID& projectID) {
	if (projectID.v.empty()) {
		throw std::runtime_error(
		    "getOrCreateProjectRegistry: empty project ID");
	}
	// Fast path: registry already exists — read lock only.
	{
		std::shared_lock lk(projectRegistriesMu);
		auto it = projectRegistries.find(projectID);
		if (it != projectRegistries.end()) return it->second.get();
	}
	// Slow path: create under write lock.
	std::unique_lock lk(projectRegistriesMu);
	auto& slot = projectRegistries[projectID];
	if (slot == nullptr) {
		slot = std::make_unique<projectRegistryData>();
	}
	return slot.get();
}

// newSymbolResponse registers a symbol in the snapshot's registry and returns the response.
// canonicalProject is the project the symbol was observed in and must be non-empty; it is recorded
// as the symbol's canonical project (first writer wins) and returned to the client so it can default
// project-scoped follow-up lookups (members/exports, node resolution) to it.
std::unique_ptr<SymbolResponse> snapshotData::newSymbolResponse(
    Symbol* symbol, const project::ID& canonicalProject) {
	if (symbol == nullptr) {
		return nullptr;
	}

	auto [id, project] = registerSymbol(symbol, canonicalProject);
	auto resp = std::make_unique<SymbolResponse>();
	resp->Id = id;
	resp->Project = project;
	resp->Name = escapeSymbolName(symbol->name);
	resp->Flags = uint32_t(symbol->flags);
	resp->CheckFlags = uint32_t(symbol->checkFlags);

	if (!symbol->declarations.empty()) {
		resp->Declarations.resize(symbol->declarations.size());
		for (size_t i = 0; i < symbol->declarations.size(); ++i) {
			resp->Declarations[i] = nodeHandleFrom(symbol->declarations[i]);
		}
	}

	if (symbol->valueDeclaration != nullptr) {
		resp->ValueDeclaration = nodeHandleFrom(symbol->valueDeclaration);
	}

	if (symbol->parent != nullptr) {
		resp->Parent = SymbolHandle(symbol->parent);
	}

	if (symbol->exportSymbol != nullptr) {
		resp->ExportSymbol = SymbolHandle(symbol->exportSymbol);
	}

	return resp;
}

// registerSymbol registers a symbol in the snapshot's registry and returns its handle along with
// its canonical project. The canonical project is the project the symbol was first observed in
// (first writer wins for stability) and is always non-empty: every symbol handed to a client must
// carry a project so that project-scoped follow-up lookups (members/exports, parent, node
// resolution) have a default context. Callers must supply a non-empty project.
std::pair<SymbolID, project::ID> snapshotData::registerSymbol(
    Symbol* symbol, const project::ID& canonicalProject) {
	if (symbol == nullptr) {
		return {0, project::ID("")};
	}
	if (canonicalProject.v.empty()) {
		throw std::runtime_error(
		    "registerSymbol requires a non-empty canonical project");
	}
	SymbolID id = SymbolHandle(symbol);
	std::unique_lock lk(symbolRegistryMu);
	auto it = symbolRegistry.find(id);
	if (it != symbolRegistry.end()) {
		if (it->second != symbol) {
			throw std::runtime_error("duplicate symbol");
		}
	} else {
		symbolRegistry[id] = symbol;
	}
	auto p = symbolCanonicalProjects.find(id);
	project::ID project;
	if (p == symbolCanonicalProjects.end()) {
		symbolCanonicalProjects[id] = canonicalProject;
		project = canonicalProject;
	} else {
		project = p->second;
	}
	return {id, project};
}

// newTypeResponse registers a type in the project's registry and returns the response.
std::unique_ptr<TypeResponse> snapshotData::newTypeResponse(
    const project::ID& projectID, checker::Type* t, checker::Checker* c) {
	if (t == nullptr) {
		return nullptr;
	}
	// newTypeResponse (proto.go) reads raw fields for the common cases.
	auto shared = api::newTypeResponse(t, registerType(projectID, t));
	auto resp = std::make_unique<TypeResponse>(*shared);
	if (t->objectFlags & checker::ObjectFlagsMapped) {
		// Go: mapped.ResolveComponents(c, t) — the four getters resolve and
		// populate the MappedType fields; register their results.
		resp->TypeParameter =
		    registerType(projectID, c->getTypeParameterFromMappedType(t));
		resp->ConstraintType =
		    registerType(projectID, c->getConstraintTypeFromMappedType(t));
		resp->NameType =
		    registerType(projectID, c->getNameTypeFromMappedType(t));
		resp->TemplateType =
		    registerType(projectID, c->getTemplateTypeFromMappedType(t));
	}
	if (checker::IsTupleTypeTarget(t)) {
		auto& elementInfos = t->AsTupleType()->elementInfos;
		for (size_t i = 0; i < elementInfos.size(); ++i) {
			if (Node* declaration = elementInfos[i].labeledDeclaration) {
				if (resp->LabeledElementDeclarations.empty()) {
					resp->LabeledElementDeclarations.resize(
					    elementInfos.size());
				}
				resp->LabeledElementDeclarations[i] =
				    nodeHandleFrom(declaration);
			}
		}
	}
	return resp;
}

TypeID snapshotData::registerType(const project::ID& projectID,
                                  checker::Type* t) {
	if (t == nullptr) {
		return 0;
	}
	TypeID id = TypeHandle(t);
	projectRegistryData* reg = getOrCreateProjectRegistry(projectID);
	std::unique_lock lk(reg->typeRegistryMu);
	auto it = reg->typeRegistry.find(id);

	if (it != reg->typeRegistry.end()) {
		if (it->second != t) {
			throw std::runtime_error("duplicate type");
		}
		return id;
	}
	reg->typeRegistry[id] = t;
	return id;
}

// resolveSymbolHandle resolves a symbol handle within the snapshot's registry.
std::pair<Symbol*, gostd::Error> snapshotData::resolveSymbolHandle(
    SymbolID handle) {
	if (handle == 0) {
		return {nullptr, gostd::errorf("%w: empty symbol handle",
		                               {ErrClientError})};
	}

	std::shared_lock lk(symbolRegistryMu);
	auto it = symbolRegistry.find(handle);

	if (it == symbolRegistry.end()) {
		return {nullptr,
		        gostd::errorf("%w: symbol handle %d not found in snapshot registry",
		                      {ErrClientError, handle})};
	}

	return {it->second, nullptr};
}

// resolveTypeHandle resolves a type handle within the project's registry.
std::pair<checker::Type*, gostd::Error> snapshotData::resolveTypeHandle(
    const project::ID& projectID, TypeID handle) {
	if (handle == 0) {
		return {nullptr, gostd::errorf("%w: empty type handle",
		                               {ErrClientError})};
	}
	if (projectID.v.empty()) {
		return {nullptr,
		        gostd::errorf("%w: empty project ID for type handle %d",
		                      {ErrClientError, uint64_t(handle)})};
	}

	projectRegistryData* reg;
	{
		std::shared_lock lk(projectRegistriesMu);
		auto it = projectRegistries.find(projectID);
		reg = it == projectRegistries.end() ? nullptr : it->second.get();
	}

	if (reg == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: type handle %d not found (no registry for project %s)",
		                      {ErrClientError, uint64_t(handle), projectID.v})};
	}

	std::shared_lock lk(reg->typeRegistryMu);
	auto it = reg->typeRegistry.find(handle);

	if (it == reg->typeRegistry.end()) {
		return {nullptr,
		        gostd::errorf("%w: type handle %d not found in project registry",
		                      {ErrClientError, uint64_t(handle)})};
	}

	return {it->second, nullptr};
}

// resolveSignatureHandle resolves a signature handle within the project's registry.
std::pair<checker::Signature*, gostd::Error>
snapshotData::resolveSignatureHandle(const project::ID& projectID,
                                     SignatureID handle) {
	if (handle == 0) {
		return {nullptr, gostd::errorf("%w: empty signature handle",
		                               {ErrClientError})};
	}
	if (projectID.v.empty()) {
		return {nullptr,
		        gostd::errorf("%w: empty project ID for signature handle %d",
		                      {ErrClientError, handle})};
	}

	projectRegistryData* reg;
	{
		std::shared_lock lk(projectRegistriesMu);
		auto it = projectRegistries.find(projectID);
		reg = it == projectRegistries.end() ? nullptr : it->second.get();
	}

	if (reg == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: signature handle %d not found (no registry for project %s)",
		                      {ErrClientError, handle, projectID.v})};
	}

	std::shared_lock lk(reg->signatureRegistryMu);
	auto it = reg->signatureRegistry.find(handle);

	if (it == reg->signatureRegistry.end()) {
		return {nullptr,
		        gostd::errorf("%w: signature handle %d not found in project registry",
		                      {ErrClientError, handle})};
	}

	return {it->second, nullptr};
}

// newSignatureResponse registers a signature in the project's registry and returns the response.
std::unique_ptr<SignatureResponse> snapshotData::newSignatureResponse(
    const project::ID& projectID, checker::Signature* sig) {
	if (sig == nullptr) {
		return nullptr;
	}
	auto resp = std::make_unique<SignatureResponse>();
	resp->Id = registerSignature(projectID, sig);
	resp->Flags = uint32_t(sig->flags);

	if (sig->declaration != nullptr) {
		resp->Declaration = nodeHandleFrom(sig->declaration);
	}

	if (!sig->typeParameters.empty()) {
		resp->TypeParameters = typeHandles(sig->typeParameters);
	}

	if (!sig->parameters.empty()) {
		resp->Parameters = symbolHandles(sig->parameters);
	}

	if (sig->thisParameter != nullptr) {
		resp->ThisParameter = SymbolHandle(sig->thisParameter);
	}

	if (sig->target != nullptr) {
		resp->Target = SignatureHandle(sig->target);
	}

	return resp;
}

SignatureID snapshotData::registerSignature(const project::ID& projectID,
                                            checker::Signature* sig) {
	if (sig == nullptr) {
		return 0;
	}
	SignatureID id = SignatureHandle(sig);
	projectRegistryData* reg = getOrCreateProjectRegistry(projectID);
	std::unique_lock lk(reg->signatureRegistryMu);
	auto it = reg->signatureRegistry.find(id);

	if (it != reg->signatureRegistry.end()) {
		if (it->second != sig) {
			throw std::runtime_error("duplicate signature");
		}
		return id;
	}
	reg->signatureRegistry[id] = sig;
	return id;
}

// resolveNodeHandle — session.go:4523.
std::pair<Node*, gostd::Error> snapshotData::resolveNodeHandle(
    compiler::SimpleProgram* program, const NodeHandle& handle) {
	const std::string& s = handle;
	// Format: "index.kind.path" — we need index and path, kind is informational only.
	size_t firstDot = s.find('.');
	if (firstDot == std::string::npos) {
		return {nullptr, gostd::errorf("%w: invalid node handle %q",
		                               {ErrClientError, handle})};
	}
	size_t secondDot = s.find('.', firstDot + 1);
	if (secondDot == std::string::npos) {
		return {nullptr, gostd::errorf("%w: invalid node handle %q",
		                               {ErrClientError, handle})};
	}

	uint32_t idx;
	const char* begin = s.data();
	const char* end = begin + firstDot;
	auto conv = std::from_chars(begin, end, idx);
	if (conv.ec != std::errc() || conv.ptr != end) {
		return {nullptr,
		        gostd::errorf("%w: invalid node handle %q: %w",
		                      {ErrClientError, handle,
		                       gostd::newError("strconv.ParseUint: invalid syntax")})};
	}
	tspath::Path path(s.substr(secondDot + 1));

	SourceFile* sourceFile = program->GetSourceFileByPath(path);
	if (sourceFile == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: node handle %q could not be resolved (file may not be loaded or handle may be stale)",
		                      {ErrClientError, handle})};
	}
	encoder::NodeIndexTable* table = encoder::GetNodeIndexTable(sourceFile);

	if (table != nullptr && idx < table->Nodes.size()) {
		Node* node = table->Nodes[idx];
		if (node != nullptr) {
			return {node, nullptr};
		}
	}
	return {nullptr,
	        gostd::errorf("%w: node handle %q could not be resolved (file may not be loaded or handle may be stale)",
	                      {ErrClientError, handle})};
}

// computeSnapshotChanges computes the per-project source file differences between
// two snapshots. It uses DiffOrderedMaps on projects to find changed/removed projects,
// then DiffMaps on FilesByPath for each changed project to collect file-level changes.
std::unique_ptr<SnapshotChanges> computeSnapshotChanges(project::Snapshot* prev,
                                                      project::Snapshot* next) {
	auto* prevProjects = prev->ProjectCollection->ProjectsByID();
	auto* nextProjects = next->ProjectCollection->ProjectsByID();

	SnapshotChanges changes;

	collections::diffOrderedMaps<project::ID, project::Project*>(
	    prevProjects, nextProjects,
	    // onAdded: new project — nothing to retain from previous snapshot.
	    std::function<void(project::ID, project::Project*)>{
	        [](project::ID, project::Project*) {}},
	    // onRemoved: project removed entirely.
	    std::function<void(project::ID, project::Project*)>{
	        [&changes](project::ID, project::Project* oldProj) {
	            changes.RemovedProjects.push_back(oldProj->ID());
	        }},
	    // onModified: project changed, diff its files.
	    std::function<void(project::ID, project::Project*, project::Project*)>{
	        [&changes](project::ID, project::Project* oldProj,
	                   project::Project* newProj) {
	            if (oldProj->GetProgram() == newProj->GetProgram()) {
	                return;
	            }
	            const std::unordered_map<tspath::Path, SourceFile*>* oldFiles =
	                nullptr;
	            const std::unordered_map<tspath::Path, SourceFile*>* newFiles =
	                nullptr;
	            if (auto* p = oldProj->GetProgram(); p != nullptr) {
	                oldFiles = &p->FilesByPath();
	            }
	            if (auto* p = newProj->GetProgram(); p != nullptr) {
	                newFiles = &p->FilesByPath();
	            }
	            ProjectFileChanges projectChanges;
	            tsc::DiffMaps(
	                oldFiles, newFiles,
	                std::function<void(tspath::Path, SourceFile*)>{}, // onAdded: new file in project, not a change.
	                std::function<void(tspath::Path, SourceFile*)>{
	                    [&projectChanges](tspath::Path path, SourceFile*) {
	                        projectChanges.DeletedFiles.push_back(path);
	                    }},
	                std::function<void(tspath::Path, SourceFile*, SourceFile*)>{
	                    [&projectChanges](tspath::Path path, SourceFile*,
	                                      SourceFile*) {
	                        projectChanges.ChangedFiles.push_back(path);
	                    }});
	            if (!projectChanges.ChangedFiles.empty() ||
	                !projectChanges.DeletedFiles.empty()) {
	                changes.ChangedProjects[newProj->ID()] =
	                    std::make_shared<ProjectFileChanges>(
	                        std::move(projectChanges));
	            }
	        }});
	return std::make_unique<SnapshotChanges>(std::move(changes));
}

// checkerSetup methods — session.go.

std::unique_ptr<SymbolResponse> checkerSetup::newSymbolResponse(Symbol* sym) {
	return sd->newSymbolResponse(sym, projectID);
}

std::unique_ptr<SignatureResponse> checkerSetup::newSignatureResponse(
    checker::Signature* sig) {
	return sd->newSignatureResponse(projectID, sig);
}

std::unique_ptr<IndexInfoResponse> checkerSetup::newIndexInfoResponse(
    checker::IndexInfo* info) {
	if (info == nullptr) {
		return nullptr;
	}
	auto result = std::make_unique<IndexInfoResponse>();
	result->KeyType =
	    *sd->newTypeResponse(projectID, info->keyType, checker);
	result->ValueType =
	    *sd->newTypeResponse(projectID, info->valueType, checker);
	result->IsReadonly = info->isReadonly;
	if (info->declaration != nullptr) {
		result->Declaration = sd->nodeHandleFrom(info->declaration);
	}
	return result;
}

std::pair<checker::Type*, gostd::Error> checkerSetup::resolveTypeHandle(
    TypeID id) {
	return sd->resolveTypeHandle(projectID, id);
}

std::pair<Symbol*, gostd::Error> checkerSetup::resolveSymbolHandle(SymbolID id) {
	return sd->resolveSymbolHandle(id);
}

std::pair<checker::Signature*, gostd::Error>
checkerSetup::resolveSignatureHandle(SignatureID id) {
	return sd->resolveSignatureHandle(projectID, id);
}

// resolveLocation resolves an optional location, given either as a node handle or as a
// file and position. Returns nil when neither is provided.
std::pair<Node*, gostd::Error> checkerSetup::resolveLocation(
    const NodeHandle& handle, const DocumentIdentifier* file,
    const uint32_t* position) {
	if (!handle.empty()) {
		return sd->resolveNodeHandle(program, handle);
	}
	if (file != nullptr && position != nullptr) {
		SourceFile* sourceFile = program->GetSourceFile(file->ToFileName());
		if (sourceFile == nullptr) {
			return {nullptr,
			        gostd::errorf("%w: source file not found: %v",
			                      {ErrClientError, file->String()})};
		}
		return {astnav::getTouchingPropertyName(
		            sourceFile,
		            sourceFile->GetPositionMap()->UTF16ToUTF8(int(*position))),
		        nullptr};
	}
	return {nullptr, nullptr};
}

// Session — session.go:378.

// newSession — session.go:491. Defined below the two exported ctors.
static std::shared_ptr<Session> newSession(
    project::SnapshotHost* snapshotHost,
    std::function<gostd::Context(gostd::Context)> withLocale,
    const SessionOptions* options);

// NewLSPSession creates a new API session with the given project session.
std::shared_ptr<Session> NewLSPSession(project::Session* projectSession,
                                       const SessionOptions* options) {
	auto s = newSession(
	    projectSession->SnapshotHost_,
	    [projectSession](gostd::Context ctx) {
	        return projectSession->WithCurrentLocale(ctx);
	    },
	    options);
	s->projectSession = projectSession;
	return s;
}

// NewStandaloneSession creates an API session with an independently owned snapshot host.
std::shared_ptr<Session> NewStandaloneSession(project::SessionInit* init,
                                              const SessionOptions* options) {
	project::SnapshotHost* snapshotHost = project::NewSnapshotHost(init);
	auto s = newSession(snapshotHost, nullptr, options);
	s->ownsSnapshotHost = true;
	return s;
}

// newSession — session.go:491.
static std::shared_ptr<Session> newSession(
    project::SnapshotHost* snapshotHost,
    std::function<gostd::Context(gostd::Context)> withLocale,
    const SessionOptions* options) {
	uint64_t id = ++sessionIDCounter;
	if (withLocale == nullptr) {
		withLocale = [](gostd::Context ctx) { return ctx; };
	}
	auto s = std::make_shared<Session>();
	s->id = formatSessionID(id);
	s->snapshotHost = snapshotHost;
	s->withLocale = std::move(withLocale);
	if (options != nullptr) {
		s->useBinaryResponses = options->UseBinaryResponses;
	}
	return s;
}

// ID returns the unique identifier for this session.
std::string Session::ID() { return id; }

void Session::SetConnection(std::shared_ptr<ipc::Conn> conn) {
	this->conn = std::move(conn);
}

std::string Session::GetCurrentDirectory() {
	return snapshotHost->GetCurrentDirectory();
}

std::shared_ptr<vfs::FS> Session::FS() {
	if (projectSession != nullptr) {
		return projectSession->FS();
	}
	return snapshotHost->FS();
}

std::string Session::DefaultLibraryPath() {
	if (projectSession != nullptr) {
		return projectSession->DefaultLibraryPath();
	}
	return snapshotHost->DefaultLibraryPath();
}

bool Session::useCaseSensitiveFileNames() {
	return snapshotHost->FS()->UseCaseSensitiveFileNames();
}

// getSnapshotData looks up snapshot data by handle.
std::pair<snapshotData*, gostd::Error> Session::getSnapshotData(
    SnapshotID handle) {
	std::shared_lock lk(snapshotsMu);
	auto it = snapshots.find(handle);
	if (it == snapshots.end()) {
		return {nullptr, gostd::errorf("%w: snapshot %d not found",
		                               {ErrClientError, handle})};
	}
	return {it->second.get(), nullptr};
}

// retainSnapshotData pins snapshot data while an operation builds a derived snapshot.
std::pair<snapshotData*, gostd::Error> Session::retainSnapshotData(
    SnapshotID handle) {
	std::unique_lock lk(snapshotsMu);
	auto it = snapshots.find(handle);
	if (it == snapshots.end()) {
		return {nullptr, gostd::errorf("%w: snapshot %d not found",
		                               {ErrClientError, handle})};
	}
	it->second->refCount++;
	return {it->second.get(), nullptr};
}

gostd::Error Session::releaseSnapshot(SnapshotID handle) {
	snapshotData* sd = nullptr;
	{
		std::unique_lock lk(snapshotsMu);
		auto it = snapshots.find(handle);
		if (it == snapshots.end()) {
			return gostd::errorf("%w: snapshot %d not found",
			                     {ErrClientError, handle});
		}
		sd = it->second.get();
		sd->refCount--;
		if (sd->refCount <= 0) {
			snapshots.erase(it);
		} else {
			sd = nullptr;
		}
	}
	if (sd != nullptr) {
		sd->snapshot->Deref();
	}
	return nullptr;
}

// setupChecker resolves snapshot, program, and type checker for a project.
// Callers must defer setup.done() to release the checker.
std::pair<checkerSetup, gostd::Error> Session::setupChecker(
    gostd::Context ctx, SnapshotID snapshot,
    const project::ID& projectHandle) {
	auto [sd, err] = getSnapshotData(snapshot);
	if (err) {
		return {checkerSetup{}, err};
	}

	auto [program, err2] = sd->getProgram(projectHandle);
	if (err2) {
		return {checkerSetup{}, err2};
	}

	// Go: program.GetTypeChecker(core.WithCheckerLifetime(ctx,
	// core.CheckerLifetimeAPI)) — the C++ program holds a single lazily-created
	// checker; done() is a no-op.
	checker::Checker* c = program->getChecker();
	return {checkerSetup{sd, program, c, []() {}, projectHandle}, nullptr};
}

// snapshotLSHost — Go's project.Snapshot satisfies ls.Host structurally; C++
// needs an explicit adapter. Owned by the returned LanguageService (raw,
// leak-tolerant like the rest of the dep-stubbed graph).
struct snapshotLSHost : ls::Host {
	project::Snapshot* snapshot = nullptr;
	explicit snapshotLSHost(project::Snapshot* s) : snapshot(s) {}

	bool UseCaseSensitiveFileNames() override {
		return snapshot->UseCaseSensitiveFileNames();
	}
	std::pair<std::string, bool> ReadFile(
	    const std::string& fileName) override {
		return snapshot->ReadFile(fileName);
	}
	lsconv::Converters* Converters() override {
		return snapshot->Converters();
	}
	lsutil::UserPreferences GetPreferences(
	    const std::string& activeFile) override {
		return snapshot->GetPreferences(activeFile);
	}
	sourcemap::ECMALineInfo* GetECMALineInfo(
	    const std::string& fileName) override {
		return snapshot->GetECMALineInfo(fileName);
	}
	autoimport::Registry* AutoImportRegistry() override {
		return snapshot->AutoImportRegistry();
	}
	std::vector<std::string> ReadDirectory(
	    const std::string& currentDir, const std::string& path,
	    const std::vector<std::string>& extensions,
	    const std::vector<std::string>& excludes,
	    const std::vector<std::string>& includes, int depth) override {
		return snapshot->ReadDirectory(currentDir, path, extensions, excludes,
		                               includes, depth);
	}
	std::vector<std::string> GetDirectories(
	    const std::string& path) override {
		return snapshot->GetDirectories(path);
	}
	bool DirectoryExists(const std::string& path) override {
		return snapshot->DirectoryExists(path);
	}
	bool FileExists(const std::string& path) override {
		return snapshot->FileExists(path);
	}
};

// setupLanguageService creates a LanguageService for the given snapshot/project.
// Unlike setupChecker, this does NOT acquire a checker from the pool, so callers that
// only need an LS (and not a Checker) can avoid blocking on / holding a pooled checker.
//
// The LS acquires its own checker internally (keyed by the ctx's checker lifetime).
// If a handler returns symbol/type/signature handles the client may later re-query
// on the API checker (e.g. completion with IncludeSymbol -> GetTypeOfSymbol), wrap
// ctx with core.WithCheckerLifetime(ctx, core.CheckerLifetimeAPI) so those handles
// are produced on the persistent API checker and stay resolvable. Only safe when the
// LS operation acquires a checker exactly once; nested acquisitions (e.g. find-all-
// references) would deadlock on the single-slot persistent checker.
std::pair<ls::LanguageService*, gostd::Error> Session::setupLanguageService(
    project::Snapshot* snapshot, compiler::SimpleProgram* program,
    const project::ID& projectHandle, const std::string& activeFile) {
	project::Project* proj =
	    snapshot->ProjectCollection->GetProject(projectHandle);
	if (proj == nullptr) {
		return {nullptr, gostd::errorf("%w: project %s not found",
		                               {ErrClientError, projectHandle.v})};
	}
	return {ls::NewLanguageService(proj->ID().str(), program,
	                               new snapshotLSHost{snapshot}, activeFile),
	        nullptr};
}

// HandleRequest implements Handler.
std::pair<json::Value, gostd::Error> Session::HandleRequest(
    gostd::Context ctx, std::string_view method, const json::Value& params) {
	if (withLocale != nullptr) {
		ctx = withLocale(ctx);
	}
	// Handle simple methods that don't need param parsing
	if (method == "echo") {
		// Return raw binary for msgpack protocol compatibility
		if (useBinaryResponses) {
			return {json::Value(params), nullptr};
		}
		return {params, nullptr};
	}
	if (method == "ping") {
		return {json::Value("\"pong\""), nullptr};
	}

	auto [rv, err] = handleRequest(ctx, std::string(method), params);
	if (err) {
		return {json::Value(), err};
	}
	// RawBinary bytes ride back inside the Value; the msgpack protocol caller
	// writes them uninterpreted (WriteResponseRaw). JSON paths only produce
	// isRaw=false.
	if (rv.isRaw) {
		return {json::Value(std::move(rv.data)), nullptr};
	}
	return {json::Value(rv.data.empty() ? "null" : std::move(rv.data)),
	        nullptr};
}

// HandleNotification implements Handler.
gostd::Error Session::HandleNotification(gostd::Context ctx,
                                         std::string_view method,
                                         const json::Value& params) {
	// TODO: Implement notification handling
	return nullptr;
}

// handleRequest — session.go:718 switch over Method. `parsed` holds
// shared_ptr<T> produced by unmarshalPayload (Go `parsed.(*T)`).
std::pair<ResultValue, gostd::Error> Session::handleRequest(
    gostd::Context ctx, const std::string& method, const json::Value& params) {
	auto parsedResult = unmarshalPayload(method, params);
	auto& parsed = parsedResult.first;
	auto& uerr = parsedResult.second;
	if (!uerr.empty()) {
		return {ResultValue{},
		        gostd::errorf("%w: %w",
		                      {ErrInvalidRequest, gostd::newError(uerr)})};
	}

	// call wraps a typed handler: run, then marshal the result (Go's
	// `return s.handleX(...)` — conn marshals the `any` result).
	auto call = [&](auto&& fn) -> std::pair<ResultValue, gostd::Error> {
		auto [resp, err] = fn();
		if (err) return {ResultValue{}, err};
		return marshalResult(resp);
	};

	if (method == MethodBatchRequests) {
		return call([&] { return handleBatchRequests(ctx, unmarshalParam<BatchRequestsParams>(parsed)); });
	}
	if (method == MethodRelease) {
		return handleRelease(ctx, unmarshalParam<ReleaseParams>(parsed));
	}
	if (method == MethodReleaseSourceFile) {
		return handleReleaseSourceFile(unmarshalParam<ReleaseSourceFileParams>(parsed));
	}
	if (method == MethodInitialize) {
		return call([&] { return handleInitialize(ctx); });
	}
	if (method == MethodCreateSnapshot) {
		return call([&] { return handleCreateSnapshot(ctx, unmarshalParam<CreateSnapshotParams>(parsed)); });
	}
	if (method == MethodUpdateSnapshot) {
		return call([&] { return handleUpdateSnapshot(ctx, unmarshalParam<UpdateSnapshotParams>(parsed)); });
	}
	if (method == MethodGetCurrentLanguageServerSnapshot) {
		return call([&] { return handleGetCurrentLanguageServerSnapshot(ctx, unmarshalParam<GetCurrentLanguageServerSnapshotParams>(parsed)); });
	}
	if (method == MethodCreateModuleResolver) {
		return call([&] { return handleCreateModuleResolver(unmarshalParam<CreateModuleResolverParams>(parsed)); });
	}
	if (method == MethodReleaseModuleResolver) {
		return handleReleaseModuleResolver(unmarshalParam<ReleaseModuleResolverParams>(parsed));
	}
	if (method == MethodResolveModuleName) {
		return call([&] { return handleResolveModuleName(ctx, unmarshalParam<ResolveModuleNameParams>(parsed)); });
	}
	if (method == MethodParseCommandLine) {
		return call([&] { return handleParseCommandLine(ctx, unmarshalParam<ParseCommandLineParams>(parsed)); });
	}
	if (method == MethodReadConfigFile) {
		return call([&] { return handleReadConfigFile(ctx, unmarshalParam<ReadConfigFileParams>(parsed)); });
	}
	if (method == MethodParseJsonConfigFile) {
		return call([&] { return handleParseJsonConfigFileContent(ctx, unmarshalParam<ParseJsonConfigFileContentParams>(parsed)); });
	}
	if (method == MethodParseConfigFile) {
		return call([&] { return handleParseConfigFile(ctx, unmarshalParam<ParseConfigFileParams>(parsed)); });
	}
	if (method == MethodCreateBuildOrchestrator) {
		return call([&] { return handleCreateBuildOrchestrator(ctx, unmarshalParam<CreateBuildOrchestratorParams>(parsed)); });
	}
	if (method == MethodDisposeBuildOrchestrator) {
		return handleDisposeBuildOrchestrator(ctx, unmarshalParam<DisposeBuildOrchestratorParams>(parsed));
	}
	if (method == MethodBuild) {
		return call([&] { return handleBuild(ctx, unmarshalParam<BuildParams>(parsed)); });
	}
	if (method == MethodBuildReferences) {
		return call([&] { return handleBuildReferences(ctx, unmarshalParam<BuildParams>(parsed)); });
	}
	if (method == MethodCleanBuild) {
		return call([&] { return handleCleanBuild(ctx, unmarshalParam<CleanBuildParams>(parsed)); });
	}
	if (method == MethodCleanReferences) {
		return call([&] { return handleCleanReferences(ctx, unmarshalParam<CleanBuildParams>(parsed)); });
	}
	if (method == MethodCreateSourceFile) {
		return handleCreateSourceFile(ctx, unmarshalParam<CreateSourceFileParams>(parsed));
	}
	if (method == MethodCreateSourceFileFromFile) {
		return handleCreateSourceFileFromFile(ctx, unmarshalParam<CreateSourceFileFromFileParams>(parsed));
	}
	if (method == MethodTranspileModule) {
		return call([&] { return handleTranspile(ctx, unmarshalParam<TranspileParams>(parsed), false); });
	}
	if (method == MethodTranspileModuleFromFile) {
		return call([&] { return handleTranspileFromFile(ctx, unmarshalParam<TranspileFromFileParams>(parsed), false); });
	}
	if (method == MethodTranspileDeclaration) {
		return call([&] { return handleTranspile(ctx, unmarshalParam<TranspileParams>(parsed), true); });
	}
	if (method == MethodTranspileDeclarationFromFile) {
		return call([&] { return handleTranspileFromFile(ctx, unmarshalParam<TranspileFromFileParams>(parsed), true); });
	}
	if (method == MethodGetDefaultProjectForFile) {
		return call([&] { return handleGetDefaultProjectForFile(ctx, unmarshalParam<GetDefaultProjectForFileParams>(parsed)); });
	}
	if (method == MethodGetSourceFile) {
		return handleGetSourceFile(ctx, unmarshalParam<GetSourceFileParams>(parsed));
	}
	if (method == MethodGetSourceFileNames) {
		return call([&] { return handleGetSourceFileNames(ctx, unmarshalParam<GetSourceFileNamesParams>(parsed)); });
	}
	if (method == MethodGetSourceFileMetadata) {
		return call([&] { return handleGetSourceFileMetadata(ctx, unmarshalParam<GetSourceFileParams>(parsed)); });
	}
	if (method == MethodGetModeForUsageLocation) {
		return call([&] { return handleGetModeForUsageLocation(ctx, unmarshalParam<GetModeForUsageLocationParams>(parsed)); });
	}
	if (method == MethodGetModeForResolutionAtIndex) {
		return call([&] { return handleGetModeForResolutionAtIndex(ctx, unmarshalParam<GetModeForResolutionAtIndexParams>(parsed)); });
	}
	if (method == MethodGetResolvedModule) {
		return call([&] { return handleGetResolvedModule(ctx, unmarshalParam<GetResolvedModuleParams>(parsed)); });
	}
	if (method == MethodGetResolvedModuleFromModuleSpecifier) {
		return call([&] { return handleGetResolvedModuleFromModuleSpecifier(ctx, unmarshalParam<GetResolvedModuleFromModuleSpecifierParams>(parsed)); });
	}
	if (method == MethodGetResolvedTypeReferenceDirective) {
		return call([&] { return handleGetResolvedTypeReferenceDirective(ctx, unmarshalParam<GetResolvedTypeReferenceDirectiveParams>(parsed)); });
	}
	if (method == MethodGetResolvedTypeReferenceDirectiveFromReference) {
		return call([&] { return handleGetResolvedTypeReferenceDirectiveFromReference(ctx, unmarshalParam<GetResolvedTypeReferenceDirectiveFromReferenceParams>(parsed)); });
	}
	if (method == MethodGetConfigFileNames) {
		return call([&] { return handleGetConfigFileNames(ctx, unmarshalParam<GetProjectDiagnosticsParams>(parsed)); });
	}
	if (method == MethodGetConfigSourceFile) {
		return handleGetConfigSourceFile(ctx, unmarshalParam<GetSourceFileParams>(parsed));
	}
	if (method == MethodGetSymbolAtPosition) {
		return call([&] { return handleGetSymbolAtPosition(ctx, unmarshalParam<GetSymbolAtPositionParams>(parsed)); });
	}
	if (method == MethodGetSymbolsAtPositions) {
		return call([&] { return handleGetSymbolsAtPositions(ctx, unmarshalParam<GetSymbolsAtPositionsParams>(parsed)); });
	}
	if (method == MethodGetSymbolAtLocation) {
		return call([&] { return handleGetSymbolAtLocation(ctx, unmarshalParam<GetSymbolAtLocationParams>(parsed)); });
	}
	if (method == MethodGetSymbolsAtLocations) {
		return call([&] { return handleGetSymbolsAtLocations(ctx, unmarshalParam<GetSymbolsAtLocationsParams>(parsed)); });
	}
	if (method == MethodGetSymbolOfSourceFile) {
		return call([&] { return handleGetSymbolOfSourceFile(ctx, unmarshalParam<GetSymbolOfSourceFileParams>(parsed)); });
	}
	if (method == MethodGetSymbolsOfSourceFiles) {
		return call([&] { return handleGetSymbolsOfSourceFiles(ctx, unmarshalParam<GetSymbolsOfSourceFilesParams>(parsed)); });
	}
	if (method == MethodGetTypeOfSymbol) {
		return call([&] { return handleGetTypeOfSymbol(ctx, unmarshalParam<GetTypeOfSymbolParams>(parsed)); });
	}
	if (method == MethodGetTypesOfSymbols) {
		return call([&] { return handleGetTypesOfSymbols(ctx, unmarshalParam<GetTypesOfSymbolsParams>(parsed)); });
	}
	if (method == MethodGetDeclaredTypeOfSymbol) {
		return call([&] { return handleGetDeclaredTypeOfSymbol(ctx, unmarshalParam<GetTypeOfSymbolParams>(parsed)); });
	}
	if (method == MethodGetNonMissingTypeOfSymbol) {
		return call([&] { return handleGetNonMissingTypeOfSymbol(ctx, unmarshalParam<GetTypeOfSymbolParams>(parsed)); });
	}
	if (method == MethodResolveName) {
		return call([&] { return handleResolveName(ctx, unmarshalParam<ResolveNameParams>(parsed)); });
	}
	if (method == MethodGetSymbolsInScope) {
		return call([&] { return handleGetSymbolsInScope(ctx, unmarshalParam<GetSymbolsInScopeParams>(parsed)); });
	}
	if (method == MethodGetSignaturesOfType) {
		return call([&] { return handleGetSignaturesOfType(ctx, unmarshalParam<GetSignaturesOfTypeParams>(parsed)); });
	}
	if (method == MethodGetResolvedSignature) {
		return call([&] { return handleGetResolvedSignature(ctx, unmarshalParam<GetResolvedSignatureParams>(parsed)); });
	}
	if (method == MethodGetTypeAtLocation) {
		return call([&] { return handleGetTypeAtLocation(ctx, unmarshalParam<GetTypeAtLocationParams>(parsed)); });
	}
	if (method == MethodGetTypeAtLocations) {
		return call([&] { return handleGetTypeAtLocations(ctx, unmarshalParam<GetTypeAtLocationsParams>(parsed)); });
	}
	if (method == MethodGetTypeAtPosition) {
		return call([&] { return handleGetTypeAtPosition(ctx, unmarshalParam<GetTypeAtPositionParams>(parsed)); });
	}
	if (method == MethodGetTypesAtPositions) {
		return call([&] { return handleGetTypesAtPositions(ctx, unmarshalParam<GetTypesAtPositionsParams>(parsed)); });
	}
	if (method == MethodGetParentOfSymbol) {
		return call([&] { return handleGetParentOfSymbol(ctx, unmarshalParam<GetSymbolPropertyParams>(parsed)); });
	}
	if (method == MethodGetMembersOfSymbol) {
		return call([&] { return handleGetMembersOfSymbol(ctx, unmarshalParam<GetSymbolPropertyParams>(parsed)); });
	}
	if (method == MethodGetExportsOfSymbol) {
		return call([&] { return handleGetExportsOfSymbol(ctx, unmarshalParam<GetSymbolPropertyParams>(parsed)); });
	}
	if (method == MethodGetExportSymbolOfSymbol) {
		return call([&] { return handleGetExportSymbolOfSymbol(ctx, unmarshalParam<GetSymbolPropertyParams>(parsed)); });
	}
	if (method == MethodGetSymbolOfType) {
		return call([&] { return handleGetSymbolOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetTargetOfType) {
		return call([&] { return handleGetTargetOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetFreshTypeOfType) {
		return call([&] { return handleGetFreshTypeOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetRegularTypeOfType) {
		return call([&] { return handleGetRegularTypeOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetTypesOfType) {
		return call([&] { return handleGetTypesOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetTypeParametersOfType) {
		return call([&] { return handleGetTypeParametersOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetOuterTypeParametersOfType) {
		return call([&] { return handleGetOuterTypeParametersOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetLocalTypeParametersOfType) {
		return call([&] { return handleGetLocalTypeParametersOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetThisTypeOfType) {
		return call([&] { return handleGetThisTypeOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetAliasTypeArgumentsOfType) {
		return call([&] { return handleGetAliasTypeArgumentsOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetAliasSymbolOfType) {
		return call([&] { return handleGetAliasSymbolOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetObjectTypeOfType) {
		return call([&] { return handleGetObjectTypeOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetIndexTypeOfType) {
		return call([&] { return handleGetIndexTypeOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetCheckTypeOfType) {
		return call([&] { return handleGetCheckTypeOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetExtendsTypeOfType) {
		return call([&] { return handleGetExtendsTypeOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetBaseTypeOfType) {
		return call([&] { return handleGetBaseTypeOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetConstraintOfType) {
		return call([&] { return handleGetConstraintOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetTypeParameterOfMappedType) {
		return call([&] { return handleGetTypeParameterOfMappedType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetConstraintTypeOfMappedType) {
		return call([&] { return handleGetConstraintTypeOfMappedType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetNameTypeOfMappedType) {
		return call([&] { return handleGetNameTypeOfMappedType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetTemplateTypeOfMappedType) {
		return call([&] { return handleGetTemplateTypeOfMappedType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetTrueTypeOfConditionalType) {
		return call([&] { return handleGetTrueTypeOfConditionalType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetFalseTypeOfConditionalType) {
		return call([&] { return handleGetFalseTypeOfConditionalType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetTypeParametersOfSignature) {
		return call([&] { return handleGetTypeParametersOfSignature(ctx, unmarshalParam<GetSignaturePropertyParams>(parsed)); });
	}
	if (method == MethodGetParametersOfSignature) {
		return call([&] { return handleGetParametersOfSignature(ctx, unmarshalParam<GetSignaturePropertyParams>(parsed)); });
	}
	if (method == MethodGetThisParameterOfSignature) {
		return call([&] { return handleGetThisParameterOfSignature(ctx, unmarshalParam<GetSignaturePropertyParams>(parsed)); });
	}
	if (method == MethodGetTargetOfSignature) {
		return call([&] { return handleGetTargetOfSignature(ctx, unmarshalParam<GetSignaturePropertyParams>(parsed)); });
	}
	if (method == MethodGetContextualType) {
		return call([&] { return handleGetContextualType(ctx, unmarshalParam<GetContextualTypeParams>(parsed)); });
	}
	if (method == MethodGetContextualTypeForArgument) {
		return call([&] { return handleGetContextualTypeForArgument(ctx, unmarshalParam<GetContextualTypeForArgumentParams>(parsed)); });
	}
	if (method == MethodGetAwaitedType) {
		return call([&] { return handleGetAwaitedType(ctx, unmarshalParam<CheckerTypeParams>(parsed)); });
	}
	if (method == MethodGetBaseTypeOfLiteralType) {
		return call([&] { return handleGetBaseTypeOfLiteralType(ctx, unmarshalParam<GetBaseTypeOfLiteralTypeParams>(parsed)); });
	}
	if (method == MethodGetNonNullableType) {
		return call([&] { return handleGetNonNullableType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetTypeFromTypeNode) {
		return call([&] { return handleGetTypeFromTypeNode(ctx, unmarshalParam<GetTypeFromTypeNodeParams>(parsed)); });
	}
	if (method == MethodGetWidenedType) {
		return call([&] { return handleGetWidenedType(ctx, unmarshalParam<GetWidenedTypeParams>(parsed)); });
	}
	if (method == MethodGetParameterType) {
		return call([&] { return handleGetParameterType(ctx, unmarshalParam<GetParameterTypeParams>(parsed)); });
	}
	if (method == MethodGetTypeParameterAtPosition) {
		return call([&] { return handleGetTypeParameterAtPosition(ctx, unmarshalParam<GetParameterTypeParams>(parsed)); });
	}
	if (method == MethodIsArrayLikeType) {
		return call([&] { return handleIsArrayLikeType(ctx, unmarshalParam<IsArrayLikeTypeParams>(parsed)); });
	}
	if (method == MethodIsTypeAssignableTo) {
		return call([&] { return handleIsTypeAssignableTo(ctx, unmarshalParam<IsTypeAssignableToParams>(parsed)); });
	}
	if (method == MethodGetShorthandAssignmentValueSymbol) {
		return call([&] { return handleGetShorthandAssignmentValueSymbol(ctx, unmarshalParam<GetTypeAtLocationParams>(parsed)); });
	}
	if (method == MethodGetTypeOfSymbolAtLocation) {
		return call([&] { return handleGetTypeOfSymbolAtLocation(ctx, unmarshalParam<GetTypeOfSymbolAtLocationParams>(parsed)); });
	}
	if (method == MethodTypeToTypeNode) {
		return handleTypeToTypeNode(ctx, unmarshalParam<TypeToTypeNodeParams>(parsed));
	}
	if (method == MethodSignatureToSignatureDeclaration) {
		return handleSignatureToSignatureDeclaration(ctx, unmarshalParam<SignatureToSignatureDeclarationParams>(parsed));
	}
	if (method == MethodTypeToString) {
		return handleTypeToString(ctx, unmarshalParam<TypeToTypeNodeParams>(parsed));
	}
	if (method == MethodIsContextSensitive) {
		return call([&] { return handleIsContextSensitive(ctx, unmarshalParam<GetContextualTypeParams>(parsed)); });
	}
	if (method == MethodGetReturnTypeOfSignature) {
		return call([&] { return handleGetReturnTypeOfSignature(ctx, unmarshalParam<GetSignaturePropertyParams>(parsed)); });
	}
	if (method == MethodGetRestTypeOfSignature) {
		return call([&] { return handleGetRestTypeOfSignature(ctx, unmarshalParam<CheckerSignatureParams>(parsed)); });
	}
	if (method == MethodGetTypePredicateOfSignature) {
		return call([&] { return handleGetTypePredicateOfSignature(ctx, unmarshalParam<CheckerSignatureParams>(parsed)); });
	}
	if (method == MethodGetBaseTypes) {
		return call([&] { return handleGetBaseTypes(ctx, unmarshalParam<CheckerTypeParams>(parsed)); });
	}
	if (method == MethodGetPropertiesOfType) {
		return call([&] { return handleGetPropertiesOfType(ctx, unmarshalParam<CheckerTypeParams>(parsed)); });
	}
	if (method == MethodGetApparentPropertiesOfType) {
		return call([&] { return handleGetApparentPropertiesOfType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetApparentType) {
		return call([&] { return handleGetApparentType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetReducedType) {
		return call([&] { return handleGetReducedType(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetPropertyOfType) {
		return call([&] { return handleGetPropertyOfType(ctx, unmarshalParam<GetPropertyOfTypeParams>(parsed)); });
	}
	if (method == MethodGetTypeOfPropertyOfType) {
		return call([&] { return handleGetTypeOfPropertyOfType(ctx, unmarshalParam<GetPropertyOfTypeParams>(parsed)); });
	}
	if (method == MethodGetIndexInfoOfType) {
		return call([&] { return handleGetIndexInfoOfType(ctx, unmarshalParam<GetIndexInfoOfTypeParams>(parsed)); });
	}
	if (method == MethodGetIndexInfosOfType) {
		return call([&] { return handleGetIndexInfosOfType(ctx, unmarshalParam<CheckerTypeParams>(parsed)); });
	}
	if (method == MethodGetConstraintOfTypeParameter) {
		return call([&] { return handleGetConstraintOfTypeParameter(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetBaseConstraintOfType) {
		return call([&] { return handleGetBaseConstraintOfType(ctx, unmarshalParam<CheckerTypeParams>(parsed)); });
	}
	if (method == MethodGetDefaultFromTypeParameter) {
		return call([&] { return handleGetDefaultFromTypeParameter(ctx, unmarshalParam<GetTypePropertyParams>(parsed)); });
	}
	if (method == MethodGetTypeArguments) {
		return call([&] { return handleGetTypeArguments(ctx, unmarshalParam<CheckerTypeParams>(parsed)); });
	}
	if (method == MethodGetImportAdderEdits) {
		return call([&] { return handleGetImportAdderEdits(ctx, unmarshalParam<GetImportAdderEditsParams>(parsed)); });
	}
	if (method == MethodGetConstantValue) {
		return call([&] { return handleGetConstantValue(ctx, unmarshalParam<CheckerNodeParams>(parsed)); });
	}
	if (method == MethodGetSignatureFromDeclaration) {
		return call([&] { return handleGetSignatureFromDeclaration(ctx, unmarshalParam<CheckerNodeParams>(parsed)); });
	}
	if (method == MethodGetExportSpecifierLocalTarget) {
		return call([&] { return handleGetExportSpecifierLocalTargetSymbol(ctx, unmarshalParam<CheckerNodeParams>(parsed)); });
	}
	if (method == MethodGetAliasedSymbol) {
		return call([&] { return handleGetAliasedSymbol(ctx, unmarshalParam<CheckerSymbolParams>(parsed)); });
	}
	if (method == MethodGetImmediateAliasedSymbol) {
		return call([&] { return handleGetImmediateAliasedSymbol(ctx, unmarshalParam<CheckerSymbolParams>(parsed)); });
	}
	if (method == MethodGetTargetSymbol) {
		return call([&] { return handleMethodGetTargetSymbol(ctx, unmarshalParam<CheckerSymbolParams>(parsed)); });
	}
	if (method == MethodGetExportSymbolOfSymbolForChecker) {
		return call([&] { return handleGetExportSymbolOfSymbolForChecker(ctx, unmarshalParam<CheckerSymbolParams>(parsed)); });
	}
	if (method == MethodGetFullyQualifiedName) {
		return call([&] { return handleGetFullyQualifiedName(ctx, unmarshalParam<CheckerSymbolParams>(parsed)); });
	}
	if (method == MethodGetExportsOfModule) {
		return call([&] { return handleGetExportsOfModule(ctx, unmarshalParam<CheckerSymbolParams>(parsed)); });
	}
	if (method == MethodGetMemberInModuleExports) {
		return call([&] { return handleGetMemberInModuleExports(ctx, unmarshalParam<GetMemberInModuleExportsParams>(parsed)); });
	}
	if (method == MethodGetJSDocTags) {
		return call([&] { return handleGetJSDocTags(ctx, unmarshalParam<CheckerSymbolParams>(parsed)); });
	}
	if (method == MethodGetDocumentationComment) {
		return call([&] { return handleGetDocumentationComment(ctx, unmarshalParam<CheckerSymbolParams>(parsed)); });
	}
	if (method == MethodIsArrayType) {
		return call([&] { return handleIsArrayType(ctx, unmarshalParam<CheckerTypeParams>(parsed)); });
	}
	if (method == MethodIsReadonlySymbol) {
		return call([&] { return handleIsReadonlySymbol(ctx, unmarshalParam<CheckerSymbolParams>(parsed)); });
	}
	if (method == MethodGetReferencesToSymbolInFile) {
		return call([&] { return handleGetReferencesToSymbolInFile(ctx, unmarshalParam<GetReferencesToSymbolInFileParams>(parsed)); });
	}
	if (method == MethodGetReferencedSymbolsForNode) {
		return call([&] { return handleGetReferencedSymbolsForNode(ctx, unmarshalParam<GetReferencedSymbolsForNodeParams>(parsed)); });
	}
	if (method == MethodGetSignatureUsages) {
		return call([&] { return handleGetSignatureUsages(ctx, unmarshalParam<GetSignatureUsagesParams>(parsed)); });
	}
	if (method == MethodGetCompletionsAtPosition) {
		return call([&] { return handleGetCompletionsAtPosition(ctx, unmarshalParam<GetCompletionsAtPositionParams>(parsed)); });
	}
	if (method == MethodPrintNode) {
		return call([&] { return handlePrintNode(ctx, unmarshalParam<PrintNodeParams>(parsed)); });
	}
	if (method == MethodFormatNodeForInsertion) {
		return call([&] { return handleFormatNodeForInsertion(ctx, unmarshalParam<FormatNodeForInsertionParams>(parsed)); });
	}
	if (method == MethodEmit) {
		return call([&] { return handleEmit(ctx, unmarshalParam<EmitParams>(parsed)); });
	}
	if (method == MethodEmitToString) {
		return call([&] { return handleEmitToString(ctx, unmarshalParam<EmitParams>(parsed)); });
	}
	if (method == MethodGetJavaScriptEmit) {
		return call([&] { return handleSelectedFilesEmit(ctx, unmarshalParam<SelectedFilesEmitParams>(parsed), compiler::EmitOnly::EmitOnlyJs); });
	}
	if (method == MethodGetDeclarationEmit) {
		return call([&] { return handleSelectedFilesEmit(ctx, unmarshalParam<SelectedFilesEmitParams>(parsed), compiler::EmitOnly::EmitOnlyDts); });
	}
	if (method == MethodGetAnyType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetAnyType(); }); });
	}
	if (method == MethodGetStringType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetStringType(); }); });
	}
	if (method == MethodGetNumberType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetNumberType(); }); });
	}
	if (method == MethodGetBooleanType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetBooleanType(); }); });
	}
	if (method == MethodGetVoidType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetVoidType(); }); });
	}
	if (method == MethodGetUndefinedType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetUndefinedType(); }); });
	}
	if (method == MethodGetNullType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetNullType(); }); });
	}
	if (method == MethodGetNeverType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetNeverType(); }); });
	}
	if (method == MethodGetUnknownType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetUnknownType(); }); });
	}
	if (method == MethodGetBigIntType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetBigIntType(); }); });
	}
	if (method == MethodGetESSymbolType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetESSymbolType(); }); });
	}
	if (method == MethodGetNonPrimitiveType) {
		return call([&] { return handleGetIntrinsicType(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed), [](checker::Checker* c) { return c->GetNonPrimitiveType(); }); });
	}
	if (method == MethodGetWellKnownSymbols) {
		return call([&] { return handleGetWellKnownSymbols(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed)); });
	}
	if (method == MethodGetWellKnownSignatures) {
		return call([&] { return handleGetWellKnownSignatures(ctx, unmarshalParam<GetIntrinsicTypeParams>(parsed)); });
	}
	if (method == MethodGetSyntacticDiagnostics) {
		return call([&] { return handleGetSyntacticDiagnostics(ctx, unmarshalParam<GetDiagnosticsParams>(parsed)); });
	}
	if (method == MethodGetBindDiagnostics) {
		return call([&] { return handleGetBindDiagnostics(ctx, unmarshalParam<GetDiagnosticsParams>(parsed)); });
	}
	if (method == MethodGetSemanticDiagnostics) {
		return call([&] { return handleGetSemanticDiagnostics(ctx, unmarshalParam<GetDiagnosticsParams>(parsed)); });
	}
	if (method == MethodGetSuggestionDiagnostics) {
		return call([&] { return handleGetSuggestionDiagnostics(ctx, unmarshalParam<GetDiagnosticsParams>(parsed)); });
	}
	if (method == MethodGetDeclarationDiagnostics) {
		return call([&] { return handleGetDeclarationDiagnostics(ctx, unmarshalParam<GetDiagnosticsParams>(parsed)); });
	}
	if (method == MethodGetProgramDiagnostics) {
		return call([&] { return handleGetProgramDiagnostics(ctx, unmarshalParam<GetProjectDiagnosticsParams>(parsed)); });
	}
	if (method == MethodGetGlobalDiagnostics) {
		return call([&] { return handleGetGlobalDiagnostics(ctx, unmarshalParam<GetProjectDiagnosticsParams>(parsed)); });
	}
	if (method == MethodGetConfigFileParsingDiagnostics) {
		return call([&] { return handleGetConfigFileParsingDiagnostics(ctx, unmarshalParam<GetProjectDiagnosticsParams>(parsed)); });
	}
	if (method == MethodStartCPUProfile) {
		return handleStartCPUProfile(ctx, unmarshalParam<ProfileParams>(parsed));
	}
	if (method == MethodStopCPUProfile) {
		return call([&] { return handleStopCPUProfile(ctx); });
	}
	if (method == MethodSaveHeapProfile) {
		return call([&] { return handleSaveHeapProfile(ctx, unmarshalParam<ProfileParams>(parsed)); });
	}
	return {ResultValue{},
	        gostd::newError("unknown method: " + method)};
}

// unmarshalers — proto.go:569. Maps each Method to a decoder producing a
// shared_ptr<T> params object held in std::any (Go's `any` = `*T`).
// noParams lives in proto.h.
using unmarshallerFn =
    std::function<std::pair<std::any, std::string>(const json::Value&)>;

// newBatchResponsePage — session.go:1083 (defined below handleBatchRequests).
static std::pair<batchResponsePage, gostd::Error> newBatchResponsePageImpl(
    const std::vector<BatchResponse>& responses);

static const std::unordered_map<std::string_view, unmarshallerFn> unmarshalers = {
    {MethodBatchRequests, &unmarshallerFor<BatchRequestsParams>},
    {MethodRelease, &unmarshallerFor<ReleaseParams>},
    {MethodReleaseSourceFile, &unmarshallerFor<ReleaseSourceFileParams>},
    {MethodInitialize, &noParams},
    {MethodCreateSnapshot, &unmarshallerFor<CreateSnapshotParams>},
    {MethodUpdateSnapshot, &unmarshallerFor<UpdateSnapshotParams>},
    {MethodGetCurrentLanguageServerSnapshot,
     &unmarshallerFor<GetCurrentLanguageServerSnapshotParams>},
    {MethodCreateBuildOrchestrator,
     &unmarshallerFor<CreateBuildOrchestratorParams>},
    {MethodDisposeBuildOrchestrator,
     &unmarshallerFor<DisposeBuildOrchestratorParams>},
    {MethodBuild, &unmarshallerFor<BuildParams>},
    {MethodBuildReferences, &unmarshallerFor<BuildParams>},
    {MethodCleanBuild, &unmarshallerFor<CleanBuildParams>},
    {MethodCleanReferences, &unmarshallerFor<CleanBuildParams>},
    {MethodCreateModuleResolver, &unmarshallerFor<CreateModuleResolverParams>},
    {MethodReleaseModuleResolver,
     &unmarshallerFor<ReleaseModuleResolverParams>},
    {MethodResolveModuleName, &unmarshallerFor<ResolveModuleNameParams>},
    {MethodParseCommandLine, &unmarshallerFor<ParseCommandLineParams>},
    {MethodReadConfigFile, &unmarshallerFor<ReadConfigFileParams>},
    {MethodParseJsonConfigFile,
     &unmarshallerFor<ParseJsonConfigFileContentParams>},
    {MethodParseConfigFile, &unmarshallerFor<ParseConfigFileParams>},
    {MethodCreateSourceFile, &unmarshallerFor<CreateSourceFileParams>},
    {MethodCreateSourceFileFromFile,
     &unmarshallerFor<CreateSourceFileFromFileParams>},
    {MethodTranspileModule, &unmarshallerFor<TranspileParams>},
    {MethodTranspileModuleFromFile,
     &unmarshallerFor<TranspileFromFileParams>},
    {MethodTranspileDeclaration, &unmarshallerFor<TranspileParams>},
    {MethodTranspileDeclarationFromFile,
     &unmarshallerFor<TranspileFromFileParams>},
    {MethodGetDefaultProjectForFile,
     &unmarshallerFor<GetDefaultProjectForFileParams>},
    {MethodGetSourceFile, &unmarshallerFor<GetSourceFileParams>},
    {MethodGetSourceFileNames, &unmarshallerFor<GetSourceFileNamesParams>},
    {MethodGetSourceFileMetadata, &unmarshallerFor<GetSourceFileParams>},
    {MethodGetModeForUsageLocation,
     &unmarshallerFor<GetModeForUsageLocationParams>},
    {MethodGetModeForResolutionAtIndex,
     &unmarshallerFor<GetModeForResolutionAtIndexParams>},
    {MethodGetResolvedModule, &unmarshallerFor<GetResolvedModuleParams>},
    {MethodGetResolvedModuleFromModuleSpecifier,
     &unmarshallerFor<GetResolvedModuleFromModuleSpecifierParams>},
    {MethodGetResolvedTypeReferenceDirective,
     &unmarshallerFor<GetResolvedTypeReferenceDirectiveParams>},
    {MethodGetResolvedTypeReferenceDirectiveFromReference,
     &unmarshallerFor<GetResolvedTypeReferenceDirectiveFromReferenceParams>},
    {MethodGetConfigFileNames, &unmarshallerFor<GetProjectDiagnosticsParams>},
    {MethodGetConfigSourceFile, &unmarshallerFor<GetSourceFileParams>},
    {MethodGetSymbolAtPosition, &unmarshallerFor<GetSymbolAtPositionParams>},
    {MethodGetSymbolsAtPositions,
     &unmarshallerFor<GetSymbolsAtPositionsParams>},
    {MethodGetSymbolAtLocation, &unmarshallerFor<GetSymbolAtLocationParams>},
    {MethodGetSymbolsAtLocations,
     &unmarshallerFor<GetSymbolsAtLocationsParams>},
    {MethodGetSymbolOfSourceFile,
     &unmarshallerFor<GetSymbolOfSourceFileParams>},
    {MethodGetSymbolsOfSourceFiles,
     &unmarshallerFor<GetSymbolsOfSourceFilesParams>},
    {MethodGetTypeOfSymbol, &unmarshallerFor<GetTypeOfSymbolParams>},
    {MethodGetTypesOfSymbols, &unmarshallerFor<GetTypesOfSymbolsParams>},
    {MethodGetDeclaredTypeOfSymbol, &unmarshallerFor<GetTypeOfSymbolParams>},
    {MethodGetNonMissingTypeOfSymbol, &unmarshallerFor<GetTypeOfSymbolParams>},
    {MethodResolveName, &unmarshallerFor<ResolveNameParams>},
    {MethodGetSymbolsInScope, &unmarshallerFor<GetSymbolsInScopeParams>},
    {MethodGetSignaturesOfType, &unmarshallerFor<GetSignaturesOfTypeParams>},
    {MethodGetResolvedSignature, &unmarshallerFor<GetResolvedSignatureParams>},
    {MethodGetTypeAtLocation, &unmarshallerFor<GetTypeAtLocationParams>},
    {MethodGetTypeAtLocations, &unmarshallerFor<GetTypeAtLocationsParams>},
    {MethodGetTypeAtPosition, &unmarshallerFor<GetTypeAtPositionParams>},
    {MethodGetTypesAtPositions, &unmarshallerFor<GetTypesAtPositionsParams>},

    {MethodGetParentOfSymbol, &unmarshallerFor<GetSymbolPropertyParams>},
    {MethodGetMembersOfSymbol, &unmarshallerFor<GetSymbolPropertyParams>},
    {MethodGetExportsOfSymbol, &unmarshallerFor<GetSymbolPropertyParams>},
    {MethodGetExportSymbolOfSymbol, &unmarshallerFor<GetSymbolPropertyParams>},

    {MethodGetSymbolOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetTargetOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetFreshTypeOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetRegularTypeOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetTypesOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetTypeParametersOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetOuterTypeParametersOfType,
     &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetLocalTypeParametersOfType,
     &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetThisTypeOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetAliasTypeArgumentsOfType,
     &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetAliasSymbolOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetObjectTypeOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetIndexTypeOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetCheckTypeOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetExtendsTypeOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetBaseTypeOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetConstraintOfType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetTypeParameterOfMappedType,
     &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetConstraintTypeOfMappedType,
     &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetNameTypeOfMappedType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetTemplateTypeOfMappedType,
     &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetTrueTypeOfConditionalType,
     &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetFalseTypeOfConditionalType,
     &unmarshallerFor<GetTypePropertyParams>},

    {MethodGetTypeParametersOfSignature,
     &unmarshallerFor<GetSignaturePropertyParams>},
    {MethodGetParametersOfSignature,
     &unmarshallerFor<GetSignaturePropertyParams>},
    {MethodGetThisParameterOfSignature,
     &unmarshallerFor<GetSignaturePropertyParams>},
    {MethodGetTargetOfSignature, &unmarshallerFor<GetSignaturePropertyParams>},

    {MethodGetContextualType, &unmarshallerFor<GetContextualTypeParams>},
    {MethodGetContextualTypeForArgument,
     &unmarshallerFor<GetContextualTypeForArgumentParams>},
    {MethodGetAwaitedType, &unmarshallerFor<CheckerTypeParams>},
    {MethodGetBaseTypeOfLiteralType,
     &unmarshallerFor<GetBaseTypeOfLiteralTypeParams>},
    {MethodGetNonNullableType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetTypeFromTypeNode, &unmarshallerFor<GetTypeFromTypeNodeParams>},
    {MethodGetWidenedType, &unmarshallerFor<GetWidenedTypeParams>},
    {MethodGetParameterType, &unmarshallerFor<GetParameterTypeParams>},
    {MethodGetTypeParameterAtPosition,
     &unmarshallerFor<GetParameterTypeParams>},
    {MethodIsArrayLikeType, &unmarshallerFor<IsArrayLikeTypeParams>},
    {MethodIsTypeAssignableTo, &unmarshallerFor<IsTypeAssignableToParams>},
    {MethodGetShorthandAssignmentValueSymbol,
     &unmarshallerFor<GetTypeAtLocationParams>},
    {MethodGetTypeOfSymbolAtLocation,
     &unmarshallerFor<GetTypeOfSymbolAtLocationParams>},
    {MethodTypeToTypeNode, &unmarshallerFor<TypeToTypeNodeParams>},
    {MethodSignatureToSignatureDeclaration,
     &unmarshallerFor<SignatureToSignatureDeclarationParams>},
    {MethodTypeToString, &unmarshallerFor<TypeToTypeNodeParams>},
    {MethodIsContextSensitive, &unmarshallerFor<GetContextualTypeParams>},
    {MethodGetReturnTypeOfSignature,
     &unmarshallerFor<GetSignaturePropertyParams>},
    {MethodGetRestTypeOfSignature, &unmarshallerFor<CheckerSignatureParams>},
    {MethodGetTypePredicateOfSignature,
     &unmarshallerFor<CheckerSignatureParams>},
    {MethodGetBaseTypes, &unmarshallerFor<CheckerTypeParams>},
    {MethodGetPropertiesOfType, &unmarshallerFor<CheckerTypeParams>},
    {MethodGetApparentPropertiesOfType,
     &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetApparentType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetReducedType, &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetPropertyOfType, &unmarshallerFor<GetPropertyOfTypeParams>},
    {MethodGetTypeOfPropertyOfType,
     &unmarshallerFor<GetPropertyOfTypeParams>},
    {MethodGetIndexInfoOfType, &unmarshallerFor<GetIndexInfoOfTypeParams>},
    {MethodGetIndexInfosOfType, &unmarshallerFor<CheckerTypeParams>},
    {MethodGetConstraintOfTypeParameter,
     &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetBaseConstraintOfType, &unmarshallerFor<CheckerTypeParams>},
    {MethodGetDefaultFromTypeParameter,
     &unmarshallerFor<GetTypePropertyParams>},
    {MethodGetTypeArguments, &unmarshallerFor<CheckerTypeParams>},
    {MethodGetImportAdderEdits, &unmarshallerFor<GetImportAdderEditsParams>},
    {MethodGetConstantValue, &unmarshallerFor<CheckerNodeParams>},
    {MethodGetSignatureFromDeclaration, &unmarshallerFor<CheckerNodeParams>},
    {MethodGetExportSpecifierLocalTarget, &unmarshallerFor<CheckerNodeParams>},
    {MethodGetAliasedSymbol, &unmarshallerFor<CheckerSymbolParams>},
    {MethodGetImmediateAliasedSymbol, &unmarshallerFor<CheckerSymbolParams>},
    {MethodGetTargetSymbol, &unmarshallerFor<CheckerSymbolParams>},
    {MethodGetExportSymbolOfSymbolForChecker,
     &unmarshallerFor<CheckerSymbolParams>},
    {MethodGetFullyQualifiedName, &unmarshallerFor<CheckerSymbolParams>},
    {MethodGetExportsOfModule, &unmarshallerFor<CheckerSymbolParams>},
    {MethodGetMemberInModuleExports,
     &unmarshallerFor<GetMemberInModuleExportsParams>},
    {MethodGetJSDocTags, &unmarshallerFor<CheckerSymbolParams>},
    {MethodGetDocumentationComment, &unmarshallerFor<CheckerSymbolParams>},
    {MethodIsArrayType, &unmarshallerFor<CheckerTypeParams>},
    {MethodIsReadonlySymbol, &unmarshallerFor<CheckerSymbolParams>},
    {MethodGetReferencesToSymbolInFile,
     &unmarshallerFor<GetReferencesToSymbolInFileParams>},
    {MethodGetReferencedSymbolsForNode,
     &unmarshallerFor<GetReferencedSymbolsForNodeParams>},
    {MethodGetSignatureUsages, &unmarshallerFor<GetSignatureUsagesParams>},
    {MethodGetCompletionsAtPosition,
     &unmarshallerFor<GetCompletionsAtPositionParams>},
    {MethodPrintNode, &unmarshallerFor<PrintNodeParams>},
    {MethodFormatNodeForInsertion,
     &unmarshallerFor<FormatNodeForInsertionParams>},
    {MethodEmit, &unmarshallerFor<EmitParams>},
    {MethodEmitToString, &unmarshallerFor<EmitParams>},
    {MethodGetJavaScriptEmit, &unmarshallerFor<SelectedFilesEmitParams>},
    {MethodGetDeclarationEmit, &unmarshallerFor<SelectedFilesEmitParams>},
    {MethodGetAnyType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetStringType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetNumberType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetBooleanType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetVoidType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetUndefinedType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetNullType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetNeverType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetUnknownType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetBigIntType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetESSymbolType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetNonPrimitiveType, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetWellKnownSymbols, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetWellKnownSignatures, &unmarshallerFor<GetIntrinsicTypeParams>},
    {MethodGetSyntacticDiagnostics, &unmarshallerFor<GetDiagnosticsParams>},
    {MethodGetBindDiagnostics, &unmarshallerFor<GetDiagnosticsParams>},
    {MethodGetSemanticDiagnostics, &unmarshallerFor<GetDiagnosticsParams>},
    {MethodGetSuggestionDiagnostics, &unmarshallerFor<GetDiagnosticsParams>},
    {MethodGetDeclarationDiagnostics, &unmarshallerFor<GetDiagnosticsParams>},
    {MethodGetProgramDiagnostics, &unmarshallerFor<GetProjectDiagnosticsParams>},
    {MethodGetGlobalDiagnostics, &unmarshallerFor<GetProjectDiagnosticsParams>},
    {MethodGetConfigFileParsingDiagnostics,
     &unmarshallerFor<GetProjectDiagnosticsParams>},
    {MethodStartCPUProfile, &unmarshallerFor<ProfileParams>},
    {MethodStopCPUProfile, &noParams},
    {MethodSaveHeapProfile, &unmarshallerFor<ProfileParams>},
};

// unmarshalPayload — proto.go:2026.
std::pair<std::any, std::string> unmarshalPayload(std::string_view method,
                                                  const json::Value& payload) {
	auto it = unmarshalers.find(method);
	if (it == unmarshalers.end()) {
		return {std::any{},
		        "unknown API method " + gostd::sprintf("%q", {method})};
	}
	return it->second(payload);
}

// handleBatchRequests — session.go:1061.
std::pair<std::unique_ptr<BatchRequestsResponse>, gostd::Error>
Session::handleBatchRequests(gostd::Context ctx,
                             const BatchRequestsParams* params) {
	if (!params->ContinuationToken.empty()) {
		auto [value, ok] =
		    batchResponsePages.LoadAndDelete(params->ContinuationToken);
		if (!ok) {
			return {nullptr,
			        gostd::errorf("%w: invalid batch continuation token",
			                      {ErrClientError})};
		}
		return paginateBatchResponses(value, nullptr,
		                              params->MaxResponseBytesPerPage);
	}

	std::vector<BatchResponse> responses(params->Requests.size());
	for (size_t i = 0; i < params->Requests.size(); ++i) {
		responses[i] = handleBatchRequest(ctx, params->Requests[i]);
	}
	auto [page, err] = newBatchResponsePageImpl(responses);
	if (err) {
		return {nullptr, err};
	}
	return paginateBatchResponses(page, &responses,
	                              params->MaxResponseBytesPerPage);
}

// newBatchResponsePage — session.go:1083.
static std::pair<batchResponsePage, gostd::Error> newBatchResponsePageImpl(
    const std::vector<BatchResponse>& responses) {
	std::vector<json::Value> encodedResponses(responses.size());
	for (size_t i = 0; i < responses.size(); ++i) {
		auto [encoded, err] = json::marshal(responses[i]);
		if (!err.empty()) {
			return {batchResponsePage{}, gostd::newError(err)};
		}
		encodedResponses[i] = encoded;
	}
	return {batchResponsePage{std::move(encodedResponses)}, nullptr};
}

// paginateBatchResponses — session.go:1097.
std::pair<std::unique_ptr<BatchRequestsResponse>, gostd::Error>
Session::paginateBatchResponses(batchResponsePage& page,
                                std::vector<BatchResponse>* responses,
                                int64_t maxResponseBytesPerPage) {
	if (maxResponseBytesPerPage <= 0) {
		maxResponseBytesPerPage = DefaultMaxResponseBytesPerPage;
	}
	int64_t encodedLength = int64_t(strlen(R"({"responses":[]})"));
	size_t pageLength = 0;
	for (auto& encoded : page.encodedResponses) {
		int64_t additionalLength = int64_t(encoded.size());
		if (pageLength > 0) {
			additionalLength++;
		}
		if (pageLength > 0 &&
		    encodedLength + additionalLength > maxResponseBytesPerPage) {
			break;
		}
		encodedLength += additionalLength;
		pageLength++;
	}

	if (pageLength == page.encodedResponses.size()) {
		auto resp = std::make_unique<BatchRequestsResponse>();
		if (responses != nullptr) {
			resp->Responses = *responses;
		}
		resp->encodedResponses = std::move(page.encodedResponses);
		return {std::move(resp), nullptr};
	}
	std::string continuationToken =
	    id + "-" + std::to_string(++nextBatchResponsePageID);
	int64_t continuationLength =
	    int64_t(strlen(R"(,"continuationToken":"")")) +
	    int64_t(continuationToken.size());
	while (pageLength > 1 &&
	       encodedLength + continuationLength > maxResponseBytesPerPage) {
		encodedLength -= int64_t(page.encodedResponses[pageLength - 1].size()) + 1;
		pageLength--;
	}
	auto response = std::make_unique<BatchRequestsResponse>();
	response->ContinuationToken = continuationToken;
	response->encodedResponses = std::vector<json::Value>(
	    std::make_move_iterator(page.encodedResponses.begin()),
	    std::make_move_iterator(page.encodedResponses.begin() + pageLength));
	std::vector<json::Value> remainingResponses(
	    std::make_move_iterator(page.encodedResponses.begin() + pageLength),
	    std::make_move_iterator(page.encodedResponses.end()));
	if (responses != nullptr) {
		response->Responses.assign(responses->begin(),
		                           responses->begin() + pageLength);
	}
	batchResponsePages.Store(continuationToken,
	                         batchResponsePage{std::move(remainingResponses)});
	return {std::move(response), nullptr};
}

// handleBatchRequest — session.go:1142.
BatchResponse Session::handleBatchRequest(gostd::Context ctx,
                                          const BatchRequest& request) {
	BatchResponse response;
	response.Method = request.Method;
	if (request.Method == MethodBatchRequests) {
		response.Error = gostd::sprintf("%s: batchRequests cannot be nested",
		                                {ErrInvalidRequest});
		return response;
	}
	// Go's `defer recover` — catch all handler exceptions and report like Go's
	// panic recovery (debug.Stack() has no portable equivalent; message only).
	try {
		auto [rv, err] = handleRequest(ctx, request.Method, request.Params);
		if (err) {
			response.Error = err->Error();
		} else if (rv.isRaw && isSourceFileResponseMethod(request.Method)) {
			if (rv.data.empty()) {
				response.Result = json::Value();
			} else {
				SourceFileResponse sfr;
				sfr.Data = base64Encode(rv.data);
				auto [data, merr] = json::marshal(sfr);
				if (merr.empty()) {
					response.Result = json::Value(std::move(data));
				} else {
					response.Error = merr;
				}
			}
		} else if (rv.isRaw) {
			// RawBinary for a non-source-file method: Go json.Marshal would
			// emit []byte as a base64 string.
			response.Result =
			    json::Value("\"" + base64Encode(rv.data) + "\"");
		} else {
			response.Result = rv.data.empty() ? json::Value("null")
			                                  : json::Value(std::move(rv.data));
		}
	} catch (const std::exception& e) {
		response.Result = json::Value();
		response.Error = std::string("panic: ") + e.what() + "\n" +
		                 "<stack unavailable>";
	} catch (...) {
		response.Result = json::Value();
		response.Error = "panic: unknown\n<stack unavailable>";
	}
	return response;
}

// isSourceFileResponseMethod — session.go:1165.
bool isSourceFileResponseMethod(Method method) {
	return method == MethodCreateSourceFile ||
	       method == MethodCreateSourceFileFromFile || method == MethodGetSourceFile ||
	       method == MethodGetConfigSourceFile || method == MethodTypeToTypeNode ||
	       method == MethodSignatureToSignatureDeclaration;
}

// handleStartCPUProfile — session.go:1183.
std::pair<ResultValue, gostd::Error> Session::handleStartCPUProfile(
    gostd::Context, const ProfileParams* params) {
	if (params == nullptr || params->Dir.empty()) {
		return {ResultValue{},
		        gostd::errorf("%w: dir is required", {ErrClientError})};
	}
	if (std::string err = cpuProfiler.startCPUProfile(params->Dir); !err.empty()) {
		return {ResultValue{},
		        gostd::errorf("%w: failed to start CPU profile: %w",
		                      {ErrClientError, gostd::newError(err)})};
	}
	return {ResultValue{}, nullptr};
}

// handleStopCPUProfile — session.go:1193.
std::pair<std::unique_ptr<ProfileResult>, gostd::Error>
Session::handleStopCPUProfile(gostd::Context) {
	auto [filePath, err] = cpuProfiler.stopCPUProfile();
	if (!err.empty()) {
		return {nullptr,
		        gostd::errorf("%w: failed to stop CPU profile: %w",
		                      {ErrClientError, gostd::newError(err)})};
	}
	auto resp = std::make_unique<ProfileResult>();
	resp->File = filePath;
	return {std::move(resp), nullptr};
}

// handleSaveHeapProfile — session.go:1201.
std::pair<std::unique_ptr<ProfileResult>, gostd::Error>
Session::handleSaveHeapProfile(gostd::Context, const ProfileParams* params) {
	if (params == nullptr || params->Dir.empty()) {
		return {nullptr, gostd::errorf("%w: dir is required", {ErrClientError})};
	}
	auto [filePath, err] = pprof::saveHeapProfile(params->Dir);
	if (!err.empty()) {
		return {nullptr,
		        gostd::errorf("%w: failed to save heap profile: %w",
		                      {ErrClientError, gostd::newError(err)})};
	}
	auto resp = std::make_unique<ProfileResult>();
	resp->File = filePath;
	return {std::move(resp), nullptr};
}

// handleInitialize — session.go:1218.
std::pair<std::unique_ptr<InitializeResponse>, gostd::Error>
Session::handleInitialize(gostd::Context) {
	auto resp = std::make_unique<InitializeResponse>();
	resp->UseCaseSensitiveFileNames = useCaseSensitiveFileNames();
	resp->CurrentDirectory = GetCurrentDirectory();
	return {std::move(resp), nullptr};
}

// handleCreateSnapshot creates a new independent snapshot.
std::pair<std::unique_ptr<CreateSnapshotResponse>, gostd::Error>
Session::handleCreateSnapshot(gostd::Context ctx,
                              const CreateSnapshotParams* params) {
	auto [apiRequest, err] = toAPISnapshotRequest(ctx, params);
	if (err) {
		return {nullptr, err};
	}

	snapshotOpenState openState =
	    reconcileSnapshotOpens(apiRequest.get(), snapshotOpenState{});
	project::FileChangeSummary fileChanges =
	    toFileChangeSummary(params->FileNotifications.get());
	std::shared_ptr<vfs::FS> snapshotFileSystem;
	if (params->FileSystem != nullptr) {
		auto [fileSystem, fileSystemErr] =
		    requestfilesystem::NewForUpdate(params->FileSystem.get(), FS(),
		                                    GetCurrentDirectory(), &fileChanges);
		if (fileSystemErr) {
			return {nullptr,
			        gostd::errorf("%w: %w", {ErrClientError, fileSystemErr})};
		}
		snapshotFileSystem = fileSystem;
		apiRequest->FileSystem = fileSystem;
		apiRequest->ReplaceFileSystem =
		    params->FileSystem->kind == requestfilesystem::KindFull;
	}
	project::Snapshot* root = snapshotHost->NewRootSnapshot();
	auto [snapshot, err2] =
	    snapshotHost->CloneSnapshot(ctx, root, fileChanges, apiRequest.get());
	root->Deref();
	if (err2) {
		snapshot->Deref();
		return {nullptr,
		        gostd::errorf("%w: failed to create snapshot: %w",
		                      {ErrClientError, err2})};
	}
	if (gostd::Error merr = moduleResolutionError(snapshot)) {
		snapshot->Deref();
		return {nullptr, merr};
	}

	auto response =
	    createSnapshotResponse(snapshot, nullptr, params);
	registerSnapshot(snapshot, std::move(openState), snapshotFileSystem);
	return {std::move(response), nullptr};
}

// handleUpdateSnapshot — session.go:1254.
std::pair<std::unique_ptr<CreateSnapshotResponse>, gostd::Error>
Session::handleUpdateSnapshot(gostd::Context ctx,
                              const UpdateSnapshotParams* params) {
	auto [baseSD, err] = retainSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}
	deferGuard releaseGuard{[&] { (void)releaseSnapshot(params->Snapshot); }};

	const CreateSnapshotParams* changes = params->Changes.get();
	if (changes == nullptr) {
		static const CreateSnapshotParams emptyChanges;
		changes = &emptyChanges;
	}
	auto [apiRequest, err2] = toAPISnapshotRequest(ctx, changes);
	if (err2) {
		return {nullptr, err2};
	}
	snapshotOpenState openState = reconcileSnapshotOpens(
	    apiRequest.get(),
	    snapshotOpenState{baseSD->openProjects, baseSD->openFiles});
	project::FileChangeSummary fileChanges =
	    toFileChangeSummary(changes->FileNotifications.get());
	std::shared_ptr<vfs::FS> snapshotFileSystem = baseSD->fileSystem;
	if (changes->FileSystem != nullptr) {
		std::shared_ptr<vfs::FS> baseFileSystem = snapshotFileSystem;
		if (baseFileSystem == nullptr) {
			baseFileSystem = FS();
		}
		auto [fileSystem, fileSystemErr] =
		    requestfilesystem::NewForUpdate(changes->FileSystem.get(),
		                                    baseFileSystem, GetCurrentDirectory(),
		                                    &fileChanges);
		if (fileSystemErr) {
			return {nullptr,
			        gostd::errorf("%w: %w", {ErrClientError, fileSystemErr})};
		}
		snapshotFileSystem = fileSystem;
	}
	if (snapshotFileSystem != nullptr) {
		apiRequest->FileSystem = snapshotFileSystem;
		apiRequest->ReplaceFileSystem =
		    changes->FileSystem != nullptr &&
		    changes->FileSystem->kind == requestfilesystem::KindFull;
	}
	auto [snapshot, err3] = snapshotHost->CloneSnapshot(
	    ctx, baseSD->snapshot, fileChanges, apiRequest.get());
	if (err3) {
		snapshot->Deref();
		return {nullptr,
		        gostd::errorf("%w: failed to update snapshot: %w",
		                      {ErrClientError, err3})};
	}
	if (gostd::Error merr = moduleResolutionError(snapshot)) {
		snapshot->Deref();
		return {nullptr, merr};
	}

	auto response =
	    createSnapshotResponse(snapshot, baseSD->snapshot, changes);
	registerSnapshot(snapshot, std::move(openState), snapshotFileSystem);
	return {std::move(response), nullptr};
}

// toAPISnapshotRequest — session.go:1297.
std::pair<std::unique_ptr<project::APISnapshotRequest>, gostd::Error>
Session::toAPISnapshotRequest(gostd::Context ctx,
                              const SnapshotRequestChangesParams* changes) {
	auto apiRequest = std::make_unique<project::APISnapshotRequest>();

	for (const auto& p : changes->OpenProjects) {
		std::string configFileName =
		    p.ToAbsoluteFileName(GetCurrentDirectory());
		auto [configuredProjectID, ok] =
		    project::ParseConfiguredProjectID(toPath(configFileName));
		if (!ok) {
			return {nullptr,
			        gostd::errorf("%w: invalid configured project ID: %s",
			                      {ErrClientError, configFileName})};
		}
		if (apiRequest->EnsurePrograms == nullptr) {
			apiRequest->EnsurePrograms =
			    collections::newSetWithSizeHint<project::ID>(
			        changes->OpenProjects.size());
		}
		apiRequest->EnsurePrograms->Add(project::ID(configuredProjectID));
		if (apiRequest->OpenProjects == nullptr) {
			apiRequest->OpenProjects =
			    collections::newSetWithSizeHint<std::string>(
			        changes->OpenProjects.size());
		}
		apiRequest->OpenProjects->Add(configFileName);
	}

	for (const auto& p : changes->CloseProjects) {
		tspath::Path configPath =
		    toPath(p.ToAbsoluteFileName(GetCurrentDirectory()));
		if (apiRequest->CloseProjects == nullptr) {
			apiRequest->CloseProjects =
			    collections::newSetWithSizeHint<tspath::Path>(
			        changes->CloseProjects.size());
		}
		apiRequest->CloseProjects->Add(configPath);
	}

	for (const auto& f : changes->OpenFiles) {
		std::string fileName = f.ToAbsoluteFileName(GetCurrentDirectory());
		tspath::Path path = toPath(fileName);
		if (apiRequest->OpenFiles.empty()) {
		}
		if (apiRequest->OpenFiles.find(path) == apiRequest->OpenFiles.end()) {
			apiRequest->OpenFiles[path] = fileName;
			apiRequest->EnsureFiles[path] = fileName;
		}
	}

	for (const auto& f : changes->CloseFiles) {
		tspath::Path path =
		    toPath(f.ToURI(GetCurrentDirectory()).FileName());
		if (apiRequest->CloseFiles == nullptr) {
			apiRequest->CloseFiles =
			    collections::newSetWithSizeHint<tspath::Path>(
			        changes->CloseFiles.size());
		}
		apiRequest->CloseFiles->Add(path);
	}

	apiRequest->CreatePrograms.resize(changes->CreatePrograms.size());
	for (size_t i = 0; i < changes->CreatePrograms.size(); ++i) {
		auto& programParams = changes->CreatePrograms[i];
		if (programParams == nullptr) {
			return {nullptr,
			        gostd::errorf("%w: createPrograms[%d] must not be null",
			                      {ErrClientError, int64_t(i)})};
		}
		std::vector<std::string> rootFileNames(
		    programParams->RootFiles.size());
		for (size_t j = 0; j < programParams->RootFiles.size(); ++j) {
			rootFileNames[j] =
			    programParams->RootFiles[j].ToAbsoluteFileName(
			        GetCurrentDirectory());
		}
		auto request = std::make_unique<project::APICreateProgramRequest>();
		request->RootFileNames = std::move(rootFileNames);
		request->CompilerOptions = const_cast<tsc::CompilerOptions*>(
		    &programParams->CompilerOptions);
		if (programParams->Options != nullptr) {
			request->ProjectReferences = tsc::mapVec<tsc::ProjectReference*>(
			    programParams->Options->ProjectReferences,
			    [](const std::shared_ptr<tsc::ProjectReference>& p) {
				    return p.get();
			    });
			request->ConfigFileParsingDiagnostics =
			    tsc::mapVec<Diagnostic*>(
			        programParams->Options->ConfigFileParsingDiagnostics,
			        [](const std::shared_ptr<DiagnosticResponse>& d) {
				        return d->ToDiagnostic();
			        });
			auto [factory, ferr] =
			    moduleResolverFactory(ctx, programParams->Options.get());
			if (ferr) {
				return {nullptr, ferr};
			}
			request->ModuleResolverFactory = std::move(factory);
			request->ModuleResolverID =
			    uint64_t(programParams->Options->ModuleResolver);
		}
		apiRequest->CreatePrograms[i] = request.release();
	}
	apiRequest->ReconfigurePrograms.resize(changes->ReconfigurePrograms.size());
	collections::Set<project::SyntheticProjectID> reconfiguredProgramIDs;
	for (size_t i = 0; i < changes->ReconfigurePrograms.size(); ++i) {
		auto& programParams = changes->ReconfigurePrograms[i];
		if (programParams == nullptr) {
			return {nullptr,
			        gostd::errorf("%w: reconfigurePrograms[%d] must not be null",
			                      {ErrClientError, int64_t(i)})};
		}
		auto [programID, ok] =
		    project::ParseSyntheticProjectID(programParams->Id);
		if (!ok) {
			return {nullptr,
			        gostd::errorf("%w: invalid synthetic project handle: %s",
			                      {ErrClientError, programParams->Id})};
		}
		if (reconfiguredProgramIDs.Has(programID)) {
			return {nullptr,
			        gostd::errorf("%w: synthetic program reconfigured more than once: %s",
			                      {ErrClientError, programID})};
		}
		reconfiguredProgramIDs.Add(programID);
		std::vector<std::string> rootFileNames(
		    programParams->RootFiles.size());
		for (size_t j = 0; j < programParams->RootFiles.size(); ++j) {
			rootFileNames[j] =
			    programParams->RootFiles[j].ToAbsoluteFileName(
			        GetCurrentDirectory());
		}
		auto request = std::make_unique<project::APIReconfigureProgramRequest>();
		request->ProgramID = programID;
		request->RootFileNames = std::move(rootFileNames);
		request->CompilerOptions = const_cast<tsc::CompilerOptions*>(
		    &programParams->CompilerOptions);
		if (programParams->Options != nullptr) {
			request->ProjectReferences = tsc::mapVec<tsc::ProjectReference*>(
			    programParams->Options->ProjectReferences,
			    [](const std::shared_ptr<tsc::ProjectReference>& p) {
				    return p.get();
			    });
			request->ConfigFileParsingDiagnostics =
			    tsc::mapVec<Diagnostic*>(
			        programParams->Options->ConfigFileParsingDiagnostics,
			        [](const std::shared_ptr<DiagnosticResponse>& d) {
				        return d->ToDiagnostic();
			        });
			auto [factory, ferr] =
			    moduleResolverFactory(ctx, programParams->Options.get());
			if (ferr) {
				return {nullptr, ferr};
			}
			request->ModuleResolverFactory = std::move(factory);
			request->ModuleResolverID =
			    uint64_t(programParams->Options->ModuleResolver);
		}
		apiRequest->ReconfigurePrograms[i] = request.release();
	}
	if (!changes->RemovePrograms.empty()) {
		apiRequest->RemovePrograms =
		    collections::newSetWithSizeHint<project::SyntheticProjectID>(
		        changes->RemovePrograms.size());
	}
	for (const auto& programID : changes->RemovePrograms) {
		if (reconfiguredProgramIDs.Has(programID)) {
			return {nullptr,
			        gostd::errorf("%w: synthetic program cannot be reconfigured and removed: %s",
			                      {ErrClientError, programID})};
		}
		apiRequest->RemovePrograms->Add(programID);
	}
	if (changes->EnsurePrograms != nullptr) {
		apiRequest->EnsureAllPrograms = changes->EnsurePrograms->All;
		if (!changes->EnsurePrograms->Projects.empty() &&
		    apiRequest->EnsurePrograms == nullptr) {
			apiRequest->EnsurePrograms =
			    collections::newSetWithSizeHint<project::ID>(
			        changes->EnsurePrograms->Projects.size());
		}
		for (const auto& program : changes->EnsurePrograms->Projects) {
			apiRequest->EnsurePrograms->Add(program);
		}
	}
	return {std::move(apiRequest), nullptr};
}

// toLanguageServerSnapshotUpdate — session.go:1444.
std::pair<std::unique_ptr<languageServerSnapshotUpdate>, gostd::Error>
Session::toLanguageServerSnapshotUpdate(
    gostd::Context ctx, const SnapshotRequestChangesParams* changes) {
	auto [apiRequest, err] = toAPISnapshotRequest(ctx, changes);
	if (err) {
		return {nullptr, err};
	}
	auto update = std::make_unique<languageServerSnapshotUpdate>();
	update->request = std::move(apiRequest);
	update->openState = reconcileSnapshotOpens(
	    update->request.get(),
	    snapshotOpenState{openProjects, openFiles});

	// Go iterates RemovePrograms.Keys() while deleting — snapshot the keys
	// first since unordered_set iterators invalidate on erase.
	if (update->request->RemovePrograms != nullptr) {
		std::vector<project::SyntheticProjectID> keys(
		    update->request->RemovePrograms->Keys().begin(),
		    update->request->RemovePrograms->Keys().end());
		for (const auto& programID : keys) {
			if (!createdPrograms.Has(programID)) {
				update->request->RemovePrograms->Delete(programID);
			}
		}
	}
	for (const auto& reconfigure : update->request->ReconfigurePrograms) {
		if (!createdPrograms.Has(reconfigure->ProgramID)) {
			return {nullptr,
			        gostd::errorf("%w: synthetic program is not owned by this API session: %s",
			                      {ErrClientError, reconfigure->ProgramID})};
		}
	}
	return {std::move(update), nullptr};
}

// languageServerSnapshotUpdate::commit — session.go:1467.
void languageServerSnapshotUpdate::commit(Session* s,
                                          project::Snapshot* snapshot) {
	s->openProjects = openState.openProjects.Clone();
	s->openFiles = openState.openFiles.Clone();
	if (request->RemovePrograms != nullptr) {
		for (const auto& programID : request->RemovePrograms->Keys()) {
			s->createdPrograms.Delete(programID);
		}
	}
	for (project::Project* program : snapshot->CreatedPrograms()) {
		auto [programID, ok] = program->ID().Synthetic();
		if (!ok) {
			TSC_UNREACHABLE(
			    (std::string(
			         "created program has non-synthetic project ID: ") +
			     program->ID().String())
			        .c_str());
		}
		s->createdPrograms.Add(programID);
	}
}

// reconcileSnapshotOpens — session.go:1489.
snapshotOpenState Session::reconcileSnapshotOpens(
    project::APISnapshotRequest* apiRequest, snapshotOpenState base) {
	snapshotOpenState state{base.openProjects.Clone(), base.openFiles.Clone()};
	if (apiRequest->CloseProjects != nullptr) {
		for (const auto& path : apiRequest->CloseProjects->Keys()) {
			if (state.openProjects.Has(path)) {
				state.openProjects.Delete(path);
			} else {
				apiRequest->CloseProjects->Delete(path);
			}
		}
	}
	if (apiRequest->OpenProjects != nullptr) {
		// Iterate a copy: the set is mutated (Delete) during iteration.
		std::vector<std::string> keys(apiRequest->OpenProjects->Keys().begin(),
		                              apiRequest->OpenProjects->Keys().end());
		for (const auto& configFileName : keys) {
			tspath::Path path = toPath(configFileName);
			if (state.openProjects.Has(path)) {
				apiRequest->OpenProjects->Delete(configFileName);
			} else {
				state.openProjects.Add(path);
			}
		}
	}
	if (apiRequest->CloseFiles != nullptr) {
		std::vector<tspath::Path> keys(apiRequest->CloseFiles->Keys().begin(),
		                               apiRequest->CloseFiles->Keys().end());
		for (const auto& path : keys) {
			if (state.openFiles.Has(path)) {
				state.openFiles.Delete(path);
			} else {
				apiRequest->CloseFiles->Delete(path);
			}
		}
	}
	{
		std::vector<tspath::Path> keys;
		keys.reserve(apiRequest->OpenFiles.size());
		for (const auto& [path, _] : apiRequest->OpenFiles) {
			keys.push_back(path);
		}
		for (const auto& path : keys) {
			if (state.openFiles.Has(path)) {
				apiRequest->OpenFiles.erase(path);
			} else {
				state.openFiles.Add(path);
			}
		}
	}
	return state;
}

// registerSnapshot — session.go:1526.
void Session::registerSnapshot(project::Snapshot* snapshot,
                               snapshotOpenState openState,
                               const std::shared_ptr<vfs::FS>& fileSystem) {
	// If the same snapshot ID is returned (no changes), we increment the ref count
	// so each client-side Snapshot can be disposed independently.
	SnapshotID handle = snapshotHandle(snapshot);
	std::unique_lock lk(snapshotsMu);
	auto it = snapshots.find(handle);
	if (it != snapshots.end()) {
		// Same snapshot already stored — release the caller's ref since
		// the stored snapshot already has one, and bump the API refcount.
		snapshot->Deref();
		it->second->refCount++;
	} else {
		auto sd = std::make_unique<snapshotData>();
		sd->snapshot = snapshot;
		sd->fileSystem = fileSystem;
		sd->refCount = 1;
		sd->openProjects = openState.openProjects.Clone();
		sd->openFiles = openState.openFiles.Clone();
		snapshots[handle] = std::move(sd);
	}
}

// handleGetCurrentLanguageServerSnapshot — session.go:1545.
std::pair<std::unique_ptr<CreateSnapshotResponse>, gostd::Error>
Session::handleGetCurrentLanguageServerSnapshot(
    gostd::Context ctx,
    const GetCurrentLanguageServerSnapshotParams* params) {
	if (projectSession == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: getCurrentLanguageServerSnapshot requires an LSP-connected API session",
		                      {ErrClientError})};
	}
	project::Snapshot* baseSnapshot = nullptr;
	deferGuard baseRelease;
	if (params->BaseSnapshot != 0) {
		auto [baseSD, err] = retainSnapshotData(params->BaseSnapshot);
		if (err) {
			return {nullptr, err};
		}
		baseRelease.f = [&] {
			(void)releaseSnapshot(params->BaseSnapshot);
		};
		baseSnapshot = baseSD->snapshot;
	}

	std::lock_guard languageServerUpdateLock(languageServerUpdateMu);

	const LanguageServerSnapshotChanges* changes = params->Changes.get();
	if (changes == nullptr) {
		static const LanguageServerSnapshotChanges emptyChanges;
		changes = &emptyChanges;
	}
	auto [update, err2] = toLanguageServerSnapshotUpdate(ctx, changes);
	if (err2) {
		return {nullptr, err2};
	}

	auto [snapshot, err3] = projectSession->APIUpdate(
	    ctx, project::FileChangeSummary{}, update->request.get());
	if (err3) {
		return {nullptr,
		        gostd::errorf("%w: failed to update language server snapshot: %w",
		                      {ErrClientError, err3})};
	}

	update->commit(this, snapshot);
	auto response =
	    createSnapshotResponse(snapshot, baseSnapshot, changes);
	registerSnapshot(snapshot,
	                 snapshotOpenState{openProjects, openFiles}, nullptr);
	return {std::move(response), nullptr};
}

// handleRelease decrements the ref count for a snapshot.
// The snapshot and its registries are only cleaned up when the ref count reaches zero.
std::pair<ResultValue, gostd::Error> Session::handleRelease(
    gostd::Context, const ReleaseParams* params) {
	if (params == nullptr || params->Snapshot == 0) {
		return {ResultValue{},
		        gostd::errorf("%w: empty handle", {ErrClientError})};
	}

	if (gostd::Error err = releaseSnapshot(params->Snapshot)) {
		return {ResultValue{}, err};
	}
	return marshalResult(true);
}

// handleGetDefaultProjectForFile returns the default project for a given file,
// or nil if no project currently contains the file.
// @gen-proto-nullable
std::pair<std::unique_ptr<ProjectResponse>, gostd::Error>
Session::handleGetDefaultProjectForFile(
    gostd::Context ctx, const GetDefaultProjectForFileParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}

	lsproto::DocumentUri uri = params->File.ToURI(GetCurrentDirectory());
	project::Project* proj = sd->snapshot->GetDefaultProject(uri);
	if (proj == nullptr) {
		return {nullptr, nullptr};
	}

	auto resp = NewProjectResponse(proj);
	return {std::unique_ptr<ProjectResponse>(
	            new ProjectResponse(std::move(*resp))),
	        nullptr};
}

// createSnapshotResponse — session.go:4612.
std::unique_ptr<CreateSnapshotResponse> Session::createSnapshotResponse(
    project::Snapshot* snapshot, project::Snapshot* base,
    const SnapshotRequestChangesParams* request) {
	auto operation = createSnapshotOperationResponse(snapshot, request);
	if (base == nullptr) {
		std::vector<project::Project*> projects =
		    snapshot->ProjectCollection->Projects();
		std::vector<std::shared_ptr<ProjectResponse>> projectResponses;
		projectResponses.reserve(projects.size());
		for (project::Project* proj : projects) {
			if (proj->CommandLine != nullptr) {
				projectResponses.push_back(NewProjectResponse(proj));
			}
		}
		auto response = std::make_unique<CreateSnapshotResponse>();
		response->Snapshot = snapshotHandle(snapshot);
		response->Projects = std::move(projectResponses);
		response->Operation = std::move(operation);
		return response;
	}

	std::vector<std::shared_ptr<ProjectResponse>> projectResponses;
	collections::diffOrderedMaps<project::ID, project::Project*>(
	    base->ProjectCollection->ProjectsByID(),
	    snapshot->ProjectCollection->ProjectsByID(),
	    [&](const project::ID&, project::Project* proj) {
		    if (proj->CommandLine != nullptr) {
			    projectResponses.push_back(NewProjectResponse(proj));
		    }
	    },
	    [](const project::ID&, project::Project*) {},
	    [&](const project::ID&, project::Project* oldProj,
	        project::Project* newProj) {
		    if (oldProj != newProj && newProj->CommandLine != nullptr) {
			    projectResponses.push_back(NewProjectResponse(newProj));
		    }
	    });
	auto response = std::make_unique<CreateSnapshotResponse>();
	response->Snapshot = snapshotHandle(snapshot);
	response->Projects = std::move(projectResponses);
	response->Changes = computeSnapshotChanges(base, snapshot);
	response->Operation = std::move(operation);
	return response;
}

// createSnapshotOperationResponse — session.go:4645.
std::unique_ptr<SnapshotOperationResponse>
Session::createSnapshotOperationResponse(
    project::Snapshot* snapshot, const SnapshotRequestChangesParams* request) {
	auto operation = std::make_unique<SnapshotOperationResponse>();
	if (request == nullptr) {
		return operation;
	}

	if (!request->CreatePrograms.empty()) {
		std::vector<project::Project*> createdPrograms =
		    snapshot->CreatedPrograms();
		if (createdPrograms.size() != request->CreatePrograms.size()) {
			throw std::runtime_error(
			    "created program result count does not match request");
		}
		std::vector<project::SyntheticProjectID> results(
		    createdPrograms.size());
		for (size_t i = 0; i < createdPrograms.size(); ++i) {
			auto [programID, ok] = createdPrograms[i]->ID().Synthetic();
			if (!ok) {
				throw std::runtime_error(
				    "created program has non-synthetic project ID");
			}
			results[i] = programID;
		}
		operation->CreatedPrograms = std::move(results);
	}

	if (!request->OpenFiles.empty()) {
		std::vector<std::shared_ptr<OpenedFileOperationResult>> results(
		    request->OpenFiles.size());
		for (size_t i = 0; i < request->OpenFiles.size(); ++i) {
			const DocumentIdentifier& file = request->OpenFiles[i];
			project::Project* project =
			    snapshot->GetDefaultProject(file.ToURI(GetCurrentDirectory()));
			if (project == nullptr) {
				throw std::runtime_error(
				    "no project found for opened file " +
				    file.ToAbsoluteFileName(GetCurrentDirectory()));
			}
			results[i] = std::make_shared<OpenedFileOperationResult>();
			results[i]->Project = project->ID();
		}
		operation->OpenedFiles = std::move(results);
	}
	return operation;
}

// Close closes the session and releases all active snapshots,
// regardless of their ref counts.
void Session::Close() {
	std::call_once(closeOnce, [this] {
		releaseLanguageServerRefs();
		releaseSourceFileLeases();

		std::vector<project::Snapshot*> snapshotsToRelease;
		{
			std::lock_guard lock(snapshotsMu);
			snapshotsToRelease.reserve(snapshots.size());
			for (auto& [_, sd] : snapshots) {
				snapshotsToRelease.push_back(sd->snapshot);
			}
			snapshots.clear();
		}
		for (project::Snapshot* snapshot : snapshotsToRelease) {
			snapshot->Deref();
		}

		if (ownsSnapshotHost) {
			snapshotHost->Close();
		}
		batchResponsePages.Clear();
	});
}

// releaseLanguageServerRefs — session.go:4702.
void Session::releaseLanguageServerRefs() {
	if (projectSession == nullptr) {
		return;
	}

	std::lock_guard lock(languageServerUpdateMu);
	if (openProjects.Size() == 0 && openFiles.Size() == 0 &&
	    createdPrograms.Size() == 0) {
		return;
	}

	project::APISnapshotRequest apiRequest;
	if (openProjects.Size() > 0) {
		auto cloned = openProjects.Clone();
		apiRequest.CloseProjects = new collections::Set<tspath::Path>(
		    std::move(cloned));
	}
	if (openFiles.Size() > 0) {
		auto cloned = openFiles.Clone();
		apiRequest.CloseFiles = new collections::Set<tspath::Path>(
		    std::move(cloned));
	}
	if (createdPrograms.Size() > 0) {
		apiRequest.RemovePrograms = collections::newSetWithSizeHint<
		    project::SyntheticProjectID>(createdPrograms.Size());
		for (const auto& programID : createdPrograms.Keys()) {
			apiRequest.RemovePrograms->Add(programID);
		}
	}
	auto [snapshot, err] =
	    projectSession->APIUpdate(withLocale(gostd::contextBackground()),
	                              project::FileChangeSummary{}, &apiRequest);
	if (err) {
		return;
	}
	snapshot->Deref();
	openProjects.Clear();
	openFiles.Clear();
	createdPrograms.Clear();
}

// toPath converts a file name to a normalized path.
tspath::Path Session::toPath(const std::string& fileName) {
	return tspath::toPath(fileName, GetCurrentDirectory(),
	                      useCaseSensitiveFileNames());
}

// toFileChangeSummary converts API file changes to a project.FileChangeSummary.
project::FileChangeSummary Session::toFileChangeSummary(
    const FileNotifications* changes) {
	if (changes == nullptr) {
		return project::FileChangeSummary{};
	}
	project::FileChangeSummary summary;
	if (changes->InvalidateAll) {
		summary.InvalidateAll = true;
		summary.IncludesWatchChangeOutsideNodeModules = true;
		return summary;
	}
	std::string cwd = GetCurrentDirectory();
	for (const auto& doc : changes->Changed) {
		summary.Changed.Add(doc.ToURI(cwd));
	}
	for (const auto& doc : changes->Created) {
		summary.Created.Add(doc.ToURI(cwd));
	}
	for (const auto& doc : changes->Deleted) {
		summary.Deleted.Add(doc.ToURI(cwd));
	}
	if (summary.Changed.Size() + summary.Created.Size() +
	        summary.Deleted.Size() > 0) {
		summary.IncludesWatchChangeOutsideNodeModules = true;
	}
	return summary;
}

// getDiagnostics — session.go:4779.
std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
Session::getDiagnostics(
    gostd::Context ctx, const GetDiagnosticsParams* params,
    const std::function<std::vector<Diagnostic*>(
        compiler::SimpleProgram*, gostd::Context, SourceFile*)>& getter) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<std::shared_ptr<DiagnosticResponse>>{}, err};
	}

	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {std::vector<std::shared_ptr<DiagnosticResponse>>{}, err};
	}

	if (!params->Files.empty()) {
		std::vector<Diagnostic*> allDiags;
		for (const DocumentIdentifier& file : params->Files) {
			auto [sourceFile, ferr] = resolveOptionalSourceFile(program, &file);
			if (ferr) {
				return {{}, ferr};
			}
			auto fileDiags = getter(program, ctx, sourceFile);
			allDiags.insert(allDiags.end(), fileDiags.begin(),
			                fileDiags.end());
		}
		return {NewDiagnosticResponses(std::move(allDiags)), nullptr};
	}

	return {NewDiagnosticResponses(getter(program, ctx, nullptr)), nullptr};
}

// @gen-proto-nullable
std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
Session::handleGetSyntacticDiagnostics(gostd::Context ctx,
                                       const GetDiagnosticsParams* params) {
	// ctx = core.WithCheckerLifetime(ctx, core.CheckerLifetimeDiagnostics)
	// — C++ SimpleProgram owns a single checker; the lifetime marker is a
	// no-op here.
	return getDiagnostics(
	    ctx, params,
	    [](compiler::SimpleProgram* p, gostd::Context, SourceFile* f) {
		    return p->GetSyntacticDiagnostics(f);
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
Session::handleGetBindDiagnostics(gostd::Context ctx,
                                  const GetDiagnosticsParams* params) {
	return getDiagnostics(
	    ctx, params,
	    [](compiler::SimpleProgram* p, gostd::Context, SourceFile* f) {
		    return p->GetBindDiagnostics(f);
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
Session::handleGetSemanticDiagnostics(gostd::Context ctx,
                                      const GetDiagnosticsParams* params) {
	return getDiagnostics(
	    ctx, params,
	    [](compiler::SimpleProgram* p, gostd::Context, SourceFile* f) {
		    return p->GetSemanticDiagnostics(f);
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
Session::handleGetSuggestionDiagnostics(gostd::Context ctx,
                                        const GetDiagnosticsParams* params) {
	return getDiagnostics(
	    ctx, params,
	    [](compiler::SimpleProgram* p, gostd::Context, SourceFile* f) {
		    return p->GetSuggestionDiagnostics(f);
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
Session::handleGetDeclarationDiagnostics(gostd::Context ctx,
                                         const GetDiagnosticsParams* params) {
	return getDiagnostics(
	    ctx, params,
	    [](compiler::SimpleProgram* p, gostd::Context, SourceFile* f) {
		    return p->GetDeclarationDiagnostics(f);
	    });
}

// handleGetConfigFileParsingDiagnostics returns config file parsing diagnostics.
// @gen-proto-nullable
std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
Session::handleGetConfigFileParsingDiagnostics(
    gostd::Context ctx, const GetProjectDiagnosticsParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<std::shared_ptr<DiagnosticResponse>>{}, err};
	}

	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {std::vector<std::shared_ptr<DiagnosticResponse>>{}, err};
	}

	return {NewDiagnosticResponses(
	            program->GetConfigFileParsingDiagnostics()),
	        nullptr};
}

// handleGetProgramDiagnostics returns program-wide diagnostics, including options diagnostics.
// @gen-proto-nullable
std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
Session::handleGetProgramDiagnostics(
    gostd::Context ctx, const GetProjectDiagnosticsParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<std::shared_ptr<DiagnosticResponse>>{}, err};
	}

	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {std::vector<std::shared_ptr<DiagnosticResponse>>{}, err};
	}

	return {NewDiagnosticResponses(program->GetProgramDiagnostics()), nullptr};
}

// handleGetGlobalDiagnostics returns global (non-file-specific) semantic diagnostics.
// @gen-proto-nullable
std::pair<std::vector<std::shared_ptr<DiagnosticResponse>>, gostd::Error>
Session::handleGetGlobalDiagnostics(
    gostd::Context ctx, const GetProjectDiagnosticsParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<std::shared_ptr<DiagnosticResponse>>{}, err};
	}

	project::Project* proj;
	std::tie(proj, err) = sd->getProject(params->Project);
	if (err) {
		return {std::vector<std::shared_ptr<DiagnosticResponse>>{}, err};
	}

	compiler::SimpleProgram* program = proj->GetProgram();
	if (program == nullptr) {
		return {std::vector<std::shared_ptr<DiagnosticResponse>>{}, gostd::errorf("%w: project has no program",
		                          {ErrClientError})};
	}

	// Global diagnostics are accumulated lazily by the project's checker pool as
	// files are checked. Force a full semantic pass so any global (non-file-specific)
	// diagnostics are produced; otherwise this would return an empty result for
	// projects using an external checker pool (the typical API case), since
	// compiler.Program.GetGlobalDiagnostics only reports for the internal pool.
	program->GetSemanticDiagnostics(nullptr);

	std::vector<Diagnostic*> diags =
	    tsc::Filter(proj->GetProjectDiagnostics(ctx),
	                [](Diagnostic* d) { return d->File() == nullptr; });
	return {NewDiagnosticResponses(std::move(diags)), nullptr};
}

// handleCreateBuildOrchestrator — session.go:1619.
std::pair<std::unique_ptr<CreateBuildOrchestratorResponse>, gostd::Error>
Session::handleCreateBuildOrchestrator(
    gostd::Context ctx, const CreateBuildOrchestratorParams* params) {
	apiBuildSystem* buildSys = getBuildSys(params);
	tsoptions::ParsedBuildCommandLine* command =
	    tsoptions::ParseBuildCommandLine(params->RootNames, buildSys);
	auto createdOrchestratorResponse =
	    std::make_unique<CreateBuildOrchestratorResponse>();
	if (params->CompilerOptions != nullptr) {
		// Copy: command outlives params; Go mutates the shared object.
		command->CompilerOptions =
		    new CompilerOptions(*params->CompilerOptions);
	}
	if (params->BuildOptions != nullptr) {
		command->BuildOptions = new BuildOptions(*params->BuildOptions);
	}
	std::unique_ptr<build::Orchestrator> orchestrator(
	    build::NewOrchestrator(build::Options{
	        .Sys = buildSys,
	        .Command = command,
	    }));
	createdOrchestratorResponse->BuildOrchestratorID =
	    NewBuildOrchestratorID();
	std::lock_guard lock(buildMu);
	buildOrchestrators[createdOrchestratorResponse->BuildOrchestratorID] =
	    std::move(orchestrator);
	return {std::move(createdOrchestratorResponse), nullptr};
}

// handleDisposeBuildOrchestrator — session.go:1642.
std::pair<ResultValue, gostd::Error> Session::handleDisposeBuildOrchestrator(
    gostd::Context ctx, const DisposeBuildOrchestratorParams* params) {
	std::lock_guard lock(buildMu);
	auto it = buildOrchestrators.find(params->BuildOrchestratorID);
	if (it == buildOrchestrators.end()) {
		return {ResultValue{},
		        gostd::newError(
		            "build orchestrator not found while disposing")};
	}
	buildOrchestrators.erase(it);
	return marshalResult(true);
}

// handleBuild — session.go:1652.
std::pair<std::unique_ptr<BuildResponse>, gostd::Error> Session::handleBuild(
    gostd::Context ctx, const BuildParams* params) {
	std::lock_guard lock(buildMu);
	auto it = buildOrchestrators.find(params->BuildOrchestratorID);
	if (it == buildOrchestrators.end()) {
		return {nullptr,
		        gostd::errorf("build orchestrator not found while building %s",
		                      {params->Project})};
	}
	std::unique_ptr<build::OrchestratorResult> result(
	    it->second->Build(ctx, params->Project));

	auto resp = std::make_unique<BuildResponse>();
	resp->Status = result->Result.Status;
	resp->Diagnostics = NewDiagnosticResponses(result->Errors);
	resp->Statistics = result->Statistics;
	return {std::move(resp), nullptr};
}

// handleBuildReferences — session.go:1667.
std::pair<std::unique_ptr<BuildResponse>, gostd::Error>
Session::handleBuildReferences(gostd::Context ctx, const BuildParams* params) {
	std::lock_guard lock(buildMu);
	auto it = buildOrchestrators.find(params->BuildOrchestratorID);
	if (it == buildOrchestrators.end()) {
		return {nullptr,
		        gostd::errorf("build orchestrator not found for building references for %s",
		                      {params->Project})};
	}
	std::unique_ptr<build::OrchestratorResult> result(
	    it->second->BuildReferences(ctx, params->Project));

	auto resp = std::make_unique<BuildResponse>();
	resp->Status = result->Result.Status;
	resp->Diagnostics = NewDiagnosticResponses(result->Errors);
	resp->Statistics = result->Statistics;
	return {std::move(resp), nullptr};
}

// handleCleanBuild — session.go:1682.
std::pair<std::unique_ptr<CleanBuildResponse>, gostd::Error>
Session::handleCleanBuild(gostd::Context ctx, const CleanBuildParams* params) {
	std::lock_guard lock(buildMu);
	auto it = buildOrchestrators.find(params->BuildOrchestratorID);
	if (it == buildOrchestrators.end()) {
		return {nullptr,
		        gostd::errorf("build orchestrator not found while cleaning %s",
		                      {params->Project})};
	}
	std::unique_ptr<build::OrchestratorResult> result(
	    it->second->Clean(params->Project));
	auto resp = std::make_unique<CleanBuildResponse>();
	resp->Status = result->Result.Status;
	resp->Diagnostics = NewDiagnosticResponses(result->Errors);
	resp->Statistics = result->Statistics;
	resp->FilesDeleted = result->FilesToDelete;
	return {std::move(resp), nullptr};
}

// handleCleanReferences — session.go:1697.
std::pair<std::unique_ptr<CleanBuildResponse>, gostd::Error>
Session::handleCleanReferences(gostd::Context ctx,
                               const CleanBuildParams* params) {
	std::lock_guard lock(buildMu);
	auto it = buildOrchestrators.find(params->BuildOrchestratorID);
	if (it == buildOrchestrators.end()) {
		return {nullptr,
		        gostd::errorf("build orchestrator not found while cleaning references for %s",
		                      {params->Project})};
	}
	std::unique_ptr<build::OrchestratorResult> result(
	    it->second->CleanReferences(params->Project));
	auto resp = std::make_unique<CleanBuildResponse>();
	resp->Status = result->Result.Status;
	resp->Diagnostics = NewDiagnosticResponses(result->Errors);
	resp->Statistics = result->Statistics;
	resp->FilesDeleted = result->FilesToDelete;
	return {std::move(resp), nullptr};
}

// getBuildSys — session.go:1712.
apiBuildSystem* Session::getBuildSys(
    const CreateBuildOrchestratorParams* params) {
	std::string currentDirectory = params->Cwd;
	if (currentDirectory.empty()) {
		currentDirectory = GetCurrentDirectory();
	}
	return new apiBuildSystem{this, std::move(currentDirectory),
	                          std::chrono::system_clock::now()};
}

// --- apiBuildSystem — session.go:1728 (io.Discard writer; delegates FS to
// the session's snapshot host). ---

// discardStream — io.Discard equivalent for std::ostream.
class discardStream : public std::ostream {
	struct nullbuf : std::streambuf {
		int overflow(int c) override { return c; }
	};
	nullbuf buf;

public:
	discardStream() : std::ostream(&buf) {}
};

static std::ostream* discardOstream() {
	static discardStream s;
	return &s;
}

std::ostream* apiBuildSystem::Writer() { return discardOstream(); }
std::ostream* apiBuildSystem::ErrorWriter() { return discardOstream(); }
std::shared_ptr<vfs::FS> apiBuildSystem::fs() {
	return session->snapshotHost->FS();
}
std::string apiBuildSystem::DefaultLibraryPath() {
	return session->DefaultLibraryPath();
}
std::string apiBuildSystem::GetCurrentDirectory() { return currentDirectory; }
bool apiBuildSystem::WriteOutputIsTTY() { return false; }
int apiBuildSystem::GetWidthOfTerminal() { return 0; }
std::pair<std::string, bool> apiBuildSystem::GetEnvironmentVariable(
    std::string_view) {
	return {"", false};
}
std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
apiBuildSystem::Spawn(const std::vector<std::string>&, const std::string&,
                      gostd::io::Writer*) {
	return {nullptr,
	        gostd::newError(
	            "spawning processes is not supported by the API build orchestrator")};
}
vfs::TimePoint apiBuildSystem::Now() {
	return std::chrono::system_clock::now();
}
gostd::Duration apiBuildSystem::SinceStart() {
	return std::chrono::duration_cast<gostd::Duration>(
	    std::chrono::system_clock::now() - start);
}

// marshalCompilerOptionsValue — defined below handleReadConfigFile.
static std::pair<std::string, std::string> marshalCompilerOptionsValue(
    const tsoptions::CompilerOptionsValue& v);

// handleParseCommandLine parses command-line arguments.
std::pair<std::unique_ptr<ConfigFileResponse>, gostd::Error>
Session::handleParseCommandLine(gostd::Context ctx,
                                const ParseCommandLineParams* params) {
	snapshotHostParseConfigHost host{snapshotHost};
	return {std::unique_ptr<ConfigFileResponse>(new ConfigFileResponse(
	            std::move(*NewConfigFileResponse(
	                tsoptions::ParseCommandLine(params->CommandLine,
	                                            &host))))),
	        nullptr};
}

// handleReadConfigFile reads and parses a JSON configuration file.
std::pair<std::unique_ptr<ReadConfigFileResponse>, gostd::Error>
Session::handleReadConfigFile(gostd::Context ctx,
                              const ReadConfigFileParams* params) {
	std::string configFileName =
	    params->File.ToAbsoluteFileName(GetCurrentDirectory());
	auto [configFileContent, ok] =
	    snapshotHost->FS()->ReadFile(configFileName);
	if (!ok) {
		auto resp = std::make_unique<ReadConfigFileResponse>();
		resp->Config = json::Value("{}");
		resp->Error = NewDiagnosticResponse(tsoptions::newCompilerDiagnostic(
		    Cannot_read_file_0, {configFileName}));
		return {std::move(resp), nullptr};
	}

	auto [config, parseErrors] = tsoptions::ParseConfigFileTextToJson(
	    configFileName, toPath(configFileName), configFileContent);
	auto response = std::make_unique<ReadConfigFileResponse>();
	auto [marshaled, merr] = marshalCompilerOptionsValue(config);
	if (!merr.empty()) {
		return {nullptr, gostd::newError(merr)};
	}
	response->Config = json::Value(std::move(marshaled));
	if (!parseErrors.empty()) {
		response->Error = NewDiagnosticResponse(parseErrors[0]);
	}
	return {std::move(response), nullptr};
}

// marshalCompilerOptionsValue — writes a CompilerOptionsValue (Go `any`) as
// JSON. encoding/json marshals Go's diagnostics.Message (all unexported
// fields) as {}.
static void marshalCompilerOptionsValueTo(
    const tsoptions::CompilerOptionsValue& v, std::string& out) {
	using namespace tsoptions;
	const auto& vv = v.v;
	if (std::holds_alternative<std::monostate>(vv)) {
		out += "null";
	} else if (const bool* p = std::get_if<bool>(&vv)) {
		out += json::marshalBool(*p);
	} else if (const int64_t* p = std::get_if<int64_t>(&vv)) {
		out += std::to_string(*p);
	} else if (const double* p = std::get_if<double>(&vv)) {
		out += json::detail::goFloat(*p);
	} else if (const Tristate* p = std::get_if<Tristate>(&vv)) {
		out += *p == Tristate::True    ? "true"
		       : *p == Tristate::False ? "false"
		                               : "null";
	} else if (const std::string* p = std::get_if<std::string>(&vv)) {
		out += json::marshalString(*p);
	} else if (std::get_if<const DiagnosticMessage*>(&vv)) {
		out += "{}";  // encoding/json: no exported fields
	} else if (const JsonStrList* p = std::get_if<JsonStrList>(&vv)) {
		out += "[";
		for (size_t i = 0; i < p->size(); ++i) {
			if (i) out += ",";
			out += json::marshalString((*p)[i]);
		}
		out += "]";
	} else if (const JsonArray* p = std::get_if<JsonArray>(&vv)) {
		out += "[";
		for (size_t i = 0; i < p->size(); ++i) {
			if (i) out += ",";
			marshalCompilerOptionsValueTo((*p)[i], out);
		}
		out += "]";
	} else if (const JsonObjectPtr* p = std::get_if<JsonObjectPtr>(&vv)) {
		out += "{";
		bool first = true;
		for (const auto& key : (*p)->Keys()) {
			if (!first) out += ",";
			first = false;
			out += json::marshalString(key);
			out += ":";
			marshalCompilerOptionsValueTo(*(*p)->Get(key).first, out);
		}
		out += "}";
	} else if (const JsonGoMapPtr* p = std::get_if<JsonGoMapPtr>(&vv)) {
		// map[string]any — Go's encoding/json sorts map keys.
		std::vector<std::string> keys;
		keys.reserve((*p)->size());
		for (const auto& [k, _] : **p) keys.push_back(k);
		std::sort(keys.begin(), keys.end());
		out += "{";
		bool first = true;
		for (const auto& key : keys) {
			if (!first) out += ",";
			first = false;
			out += json::marshalString(key);
			out += ":";
			marshalCompilerOptionsValueTo((*p)->at(key), out);
		}
		out += "}";
	}
}

static std::pair<std::string, std::string> marshalCompilerOptionsValue(
    const tsoptions::CompilerOptionsValue& v) {
	std::string out;
	marshalCompilerOptionsValueTo(v, out);
	return {std::move(out), {}};
}

// handleParseJsonConfigFileContent parses an in-memory JSON configuration.
std::pair<std::unique_ptr<ConfigFileResponse>, gostd::Error>
Session::handleParseJsonConfigFileContent(
    gostd::Context ctx, const ParseJsonConfigFileContentParams* params) {
	if ((!params->ConfigDirectory.has_value()) ==
	    (params->ConfigFileName == nullptr)) {
		return {nullptr,
		        gostd::errorf("%w: exactly one of configDirectory or configFileName is required",
		                      {ErrClientError})};
	}

	std::string basePath;
	std::string configFileName;
	if (params->ConfigDirectory.has_value()) {
		basePath = tspath::getNormalizedAbsolutePath(*params->ConfigDirectory,
		                                           GetCurrentDirectory());
	} else {
		configFileName =
		    params->ConfigFileName->ToAbsoluteFileName(GetCurrentDirectory());
		basePath = tspath::getDirectoryPath(configFileName);
	}

	snapshotHostParseConfigHost host{snapshotHost};
	tsoptions::ParsedCommandLine* parsedCommandLine =
	    tsoptions::ParseJsonConfigFileContent(
	        jsonValueToAny(params->JSON), &host, basePath,
	        nullptr, /*existingOptions*/
	        configFileName,
	        {}, /*resolutionStack*/
	        nullptr /*extendedConfigCache*/);
	return {std::unique_ptr<ConfigFileResponse>(new ConfigFileResponse(
	            std::move(*NewConfigFileResponse(parsedCommandLine)))),
	        nullptr};
}

// handleParseConfigFile parses a tsconfig.json file and returns its contents.
std::pair<std::unique_ptr<ConfigFileResponse>, gostd::Error>
Session::handleParseConfigFile(gostd::Context ctx,
                               const ParseConfigFileParams* params) {
	std::string configFileName =
	    params->File.ToAbsoluteFileName(GetCurrentDirectory());
	auto [configFileContent, ok] =
	    snapshotHost->FS()->ReadFile(configFileName);
	if (!ok) {
		return {nullptr,
		        gostd::errorf("%w: could not read file %q",
		                      {ErrClientError, configFileName})};
	}

	std::string configDir = tspath::getDirectoryPath(configFileName);
	tsoptions::TsConfigSourceFile* tsConfigSourceFile =
	    tsoptions::NewTsconfigSourceFileFromFilePath(
	        configFileName, toPath(configFileName), configFileContent);
	snapshotHostParseConfigHost host{snapshotHost};
	tsoptions::ParsedCommandLine* parsedCommandLine =
	    tsoptions::ParseJsonSourceFileConfigFileContent(
	        tsConfigSourceFile, &host, configDir,
	        nullptr, /*existingOptions*/
	        tsoptions::JsonObjectPtr{}, /*existingOptionsRaw*/
	        configFileName,
	        {}, /*resolutionStack*/
	        nullptr /*extendedConfigCache*/);
	return {std::unique_ptr<ConfigFileResponse>(new ConfigFileResponse(
	            std::move(*NewConfigFileResponse(parsedCommandLine)))),
	        nullptr};
}

// handleTranspile — session.go:1828.
std::pair<std::unique_ptr<TranspileOutputResponse>, gostd::Error>
Session::handleTranspile(gostd::Context ctx, const TranspileParams* params,
                         bool declaration) {
	return transpileOutput(ctx, params->Input, params->Options, declaration);
}

// @gen-proto-result: SourceFileResponse
std::pair<ResultValue, gostd::Error> Session::handleCreateSourceFile(
    gostd::Context ctx, const CreateSourceFileParams* params) {
	auto [lease, err] =
	    createSourceFile(params->FileName, params->SourceText, params->Options);
	if (err) {
		return {ResultValue{}, err};
	}
	return encodeLeasedSourceFile(std::move(lease));
}

// @gen-proto-result: SourceFileResponse
std::pair<ResultValue, gostd::Error> Session::handleCreateSourceFileFromFile(
    gostd::Context ctx, const CreateSourceFileFromFileParams* params) {
	std::string fileName = tspath::getNormalizedAbsolutePath(
	    params->FileName, GetCurrentDirectory());
	auto [sourceText, ok] = snapshotHost->FS()->ReadFile(fileName);
	if (!ok) {
		return {ResultValue{},
		        gostd::errorf("%w: could not read file %q",
		                      {ErrClientError, fileName})};
	}
	auto [lease, err] =
	    createSourceFile(fileName, sourceText, params->Options);
	if (err) {
		return {ResultValue{}, err};
	}
	return encodeLeasedSourceFile(std::move(lease));
}

// createSourceFile — session.go:1857.
std::pair<std::shared_ptr<project::SourceFileLease>, gostd::Error>
Session::createSourceFile(const std::string& fileName,
                          const std::string& sourceText,
                          const CreateSourceFileOptions& options) {
	ScriptKind scriptKind = options.ScriptKind;
	if (scriptKind == ScriptKind::Unknown) {
		scriptKind = ensureScriptKindFromFileName(fileName);
	}
	if (!isValidCreateSourceFileScriptKind(scriptKind)) {
		return {nullptr,
		        gostd::errorf("%w: invalid scriptKind %d",
		                      {ErrClientError, int64_t(scriptKind)})};
	}
	std::string normalized =
	    tspath::getNormalizedAbsolutePath(fileName, GetCurrentDirectory());
	SourceFileParseOptions parseOptions;
	parseOptions.FileName = normalized;
	parseOptions.Path = toPath(normalized);
	return {acquireSourceFile(std::move(parseOptions), sourceText, scriptKind),
	        nullptr};
}

std::shared_ptr<project::SourceFileLease> Session::acquireSourceFile(
    SourceFileParseOptions options, const std::string& sourceText,
    ScriptKind scriptKind) {
	return snapshotHost->AcquireSourceFile(std::move(options), sourceText,
	                                       int(scriptKind));
}

// encodeLeasedSourceFile — session.go:1877.
std::pair<ResultValue, gostd::Error> Session::encodeLeasedSourceFile(
    std::shared_ptr<project::SourceFileLease> lease) {
	auto [data, indexTable, err] =
	    encoder::EncodeSourceFile(lease->SourceFile());
	if (err) {
		lease->Release();
		return {ResultValue{},
		        gostd::errorf("failed to encode source file: %w", {err})};
	}
	SourceFileLeaseID id = ++nextSourceFileLeaseID;
	encoder::SetSourceFileLease(data, uint64_t(id));
	{
		std::lock_guard lock(sourceFileLeasesMu);
		sourceFileLeases[id] = lease;
	}
	if (useBinaryResponses) {
		return {ResultValue{true, std::string(reinterpret_cast<const char*>(
		                              data.data()),
		                              data.size())},
		        nullptr};
	}
	SourceFileResponse resp;
	resp.Data = base64Encode(data);
	auto [marshaled, merr] = json::marshal(resp);
	if (!merr.empty()) {
		return {ResultValue{}, gostd::newError(merr)};
	}
	return {ResultValue{false, std::move(marshaled)}, nullptr};
}

// handleReleaseSourceFile — session.go:1897.
std::pair<ResultValue, gostd::Error> Session::handleReleaseSourceFile(
    const ReleaseSourceFileParams* params) {
	if (params == nullptr || params->Lease == 0) {
		return {ResultValue{},
		        gostd::errorf("%w: empty source file lease", {ErrClientError})};
	}
	std::shared_ptr<project::SourceFileLease> lease;
	{
		std::lock_guard lock(sourceFileLeasesMu);
		auto it = sourceFileLeases.find(params->Lease);
		if (it != sourceFileLeases.end()) {
			lease = it->second;
			sourceFileLeases.erase(it);
		}
	}
	if (lease == nullptr) {
		return {ResultValue{},
		        gostd::errorf("%w: source file lease %d not found",
		                      {ErrClientError, uint64_t(params->Lease)})};
	}
	lease->Release();
	return marshalResult(true);
}

// releaseSourceFileLeases — session.go:1914.
void Session::releaseSourceFileLeases() {
	std::vector<std::shared_ptr<project::SourceFileLease>> leases;
	{
		std::lock_guard lock(sourceFileLeasesMu);
		leases.reserve(sourceFileLeases.size());
		for (auto& [_, lease] : sourceFileLeases) {
			leases.push_back(lease);
		}
		sourceFileLeases.clear();
	}
	for (auto& lease : leases) {
		lease->Release();
	}
}

// isValidCreateSourceFileScriptKind — session.go:1927.
bool isValidCreateSourceFileScriptKind(ScriptKind scriptKind) {
	switch (scriptKind) {
	case ScriptKind::JS:
	case ScriptKind::JSX:
	case ScriptKind::TS:
	case ScriptKind::TSX:
	case ScriptKind::JSON:
		return true;
	default:
		return false;
	}
}

// handleTranspileFromFile — session.go:1935.
std::pair<std::unique_ptr<TranspileOutputResponse>, gostd::Error>
Session::handleTranspileFromFile(gostd::Context ctx,
                                 const TranspileFromFileParams* params,
                                 bool declaration) {
	std::string fileName = tspath::getNormalizedAbsolutePath(
	    params->FileName, GetCurrentDirectory());
	auto [input, ok] = snapshotHost->FS()->ReadFile(fileName);
	if (!ok) {
		return {nullptr,
		        gostd::errorf("%w: could not read file %q",
		                      {ErrClientError, fileName})};
	}
	TranspileOptions options = params->Options;
	options.FileName = fileName;
	return transpileOutput(ctx, input, options, declaration);
}

// transpileOutput — session.go:1944.
std::pair<std::unique_ptr<TranspileOutputResponse>, gostd::Error>
transpileOutput(gostd::Context ctx, const std::string& input,
                const TranspileOptions& options, bool declaration) {
	transpile::Options transpileOptions;
	transpileOptions.CompilerOptions = options.CompilerOptions.get();
	transpileOptions.FileName = options.FileName;
	transpileOptions.ReportDiagnostics = options.ReportDiagnostics;
	// Go takes a context.Context here; the C++ transpile API drops it.
	std::unique_ptr<transpile::Output> output(
	    declaration ? transpile::TranspileDeclaration(input, transpileOptions)
	                : transpile::TranspileModule(input, transpileOptions));
	if (output == nullptr) {
		if (gostd::Error cerr = gostd::ctxErr(ctx)) {
			return {nullptr, cerr};
		}
		return {nullptr,
		        gostd::newError("transpilation produced no output")};
	}
	auto resp = std::make_unique<TranspileOutputResponse>();
	resp->OutputText = output->OutputText;
	resp->Diagnostics = NewDiagnosticResponses(output->Diagnostics);
	resp->SourceMapText = output->SourceMapText;
	return {std::move(resp), nullptr};
}

// handleGetSourceFile returns a source file from a project within a snapshot.
// @gen-proto-result: SourceFileResponse
// @gen-proto-nullable
std::pair<ResultValue, gostd::Error> Session::handleGetSourceFile(
    gostd::Context ctx, const GetSourceFileParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {ResultValue{}, err};
	}

	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {ResultValue{}, err};
	}

	return encodeSourceFileResponse(
	    program->GetSourceFile(params->File.ToFileName()));
}

// handleGetConfigFileNames returns tsconfig file names associated with the project's command line.
// @gen-proto-nullable
std::pair<std::vector<std::string>, gostd::Error>
Session::handleGetConfigFileNames(
    gostd::Context ctx, const GetProjectDiagnosticsParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<std::string>{}, err};
	}

	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {std::vector<std::string>{}, err};
	}

	tsoptions::ParsedCommandLine* commandLine = program->CommandLine();
	if (commandLine == nullptr || commandLine->ConfigFile == nullptr ||
	    commandLine->ConfigFile->SourceFile == nullptr) {
		return {{}, nullptr};
	}

	std::vector<std::string> extendedFiles =
	    commandLine->ExtendedSourceFiles();
	std::vector<std::string> configFiles;
	configFiles.reserve(extendedFiles.size() + 1);
	configFiles.push_back(commandLine->ConfigFile->SourceFile->FileName());
	configFiles.insert(configFiles.end(), extendedFiles.begin(),
	                   extendedFiles.end());
	return {std::move(configFiles), nullptr};
}

// handleGetConfigSourceFile returns a tsconfig source file associated with the project's command line.
// @gen-proto-result: SourceFileResponse
// @gen-proto-nullable
std::pair<ResultValue, gostd::Error> Session::handleGetConfigSourceFile(
    gostd::Context ctx, const GetSourceFileParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {ResultValue{}, err};
	}

	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {ResultValue{}, err};
	}

	tsoptions::ParsedCommandLine* commandLine = program->CommandLine();
	if (commandLine == nullptr || commandLine->ConfigFile == nullptr ||
	    commandLine->ConfigFile->SourceFile == nullptr) {
		return encodeSourceFileResponse(nullptr);
	}

	tspath::Path requestedPath = tspath::toPath(
	    params->File.ToFileName(), program->GetCurrentDirectory(),
	    program->UseCaseSensitiveFileNames());
	SourceFile* rootConfigSourceFile = commandLine->ConfigFile->SourceFile;
	if (rootConfigSourceFile->Path() == requestedPath) {
		return encodeSourceFileResponse(rootConfigSourceFile);
	}

	for (const std::string& configFileName :
	     commandLine->ExtendedSourceFiles()) {
		if (tspath::toPath(configFileName, program->GetCurrentDirectory(),
		                   program->UseCaseSensitiveFileNames()) !=
		    requestedPath) {
			continue;
		}

		auto [configFileContent, ok] =
		    sd->snapshot->ReadFile(configFileName);
		if (!ok) {
			return encodeSourceFileResponse(nullptr);
		}

		tsoptions::TsConfigSourceFile* configSourceFile =
		    tsoptions::NewTsconfigSourceFileFromFilePath(
		        configFileName, requestedPath, configFileContent);
		return encodeSourceFileResponse(configSourceFile->SourceFile);
	}

	return encodeSourceFileResponse(nullptr);
}

// encodeSourceFileResponse — session.go:2024.
std::pair<ResultValue, gostd::Error> Session::encodeSourceFileResponse(
    SourceFile* sourceFile) {
	if (sourceFile == nullptr) {
		if (useBinaryResponses) {
			return {ResultValue{true, ""}, nullptr};
		}
		return {ResultValue{}, nullptr};
	}

	// Encode the full source file.
	auto [data, indexTable, err] = encoder::EncodeSourceFile(sourceFile);
	if (err) {
		return {ResultValue{},
		        gostd::errorf("failed to encode source file: %w", {err})};
	}

	if (useBinaryResponses) {
		return {ResultValue{true, std::string(reinterpret_cast<const char*>(
		                              data.data()),
		                              data.size())},
		        nullptr};
	}
	SourceFileResponse sfr;
	sfr.Data = base64Encode(data);
	auto [marshaled, merr] = json::marshal(sfr);
	if (!merr.empty()) {
		return {ResultValue{}, gostd::newError(merr)};
	}
	return {ResultValue{false, std::move(marshaled)}, nullptr};
}

// handleGetSourceFileNames returns file names of all source files in a project.
std::pair<std::vector<std::string>, gostd::Error>
Session::handleGetSourceFileNames(gostd::Context ctx,
                                  const GetSourceFileNamesParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<std::string>{}, err};
	}

	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {std::vector<std::string>{}, err};
	}

	std::vector<SourceFile*> sourceFiles = program->GetSourceFiles();
	std::vector<std::string> result(sourceFiles.size());
	for (size_t i = 0; i < sourceFiles.size(); ++i) {
		result[i] = sourceFiles[i]->FileName();
	}
	return {std::move(result), nullptr};
}

// handleGetSourceFileMetadata returns program-stored metadata for a single source file.
// The client fetches this lazily per file and caches it.
// @gen-proto-nullable
std::pair<std::unique_ptr<SourceFileMetadata>, gostd::Error>
Session::handleGetSourceFileMetadata(gostd::Context ctx,
                                    const GetSourceFileParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}

	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {nullptr, err};
	}

	SourceFile* sourceFile =
	    program->GetSourceFile(params->File.ToFileName());
	if (sourceFile == nullptr) {
		return {nullptr, nullptr};
	}

	const SourceFileMetaData& metaData =
	    program->GetSourceFileMetaData(sourceFile->Path());
	auto resp = std::make_unique<SourceFileMetadata>();
	resp->IsDefaultLibrary =
	    program->IsSourceFileDefaultLibrary(sourceFile->Path());
	resp->IsFromExternalLibrary =
	    program->IsSourceFileFromExternalLibrary(sourceFile);
	resp->PackageJsonType = metaData.PackageJsonType;
	resp->PackageJsonDirectory = metaData.PackageJsonDirectory;
	resp->ImpliedNodeFormat = metaData.ImpliedNodeFormat;
	return {std::move(resp), nullptr};
}

// newResolvedModuleResponse — session.go:2095.
std::unique_ptr<ResolvedModule> newResolvedModuleResponse(
    module::ResolvedModule* resolution) {
	if (resolution == nullptr || !resolution->IsResolved()) {
		return nullptr;
	}
	auto resp = std::make_unique<ResolvedModule>();
	resp->ResolvedFileName = resolution->ResolvedFileName;
	resp->OriginalPath = resolution->OriginalPath;
	resp->Extension = resolution->Extension;
	resp->ResolvedUsingTsExtension = resolution->ResolvedUsingTsExtension;
	resp->ResolvedUsingExtraExtensions =
	    resolution->ResolvedUsingExtraExtensions;
	resp->PackageId = NewPackageId(resolution->PackageId);
	resp->IsExternalLibraryImport = resolution->IsExternalLibraryImport;
	resp->AlternateResult = resolution->AlternateResult;
	return resp;
}

// newResolvedTypeReferenceDirectiveResponse — session.go:2110.
std::unique_ptr<ResolvedTypeReferenceDirective>
newResolvedTypeReferenceDirectiveResponse(
    module::ResolvedTypeReferenceDirective* resolution) {
	if (resolution == nullptr || !resolution->IsResolved()) {
		return nullptr;
	}
	auto resp = std::make_unique<ResolvedTypeReferenceDirective>();
	resp->Primary = resolution->Primary;
	resp->ResolvedFileName = resolution->ResolvedFileName;
	resp->OriginalPath = resolution->OriginalPath;
	resp->PackageId = NewPackageId(resolution->PackageId);
	resp->IsExternalLibraryImport = resolution->IsExternalLibraryImport;
	return resp;
}

// handleGetModeForUsageLocation — session.go:2151.
std::pair<ResolutionMode, gostd::Error> Session::handleGetModeForUsageLocation(
    gostd::Context ctx, const GetModeForUsageLocationParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {ResolutionModeNone, err};
	}
	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {ResolutionModeNone, err};
	}
	SourceFile* sourceFile;
	std::tie(sourceFile, err) =
	    resolveOptionalSourceFile(program, &params->File);
	if (err) {
		return {ResolutionModeNone, err};
	}
	Node* usage;
	std::tie(usage, err) =
	    sd->resolveNodeHandle(program, params->Usage);
	if (err) {
		return {ResolutionModeNone, err};
	}
	if (!isStringLiteralLike(usage)) {
		return {ResolutionModeNone,
		        gostd::errorf("%w: usage must be a StringLiteralLike node",
		                      {ErrClientError})};
	}
	return {program->GetModeForUsageLocation(sourceFile, usage), nullptr};
}

// handleGetModeForResolutionAtIndex — session.go:2172.
std::pair<ResolutionMode, gostd::Error>
Session::handleGetModeForResolutionAtIndex(
    gostd::Context ctx, const GetModeForResolutionAtIndexParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {ResolutionModeNone, err};
	}
	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {ResolutionModeNone, err};
	}
	SourceFile* sourceFile;
	std::tie(sourceFile, err) =
	    resolveOptionalSourceFile(program, &params->File);
	if (err) {
		return {ResolutionModeNone, err};
	}
	// SourceFile has an `imports` FIELD that shadows Node::imports().
	int resolutionCount =
	    int(static_cast<Node*>(sourceFile)->imports().size());
	for (Node* augmentation : sourceFile->ModuleAugmentations) {
		if (augmentation->kind == Kind::StringLiteral) {
			resolutionCount++;
		}
	}
	if (params->Index < 0 || params->Index >= resolutionCount) {
		return {ResolutionModeNone,
		        gostd::errorf("%w: invalid resolution index",
		                      {ErrClientError})};
	}
	return {program->GetModeForResolutionAtIndex(sourceFile, params->Index),
	        nullptr};
}

// @gen-proto-nullable
std::pair<std::unique_ptr<ResolvedModule>, gostd::Error>
Session::handleGetResolvedModule(gostd::Context ctx,
                                 const GetResolvedModuleParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}
	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {nullptr, err};
	}
	SourceFile* sourceFile;
	std::tie(sourceFile, err) =
	    resolveOptionalSourceFile(program, &params->File);
	if (err) {
		return {nullptr, err};
	}
	if (sourceFile == nullptr) {
		// Go: program.GetResolvedModule(nil, ...) panics on nil.Path().
		throw std::runtime_error(
		    "runtime error: invalid memory address or nil pointer dereference");
	}
	return {newResolvedModuleResponse(program->getResolvedModuleByPath(
	            sourceFile->Path(), params->ModuleName, params->Mode)),
	        nullptr};
}

// @gen-proto-nullable
std::pair<std::unique_ptr<ResolvedModule>, gostd::Error>
Session::handleGetResolvedModuleFromModuleSpecifier(
    gostd::Context ctx,
    const GetResolvedModuleFromModuleSpecifierParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}
	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {nullptr, err};
	}
	Node* node;
	std::tie(node, err) =
	    sd->resolveNodeHandle(program, params->ModuleSpecifier);
	if (err) {
		return {nullptr, err};
	}
	if (!isStringLiteralLike(node)) {
		return {nullptr,
		        gostd::errorf("%w: moduleSpecifier must be a StringLiteralLike node",
		                      {ErrClientError})};
	}
	SourceFile* sourceFile = getSourceFileOfNode(node);
	if (params->SourceFile != nullptr) {
		std::tie(sourceFile, err) =
		    resolveOptionalSourceFile(program, params->SourceFile.get());
		if (err) {
			return {nullptr, err};
		}
	}
	if (sourceFile == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: moduleSpecifier must have a SourceFile ancestor or sourceFile must be provided",
		                      {ErrClientError})};
	}
	ResolutionMode mode =
	    program->GetModeForUsageLocation(sourceFile, node);
	return {newResolvedModuleResponse(program->getResolvedModuleByPath(
	            sourceFile->Path(), node->text(), mode)),
	        nullptr};
}

// @gen-proto-nullable
std::pair<std::unique_ptr<ResolvedTypeReferenceDirective>, gostd::Error>
Session::handleGetResolvedTypeReferenceDirective(
    gostd::Context ctx,
    const GetResolvedTypeReferenceDirectiveParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}
	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {nullptr, err};
	}
	SourceFile* sourceFile;
	std::tie(sourceFile, err) =
	    resolveOptionalSourceFile(program, &params->File);
	if (err) {
		return {nullptr, err};
	}
	if (sourceFile == nullptr) {
		throw std::runtime_error(
		    "runtime error: invalid memory address or nil pointer dereference");
	}
	return {newResolvedTypeReferenceDirectiveResponse(
	            program->GetResolvedTypeReferenceDirective(
	                sourceFile, params->TypeDirectiveName, params->Mode)),
	        nullptr};
}

// @gen-proto-nullable
std::pair<std::unique_ptr<ResolvedTypeReferenceDirective>, gostd::Error>
Session::handleGetResolvedTypeReferenceDirectiveFromReference(
    gostd::Context ctx,
    const GetResolvedTypeReferenceDirectiveFromReferenceParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}
	compiler::SimpleProgram* program;
	std::tie(program, err) = sd->getProgram(params->Project);
	if (err) {
		return {nullptr, err};
	}
	SourceFile* sourceFile;
	std::tie(sourceFile, err) =
	    resolveOptionalSourceFile(program, &params->SourceFile);
	if (err) {
		return {nullptr, err};
	}
	ResolutionMode mode = params->ResolutionMode;
	if (mode == ResolutionModeNone) {
		mode = program->GetDefaultResolutionModeForFile(sourceFile);
	}
	return {newResolvedTypeReferenceDirectiveResponse(
	            program->GetResolvedTypeReferenceDirective(
	                sourceFile, params->TypeDirectiveName, mode)),
	        nullptr};
}

// resolveOptionalSourceFile — session.go:4903.
std::pair<SourceFile*, gostd::Error> Session::resolveOptionalSourceFile(
    compiler::SimpleProgram* program, const DocumentIdentifier* file) {
	if (file == nullptr) {
		return {nullptr, nullptr};
	}
	SourceFile* sourceFile = program->GetSourceFile(file->ToFileName());
	if (sourceFile == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: source file not found: %v",
		                      {ErrClientError, file->String()})};
	}
	return {sourceFile, nullptr};
}

// handleGetSymbolAtPosition returns the symbol at a position in a file.
// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetSymbolAtPosition(gostd::Context ctx,
                                   const GetSymbolAtPositionParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto* sourceFile = setup.program->GetSourceFile(params->File.ToFileName());
	if (sourceFile == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: source file not found: %v",
		                      {ErrClientError, params->File.ToFileName()})};
	}

	auto* positionMap = sourceFile->GetPositionMap();
	auto* node = astnav::getTouchingPropertyName(
	    sourceFile, positionMap->UTF16ToUTF8(int(params->Position)));
	if (node == nullptr) {
		return {nullptr, nullptr};
	}

	auto* symbol = setup.checker->GetSymbolAtLocation(node);
	if (symbol == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.newSymbolResponse(symbol), nullptr};
}

// handleGetSymbolsAtPositions returns the symbols at multiple positions in a file.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::handleGetSymbolsAtPositions(
    gostd::Context ctx, const GetSymbolsAtPositionsParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto* sourceFile = setup.program->GetSourceFile(params->File.ToFileName());
	if (sourceFile == nullptr) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{},
		        gostd::errorf("%w: source file not found: %v",
		                      {ErrClientError, params->File.ToFileName()})};
	}

	auto* positionMap = sourceFile->GetPositionMap();
	std::vector<std::unique_ptr<SymbolResponse>> results(params->Positions.size());
	for (size_t i = 0; i < params->Positions.size(); i++) {
		auto* node = astnav::getTouchingPropertyName(
		    sourceFile, positionMap->UTF16ToUTF8(int(params->Positions[i])));
		if (node == nullptr) {
			continue;
		}
		auto* symbol = setup.checker->GetSymbolAtLocation(node);
		if (symbol != nullptr) {
			results[i] = setup.newSymbolResponse(symbol);
		}
	}
	return {std::move(results), nullptr};
}

// handleGetSymbolOfSourceFile returns the module symbol of a source file.
// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetSymbolOfSourceFile(gostd::Context ctx,
                                     const GetSymbolOfSourceFileParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto* sourceFile = setup.program->GetSourceFile(params->File.ToFileName());
	if (sourceFile == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: source file not found: %v",
		                      {ErrClientError, params->File.ToFileName()})};
	}

	auto* symbol = setup.checker->GetSymbolAtLocation(sourceFile);
	if (symbol == nullptr) {
		return {nullptr, nullptr};
	}
	return {setup.newSymbolResponse(symbol), nullptr};
}

// handleGetSymbolsOfSourceFiles returns the module symbols of multiple source files.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::handleGetSymbolsOfSourceFiles(
    gostd::Context ctx, const GetSymbolsOfSourceFilesParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	std::vector<std::unique_ptr<SymbolResponse>> results(params->Files.size());
	for (size_t i = 0; i < params->Files.size(); i++) {
		const auto& file = params->Files[i];
		auto* sourceFile = setup.program->GetSourceFile(file.ToFileName());
		if (sourceFile == nullptr) {
			return {std::vector<std::unique_ptr<SymbolResponse>>{},
			        gostd::errorf("%w: source file not found: %v",
			                      {ErrClientError, file.ToFileName()})};
		}
		auto* symbol = setup.checker->GetSymbolAtLocation(sourceFile);
		if (symbol != nullptr) {
			results[i] = setup.newSymbolResponse(symbol);
		}
	}
	return {std::move(results), nullptr};
}

// handleGetSymbolAtLocation returns the symbol for a node.
// If the node is an identifier, this resolves the symbol that identifier refers to.
// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetSymbolAtLocation(gostd::Context ctx,
                                   const GetSymbolAtLocationParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {nullptr, err2};
	}
	if (node == nullptr) {
		return {nullptr, nullptr};
	}

	auto* symbol = setup.checker->GetSymbolAtLocation(node);
	if (symbol == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.newSymbolResponse(symbol), nullptr};
}

// handleGetSymbolsAtLocations returns the symbols for multiple nodes.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::handleGetSymbolsAtLocations(
    gostd::Context ctx, const GetSymbolsAtLocationsParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	std::vector<std::unique_ptr<SymbolResponse>> results(
	    params->Locations.size());
	for (size_t i = 0; i < params->Locations.size(); i++) {
		auto [node, e] =
		    setup.sd->resolveNodeHandle(setup.program, params->Locations[i]);
		if (e) {
			return {std::vector<std::unique_ptr<SymbolResponse>>{}, e};
		}
		if (node == nullptr) {
			continue;
		}
		auto* symbol = setup.checker->GetSymbolAtLocation(node);
		if (symbol != nullptr) {
			results[i] = setup.newSymbolResponse(symbol);
		}
	}
	return {std::move(results), nullptr};
}

// handleGetTypeOfSymbol returns the type of a symbol.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTypeOfSymbol(gostd::Context ctx,
                               const GetTypeOfSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID, setup.checker->GetTypeOfSymbol(symbol),
	            setup.checker),
	        nullptr};
}

// handleGetTypesOfSymbols returns the types of multiple symbols.
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetTypesOfSymbols(gostd::Context ctx,
                                 const GetTypesOfSymbolsParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	std::vector<std::unique_ptr<TypeResponse>> results;
	for (auto symID : params->Symbols) {
		auto [symbol, e] = setup.resolveSymbolHandle(symID);
		if (e) {
			return {std::vector<std::unique_ptr<TypeResponse>>{}, e};
		}
		results.push_back(setup.sd->newTypeResponse(
		    setup.projectID, setup.checker->GetTypeOfSymbol(symbol),
		    setup.checker));
	}
	return {std::move(results), nullptr};
}

// handleGetDeclaredTypeOfSymbol returns the declared type of a symbol.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetDeclaredTypeOfSymbol(
    gostd::Context ctx, const GetTypeOfSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetDeclaredTypeOfSymbol(symbol), setup.checker),
	        nullptr};
}

// handleGetNonMissingTypeOfSymbol returns the non-missing type of a symbol.
// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetNonMissingTypeOfSymbol(
    gostd::Context ctx, const GetTypeOfSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetNonMissingTypeOfSymbol(symbol), setup.checker),
	        nullptr};
}

// handleResolveName resolves a name in the given context.
// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleResolveName(gostd::Context ctx,
                           const ResolveNameParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	// Resolve location node - either from node handle or from fileName+position
	auto [location, err2] =
	    setup.resolveLocation(params->Location, params->File.get(),
	                          params->Position ? &*params->Position : nullptr);
	if (err2) {
		return {nullptr, err2};
	}

	auto* symbol = setup.checker->ResolveName(
	    params->Name, location, SymbolFlags(params->Meaning),
	    params->ExcludeGlobals);
	if (symbol == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.newSymbolResponse(symbol), nullptr};
}

// handleGetSymbolsInScope returns the symbols in scope at a location.
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::handleGetSymbolsInScope(gostd::Context ctx,
                                 const GetSymbolsInScopeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [location, err2] =
	    setup.resolveLocation(params->Location, params->File.get(),
	                          params->Position ? &*params->Position : nullptr);
	if (err2) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err2};
	}
	if (location == nullptr) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{},
		        gostd::errorf("%w: getSymbolsInScope requires a location",
		                      {ErrClientError})};
	}

	auto symbols = setup.checker->GetSymbolsInScope(
	    location, SymbolFlags(params->Meaning));
	std::vector<std::unique_ptr<SymbolResponse>> results;
	for (auto sym : symbols) {
		results.push_back(setup.newSymbolResponse(sym));
	}
	return {std::move(results), nullptr};
}

// handleGetSignaturesOfType returns the call or construct signatures of a type.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<SignatureResponse>>, gostd::Error>
Session::handleGetSignaturesOfType(
    gostd::Context ctx, const GetSignaturesOfTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<SignatureResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {std::vector<std::unique_ptr<SignatureResponse>>{}, err2};
	}

	auto signatures = setup.checker->GetSignaturesOfType(
	    t, checker::SignatureKind(params->Kind));
	std::vector<std::unique_ptr<SignatureResponse>> results(signatures.size());
	for (size_t i = 0; i < signatures.size(); i++) {
		results[i] = setup.newSignatureResponse(signatures[i]);
	}
	return {std::move(results), nullptr};
}

// handleGetResolvedSignature returns the resolved signature for a call expression.
// @gen-proto-nullable
std::pair<std::unique_ptr<SignatureResponse>, gostd::Error>
Session::handleGetResolvedSignature(gostd::Context ctx,
                                    const GetResolvedSignatureParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {nullptr, err2};
	}
	if (node == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.newSignatureResponse(
	            setup.checker->GetResolvedSignature(node)),
	        nullptr};
}

// handleGetTypeAtLocation returns the type at a node location.
// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTypeAtLocation(gostd::Context ctx,
                                 const GetTypeAtLocationParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {nullptr, err2};
	}
	if (node == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.sd->newTypeResponse(setup.projectID,
	                                  setup.checker->GetTypeAtLocation(node),
	                                  setup.checker),
	        nullptr};
}

// handleGetTypeAtLocations returns the types at multiple node locations.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetTypeAtLocations(
    gostd::Context ctx, const GetTypeAtLocationsParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	std::vector<std::unique_ptr<TypeResponse>> results(
	    params->Locations.size());
	for (size_t i = 0; i < params->Locations.size(); i++) {
		auto [node, e] =
		    setup.sd->resolveNodeHandle(setup.program, params->Locations[i]);
		if (e) {
			return {std::vector<std::unique_ptr<TypeResponse>>{}, e};
		}
		// resolveNodeHandle errors on an unresolvable handle and GetTypeAtLocation
		// never returns nil, so every element resolves to a type.
		results[i] = setup.sd->newTypeResponse(
		    setup.projectID, setup.checker->GetTypeAtLocation(node),
		    setup.checker);
	}
	return {std::move(results), nullptr};
}

// handleGetTypeAtPosition returns the type at a position in a file.
// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTypeAtPosition(gostd::Context ctx,
                                 const GetTypeAtPositionParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto* sourceFile = setup.program->GetSourceFile(params->File.ToFileName());
	if (sourceFile == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: source file not found: %v",
		                      {ErrClientError, params->File.ToFileName()})};
	}

	auto* positionMap = sourceFile->GetPositionMap();
	auto* node = astnav::getTouchingPropertyName(
	    sourceFile, positionMap->UTF16ToUTF8(int(params->Position)));
	if (node == nullptr) {
		return {nullptr, nullptr};
	}

	auto* t = setup.checker->GetTypeAtLocation(node);
	if (t == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.sd->newTypeResponse(setup.projectID, t, setup.checker),
	        nullptr};
}

// handleGetTypesAtPositions returns the types at multiple positions in a file.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetTypesAtPositions(
    gostd::Context ctx, const GetTypesAtPositionsParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto* sourceFile = setup.program->GetSourceFile(params->File.ToFileName());
	if (sourceFile == nullptr) {
		return {std::vector<std::unique_ptr<TypeResponse>>{},
		        gostd::errorf("%w: source file not found: %v",
		                      {ErrClientError, params->File.ToFileName()})};
	}

	auto* positionMap = sourceFile->GetPositionMap();
	std::vector<std::unique_ptr<TypeResponse>> results(params->Positions.size());
	for (size_t i = 0; i < params->Positions.size(); i++) {
		auto* node = astnav::getTouchingPropertyName(
		    sourceFile, positionMap->UTF16ToUTF8(int(params->Positions[i])));
		if (node == nullptr) {
			continue;
		}
		auto* t = setup.checker->GetTypeAtLocation(node);
		if (t != nullptr) {
			results[i] = setup.sd->newTypeResponse(setup.projectID, t,
			                                       setup.checker);
		}
	}
	return {std::move(results), nullptr};
}

// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetParentOfSymbol(gostd::Context ctx,
                                 const GetSymbolPropertyParams* params) {
	return resolveSymbolPropertyOfSymbol(
	    params, [](Symbol* sym) -> Symbol* { return sym->parent; });
}

// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::handleGetMembersOfSymbol(gostd::Context ctx,
                                  const GetSymbolPropertyParams* params) {
	return resolveSymbolTablePropertyOfSymbol(
	    ctx, params,
	    [](Symbol* symbol) -> const SymbolTable* { return &symbol->members; });
}

// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::handleGetExportsOfSymbol(gostd::Context ctx,
                                  const GetSymbolPropertyParams* params) {
	return resolveSymbolTablePropertyOfSymbol(
	    ctx, params,
	    [](Symbol* symbol) -> const SymbolTable* { return &symbol->exports; });
}

// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetExportSymbolOfSymbol(
    gostd::Context ctx, const GetSymbolPropertyParams* params) {
	return resolveSymbolPropertyOfSymbol(
	    params, [](Symbol* sym) -> Symbol* { return sym->exportSymbol; });
}

// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetSymbolOfType(gostd::Context ctx,
                               const GetTypePropertyParams* params) {
	return resolveSymbolPropertyOfType(
	    params, [](checker::Type* t) -> Symbol* { return t->symbol; });
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTargetOfType(gostd::Context ctx,
                               const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params,
	    [](checker::Type* t) -> checker::Type* { return t->Target(); });
}

// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetFreshTypeOfType(gostd::Context ctx,
                                  const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsLiteralType()->freshType;
	    });
}

// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetRegularTypeOfType(gostd::Context ctx,
                                    const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsLiteralType()->regularType;
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetTypesOfType(gostd::Context ctx,
                              const GetTypePropertyParams* params) {
	return resolveTypeArrayPropertyOfType(
	    ctx, params,
	    [](checker::Type* t) -> std::vector<checker::Type*> {
		    return t->types();
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetTypeParametersOfType(gostd::Context ctx,
                                       const GetTypePropertyParams* params) {
	return resolveTypeArrayPropertyOfType(
	    ctx, params,
	    [](checker::Type* t) -> std::vector<checker::Type*> {
		    return interfaceTypeTypeParameters(t->AsInterfaceType());
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetOuterTypeParametersOfType(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	return resolveTypeArrayPropertyOfType(
	    ctx, params,
	    [](checker::Type* t) -> std::vector<checker::Type*> {
		    return interfaceTypeOuterTypeParameters(t->AsInterfaceType());
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetLocalTypeParametersOfType(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	return resolveTypeArrayPropertyOfType(
	    ctx, params,
	    [](checker::Type* t) -> std::vector<checker::Type*> {
		    return interfaceTypeLocalTypeParameters(t->AsInterfaceType());
	    });
}

// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetThisTypeOfType(gostd::Context ctx,
                                 const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsInterfaceType()->thisType;
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetAliasTypeArgumentsOfType(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	return resolveTypeArrayPropertyOfType(
	    ctx, params,
	    [](checker::Type* t) -> std::vector<checker::Type*> {
		    if (t->alias == nullptr) {
			    return {};
		    }
		    return t->alias->TypeArguments();
	    });
}

// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetAliasSymbolOfType(gostd::Context ctx,
                                    const GetTypePropertyParams* params) {
	return resolveSymbolPropertyOfType(
	    params, [](checker::Type* t) -> Symbol* {
		    if (t->alias == nullptr) {
			    return nullptr;
		    }
		    return t->alias->SymbolOrNil();
	    });
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetObjectTypeOfType(gostd::Context ctx,
                                   const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsIndexedAccessType()->objectType;
	    });
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetIndexTypeOfType(gostd::Context ctx,
                                  const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsIndexedAccessType()->indexType;
	    });
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetCheckTypeOfType(gostd::Context ctx,
                                  const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsConditionalType()->checkType;
	    });
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetExtendsTypeOfType(gostd::Context ctx,
                                    const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsConditionalType()->extendsType;
	    });
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetBaseTypeOfType(gostd::Context ctx,
                                 const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsSubstitutionType()->baseType;
	    });
}

// handleGetConstraintOfType returns the constraint of a substitution type.
// Type parameter constraints are handled by handleGetConstraintOfTypeParameter.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetConstraintOfType(gostd::Context ctx,
                                   const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsSubstitutionType()->constraint;
	    });
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTypeParameterOfMappedType(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsMappedType()->typeParameter;
	    });
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetConstraintTypeOfMappedType(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsMappedType()->constraintType;
	    });
}

// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetNameTypeOfMappedType(gostd::Context ctx,
                                       const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsMappedType()->nameType;
	    });
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTemplateTypeOfMappedType(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	return resolveTypePropertyOfType(
	    ctx, params, [](checker::Type* t) -> checker::Type* {
		    return t->AsMappedType()->templateType;
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetTypeParametersOfSignature(
    gostd::Context ctx, const GetSignaturePropertyParams* params) {
	return resolveTypeArrayPropertyOfSignature(
	    ctx, params,
	    [](checker::Signature* s) -> std::vector<checker::Type*> {
		    return s->typeParameters;
	    });
}

// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::handleGetParametersOfSignature(
    gostd::Context ctx, const GetSignaturePropertyParams* params) {
	return resolveSymbolArrayPropertyOfSignature(
	    params, [](checker::Signature* s) -> std::vector<Symbol*> {
		    return s->parameters;
	    });
}

// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetThisParameterOfSignature(
    gostd::Context ctx, const GetSignaturePropertyParams* params) {
	return resolveSymbolPropertyOfSignature(
	    params, [](checker::Signature* s) -> Symbol* {
		    return s->thisParameter;
	    });
}

// @gen-proto-nullable
std::pair<std::unique_ptr<SignatureResponse>, gostd::Error>
Session::handleGetTargetOfSignature(
    gostd::Context ctx, const GetSignaturePropertyParams* params) {
	return resolveSignaturePropertyOfSignature(
	    params, [](checker::Signature* s) -> checker::Signature* {
		    return s->target;
	    });
}

// resolveTypePropertyOfType resolves a type property of type `Type` and returns a type response.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::resolveTypePropertyOfType(
    gostd::Context ctx, const GetTypePropertyParams* params,
    const std::function<checker::Type*(checker::Type*)>& getter) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.sd->resolveTypeHandle(params->Project, params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	auto* result = getter(t);
	if (result == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.sd->newTypeResponse(setup.projectID, result, setup.checker),
	        nullptr};
}

// resolveTypeArrayPropertyOfType resolves a type property of an array of types and returns an array of type responses.
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::resolveTypeArrayPropertyOfType(
    gostd::Context ctx, const GetTypePropertyParams* params,
    const std::function<std::vector<checker::Type*>(checker::Type*)>& getter) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.sd->resolveTypeHandle(params->Project, params->Type);
	if (err2) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err2};
	}

	auto types = getter(t);
	if (types.empty()) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, nullptr};
	}

	std::vector<std::unique_ptr<TypeResponse>> results(types.size());
	for (size_t i = 0; i < types.size(); i++) {
		results[i] =
		    setup.sd->newTypeResponse(setup.projectID, types[i], setup.checker);
	}
	return {std::move(results), nullptr};
}

// resolveSymbolPropertyOfType resolves a type property of type `Symbol` and returns a symbol response.
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::resolveSymbolPropertyOfType(
    const GetTypePropertyParams* params,
    const std::function<Symbol*(checker::Type*)>& getter) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}

	auto [t, err2] = sd->resolveTypeHandle(params->Project, params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	auto* result = getter(t);
	if (result == nullptr) {
		return {nullptr, nullptr};
	}
	return {sd->newSymbolResponse(result, params->Project), nullptr};
}

// resolveSymbolPropertyOfSymbol resolves a symbol property of type `Symbol` and returns a symbol response.
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::resolveSymbolPropertyOfSymbol(
    const GetSymbolPropertyParams* params,
    const std::function<Symbol*(Symbol*)>& getter) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}

	auto [symbol, err2] = sd->resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {nullptr, err2};
	}

	auto* result = getter(symbol);
	if (result == nullptr) {
		return {nullptr, nullptr};
	}
	return {sd->newSymbolResponse(result, params->Project), nullptr};
}

// resolveSymbolTablePropertyOfSymbol resolves a symbol property of type `SymbolTable` and returns an array of symbol responses.
// Results are sorted using the checker's canonical symbol ordering so that API consumers receive
// a stable, deterministic order instead of Go's randomized map iteration order.
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::resolveSymbolTablePropertyOfSymbol(
    gostd::Context ctx, const GetSymbolPropertyParams* params,
    const std::function<const SymbolTable*(Symbol*)>& getter) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err};
	}

	auto [symbol, err2] = sd->resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err2};
	}

	auto* symbolTable = getter(symbol);
	if (symbolTable == nullptr || symbolTable->empty()) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, nullptr};
	}
	if (symbolTable->size() == 1) {
		for (auto& entry : *symbolTable) {
			std::vector<std::unique_ptr<SymbolResponse>> single;
			single.push_back(
			    sd->newSymbolResponse(entry.second, params->Project));
			return {std::move(single), nullptr};
		}
	}

	// More than one symbol, need a checker to sort
	auto [setup, err3] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err3) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err3};
	}
	deferGuard _done{setup.done};

	std::vector<Symbol*> symbols;
	symbols.reserve(symbolTable->size());
	for (auto& entry : *symbolTable) {
		symbols.push_back(entry.second);
	}
	auto* checker = setup.checker;
	std::sort(symbols.begin(), symbols.end(),
	          [checker](Symbol* a, Symbol* b) {
		          return checker->CompareSymbols(a, b) < 0;
	          });

	std::vector<std::unique_ptr<SymbolResponse>> results(symbols.size());
	for (size_t i = 0; i < symbols.size(); i++) {
		results[i] = setup.newSymbolResponse(symbols[i]);
	}
	return {std::move(results), nullptr};
}

// resolveSymbolArrayPropertyOfSignature resolves a signature property of an array of symbols and returns an array of symbol responses.
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::resolveSymbolArrayPropertyOfSignature(
    const GetSignaturePropertyParams* params,
    const std::function<std::vector<Symbol*>(checker::Signature*)>& getter) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err};
	}

	auto [sig, err2] =
	    sd->resolveSignatureHandle(params->Project, params->Signature);
	if (err2) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err2};
	}

	auto symbols = getter(sig);
	if (symbols.empty()) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, nullptr};
	}

	std::vector<std::unique_ptr<SymbolResponse>> results(symbols.size());
	for (size_t i = 0; i < symbols.size(); i++) {
		results[i] = sd->newSymbolResponse(symbols[i], params->Project);
	}
	return {std::move(results), nullptr};
}

// resolveSymbolPropertyOfSignature resolves a signature property of type `Symbol` and returns a symbol response.
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::resolveSymbolPropertyOfSignature(
    const GetSignaturePropertyParams* params,
    const std::function<Symbol*(checker::Signature*)>& getter) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}

	auto [sig, err2] =
	    sd->resolveSignatureHandle(params->Project, params->Signature);
	if (err2) {
		return {nullptr, err2};
	}

	auto* result = getter(sig);
	if (result == nullptr) {
		return {nullptr, nullptr};
	}
	return {sd->newSymbolResponse(result, params->Project), nullptr};
}

std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::resolveTypeArrayPropertyOfSignature(
    gostd::Context ctx, const GetSignaturePropertyParams* params,
    const std::function<std::vector<checker::Type*>(checker::Signature*)>&
        getter) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [sig, err2] =
	    setup.sd->resolveSignatureHandle(params->Project, params->Signature);
	if (err2) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err2};
	}

	auto types = getter(sig);
	if (types.empty()) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, nullptr};
	}

	std::vector<std::unique_ptr<TypeResponse>> results(types.size());
	for (size_t i = 0; i < types.size(); i++) {
		results[i] =
		    setup.sd->newTypeResponse(setup.projectID, types[i], setup.checker);
	}
	return {std::move(results), nullptr};
}

std::pair<std::unique_ptr<SignatureResponse>, gostd::Error>
Session::resolveSignaturePropertyOfSignature(
    const GetSignaturePropertyParams* params,
    const std::function<checker::Signature*(checker::Signature*)>& getter) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}

	auto [sig, err2] =
	    sd->resolveSignatureHandle(params->Project, params->Signature);
	if (err2) {
		return {nullptr, err2};
	}

	auto* result = getter(sig);
	if (result == nullptr) {
		return {nullptr, nullptr};
	}
	return {sd->newSignatureResponse(params->Project, result), nullptr};
}

// handleGetContextualType returns the contextual type for a node.
// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetContextualType(gostd::Context ctx,
                                 const GetContextualTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {nullptr, err2};
	}
	if (node == nullptr) {
		return {nullptr, nullptr};
	}

	auto* t = setup.checker->GetContextualType(node, checker::ContextFlagsNone);
	if (t == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.sd->newTypeResponse(setup.projectID, t, setup.checker),
	        nullptr};
}

// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetContextualTypeForArgument(
    gostd::Context ctx, const GetContextualTypeForArgumentParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {nullptr, err2};
	}
	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetContextualTypeForArgumentAtIndex(
	                node, int(params->Index)),
	            setup.checker),
	        nullptr};
}

// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetAwaitedType(gostd::Context ctx,
                              const CheckerTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}
	return {setup.sd->newTypeResponse(setup.projectID,
	                                  setup.checker->GetAwaitedType(t),
	                                  setup.checker),
	        nullptr};
}

// handleGetBaseTypeOfLiteralType returns the base type of a literal type (e.g. number for 42).
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetBaseTypeOfLiteralType(
    gostd::Context ctx, const GetBaseTypeOfLiteralTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetBaseTypeOfLiteralType(t), setup.checker),
	        nullptr};
}

// handleGetNonNullableType returns the type with null and undefined removed.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetNonNullableType(gostd::Context ctx,
                                  const GetTypePropertyParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(setup.projectID,
	                                  setup.checker->GetNonNullableType(t),
	                                  setup.checker),
	        nullptr};
}

// handleGetTypeFromTypeNode returns the type for a type node.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTypeFromTypeNode(
    gostd::Context ctx, const GetTypeFromTypeNodeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetTypeFromTypeNode(node), setup.checker),
	        nullptr};
}

// handleGetWidenedType returns the widened type.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetWidenedType(gostd::Context ctx,
                              const GetWidenedTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(setup.projectID,
	                                  setup.checker->GetWidenedType(t),
	                                  setup.checker),
	        nullptr};
}

// handleGetParameterType returns the type of a parameter at a given index in a signature.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetParameterType(gostd::Context ctx,
                                const GetParameterTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [sig, err2] = setup.resolveSignatureHandle(params->Signature);
	if (err2) {
		return {nullptr, err2};
	}

	if (params->Index < 0) {
		return {nullptr,
		        gostd::errorf("%w: invalid parameter index",
		                      {ErrClientError})};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetTypeAtPosition(sig, int(params->Index)),
	            setup.checker),
	        nullptr};
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTypeParameterAtPosition(
    gostd::Context ctx, const GetParameterTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [sig, err2] = setup.resolveSignatureHandle(params->Signature);
	if (err2) {
		return {nullptr, err2};
	}
	if (params->Index < 0) {
		return {nullptr,
		        gostd::errorf("%w: invalid parameter index",
		                      {ErrClientError})};
	}
	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetTypeParameterAtPosition(sig,
	                                                      int(params->Index)),
	            setup.checker),
	        nullptr};
}

// handleIsArrayLikeType returns whether a type is array-like.
std::pair<bool, gostd::Error> Session::handleIsArrayLikeType(
    gostd::Context ctx, const IsArrayLikeTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {false, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {false, err2};
	}

	return {setup.checker->IsArrayLikeType(t), nullptr};
}

// handleIsTypeAssignableTo returns whether source is assignable to target.
std::pair<bool, gostd::Error> Session::handleIsTypeAssignableTo(
    gostd::Context ctx, const IsTypeAssignableToParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {false, err};
	}
	deferGuard _done{setup.done};

	auto [source, err2] = setup.resolveTypeHandle(params->Source);
	if (err2) {
		return {false, err2};
	}
	auto [target, err3] = setup.resolveTypeHandle(params->Target);
	if (err3) {
		return {false, err3};
	}

	return {setup.checker->IsTypeAssignableTo(source, target), nullptr};
}

// handleGetShorthandAssignmentValueSymbol returns the value symbol of a shorthand property assignment.
// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetShorthandAssignmentValueSymbol(
    gostd::Context ctx, const GetTypeAtLocationParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {nullptr, err2};
	}
	if (node == nullptr) {
		return {nullptr, nullptr};
	}

	auto* symbol = setup.checker->GetShorthandAssignmentValueSymbol(node);
	if (symbol == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.newSymbolResponse(symbol), nullptr};
}

// handleGetTypeOfSymbolAtLocation returns the narrowed type of a symbol at a specific location.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTypeOfSymbolAtLocation(
    gostd::Context ctx, const GetTypeOfSymbolAtLocationParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {nullptr, err2};
	}

	auto [node, err3] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err3) {
		return {nullptr, err3};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetTypeOfSymbolAtLocation(symbol, node),
	            setup.checker),
	        nullptr};
}

// handleTypeToTypeNode converts a Type to a TypeNode AST and returns it as binary-encoded data.
// @gen-proto-result: SourceFileResponse
// @gen-proto-nullable
std::pair<ResultValue, gostd::Error> Session::handleTypeToTypeNode(
    gostd::Context ctx, const TypeToTypeNodeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {ResultValue{}, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {ResultValue{}, err2};
	}

	Node* enclosingDeclaration = nullptr;
	if (!params->Location.empty()) {
		std::tie(enclosingDeclaration, err2) =
		    setup.sd->resolveNodeHandle(setup.program, params->Location);
		if (err2) {
			return {ResultValue{}, err2};
		}
	}

	auto* typeNode =
	    setup.checker->TypeToTypeNode(t, enclosingDeclaration,
	                                  nodebuilder::Flags(params->Flags), nullptr);
	if (typeNode == nullptr) {
		return {ResultValue{}, nullptr};
	}

	auto [data, _table, err3] = encoder::EncodeNode(typeNode, nullptr);
	if (err3) {
		return {ResultValue{},
		        gostd::errorf("failed to encode type node: %w", {err3})};
	}

	if (useBinaryResponses) {
		return {ResultValue{true, std::string(data.begin(), data.end())},
		        nullptr};
	}
	auto resp = SourceFileResponse{base64Encode(data)};
	auto [v, merr] = marshalResult(resp);
	return {v, merr};
}

// @gen-proto-result: SourceFileResponse
// @gen-proto-nullable
std::pair<ResultValue, gostd::Error>
Session::handleSignatureToSignatureDeclaration(
    gostd::Context ctx, const SignatureToSignatureDeclarationParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {ResultValue{}, err};
	}
	deferGuard _done{setup.done};

	auto [sig, err2] = setup.resolveSignatureHandle(params->Signature);
	if (err2) {
		return {ResultValue{}, err2};
	}

	Node* enclosingDeclaration = nullptr;
	if (!params->Location.empty()) {
		std::tie(enclosingDeclaration, err2) =
		    setup.sd->resolveNodeHandle(setup.program, params->Location);
		if (err2) {
			return {ResultValue{}, err2};
		}
	}

	auto* node = setup.checker->SignatureToSignatureDeclaration(
	    sig, Kind(params->Kind), enclosingDeclaration,
	    nodebuilder::Flags(params->Flags));
	if (node == nullptr) {
		return {ResultValue{}, nullptr};
	}

	auto [data, _table, err3] = encoder::EncodeNode(node, nullptr);
	if (err3) {
		return {ResultValue{},
		        gostd::errorf("failed to encode signature declaration: %w",
		                      {err3})};
	}

	if (useBinaryResponses) {
		return {ResultValue{true, std::string(data.begin(), data.end())},
		        nullptr};
	}
	auto resp = SourceFileResponse{base64Encode(data)};
	auto [v, merr] = marshalResult(resp);
	return {v, merr};
}

// handleTypeToString converts a Type to its string representation.
std::pair<ResultValue, gostd::Error> Session::handleTypeToString(
    gostd::Context ctx, const TypeToTypeNodeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {ResultValue{}, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {ResultValue{}, err2};
	}

	Node* enclosingDeclaration = nullptr;
	if (!params->Location.empty()) {
		std::tie(enclosingDeclaration, err2) =
		    setup.sd->resolveNodeHandle(setup.program, params->Location);
		if (err2) {
			return {ResultValue{}, err2};
		}
	}

	std::string result;
	if (params->Flags != 0) {
		result = setup.checker->TypeToStringEx(
		    t, enclosingDeclaration, checker::TypeFormatFlags(params->Flags),
		    nullptr);
	} else {
		result = setup.checker->TypeToStringEx(
		    t, enclosingDeclaration,
		    checker::TypeFormatFlagsAllowUniqueESSymbolType |
		        checker::TypeFormatFlagsUseAliasDefinedOutsideCurrentScope,
		    nullptr);
	}
	return marshalResult(result);
}

// handlePrintNode decodes a binary-encoded AST node and prints it to text.
std::pair<std::string, gostd::Error> Session::handlePrintNode(
    gostd::Context ctx, const PrintNodeParams* params) {
	auto [node, err] = decodePrintNode(params->Data);
	if (err) {
		return {"", err};
	}

	SourceFile* sourceFile = nullptr;
	if (isSourceFile(node)) {
		sourceFile = static_cast<SourceFile*>(node);
	}
	auto prn = std::unique_ptr<printer::Printer>(newPrinter(params));
	return {prn->Emit(node, sourceFile), nullptr};
}

std::pair<Node*, gostd::Error> decodePrintNode(const std::string& encoded) {
	auto [data, badByte] = base64Decode(encoded);
	if (badByte >= 0) {
		return {nullptr,
		        gostd::errorf(
		            "%w: invalid base64 data: illegal base64 data at input "
		            "byte %d",
		            {ErrClientError, badByte})};
	}

	auto [node, err2] = encoder::DecodeNodes(data);
	if (err2) {
		return {nullptr,
		        gostd::errorf("%w: failed to decode AST: %w",
		                      {ErrClientError, err2})};
	}
	return {node, nullptr};
}

printer::Printer* newPrinter(const PrintNodeParams* params) {
	printer::PrinterOptions options;
	options.PreserveSourceNewlines = params->PreserveSourceNewlines;
	options.NeverAsciiEscape = params->NeverAsciiEscape;
	options.TerminateUnterminatedLiterals = params->TerminateUnterminatedLiterals;
	return printer::NewPrinter(options, printer::PrintHandlers{}, nullptr);
}

std::pair<std::unique_ptr<EmitResponse>, gostd::Error> Session::handleEmit(
    gostd::Context ctx, const EmitParams* params) {
	auto [program, options, err] = getEmitOptions(params);
	if (err) {
		return {nullptr, err};
	}
	std::shared_ptr<std::unordered_map<std::string, std::string>> outputFiles;
	std::mutex outputMu;
	auto [sd, err2] = getSnapshotData(params->Snapshot);
	if (err2) {
		return {nullptr, err2};
	}
	if (requestfilesystem::HasFullFileSystem(sd->fileSystem)) {
		outputFiles = std::make_shared<
		    std::unordered_map<std::string, std::string>>();
		options.WriteFile = [&outputFiles, &outputMu](
		                        const std::string& fileName,
		                        const std::string& text,
		                        compiler::WriteFileData* /*data*/)
		    -> std::optional<std::string> {
			std::lock_guard lk(outputMu);
			(*outputFiles)[fileName] = text;
			return std::nullopt;
		};
	} else {
		auto* fs = snapshotHost->FS().get();
		options.WriteFile = [fs](const std::string& fileName,
		                         const std::string& text,
		                         compiler::WriteFileData* /*data*/)
		    -> std::optional<std::string> {
			auto e = fs->WriteFile(fileName, text);
			if (e) {
				return e.str();
			}
			return std::nullopt;
		};
	}
	auto [result, err3] = emitProgram(ctx, program, options);
	if (err3) {
		return {nullptr, err3};
	}
	auto emittedFiles = result->EmittedFiles;
	auto resp = std::make_unique<EmitResponse>();
	resp->EmitSkipped = result->EmitSkipped;
	resp->Diagnostics = nonNilDiagnostics(result->Diagnostics);
	resp->EmittedFiles = std::move(emittedFiles);
	if (outputFiles != nullptr) {
		resp->EmittedFilesContents.resize(resp->EmittedFiles.size());
		for (size_t i = 0; i < resp->EmittedFiles.size(); i++) {
			resp->EmittedFilesContents[i] = (*outputFiles)[resp->EmittedFiles[i]];
		}
	}
	return {std::move(resp), nullptr};
}

std::pair<std::unique_ptr<EmitOutputResponse>, gostd::Error>
Session::handleEmitToString(gostd::Context ctx, const EmitParams* params) {
	auto [program, options, err] = getEmitOptions(params);
	if (err) {
		return {nullptr, err};
	}
	return emitToOutput(ctx, program, std::move(options));
}

std::pair<std::unique_ptr<EmitOutputResponse>, gostd::Error>
Session::handleSelectedFilesEmit(gostd::Context ctx,
                                 const SelectedFilesEmitParams* params,
                                 compiler::EmitOnly emitOnly) {
	auto [program, err] = getEmitProgram(params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	if (params->Files.empty()) {
		return {nullptr,
		        gostd::errorf("%w: files is required", {ErrClientError})};
	}
	compiler::EmitOptions options;
	for (const auto& file : params->Files) {
		auto [sourceFile, e] = resolveOptionalSourceFile(program, &file);
		if (e) {
			return {nullptr, e};
		}
		options.TargetSourceFiles.push_back(sourceFile);
	}
	options.EmitOnly = emitOnly;
	options.ForceEmit = true;
	return emitToOutput(ctx, program, std::move(options));
}

std::pair<std::unique_ptr<EmitOutputResponse>, gostd::Error> emitToOutput(
    gostd::Context ctx, compiler::SimpleProgram* program,
    compiler::EmitOptions options) {
	std::mutex mu;
	std::vector<std::unique_ptr<EmitOutputFile>> outputFiles;
	options.WriteFile =
	    [&outputFiles, &mu](const std::string& fileName,
	                        const std::string& text,
	                        compiler::WriteFileData* data)
	    -> std::optional<std::string> {
		std::optional<std::string> sourceFileName;
		if (data != nullptr && data->SourceFile != nullptr) {
			sourceFileName = data->SourceFile->FileName();
		}
		auto out = std::make_unique<EmitOutputFile>();
		out->FileName = fileName;
		out->Text = text;
		out->SourceFileName = std::move(sourceFileName);
		std::lock_guard lk(mu);
		outputFiles.push_back(std::move(out));
		return std::nullopt;
	};

	auto [result, err] = emitProgram(ctx, program, options);
	if (err) {
		return {nullptr, err};
	}
	std::sort(outputFiles.begin(), outputFiles.end(),
	          [](const std::unique_ptr<EmitOutputFile>& a,
	             const std::unique_ptr<EmitOutputFile>& b) {
		          return a->FileName < b->FileName;
	          });
	std::vector<std::shared_ptr<EmitOutputFile>> outputFilesShared;
	outputFilesShared.reserve(outputFiles.size());
	for (auto& f : outputFiles) {
		outputFilesShared.push_back(std::shared_ptr<EmitOutputFile>(
		    f.release()));
	}
	auto resp = std::make_unique<EmitOutputResponse>();
	resp->EmitSkipped = result->EmitSkipped;
	resp->Diagnostics = nonNilDiagnostics(result->Diagnostics);
	resp->OutputFiles = std::move(outputFilesShared);
	return {std::move(resp), nullptr};
}

std::tuple<compiler::SimpleProgram*, compiler::EmitOptions, gostd::Error>
Session::getEmitOptions(const EmitParams* params) {
	auto [program, err] = getEmitProgram(params->Snapshot, params->Project);
	if (err) {
		return {nullptr, compiler::EmitOptions{}, err};
	}
	auto [emitOnly, err2] = getEmitOnly(
	    params->EmitOnly ? &*params->EmitOnly : nullptr);
	if (err2) {
		return {nullptr, compiler::EmitOptions{}, err2};
	}
	compiler::EmitOptions options;
	options.EmitOnly = emitOnly;
	return {program, options, nullptr};
}

std::pair<compiler::SimpleProgram*, gostd::Error> Session::getEmitProgram(
    SnapshotID snapshot, const project::ID& projectID) {
	auto [sd, err] = getSnapshotData(snapshot);
	if (err) {
		return {nullptr, err};
	}
	return sd->getProgram(projectID);
}

std::pair<compiler::EmitOnly, gostd::Error> getEmitOnly(
    const uint32_t* value) {
	if (value == nullptr) {
		return {compiler::EmitOnly::EmitAll, nullptr};
	}
	if (*value > uint32_t(compiler::EmitOnly::EmitOnlyDts)) {
		return {compiler::EmitOnly::EmitAll,
		        gostd::errorf("%w: invalid emitOnly value: %d",
		                      {ErrClientError, uint64_t(*value)})};
	}
	return {compiler::EmitOnly(*value), nullptr};
}

std::pair<std::unique_ptr<compiler::EmitResult>, gostd::Error> emitProgram(
    gostd::Context ctx, compiler::SimpleProgram* program,
    const compiler::EmitOptions& options) {
	compiler::EmitOptions mutableOptions = options;
	auto* result = program->Emit(&mutableOptions);
	if (result != nullptr) {
		return {std::unique_ptr<compiler::EmitResult>(result), nullptr};
	}
	if (auto e = gostd::ctxErr(ctx)) {
		return {nullptr, e};
	}
	return {nullptr,
	        gostd::newError("compiler emit returned nil result")};
}

std::vector<std::shared_ptr<DiagnosticResponse>> nonNilDiagnostics(
    const std::vector<Diagnostic*>& diags) {
	auto result = NewDiagnosticResponses(diags);
	return result;
}

// handleFormatNodeForInsertion formats a synthesized node with the correct indentation
// for insertion at a specific position in an existing file.
std::pair<std::string, gostd::Error> Session::handleFormatNodeForInsertion(
    gostd::Context ctx, const FormatNodeForInsertionParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {"", err};
	}

	auto [program, err2] = sd->getProgram(params->Project);
	if (err2) {
		return {"", err2};
	}

	auto [targetSourceFile, err3] =
	    resolveOptionalSourceFile(program, &params->File);
	if (err3) {
		return {"", err3};
	}

	auto [data, badByte] = base64Decode(params->Data);
	if (badByte >= 0) {
		return {"",
		        gostd::errorf(
		            "%w: invalid base64 data: illegal base64 data at input "
		            "byte %d",
		            {ErrClientError, badByte})};
	}

	auto [node, err5] = encoder::DecodeNodes(data);
	if (err5) {
		return {"", gostd::errorf("%w: failed to decode AST: %w",
		                          {ErrClientError, err5})};
	}

	int pos = targetSourceFile->GetPositionMap()->UTF16ToUTF8(
	    int(params->Position));
	auto formatOptions =
	    sd->snapshot->UserPreferences().FormatCodeSettings;
	auto newLine = formatOptions.NewLineCharacter;

	NodeFactory factory{NodeFactoryHooks{}};
	auto [text, nodeWithPos] = printer::PrintAndPositionNode(
	    &factory, node, nullptr, newLine, formatOptions.IndentSize, nullptr);
	auto* syntheticFile = printer::CreateSyntheticSourceFile(
	    &factory, nodeWithPos, text, targetSourceFile->ParseOptions());

	bool isAtLineStart =
	    format::GetLineStartPositionForPosition(pos, targetSourceFile) == pos;
	int initialIndentation = format::GetIndentation(pos, targetSourceFile,
	                                                formatOptions,
	                                                isAtLineStart);

	int delta = 0;
	if (formatOptions.IndentSize != 0 &&
	    format::ShouldIndentChildNode(formatOptions, node, nullptr, nullptr)) {
		delta = formatOptions.IndentSize;
	}

	auto fctx = format::WithFormatCodeSettings(format::FormatRequestContext{},
	                                           formatOptions, newLine);
	auto changes = format::FormatNodeGivenIndentation(
	    fctx, nodeWithPos, syntheticFile, targetSourceFile->LanguageVariant,
	    initialIndentation, delta);

	return {ApplyBulkEdits(text, changes), nullptr};
}

// handleGetIntrinsicType returns an intrinsic type (any, string, number, etc.).
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetIntrinsicType(
    gostd::Context ctx, const GetIntrinsicTypeParams* params,
    const std::function<checker::Type*(checker::Checker*)>& getter) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto* t = getter(setup.checker);
	if (t == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.sd->newTypeResponse(setup.projectID, t, setup.checker),
	        nullptr};
}

// handleGetWellKnownSymbols returns the handle ids of the per-checker singleton
// symbols (unknown, undefined, arguments) so the client can identify them by id.
std::pair<std::unique_ptr<WellKnownSymbolsResponse>, gostd::Error>
Session::handleGetWellKnownSymbols(gostd::Context ctx,
                                   const GetIntrinsicTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto resp = std::make_unique<WellKnownSymbolsResponse>();
	resp->Unknown =
	    setup.sd->registerSymbol(setup.checker->GetUnknownSymbol(),
	                             setup.projectID)
	        .first;
	resp->Undefined =
	    setup.sd->registerSymbol(setup.checker->GetUndefinedSymbol(),
	                             setup.projectID)
	        .first;
	resp->Arguments =
	    setup.sd->registerSymbol(setup.checker->GetArgumentsSymbol(),
	                             setup.projectID)
	        .first;
	return {std::move(resp), nullptr};
}

// handleGetWellKnownSignatures returns the handle id of the per-checker unknown
// signature so the client can identify it by id.
std::pair<std::unique_ptr<WellKnownSignaturesResponse>, gostd::Error>
Session::handleGetWellKnownSignatures(gostd::Context ctx,
                                      const GetIntrinsicTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto resp = std::make_unique<WellKnownSignaturesResponse>();
	resp->Unknown = setup.sd->registerSignature(
	    setup.projectID, setup.checker->GetUnknownSignature());
	return {std::move(resp), nullptr};
}

// handleIsContextSensitive returns whether a node is context-sensitive.
std::pair<bool, gostd::Error> Session::handleIsContextSensitive(
    gostd::Context ctx, const GetContextualTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {false, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {false, err2};
	}
	if (node == nullptr) {
		return {false, nullptr};
	}

	return {setup.checker->IsContextSensitive(node), nullptr};
}

// handleGetReturnTypeOfSignature returns the return type of a signature.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetReturnTypeOfSignature(
    gostd::Context ctx, const GetSignaturePropertyParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [sig, err2] = setup.resolveSignatureHandle(params->Signature);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetReturnTypeOfSignature(sig), setup.checker),
	        nullptr};
}

// handleGetRestTypeOfSignature returns the rest type of a signature.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetRestTypeOfSignature(
    gostd::Context ctx, const CheckerSignatureParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [sig, err2] = setup.resolveSignatureHandle(params->Signature);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetRestTypeOfSignature(sig), setup.checker),
	        nullptr};
}

// handleGetTypePredicateOfSignature returns the type predicate of a signature.
// @gen-proto-nullable
std::pair<std::unique_ptr<TypePredicateResponse>, gostd::Error>
Session::handleGetTypePredicateOfSignature(
    gostd::Context ctx, const CheckerSignatureParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [sig, err2] = setup.resolveSignatureHandle(params->Signature);
	if (err2) {
		return {nullptr, err2};
	}

	auto* pred = setup.checker->GetTypePredicateOfSignature(sig);
	if (pred == nullptr) {
		return {nullptr, nullptr};
	}

	auto resp = std::make_unique<TypePredicateResponse>();
	resp->Kind = int32_t(pred->kind);
	resp->ParameterIndex = pred->parameterIndex;
	resp->ParameterName = pred->parameterName;
	if (pred->t != nullptr) {
		resp->Type = std::shared_ptr<TypeResponse>(
		    setup.sd->newTypeResponse(setup.projectID, pred->t, setup.checker)
		        .release());
	}

	return {std::move(resp), nullptr};
}

// handleIsArrayType returns whether a type is Array<T> or ReadonlyArray<T>.
std::pair<bool, gostd::Error> Session::handleIsArrayType(
    gostd::Context ctx, const CheckerTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {false, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {false, err2};
	}

	return {setup.checker->IsArrayType(t), nullptr};
}

// handleIsReadonlySymbol returns whether a symbol is a readonly symbol.
std::pair<bool, gostd::Error> Session::handleIsReadonlySymbol(
    gostd::Context ctx, const CheckerSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {false, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {false, err2};
	}

	return {setup.checker->IsReadonlySymbol(symbol), nullptr};
}

// handleGetBaseTypes returns the base types of an interface/class type.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetBaseTypes(gostd::Context ctx,
                            const CheckerTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err2};
	}

	auto baseTypes = setup.checker->GetBaseTypes(t);
	if (baseTypes.empty()) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, nullptr};
	}

	std::vector<std::unique_ptr<TypeResponse>> results(baseTypes.size());
	for (size_t i = 0; i < baseTypes.size(); i++) {
		results[i] = setup.sd->newTypeResponse(setup.projectID, baseTypes[i],
		                                       setup.checker);
	}

	return {std::move(results), nullptr};
}

// handleGetPropertiesOfType returns the properties of a type.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::handleGetPropertiesOfType(gostd::Context ctx,
                                   const CheckerTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err2};
	}

	auto props = setup.checker->GetPropertiesOfType(t);
	if (props.empty()) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, nullptr};
	}

	std::vector<std::unique_ptr<SymbolResponse>> results(props.size());
	for (size_t i = 0; i < props.size(); i++) {
		results[i] = setup.newSymbolResponse(props[i]);
	}

	return {std::move(results), nullptr};
}

// handleGetApparentPropertiesOfType returns the apparent properties of a type,
// including CallableFunction or NewableFunction members where applicable.
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::handleGetApparentPropertiesOfType(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err2};
	}

	auto props = setup.checker->GetApparentProperties(t);
	std::vector<std::unique_ptr<SymbolResponse>> results(props.size());
	for (size_t i = 0; i < props.size(); i++) {
		results[i] = setup.newSymbolResponse(props[i]);
	}
	return {std::move(results), nullptr};
}

// handleGetApparentType returns the apparent type of a type.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetApparentType(gostd::Context ctx,
                               const GetTypePropertyParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(setup.projectID,
	                                  setup.checker->GetApparentType(t),
	                                  setup.checker),
	        nullptr};
}

// handleGetReducedType returns the reduced type of a type.
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetReducedType(gostd::Context ctx,
                              const GetTypePropertyParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(setup.projectID,
	                                  setup.checker->GetReducedType(t),
	                                  setup.checker),
	        nullptr};
}

// handleGetIndexInfosOfType returns the index infos of a type.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<IndexInfoResponse>>, gostd::Error>
Session::handleGetIndexInfosOfType(gostd::Context ctx,
                                   const CheckerTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<IndexInfoResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {std::vector<std::unique_ptr<IndexInfoResponse>>{}, err2};
	}

	auto infos = setup.checker->GetIndexInfosOfType(t);
	if (infos.empty()) {
		return {std::vector<std::unique_ptr<IndexInfoResponse>>{}, nullptr};
	}

	std::vector<std::unique_ptr<IndexInfoResponse>> results(infos.size());
	for (size_t i = 0; i < infos.size(); i++) {
		results[i] = setup.newIndexInfoResponse(infos[i]);
	}

	return {std::move(results), nullptr};
}

std::tuple<checkerSetup, checker::Type*, checker::Type*, gostd::Error>
Session::resolveIndexInfoRequest(gostd::Context ctx,
                                 const GetIndexInfoOfTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {checkerSetup{}, nullptr, nullptr, err};
	}

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		setup.done();
		return {checkerSetup{}, nullptr, nullptr, err2};
	}

	checker::Type* keyType = nullptr;
	switch (checker::IndexKind(params->Kind)) {
	case checker::IndexKind::String:
		keyType = setup.checker->GetStringType();
		break;
	case checker::IndexKind::Number:
		keyType = setup.checker->GetNumberType();
		break;
	default:
		setup.done();
		return {checkerSetup{}, nullptr, nullptr,
		        gostd::errorf("%w: invalid index kind %d",
		                      {ErrClientError, params->Kind})};
	}
	return {setup, t, keyType, nullptr};
}

// @gen-proto-nullable
std::pair<std::unique_ptr<IndexInfoResponse>, gostd::Error>
Session::handleGetIndexInfoOfType(
    gostd::Context ctx, const GetIndexInfoOfTypeParams* params) {
	auto [setup, t, keyType, err] = resolveIndexInfoRequest(ctx, params);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};
	return {setup.newIndexInfoResponse(
	            setup.checker->GetIndexInfoOfType(t, keyType)),
	        nullptr};
}

// handleGetConstraintOfTypeParameter returns the constraint of a type parameter.
// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetConstraintOfTypeParameter(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	auto* constraint = setup.checker->GetConstraintOfTypeParameter(t);
	if (constraint == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.sd->newTypeResponse(setup.projectID, constraint,
	                                  setup.checker),
	        nullptr};
}

// handleGetDefaultFromTypeParameter returns the default type of a type parameter.
// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetDefaultFromTypeParameter(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetDefaultFromTypeParameter(t), setup.checker),
	        nullptr};
}

// handleGetBaseConstraintOfType returns the base constraint of an instantiable type.
// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetBaseConstraintOfType(
    gostd::Context ctx, const CheckerTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	auto* constraint = setup.checker->GetBaseConstraintOfType(t);
	if (constraint == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.sd->newTypeResponse(setup.projectID, constraint,
	                                  setup.checker),
	        nullptr};
}

// handleGetPropertyOfType returns a named property symbol of a type.
// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetPropertyOfType(gostd::Context ctx,
                                 const GetPropertyOfTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	auto* prop = setup.checker->GetPropertyOfType(t, params->Name);
	if (prop == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.newSymbolResponse(prop), nullptr};
}

// @gen-proto-nullable
std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTypeOfPropertyOfType(
    gostd::Context ctx, const GetPropertyOfTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetTypeOfPropertyOfType(t, params->Name),
	            setup.checker),
	        nullptr};
}

// handleGetConstantValue returns the constant value of an enum member or const enum access.
// @gen-proto-nullable
std::pair<std::unique_ptr<ConstantValueResponse>, gostd::Error>
Session::handleGetConstantValue(gostd::Context ctx,
                                const CheckerNodeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {nullptr, err2};
	}
	if (node == nullptr) {
		return {nullptr, nullptr};
	}

	auto result = std::make_unique<ConstantValueResponse>();
	auto value = setup.checker->GetConstantValue(node);
	result->IsNumber = std::holds_alternative<Number>(value);
	result->Value = literalValueToJSON(value);
	return {std::move(result), nullptr};
}

// handleGetSignatureFromDeclaration returns the signature of a function-like declaration.
std::pair<std::unique_ptr<SignatureResponse>, gostd::Error>
Session::handleGetSignatureFromDeclaration(
    gostd::Context ctx, const CheckerNodeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.newSignatureResponse(
	            setup.checker->GetSignatureFromDeclaration(node)),
	        nullptr};
}

// handleGetExportSpecifierLocalTargetSymbol returns the local target symbol of an export specifier.
// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetExportSpecifierLocalTargetSymbol(
    gostd::Context ctx, const CheckerNodeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [node, err2] =
	    setup.sd->resolveNodeHandle(setup.program, params->Location);
	if (err2) {
		return {nullptr, err2};
	}
	if (node == nullptr) {
		return {nullptr, nullptr};
	}

	auto* symbol = setup.checker->GetExportSpecifierLocalTargetSymbol(node);
	if (symbol == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.newSymbolResponse(symbol), nullptr};
}

// handleGetAliasedSymbol resolves an alias symbol to its target.
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetAliasedSymbol(gostd::Context ctx,
                                const CheckerSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.newSymbolResponse(setup.checker->GetAliasedSymbol(symbol)),
	        nullptr};
}

// handleGetFullyQualifiedName returns the fully qualified name of a symbol
// (e.g. `"/path/to/module".Namespace.Name`).
std::pair<std::string, gostd::Error> Session::handleGetFullyQualifiedName(
    gostd::Context ctx, const CheckerSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {"", err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {"", err2};
	}
	if (symbol == nullptr) {
		return {"", nullptr};
	}

	return {setup.checker->GetFullyQualifiedName(symbol), nullptr};
}

// handleGetImmediateAliasedSymbol resolves one level of alias indirection.
// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetImmediateAliasedSymbol(
    gostd::Context ctx, const CheckerSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {nullptr, err2};
	}
	if (symbol == nullptr) {
		return {nullptr, nullptr};
	}

	auto* aliased = setup.checker->GetImmediateAliasedSymbol(symbol);
	if (aliased == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.newSymbolResponse(aliased), nullptr};
}

// handleGetTargetSymbol returns the target symbol if the symbol is instantiated,
// otherwise returns the provided symbol.
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleMethodGetTargetSymbol(gostd::Context ctx,
                                     const CheckerSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.newSymbolResponse(setup.checker->GetTargetSymbol(symbol)),
	        nullptr};
}

std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetExportSymbolOfSymbolForChecker(
    gostd::Context ctx, const CheckerSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {nullptr, err2};
	}
	return {setup.newSymbolResponse(
	            setup.checker->GetExportSymbolOfSymbol(symbol)),
	        nullptr};
}

// handleGetExportsOfModule returns the resolved exports of a module symbol,
// including those introduced by `export *` and re-exports.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<SymbolResponse>>, gostd::Error>
Session::handleGetExportsOfModule(gostd::Context ctx,
                                  const CheckerSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, err2};
	}
	if (symbol == nullptr) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, nullptr};
	}

	auto exports = setup.checker->GetExportsOfModule(symbol);
	if (exports.empty()) {
		return {std::vector<std::unique_ptr<SymbolResponse>>{}, nullptr};
	}
	auto* checker = setup.checker;
	std::sort(exports.begin(), exports.end(),
	          [checker](Symbol* a, Symbol* b) {
		          return checker->CompareSymbols(a, b) < 0;
	          });

	std::vector<std::unique_ptr<SymbolResponse>> results(exports.size());
	for (size_t i = 0; i < exports.size(); i++) {
		results[i] = setup.newSymbolResponse(exports[i]);
	}

	return {std::move(results), nullptr};
}

// handleGetMemberInModuleExports returns an export by name from a module symbol.
// @gen-proto-nullable
std::pair<std::unique_ptr<SymbolResponse>, gostd::Error>
Session::handleGetMemberInModuleExports(
    gostd::Context ctx, const GetMemberInModuleExportsParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {nullptr, err2};
	}
	if (symbol == nullptr) {
		return {nullptr, nullptr};
	}

	auto* member =
	    setup.checker->TryGetMemberInModuleExports(params->Name, symbol);
	if (member == nullptr) {
		return {nullptr, nullptr};
	}

	return {setup.newSymbolResponse(member), nullptr};
}

// handleGetJSDocTags returns the JSDoc tags of a symbol as structured name/text pairs.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<JSDocTagInfo>>, gostd::Error>
Session::handleGetJSDocTags(gostd::Context ctx,
                            const CheckerSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<JSDocTagInfo>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {std::vector<std::unique_ptr<JSDocTagInfo>>{}, err2};
	}
	if (symbol == nullptr) {
		return {std::vector<std::unique_ptr<JSDocTagInfo>>{}, nullptr};
	}

	auto tags = ls::GetSymbolJSDocTags(symbol);
	if (tags.empty()) {
		return {std::vector<std::unique_ptr<JSDocTagInfo>>{}, nullptr};
	}
	std::vector<std::unique_ptr<JSDocTagInfo>> results(tags.size());
	for (size_t i = 0; i < tags.size(); i++) {
		auto tag = std::make_unique<JSDocTagInfo>();
		tag->Name = tags[i].Name;
		tag->Text = tags[i].Text;
		results[i] = std::move(tag);
	}
	return {std::move(results), nullptr};
}

// handleGetDocumentationComment returns the rendered documentation comment of a symbol as plain text.
std::pair<std::string, gostd::Error> Session::handleGetDocumentationComment(
    gostd::Context ctx, const CheckerSymbolParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {"", err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {"", err2};
	}
	if (symbol == nullptr) {
		return {"", nullptr};
	}

	return {ls::GetSymbolDocumentationComment(setup.checker, symbol), nullptr};
}

// handleGetTypeArguments returns the type arguments of a type reference.
// @gen-proto-nullable
std::pair<std::vector<std::unique_ptr<TypeResponse>>, gostd::Error>
Session::handleGetTypeArguments(gostd::Context ctx,
                                const CheckerTypeParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] = setup.resolveTypeHandle(params->Type);
	if (err2) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, err2};
	}

	auto typeArgs = setup.checker->GetTypeArguments(t);
	if (typeArgs.empty()) {
		return {std::vector<std::unique_ptr<TypeResponse>>{}, nullptr};
	}

	std::vector<std::unique_ptr<TypeResponse>> results(typeArgs.size());
	for (size_t i = 0; i < typeArgs.size(); i++) {
		results[i] = setup.sd->newTypeResponse(setup.projectID, typeArgs[i],
		                                       setup.checker);
	}

	return {std::move(results), nullptr};
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetTrueTypeOfConditionalType(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] =
	    setup.sd->resolveTypeHandle(params->Project, params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetTrueTypeOfConditionalType(t), setup.checker),
	        nullptr};
}

std::pair<std::unique_ptr<TypeResponse>, gostd::Error>
Session::handleGetFalseTypeOfConditionalType(
    gostd::Context ctx, const GetTypePropertyParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {nullptr, err};
	}
	deferGuard _done{setup.done};

	auto [t, err2] =
	    setup.sd->resolveTypeHandle(params->Project, params->Type);
	if (err2) {
		return {nullptr, err2};
	}

	return {setup.sd->newTypeResponse(
	            setup.projectID,
	            setup.checker->GetFalseTypeOfConditionalType(t), setup.checker),
	        nullptr};
}

std::pair<std::vector<std::unique_ptr<TextEdit>>, gostd::Error>
Session::handleGetImportAdderEdits(
    gostd::Context ctx, const GetImportAdderEditsParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<std::unique_ptr<TextEdit>>{}, err};
	}

	auto projectID = params->Project;
	auto* workingSnapshot = sd->snapshot;
	auto [program, err2] = sd->getProgram(params->Project);
	if (err2) {
		return {std::vector<std::unique_ptr<TextEdit>>{}, err2};
	}
	auto* sourceFile = program->GetSourceFile(params->File.ToFileName());
	if (sourceFile == nullptr) {
		return {std::vector<std::unique_ptr<TextEdit>>{},
		        gostd::errorf("%w: source file not found: %v",
		                      {ErrClientError, params->File.ToFileName()})};
	}

	auto userPreferences = workingSnapshot->UserPreferences();
	if (auto* registry = workingSnapshot->AutoImportRegistry();
	    registry == nullptr ||
	    !registry->IsPreparedForImportingFile(sourceFile->FileName(),
	                                          projectID, userPreferences)) {
		auto* preparedSnapshot = snapshotHost->CloneSnapshotWithAutoImports(
		    ctx, workingSnapshot,
		    params->File.ToURI(GetCurrentDirectory()), nullptr);
		if (projectSession != nullptr) {
			projectSession->TryAdoptSnapshotInBackground(workingSnapshot,
			                                           preparedSnapshot);
		}
		deferGuard _deref{[preparedSnapshot] { preparedSnapshot->Deref(); }};

		workingSnapshot = preparedSnapshot;
		auto* proj = workingSnapshot->ProjectCollection->GetProject(projectID);
		if (proj == nullptr) {
			return {std::vector<std::unique_ptr<TextEdit>>{},
			        gostd::errorf("%w: project %s not found",
			                      {ErrClientError, projectID.v})};
		}
		program = proj->GetProgram();
		if (program == nullptr) {
			return {std::vector<std::unique_ptr<TextEdit>>{},
			        gostd::errorf("%w: project has no program",
			                      {ErrClientError})};
		}
		sourceFile = program->GetSourceFile(params->File.ToFileName());
		if (sourceFile == nullptr) {
			return {std::vector<std::unique_ptr<TextEdit>>{},
			        gostd::errorf("%w: source file not found: %v",
			                      {ErrClientError, params->File.ToFileName()})};
		}
		userPreferences = workingSnapshot->UserPreferences();
	}

	auto* registry = workingSnapshot->AutoImportRegistry();
	if (registry == nullptr) {
		return {std::vector<std::unique_ptr<TextEdit>>{}, nullptr};
	}

	// Go: program.GetTypeChecker(ctx) — the C++ program holds a single
	// lazily-created checker; done() is a no-op.
	auto* ch = program->getChecker();

	auto* view = autoimport::NewView(registry, sourceFile, projectID, program,
	                                 ch,
	                                 userPreferences.ModuleSpecifierPreferences());
	auto* importAdder = autoimport::NewImportAdder(
	    ctx, program, ch, sourceFile, view,
	    workingSnapshot->GetPreferences(sourceFile->FileName())
	        .FormatCodeSettings,
	    workingSnapshot->Converters(), userPreferences);

	for (size_t i = 0; i < params->Actions.size(); i++) {
		const auto& action = params->Actions[i];
		if (action.Kind == ImportAdderActionKindImportSymbol) {
			if (action.Symbol == 0) {
				return {std::vector<std::unique_ptr<TextEdit>>{},
				        gostd::errorf(
				            "%w: import adder action %d missing symbol",
				            {ErrClientError, i})};
			}
			auto [symbol, e] = sd->resolveSymbolHandle(action.Symbol);
			if (e) {
				return {std::vector<std::unique_ptr<TextEdit>>{}, e};
			}
			bool isValidTypeOnlyUseSite = true;
			if (action.IsValidTypeOnlyUseSite.has_value()) {
				isValidTypeOnlyUseSite = *action.IsValidTypeOnlyUseSite;
			}
			importAdder->AddImportFromExportedSymbol(symbol,
			                                       isValidTypeOnlyUseSite);
		} else {
			return {std::vector<std::unique_ptr<TextEdit>>{},
			        gostd::errorf(
			            "%w: unknown import adder action kind %q",
			            {ErrClientError, action.Kind})};
		}
	}

	if (!importAdder->HasFixes()) {
		return {std::vector<std::unique_ptr<TextEdit>>{}, nullptr};
	}
	return {toAPITextEdits(sourceFile, importAdder->Edits()), nullptr};
}

std::vector<std::unique_ptr<TextEdit>> toAPITextEdits(
    SourceFile* sourceFile, const std::vector<lsproto::TextEdit>& edits) {
	auto originalText = sourceFile->OriginalText();
	auto* lineMap = lsconv::ComputeLSPLineStarts(originalText);
	auto* positionMap = computePositionMap(originalText);
	std::vector<std::unique_ptr<TextEdit>> result(edits.size());
	for (size_t i = 0; i < edits.size(); i++) {
		const auto& edit = edits[i];
		auto [start, ok] =
		    originalTextOffset(lineMap, edit.range.Start,
		                       int(originalText.size()));
		if (!ok) {
			return {};
		}
		auto [end, ok2] =
		    originalTextOffset(lineMap, edit.range.End,
		                       int(originalText.size()));
		if (!ok2) {
			return {};
		}
		auto e = std::make_unique<TextEdit>();
		e->Pos = positionMap->UTF8ToUTF16(start);
		e->End = positionMap->UTF8ToUTF16(end);
		e->NewText = edit.newText;
		result[i] = std::move(e);
	}
	return result;
}

std::pair<int, bool> originalTextOffset(lsconv::LSPLineMap* lineMap,
                                        lsproto::Position position,
                                        int textLength) {
	int line = int(position.Line);
	if (line < 0 || line >= int(lineMap->LineStarts.size())) {
		return {0, false};
	}
	int offset =
	    int(lineMap->LineStarts[line]) + int(position.Character);
	if (offset < int(lineMap->LineStarts[line]) || offset > textLength) {
		return {0, false};
	}
	return {offset, true};
}

// handleGetReferencesToSymbolInFile returns node handles for all identifiers in a file that reference the given symbol.
std::pair<std::vector<NodeHandle>, gostd::Error>
Session::handleGetReferencesToSymbolInFile(
    gostd::Context ctx, const GetReferencesToSymbolInFileParams* params) {
	auto [setup, err] = setupChecker(ctx, params->Snapshot, params->Project);
	if (err) {
		return {std::vector<NodeHandle>{}, err};
	}
	deferGuard _done{setup.done};

	auto [symbol, err2] = setup.resolveSymbolHandle(params->Symbol);
	if (err2) {
		return {std::vector<NodeHandle>{}, err2};
	}
	if (symbol == nullptr) {
		return {std::vector<NodeHandle>{}, nullptr};
	}

	auto* sourceFile = setup.program->GetSourceFile(params->File.ToFileName());
	if (sourceFile == nullptr) {
		return {std::vector<NodeHandle>{},
		        gostd::errorf("%w: source file not found: %v",
		                      {ErrClientError, params->File.ToFileName()})};
	}

	auto nodes =
	    setup.checker->GetReferencesToSymbolInFile(sourceFile, symbol);
	std::vector<NodeHandle> result(nodes.size());
	for (size_t i = 0; i < nodes.size(); i++) {
		result[i] = setup.sd->nodeHandleFrom(nodes[i]);
	}
	return {std::move(result), nullptr};
}

// @gen-proto-nullable
std::pair<std::vector<SignatureUsageResponse>, gostd::Error>
Session::handleGetSignatureUsages(
    gostd::Context ctx, const GetSignatureUsagesParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<SignatureUsageResponse>{}, err};
	}
	auto [program, err2] = sd->getProgram(params->Project);
	if (err2) {
		return {std::vector<SignatureUsageResponse>{}, err2};
	}

	auto [signatureDecl, err3] =
	    sd->resolveNodeHandle(program, params->SignatureDecl);
	if (err3) {
		return {std::vector<SignatureUsageResponse>{}, err3};
	}
	if (signatureDecl == nullptr) {
		return {std::vector<SignatureUsageResponse>{}, nullptr};
	}

	auto [langSvc, err4] = setupLanguageService(sd->snapshot, program,
	                                          params->Project, "");
	if (err4) {
		return {std::vector<SignatureUsageResponse>{}, err4};
	}

	auto usages = langSvc->GetSignatureUsages(ctx, signatureDecl);
	if (usages.empty()) {
		return {std::vector<SignatureUsageResponse>{}, nullptr};
	}

	std::vector<SignatureUsageResponse> result;
	for (const auto& u : usages) {
		SignatureUsageResponse entry;
		entry.Name = sd->nodeHandleFrom(u.Name);
		if (u.Call != nullptr) {
			entry.Call = sd->nodeHandleFrom(u.Call);
		}
		result.push_back(std::move(entry));
	}
	return {std::move(result), nullptr};
}

// handleGetCompletionsAtPosition returns completions at a position in a document.
// @gen-proto-nullable
std::pair<std::unique_ptr<CompletionInfoResponse>, gostd::Error>
Session::handleGetCompletionsAtPosition(
    gostd::Context ctx, const GetCompletionsAtPositionParams* params) {
	if (params->IncludeSymbol) {
		// Go: core.WithCheckerLifetime(ctx, core.CheckerLifetimeAPI) — no
		// C++ equivalent (single lazily-created checker); see setupChecker.
	}
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {nullptr, err};
	}
	auto run = [&](project::Snapshot* snapshot,
	               compiler::SimpleProgram* program)
	    -> std::pair<ls::CompletionList*, gostd::Error> {
		auto* sourceFile =
		    program->GetSourceFile(params->File.ToFileName());
		if (sourceFile == nullptr) {
			return {nullptr, nullptr};
		}
		auto [langSvc, e] = setupLanguageService(snapshot, program,
		                                       params->Project, "");
		if (e) {
			return {nullptr, e};
		}
		int internalPos = sourceFile->GetPositionMap()->UTF16ToUTF8(
		    int(params->Position));
		return langSvc->GetCompletionsAtPosition(
		    ctx, sourceFile, internalPos,
		    params->TriggerCharacter ? &*params->TriggerCharacter : nullptr,
		    params->IncludeSymbol);
	};

	auto [program, err2] = sd->getProgram(params->Project);
	if (err2) {
		return {nullptr, err2};
	}
	auto [result, runErr] = run(sd->snapshot, program);
	if (gostd::errorIs(runErr, ls::ErrNeedsAutoImports)) {
		auto* preparedSnapshot = snapshotHost->CloneSnapshotWithAutoImports(
		    ctx, sd->snapshot,
		    params->File.ToURI(GetCurrentDirectory()), nullptr);
		if (projectSession != nullptr) {
			projectSession->TryAdoptSnapshotInBackground(sd->snapshot,
			                                           preparedSnapshot);
		}
		deferGuard _deref{[preparedSnapshot] { preparedSnapshot->Deref(); }};
		if (auto cerr = gostd::ctxErr(ctx)) {
			return {nullptr, cerr};
		}
		auto projectID = params->Project;
		auto* proj = preparedSnapshot->ProjectCollection->GetProject(projectID);
		if (proj == nullptr) {
			return {nullptr,
			        gostd::errorf("%w: project %s not found",
			                      {ErrClientError, projectID.v})};
		}
		program = proj->GetProgram();
		if (program == nullptr) {
			return {nullptr,
			        gostd::errorf("%w: project has no program",
			                      {ErrClientError})};
		}
		std::tie(result, runErr) = run(preparedSnapshot, program);
	}
	if (runErr || result == nullptr) {
		return {nullptr, runErr};
	}
	std::vector<std::shared_ptr<CompletionEntryResponse>> entries;
	entries.reserve(result->Items.size());
	for (auto& item : result->Items) {
		auto entry = std::make_shared<CompletionEntryResponse>();
		entry->Name = item->Label;
		if (!item->SortText.empty()) {
			entry->SortText = item->SortText;
		}
		if (!item->InsertText.empty()) {
			entry->InsertText = item->InsertText;
		}
		if (!item->FilterText.empty()) {
			entry->FilterText = item->FilterText;
		}
		if (!item->Detail.empty()) {
			entry->Detail = item->Detail;
		}
		if (item->hasKind) {
			entry->Kind = uint32_t(item->Kind);
		}
		if (item->LabelDetails != nullptr) {
			auto ld = std::make_shared<CompletionEntryLabelDetailsResponse>();
			ld->Detail = item->LabelDetails->Detail;
			if (!item->LabelDetails->Description.empty()) {
				ld->Description = item->LabelDetails->Description;
			}
			entry->LabelDetails = std::move(ld);
		}
		if (item->Symbol != nullptr) {
			entry->Symbol = std::shared_ptr<SymbolResponse>(
			    sd->newSymbolResponse(item->Symbol, params->Project)
			        .release());
		}
		entries.push_back(std::move(entry));
	}
	auto resp = std::make_unique<CompletionInfoResponse>();
	resp->IsIncomplete = result->IsIncomplete;
	resp->Entries = std::move(entries);
	return {std::move(resp), nullptr};
}

// handleGetReferencedSymbolsForNode returns node handles for all references found at a node.
// @gen-proto-nullable
std::pair<std::vector<ReferencedSymbolEntry>, gostd::Error>
Session::handleGetReferencedSymbolsForNode(
    gostd::Context ctx, const GetReferencedSymbolsForNodeParams* params) {
	auto [sd, err] = getSnapshotData(params->Snapshot);
	if (err) {
		return {std::vector<ReferencedSymbolEntry>{}, err};
	}
	auto [program, err2] = sd->getProgram(params->Project);
	if (err2) {
		return {std::vector<ReferencedSymbolEntry>{}, err2};
	}

	auto [node, err3] = sd->resolveNodeHandle(program, params->Node);
	if (err3) {
		return {std::vector<ReferencedSymbolEntry>{}, err3};
	}
	if (node == nullptr) {
		return {std::vector<ReferencedSymbolEntry>{}, nullptr};
	}

	auto [langSvc, err4] = setupLanguageService(sd->snapshot, program,
	                                          params->Project, "");
	if (err4) {
		return {std::vector<ReferencedSymbolEntry>{}, err4};
	}

	auto sourceFiles = program->GetSourceFiles();
	auto entries = langSvc->GetReferencedSymbolsForNode(ctx, params->Position,
	                                                  node, sourceFiles);
	if (entries.empty()) {
		return {std::vector<ReferencedSymbolEntry>{}, nullptr};
	}

	std::vector<ReferencedSymbolEntry> result;
	for (auto* entry : entries) {
		auto* defNode = entry->DefinitionNode();
		if (defNode == nullptr) {
			continue;
		}
		std::vector<NodeHandle> refs;
		for (auto* ref : entry->References()) {
			if (ref->IsNodeEntry()) {
				refs.push_back(sd->nodeHandleFrom(ref->Node()));
			}
		}
		ReferencedSymbolEntry re;
		re.Definition = sd->nodeHandleFrom(defNode);
		re.References = std::move(refs);
		if (auto* sym = entry->DefinitionSymbol(); sym != nullptr) {
			re.Symbol = std::shared_ptr<SymbolResponse>(
			    sd->newSymbolResponse(sym, params->Project).release());
		}
		result.push_back(std::move(re));
	}
	return {std::move(result), nullptr};
}

} // namespace tsc::api
