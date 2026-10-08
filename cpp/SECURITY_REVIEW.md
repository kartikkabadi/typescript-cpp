# Security Review — `cpp/` TypeScript port

Adversarial audit of the C++23 port against the Go oracle in `tsc/internal`.
Threat model (per upstream `SECURITY.md` / the repo's `security-report-check`
guidance): `tsc`/`tsgo` is a build tool and language server, not a sandbox —
arbitrary code execution is the bar; crashes, hangs, resource consumption and
config-driven file writes are documented non-guarantees. The port-specific bar
applied here is stricter: **the C++ side must not be worse than the Go side**.
Every finding below is severity-ranked, given a concrete exploit sketch or
divergence, and marked with its upstream-parity status.

## Methodology

File-by-file read of every security-relevant surface, with the corresponding
Go oracle (`tsc/internal/**`) opened alongside to detect dropped checks or
weakened invariants. Surfaces covered:

- Path handling & traversal — `internal/vfs/**` (osvfs, vfsmatch, dirFS,
  realpath), `internal/tspath/tspath.h`, module/specifier path resolution.
- Command/process execution — `cmd/tscpp/sys.cpp` (`spawnProcess`),
  `cmd/tscpp/lsp.cpp` (`npmInstall`), `internal/project/ata/**`,
  `internal/contentmapper/hostimpl.cpp`.
- LSP/IPC message handling — `internal/lsp/**`, `internal/jsonrpc/**`,
  `internal/ipc/**`, `internal/api/**` (session dispatch, requestfilesystem,
  encoder/decoder).
- ReDoS / regex engine — `internal/gostd/regexp.*`, `vfsmatch`,
  `modulespecifiers`, `lsp_stack_sanitizer`, `semver`, all `std::regex` call
  sites enumerated.
- Integer/overflow surfaces — Content-Length parsing, `TextPos` widths,
  `spanmap`, line/offset maps.
- Dependencies — `cpp/third_party/**` enumeration + advisory check.
- Secrets scan — working tree + `git log -p` pattern pass.
- Logging — env/trace/log-file surfaces vs Go.

## Findings

| # | Severity | Location | Issue | Upstream parity | Disposition |
|---|----------|----------|-------|-----------------|-------------|
| F1 | Medium | `internal/modulespecifiers/util.cpp:137-156,161-224` | User-controlled `autoImportSpecifierExcludeRegexes` compiled by `std::regex` (backtracking NFA) instead of RE2 (linear). A pattern like `(a+)+$` on a long specifier is exponential; `(a|a)*$` on a ~30-char specifier can hang the language server for hours. Deliverable via a trusted workspace's `.vscode/settings.json` (`typescript.preferences.autoImportSpecifierExcludeRegexes`) or any client-supplied preferences payload. | **Worse than Go** — RE2 cannot be DoS'd this way; also a semantic divergence: RE2 rejects lookahead/backrefs, `std::regex` ECMAScript executes them (backref matching is NP-complete). | **Partially fixed** — `stringToRegex` now pre-rejects constructs RE2 rejects (`\1`–`\9` backrefs outside char classes, `(?=`, `(?!`, `(?<=`, `(?<!`), so patterns Go could never run are skipped identically. The residual ReDoS on RE2-valid catastrophic patterns (`(a+)+$`) is inherent to shipping a backtracking engine; a full fix requires an RE2-class engine and is documented, not patched (an engine swap would change match semantics = divergence). |
| F2 | Medium | `internal/lsp/lsp_handlers.cpp:1301-1397` (`handleInitializeAPISession`) + `internal/ipc/transport_unix.cpp` (`newPipeListener`) | Client-supplied `params->Pipe` is used unvalidated as a unix-socket path; `newPipeListener` unconditionally `unlink()`s whatever exists there before `bind()`. A client (or a bridge proxying untrusted initialize traffic) can delete any file the server process can unlink and leave a socket at a chosen path. | **Matches upstream** — `server.go:2292-2296` and `transport_unix.go` do `_ = os.Remove(path)` + `net.Listen` identically, and the attached client already controls the whole session. | Documented; not fixed (any validation would diverge from Go). |
| F3 | Low | `internal/jsonrpc/jsonrpc.cpp` `Reader::Read` | `Content-Length` is range-checked for overflow/`n<0` but there is no size cap: `std::string data(contentLength,'\0')` allocates the declared length up front. A peer sending `Content-Length: 40GB` causes an immediate large allocation → OOM/DoS. | **Matches upstream** — Go does `make([]byte, n)` with the same lack of cap. | Documented; not fixed. |
| F4 | Low | `internal/lsp/lsproto/lsproto.cpp:440-535` (`documentUriFileName`/`urlDecode`) | A malformed `%` escape in a client-supplied `DocumentUri` panics (`lsprotoPanic`). Contained by the per-request `recover_` but a panic-crossing-boundary on LSP input is still an availability surface. | **Matches upstream** — `lsp.go:19-55` panics identically on `url.Parse` error. | Documented; not fixed. |
| F5 | Low | `internal/modulespecifiers/util.cpp` `stringToRegex` (after F1 fix) | Inverse semantic divergence remains: RE2 syntax std::regex rejects — e.g. inline flags `(?i:…)`, `\k<name>`, `\C`, `\Q…\E` — compile fine in Go but throw `regex_error` → pattern skipped. Exclusions Go would apply may be silently dropped in C++. | **Worse than Go** (minor): a valid-Go exclusion becomes a no-op. Cannot be fixed without RE2 semantics. | Documented; not fixed. |

