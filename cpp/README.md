# tscpp — TypeScript compiler, ported to C++

`cpp/` is a function-by-function C++ port of `tsc/internal` — the Go
implementation of the TypeScript compiler (the "corsa" rewrite that ships as
`tsc` in TypeScript 7+/native-preview). The goal of the port is a compiler
that is *significantly faster* than both the JS (`typescript.js`) and Go
implementations, with **zero behavioral regressions** — verified byte-for-byte
against the Go implementation, which is kept in-tree as the conformance
oracle. Oracle pin: `microsoft/TypeScript@aa814927` (2026-10-10) — see
`cpp/ORACLE_BUMP.md` and `cpp/ORACLE_DELTA.md` for the bump ledger.

## Status

| Component | Source (Go) | Port | Status |
|---|---|---|---|
| Diagnostics catalog | `diagnostics/` | `cpp/internal/diagnostics` | complete (generated) |
| Unicode tables | `stringutil/unicode` | `cpp/internal/stringutil` | complete (generated) |
| Number/JSON scanning helpers | `jsnum` | `cpp/internal/jsnum` | complete |
| Path handling | `tspath` | `cpp/internal/tspath` | complete |
| AST node model + factory | `ast/` | `cpp/internal/ast` | complete (generated structs) |
| Scanner | `scanner/` | `cpp/internal/scanner` | complete, incl. regexp |
| Parser | `parser/parser.go` | `cpp/internal/parser/parser.cpp` | complete |
| JSDoc parser | `parser/jsdoc.go` | `cpp/internal/parser/jsdoc.cpp` | complete |
| Reparser (JSDoc→declarations) | `parser/reparser.go` | `cpp/internal/parser/reparser.cpp` | complete |
| Binder | `binder/` | `cpp/internal/binder` | complete |
| Checker | `checker/` | `cpp/internal/checker` | complete — every Go checker file ported function-by-function: bootstrap, mapper/link store, JSDoc, tracer, grammar checks, node copy, module resolution, statements/class machinery, signatures, members, typenodes, declared types, widening, type ops, contextual typing, control-flow narrowing, inference, expression checks, JSX, module/alias resolution, relater (assignability), nodebuilder (type→TypeNode), emit resolver, services, exports, symbol accessibility (~2,220 `Checker::`/`EmitResolver::`/etc. defs across ~30 files, ~87k lines) |
| Transformers | `transformers/` | `cpp/internal/transformers` | complete — root package (transformer/chain/modifiervisitor/utilities/destructuring) + all sub-packages: estransforms (17 files incl. classfields 4k, esdecorator, namedevaluation, classthis, async family), jsxtransforms, moduletransforms, inliners, tstransforms (6 files), declarations (transform/diagnostics/tracker/supplementalreferences/util) |
| Emitter | `printer/`, `compiler/emitter.go`, `compiler/emitHost.go` | `cpp/internal/printer`, `cpp/internal/compiler/emitter.cpp` | emitter + emitHost + `Program::Emit` ported; `tscpp emit`/`tscpp emitdump` byte-identical to the Go `emitdump` oracle on the full corpus (12,734/12,734); sourcemap/spanmap/contentmapper/incremental landed |
| Stage-5 | `tsoptions`, `format`, `contentmapper`+`spanmap`, `execute/incremental`, `vfs`, `locale`, `json`, `jsonrpc`, `xxh3`, `gostd`, `transpile` | `cpp/internal/{tsoptions,format,contentmapper,spanmap,execute/incremental,vfs,locale,json,jsonrpc,xxh3,gostd,transpile}` | complete — incl. `tsoptions::ParseCommandLine` CLI wiring (real flag parsing in tscpp), contentmapper→fileloader seam (`parseContentMappedFile` + host mapping + program diagnostics), transpile API (byte-identical to `transpiledump` oracle) |
| Stage-5 (merged) | `fswatch`, `ipc`, `execute/tsc`+`build`, `watchmanager`, `ls`, `api`, `project`, `lsp`, `fourslash`, `tsctests`, `lsconv` (partial — see `cpp/internal/ls/lsconv/lsconv.h`), `lspwatcher`, `collections`, `testutil/*` | `cpp/internal/...` | complete — `lsp` server + `proto.go` tail landed; `--lsp`/`--api` event streams byte-identical; `tsctestrunner` 99/99 tsc scenarios; `fourslashrunner` **4,130/4,130 (100%)** across 4,560 ported tests — full oracle parity |

