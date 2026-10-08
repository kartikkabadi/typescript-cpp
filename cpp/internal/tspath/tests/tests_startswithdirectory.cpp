// Port of tsc/internal/tspath/startsWithDirectory_test.go (package tspath).
#include <string>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
using namespace tsc;
using tsc::tspath::startsWithDirectory;

namespace {

struct startsWithDirCase {
	const char* name;
	const char* fileName;
	const char* directoryName;
	bool useCaseSensitiveFileNames;
	bool expected;
};

} // namespace

static void TestStartsWithDirectory(T* t) {
	t->Parallel();
	static const startsWithDirCase tests[] = {
	    {"exact match case sensitive", "/project/src/file.ts", "/project/src",
	     true, true},
	    {"exact match case insensitive", "/project/src/file.ts",
	     "/PROJECT/SRC", false, true},
	    {"case sensitive mismatch", "/project/src/file.ts", "/PROJECT/SRC",
	     true, false},
	    {"file not in directory", "/project/lib/file.ts", "/project/src", true,
	     false},
	    {"file in subdirectory", "/project/src/components/Button.tsx",
	     "/project/src", true, true},
	    {"file in parent directory", "/project/file.ts", "/project/src", true,
	     false},
	    {"windows style separators", "C:\\project\\src\\file.ts",
	     "C:\\project\\src", true, true},
	    {"mixed separators", "/project/src/file.ts", "\\project\\src", true,
	     false},
	    {"empty directory name", "/project/src/file.ts", "", true, false},
	    {"empty file name", "", "/project/src", true, false},
	    // File name doesn't start with directory + separator
	    {"identical paths", "/project/src", "/project/src", true, false},
	    {"directory with trailing separator", "/project/src/file.ts",
	     "/project/src/", true, true},
	    {"unicode characters", "/project/测试/file.ts", "/project/测试", true,
	     true},
	    {"unicode case insensitive", "/project/测试/file.ts", "/PROJECT/测试",
	     false, true},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* t) {
			t->Parallel();
			bool result = startsWithDirectory(
				tt.fileName, tt.directoryName, tt.useCaseSensitiveFileNames);
			if (result != tt.expected) {
				t->Errorf(
					"StartsWithDirectory(%s, %s, %v) = %v, expected %v",
					{tt.fileName, tt.directoryName,
					 tt.useCaseSensitiveFileNames, result, tt.expected});
			}
		});
	}
}

static void TestStartsWithDirectoryEdgeCases(T* t) {
	t->Parallel();
	static const startsWithDirCase tests[] = {
	    {"file name shorter than directory", "/proj", "/project", true, false},
	    {"file name starts with directory but no separator",
	     "/projectsrc/file.ts", "/project", true, false},
	    {"relative paths", "src/file.ts", "src", true, true},
	    {"absolute vs relative", "/project/src/file.ts", "project/src", true,
	     false},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* t) {
			t->Parallel();
			bool result = startsWithDirectory(
				tt.fileName, tt.directoryName, tt.useCaseSensitiveFileNames);
			if (result != tt.expected) {
				t->Errorf(
					"StartsWithDirectory(%s, %s, %v) = %v, expected %v",
					{tt.fileName, tt.directoryName,
					 tt.useCaseSensitiveFileNames, result, tt.expected});
			}
		});
	}
}

REGISTER_UNIT_TEST("tspath.TestStartsWithDirectory", TestStartsWithDirectory);
REGISTER_UNIT_TEST("tspath.TestStartsWithDirectoryEdgeCases",
				   TestStartsWithDirectoryEdgeCases);