## Fix pushed

**`internal/modulespecifiers/util.cpp` — `stringToRegex`** (see F1/F5):
before `std::regex` construction, patterns are scanned for constructs RE2
rejects — `\1`–`\9` backreferences outside a character class, and `(?=`,
`(?!`, `(?<=`, `(?<!` look-arounds — and are treated as failed compiles
(cached `nullptr`, skipped by `IsExcludedByRegex`), exactly as Go treats
them. This removes the NP-complete backref class and the
executes-in-C++/rejected-in-Go divergence while preserving every pattern
Go can actually run. Inside `[…]` a `\1`–`\9` remains an octal escape per
both grammars, so class membership is tracked to avoid over-rejecting.
Residual edge: `]` as the first char of `[…]`/`[^…]` is literal in both
grammars but counted as a class close by the scan, so a `\d` digit-escape
immediately after it may be treated as a backref — rejecting a pattern Go
accepts. That direction only skips an exclusion; it never runs a pattern
Go wouldn't.

Verified: `cmake -B cpp/build -S cpp -G Ninja -DCMAKE_BUILD_TYPE=Release`
+ `ninja tscpp unittestrunner` (clang-15, lld) and `./unittestrunner`
(see "Verification" below).

## Surfaces audited — no findings

- **Path handling & traversal.** `vfs.cpp` `validPath` is a faithful
  `fs.ValidPath` port (UTF-8 validity, rejects empty/`.`/`..` elements);
  `osvfs/dirFS::join` gates every `Open`/`ReadFile`/`ReadDir`/`Stat`/
  `Lstat`/`ReadLink`/`Remove` on it, so `..` traversal is blocked the same
  as Go's `filepathlite.Localize`; NUL is rejected. `nativepathRealpath`
  resolves via `O_PATH` + `/proc/self/fd` loops (no TOCTOU-prone
  path-chasing) with `realpath(3)` fallback; `osFSRealpath` asserts
  rooted. `tspath.h`: `getEncodedRootLength`/`getRootLength` mirror Go
  including `^/` untitled roots, `~`-encoded URL roots, DOS volumes and
  UNC; `normalizePath`/`reducePathComponents` clamp `..` at the root;
  `toPath`/`containsPath`/`getBasePaths`/`getRelativePathIfInSameVolume`
  carry the same volume/containment checks. `vfsmatch` is a hand-rolled
  O(n·m) iterative segment matcher — no regex, no ReDoS; symlink cycles
  are detected via canonical-path `visited` set.
