# Oracle delta report: `2d8450f9` → `aa814927` (2026-10-10)

Branch: `devin/cpp-oradelta2` (base `devin/cpp-port` @ `1a6eec4932`).

Upstream `microsoft/TypeScript` `main` moved from pin `2d8450f9b5` to
`aa8149273b` — **1 new commit** touching `tsc/`
(`git log 2d8450f9b5..aa8149273b -- tsc/`).

## Upstream commits vendored + ported

| Upstream | Title | Vendor commit | C++ port commit |
|---|---|---|---|
| `aa8149273b` | Dense link stores (#64711) | `1c2ea1aec0` | `dc6a05eae7` |

### What the commit does

Link-store ID allocation is reworked so links pack densely:

- `ast/utilities.go`: new `NodeIdGenerator` / `SymbolIdGenerator`. When a
  node/symbol has no ID yet, the generator grabs a chunk of `BlockIdSize`
  (=`LinkPageSize`, 256) IDs from new central atomics `nextNodeBlockId` /
  `nextSymbolBlockId` and assigns IDs at `BlockIdOffset` (2^48) + offset —
  every block of 256 IDs is consecutive.
- `core/linkstore.go`: `PagedLinkStore` becomes pages-of-pointers
  (`[]*[256]*V`) over an embedded `Arena[V]`; the high-index `pageMap` is
  dropped (block ids are dense by construction). `pageShift`/`pageSize`/
  `pageMask`/`maxPageCount` become exported `LinkPageShift`/`LinkPageSize`/
  `LinkPageMask`.
- `checker/links.go`: `nodeLinkStore` and `symbolLinkStore` (renamed from
  `symbolArenaLinkStore`) hold `gen` + `links` (`LinkStore[uint64,V]`
  fallback) + `pages` (`PagedLinkStore[V]`); Get/TryGet dispatch on
  `id >= BlockIdOffset && id < BlockIdOffset + maxPageLinkCount` (16M).
- `checker/checker.go`: `valueSymbolLinks` field re-typed to
  `symbolLinkStore`.

### C++ port (`dc6a05eae7`, 5 files, +150/-69)

- `cpp/internal/core/linkstore.h` — exported `LinkPageShift`/`LinkPageSize`/
  `LinkPageMask`; `PagedLinkStore<V>` rewritten as `std::vector<Page*>` of
  `std::array<V*,256>` pages + embedded `Arena` (matches Go's ownership —
  each store owns its arena).
- `cpp/internal/ast/ast.h` — `NodeId` widened `uint32_t`→`uint64_t` (Go
  `ids.go` declares `NodeId uint64`; block ids at 2^48 do not fit 32 bits —
  required for faithfulness, not just the new code path). Added
  `BlockIdOffset`/`BlockIdSize` constants and `NodeIdGenerator`/
  `SymbolIdGenerator` declarations.
- `cpp/internal/ast/utilities.cpp` — `nextNodeBlockId`/`nextSymbolBlockId`
  central atomics + generator method definitions (same CAS-on-zero pattern
  as `getNodeId`).
- `cpp/internal/checker/checker.h` — `maxPageLinkCount`; `nodeLinkStore`/
  `symbolLinkStore` rewritten with `gen`/`links`/`pages` triple dispatch;
  `valueSymbolLinks` re-typed (each store now self-contained — owns its
  `links` arena like Go).
- `cpp/internal/checker/checker.cpp` — `compareSymbols` ID fallback: Go
  computes `int(id1)-int(id2)` in 64 bits; the C++ `static_cast<int>`
  subtraction would wrap and could flip ordering once block ids exist —
  now compares full `int64_t` difference (sign semantics identical).

### Semantics notes

- `Has()` on a not-yet-queried entity now assigns a block id (Go does the
  same — `TryGet` goes through `gen.GetXId`), then misses — behavior parity.
- Two ID spaces coexist: sequential IDs from `nextNodeId`/`nextSymbolId`
  (assigned via `getNodeId`/`getSymbolId`, e.g. during bind) fall back to
  the `links` map; only generator-assigned block ids land in `pages`.
- The old `nodeLinkStore::Has` could report true for any entity landing in
  an allocated page (values lived inline in pages). The new pointer pages
  fix that — matching the upstream change exactly.

## Gate results

| Gate | Result |
|---|---|
| `git diff upstream/main -- tsc/` | only repo-local `tsc/cmd/*` + `testdata/tests/cases/compiler/deeplyNestedMappedTypes.js` (repo-local regression fixture added in `d6d81606ae`, not upstream) |
| `go build ./tsc/...` | green |
| `ninja` (`tscpp unittestrunner tsctestrunner fourslashrunner`, Release) | green — 639 targets |
| `./unittestrunner` | **1122/1122 pass** (delta adds no tests) |
| `./tsctestrunner` | **117/117 pass** |
| `./fourslashrunner -run 'Test[A-C]'` | **1195/1195 pass**, 0 regressions |

Regressions found and fixed during this delta: none.

## Counts

- New upstream commits: **1**
- Files vendored: 4 (`ast/utilities.go`, `core/linkstore.go`,
  `checker/links.go`, `checker/checker.go`; +120/-66)
- Files ported: 5 (`linkstore.h`, `ast.h`, `utilities.cpp`, `checker.h`,
  `checker.cpp`; +150/-69)
- Oracle pin: `microsoft/TypeScript@aa814927` (2026-10-10)
