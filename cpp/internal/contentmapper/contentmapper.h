// contentmapper.h — port of tsc/internal/contentmapper.
//
// Package contentmapper defines the types describing an external content
// mapper: a plugin that transforms otherwise unsupported file content (e.g.
// .vue) into virtual TypeScript during program construction.
//
// A mapper is declared in tsconfig (Definition), its implementation is
// described by fields in its npm package's package.json (Manifest), and the
// two are combined once the package is resolved (Mapper). Resolution itself
// lives in the tsoptions package (it needs node module resolution).
//
// The package also drives the configured content mappers at build time
// (Host): it spawns each mapper's package as a child process and talks to it
// over a JSON-RPC connection (reusing internal/ipc), turning content-mapped
// source files into virtual TypeScript. Processes are consolidated by mapper
// identity, so many projects that use the same mapper version share a single
// process.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/gostd/gostd.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/locale/locale.h"
#include "internal/spanmap/spanmap.h"
#include "internal/xxh3/xxh3.h"

namespace tsc::contentmapper {

// ---------------------------------------------------------------------------
// contentmapper.go
// ---------------------------------------------------------------------------

inline const gostd::Error ErrProjectUnavailable =
	gostd::newError("content mapper project is unavailable");

// Definition is a content mapper as declared in a tsconfig's
// "contentMappers": the npm package that implements the mapper and the
// otherwise unsupported file extensions it registers.
struct Definition {
	std::string Package;                  // `json:"package"`
	std::vector<std::string> Extensions;  // `json:"extensions"`
	json::Value Options;                  // `json:"options,omitempty"`
};

// Manifest is the content-mapper information read from a package's
// package.json: its name and version (which form the mapper's identity), the
// argv used to run it, and the compiler options it declares it depends on.
// (Named members — the C++ port uses fields named Definition and Manifest in
// place of Go's embedded fields.)
struct Manifest {
	std::string Name;
	std::string Version;
	std::vector<std::string> Exec;
	std::vector<std::string> CompilerOptions;
	bool DynamicConfig = false;
};

// Mapper is a resolved content mapper: its tsconfig Definition combined with
// the Manifest resolved from the package's package.json, plus the package
// directory used as the mapper's working directory.
struct Mapper {
	::tsc::contentmapper::Definition Definition;
	::tsc::contentmapper::Manifest Manifest; // `json:"-"`
	// PackageDirectory is the real path directory returned by package
	// resolution for package-based mappers.
	std::string PackageDirectory; // `json:"-"`
	// ContributionID is provided by an LSP client extension for inferred
	// project content mappers.
	std::string ContributionID; // `json:"-"`

	// DiagnosticName returns the best available user-facing name, including
	// when manifest resolution failed — contentmapper.go:67.
	std::string DiagnosticName() const;
	// Identity returns the mapper's "name@version" identity, or just the name
	// when it declares no version, or an empty string when the mapper has not
	// been resolved to a name — contentmapper.go:80.
	std::string Identity() const;
	std::string manifestIdentity() const;
	// TransformIdentity returns a fingerprint of everything besides a file's
	// content that determines the output of transforming it with this mapper
	// under the given options — contentmapper.go:104.
	xxh3::Uint128 TransformIdentity(const CompilerOptions* options) const;
	// MarshalDeclaredOptions marshals just the compiler options this mapper
	// declared it depends on, in the declared order, skipping any that are
	// unset — contentmapper.go:119.
	std::pair<collections::OrderedMap<std::string, json::Value>, gostd::Error>
	MarshalDeclaredOptions(const CompilerOptions* options) const;
};

// IsSupportedVirtualExtension — contentmapper.go:62.
bool IsSupportedVirtualExtension(std::string_view extension);

// ---------------------------------------------------------------------------
// host.go
// ---------------------------------------------------------------------------

// TransformErrorKind identifies the stage at which a content mapper transform
// failed.
enum class TransformErrorKind : uint8_t {
	Unknown = 0,
	Initialize = 1,
	Project = 2,
	Request = 3,
	Response = 4,
	Mappings = 5,
};
inline constexpr TransformErrorKind TransformErrorKindUnknown =
	TransformErrorKind::Unknown;
inline constexpr TransformErrorKind TransformErrorKindInitialize =
	TransformErrorKind::Initialize;
inline constexpr TransformErrorKind TransformErrorKindProject =
	TransformErrorKind::Project;
inline constexpr TransformErrorKind TransformErrorKindRequest =
	TransformErrorKind::Request;
inline constexpr TransformErrorKind TransformErrorKindResponse =
	TransformErrorKind::Response;
inline constexpr TransformErrorKind TransformErrorKindMappings =
	TransformErrorKind::Mappings;

// TransformError reports a failure while preparing, requesting, or decoding a
// transform.
struct TransformError : gostd::ErrObj {
	TransformErrorKind Kind = TransformErrorKind::Unknown;
	gostd::Error err;