- **Command/process execution.** All spawn sites use `fork()` + `execvp`
  with pre-built argv vectors — `cmd/tscpp/sys.cpp::spawnProcess`,
  `cmd/tscpp/lsp.cpp::npmInstall`, `project/ata.cpp::installWorker`
  (`npm install --ignore-scripts <pkgs> --save-dev
  --user-agent="typesInstaller/…"`), `contentmapper/hostimpl.cpp`
  (manifest `Exec` vector). No shell is ever invoked; argument injection
  is not possible. ATA package names pass `ValidatePackageName`
  (`queryEscape` verified byte-identical to `url.QueryEscape` +
  `validatePackageNameWorker`) plus `typesRegistry` membership before
  reaching npm; chunking (<8000 chars/arg) matches `ata.go`.
- **LSP/IPC framing.** `jsonrpc.cpp` header parse uses `from_chars` with
  `out_of_range` → error, `n < 0` → `ErrInvalidContentLength`,
  `contentLength <= 0` → `ErrNoContentLength`. Response dispatch in
  `ipc/conn_async.cpp` is gated on `IsResponse()` (`Id.has_value() &&
  Method.empty()`) before dereferencing `msg->Id` — identical to Go's
  `m.ID != nil && m.Method == ""`. `lsp_server.cpp`: pre-initialize
  requests rejected (`ServerNotInitialized`), unknown methods →
  `InvalidRequest`, per-request context + detached threads holding
  `shared_from_this()`, handler panics contained by `recover_`.
  `api/session.cpp` dispatch follows the same envelope/validation shape
  as `api/session.go`; `requestfilesystem` path-tree operates on
  `toPath`-canonicalized absolute paths.
- **Integer/overflow.** Content-Length: `from_chars` bounds-checked (see
  F3 for the allocation cap parity item). Positions are `int32`
  (`TextPos` = `TextPos int32` in `core/text.go`) on both sides; span
  arithmetic (`spanmap`, `TextChange`) and line/offset maps use the same
  widths and same >2 GB file limitation as Go. JSON decoder stack depth
  capped at `maxNestingDepth = 10000`, identical to `jsontext/state.go`.
- **JSON-RPC extras.** Batching/continuation tokens, cancel-request and
  notification handler registration all gate on `session == nullptr`
  exactly where Go does.
- **Dependencies.** Only vendored third-party code is
  `cpp/third_party/mimalloc` = **v2.1.7** (`MI_MALLOC_VERSION 217`), the
  latest v2 release (2024-05-21). No CVEs or advisories apply to the C
  library at this version; `GHSA-g23h-7vf9-xc25` /
  `RUSTSEC-2022-0094` (bad-alignment) affect the Rust `mimalloc` crate
  0.1.x binding only, not this source. No other vendored libraries —
  everything else under `cpp/` is first-party.
- **Secrets/credentials.** Working tree scanned for token/key patterns
  (`ghp_`, `gho_`, `github_pat_`, `sk-`, `AKIA`, private-key blocks,
  `x-access-token`) — no hits. `git log -p --all` pattern pass across
  history — no hits. No credentials are committed; nothing to rotate.
- **Logging.** No environment dumps, no credential echoing. Debug env
  vars (`TSCPP_DEBUG_CRASH`, `TS_WATCH_DEBUG`, `TS_CONTENT_MAPPER_DEBUG`)
  mirror Go's `TSS_DEBUG`-style gates. `lsp_logger` writes only to
  caller-specified paths with verbosity gating per `logger.go`.
  `lsp_stack_sanitizer` uses the same fixed
  `(?i)(key|token|signature|sig|pwd)` keyword regex as
  `stack_sanitizer.go` — not user-controlled.

## Explicit scope notes

- Per upstream policy, local DoS (CPU/memory/hangs) from adversarial
  *input programs* is out of scope for both sides; F1 is still reported
  because it makes the port strictly weaker than Go on identical config.
- `internal/scanner/regexp.cpp` is a regex-literal *validator* port
  (`regexp.go`), not a matching engine — it cannot ReDoS.
- Content mappers execute code only when the caller opts in
  (`--runExternalCode` / manifest-gated), matching the upstream
  exception; exec vectors are argv-based with no shell.
- The C++ panics/`TSC_UNREACHABLE` sites observed correspond to
  `panic()`/`AssertNever` in Go — crash-for-crash parity, which the
  upstream policy treats as non-vulnerabilities.
