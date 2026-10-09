# C++ port cleanup audit

Branch: `devin/cpp-cleanup` (base `devin/cpp-port`).

Scope: dead code, port-debt markers, debug leftovers, stray artifacts, cheap build
warnings — while preserving byte-for-byte Go parity with the oracle under
`tsc/internal/` (untouched; `git diff tsc/` is empty).

## Marker classification (TODO/FIXME/XXX/HACK/DEBUG)

- ~96 files under `cpp/internal/**` matched. Each marker was grepped against
  `tsc/internal/**`; comments that appear verbatim in the Go oracle (including
  upstream `TODO(...)`, `NOTE`, `HACK` comments citing GitHub users/issues) were
  **kept exactly**.
- Port-side `TODO`/`NYI` comments that mirror an existing `// TODO` / unimplemented
  path in the Go source were kept (they are parity notes, not debt).
- Our own scaffolding markers were removed: `int _ = 0;` placeholder statements
  (~20 sites across fourslash/compiler test files), orphan comment blocks, and
  commented-out dead code that has no Go counterpart.

## Removed (our port debt / dead code)

- `checker_members.cpp`: dead `bool instantiated` flag — Go used it only to guard a
  `maps.Clone` the port deliberately dropped (comment retained).
- `tsconfigparsing.cpp`: dead `totalContentMapperExtensions` accumulator loop.
- `checker_typeops.cpp`: unused `isDeprecated` lambda (Go inlines
  `IsDeprecatedDeclaration`).
- `program.cpp`: vestigial `{ auto& o = *opts.Config->...; }` block;
  `for (; x < n;)` → `while (x < n)` where Go uses `for x < n`.
- `core/types.h`: dead `allMatch` flag in `Filter`.
- `baselineutil.cpp`: empty `for (auto& r : ranges) {}` loop.
- `fourslash.cpp`: unused `resultAsSpans.back()` reference; narrowed lambda captures.
- `patience.cpp`: unused `sliceTo` lambda.
- `session.cpp`: unused `pid` reference and dead lambda capture.
- `checker_utilities.cpp`: dead `kEscapedChars` table.
- `completionlist`/`ParsedCommandLine`: `this == nullptr` / `this != nullptr` guards —
  UB-adjacent dead checks; call sites already nil-guard (Go nil-receiver semantics).
- `tests_packagejson.cpp`: orphaned `template <typename T>` line.

## Warning fixes (behavior-preserving)

- `-Wunused-variable` / unused-but-set: removed or `[[maybe_unused]]` for
  TSC_ASSERT-only locals (Go keeps the vars for the asserts).
- `-Wunused-lambda-capture`: dropped unused captures (19 sites); `this` restored
  wherever the lambda actually calls a member (`lsdeps.h`).
- `-Wmissing-override` → added `override` (program.h ×10, textwriter `Grow`).
- `-Wreorder-ctor` / `-Wreorder-init-list`: init lists reordered to declaration
  order (arena.h, api.cpp, session/test fixtures).
- `-Wsign-compare`: explicit `static_cast`s (programtosnapshot, watchmanager,
  test_parser, sourcedefinition, scanner, testutil).
- `-Wparentheses` / `-Wbitwise-op-parentheses` / `-Wlogical-op-parentheses` (~185
  sites, clang fixits): added explicit parens — no semantic change; mixed
  `^`/`|`/`&&`/`||` expressions parenthesized to match the actual precedence.
- `-Wtautological-undefined-compare`: removed null-`this` checks (see above).
- `-Wmismatched-tags`: `class SourceFile;` → `struct SourceFile;`; dropped
  redundant `class Marker` elaborated specifier.
- `-Wgnu-designator`: GNU `field:` designators → standard C++20 `.field =` (13).
- `-Wtrigraph` (scanner.cpp): `{ "?\?=", ... }` escaped per upstream table intent.
- `-Wunused-function` (68): `[[maybe_unused]]` applied.
  - 50 of these **exist in the Go oracle** (same helper name found in
    `tsc/internal/`) — faithful ports Go itself keeps; annotated, not removed.
  - 18 have no Go counterpart (`stringsCut`, `urlPathEscape`, `unmarshalRawSourceMap`,
    `isObjectBindingOrAssignmentElement`, etc.) — annotated rather than deleted as a
    conservative choice; flagged here for a future review pass.
- `-Wunused-local-typedef`: removed `using T` where genuinely unused; **restored** in
  12 functions that do use it (contextual/decltypes/instantiate map/filter helpers).

## Real bugs found and fixed (divergences, not just warnings)

1. **`gencpp.py` regex bug → `privateName` shadow** (nodes_generated.h):
   `re.match(r"(\w+) :?= (.*)$")` made plain `=` emit `auto x =`, shadowing the
   outer variable — `computeSubtreeFacts_PropertyAccessExpression` always dropped
   the private-identifier bit vs Go `ast.go:2114`. Fixed in the generator and
   hand-applied to the checked-in header (full regen deferred — the oracle has
   drifted since the last generation, e.g. `IsContentMapped`).
2. **`propagateSubtreeFacts`**: 20 generated sites emitted an unused
   `static_cast` self-variable; fixed generator + applied to checked-in header.
3. **`tests_c_autoimport2.cpp`**: two `R"TS"` fixtures were missing a blank line
   vs the Go fixture (`\n\n\n`) — caused fourslash baseline diffs
   (`TestAutoImportNewLine`, `TestAutoImportNewLineWithHeaderComment`). Fixed;
   tests now match the Go baseline.

## Deliberately kept

- `TSC_UNREACHABLE` — faithful Go panic port.
- Per-TU file-local replicas of TU-local Go helpers (PORTING.md convention).
- Go-side `// TODO(jakebailey)`-style comments — verbatim oracle comments.
- Fix-report docs (TRUNCATION_FIX.md, SNAPREF_FIX.md, DOCHL_FIX.md,
  CODEFIXTRUNC_FIX.md) — audit trail; indexed in `cpp/README.md`.
- Debug `fprintf`/`printf` in runners/tools only (no stray prints in internals).

## Remaining real gaps / structural warnings skipped

- `-Wmissing-field-initializers` (~247 sites): deliberate `Type{cond}` Go-style
  zero-init idiom; silencing requires touching every struct literal — skipped.
- `-Wswitch` (~154 sites): exhaustive-enum switches that intentionally rely on
  `default:`-less patterns matching Go's switch coverage — structural, skipped.
- `lsutil.h` marker kept: mirrors a Go comment.
- 18 `[[maybe_unused]]` non-oracle helpers listed above await a definitive
  keep/delete review.

## Verification

- `ninja -C cpp/build tscpp unittestrunner tsctestrunner fourslashrunner` green.
- `unittestrunner` 1110/1110, `tsctestrunner` 117/117.
- `fourslashrunner -run 'Test[A-C]'` all pass.
- `git diff tsc/` EMPTY; `go build ./tsc/...` green.
- `.github/workflows/cpp.yml` reviewed — mirrors this local flow; unedited.
