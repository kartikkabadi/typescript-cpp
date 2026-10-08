// tests_clean.cpp — port of
// tsc/internal/execute/build/clean_test.go.
#include <sstream>
#include <string>
#include <vector>

#include "internal/execute/build/build.h"
#include "internal/execute/tsc/compile.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::build;
namespace etsc = ::tsc::execute::tsc;
namespace tsctests = ::tsc::execute::tsctests;
namespace build = ::tsc::execute::build;


using gostd::testing::T;
using tsctests::FileMap;

// cleanTestSystem — Go's cleanTestSystem wraps *tsctests.TestSys overriding
// Writer/ErrorWriter with a strings.Builder. TestSys is final here, so this
// wraps it: every etsc::System member forwards to inner, except the writers
// which feed `output`.
struct cleanTestSystem : etsc::System {
	std::unique_ptr<tsctests::TestSys> inner;
	std::ostringstream output;

	explicit cleanTestSystem(std::unique_ptr<tsctests::TestSys> inner)
	    : inner(std::move(inner)) {}

	std::ostream* Writer() override { return &output; }
	std::ostream* ErrorWriter() override { return &output; }
	std::shared_ptr<vfs::FS> fs() override { return inner->fs(); }
	std::string DefaultLibraryPath() override {
		return inner->DefaultLibraryPath();
	}
	std::string GetCurrentDirectory() override {
		return inner->GetCurrentDirectory();
	}
	bool WriteOutputIsTTY() override { return inner->WriteOutputIsTTY(); }
	int GetWidthOfTerminal() override { return inner->GetWidthOfTerminal(); }
	std::pair<std::string, bool>
	GetEnvironmentVariable(std::string_view name) override {
		return inner->GetEnvironmentVariable(name);
	}
	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr) override {
		return inner->Spawn(command, dir, stderr);
	}
	vfs::TimePoint Now() override { return inner->Now(); }
	gostd::Duration SinceStart() override { return inner->SinceStart(); }
};

cleanTestSystem* newCleanTestSystem() {
	return new cleanTestSystem(tsctests::NewTscSystem(
	    FileMap{
	        {"/project/a/tsconfig.json",
	         std::string(R"({
			"compilerOptions": { "composite": true, "noLib": true, "outDir": "dist" },
			"files": ["index.ts"],
			"references": [{ "path": "../b" }]
		})")},
	        {"/project/a/index.ts", "export const a = 1;"},
	        {"/project/a/dist/index.js", "export const a = 1;"},
	        {"/project/a/dist/index.d.ts", "export declare const a = 1;"},
	        {"/project/b/tsconfig.json",
	         "{ \"compilerOptions\": { \"composite\": true, \"noLib\": true, \"outDir\": \"dist\" }, \"files\": [\"index.ts\"] }"},
	        {"/project/b/index.ts", "export const b = 1;"},
	        {"/project/b/dist/index.js", "export const b = 1;"},
	        {"/project/b/dist/index.d.ts", "export declare const b = 1;"},
	        {"/project/c/tsconfig.json",
	         "{ \"compilerOptions\": { \"composite\": true, \"noLib\": true, \"outDir\": \"dist\" }, \"files\": [\"index.ts\"] }"},
	        {"/project/c/index.ts", "export const c = 1;"},
	        {"/project/c/dist/index.js", "export const c = 1;"},
	        {"/project/c/dist/index.d.ts", "export declare const c = 1;"},
	        {"/project/cycle1/tsconfig.json",
	         std::string(R"({
			"compilerOptions": { "composite": true, "noLib": true, "outDir": "dist" },
			"files": ["index.ts"],
			"references": [{ "path": "../cycle2" }]
		})")},
	        {"/project/cycle1/index.ts", "export const cycle1 = 1;"},
	        {"/project/cycle1/dist/index.js", "export const cycle1 = 1;"},
	        {"/project/cycle2/tsconfig.json",
	         std::string(R"({
			"compilerOptions": { "composite": true, "noLib": true, "outDir": "dist" },
			"files": ["index.ts"],
			"references": [{ "path": "../cycle1" }]
		})")},
	        {"/project/cycle2/index.ts", "export const cycle2 = 1;"},
	        {"/project/cycle2/dist/index.js", "export const cycle2 = 1;"},
	    },
	    true, "/project"));
}