	std::string Error() const override;
	gostd::Error Unwrap() const { return err; }
	std::vector<gostd::Error> unwrap() const override { return {err}; }
};
// NewTransformError creates a transform error for the given stage and
// underlying error — host.go:32.
TransformError* NewTransformError(TransformErrorKind kind, gostd::Error err);

// DiagnosticDirectiveErrorKind identifies why a diagnostic directive was
// rejected.
enum class DiagnosticDirectiveErrorKind : uint8_t {
	InvalidRange = 0,
	InvalidPolicy = 1,
	ExpectMissingUnusedDiagnostic = 2,
	InvalidUnusedDiagnosticIndex = 3,
	Overlap = 4,
};
inline constexpr DiagnosticDirectiveErrorKind
	DiagnosticDirectiveErrorKindInvalidRange =
		DiagnosticDirectiveErrorKind::InvalidRange;
inline constexpr DiagnosticDirectiveErrorKind
	DiagnosticDirectiveErrorKindInvalidPolicy =
		DiagnosticDirectiveErrorKind::InvalidPolicy;
inline constexpr DiagnosticDirectiveErrorKind
	DiagnosticDirectiveErrorKindExpectMissingUnusedDiagnostic =
		DiagnosticDirectiveErrorKind::ExpectMissingUnusedDiagnostic;
inline constexpr DiagnosticDirectiveErrorKind
	DiagnosticDirectiveErrorKindInvalidUnusedDiagnosticIndex =
		DiagnosticDirectiveErrorKind::InvalidUnusedDiagnosticIndex;
inline constexpr DiagnosticDirectiveErrorKind DiagnosticDirectiveErrorKindOverlap =
	DiagnosticDirectiveErrorKind::Overlap;

using DiagnosticDirectivePolicy = uint8_t; // hostimpl.go:125
inline constexpr DiagnosticDirectivePolicy DiagnosticDirectivePolicyIgnore = 0;
inline constexpr DiagnosticDirectivePolicy DiagnosticDirectivePolicyExpect = 1;

// DiagnosticDirectiveError reports an invalid diagnostic directive in a
// transform response.
struct DiagnosticDirectiveError : gostd::ErrObj {
	DiagnosticDirectiveErrorKind Kind =
		DiagnosticDirectiveErrorKind::InvalidRange;
	int Index = 0;
	int SupplementalIndex = 0;
	DiagnosticDirectivePolicy Policy = 0;

