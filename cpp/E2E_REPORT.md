# E2E report — C++ TypeScript port vs Go oracle on real-world codebases

Branch: `devin/cpp-e2e` (base `devin/cpp-port`).

Goal: verify `tscpp` is byte-identical to the vendored Go oracle (`tsc/internal`,
built as `/tmp/tsgo-tsc` with Go 1.27) on large real-world TypeScript projects:
diagnostics (stdout+stderr+exit code), emitted trees, `tsc -b`, and
`--extendedDiagnostics` counts.

## Environment

- clang++-15 / lld-15 / ninja / CMake release build (`-DCMAKE_BUILD_TYPE=Release`)
- Go oracle: `cd tsc && go build -o /tmp/tsgo-tsc ./cmd/tsc` (Go 1.27.x)
- Port: `cpp/build/tscpp` invoked as `tscpp tsc …`
- node 22 toolchain at `~/tools/node22`

## Targets

| repo | version | config used | program size |
|---|---|---|---|
| `microsoft/TypeScript` | `release-6.0` branch | `src/compiler/tsconfig.json` (module NodeNext, project build via `src/tsconfig.json`, 13 refs) | 248 files in compiler proj |
| `nest` (nestjs/nest) | tip of `master` | root `tsconfig.json` (Node16 + paths, `include: packages/**, integration/**`) | 670 source files, 2851 incl. libs |
| `effect` (Effect-TS/effect) | commit `13ace20`, pnpm install | `packages/effect/tsconfig.json` (extends strict `tsconfig.base.json`, NodeNext, composite+declaration+declMap) | 503 files |

Setup notes:
- `microsoft/TypeScript@main` no longer has `src/`; `release-6.0` was used.
- nest's `npm ci` fails with ERESOLVE on its own lockfile; installed with
  `npm install --legacy-peer-deps` (documented as a setup deviation — the
  installed node_modules tree is what matters for resolution coverage).
- effect installs cleanly with pnpm (1074 packages).

## Results

| surface | TS6 `src/compiler` | nest | effect `packages/effect` |
|---|---|---|---|
| `--noEmit` diagnostics | PASS — byte-identical (21 errors each, exit 2) | PASS — byte-identical (40 lines each, exit 2) | PASS — byte-identical (3 pre-existing upstream errors, exit 2) |
| emit diagnostics | PASS — byte-identical | PASS — byte-identical | PASS — byte-identical |
| emitted tree (`diff -r`) | PASS — all files identical | PASS — all files identical | 6 files differ — **Go oracle nondeterminism**, see below |
| `--declaration` emit | (config already declares) | PASS — 1082 `.d.ts` byte-identical, diag identical | (config already declares) |
| `tsc -b` | PASS — `src/tsconfig.json` (13-project solution): diagnostics byte-identical, whole `built/local` tree byte-identical incl. `.tsbuildinfo` | — | — |
| `--extendedDiagnostics` | PASS — all counters identical (Files 248, Lines 258687, Identifiers 425247, Symbols 117348, Types 332, Instantiations 0); only timing/memory columns differ (normalized) | — | — |
| determinism (2 runs) | both tools deterministic | — | tscpp deterministic; **Go oracle nondeterministic on emit** — see below |

All runs were performed from the same cwd per tool pair; absolute-path
prefixes were the only normalization applied (none needed — every run used the
same directories).

## Divergences found

### 1. `TypeFactsFunctionStrictFacts` used the wrong fact bit — fixed `7fb5f2a6f5`

**Symptom.** On `microsoft/TypeScript` `release-6.0`, `tscpp -p src/compiler/tsconfig.json --noEmit`
printed one extra error vs the oracle:
`src/compiler/transformer.ts(214,…): error TS2345` — a function-typed value
survived `typeof x !== "function"` narrowing and failed an assignment check.

**Root cause.** `cpp/internal/checker/checker.h` defined
`TypeFactsFunctionStrictFacts` with `TypeFactsTypeofNEFunction` where Go
`checker.go:469` uses `TypeFactsTypeofNEObject`. The wrong bit meant
`getTypeWithFacts` never removed function types under `typeof !== "function"`,
leaving a bogus candidate that produced the spurious TS2345. One-line fix;
the whole TypeFacts constants block was re-audited against Go — no other
substitution errors.

**Verification.** Repro file `typeof f !== "function"` narrowing of a union
member is now identical (exit 0 both). Full TS6 noEmit + emit re-run after the
fix: diagnostics and emitted tree byte-identical.

