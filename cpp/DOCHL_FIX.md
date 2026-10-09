# TestDocumentHighlights02 — missing `/b.ts` sections in multi-file document highlights

## Root cause

The C++ fourslash harness built `file://` URIs for `filesToSearch` (and for the
rename/willRenameFiles paths) from bare fixture filenames (`"a.ts"`, `"b.ts"`)
by calling `lsconv::FileNameToDocumentURI(file)` directly. The Go oracle wraps
each fixture name in `tspath.ToRootedFilePath(file, rootDir)` first, producing
`"/a.ts"` → `file:///a.ts`.

Without the wrap, the server-side `documentUriFileName` conversion produced
`//a.ts` — a path that does not exist in the program — so
`getScriptInfo`/`GetSourceFile` missed for every searched file. The
multi-document-highlight handler fell back to `{sourceFile}` (the active file
only), emitting only `/a.ts` highlights and none for `/b.ts`.

The LSP handler itself (`textDocument/multiDocumentHighlight`, upstream
`46441536b51`) was already implemented in the port — verified by instrumenting
the handler: the request arrived, `FilesToSearch` was parsed, but every file
lookup failed because the URIs were wrong at the harness side. The 61-commit
oracle bump regenerated `documentHighlights02.baseline.jsonc` to include the
`/b.ts` sections, which exposed the harness URI bug.

## Go oracle sites

- `tsc/internal/fourslash/fourslash.go:3794` — `verifyBaselineDocumentHighlights`:
  `FileNameToDocumentURI(tspath.ToRootedFilePath(file, rootDir))`
- `tsc/internal/fourslash/fourslash.go:5063` — `VerifyRename` `getScriptInfo`
  on `ToRootedFilePath(fileName, rootDir)`
- `tsc/internal/fourslash/fourslash.go:5078-5083` —
  `VerifyWillRenameFilesEdits`: `OldUri`/`NewUri` and `getOrLoadScriptInfo` all
  on `ToRootedFilePath(...)`

## Fix (cpp/internal/fourslash/fourslash.cpp)

Same three call sites, wrapped identically with
`tspath::toRootedFilePath(file, tspath::rootedDirectoryPathFromNormalized(rootDir))`
(fourslash `rootDir == "/"`):

- `verifyBaselineDocumentHighlights` — `FilesToSearch` URI construction.
- `VerifyRename` — `getScriptInfo` lookup.
- `VerifyWillRenameFilesEdits` — `OldUri`/`NewUri` construction and
  `getOrLoadScriptInfo` lookups (file open + param conversion).

Added `#include "internal/tspath/typed_paths.h"`.

## Verification

- `./fourslashrunner -run 'TestDocumentHighlights'` — 12/12 pass
  (previously `TestDocumentHighlights02` failed on baseline diff).
- `./fourslashrunner -run 'TestDocument'` — 41/41 pass (no regressions in
  rename/willRenameFiles either — those baselines were already passing because
  single-file lookups still resolved through a different fallback path, and
  remain byte-identical).
- `./fourslashrunner -run 'Baseline'` — 0 tests matched.
- `unittestrunner` — 1110/1110. `tsctestrunner` — 117/117.
- `go build ./tsc/...` — green (all temporary Go/c++ instrumentation reverted;
  `git diff tsc/` and `checker_nodebuilder.cpp` are clean).

## TestCodeFixClassImplementInterfaceNoTruncationProperties — classification

Still fails on `devin/cpp-dochl`, but it is a **stale ported test
expectation**, not a runtime divergence — sibling-owned
(`devin/cpp-codefixtrunc` already carries the resync).

Upstream `af585c53bb`/`65a8caeef8` (in the oracle bump) added an up-front
`checkTruncationLength()` early-return in `visitAndTransformType`, so the last
of the 676 mapped members now serializes as `zz: /*elided*/ any;` under
`FlagsNoTruncation` instead of descending into `zz: {  /*elided*/ };`. The
vendored Go test literal was updated; the ported literal in
`tests_codefix.cpp` was not — it still ends `zz: {  /*elided*/ };`.

Verified by instrumenting both nodebuilders: the cpp port fires the same
`visitAndTransformType` head-trunc once (same typeid 101081), and
`addPropertyToElementList(zz)` returns the `/*elided*/`-annotated `AnyKeyword`
node — i.e. our runtime output already matches the new Go oracle
(`zz: /*elided*/ any`). The `{ /*elided*/ }` text exists only in the ported
test's stale expected literal. Byte-identical literal resync fixes it; left to
the sibling fix per the task split.
