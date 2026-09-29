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

## Stage 2 — Binder & symbols (next)

- [ ] Port `tsc/internal/binder` → `cpp/internal/binder`
      (file-level symbol tables, flow-node graph, locals allocation).
- [ ] Conformance: dump each SourceFile's `locals`, `symbolCount`,
      `bindDiagnostics` in both implementations and diff.
- [ ] Extend `tscpp` driver with a `bind`/`symbols` mode.

## Stage 3 — Checker (the big one)

- [ ] Port `tsc/internal/checker` (~60k lines of Go) → `cpp/internal/checker`:
      type instantiation, inference, assignability, control-flow narrowing,
      relation results caching.
- [ ] Conformance: `tscpp check <file>` emits `TS<code>:pos:end` per
      diagnostic; diff vs `tsc` (Go) `--noEmit` on the whole corpus,
      then on DefinitelyTyped-style real-world projects.
- [ ] Performance budget: aim ≥3× Go `tsc --noEmit` on the std suite.

## Stage 4 — JS emit + project mode

- [ ] Port `printer` (emitter) + `transformers` (esnext→target lowering).
- [ ] `tscpp build <file>`: transpile `tsc` equivalent; diff emitted JS
      byte-for-byte vs Go `--emit`.
- [ ] `tsconfig.json` / `-p` project mode: options parsing
      (`tsc/internal/tsoptions`), module resolution
      (`tsc/internal/module`), incremental `.tsbuildinfo`.

## Stage 5 — The rest of the stack

- [ ] `--declaration` emit.
- [ ] Watch mode / incremental reuse (`sourceFileVersion`, `affectedFiles`).
- [ ] VFS + `tsc.cpp` CLI parity (same flags as `tsc`).
- [ ] Editor surface if in scope: language service queries
      (`ls/`), fourslash-style conformance.

## Performance work (ongoing)

- [x] Arena allocation for all AST nodes (single free per file).
- [x] Flat `std::vector` child lists (Go `[]*Node` equivalent, no boxing).
- [x] Scanner hot loop: ASCII fast paths, UTF-8 decode only when needed.
- [ ] PGO / LTO build profile.
- [ ] Interned strings for identifiers & type references.
- [ ] Parallel parse across files (checker Stage 3 prerequisite: none —
      parsing is embarrassingly parallel).

## Conformance rules

1. No stage is "done" until the corpus diff is 0 failures.
2. Corpus = every source file under `tsc/testdata` (minus `node_modules`
   fixtures and tooling). Extend with real-world repos for the checker.
3. Known-divergence items must be documented here with the Go-side
   rationale — currently: none.
