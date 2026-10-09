# RE2 migration — `gostd::regexp`

Closes `SECURITY_REVIEW.md` finding **F1** (ReDoS residual): `gostd::regexp` ran on
`std::regex` — a backtracking engine — while Go's `regexp` is RE2-class
(linear-time, automata-based). RE2-valid but catastrophic patterns such as
`(a+)+$`, deliverable via the user-controlled
`autoImportSpecifierExcludeRegexes` tsconfig option, now cost microseconds
instead of hanging (or, under libc++, throwing `std::regex_error` and
aborting the process when uncaught).

## Verdict: full RE2 swap

No std::regex fallback remains on the `gostd::regexp` path. `semver.h` still
uses `std::regex` for simple version parsing — that file does not go through
`gostd::regexp` (Go's semver doesn't either), its patterns are fixed literals
with no nesting, so no backtracking exposure; intentionally out of scope.

## Vendor pin

- **RE2 release `2023-03-01`**, vendored under `cpp/third_party/re2/` —
  the last self-contained release: Abseil became a required dependency one
  release later (2023-06-02), so this pin avoids dragging in absl.
- License: **BSD-3-Clause** (`cpp/third_party/re2/LICENSE`), not Apache-2.0.
- Sources: `re2/*.cc` (21 library TUs; tests/benchmarks/fuzzers excluded) +
  `util/{rune,strutil}.cc`. ICU `#include "unicode/..."` lines are all behind
  `RE2_USE_ICU` — never defined here.
- Built as `tsc_re2` OBJECT library in `cpp/CMakeLists.txt`; linked into `tsc`.

## API mapping

| `gostd::regexp` (Go API)      | Engine call                                   |
|-------------------------------|-----------------------------------------------|
| ctor = `MustCompile`          | `RE2::RE2(pattern, opts)` + `opts.set_log_errors(false)`; `!ok()` → `std::runtime_error` (MustCompile panic) |
| `MatchString`                 | `RE2::Match(s, 0, s.size(), UNANCHORED, nullptr, 0)` |
| `FindStringSubmatch`          | `RE2::Match(..., pieces, ngroups+1)` → `s`/`""` |
| `FindAllStringSubmatch`       | `eachMatch` iterator + same `Match` call      |
| `FindAllStringIndex` (new)    | `eachMatch` iterator → `[start,end)` pairs    |
| `Split`                       | Go `Split` verbatim over `eachMatch`          |
| `ReplaceAllString`            | Go `replaceAll` verbatim + `expand`/`extract` |

## Parity decisions (each ported from `tsc/internal`, verified against the Go oracle)

- **Iteration**: two distinct Go loops ported verbatim — `Regexp.matches`
  (Find*/Split: skips an empty match that abuts the previous match's end)
  and `replaceAll` (suppresses only the `repl` expansion there; the match
  itself is still consumed). `searchPos` advance matches Go's rune-width step
  (`runeWidthAt` decodes UTF-8; invalid bytes = width 1, matching Go's
  RuneError handling).
- **Anchoring**: `RE2::Match(text, startpos, endpos, UNANCHORED, ...)` anchors
  `^` and required prefixes to absolute text start, not `startpos` — identical
  to Go's `doExecute(pos)`. No offset adjustment needed.
- **`$` templates**: Go `expand`/`extract` ported verbatim — `$$`→`$`,
  all-digit names → group index, leading-zero / `>=1e8` → named lookup via
  `NamedCapturingGroups()`, out-of-range/absent → `""`. Same surface RE2's
  `Rewrite` would give, but ported to keep Go's exact reject rules.
- **Compile accept/reject**: RE2 accepts exactly Go's syntax — `(?i)`,
  `(?m)`, `(?P<n>)`, `\x{...}`, `\d\s\w\b`, `^$` text-anchored unless
  `(?m)`. Look-ahead, backrefs, `[\1]` (invalid escape), `a{2,1}` all fail
  `Compile` identically — the old `re2Unsupported` pre-scan in
  `modulespecifiers/util.cpp` is deleted because RE2 *is* the arbiter now.
- **Thread safety**: `Regexp` is read-only after compile (RE2 is
  thread-safe for concurrent `Match`), like Go's `Regexp` — the
  `regexSearchMu` mutex in `modulespecifiers` is removed.
- Case-insensitivity is applied where Go applies it: call sites wrap the
  pattern as `(?i:...)` before compiling (same string the Go code feeds
  `regexp.Compile`).

## Call sites converted (public API kept; no behavior change)

- `internal/modulespecifiers/util.cpp` — `stringToRegex` now returns
  `shared_ptr<gostd::regexp::Regexp>`; `IsExcludedByRegex` uses
  `MatchString`; the hand-rolled RE2-validity gate is gone.
- `internal/testutil/fsbaselineutil/fsbaselineutil.cpp` — internal-symbol
  sanitizer uses `FindAllStringIndex` (restores Go's `\x{FFFD}` pattern that
  std::regex could not express).
- `internal/diagnostics/tests/tests_diagnostics.cpp` — `{\d+}` placeholder
  matcher.
- `cmd/{unittestrunner,tsctestrunner,fourslashrunner}` — `-run` filter.

## Benchmark — `(a+)+$` × `"a"*32 + "b"` (non-matching worst case)

| Engine               | Result                                    |
|----------------------|-------------------------------------------|
| `std::regex` (libc++)| **throws `std::regex_error`** — uncaught ⇒ process abort |
| **RE2 2023-03-01**   | `false` in **12 µs**                      |

(Under libstdc++ the same input backtracks exponentially — effectively a
hang; libc++ caps complexity and crashes instead. Both are the F1 residual;
both are gone.)

## Test coverage

8 new `gostd.TestRegexp*` unit tests (`internal/gostd/tests/tests_regexp.cpp`)
assert Go-oracle output verbatim: submatch, empty-match iteration, Split,
ReplaceAll (incl. `${1}`/`${name}`/`${bad` template rules), `^$`/`(?m)`/`\b`
anchoring, FindAllStringIndex, compile-time rejects, and a
`TestRegexpCatastrophicCompletes` perf guard (<500 ms; measured 12 µs).

## Verification (on `devin/cpp-re2`)

- `ninja -C cpp/build tscpp unittestrunner tsctestrunner fourslashrunner` — green
- unittestrunner **1118/1118** (1110 + 8 new), tsctestrunner **117/117**,
  fourslash `-run 'Test[A-F]'` **1762/1762** (skips are pre-existing known
  failures / GOOS-gated)
- `go build ./tsc/...` green; `git diff tsc/` empty (oracle untouched)