	std::string Error() const override;
};

// InvalidVirtualExtensionError reports an unsupported or missing virtual
// extension on a mapped output.
struct InvalidVirtualExtensionError : gostd::ErrObj {
	std::string Extension;
	std::string Error() const override;
};

// ProjectErrorKind identifies why a mapper's openProject response was
// rejected.
enum class ProjectErrorKind : uint8_t {
	MalformedResponse = 0,
	MissingConfigIdentity = 1,
	NonAbsoluteWatchedFile = 2,
	UnexpectedConfigIdentity = 3,
	UnexpectedWatchedFiles = 4,
};
inline constexpr ProjectErrorKind ProjectErrorKindMalformedResponse =
	ProjectErrorKind::MalformedResponse;
inline constexpr ProjectErrorKind ProjectErrorKindMissingConfigIdentity =
	ProjectErrorKind::MissingConfigIdentity;
inline constexpr ProjectErrorKind ProjectErrorKindNonAbsoluteWatchedFile =
	ProjectErrorKind::NonAbsoluteWatchedFile;
inline constexpr ProjectErrorKind ProjectErrorKindUnexpectedConfigIdentity =
	ProjectErrorKind::UnexpectedConfigIdentity;
inline constexpr ProjectErrorKind ProjectErrorKindUnexpectedWatchedFiles =
	ProjectErrorKind::UnexpectedWatchedFiles;

// ProjectError reports an invalid mapper openProject response.
struct ProjectError : gostd::ErrObj {
	ProjectErrorKind Kind = ProjectErrorKind::MalformedResponse;
	std::string Error() const override;
};

// PositionEncoding is the coordinate space a mapper uses for mappings and
// diagnostics — hostimpl.go:92.
using PositionEncoding = std::string;
inline const PositionEncoding PositionEncodingUTF8 = "utf-8";
inline const PositionEncoding PositionEncodingUTF16 = "utf-16";

// InitializeErrorKind identifies why a mapper's initialize response was
// rejected.
enum class InitializeErrorKind : uint8_t {
	ProcessStart = 0,
	ProcessExit = 1,
	NoResponse = 2,
	InvalidResponse = 3,
	Request = 4,
	PositionEncoding = 5,
	EmptyDiagnosticSource = 6,
	ReservedDiagnosticSource = 7,
};
inline constexpr InitializeErrorKind InitializeErrorKindProcessStart =
	InitializeErrorKind::ProcessStart;
inline constexpr InitializeErrorKind InitializeErrorKindProcessExit =
	InitializeErrorKind::ProcessExit;
inline constexpr InitializeErrorKind InitializeErrorKindNoResponse =
	InitializeErrorKind::NoResponse;
inline constexpr InitializeErrorKind InitializeErrorKindInvalidResponse =
	InitializeErrorKind::InvalidResponse;
inline constexpr InitializeErrorKind InitializeErrorKindRequest =
	InitializeErrorKind::Request;
inline constexpr InitializeErrorKind InitializeErrorKindPositionEncoding =
	InitializeErrorKind::PositionEncoding;
inline constexpr InitializeErrorKind InitializeErrorKindEmptyDiagnosticSource =
	InitializeErrorKind::EmptyDiagnosticSource;
inline constexpr InitializeErrorKind
	InitializeErrorKindReservedDiagnosticSource =
		InitializeErrorKind::ReservedDiagnosticSource;

// InitializeError reports an invalid or unsupported mapper initialize
// response.
struct InitializeError : gostd::ErrObj {
	InitializeErrorKind Kind = InitializeErrorKind::ProcessStart;
	std::string MapperName;
	std::string Command;
	std::string Detail;
	int ExitCode = 0;
	int TimeoutSeconds = 0;
	PositionEncoding PositionEncoding;
	std::string DiagnosticSource;

