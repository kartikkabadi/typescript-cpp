# Hazard-pattern audit

Audit of `cpp/internal/**` against the Go oracle `tsc/internal/**` for
siblings of nine known divergence-bug classes. Mapping rule:
`cpp/internal/<pkg>/<file>.cpp` ↔ `tsc/internal/<pkg>/<file>.go`.

**Result: 5 real bugs found, all fixed.** Remaining sites classified
OK (faithful or equivalent); nothing left SUSPECT.

| # | Class | Sites audited | Fixed | Verdict |
|---|-------|---------------|-------|---------|
| 1 | `defer` as block-scope RAII | ~117 guard sites across 11 TUs | 4 sites / 3 bugs | fixed |
| 2 | `ref()`/`Deref()` vs Go raw capture | 43 `->ref()/->Ref()/->Deref()` in project/api/compiler | 2 (same sites as class 1) | fixed |
| 3 | Eval-order in call arguments | spot-checked n-arg calls in emitter/checker/printer | 0 | clean |
| 4 | `string_view`/temporaries | member views, `substr` holds, `errorAs`/`unwrap` (49 sites) | 1 | fixed |
| 5 | `[&]` captures outliving scope | `std::function`/callback registrations (1421 mentions; audited stores) | 0 | clean |
| 6 | Mid-iteration container mutation | 72 `Range(` callers (SyncMap::Range holds `mu`; 65 callers checked, none self-mutate) | 0 | clean |
| 7 | Slice-append aliasing | 259 `.data()`/`&v[0]` sites; those crossing `push_back`/`emplace_back` | 0 | clean |
| 8 | NaN/int-conversion UB | float→int casts in jsnum/core (jsnum uses explicit NaN→0 Go semantics) | 0 | clean |
| 9 | `shared_ptr` lifetime vs Go GC | temporary `shared_ptr` `.get()`/member binds; global/static `shared_ptr`s | 1 (same site as class 4) | fixed |

## Fixed bugs

1. **`SyncMapEntry::deleteLocked` released `entry->mu` before writing
   `entry->delete_` — `cpp/internal/project/dirty/dirty.h`** (class 1).
   `std::lock_guard<std::mutex>` inside `if (loaded)` destructed at the
   `if` body; `entry->delete_ = true` ran unlocked. Go
   `defer entry.mu.Unlock()` holds to function exit —
   `tsc/internal/collections/syncmap.go:171-184` (`deleteLocked`).
   Fixed with function-scope `std::unique_lock` + `defer_lock`, `el.lock()`
   inside `if (loaded)`, matching the adjacent `changeLocked` port.

2. **`parseCache::loadOrStore` released `entry->mu` before `parse(key)`
   — `cpp/internal/execute/build/build.h`** (class 1).
   `unlockEntry` scoped inside `if (loaded)` released the winning entry's
   lock before `newEntry->value = parse(key)`, losing Go's single-parse
   guarantee (a concurrent `LoadOrStore` could also parse). Go
   `defer entry.mu.Unlock()` spans `parse(key)` —
   `tsc/internal/execute/build.go`. Fixed with function-scope
   `unique_lock` assigned inside `if (loaded)` via `adopt_lock`.

3. **`preparedSnapshot` `Deref()` fired at `if`-block exit while derived
   `workingSnapshot`/`program`/checker were still in use —
   `cpp/internal/api/session.cpp` (two sites)** (classes 1+2).
   `deferGuard` inside the `if` destructed at block end; Go
   `defer preparedSnapshot.Deref()` fires at function exit —
   `tsc/internal/api/session.go:3247` and `:5519`. Fixed by hoisting to
   function-scope `std::optional<deferGuard>` + `emplace`. Also hardened
   `deferGuard` with an explicit ctor and a move-steal ctor so a
   temporary assigned into `optional` cannot fire twice (that exact
   mistake caused a `snapshot ref count below zero` regression caught by
   `api.TestCompletionRetriesWithAutoImports` /
   `TestFreshSnapshotIncludesDependencyAutoImports` during verification).

4. **`resolutionState` dropped `containingDirectoryHasTrailingSeparator`
   and stored the separator-forced directory —
   `cpp/internal/module/resolver.{h,cpp}`** (class 1 — deferred state
   restore divergence).
   `loadModuleFromTargetExportOrImport` stored
   `ensureTrailingDirectorySeparator(scope->PackageDirectory)` where Go
   stores the RAW `scope.PackageDirectory.AsDirectoryPath()` and sets
   `r.containingDirectoryHasTrailingSeparator = true` —
   `tsc/internal/module/resolver.go:971-976`. The with-separator path
   corrupted the ancestor walk in `resolveNodeLike`:
   `getBaseFileName("/x/node_modules/")` returns `""` while Go's
   `RootedDirectoryPath.BaseName()` strips the separator →
   `"node_modules"`, so C++ wrongly probed `node_modules/node_modules`.
   Fixed: added the field + `containingDirectoryPath()` helper
   (`resolver.go:318`), used it at the `:594` trace write, and
   save/set/restore all three fields (name, containingDirectory, flag).

5. **`capsHasItemDefault` bound `const auto&` to a member of a
   refcount-1 `shared_ptr` temporary —
   `cpp/internal/ls/completions.cpp`** (classes 4+9).
   `resolvedCaps(ctx)->TextDocument.Completion.CompletionList.ItemDefaults`
   bound a reference to a member of the temporary; the reference dangled
   once the `shared_ptr` destructed (`resolvedCaps` fabricates a fresh
   object each call — Go GC would pin it). Fixed by holding
   `auto caps = resolvedCaps(ctx);` for the read.

## Classes confirmed clean

- **Class 3 (eval-order)**: spot-checked multi-arg calls in
  emitter/checker/printer where children touch sibling state — all
  left-to-right, matching Go.
- **Class 5 (`[&]` escapes)**: lambdas stored in `std::function`/
  callbacks capture heap objects or run synchronously before scope exit.
- **Class 6 (Range mutation)**: `SyncMap::Range` holds `mu` across the
  callback — a self-mutating caller would deadlock outright; none exist
  (all 65 call sites inspected). `BreadthFirstSearchLevel::Range`
  (`core/utilities.h`) already iterates live keys by index — the known
  OrderedMap BFS fix.
- **Class 7 (slice-append aliasing)**: `.data()`/`begin()`/`&v[0]` uses
  re-fetch after mutation or use indices — no pointer held across
  `push_back`/`emplace_back`.
- **Class 8 (NaN conversions)**: `jsnum`/`core` float→int conversions go
  through helpers with explicit Go NaN→0 semantics; no raw
  `static_cast<int>` on arbitrary doubles.
- **Class 2/9 residual**: remaining `->ref()`/`->Deref()` sites match
  places where Go genuinely owns a reference; global `shared_ptr`s hold
  process-lifetime objects Go also never frees.

## Verification

- `ninja -C cpp/build tscpp unittestrunner tsctestrunner fourslashrunner` — clean.
- `unittestrunner` 1110/1110; `tsctestrunner` 117/117.
- `fourslashrunner`: `TestType.*` 15/15, `TestFind.*` 217/217,
  `TestQuickInfo.*` 252/252, `TestCodeFix.*` 232/232 (non-SKIP).
  No `TestCheck*` tests exist in this runner.
- `tsc/` untouched (read-only oracle); no Go toolchain on this box —
  `go build ./tsc/...` not applicable here.
