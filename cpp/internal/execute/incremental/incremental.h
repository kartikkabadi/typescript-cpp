#pragma once

// Port of tsc/internal/execute/incremental — incremental build state
// (snapshot), .tsbuildinfo (BuildInfo) serialization, affected-files
// tracking, and the incremental Program wrapper that owns emitBuildInfo.
//
// Go context.Context params are dropped throughout (the port is
// single-threaded); Go `error` returns are std::optional<std::string>
// (nullopt == nil) and iter.Seq values are yield-callback parameters.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/packagejson/packagejson.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace tsc::execute::incremental {

// ===========================================================================
// snapshot.go — FileInfo / FileEmitKind / emitSignature
// ===========================================================================

// snapshot.go:16 FileInfo — per-file version + emit signature.
struct FileInfo {
	std::string version;
	std::string signature;
	bool affectsGlobalScope = false;
	ResolutionMode impliedNodeFormat = ResolutionModeNone;

	std::string_view Version() const { return version; }
	std::string_view Signature() const { return signature; }
	bool AffectsGlobalScope() const { return affectsGlobalScope; }
	ResolutionMode ImpliedNodeFormat() const { return impliedNodeFormat; }
};

// snapshot.go:79 ComputeHash — xxh3 128-bit hash of text (Go
// xxh3.HashString128), hex-encoded; with hashWithText the "text" is
// appended after a ':' so mismatched hashes can be diffed.
std::string ComputeHash(std::string_view text, bool hashWithText);

// snapshot.go:389 getTextHandlingSourceMapForSignature — package-internal.
std::string_view getTextHandlingSourceMapForSignature(
    std::string_view text, const compiler::WriteFileData* data);


// snapshot.go:90 FileEmitKind — bitmask of pending emit outputs.
using FileEmitKind = uint32_t;
inline constexpr FileEmitKind FileEmitKindNone = 0;
inline constexpr FileEmitKind FileEmitKindJs = 1 << 0;
inline constexpr FileEmitKind FileEmitKindJsMap = 1 << 1;
inline constexpr FileEmitKind FileEmitKindJsInlineMap = 1 << 2;
inline constexpr FileEmitKind FileEmitKindDtsErrors = 1 << 3;
inline constexpr FileEmitKind FileEmitKindDtsEmit = 1 << 4;
inline constexpr FileEmitKind FileEmitKindDtsMap = 1 << 5;
inline constexpr FileEmitKind FileEmitKindDts =
    FileEmitKindDtsErrors | FileEmitKindDtsEmit;
inline constexpr FileEmitKind FileEmitKindAllJs =
    FileEmitKindJs | FileEmitKindJsMap | FileEmitKindJsInlineMap;
inline constexpr FileEmitKind FileEmitKindAllDtsEmit =
    FileEmitKindDtsEmit | FileEmitKindDtsMap;
inline constexpr FileEmitKind FileEmitKindAllDts =
    FileEmitKindDts | FileEmitKindDtsMap;
inline constexpr FileEmitKind FileEmitKindAll =
    FileEmitKindAllJs | FileEmitKindAllDts;

// snapshot.go:103 GetFileEmitKind.
FileEmitKind GetFileEmitKind(const CompilerOptions* options);

// snapshot.go:133 getPendingEmitKindWithOptions.
FileEmitKind getPendingEmitKindWithOptions(
    const CompilerOptions* options, const CompilerOptions* oldOptions);

// snapshot.go:151 getPendingEmitKind.
FileEmitKind getPendingEmitKind(FileEmitKind emitKind,
                                FileEmitKind oldEmitKind);

// snapshot.go:193 emitSignature — the file's stored signature plus
// signatures computed for older options.
struct emitSignature {
	std::string signature;
	// signatureWithDifferentOptions — Go []string; std::nullopt == nil.
	std::optional<std::vector<std::string>> signatureWithDifferentOptions;

	// snapshot.go:198.
	emitSignature* getNewEmitSignature(const CompilerOptions* oldOptions,
	                                   const CompilerOptions* newOptions);
};