build::Orchestrator*
newCleanTestOrchestrator(etsc::System* sys,
                         const std::vector<std::string>& args) {
	std::vector<std::string> all{"--build"};
	for (auto& a : args)
		all.push_back(a);
	auto* command = tsoptions::ParseBuildCommandLine(all, sys);
	return build::NewOrchestrator(
	    build::Options{sys, command, nullptr});
}

void TestClean(T* t) {
	t->Parallel();

	t->Run("cleans selected project and references", [](T* t) {
		t->Parallel();
		auto* sys = newCleanTestSystem();
		auto* orchestrator =
		    newCleanTestOrchestrator(sys, {"a", "c"});

		auto* result = orchestrator->Clean("a");
		if (result->Result.Status != etsc::ExitStatusSuccess) {
			t->Errorf("Status = %v, want ExitStatusSuccess",
			          {(int)result->Result.Status});
		}
		if (result->Statistics.Projects != 2) {
			t->Errorf("Projects = %d, want 2",
			          {result->Statistics.Projects});
		}
		if (sys->fs()->FileExists("/project/a/dist/index.js")) {
			t->Error({"/project/a/dist/index.js should be deleted"});
		}
		if (sys->fs()->FileExists("/project/b/dist/index.js")) {
			t->Error({"/project/b/dist/index.js should be deleted"});
		}
		if (!sys->fs()->FileExists("/project/c/dist/index.js")) {
			t->Error({"/project/c/dist/index.js should exist"});
		}
	});

	t->Run("dry run preserves outputs", [](T* t) {
		t->Parallel();
		auto* sys = newCleanTestSystem();
		auto* orchestrator = newCleanTestOrchestrator(sys, {"--dry", "a"});

		auto* result = orchestrator->Clean("a");
		if (result->Result.Status != etsc::ExitStatusSuccess) {
			t->Errorf("Status = %v, want ExitStatusSuccess",
			          {(int)result->Result.Status});
		}
		if (result->Statistics.Projects != 2) {
			t->Errorf("Projects = %d, want 2",
			          {result->Statistics.Projects});
		}
		if (result->FilesToDelete.empty()) {
			t->Error({"expected FilesToDelete"});
		}
		if (!sys->fs()->FileExists("/project/a/dist/index.js")) {
			t->Error({"/project/a/dist/index.js should exist"});
		}
		if (!sys->fs()->FileExists("/project/b/dist/index.js")) {
			t->Error({"/project/b/dist/index.js should exist"});
		}
	});

	t->Run("rejects project outside build", [](T* t) {
		t->Parallel();
		auto* sys = newCleanTestSystem();
		auto* orchestrator = newCleanTestOrchestrator(sys, {"a"});

		auto* result = orchestrator->Clean("c");
		if (result->Result.Status !=
		    etsc::ExitStatusInvalidProject_OutputsSkipped) {
			t->Errorf(
			    "Status = %v, want ExitStatusInvalidProject_OutputsSkipped",
			    {(int)result->Result.Status});
		}
		if (!sys->fs()->FileExists("/project/a/dist/index.js")) {
			t->Error({"/project/a/dist/index.js should exist"});
		}
		if (!sys->fs()->FileExists("/project/b/dist/index.js")) {
			t->Error({"/project/b/dist/index.js should exist"});
		}
		if (!sys->fs()->FileExists("/project/c/dist/index.js")) {
			t->Error({"/project/c/dist/index.js should exist"});
		}
	});

	t->Run("rejects circular build", [](T* t) {
		t->Parallel();
		auto* sys = newCleanTestSystem();
		auto* orchestrator = newCleanTestOrchestrator(sys, {"cycle1"});

		auto* result = orchestrator->Clean("cycle1");
		if (result->Result.Status !=
		    etsc::ExitStatusProjectReferenceCycle_OutputsSkipped) {
			t->Errorf(
			    "Status = %v, want "
			    "ExitStatusProjectReferenceCycle_OutputsSkipped",
			    {(int)result->Result.Status});
		}
		if (result->Errors.empty()) {
			t->Error({"expected Errors"});
		}
		if (!sys->fs()->FileExists("/project/cycle1/dist/index.js")) {
			t->Error({"/project/cycle1/dist/index.js should exist"});
		}
		if (!sys->fs()->FileExists("/project/cycle2/dist/index.js")) {
			t->Error({"/project/cycle2/dist/index.js should exist"});
		}
	});
}
REGISTER_UNIT_TEST("build.TestClean", TestClean);

} // namespace
} // namespace tsc
