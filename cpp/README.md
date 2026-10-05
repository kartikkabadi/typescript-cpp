# tscpp — TypeScript compiler, ported to C++

`cpp/` is a function-by-function C++ port of `tsc/internal` — the Go
implementation of the TypeScript compiler (the "corsa" rewrite that ships as
`tsc` in TypeScript 7+/native-preview). The goal of the port is a compiler
that is *significantly faster* than both the JS (`typescript.js`) and Go
implementations, with **zero behavioral regressions** — verified byte-for-byte
against the Go implementation, which is kept in-tree as the conformance
oracle.

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
| Checker | `checker/` | `cpp/internal/checker` | in progress — bootstrap, mapper/link store, JSDoc, tracer, grammar checks, node copy, module resolution, statements/class machinery, signatures, members, typenodes, declared types, widening, type ops, contextual typing, control-flow narrowing, inference, expression checks, JSX, module/alias resolution, services tail (~1,800 `Checker::` defs across 27 files; ~177 `TSC_UNREACHABLE` dep stubs pending the in-flight `relater.go`, `nodebuilder*`, `emitresolver.go`, `services.go`/`exports.go`, `symbolaccessibility.go`/`printer.go` slices) |
| Emitter | `printer/`, `transformers/` | — | not started |

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

## Layout

```
cpp/
  CMakeLists.txt          cmake+ninja build (Apple clang / GCC / MSVC)
  cmd/tscpp/main.cpp      lex|lex-json|bench|parse|bench-parse|parse-all|bind|check driver
  internal/
    ast/                  Node model; nodes_generated.h is generated
    core/                 arena allocator, text/span types
    diagnostics/          messages_generated.h (generated catalog)
    jsnum/                numeric literal parsing
    parser/               parser.cpp + jsdoc.cpp + reparser.cpp + references.cpp
    scanner/              scanner.cpp + regexp.cpp
    stringutil/           unicode tables (generated)
    tspath/               path helpers
  tools/
    gencpp.py             reads tsc/internal Go sources, regenerates
                          kind.h, nodes_generated.h, factory/predicate
                          decls, messages_generated.h, unicode tables
    conformance_parse.sh  per-file byte-identical diff vs Go oracle
    conformance_corpus.sh corpus runner
tsc/cmd/parsedump/        Go-side oracle driver (same dump format)
tsc/cmd/lexdump/          Go-side oracle token dumper
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
