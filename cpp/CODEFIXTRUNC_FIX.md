# TestCodeFixClassImplementInterfaceNoTruncationProperties — first-divergence analysis

Branch: `devin/cpp-codefixtrunc`. Base: `devin/cpp-port` @ `7c7ef9f328`.

## Verdict

**Stale test expectation, not a port bug.** The C++ nodebuilder produces the
post-`65a8caeef8` oracle output; the ported test's 1.5 MB `NewFileContent`
literal still carried the pre-bump tail.

## First divergence

Comparing the vendored Go test's `NewFileContent` literal
(`tsc/internal/fourslash/tests/codeFixClassImplementInterfaceNoTruncationProperties_test.go`)
against the ported literal in `cpp/internal/fourslash/tests/tests_codefix.cpp`
(`TestCodeFixClassImplementInterfaceNoTruncationProperties`):

- 1,500,724-char common prefix; only the final member differs:

```
Go (expected, vendored):   ... /*... 527 more elided ...*/ zz: /*elided*/ any; }): void { ... }
C++ port (stale):          ... /*... 527 more elided ...*/ zz: {  /*elided*/ }; }): void { ... }
```

## Root cause

Oracle bump `fed0bf24` vendored upstream `65a8caeef8` ("Avoid cloning cached
declaration types after truncation (#63969)", vendored here as `af585c53bb`).
That commit added an up-front truncation check in `visitAndTransformType`:

```go
// tsc/internal/checker/nodebuilderimpl.go:3211 (post-bump)
func (b *NodeBuilderImpl) visitAndTransformType(t *Type, transform ...) *ast.TypeNode {
	if b.checkTruncationLength() {
		return b.createElidedInformationPlaceholder()
	}
	...
}
```

Effect on this test: `zz` is the last of 676 outer mapped-type members; by the
time its type serializes, the truncation budget is spent.

- Pre-`65a8caeef8`: `visitAndTransformType` descended into `zz`'s anonymous
  object type, emitted `{ `, then hit the budget writing members →
  `zz: {  /*elided*/ }`.
- Post-`65a8caeef8`: the up-front `checkTruncationLength()` returns
  `createElidedInformationPlaceholder()` which, under
  `nodebuilder.FlagsNoTruncation` (the codefix's flag set), emits
  `any` + synthetic `/*elided*/` comment → `zz: /*elided*/ any`
  (`nodebuilderimpl.go:342-348`).

The same upstream commit updated the Go test expectation in lockstep
(`zz: {  /*elided*/ }` → `zz: /*elided*/ any`, 2-line diff inside the 1.5 MB
literal). The C++ port received the nodebuilder change
(`checker_nodebuilder.cpp:4948-4950`, plus the `be818991ce` defer-scope fix),
so C++ actual output already produces `/*elided*/ any` — but the ported test
literal was never resynced. Same class as `TestQuickInfoCanBeTruncated`
(`devin/cpp-truncfix`): `af585c53bb` flipped both test files; only quickInfo's
port string was resynced at the time.

## Go sites

- `tsc/internal/checker/nodebuilderimpl.go:3211` — `visitAndTransformType`
  early `checkTruncationLength()` return (vendored `65a8caeef8`).
- `tsc/internal/checker/nodebuilderimpl.go:342` —
  `createElidedInformationPlaceholder` (`FlagsNoTruncation` → `/*elided*/ any`).
- `tsc/internal/fourslash/tests/codeFixClassImplementInterfaceNoTruncationProperties_test.go`
  — authoritative expectation.
- C++: `cpp/internal/checker/checker_nodebuilder.cpp:4948` — already correct.

## Fix

`cpp/internal/fourslash/tests/tests_codefix.cpp` — in
`TestCodeFixClassImplementInterfaceNoTruncationProperties`, resynced the
`NewFileContent` tail `zz: {  /*elided*/ }` → `zz: /*elided*/ any` so the
ported literal is byte-identical to the vendored Go test's.

## Verification

- `go test ./internal/fourslash/tests -run TestCodeFixClassImplementInterfaceNoTruncationProperties` — PASS (Go ground truth, `zz: /*elided*/ any`).
- `./fourslashrunner -run 'TestCodeFixClassImplementInterfaceNoTruncationProperties'` — PASS.
- `./fourslashrunner -run 'TestCodeFix'` — regression: all codefix tests.
- `./fourslashrunner -run 'TestQuickInfo'` — regression: all quickinfo tests.
- `./unittestrunner` — 1110/1110.
- `./tsctestrunner` — 117/117.
- `go build ./tsc/...` — clean.

## Note

`cpp/internal/fourslash/tests/REPORT_B.md` (6c) documents an unrelated
historical flake for this test — intermittent `std::system_error: Resource
deadlock avoided` under batch load in threaded session infra — distinct from
this deterministic content mismatch.
