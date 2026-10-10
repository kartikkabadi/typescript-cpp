# REVIEW2 — cubic-dev-ai triage (comments ≥ 2026-10-06T00:42Z)

75 new review comments on PR #1 triaged on branch `devin/cpp-review2`
(base `devin/cpp-port` @ eaf850a6fe).

**Counts: 32 REAL · 38 STALE · 5 NOISE**

Verification: `unittestrunner` 1121/1122 (`ls/autoimport.TestRegistryLifecycle`
fails only inside the full suite — known flaky family, passes standalone,
unrelated to these changes), `tsctestrunner` 117/117,
`fourslashrunner -run 'TestQuickInfo|TestCompletions'` 502/502.

| id | file | verdict | fix commit / reason |
|---|---|---|---|
| 4190366301 | cpp/cmd/tscpp/sys.cpp | STALE | argv vector built before `fork()` already |
| 4190366303 | cpp/cmd/tscpp/sys.cpp | STALE | SIGPIPE ignored before spawn already |
| 4190366308 | cpp/cmd/tscpp/sys.cpp | REAL | 202b89d076 — exec errno pipe: child writes errno post-fork, parent reads EOF=success else reaps + returns `spawn:` error |
| 4190366314 | cpp/cmd/tscpp/sys.cpp | REAL | 202b89d076 — stderr pump now bounded: `stopPump` flag + fd close + join so an fd-inheriting descendant can't hang `close()` |
| 4190366319 | cpp/cmd/tscpp/sys.cpp | STALE | pipes already created with CLOEXEC (pipe2/pipeCloexec) |
| 4190366321 | cpp/cmd/tscpp/main.cpp | REAL | 202b89d076 — `tscpp tsc` now runs under `cmd_notify::signalNotifyContext()` mirroring main.go:29 |
| 4190366327 | cpp/internal/module/resolver.cpp | STALE | resolver ownership documented/correct at HEAD |
| 4190641601 | cpp/internal/execute/execute.cpp | NOISE | session-lifetime orchestrator pin — GC-parity design, not a leak |
| 4191449183 | cpp/internal/compiler/fileloader.cpp | REAL | 07f7fe5500 — `TraceScope traceResolveLibrary` narrowed to the resolver-call block (Go fileloader.go:982-990) |
| 4191449202 | cpp/internal/tracing/tracing.cpp | NOISE | tracing session impl intentionally pinned for process lifetime |
| 4191449205 | cpp/internal/checker/checker_walk.cpp | STALE | lazy trace-arg evaluation already in place |
| 4191449210 | cpp/internal/xxh3/xxh3.h | REAL | faa316e427 — comment corrected: xxh64AvalancheSmall differs (no `x >> 33` stage) |
| 4192595301 | cpp/internal/api/session.cpp | REAL | 8ca4942eea — SourceFileLease shared_ptrs now `Release(); delete` on last drop (both acquire paths) |
| 4192595335 | cpp/internal/api/server.cpp | REAL | 8ca4942eea — SessionOptions now heap-owned via `sessionInit.KeepAlive` instead of leaked |
| 4192595369 | cpp/internal/api/session.cpp | REAL | 8ca4942eea — all lazily-allocated APISnapshotRequest/APICreateProgramRequest members (Ensure/Open/Close/Ensure/Remove programs, OpenFiles, ModuleResolverFactory) now owned via new `own()` helper |
| 4192595376 | cpp/internal/api/proto.cpp | NOISE | `unmarshalJSONFrom(ID*)` already declared in the shared header — visible to all decode sites; claim falsified |
| 4192755192 | cpp/internal/testutil/projecttestutil/clientmock_generated.h | STALE | ParamsOwner deep-copy already landed |
| 4192755195 | cpp/internal/testutil/projecttestutil/projecttestutil.cpp | STALE | optionsOwner shared_ptr already landed |
| 4192808290 | cpp/internal/ls/hovericon.cpp | REAL | 07f7fe5500 — no-op shared_ptr deleters → owning (imageId/imageElement/qiElement) |
| 4192808298 | cpp/internal/ls/hovericon.cpp | REAL | 07f7fe5500 — same fix for displayLine/docElement |
| 4193284132 | cpp/internal/project/snapshot.h | NOISE | SnapshotLSHost raw ptr is documented GC-parity: a ref would panic when the snapshot was disposed between getSnapshot and construction |
| 4193664717 | cpp/internal/project/configfileregistry.h | STALE | callers already guard `serializedRegistry != nullptr` at call sites; cad99cc146 documents that (in-method `this==nullptr` is UB — reverted) |
| 4193664728 | cpp/tools/conformance_triage.py | REAL | 840d7a5e1d — ORACLE_DIR default now `/tmp/oracle` |
| 4193664741 | cpp/internal/project/configfileregistry.h | REAL | cbe40f4bb4 — statebaseline owns ForEach*/GetTest* entry allocations until diff tables print |
| 4193664748 | cpp/internal/gostd/goseq.h | REAL | faa316e427 — added `#include <utility>` for `std::move` |
| 4194113679 | cpp/cmd/tscpp/main.cpp | REAL | 202b89d076 — transpiledump writes OutputText with fwrite (no embedded-NUL truncation) |
| 4194178180 | cpp/README.md | REAL | 291fd64245 — lsconv row now marked partial with pointer to lsconv.h |
| 4194247607 | cpp/tools/lsp_api_parity.py | REAL | 840d7a5e1d — SESSION-FAILED tagged per side + early exit(1) so identical breakage can't diff-equal |
| 4194247615 | cpp/tools/lsp_api_parity.py | REAL | 840d7a5e1d — transpile calls now send `{input, options{fileName,compilerOptions}}` matching TranspileParams |
| 4194247652 | cpp/tools/lsp_api_parity.py | REAL | 840d7a5e1d — LSP positions moved onto real symbols (add/Circle/inside-call) |
| 4194247659 | cpp/tools/lsp_api_parity.py | REAL | 840d7a5e1d — shutdown/notify wrapped in try/finally s.kill() |
| 4194247664 | cpp/tools/rpcframe.py | REAL | 840d7a5e1d — uri_for uses `Path.resolve().as_uri()` (percent-encodes `#`,`%`,spaces) |
| 4194247676 | cpp/tools/rpcframe.py | NOISE | result.items sort is deliberate: Go map order is nondeterministic run-to-run; sort is required for stable parity diff |
| 4194247683 | cpp/tools/rpcframe.py | REAL | 840d7a5e1d — backslash→slash now only for path/URI-keyed fields |
| 4194247690 | cpp/tools/rpcframe.py | REAL | 840d7a5e1d — publishDiagnostics added to skip_notifs |
| 4194746140 | cpp/internal/ls/utilities.cpp | STALE | CommentRange already copied to heap storage outliving the call |
| 4194746159 | cpp/internal/project/snapshothost.cpp | STALE | InternProjectID already mutex-guarded |
| 4194746167 | cpp/tools/lsp_api_parity.py | REAL | 840d7a5e1d — `bin_argv()`: existing path = single argv element, else shlex-split |
| 4194746174 | cpp/cmd/tscpp/main.cpp | REAL | 202b89d076 — crashExit uses cached flag + digit-built message, no getenv/snprintf in handler |
| 4194882917 | cpp/internal/checker/checker_printer.cpp | REAL | 07f7fe5500 — `pOwner` unique_ptr owns the NewPrinter allocation |
| 4196141567 | cpp/README.md | REAL | 291fd64245 — perf paragraph updated to post-fix wall-clock-parity numbers |
| 4196141574 | cpp/ROADMAP.md | STALE | `tscpp` spelled correctly at HEAD |
| 4196141589 | cpp/ROADMAP.md | STALE | PERF_REPORT.md already rewritten to post-fix numbers |
| 4199636569 | cpp/ROADMAP.md | REAL | cbe40f4bb4 — REPORT_A.md Summary block reconciled to 91 PASS/2 SKIP, 93/93 parity |
| 4202477164 | cpp/internal/gostd/gostd.h | STALE | gostd Timer rewritten (4965f5ffbd) |
| 4202477165 | cpp/internal/gostd/gostd.h | STALE | same |
| 4202477166 | cpp/internal/gostd/gostd.h | STALE | same |
| 4202815405 | cpp/internal/fourslash/fourslash.cpp | STALE | stable_sort comments accurate at HEAD |
| 4204016812 | cpp/tools/fix_designators.py | STALE | optional struct_orders + comment-aware braces already landed |
| 4204016821 | cpp/tools/fix_designators.py | STALE | same |
| 4204695132 | cpp/ROADMAP.md | STALE | ROADMAP now discloses 4,130 unique tests / 7 duplicate registrations |
| 4204695140 | cpp/ROADMAP.md | REAL | cbe40f4bb4 — REPORT_D final-recount line reconciled (4,130/4,130, 430 skips) |
| 4208562147 | .github/workflows/cpp.yml | STALE | scanner dir `conformance/scanner` already |
| 4208562200 | cpp/tools/conformance_corpus.sh | STALE | hardening landed |
| 4208562208 | cpp/tools/conformance_check.sh | STALE | hardening landed |
| 4208562216 | .github/workflows/cpp.yml | STALE | hardening landed |
| 4208562226 | cpp/tools/conformance_check.sh | STALE | hardening landed |
| 4208562232 | cpp/tools/conformance_bind.sh | STALE | hardening landed |
| 4209067514 | cpp/cmd/unittestrunner/main.cpp | STALE | regex_error catch + -run flag landed |
| 4209067524 | cpp/cmd/unittestrunner/main.cpp | STALE | same |
| 4209067603 | cpp/cmd/unittestrunner/main.cpp | STALE | same |
| 4209067611 | cpp/CMakeLists.txt | STALE | unittestrunner CI target landed |
| 4209067626 | cpp/internal/testutil/unittests/registry.h | STALE | TSCPP_UT_CAT* renaming landed |
| 4210552566 | cpp/CMakeLists.txt | STALE | comments current |
| 4210552580 | cpp/CMakeLists.txt | STALE | comments current |
| 4213555767 | .github/workflows/cpp.yml | STALE | pipefail landed |
| 4214256946 | cpp/cmd/unittestrunner/dbg_main.cpp | STALE | fixed at HEAD |
| 4214256972 | cpp/WINDOWS_PARITY.md | STALE | count corrected |
| 4215594539 | cpp/cmd/unittestrunner/main.cpp | STALE | T.Name() unqualified strip landed |
| 4215594574 | cpp/internal/project/session.cpp | STALE | stale ref-comment removed at HEAD |
| 4227526287 | cpp/cmd/fourslashrunner/main.cpp | STALE | skip=3 return contract documented |
| 4235714565 | cpp/RELIABILITY_REPORT.md | REAL | 291fd64245 — tooling-hardening claim now describes actual guards |
| 4235714573 | cpp/tools/project_parity.sh | REAL | 840d7a5e1d — bash watchdog fallback when timeout/gtimeout absent |
| 4235714580 | cpp/RELIABILITY_REPORT.md | REAL | 291fd64245 — deletion count corrected to 11 |
| 4235714599 | cpp/internal/format/rulecontext.cpp | REAL | faa316e427 — comment now cites ast.IsTrivia/IsTokenKind |
