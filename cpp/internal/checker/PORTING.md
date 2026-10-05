# Porting the checker (contributor conventions)

This file documents the conventions used while porting `tsc/internal/checker` to C++.
Follow them exactly — the port is verified byte-for-byte against the Go oracle.

You are porting one slice of `tsc/internal/checker/checker.go` (Go, ~32,662 lines, the TypeScript compiler's
semantic checker) to C++23 in `cpp/internal/checker/`. This is a faithful, function-by-function port —
the repo's contract is byte-for-byte identical diagnostics vs the Go implementation. NO simplification,
NO "improved" logic, NO skipping branches. If a Go function has 12 cases, your C++ has the same 12 cases
in the same order. Keep Go's comments where they clarify semantics; keep section banner comments of the
form `// <desc> — checker.go:<start>-<end>`.


Build with `ninja -C cpp/build`. The compiler is clang-15 (C++23). `parser.h` and `checker.h` already
have `<optional>` fixed. Verify: `ninja -C cpp/build` ends with 0 errors, and
`./cpp/build/tscpp parse <file>` / `bind` still run.

Go oracle (read-only reference): `REPO_ROOT/typescript-cpp/tsc/internal/checker/checker.go` — your assigned
line range is the source of truth. Adjacent Go code you may need to *read* (types, enums, flags) lives
in `tsc/internal/checker/types.go` (data model — mostly already in `cpp/internal/checker/types.h`),
`tsc/internal/checker/utilities.go`, `tsc/internal/ast/`, `tsc/internal/core/`.

## C++ conventions already in the codebase

Types (all live in `cpp/internal/checker/types.h` — check there first, do NOT re-declare):
- `*Type`→`Type*`, `[]*Type`→`std::vector<Type*>`, `*ast.Node`→`Node*`, `[]*ast.Node`→`std::vector<Node*>`,
  `*ast.Symbol`→`Symbol*`, `ast.SymbolTable`→`SymbolTable` (`std::unordered_map<std::string, Symbol*>`,
  from `internal/ast/symbol.h`), `*Signature`→`Signature*`, `*TypeMapper`→`TypeMapper*`,
  `*IndexInfo`→`IndexInfo*`, `*TypeAlias`→`TypeAlias*`, `*ast.Diagnostic`→`Diagnostic*`,
  `*diagnostics.Message`→`const DiagnosticMessage*`, `core.Tristate`→`Tristate`, `Ternary`→`Ternary`,
  `ast.Kind`→`SyntaxKind`, `string`→`std::string`, Go `(a, b)` multi-return → `std::pair`/struct or out-params
  (match the style of the nearest already-ported function).
- Type model: `Type` has `flags` (`TypeFlags*`), `objectFlags`, `symbol`, `As<T>()` accessors
  (`t->AsUnionType()`, `t->AsInterfaceType()`, `t->AsTypeReference()` …). Symbol is `ast::Symbol`
  with `flags`, `Declarations` (`symbol->declarations`/`Declarations` — check existing usage),
  `members`, `exports` (raw `std::unordered_map` or `nil`—the port wraps via `getSymbolTable(...)`;
  follow existing usage exactly).
- Method calls: `c.foo(...)` → `foo(...)` inside `Checker::` bodies. Free Go functions in checker
  package → `static`/anonymous-namespace free functions in your .cpp.
- `core.Concatenate(a,b)`, `core.Filter`, `core.Map`, `core.Some`, `core.Every`, `core.LastOrNil`,
  `maps.Clone`, `slices.Equal`, `slices.Contains` have NO shared C++ equivalent — write a small
  `namespace { ... }` block of inline helpers at the top of YOUR .cpp (names in anonymous namespace
  are safe across TUs). Existing code does e.g. `std::vector<Type*> filtered; for (...) if (f(x)) filtered.push_back(x);`.
- Diagnostics: `c.error(node, diagnostics.X_Y, args...)` → `error(node, X_Y, {arg1, arg2})` where
  `X_Y` is the `const DiagnosticMessage*` in `cpp/internal/diagnostics/` — same name, same file.
  `error` signature: `error(Node* location, const DiagnosticMessage* message, std::vector<std::string> args = {})`.
- Line-level accuracy matters: keep faithful field/method names. Go `nil` → `nullptr`;
  `len(x)` → `x.size()`; `for _, x := range s` → `for (auto* x : s)`.

## Coordination rules (wave-2 slices all land in checker.h/cpp together)

1. **Your own file**: `cpp/internal/checker/checker_<slice>.cpp`. Register it in `cpp/CMakeLists.txt`
   in the `tsc` library source list (alphabetical near other `internal/checker/*.cpp`).
2. **Declarations**: add declarations for the Checker methods YOU port inside
   `class Checker` in `cpp/internal/checker/checker.h`, inside ONE marked block:
   ```
   // === slice: <name> ===
   ... your declarations ...
   // === end slice: <name> ===
   ```
   Put it at the END of the class's private section (right before the final `};`). Never reorder or
   edit existing declarations.
3. **External calls — check first**: every Go function you *call* must have a declaration.
   Grep `checker.h`/`types.h` FIRST — most helpers already exist. For one that does NOT:
   - add its declaration inside YOUR marked block too (exact faithful signature),
   - and define it ONCE at the bottom of YOUR .cpp inside
     `// === dep stubs — removed when owner slice lands ===` as
     `{ TSC_UNREACHABLE("<name> — owned by <other slice>"); }` (use existing `TSC_UNREACHABLE` macro
     — see checker.cpp "Pending ports" section REPO_ROOTline 4369 for the exact idiom).
   Only stub what you actually call. Never stub something already declared+defined.
4. **types.h / other shared headers**: prefer not to edit. If you truly need a new field/enum
   (e.g., a new `TypeSystemPropertyName` value or links field the Go code uses), add it in your own
   clearly marked `// === slice: <name> ===` block at the end of the enum/struct — never mid-struct.
   Check whether the field already exists first (much of types.h is already ported).
5. **Struct/link fields**: Go link stores (`NodeLinks`, `SignatureLinks`, `MembersAndExportsLinks`…)
   are already declared in types.h. Use them; if a field is missing, add it inside a marked block.
6. Do NOT create PRs. Commit and push your branch:
   `git push https://x-access-token:${KARTIKKABADI_GITHUB_PAT}@github.com/kartikkabadi/typescript-cpp.git devin/cpp-port-SLICE`
7. `TSC_UNREACHABLE` bodies are ONLY for dep-stubs. Everything in YOUR assigned range must be real,
   faithful code — every branch, every early return, every panic (`panic("...")` → `TSC_UNREACHABLE("...")`
   or `std::abort()` matching existing usage — check existing style).
8. If a Go function in your range is ALREADY ported (exists in checker.cpp with a real body),
   leave it alone and note it — do not duplicate.

## Deliverables

- Branch `devin/cpp-port-SLICE` pushed, `ninja -C cpp/build` 0 errors, `./cpp/build/tscpp parse/bind`
  unaffected, your .cpp compiles under `-Wall` as the project does.
- Structured output at the end: `branch`, `summary`, `functions_ported` (count), `deps_stubbed`
  (list names you stubbed), `notes` (anything unusual — missing types.h fields you added, functions
  found already-ported, semantic quirks).
