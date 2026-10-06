// Port of tsc/internal/testutil/contentmappertest — realistic content mapper
// implementations used by tests.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "internal/contentmapper/contentmapper.h"
#include "internal/gostd/gostd.h"

namespace tsc::testutil::contentmappertest {

// registry.go:7 — mapper command names.
inline constexpr std::string_view TransformingMapper =
    "compiler-test-mapper";
inline constexpr std::string_view VerbatimMapper = "verbatim-mapper";
inline constexpr std::string_view ModuleVerbatimMapper =
    "module-verbatim-mapper";
inline constexpr std::string_view DynamicVerbatimMapper =
    "dynamic-verbatim-mapper";
inline constexpr std::string_view DiagnosticCodeCollisionMapper =
    "diagnostic-code-collision-mapper";
inline constexpr std::string_view FailingMapper = "failing-mapper";
inline constexpr std::string_view SynthesizingMapper = "synthesizing-mapper";
inline constexpr std::string_view ComponentMapper = "component-mapper";
inline constexpr std::string_view DuplicateMapper = "duplicate-mapper";
inline constexpr std::string_view LispMapper = "lisp-mapper";
inline constexpr std::string_view SupplementalMapper = "supplemental-mapper";
inline constexpr std::string_view SupplementalDiagnosticsMapper =
    "supplemental-diagnostics-mapper";
inline constexpr std::string_view SupplementalGlobalsMapper =
    "supplemental-globals-mapper";
inline constexpr std::string_view SupplementalModuleMapper =
    "supplemental-module-mapper";
inline constexpr std::string_view PrefixedSupplementalMapper =
    "prefixed-supplemental-mapper";
inline constexpr std::string_view UnmappedFoldingMapper =
    "unmapped-folding-mapper";
inline constexpr std::string_view HoistingMapper = "hoisting-mapper";
inline constexpr std::string_view DuplicateProjectionMapper =
    "duplicate-projection-mapper";

// ProjectLifecycle — dynamic_verbatim.go:12.
struct ProjectLifecycle {
	std::atomic<int32_t> Opens{0};
	std::atomic<int32_t> Closes{0};
};

// DeclaredOptions — transforming.go:18.
inline const std::vector<std::string> DeclaredOptions{"target", "jsx"};

// PackageName — manifest.go:5.
inline constexpr std::string_view PackageName = "mapper";

// PackageJSON — manifest.go:8.
std::string PackageJSON(std::string_view mapper);

// Serve — spawner.go:15.
gostd::Error Serve(gostd::Context ctx,
                   std::shared_ptr<gostd::io::ReadWriteCloser> rwc);

// NewSpawner — spawner.go:20.
contentmapper::Spawner* NewSpawner();

// NewSpawnerWithProjectLifecycle — spawner.go:25.
contentmapper::Spawner* NewSpawnerWithProjectLifecycle(
    ProjectLifecycle* lifecycle);

}  // namespace tsc::testutil::contentmappertest
