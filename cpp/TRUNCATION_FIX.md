# TestQuickInfoCanBeTruncated — `[...]` vs `...` divergence

## Root cause

Not a compiler divergence — a stale test expectation. The C++ emitter was already
byte-identical to the vendored Go oracle; the ported C++ test still carried the
pre-bump expected string.

Upstream commit `15dee00a1c` "Detect cycles while serializing array and tuple
types" (vendored at `0aa28ceb16`) rerouted tuple serialization through
`visitAndTransformType` so cycles are detected. Its first statement is the
sticky-truncation guard:

- `tsc/internal/checker/nodebuilderimpl.go:3234` `visitAndTransformType`
  → `:3235` `if b.checkTruncationLength() { return b.createElidedInformationPlaceholder() }`
- `checkTruncationLength` (`nodebuilderimpl.go:147`) is sticky: once
  `ctx.truncating` is set it stays true for the builder lifetime.

In the marker-6 hover, the inner `_499` member is serialized *after* the
`... 477 more ...` elision already flipped `ctx.truncating` on
(`createTypeNodesFromResolvedType` per-property truncation,
`nodebuilderimpl.go:2770`). So Go now returns the bare `...` placeholder for the
tuple member type — it never reaches `arrayOrTupleTypeToNode`/`mapToTypeNodes`
(the path that produced `[...]` before `15dee00a1c`). Upstream changed the Go
test's expected marker-6 output `_499: [...]` → `_499: ...` in the same commit;
the ported C++ test was not re-synced, so it failed against correct C++ output.

## Fix

- `cpp/internal/fourslash/tests/tests_c_quickinfo2.cpp` — resynced the marker-6
  expected string to the vendored Go expectation (`_499: ...;` inside the inner
  mapped object).

## Bonus fix found while auditing the truncation path

`cpp/internal/checker/checker_nodebuilder.cpp` `visitAndTransformType`:
the reverse-mapped `symbolDepth[origin]` restore used `scopeExit` declared
*inside* the `if` block, so the decrement ran at block end instead of at
function exit like Go's `defer` (`nodebuilderimpl.go:3303`). Hoisted the guard
to function scope — matches Go's `defer` lifetime so `symbolDepth` stays
correctly bounded during the transform below.

## Toolchain portability (unrelated, needed to build under zig/libc++)

- `cpp/internal/api/callbackfs.cpp` — explicit `duration_cast` to
  `vfs::TimePoint::duration` for the `time_t`/nanos construction.
- `cpp/internal/lsp/lsp_handlers.cpp` — `static_cast<int64_t>` on the
  `time_since_epoch` count to disambiguate the `fmtArg` overload.

## Verification

- `./fourslashrunner -run 'TestQuickInfoCanBeTruncated'` → **PASS** (was 1 failing sub-check)
- `./fourslashrunner -run 'TestQuickInfo'` → **312/312 pass**
- `./fourslashrunner -run 'TestFindAllRefs'` → **187/187 pass**
- `./unittestrunner` → **1110/1110 pass**
- `./tsctestrunner` → **117/117 pass**
- `go build ./tsc/...` → clean (no Go files touched)