// ===========================================================================
// snapshot.go — buildInfoDiagnosticWithFileName /
// DiagnosticsOrBuildInfoDiagnosticsWithFileName
// ===========================================================================

struct buildInfoDiagnosticWithFileName {
	tspath::Path file;
	bool noFile = false;
	int pos = 0;
	int end = 0;
	int32_t code = 0;
	DiagnosticCategory category = DiagnosticCategory::Error;
	std::string source;
	std::string messageText;
	std::string messageKey;
	std::vector<std::string> messageArgs;
	std::vector<buildInfoDiagnosticWithFileName*> messageChain;
	std::vector<buildInfoDiagnosticWithFileName*> relatedInformation;
	bool reportsUnnecessary = false;
	bool reportsDeprecated = false;
	bool skippedOnNoEmit = false;
	RepopulateDiagnosticInfo* repopulateInfo = nullptr;

	// snapshot.go:284 toDiagnostic.
	Diagnostic* toDiagnostic(compiler::SimpleProgram* p, SourceFile* file);
	// snapshot.go:364 toDiagnosticWithoutRepopulate.
	Diagnostic* toDiagnosticWithoutRepopulate(compiler::SimpleProgram* p,
	                                          SourceFile* file);
};

struct DiagnosticsOrBuildInfoDiagnosticsWithFileName {
	std::vector<Diagnostic*> diagnostics;
	std::vector<buildInfoDiagnosticWithFileName*> buildInfoDiagnostics;

	// snapshot.go:402 getDiagnostics — caches into `diagnostics` like Go.
	std::vector<Diagnostic*> getDiagnostics(compiler::SimpleProgram* p,
	                                        SourceFile* file);
};

// ===========================================================================
// referencemap.go
// ===========================================================================

// referencemap.go:15 referenceMap — per-file set of referenced files plus a
// lazily built reverse index.
class referenceMap {
	collections::SyncMap<tspath::Path, collections::Set<tspath::Path>*>
	    references;
	std::unordered_map<tspath::Path, collections::Set<tspath::Path>*>
	    referencedBy;
	OnceFlag referenceByOnce;

public:
	// referencemap.go:22 storeReferences.
	void storeReferences(const tspath::Path& path,
	                     collections::Set<tspath::Path>* refs);
	// referencemap.go:27 getReferences — {refs, loaded}.
	std::pair<collections::Set<tspath::Path>*, bool> getReferences(
	    const tspath::Path& path);
	// referencemap.go:31 getPathsWithReferences.
	std::vector<tspath::Path> getPathsWithReferences();
	// referencemap.go:37 getReferencedBy — yields each referencing path
	// (iter.Seq).
	void getReferencedBy(
	    const tspath::Path& path,
	    const std::function<bool(const tspath::Path&)>& yield);
};

// ===========================================================================
// snapshot.go — snapshot
// ===========================================================================

struct snapshot {
	// ==== fields serialized to .tsbuildinfo ====
	collections::SyncMap<tspath::Path, FileInfo*> fileInfos;
	const CompilerOptions* options = nullptr;
	referenceMap referencedMap;
	collections::SyncMap<tspath::Path,
	                     DiagnosticsOrBuildInfoDiagnosticsWithFileName*>
	    semanticDiagnosticsPerFile;
	collections::SyncMap<tspath::Path,
	                     DiagnosticsOrBuildInfoDiagnosticsWithFileName*>
	    emitDiagnosticsPerFile;
	collections::SyncSet<tspath::Path> changedFilesSet;
	collections::SyncMap<tspath::Path, FileEmitKind> affectedFilesPendingEmit;
	std::string latestChangedDtsFile;
	collections::SyncMap<tspath::Path, emitSignature*> emitSignatures;
	Tristate hasErrors = Tristate::Unknown;
	bool hasSemanticErrors = false;
	bool checkPending = false;
	// packageJsons / missingPackageJsons — Go []string; std::nullopt == nil.
	std::optional<std::vector<std::string>> packageJsons;
	std::optional<std::vector<std::string>> missingPackageJsons;