## Conformance

`tscpp parse` emits a pre-order dump — `N <kind> <pos> <end> <flags>` per
node, then `D`/`J`/`C <code> <pos> <end>` records for parse diagnostics, JS
diagnostics and JSDoc diagnostics respectively. `tsc/cmd/parsedump` is a
thin Go driver that emits the identical format from `tsc/internal`, so the
two implementations can be diffed file-by-file with zero tolerance.

**Result: 26,006 / 26,006 files in `tsc/testdata` produce byte-identical
output** (every `.ts/.tsx/.js/.jsx/.mts/.cts/.mjs/.cjs` file outside
`node_modules` fixtures, including the entire conformance corpus, all JS
baselines with JSDoc, and error-recovery torture cases).

`tscpp bind` runs the full binder and emits, per node, the post-bind flags,
the symbol record (`S name flags ndecls valueDeclPos parentName`), the
export localSymbol (`X`), the flow-node flags (`F`/`E`/`R`), the sorted
locals table (`L`), the container chain (`Q`), and file-level bind state
(`M`/`K`/`P`/`G`), then bind diagnostics (`B`). `tsc/cmd/bindump` is the
matching Go oracle.

**Result: 26,002 / 26,002 files produce byte-identical bind output.**

`tscpp check` runs the full program pipeline (parse → bind → check) and emits
the canonical `G <code>` / `F <file>` / `T <code> <pos> <end>` diagnostic
dump — `tsc/cmd/checkdump` is the matching Go oracle (`tsc --noEmit`).

**Result: 12,734 / 12,734 files (100%) produce byte-identical check output.**

`tscpp emitdump` runs the emit path and appends `W <file>` + verbatim content
sections for every emitted file — `tsc/cmd/emitdump` is the matching Go
oracle (`tsc`).

**Result: 12,734 / 12,734 files (100%) produce byte-identical emit output,
including `--declaration` (all member-serialization/aliasing/formatting
divergences fixed).**

`tscpp transpiledump` ports `tsc/cmd/transpiledump` — stdin-driven transpile
(module or `-decl` declaration) dump: output text + `diags:` list. Verified
byte-identical on a 200-file sample (both modes).

Dump surfaces certified vs their Go oracles (500-file samples, byte-identical):
`parse` vs `parsedump`, `bind` vs `bindump`, `lex-json` vs `lexdump` (all
500/500).

```sh
# single file
./cpp/tools/conformance_parse.sh tsc/testdata/tests/cases/compiler/abstractClasses.ts

# whole corpus (writes /tmp/tscpp_results.txt)
./cpp/tools/conformance_corpus.sh 8
```

## Performance

Measured on Apple silicon, `tscpp bench-parse` (full parse incl. JSDoc
reparse) sustains ~90–95 MB/s on mixed files and ~330 MB/s for scanning
alone; see `tscpp bench` / `tscpp bench-parse`. Node allocation is arena
based — an entire SourceFile's nodes are freed in one shot, and parsing
performs no per-node refcount/GC work.

Project-level (`tsc -p`, 100-file synthetic project, Linux/x86): after the
FENNEL checker pool (~250% CPU) and emit-arena release fixes — plus a
~50% Go-side regression that moved the baseline — tscpp reached
wall-clock parity with Go `tsc` (emit 1.05×, --noEmit 0.99×,
--declaration 1.00×; decl-emit RSS ~340 MB vs Go ~264 MB). The ROADMAP
≥3× gate was closed as contract-incompatible; residual gap is per-CPU
hot-path work; see
`cpp/tools/perf/PERF_REPORT.md` and `run_all.sh` for the reproducible
harness.

## Layout

