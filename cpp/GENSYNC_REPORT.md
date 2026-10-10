# Generator-sync report — `nodes` (gencpp.py)

Branch: `devin/cpp-gensync` (base `devin/cpp-port`)
Goal: `python3 cpp/tools/gencpp.py nodes` output == checked-in
`cpp/internal/ast/nodes_generated.h` so a full regen is committable.

## Result

`gencpp.py all` now regenerates every artifact byte-for-byte identical to the
pin: `nodes_generated.h`, `kind.h`, `messages_generated.h`,
`unicode_tables.h`, and the four `nodes_*.inc`/`nodes_visitor_generated.h`
includes — `git diff` empty on all of them.

## Files changed

| file | +/- | what |
|---|---|---|
| `cpp/tools/gencpp.py` | +53 / −15 | generator fixes only — `nodes_generated.h` needed no edits (regen == pin) |

## Fixes (generator, not the header)

1. **`tspath.RootedFilePath` literal** — oracle bump switched
   `SourceFile.fileName` to `tspath.RootedFilePath` (a `RootedPath`/`string`
   alias in Go). Added `"tspath.RootedFilePath": "std::string"` to
   `GO_VALUE_TYPES`, matching `tspath.Path`. The C++ pin keeps the field
   `std::string` (a `tspath::RootedFilePath` wrapper type exists in
   `tspath/typed_paths.h` but the `SourceFile` API is `std::string`-based;
   switching it is a separate porting decision).

2. **`IsContentMapped` dropped** — the oracle has
   `SourceFile::IsContentMapped` (ast.go:2571,
   `contentMapperInfo != nil`); the generator's hand-maintained
   `extra_members_tail["SourceFile"]` block missed it. Emitted between
   `IsBound` and `BindOnce`, matching the pin exactly.

3. **`SourceFile` deepClone `return nullptr`** — the generator stubbed the
   case ("never a clone target — mutexes aren't copyable"). Wrong: Go's
   `getDeepCloneVisitor` *does* visit SourceFiles — `VisitEachChild` →
   `updateSourceFile(statements, endOfFileToken)` when children changed,
   else `node.Clone(f)` = `newSourceFile` + `copyFrom` + `cloneNode`.
   Generator now emits that body verbatim (children cloned via
   `deepCloneNodeList`/`deepCloneNode`, `syntheticLocation` loc (-1,-1),
   `hooks.onClone` fired).

4. **`(subtreeFacts() & ~mask)` parens** — `propagateSubtreeFacts` cases
   with an extra `name` term (GetAccessor/SetAccessor/Method/Property)
   emitted `a & ~b | c`. Harmless (`&` binds tighter than `|` — identical
   semantics), but the pin parenthesizes; generator now emits
   `(subtreeFacts() & ~mask) | propagate(...)`.

5. **dropped `const_cast` on `node.AsNode()`** — *not* churn, a compile
   fix the generator was losing. `computeSubtreeFacts_CallExpression`
   takes `const CallExpression* n`; `n->as<Node>()` resolves to the const
   overload → `const Node*`, which can't feed `isImportCall(Node*)`.
   `facts_expr` now translates `node.As<X>()` on the const receiver to
   `const_cast<{sname}*>(n)->as<X>()` (Go has no const). Only one site in
   all `computeSubtreeFacts` bodies is affected (ast.go:2136 `IsImportCall`).

6. **`:?=` → `auto x =` bug — verified already fixed.** `facts_body`'s
   decl regex `(\w+) := (.*)` requires the literal colon; plain `=`
   assigns fall to the compound-assign branch emitting `x = expr;`.
   Confirmed on `computeSubtreeFacts_PropertyAccessExpression`:
   `privateName := …` → `auto privateName = …;`,
   `privateName = SubtreeContains…` → `privateName = SubtreeContains…;`.

## Verification

- `python3 cpp/tools/gencpp.py all` → `git diff` empty on all generated files.
- Release build: `CC=clang-15 CXX=clang++-15 cmake -B cpp/build -S cpp -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld`;
  `ninja -C cpp/build` — **812/812 targets, 0 errors** (warning noise is the
  known per-TU header warnings).
- `cpp/build/unittestrunner` — **1122/1122 pass** (6 SKIP: Node.js-dependent).
- `cpp/build/fourslashrunner -run 'TestQuickInfo|TestCodeFix'` —
  **484/484 pass** (103 SKIP: known-failing fourslash tests).
