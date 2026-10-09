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


// watch_test.go — resolution-lookup glob mapping preserves the stored
// presentation spellings and aggregates directories by canonical path.
void TestResolutionLookupWatcherPreservesIncludedDirectorySpelling(tsc::gostd::testing::T* t) {
	namespace assert = tsc::gotest::assert;

	t->Parallel();

	tsc::collections::SyncMap<tsc::tspath::Path, std::string> files;
	for (auto& fileName : {"/Workspace/src/index.ts",
	                     "/Project/src/index.ts", "/Lib/lib.d.ts"}) {
		files.Store(tsc::tspath::toPath(fileName, "/", false),
		            std::string(fileName));
	}

	auto result =
	    tsc::project::createResolutionLookupGlobMapper("/Workspace", "/Lib",
	                                     "/Project", false)(&files);

	assert::DeepEqual(t, result.patternsInsideWorkspace,
	                  std::vector<std::string>{"/Workspace/**/*",
	                                           "/Project/**/*",
	                                           "/Lib/**/*"});
}

void TestResolutionLookupWatcherPreservesNodeModulesSpelling(tsc::gostd::testing::T* t) {
	namespace assert = tsc::gotest::assert;

	t->Parallel();

	tsc::collections::SyncMap<tsc::tspath::Path, std::string> files;
	std::string fileName = "/External/Node_Modules/pkg/index.ts";
	files.Store(tsc::tspath::toPath(fileName, "/", false), fileName);

	auto result =
	    tsc::project::createResolutionLookupGlobMapper("/Workspace", "/Lib",
	                                     "/Project", false)(&files);

	assert::DeepEqual(t, result.patternsInsideWorkspace,
	                  std::vector<std::string>{
	                      "/External/Node_Modules/**/*"});
}

void TestResolutionLookupWatcherAggregatesUsingHostCaseSensitivity(tsc::gostd::testing::T* t) {
	namespace assert = tsc::gotest::assert;

	t->Parallel();

	for (bool useCaseSensitiveFileNames : {true, false}) {
		t->Run(useCaseSensitiveFileNames ? "case sensitive"
		                                 : "case insensitive",
		       [useCaseSensitiveFileNames](tsc::gostd::testing::T* t) {
			       t->Parallel();

			       tsc::collections::SyncMap<tsc::tspath::Path, std::string>
			           files;
			       for (auto& fileName :
			            {"/External/Lib/src/a.ts",
			             "/external/LIB/test/b.ts"}) {
				       files.Store(
				           tsc::tspath::toPath(fileName, "/",
				                          useCaseSensitiveFileNames),
				           std::string(fileName));
			       }

			       auto result =
			           tsc::project::createResolutionLookupGlobMapper(
			               "/Workspace", "/Lib", "/Project",
			               useCaseSensitiveFileNames)(&files);

			       if (useCaseSensitiveFileNames) {
				       assert::DeepEqual(
				           t, result.directoriesOutsideWorkspace,
				           std::vector<std::string>{
				               "/External/Lib/src",
				               "/external/LIB/test"});
			       } else {
				       assert::Equal(
				           t,
				           result.directoriesOutsideWorkspace
				               .size(),
				           size_t(1));
				       auto& directory =
				           result.directoriesOutsideWorkspace[0];
				       assert::Assert(
				           t,
				           directory == "/External/Lib" ||
				               directory == "/external/LIB");
			       }
		       });
	}
}

}  // namespace

REGISTER_UNIT_TEST("project.TestGetPathComponentsForWatching",
                   TestGetPathComponentsForWatching);
REGISTER_UNIT_TEST("project.TestNilWatchedFilesClone", TestNilWatchedFilesClone);
REGISTER_UNIT_TEST(
    "project.TestResolutionLookupWatcherPreservesIncludedDirectorySpelling",
    TestResolutionLookupWatcherPreservesIncludedDirectorySpelling);
REGISTER_UNIT_TEST(
    "project.TestResolutionLookupWatcherPreservesNodeModulesSpelling",
    TestResolutionLookupWatcherPreservesNodeModulesSpelling);
REGISTER_UNIT_TEST(
    "project.TestResolutionLookupWatcherAggregatesUsingHostCaseSensitivity",
    TestResolutionLookupWatcherAggregatesUsingHostCaseSensitivity);