	// ==== transient state ====
	std::atomic<bool> buildInfoEmitPending{false};
	Tristate hasErrorsFromOldState = Tristate::Unknown;
	bool hasSemanticErrorsFromOldState = false;
	OnceFlag allFilesExcludingDefaultLibraryFileOnce;
	std::vector<std::string> packageJsonsFromOldState;
	std::vector<std::string> missingPackageJsonsFromOldState;
	std::vector<SourceFile*> allFilesExcludingDefaultLibraryFile;
	bool hasChangedDtsFile = false;
	bool hasEmitDiagnostics = false;
	bool hashWithText = false;

	snapshot() = default;
	snapshot(const snapshot&) = delete;
	snapshot& operator=(const snapshot&) = delete;

	// snapshot.go:473 addFileToChangeSet.
	void addFileToChangeSet(const tspath::Path& filePath);
	// snapshot.go:477 addFileToAffectedFilesPendingEmit.
	void addFileToAffectedFilesPendingEmit(const tspath::Path& filePath,
	                                       FileEmitKind emitKind);
	// snapshot.go:496 getAllFilesExcludingDefaultLibraryFile.
	std::vector<SourceFile*> getAllFilesExcludingDefaultLibraryFile(
	    compiler::SimpleProgram* program, SourceFile* firstSourceFile);
	// snapshot.go:548 computeSignatureWithDiagnostics.
	std::string computeSignatureWithDiagnostics(
	    SourceFile* file, std::string_view text,
	    const compiler::WriteFileData* data);
	// snapshot.go:596 computeHash.
	std::string computeHash(std::string_view text);
	// snapshot.go:606 canUseIncrementalState.
	bool canUseIncrementalState() const;
};

// ===========================================================================
// buildInfo.go — .tsbuildinfo data model
// ===========================================================================

using BuildInfoFileId = int;
using BuildInfoFileIdListId = int;

// buildInfo.go:30 BuildInfoRoot — JSON: start | [start,end] | string.
struct BuildInfoRoot {
	BuildInfoFileId Start = 0;
	BuildInfoFileId End = 0;
	std::string NonIncremental;
};

struct buildInfoFileInfoNoSignature {
	std::string version;
	bool noSignature = false;
	bool affectsGlobalScope = false;
	ResolutionMode impliedNodeFormat = ResolutionModeNone;
};

struct buildInfoFileInfoWithSignature {
	std::string version;
	std::string signature;
	bool affectsGlobalScope = false;
	ResolutionMode impliedNodeFormat = ResolutionModeNone;
};

// buildInfo.go:84 BuildInfoFileInfo — JSON: signature string, or a
// {"version","noSignature",...} object, or {"version","signature",...}.
struct BuildInfoFileInfo {
	std::string signature;
	buildInfoFileInfoNoSignature* noSignature = nullptr;
	buildInfoFileInfoWithSignature* fileInfo = nullptr;

	// buildInfo.go:127 GetFileInfo — allocates a fresh FileInfo (Go
	// *FileInfo).
	FileInfo* GetFileInfo() const;
	// buildInfo.go:154 HasSignature.
	bool HasSignature() const;
};

// buildInfo.go:162 BuildInfoReferenceMapEntry — JSON [fileId,fileIdListId].
struct BuildInfoReferenceMapEntry {
	BuildInfoFileId FileId = 0;
	BuildInfoFileIdListId FileIdListId = 0;
};

// buildInfo.go:205 BuildInfoRepopulateInfo — JSON {"kind",...}.
struct BuildInfoRepopulateInfo {
	RepopulateDiagnosticKind Kind = RepopulateDiagnosticKind::None;
	std::string ModuleReference;
	ResolutionMode Mode = ResolutionModeNone;
	std::string PackageName;
};