	std::string Error() const override;
};

// SupplementalFileCollisionError reports a compiler-assigned supplemental
// filename that already exists.
struct SupplementalFileCollisionError : gostd::ErrObj {
	std::string FileName;
	std::string Error() const override;
};

// MappedResult is one virtual source file and its mapping to the original
// input.
struct MappedResult {
	std::string Text;
	std::string VirtualExtension;
	spanmap::SpanMap* Mappings = nullptr;
	std::vector<::tsc::MappedDiagnosticDirective> DiagnosticDirectives;
};

// Result is the outcome of transforming a content-mapped source file into
// virtual TypeScript.
struct Result {
	// Text is the virtual TypeScript source text that is parsed into the
	// program.
	std::string Text;
	// VirtualExtension determines how Text is parsed.
	std::string VirtualExtension;
	// Diagnostics are syntax errors in the original content.
	std::vector<::tsc::Diagnostic*> Diagnostics;
	// Mappings maps positions in Text back to the original content, so that
	// diagnostics the compiler produces against the virtual text can be
	// reported at their original locations. A successful transform must
	// return a non-null map; an empty map describes fully synthesized output.
	spanmap::SpanMap* Mappings = nullptr;
	// DiagnosticDirectives control TypeScript diagnostics produced in virtual
	// ranges.
	std::vector<::tsc::MappedDiagnosticDirective> DiagnosticDirectives;
	// Supplemental contains additional unnamed outputs associated with the
	// canonical result.
	std::vector<MappedResult> Supplemental;
};

// Request carries the inputs for transforming one content-mapped source file.
struct Request {
	// FileName is the content-mapped source file being transformed.
	std::string FileName;
	// Content is the content-mapped source file's text.
	std::string Content;
};

// ProjectSpec describes the project configuration visible to its content
// mappers.
struct ProjectSpec {
	// ConfigFileName is the absolute project configuration file name, or
	// empty for a project without one.
	std::string ConfigFileName;
	// Mappers are the resolved content mapper entries configured for the
	// project.
	std::vector<Mapper*> Mappers;
	// CompilerOptions are the project's effective compiler options.
	CompilerOptions* CompilerOptions = nullptr;
};

struct OptionPathSegment {
	std::string Property;
	int64_t Index = 0;
	bool IsIndex = false;
};

struct OptionDiagnostic {
	Mapper* Mapper = nullptr;
	std::vector<OptionPathSegment> Path;
	std::string Source;
	int32_t Code = 0;
	std::string MessageText;
};

// OperationTiming is the cumulative wall time and invocation count for one
// mapper operation.
struct OperationTiming {
	uint64_t Count = 0;
	gostd::Duration Duration{0};
};

// MapperTimings is cumulative process and protocol activity for one resolved
// mapper identity.
struct MapperTimings {
	OperationTiming Spawn;
	OperationTiming Initialize;
	OperationTiming OpenProject;
	OperationTiming CloseProject;
	OperationTiming Transform;
};

// Timings is a cumulative snapshot of content mapper process and protocol
// activity.
struct Timings {
	std::unordered_map<std::string, MapperTimings> Mappers;
	gostd::Duration RequestWait{0};