```
cpp/
  CMakeLists.txt          cmake+ninja build (Apple clang / GCC / MSVC)
  cmd/tscpp/main.cpp      lex|lex-json|bench|parse|bench-parse|parse-all|bind|check|emit|emitdump|transpiledump driver
  internal/
    ast/                  Node model; nodes_generated.h is generated
    binder/               binder + referenceresolver + exports
    checker/              ~30 checker_*.cpp slices + checker.h
    compiler/             program, host, outputpaths, emitter
    core/                 arena allocator, text/span types
    diagnostics/          messages_generated.h (generated catalog)
    jsnum/                numeric literal parsing
    module/               module specifier + name resolution
    modulespecifiers/     module specifier generation
    nodebuilder/          type→TypeNode serializer
    parser/               parser.cpp + jsdoc.cpp + reparser.cpp + references.cpp
    printer/              textwriter, emitcontext, namegenerator, printer
    scanner/              scanner.cpp + regexp.cpp
    spanmap/              span maps (source maps + edits)
    stringutil/           unicode tables (generated)
    transformers/         transformer chain + all transform packages
    tsoptions/            tsconfig/CLI option parsing
    tspath/               path helpers
  tools/
    gencpp.py             reads tsc/internal Go sources, regenerates
                          kind.h, nodes_generated.h, factory/predicate
                          decls, messages_generated.h, unicode tables
    conformance_parse.sh  per-file byte-identical diff vs Go oracle
    conformance_corpus.sh corpus runner
tsc/cmd/parsedump/        Go-side oracle driver (same dump format)
tsc/cmd/lexdump/          Go-side oracle token dumper
tsc/cmd/bindump/          Go-side oracle bind dumper
tsc/cmd/checkdump/        Go-side oracle check dumper (tsc --noEmit)
tsc/cmd/emitdump/         Go-side oracle emit dumper (tsc)
```

## Build

```sh
cmake -B cpp/build -S cpp -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C cpp/build
# Go oracle (one-time):
(cd tsc/cmd/parsedump && go build -o ../../parsedump .)
(cd tsc/cmd/lexdump   && go build -o ../../lexdump .)
```

## Porting notes (for future sessions)

- Go strings are immutable; scanner `tokenValue()` in C++ returns a
  `std::string_view` into a reused buffer. Any port must copy the string
  *before* calling `nextToken*()` — holding the view across a scan is a
  use-after-free class of bugs.
- `Parser::sourceText` is a `std::string_view` into the caller's buffer;
  the caller's `std::string` must outlive the parse (`SourceFile->text`
  owns its own copy).
- ParseFlags/contextFlags restoration: Go closures capture locals; the
  C++ port uses scratch fields for member-fn-ptr list callbacks, which
  must be saved/restored around nested list parses.
- `Node::modifiers()`/`name()` etc. are generated per-kind accessors;
  update `cpp/tools/gencpp.py` (not `nodes_generated.h`) when Go adds a
  kind→struct mapping — the generator parses the authoritative
  `n.data.(*T)` dispatch in `tsc/internal/ast`.

## Roadmap

See `cpp/ROADMAP.md`.

## Reports and audit trail

- `cpp/ORACLE_BUMP.md` — oracle pin/bump ledger (`microsoft/TypeScript` revision history)
- `cpp/SECURITY_REVIEW.md` — adversarial security audit vs the Go oracle
- `cpp/E2E_REPORT.md` — byte-identity report on real-world codebases
- `cpp/FUZZ_REPORT.md` — libFuzzer+ASan/UBSan pass over input surfaces
- `cpp/MACOS_PARITY.md` — macOS (Darwin/arm64) build/test parity notes
- `cpp/WINDOWS_PARITY.md` — Windows x64 build/test parity notes
- `cpp/CLEANUP_AUDIT.md` — dead-code / port-debt cleanup ledger
- Fix reports: `cpp/TRUNCATION_FIX.md` (quickinfo `[...]` vs `...`),
  `cpp/SNAPREF_FIX.md` (disposed-snapshot panic under parallel load),
  `cpp/DOCHL_FIX.md` (document-highlight multi-file URIs),
  `cpp/CODEFIXTRUNC_FIX.md` (codefix truncation first-divergence analysis)
- `cpp/tools/PROJECT_PARITY.md`, `cpp/tools/WATCH_PARITY.md` — project- and
  watch-mode parity reports
- `cpp/tools/perf/PERF_REPORT.md`, `cpp/tools/perf/HOTPATHS.md` — perf
  harness results and hot-path notes