// buildInfo.go:213 BuildInfoDiagnostic — flat serialized diagnostic.
struct BuildInfoDiagnostic {
	BuildInfoFileId File = 0;
	bool NoFile = false;
	int Pos = 0;
	int End = 0;
	int32_t Code = 0;
	DiagnosticCategory Category = DiagnosticCategory::Warning;
	std::string Source;
	std::string MessageText;
	std::string MessageKey;
	std::vector<std::string> MessageArgs;
	std::vector<BuildInfoDiagnostic*> MessageChain;
	std::vector<BuildInfoDiagnostic*> RelatedInformation;
	bool ReportsUnnecessary = false;
	bool ReportsDeprecated = false;
	bool SkippedOnNoEmit = false;
	BuildInfoRepopulateInfo* RepopulateInfo = nullptr;
};

// buildInfo.go:263 BuildInfoDiagnosticsOfFile — JSON [fileId, [diags]].
struct BuildInfoDiagnosticsOfFile {
	BuildInfoFileId FileId = 0;
	std::vector<BuildInfoDiagnostic*> Diagnostics;
};

// buildInfo.go:303 BuildInfoSemanticDiagnostic — JSON fileId | [fileId,
// diags].
struct BuildInfoSemanticDiagnostic {
	BuildInfoFileId FileId = 0;
	BuildInfoDiagnosticsOfFile* Diagnostics = nullptr;
};

// buildInfo.go:329 BuildInfoFilePendingEmit — JSON fileId | [fileId] |
// [fileId, emitKind].
struct BuildInfoFilePendingEmit {
	BuildInfoFileId FileId = 0;
	FileEmitKind EmitKind = 0;
};

// buildInfo.go:356 BuildInfoEmitSignature — JSON fileId | [fileId,
// signature | [] | [signature]].
struct BuildInfoEmitSignature {
	BuildInfoFileId FileId = 0;
	std::string Signature;
	bool DiffersOnlyInDtsMap = false;
	bool DiffersInOptions = false;

	// buildInfo.go:377 noEmitSignature.
	bool noEmitSignature() const;
	// buildInfo.go:381 toEmitSignature — inserts into emitSignatures like Go.
	struct emitSignature* toEmitSignature(
	    const tspath::Path& path,
	    collections::SyncMap<tspath::Path, emitSignature*>* emitSignatures);
};

// buildInfo.go:439 BuildInfoResolvedRoot — JSON [resolved, root].
struct BuildInfoResolvedRoot {
	BuildInfoFileId Resolved = 0;
	BuildInfoFileId Root = 0;
};

// buildInfo.go:510 BuildInfo — the .tsbuildinfo payload.
struct BuildInfo {
	std::string Version;
	bool Errors = false;
	bool CheckPending = false;
	std::vector<BuildInfoRoot*> Root;
	std::vector<std::string> PackageJsons;
	std::vector<std::string> MissingPackageJsons;
	std::vector<std::string> ContentMapperIdentities;
	std::vector<std::string> FileNames;
	// Go marshals fileInfos with `omitzero`: a nil slice is omitted but
	// an assigned empty slice still prints as `"fileInfos":[]`. This flag
	// records "was assigned" (setFileInfoAndEmitSignatures or unmarshal).
	bool fileInfosAssigned = false;
	std::vector<BuildInfoFileInfo*> FileInfos;
	std::vector<std::vector<BuildInfoFileId>> FileIdsList;
	tsoptions::JsonObjectPtr Options;
	std::vector<BuildInfoReferenceMapEntry*> ReferencedMap;
	std::vector<BuildInfoSemanticDiagnostic*> SemanticDiagnosticsPerFile;
	std::vector<BuildInfoDiagnosticsOfFile*> EmitDiagnosticsPerFile;
	std::vector<BuildInfoFileId> ChangeFileSet;
	std::vector<BuildInfoFilePendingEmit*> AffectedFilesPendingEmit;
	std::string LatestChangedDtsFile;
	std::vector<BuildInfoEmitSignature*> EmitSignatures;
	std::vector<BuildInfoResolvedRoot*> ResolvedRoot;
	bool SemanticErrors = false;