	// Since returns the non-negative operation delta since previous —
	// host.go:245.
	Timings Since(const Timings& previous) const;
};

// Project is the project-scoped view of a Host. It owns mapper configuration
// handles and provides the identities and watch dependencies needed for
// caching and incremental builds. Mapper projects are opened lazily when a
// transform is requested, or earlier when dynamic configuration is needed.
struct Project {
	virtual ~Project() = default;
	// Refresh closes opened mapper projects so they are reopened on the next
	// transform or configuration identity query.
	virtual gostd::Error Refresh() = 0;
	// Identities returns sorted transform identities for all configured
	// mappers. It returns an error if dynamic project configuration cannot be
	// opened or validated.
	virtual std::pair<std::vector<std::string>, gostd::Error> Identities() = 0;
	// Identity returns the transform identity for mapper, or an empty string
	// if mapper is not in this project. It returns an error if dynamic project
	// configuration cannot be opened or validated.
	virtual std::pair<std::string, gostd::Error> Identity(Mapper* mapper) = 0;
	// WatchedFiles returns the absolute files reported by mappers whose
	// package.json declares dynamicConfig. It returns an error if project
	// configuration cannot be opened or validated.
	virtual std::pair<std::vector<std::string>, gostd::Error> WatchedFiles() = 0;
	// Diagnostics returns option diagnostics cached by mapper projects that
	// have already been opened.
	virtual std::vector<OptionDiagnostic> Diagnostics() = 0;
	// Transform transforms one content-mapped source file using mapper in
	// this project's configuration.
	virtual std::pair<Result, gostd::Error>
	Transform(Mapper* mapper, const Request& request) = 0;
	// Close releases this project reference and closes mapper project handles
	// when no references remain.
	virtual gostd::Error Close() = 0;
};

// Host transforms otherwise unsupported file content into virtual TypeScript
// during program construction, by driving the configured content mappers.
// Create one with NewHost; Close tears down every mapper it spawned.
struct Host {
	virtual ~Host() = default;
	// Timings returns a cumulative snapshot of mapper process and protocol
	// activity.
	virtual tsc::contentmapper::Timings Timings() = 0;
	// Project returns a retained project-scoped view for spec. Equivalent
	// specs share underlying mapper configuration state; the caller must
	// close the returned Project.
	virtual std::shared_ptr<tsc::contentmapper::Project> Project(
	    const ProjectSpec& spec) = 0;
	// Acquire retains the processes for the given mapper identities until the
	// returned lease is released. Acquiring a mapper does not start its
	// process; processes remain lazy until Transform is called.
	virtual std::function<void()> Acquire(
	    const std::vector<Mapper*>& mappers) = 0;
	// SetLocale updates the locale used to initialize mapper processes.
	// Existing processes are stopped and respawned lazily so subsequent
	// transforms use the new locale.
	virtual void SetLocale(locale::Locale diagnosticLocale) = 0;
	// Transform maps a content-mapped source file to virtual TypeScript using
	// the given content mapper in a short-lived project with default compiler
	// options.
	//
	// A non-nil error indicates the mapper itself failed to produce a result
	// — for example the host hit a broken pipe, a process crash, or could not
	// deserialize the mapper's response.
	virtual std::pair<Result, gostd::Error>
	Transform(Mapper* mapper, const Request& request) = 0;
	// Close shuts down every mapper process the host spawned.
	virtual gostd::Error Close() = 0;
};

// ---------------------------------------------------------------------------
// hostimpl.go — exported surface
// ---------------------------------------------------------------------------

inline constexpr int initializeTimeoutSeconds = 5;
inline const gostd::Duration initializeTimeout =
	initializeTimeoutSeconds * gostd::second();

// Content mapper protocol method names.
inline constexpr std::string_view MethodInitialize = "initialize";
inline constexpr std::string_view MethodOpenProject = "openProject";
inline constexpr std::string_view MethodCloseProject = "closeProject";
inline constexpr std::string_view MethodTransform = "transform";

// InitializeParams is the parameter object for the initialize request.
struct InitializeParams {
	// Locale is the BCP 47 locale to use for mapper-authored diagnostic
	// messages, when configured.
	std::string Locale; // `json:"locale,omitempty"`
	// PositionEncodings lists the coordinate spaces the host accepts.
	std::vector<PositionEncoding> PositionEncodings; // `json:"positionEncodings"`
};

// InitializeResult is the mapper's response to the initialize request.
struct InitializeResult {
	// PositionEncoding selects the coordinate space for all mappings and
	// diagnostics.
	PositionEncoding PositionEncoding; // `json:"positionEncoding"`
	// DiagnosticSource is the prefix used for every mapper-authored
	// diagnostic code.
	std::string DiagnosticSource; // `json:"diagnosticSource"`
};

// OpenProjectParams is the parameter object for the openProject request.
struct OpenProjectParams {
	// ConfigFileName is the absolute project configuration file name, or
	// empty when there is none.
	std::string ConfigFileName; // `json:"configFileName"`
	// ProjectHandle is an opaque, process-local handle assigned by the host.
	std::string ProjectHandle; // `json:"projectHandle"`
	// Options is the mapper entry's options from the project's contentMappers
	// configuration.
	json::Value Options; // `json:"options,omitempty"`
	// CompilerOptions contains the project's effective compiler options.
	json::Value CompilerOptions; // `json:"compilerOptions"`
};

// OptionDiagnosticResult is one option diagnostic entry in an openProject
// response.
struct OptionDiagnosticResult {
	std::vector<json::Value> Path; // `json:"path"`
	std::string MessageText;       // `json:"messageText"`
	int32_t Code = 0;              // `json:"code"`
};

// OpenProjectResult is the mapper's response to an openProject request.
// ConfigIdentity and WatchedFiles may only be returned by mappers that
// declare dynamicConfig.
struct OpenProjectResult {
	// ConfigIdentity is a stable fingerprint of all dynamic configuration
	// that can affect transforms.
	std::string ConfigIdentity; // `json:"configIdentity"`
	// WatchedFiles are absolute files whose changes may alter ConfigIdentity
	// or transform output.
	std::vector<std::string> WatchedFiles; // `json:"watchedFiles,omitempty"`
	// OptionDiagnostics report invalid mapper options. Paths are relative to
	// the mapper entry's options object.
	std::vector<OptionDiagnosticResult>
		OptionDiagnostics; // `json:"optionDiagnostics,omitempty"`
};

// CloseProjectParams is the parameter object for the closeProject request.
struct CloseProjectParams {
	std::string ProjectHandle; // `json:"projectHandle"`
};

// TransformParams is the parameter object for the transform request.
struct TransformParams {
	// FileName is the absolute name of the content-mapped source file being
	// transformed.
	std::string FileName; // `json:"fileName"`
	// Content is the content-mapped source file's text.
	std::string Content; // `json:"content"`
	// ProjectHandle identifies the mapper project configuration opened for
	// this transform.
	std::string ProjectHandle; // `json:"projectHandle"`
};

struct UnusedExpectDirectiveDiagnostic {
	int32_t Code = 0;         // `json:"code"`
	std::string MessageText;  // `json:"messageText"`
};

// MappedDiagnosticDirective is encoded as
// [originalStart, originalLength, virtualStart, virtualEnd, policy,
// unusedExpectDirectiveIndex?]. An omitted index selects the only
// unused-expect diagnostic and is invalid when there is not exactly one.
struct MappedDiagnosticDirective {
	int64_t OriginalStart = 0;
	int64_t OriginalLength = 0;
	int64_t VirtualStart = 0;
	int64_t VirtualEnd = 0;
	DiagnosticDirectivePolicy Policy = 0;
	std::optional<int64_t> UnusedExpectDirectiveIndex;

