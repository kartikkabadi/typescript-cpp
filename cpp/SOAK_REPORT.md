# Soak + determinism report — `devin/cpp-soak`

Reliability pass on the C++23 tsc port: repeated suite runs, corpus dump
hashing, threaded `tsc -b` stress, and scripted watch-mode mutation loops.
Base: `be2eae305a` (`devin/cpp-port` tip). Three defect classes found and fixed
(the third is a real nondeterminism bug in the emit path; details below).

## Runs × suites matrix

| Suite | Runs | Result |
|---|---|---|
| unittestrunner | 9 | 1122/1122 ×8; run5 = 1121/1122 (1 crash → fixed) |
| tsctestrunner | 8 | 117 passed, 0 failed, 0 skipped — every run |
| fourslashrunner | 5 | 4133 ×2 (12 deterministic fails → fixed), 4143 ×1, then **4145/4145** |
| `tscpp check` dump | 2 | 26,172 files — **byte-identical across runs** |
| `tscpp emit` dump | 2 | 26,172 files per run (see hash diff below) |
| `tscpp emit --declaration` dump | 2 | 26,172 files per run (see hash diff below) |
| `tscpp tsc -b` composite | 32 concurrent builds | **1 signature** — byte-identical incl. `.tsbuildinfo` |
| watch mutation loops | 8 scenarios ×4, tsconfig-edit ×8 | 7/8 scenarios fully deterministic; see below |

Per-test status diffing across runs (PASS/FAIL/SKIP set membership) found
exactly one flip in unit runs (the crash below) and zero flips in tsctest
and fourslash.

## Flakes / divergences found + root causes

### 1. `ipc.TestSyncConnAnswersNestedRequestsInStackOrder` — intermittent SIGSEGV (FIXED)

- Symptom: FAIL in 1/8 suite runs; under 30× concurrent stress repro,
  10/30 crashed. Post-fix stress: 60/60 clean; post-fix full suite green.
- Crash stacks: `mimalloc mi_heap_page_collect` during heap-abandon collect,
  and `shared_ptr<ErrObj>::operator bool` → `basic_string::__is_long` —
  a `gostd::Error` shared_ptr read through a dangling stack reference.
- Root cause (test-side UB, not a runtime bug): the recursive `serve`
  lambda was `std::function<json::Value()> serve; serve = [&]()` — every
  capture by reference, including `peer`, `answers`, and `serve` itself.
  The detached server thread is designed to outlive the test frame
  ("heap-pinned shared_ptrs let detached threads outlive the test frame"),
  so once `t->Fatal`/return unwound the frame, the detached copy held refs
  to dead stack slots. Whether `serve` touched them raced with teardown —
  hence flaky.
- Fix (`cpp/internal/ipc/tests/tests_conn_sync.cpp`): `serve` is now a
  `std::shared_ptr<std::function<json::Value()>>` (heap-pinned, self-owned
  for recursion) and the lambda captures `[serve, peer, answers]` by
  value. Faithful to Go: `tsc/internal/ipc/conn_sync_test.go` relies on
  GC to keep these alive — the C++ port now achieves the same via
  ownership instead of stack borrowing.

### 2. 12 fourslash tests — deterministic failures (FIXED, not env-dependent)

`TestFormatAfterWhitespace`, `TestFormat{No,}SpaceAfterTemplateHeadAndMiddle`,
`TestFormatSelectionWithTrivia2`, `TestGoToImplementation{InterfaceProperty_00,Namespace_06}`,
`TestQuickInfo{CommentsCommentParsing,ForJSDocWithHttpLinks,InheritDoc}`,
`TestSignatureHelpCommentsCommentParsing{,VS}`, `TestGoToDefinitionSameFile`.

- Root cause: cleanup commit `d27b1dacc0` deleted blank lines inside
  `R"TS(...)` literals (both `content` fixtures and `VerifyCurrentFileContent`
  expectations), desyncing them from the vendored Go tests. Verified by
  `git show 3ded3f4224:...` (original port, Go-faithful) vs HEAD —
  e.g. `TestFormatAfterWhitespace` expected `\n\n}` instead of Go's
  `\n\n\n}`; `formatNoSpaceAfterTemplateHeadAndMiddle` a3 template lost
  a blank line inside a `std::string(R"TS()` concat segment.
- Fix: restored all deleted empty lines occurring inside `R"TS` literals
  in `tests_c_formatting2.cpp`, `tests_c_gotoimpl.cpp`,
  `tests_c_quickinfo2.cpp`, `tests_c_sighelp.cpp`, `tests_d_goto.cpp`
  (26 blanks) + one more a3 blank in each template-spacing test.
  Re-verified: full suite **4145/4145**, and the only per-test delta vs
  the failing runs is exactly those 12 (no PASS↔SKIP movement).
- Lesson: edits that touch the interior of `R"TS(...)` literals change
  test inputs; a blank line is content, not dead code.