	// buildInfo.go:599 IsValidVersion.
	bool IsValidVersion() const;
	// buildInfo.go:603 ContentMapperIdentitiesMatch.
	bool ContentMapperIdentitiesMatch(
	    const std::vector<std::string>& currentIdentities) const;
	// buildInfo.go:607 IsIncremental — fileNames non-empty.
	bool IsIncremental() const { return !FileNames.empty(); }
	// buildInfo.go:611 fileName.
	std::string fileName(BuildInfoFileId fileId) const;
	// buildInfo.go:615 fileInfo.
	BuildInfoFileInfo* fileInfo(BuildInfoFileId fileId) const;
	// buildInfo.go:619 GetCompilerOptions — heap CompilerOptions like Go.
	CompilerOptions* GetCompilerOptions(
	    const std::string& buildInfoDirectory) const;
	// buildInfo.go:638 IsEmitPending.
	bool IsEmitPending(tsoptions::ParsedCommandLine* resolved,
	                   const std::string& buildInfoDirectory);
	// buildInfo.go:666 GetPackageJsons — yields package-json paths
	// (iter.Seq).
	void GetPackageJsons(
	    const std::string& buildInfoDirectory,
	    const std::function<bool(const std::string&)>& yield) const;
	// buildInfo.go:670 GetMissingPackageJsons — yields paths (iter.Seq).
	void GetMissingPackageJsons(
	    const std::string& buildInfoDirectory,
	    const std::function<bool(const std::string&)>& yield) const;
	// buildInfo.go:674 GetBuildInfoRootInfoReader — heap result like Go.
	struct BuildInfoRootInfoReader* GetBuildInfoRootInfoReader(
	    const std::string& buildInfoDirectory,
	    const tspath::ComparePathsOptions& comparePathOptions) const;
};

// buildInfo.go:711 BuildInfoRootInfoReader.
struct BuildInfoRootInfoReader {
	std::unordered_map<tspath::Path, BuildInfoFileInfo*>
	    resolvedRootFileInfos;
	collections::OrderedMap<tspath::Path, tspath::Path> rootToResolved;

	// buildInfo.go:717 GetBuildInfoFileInfo — {fileInfo, resolvedPath}.
	std::pair<BuildInfoFileInfo*, tspath::Path> GetBuildInfoFileInfo(
	    const tspath::Path& inputFilePath) const;
	// buildInfo.go:735 Roots — yields resolved-root paths (iter.Seq).
	void Roots(const std::function<bool(const tspath::Path&)>& yield) const;
};

// buildInfo.go:741 ContentMapperIdentities — Go error → gostd::Error (the
// typed value ContentMapperProjectDiagnostic inspects).
std::pair<std::vector<std::string>, gostd::Error>
ContentMapperIdentities(contentmapper::Project* project);

// buildInfo.go:747 IsBuildInfoFileNameDefaultLibrary.
inline bool IsBuildInfoFileNameDefaultLibrary(
    const std::string& fileName) {
	return !tspath::pathIsRelative(fileName) &&
	       !tspath::pathIsAbsolute(fileName);
}

// Marshal/unmarshal — Go encoding/json/v2 output for BuildInfo (the
// `MarshalJSON`/`UnmarshalJSON` variants and `,omitzero` elisions are
// reproduced in buildinfo.cpp).
// buildInfo.go:97 newBuildInfoFileInfo - package-internal.
BuildInfoFileInfo* newBuildInfoFileInfo(FileInfo* fileInfo);

std::string marshalBuildInfo(const BuildInfo* buildInfo);
BuildInfo* unmarshalBuildInfo(std::string_view data);

// ===========================================================================
// host.go / incremental.go
// ===========================================================================

