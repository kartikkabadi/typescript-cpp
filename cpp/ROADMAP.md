# tscpp roadmap

The port proceeds stage-by-stage along the real compiler pipeline. Every
stage is ported faithfully from `tsc/internal` (function-by-function, no
stubbing) and verified against the Go implementation kept in-tree as the
oracle before moving on.

## Stage 1 — Front end (DONE)

- [x] Generated diagnostics catalog (`internal/diagnostics`)
- [x] Unicode identifier tables (`internal/stringutil`)
- [x] Arena allocator + core types (`internal/core`)
- [x] Generated AST node model: 192 structs, factory, per-kind accessors,
      forEachChild, subtree facts, predicates, deep clone
      (`internal/ast`, `tools/gencpp.py`)
- [x] Scanner incl. full regexp scanning (`internal/scanner`)
- [x] Parser: parser.cpp (all grammar productions), jsdoc.cpp, reparser.cpp
- [x] `tscpp` driver: `lex`, `lex-json`, `parse`, `bench`, `bench-parse`
- [x] Oracle drivers: `tsc/cmd/parsedump`, `tsc/cmd/lexdump`
- [x] Conformance: **26,006/26,006** corpus files byte-identical
      (nodes + flags + D/J/C diagnostics)

## Stage 2 — Binder & symbols (DONE)

- [x] Port `tsc/internal/binder` → `cpp/internal/binder/binder.cpp`
      (~2,300 lines: 167 functions — symbol tables, declaration/alias
      handling, ambient module patterns, CommonJS/exports expando logic,
      control-flow node graph, strict-mode diagnostics).
- [x] `tscpp bind` mode + `tsc/cmd/bindump` Go oracle: dumps per node —
      symbol flags/name/valueDeclaration/parent, localSymbol, flow-node
      flags, locals table, nextContainer chain, plus file-level
      CommonJS/External indicators, pattern ambient modules, global
      exports, symbolCount and bind diagnostics.
- [x] Conformance: 26,002/26,002 corpus files byte-identical
      (`cpp/tools/conformance_bind.sh`).

## Stage 3 — Checker (DONE)

- [x] Port `tsc/internal/checker` (~81k lines C++, every Go file covered:
      expressions, type-node resolution, signatures, members, widening,
      contextual typing, control-flow narrowing (`checker_flow`),
      assignability (`checker_relater`), inference
      (`checker_inference`), instantiation, nodebuilder,
      emitresolver, services, exports, grammarchecks, jsx,
      modulespecifiers, checkerpool (FENNEL parallel checkers)).
- [x] Conformance: `tscpp check <file>` vs `tsc/cmd/checkdump` —
      **12,734 / 12,734** corpus files byte-identical
      (`cpp/tools/conformance_check.sh`).

## Stage 4 — JS emit + project mode (DONE)

- [x] `printer` package (textwriter, emitcontext, namegenerator, factory)
      + all transformers (esnext→es5 lowering, async, classfields,
      esdeco, jsx, namedevaluation) + sourcemap/spanmap.
- [x] `tscpp emit` + `tscpp emitdump` vs `tsc/cmd/emitdump` —
      **12,734 / 12,734** byte-identical.
- [x] `tsconfig.json` / `-p` project mode: `tsoptions`, `module`
      resolution, incremental `.tsbuildinfo` — `tsc -p`/`-b` byte-identical
      incl. XXH3 parity (project parity harness: 109/109).

## Stage 5 — The rest of the stack

- [x] `--declaration` emit — **12,734 / 12,734** byte-identical.
- [x] Watch mode / incremental reuse (`ReuseProgram`, `sourceFileVersion`,
      `affectedFiles`) — tsctests watch scenarios all pass.
- [x] VFS + `tscpp tsc` CLI parity — byte-identical on --version/--help/
      --noEmit/--listFiles/--listEmittedFiles/--listFilesOnly/--showConfig/
      --traceResolution/--init/--locale/-b/-w/--dry/--clean/error paths.
- [x] `--lsp` / `--api` session protocols — byte-identical event streams.
- [x] `execute/tsctests` harness (`tsctestrunner`): **99/99** Go scenario
      ports pass (build/watch/commandline/composite/declarationEmit/noEmit,
      incl. Windows-path VFS).
- [x] fourslash LS-test harness — `fourslashrunner` (fork-per-test
      runner, static registry) + Go→C++ test transpiler. **4,560 tests
      ported — 4,130 / 4,130 PASS (100%), 430 faithful skips** (upstream
      `t.Skip`, GOOS-gated, feature-gated). Zero crashes/asserts/timeouts
      across the suite. Every divergence root-caused to a real port bug —
      see `internal/fourslash/tests/REPORT_{A,B,C,D}.md`.

## Performance work

- [x] Arena allocation for all AST nodes (single free per file).
- [x] Flat `std::vector` child lists (Go `[]*Node` equivalent, no boxing).
- [x] Scanner hot loop: ASCII fast paths, UTF-8 decode only when needed.
- [x] PGO / LTO build profile.
- [x] Parallel parse across files (`tscpp parse-all` — ~3.9× vs Go serial
      on the 26,002-file corpus).
- [x] FENNEL checker pool (parallel diagnostics/emit, ~250-300% CPU).
- [x] Emit-context arena release (decl-emit RSS 4 GB → ~340 MB).
- [x] mimalloc drop-in (mprotect storm 21,635→22 calls/run).
- [x] Parallel file parse (faithful filesparser.go:269 port — parse phase
      0.107 s → 0.034 s on perfproj, at parity with Go).
- [ ] Interned strings for identifiers & type references.
- [ ] **Gate: tscpp ≥ 3× Go `tsc --noEmit` — NOT MET; closed as
      contract-incompatible.** Final RUNS=9 medians on the 100-file
      perfproj: emit **1.049×**, --noEmit **0.991×**, --declaration
      **0.995×** — i.e. wall-clock parity (faster than Go on 2 of 3
      surfaces). The residual profile is flat (top symbol ~2.3%) and is
      composed of faithful-work shared with Go, contract-bound owning
      copies, and order-locked hashtable order that byte-identical output
      forbids changing. A ≥3× win requires non-faithful data-structure or
      scheduling changes — see `cpp/tools/perf/PERF_REPORT.md` (verdict +
      floor decomposition) and `cpp/tools/perf/HOTPATHS.md` (per-phase
      archaeology).

## Conformance rules

1. No stage is "done" until the corpus diff is 0 failures.
2. Corpus = every source file under `tsc/testdata` (minus `node_modules`
   fixtures and tooling). Extended with the `execute/tsctests` scenario
   suite (99/99), project-parity corpus (109/109), and `--lsp`/`--api`
   protocol streams.
3. Known-divergence items must be documented here with the Go-side
   rationale — currently: none.
