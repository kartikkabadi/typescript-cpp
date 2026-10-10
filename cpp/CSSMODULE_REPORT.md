# CSS module auto-import divergence — root cause and fix

Branch: `devin/cpp-cssmodule` (base `devin/cpp-port`).

## Symptom

`TestAutoImportCssModule` (`cpp/internal/fourslash/tests/tests_c_autoimport2.cpp`,
oracle `tsc/internal/fourslash/tests/autoImportCssModule_test.go`) was
`t->Skip`'d after a fixture audit restored Go's full test. Un-skipped it
failed with:

```
At marker 'noExtension': ModuleSpecifier mismatch:
    got "../types/styles.css" want "../types/styles"
```

Fixture: `/types/augmentations.ts` declares three ambient module augmentations
(`./styles.css`, `./styles`, `/types/rooted.css`); `/src/index.ts` requests
auto-import completions for one export each. `myClass` (`./styles.css`) and
`rootedClass` (`/types/rooted.css`) were already correct; only `noExtension`
(`./styles`) diverged — it inherited the `.css` specifier of its sibling.

## Root cause

The port dropped Go's `Export.UnresolvedModuleSpecifier` mechanism in all four
places it appears:

1. `ls/autoimport/export.go:97` — the `Export` field itself
   (`UnresolvedModuleSpecifier tspath.ModuleSpecifier`).
2. `ls/autoimport/extract.go:158-173` — in `extractFromModule`, when a relative
   (or rooted) ambient-augmentation name fails `ResolveModuleName`, Go records
   `unresolvedModuleSpecifier = name` and stamps it on every export extracted
   from that declaration.
3. `ls/autoimport/specifiers.go:12-28` — `View.GetModuleSpecifier`'s first
   branch: a non-empty `UnresolvedModuleSpecifier` short-circuits all other
   specifier logic. If it is dot-relative, the specifier is recomputed as the
   path from the importing file's directory to `export.ModuleFileName`
   (`CaseSensitivity.RelativePathFromDirectory`); rooted/bare specifiers are
   returned verbatim. `ResultKind` is `Relative`.
4. `ls/autoimport/view.go:219` — the dedup merge copies the field.

Without it, the `./styles` export fell through to the generic path:
`specifierCache[importingFilePath]` is keyed by `export.Path`, which
`createExport` sets to the **declaring file** — `/types/augmentations.ts`,
shared by all three augmentations. The `myClass` lookup stored
`"../types/styles.css"` under that key, and the later `noExtension` lookup
(`ModuleID=/types/styles`, `ModuleFileName=/types/styles`, identical `Path`)
hit the same cache entry and returned `"../types/styles.css"`.

Go never reaches the cache for these exports: the `UnresolvedModuleSpecifier`
branch returns first, so the collision is invisible upstream.

## Fix — restored Go behavior, no simplification

- `cpp/internal/ls/autoimport/autoimport.h`: added
  `tspath::ModuleSpecifier UnresolvedModuleSpecifier;` to `Export`
  (+ `#include "internal/tspath/typed_paths.h"` for the type).
- `cpp/internal/ls/autoimport/extract.cpp` (`extractFromModule`):
  declared `unresolvedModuleSpecifier`, set it to `toModuleSpecifier(name)` in
  the unresolved-name else-branch, and assigned it to every export appended by
  `extractFromModuleDeclaration` (`exports[exportStart:]`). The `:shrug:`
  placeholder is gone.
- `cpp/internal/ls/autoimport/specifiers.cpp` (`View::GetModuleSpecifier`):
  reinstated the first branch — non-empty `UnresolvedModuleSpecifier`,
  dot-relative specifiers re-rooted via
  `CaseSensitivity::relativePathFromDirectory(dirOf(importingFile), ModuleFileName)`
  → `AsModuleSpecifier()`, `IsExcludedByRegex` gate, return `ResultKind::Relative`.
- `cpp/internal/ls/autoimport/view.cpp`: the merged-export record now copies
  `UnresolvedModuleSpecifier`.

`ModuleID` stayed `std::string` (file-kind = path text, ambient-kind = bare
specifier text, `PathIsBareSpecifier` discriminates) — the existing porting
convention; only the missing specifier field was restored.

Result for the three augmentations:

| declare | unresolved specifier | produced |
|---|---|---|
| `./styles.css` | `./styles.css` (relative) | `../types/styles.css` |
| `./styles` | `./styles` (relative) | `../types/styles` |
| `/types/rooted.css` | `/types/rooted.css` (rooted → not relativized) | `/types/rooted.css` |

## Verification

- `fourslashrunner -run TestAutoImportCssModule`: **1/1 PASS** (un-skipped).
- `fourslashrunner -run 'TestAutoImport'`: **129/129 pass**.
- `unittestrunner`: **1122/1122 pass**.
- `fourslashrunner` (full suite): **4145/4145 pass** — no regressions vs the
  documented HEAD baseline (skips unchanged).