// host.go:17 Host — the Go incremental Host interface: FS + mtime.
class Host {
public:
	virtual ~Host() = default;
	virtual compiler::CompilerHost* FS() = 0;
	virtual std::filesystem::file_time_type GetMTime(
	    const std::string& fileName) = 0;
	virtual std::optional<std::string> SetMTime(
	    const std::string& fileName,
	    std::filesystem::file_time_type mTime) = 0;
};

// host.go:46 CreateHost.
Host* CreateHost(compiler::CompilerHost* compilerHost);

// host.go:58 GetMTime — os.Stat mtime; zero file_time_type on error.
std::filesystem::file_time_type GetMTime(compiler::CompilerHost* host,
                                         const std::string& fileName);

// incremental.go:14 BuildInfoReader.
class BuildInfoReader {
public:
	virtual ~BuildInfoReader() = default;
	virtual BuildInfo* ReadBuildInfo(tsoptions::ParsedCommandLine* config) = 0;
};

// incremental.go:33 NewBuildInfoReader.
BuildInfoReader* NewBuildInfoReader(compiler::CompilerHost* host);

class Program;

// incremental.go:39 ReadBuildInfoProgram.
Program* ReadBuildInfoProgram(tsoptions::ParsedCommandLine* config,
                              BuildInfoReader* reader,
                              compiler::CompilerHost* host);

// ===========================================================================
// program.go — incremental Program
// ===========================================================================

// program.go:42 SignatureUpdateKind — TestingData.UpdatedSignatureKinds.
enum class SignatureUpdateKind : uint8_t {
	ComputedDts = 0,
	StoredAtEmit,
	UsedVersion,
};

// program.go:112 TestingData — the test-visible per-file diagnostics maps.
struct TestingData {
	collections::SyncMap<tspath::Path,
	                     DiagnosticsOrBuildInfoDiagnosticsWithFileName*>*
	    SemanticDiagnosticsPerFile = nullptr;
	collections::SyncMap<tspath::Path,
	                     DiagnosticsOrBuildInfoDiagnosticsWithFileName*>*
	    OldProgramSemanticDiagnosticsPerFile = nullptr;
	std::unordered_map<tspath::Path, SignatureUpdateKind>
	    UpdatedSignatureKinds;
};

class Program : public compiler::ProgramLike {
public:
	incremental::snapshot* snapshot_ = nullptr;
	compiler::SimpleProgram* program_ = nullptr;
	Host* host_ = nullptr;
	TestingData* testingData_ = nullptr;

	// Go nestedEmit* fields — nested-emit timing instrumentation.
	std::mutex nestedEmitMu;
	std::function<std::chrono::system_clock::time_point()> nestedEmitNow;
	int nestedEmitDepth = 0;
	std::chrono::system_clock::time_point nestedEmitStart;
	std::chrono::nanoseconds nestedEmitTime{0};

	// program.go:31 — plain fields; construction is in NewProgram (Go
	// `&Program{...}` struct literal — buildInfoToSnapshot's path creates
	// one with only snapshot set).
	Program() = default;

	// program.go:178 testingData — allocates like Go
	// (p.testingData ||= &TestingData{}).
	struct TestingData* testingData();
	TestingData* GetTestingData(); // program.go:185 — may return nullptr.

	void panicIfNoProgram(const char* method);
	// Go's `Program()` is spelled `GetProgram()` in C++ (a member named
	// `Program` inside `class Program` would be a constructor). Go's
	// `GetProgram()` is the same accessor — one method covers both.
	compiler::SimpleProgram* GetProgram() override;
	bool HasChangedDtsFile();

