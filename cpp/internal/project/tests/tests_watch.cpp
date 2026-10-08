// Port of tsc/internal/project/watch_test.go.
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/project/watch.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

void TestGetPathComponentsForWatching(tsc::gostd::testing::T* t) {
	namespace assert = tsc::gotest::assert;
	using tsc::project::getPathComponentsForWatching;

	t->Parallel();

	assert::DeepEqual(t, getPathComponentsForWatching("/project", ""),
	                  std::vector<std::string>{"/", "project"});
	assert::DeepEqual(t, getPathComponentsForWatching("C:\\project", ""),
	                  std::vector<std::string>{"C:/", "project"});
	assert::DeepEqual(
	    t,
	    getPathComponentsForWatching("//server/share/project/tsconfig.json",
	                                 ""),
	    std::vector<std::string>{"//server/share", "project",
	                             "tsconfig.json"});
	assert::DeepEqual(
	    t,
	    getPathComponentsForWatching("\\\\server\\share\\project\\tsconfig."
	                                 "json",
	                                 ""),
	    std::vector<std::string>{"//server/share", "project",
	                             "tsconfig.json"});
	assert::DeepEqual(t, getPathComponentsForWatching("C:\\Users", ""),
	                  std::vector<std::string>{"C:/Users"});
	assert::DeepEqual(
	    t, getPathComponentsForWatching("C:\\Users\\andrew\\project", ""),
	    std::vector<std::string>{"C:/Users/andrew", "project"});
	assert::DeepEqual(t, getPathComponentsForWatching("/home", ""),
	                  std::vector<std::string>{"/home"});
	assert::DeepEqual(
	    t, getPathComponentsForWatching("/home/andrew/project", ""),
	    std::vector<std::string>{"/home/andrew", "project"});
}

void TestNilWatchedFilesClone(tsc::gostd::testing::T* t) {
	namespace assert = tsc::gotest::assert;

	t->Parallel();

	tsc::project::WatchedFiles<int>* w = nullptr;
	auto* result = tsc::project::watchedFilesClone(w, 42);
	assert::Assert(t, result == nullptr,
	               "clone on a nil `WatchedFiles` should return nil");
}

}  // namespace

REGISTER_UNIT_TEST("project.TestGetPathComponentsForWatching",
                   TestGetPathComponentsForWatching);
REGISTER_UNIT_TEST("project.TestNilWatchedFilesClone", TestNilWatchedFilesClone);