	json::Value marshalJSONTo() const;
	gostd::Error unmarshalJSONFrom(const json::Value& data);
};

// DiagnosticDirectives shares unused-expect diagnostics across compact
// directive tuples.
struct DiagnosticDirectives {
	std::vector<UnusedExpectDirectiveDiagnostic>
		UnusedExpectDirectiveDiagnostics; // `json:"unusedExpectDirectiveDiagnostics"`
	std::vector<MappedDiagnosticDirective> Directives; // `json:"directives"`
};

// MappedOutput is virtual source text and its mapping to an original input.
struct MappedOutput {
	// Text is the virtual JavaScript or TypeScript source text.
	std::string Text; // `json:"text"`
	// Extension determines the virtual source file's syntax.
	std::string Extension; // `json:"extension"`
	// Mappings is the span map's tuple-array JSON (see spanmap.Marshal),
	// expressed in the selected position encoding. Absent or empty means the
	// output is fully synthesized.
	json::Value Mappings; // `json:"mappings,omitempty"`
	// DiagnosticDirectives describe framework directives that suppress
	// TypeScript diagnostics in virtual ranges and optionally report an error
	// when no diagnostic is produced.
	std::optional<DiagnosticDirectives>
		DiagnosticDirectives; // `json:"diagnosticDirectives,omitempty"`
};

struct SupplementalOutput : MappedOutput {};

// Diagnostic is an error reported by a mapper in original-source coordinates.
struct Diagnostic {
	// MessageText is the diagnostic message.
	std::string MessageText; // `json:"messageText"`
	// Start and Length locate the diagnostic in the original content using
	// the selected position encoding.
	int64_t Start = 0;   // `json:"start"`
	int64_t Length = 0;  // `json:"length"`
	int32_t Code = 0;    // `json:"code"`
};

// TransformResult is the canonical output for one input file.
struct TransformResult : MappedOutput {
	// Diagnostics are mapper-authored errors expressed in original-source
	// coordinates.
	std::vector<Diagnostic> Diagnostics; // `json:"diagnostics,omitempty"`
	// Supplemental contains additional unnamed compiler inputs associated
	// with this source file.
	std::vector<SupplementalOutput> Supplemental; // `json:"supplemental,omitempty"`
};

// Spawner starts a child process, returning its stdio as an
// io.ReadWriteCloser (Read is the process's stdout, Write is its stdin) whose
// Close tears the process down. This seam keeps os/exec out of this package:
// production hosts spawn a real process, tests supply an in-process pipe.
struct Spawner {
	virtual ~Spawner() = default;
	virtual std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr_) = 0; // stderr_: CRT macro dodge
};