	// --- compiler::ProgramLike ---
	const CompilerOptions* Options() override;
	SourceFile* GetSourceFile(const std::string& path) override;
	std::vector<SourceFile*> GetSourceFiles() override;
	std::vector<Diagnostic*> GetConfigFileParsingDiagnostics() override;
	std::vector<Diagnostic*> GetSyntacticDiagnostics(
	    SourceFile* file) override;
	std::vector<Diagnostic*> GetBindDiagnostics(SourceFile* file) override;
	std::vector<Diagnostic*> GetProgramDiagnostics() override;
	std::vector<Diagnostic*> GetGlobalDiagnostics() override;
	std::vector<Diagnostic*> GetSemanticDiagnostics(
	    SourceFile* file) override;
	std::vector<Diagnostic*> GetDeclarationDiagnostics(
	    SourceFile* file) override;
	std::vector<Diagnostic*> GetSuggestionDiagnostics(
	    SourceFile* file) override;
	compiler::EmitResult* Emit(compiler::EmitOptions* options) override;
	std::string CommonSourceDirectory() override;
	bool IsSourceFileDefaultLibrary(const tspath::Path& path) const override;

	// program.go:265 beginNestedEmit — noop closure when nestedEmitNow is
	// unset.
	std::function<void()> beginNestedEmit();
	// program.go:285 TakeNestedEmitTime.
	std::chrono::nanoseconds TakeNestedEmitTime();

	// program.go:294 collectSemanticDiagnosticsOfAffectedFiles.
	void collectSemanticDiagnosticsOfAffectedFiles(SourceFile* file);
	// program.go:301 getSemanticDiagnosticsOfFile.
	std::vector<Diagnostic*> getSemanticDiagnosticsOfFile(SourceFile* file);
	// program.go:352 emitBuildInfo.
	compiler::EmitResult* emitBuildInfo(compiler::EmitOptions* options);
	// program.go:424 ensureHasErrorsForState.
	void ensureHasErrorsForState(compiler::SimpleProgram* program);
	// program.go:435 ensurePackageJsonsForState.
	void ensurePackageJsonsForState();
	// program.go:459 PackageJsonLookupPaths.
	std::vector<std::string> PackageJsonLookupPaths();
};

// program.go:48 NewProgram.
Program* NewProgram(
    compiler::SimpleProgram* program, Program* oldProgram, Host* host,
    std::function<std::chrono::system_clock::time_point()> nestedEmitNow,
    bool testing);

// ===========================================================================
// cross-file package-internal entry points (Go: same-package calls)
// ===========================================================================

// programtosnapshot.go:16 programToSnapshot — `hashWithText` is Go's
// testing flag (hashes carry their text for comparison).
snapshot* programToSnapshot(compiler::SimpleProgram* program,
                            Program* oldProgram, bool hashWithText);

// buildinfotosnapshot.go:16 buildInfoToSnapshot.
snapshot* buildInfoToSnapshot(BuildInfo* buildInfo,
                              tsoptions::ParsedCommandLine* resolved,
                              compiler::CompilerHost* host);

// snapshottobuildinfo.go:16 snapshotToBuildInfo — {buildInfo, err}.
std::pair<BuildInfo*, gostd::Error> snapshotToBuildInfo(
    snapshot* s, compiler::SimpleProgram* program,
    std::string_view buildInfoFileName);

// affectedfileshandler.go:245 collectAllAffectedFiles.
void collectAllAffectedFiles(Program* program);

// emitfileshandler.go:371 emitFiles.
compiler::EmitResult* emitFiles(Program* p,
                                const compiler::EmitOptions& options,
                                bool isForDtsErrors);

// snapshot.go:199 repopulateDiagnosticChain — package-internal, shared
// with programtosnapshot.cpp's diagnostic repopulation.
Diagnostic* repopulateDiagnosticChain(
    buildInfoDiagnosticWithFileName* b, compiler::SimpleProgram* p,
    SourceFile* file);

// snapshot.go:236 / :263 repopulate helpers (package-internal).
Diagnostic* repopulateModeMismatchChain(
    buildInfoDiagnosticWithFileName* b, compiler::SimpleProgram* p,
    SourceFile* file);
Diagnostic* repopulateModuleNotFoundChain(
    buildInfoDiagnosticWithFileName* b, compiler::SimpleProgram* p,
    SourceFile* file, RepopulateDiagnosticInfo* info);

}  // namespace tsc::execute::incremental
