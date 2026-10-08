# testrunner port report

| Go file | C++ file | Verdict |
|---|---|---|
| test_case_parser_test.go | tests_test_case_parser.cpp | PASS (TestMakeUnitsFromTest) |
| transpile_runner_test.go | tests_transpile_runner.cpp | PASS (TestTranspile) |
| compiler_runner_test.go | tests_compiler_runner.cpp | ported, NOT registered — see below |
| testmain_test.go | — | skipped: scaffolding only (TestMain + baseline.Track setup), no testable unit |

## testrunner.TestLocal — deferred

`TestLocal` is fully ported (`runCompilerTests` mirrors Go's `TestLocal`:
regression + conformance `CompilerBaselineRunner`s, duplicate-basename
assert, `RunTests(t)`) but is not registered because it runs the entire
compiler corpus and requires **full baseline parity** — the C++ port
produces divergent baselines on a subset of files. Go passes `ok` in ~56s.

Observed product-level divergences (first few; all under
`tsc/testdata/tests/cases/compiler/`):

1. **Post-emit diagnostics dropped** — e.g. `allowSyntheticDefaultImports10`:
   `.errors.txt` contains `error TS-1: Pre-emit (2) and post-emit (0)
   diagnostic counts do not match!` — the emit resolver loses the two
   `TS2339` diagnostics Go reports. The `import(...)` specifier also renders
   as `"/.src/b"` instead of `"b"` (module name rendering diverges from Go).
2. **`.types`/`.symbols` text diffs** — e.g. `amdLikeInputDeclarationEmit`,
   `backslashBeforeNonSpecialChar`, `classVarianceResolveCircularity2`:
   type/symbol baseline output does not match the reference text.
3. **New baselines where Go produces none** — e.g.
   `cjsExportClassExpressionNameConflict.errors.txt`: the C++ port emits a
   diagnostics baseline the reference tree does not have (and reports
   `Found diagnostic with code -1`, i.e. a logged assertion violation).
4. **Mid-corpus crash (SIGILL)** — the run aborts with signal 4 shortly
   after `commonJsExportTypeDeclarationError` (compiler suite), killing the
   remaining files. `tscpp check` on the same file succeeds — the crash is
   in a harness/verification path, not plain check.

These are conformance-layer product bugs, not test-port defects; fixing
them belongs to the check+emit parity effort. Re-register
`testrunner.TestLocal` (commented-out REGISTER_UNIT_TEST in
tests_compiler_runner.cpp) once the port is baseline-perfect.

## Product bugs fixed by this port effort

- `harnessutil::testLibFolderMap` pushed every directory entry (including
  files) onto its descent stack; `directory_iterator` then hit files and
  failed with `ENOTDIR` → `tscUnreachable` ("Failed to read lib dir").
  Go's `fs.WalkDir` only descends into directories. Fixed by pushing only
  directories.
- `jsnum::BigUint::fromDigits` rejected `_` digit separators that Go's
  `big.Int.SetString(s, 0)` accepts.
- `gostd::testing::T::Run` propagated a skipped subtest's `skipped_` to the
  parent; Go reports `--- SKIP: TestX/child` but `--- PASS: TestX`. Fixed.
- `unittestrunner` used child exit code 2 for SKIP, colliding with
  `tscUnreachable`'s Go-parity `exit(2)` panic contract — a product panic
  was reported as SKIP. Skip sentinel is now 3.