### 2. `areTypeParametersIdentical` crashed on nest — fixed `99e9fa103e`

**Symptom.** `tscpp tsc -p tsconfig.json` on nest exited 2 with **zero output** —
no diagnostics, no emitted files. `TSCPP_DEBUG_CRASH=1` showed `TSCPP-CRASH
sig=11` (SIGSEGV); gdb traced it to
`Checker::areTypeParametersIdentical` ← `checkInferType` dereferencing a corrupt
`Node*` inside `Node::text()`.

**Root cause.** The infer-type-parameter call site in `checkInferType` passed
`[](Node* decl) -> std::vector<Node*> { return {decl}; }` into a parameter
typed `std::function<NodeSlice(Node*)>`. `NodeSlice` is a non-owning
`std::span` view; the temporary vector the lambda returned was destroyed when
the `std::function` call completed, so `sourceParameters` viewed freed memory.
Go keeps the returned slice alive via GC, so the Go code is fine.

**Fix.** Changed the getter's signature to
`std::function<std::vector<Node*>(Node*)>` (`checker.h`, `checker_stmtclass.cpp`).
The class/interface call site returns `n->typeParameters()` — a `NodeSlice`
that converts implicitly to `std::vector<Node*>` — and the infer call site
already produces a real vector. Semantics identical to Go (the slice is only
read); the fix also removes the dangling-footgun for this API.

**Verification.** nest `--noEmit`: byte-identical 40 error lines, exit 2.
nest emit and `--declaration` emit: fully identical trees.

### 3. Emitted `.d.ts` union-member order differs on effect — **Go oracle nondeterminism, not a port bug**

**Symptom.** effect emit: 6 files differ between Go and C++ —
`ai/McpSchema.d.ts`, `ai/McpSchema.d.ts.map`, and
`ai/internal/mcpProtocol/v{2024_11_05,2025_03_26,2025_06_18,2025_11_25}.d.ts`.
All diffs are the *member order* of unions of deferred `Rpc<"tag", …>` type
references; the member sets are identical modulo order (verified by sorting).

**Root cause.** Union members are deferred `Rpc` type references sharing the
same `node` (`Rpc.make`'s return-type annotation), so `CompareTypes` orders
them via `compareTypeMappers`. The mapper is built in
`getObjectTypeInstantiation` from `getOuterTypeParameters(declaration)`, which
collects conditional-type `infer` parameters by iterating `node.Locals()` —
a `map[string]*Symbol` in Go (`tsc/internal/ast/symbol.go:45`). **Go's
per-run randomized map iteration makes the oracle's own output
nondeterministic**: two consecutive `/tmp/tsgo-tsc` runs on the same input
emit different member orders (verified). The C++ port iterates
`std::unordered_map` (`cpp/internal/ast/symbol.h:40`), which is a fixed but
arbitrary order — self-deterministic across runs, but not equal to any given
Go draw. Diagnostics are unaffected (Go's diagnostic output is deterministic;
only emitted declaration order varies). There is no stable Go output to match,
so the C++ side is left deterministic; flagged as an oracle-side finding.

## Coverage notes / surfaces not exercised

- `tsc -b` exercised on the 13-project TypeScript solution (diagnostics +
  emitted tree + `.tsbuildinfo` all identical). Not run on effect's 38-package
  solution graph.
- Watch mode, incremental re-emit, and LSP/fourslash surfaces were not part of
  this e2e (fourslash has its own in-repo 4,130-test corpus).
- `--extendedDiagnostics` compared on TS6: all diagnostic counters identical;
  timing and memory columns legitimately differ (normalized out per plan —
  C++ does not implement Go-style memory accounting).
- The crash fix (divergence 2) was re-verified against all three targets
  after the rebuild: all PASS results above are from the final binary.

## Conclusion

3 surfaces × 3 targets plus build mode and extended diagnostics: **all pass
byte-identically** after two product fixes, except effect's emitted `.d.ts`
union ordering, which is nondeterministic *in the Go oracle itself* and cannot
be matched. Two real port bugs found and fixed as faithful ports:

- `7fb5f2a6f5` — `TypeFactsFunctionStrictFacts`: `TypeofNEFunction` → `TypeofNEObject`
- `99e9fa103e` — `areTypeParametersIdentical`: dangling `NodeSlice` over a
  temporary `std::vector` → SIGSEGV on `infer` type-parameter checks