### 3. Watch-mode `tsconfig-edit` — residual extra rebuild cycle (ORACLE-INHERENT, not a bug)

- Earlier harness artifact (non-atomic config write → watcher parsed a
  transient empty tsconfig → default `**/*` include → emitted
  `src/main.js`) was fixed harness-side with tmp+rename; verified
  `tscpp tsc` and `tsgo` behave identically on an empty tsconfig anyway.
- Post-fix residual: 2/8 fresh iterations show one extra
  `File change detected…Found 0 errors` cycle. Emitted file trees are
  byte-identical; the diff is stdout-only. The Go oracle under the same
  harness exhibits the same class (NONDET on 1/8 iters): an fs-event batch
  occasionally splits across the 50ms debounce boundary, producing an
  extra output-identical build. Faithful ports
  (`cpp/internal/fswatch/debounce.cpp` ↔ `tsc/internal/fswatch/debounce.go`
  verified line-by-line) inherit it. No action.

### 4. `tscpp emitdump` — intermittent whole-file output loss (FIXED; real nondeterminism)

- Symptom: `emitdump` runs on `fixtures/compiler/moduleSpecifiers.ts` produced
  73–77 of 78 `W` sections under 40× concurrent stress (never below ~73,
  occasionally 0 when the process was starved). Same class inside the corpus:
  emit run1-vs-run2 showed whole `W <file>` sections missing in 37 files
  (after excluding 50 harness-poisoned artifacts, see below); decl
  run1-vs-run2 showed the same whole-W loss in 40 files.
- Root cause: `program.Emit` runs emit tasks on `parallelWorkGroup` threads
  (Go-parity: `SingleThreaded()==false` for a bare file-args CLI).
  `emitdumpFile`'s `WriteFile` callback appended to a plain
  `std::vector written` with no synchronization — concurrent
  `emplace_back` during realloc loses entries nondeterministically.
  Diagnostics (`F`/`T` sections) come from the sequential
  `getDiagnosticsOfAnyProgram` path and were never affected — every
  verified W loss carried identical diagnostics.
- Oracle check: `tsc/cmd/emitdump/main.go` has the same unguarded
  `written = append(...)` inside `program.Emit`, but the Go scheduler
  never loses an append in practice (12/12 clean on the same fixture at
  12× concurrency). The C++ race fires at ~10–40% of runs under load.
- Fix (`cpp/cmd/tscpp/main.cpp`): a mutex now serializes the appends.
  Post-fix verification: 60× concurrent moduleSpecifiers runs → 48×78,
  12 exec-failures under resource starvation (exit 127/OOM-adjacent, not
  output corruption); a controlled 12× run → 12×78 exit-0; all 87
  previously-differing corpus files ×2 sequential → 0 diffs.

### 5. `.d.ts` union member order — excluded per task (oracle-side map-order nondeterminism, documented in `cpp/E2E_REPORT.md`). No order flake observed belongs to this class.

### Harness lesson: wave poisoning

The emit run2 / decl run2 corpus waves overlapped a `ninja` relink of
`tscpp` while the nondeterminism was being instrumented: 50 emit files
and 340 decl files contain `timeout: failed to run command … RC 127`.
Those are excluded from all hash comparisons. Soak rule going forward:
never rebuild the binary under test while a dump wave is in flight.

## Corpus determinism hashes

`~/soak/soak_dump.sh` ran `tscpp <mode> <file>` per corpus file
(26,172 files under `tsc/testdata`), recording stdout+stderr+RC per file.

- check: run1 ≡ run2 (byte-identical, 26,172/26,172).
- emit run1-vs-run2 (pre-fix binary): 87 diffs = 50 RC-127 harness
  artifacts + 37 whole-W losses from the `written` race.
- decl run1-vs-run2 (pre-fix binary): 340 RC-127 artifacts excluded;
  40 diffs, all whole-W losses (the same race — decl shares
  `emitdumpFile`).
- emit run3/run4 (post-fix): launched; hash diff will be appended here
  before final commit.

## Threaded stress

`~/soak/soak_tscb.sh` + `gen_composite.py`: 7-project composite build graph
(6 chained libs + app + solution tsconfig), 32 concurrent `tscpp tsc -b`
invocations (waves of 8) → **1 signature** across all runs — emitted js/dts
and `.tsbuildinfo` files byte-identical under concurrency.

## Commits on this branch

- `74ddbb0d3f` soak: fix 2 flake classes found by repeated runs
- pending: emitdump WriteFile append mutex (flake #4)

## Notes / non-findings

- `tsgo` CLI quirks encountered while oracle-checking the harness: no
  `tsc` subcommand, `--pretty false` misparses `false` as a filename,
  `--pretty=false` is TS5023. Harness uses bare `-w`.
- Unit runs show 1,128 distinct test names: 1,122 pass + 6 faithful SKIPs,
  constant across all runs (no SKIP↔PASS flips).