struct processExitState {
	virtual ~processExitState() = default;
	virtual std::pair<int, bool> ExitCode() = 0;
};

// SpawnerFunc adapts a spawn function to the Spawner interface.
using SpawnerFuncFn =
    std::function<std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
                            gostd::Error>(const std::vector<std::string>&,
                                          const std::string&,
                                          gostd::io::Writer*)>;
struct SpawnerFunc : Spawner {
	SpawnerFuncFn fn;
	explicit SpawnerFunc(SpawnerFuncFn f) : fn(std::move(f)) {}
	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr_) override {
		return fn(command, dir, stderr_);
	}
};

// Logger receives content mapper protocol and process output as complete log
// lines.
using Logger = std::function<void(std::string_view)>;

// HostOptions configures optional content mapper process logging.
struct HostOptions {
	Logger Logger;
};

// NewHost creates a Host that spawns each mapper's process via the given
// spawner and drives it over a JSON-RPC connection. The host's lifetime is
// bound to ctx: cancelling it (e.g. the CLI's signal context on SIGINT, or a
// build/watch session ending) tears every mapper process down, so owners of
// a session context need not close the host explicitly. Close does the same
// synchronously. The shared_ptr keeps the host alive for the cancellation
// callback, mirroring Go GC semantics.
std::shared_ptr<Host> NewHost(gostd::Context ctx, Spawner* spawner,
                              locale::Locale diagnosticLocale);

// NewHostWithOptions creates a Host with optional protocol and process
// logging.
std::shared_ptr<Host> NewHostWithOptions(gostd::Context ctx, Spawner* spawner,
                                         locale::Locale diagnosticLocale,
                                         HostOptions options);

// ---------------------------------------------------------------------------
// transform.go
// ---------------------------------------------------------------------------

// SourceFiles is the canonical output and its unnamed supplemental compiler
// inputs.
struct SourceFiles {
	SourceFile* Canonical = nullptr;
	std::vector<SourceFile*> Supplemental;
};

// TransformAndParse runs the given content mapper's transform for a
// content-mapped source file and parses the resulting TypeScript, preserving
// the original file name and retaining the untransformed text on the source
// file. The mapper is supplied by the caller (which also owns the failure
// accounting) so it is neither re-resolved nor substituted here. It returns
// an error if the transform fails or the mapper produces invalid position
// mappings (a spanmap::MappingError); the caller decides how to report the
// failure and what placeholder file to substitute. It is the shared
// implementation behind CompilerHost::GetContentMappedSourceFile.
std::pair<SourceFiles, gostd::Error> TransformAndParse(
    const SourceFileParseOptions& parseOptions, const std::string& content,
    Mapper* mapper, const std::shared_ptr<Project>& project);

// ParseResult validates and parses one mapper result and all its
// supplemental outputs.
std::pair<SourceFiles, gostd::Error> ParseResult(
    const SourceFileParseOptions& parseOptions, const std::string& content,
    Mapper* mapper, const std::string& transformIdentity, Result result);

// CheckSupplementalFileNameCollisions rejects compiler-assigned virtual
// filenames that name physical files.
gostd::Error CheckSupplementalFileNameCollisions(
    const SourceFiles& files,
    const std::function<bool(std::string_view)>& fileExists);

// === package-internal (Go unexported, shared across this package's TUs) ===

// isModuleVirtualExtension — transform.go:123.
bool isModuleVirtualExtension(std::string_view extension);

namespace detail {
// json.Marshal(*core.CompilerOptions) — emits every non-zero field.
// Defined in contentmapper.cpp.
json::Value marshalCompilerOptions(const CompilerOptions* options);
} // namespace detail

} // namespace tsc::contentmapper
