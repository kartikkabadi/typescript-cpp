// Port of walkdir_test.go — walkDir + walkDirGeneric over the same cases.

#include <map>
#include <unistd.h>

#include "internal/fswatch/tests/util.h"

namespace tsc::fswatch {

namespace {

// Go: type walkDirFunc = func(dir string, recursive bool, fn func(string,
// bool) error) error
using walkDirFunc = gostd::Error (*)(const std::string&, bool,
                                     const walkFn&);

void runWalkDirTest(T* t, const std::function<void(T*, walkDirFunc)>& fn) {
	t->Helper();
	t->Parallel();
	struct {
		const char* name;
		walkDirFunc fn;
	} rts[] = {
	    {"native",
	     +[](const std::string& d, bool r, const walkFn& f) {
		     return walkDir(d, r, f);
	     }},
	    {"generic",
	     +[](const std::string& d, bool r, const walkFn& f) {
		     return walkDirGeneric(d, r, f);
	     }},
	};
	for (auto& rt : rts) {
		t->Run(rt.name, [&rt, &fn](T* st) {
			st->Parallel();
			fn(st, rt.fn);
		});
	}
}

void testWalkDirDoesNotFollowRootSymlinkedDir(T* t, walkDirFunc walk) {
	realT rt{t};
	std::string root = newTmpDir(&rt);
	std::string target = filepathJoin(t->TempDir(), "target");
	if (auto err = osMkdir(target, 0755); err != nullptr)
		t->Fatal({err});

	std::string link = filepathJoin(root, "link");
	makeDirSymlink(&rt, target, link);

	if (auto err = walk(link, true, nullptr); err == nullptr) {
		t->Fatal({"expected error for root symlinked directory"});
	}
}

void testWalkDirDoesNotFollowSymlinkedDir(T* t, walkDirFunc walk) {
	realT rt{t};
	std::string root = newTmpDir(&rt);
	std::string target = filepathJoin(t->TempDir(), "target");
	if (auto err = osMkdir(target, 0755); err != nullptr)
		t->Fatal({err});
	if (auto err =
	        osWriteFile(filepathJoin(target, "child"), "hidden", 0644);
	    err != nullptr)
		t->Fatal({err});

	std::string link = filepathJoin(root, "link");
	makeDirSymlink(&rt, target, link);

	std::map<std::string, bool> found;
	if (auto err = walk(root, true,
	                    [&](const std::string& path, bool isDir) -> Error {
		                    found[path] = isDir;
		                    return nullptr;
	                    });
	    err != nullptr) {
		t->Fatal({err});
	}
	auto it = found.find(link);
	if (it == found.end()) {
		t->Fatalf("symlink %q missing from walk", {link});
	}
	if (it->second) {
		t->Fatalf("symlink %q was treated as a directory", {link});
	}
	if (found.count(filepathJoin(link, "child"))) {
		t->Fatal({"walkDir followed symlinked directory"});
	}
}

void testWalkDirIgnoresUnreadableSubdir(T* t, walkDirFunc walk) {
	// runtime.GOOS == "windows" — n/a (Linux-only build).
	if (::geteuid() == 0) {
		t->Skip({"root can read directories regardless of mode bits"});
	}

	realT rt{t};
	std::string root = newTmpDir(&rt);
	std::string denied = filepathJoin(root, "denied");
	if (auto err = osMkdir(denied, 0700); err != nullptr)
		t->Fatal({err});
	std::string child = filepathJoin(denied, "child");
	if (auto err = osWriteFile(child, "hidden", 0644); err != nullptr)
		t->Fatal({err});
	if (auto err = osChmod(denied, 0); err != nullptr)
		t->Fatal({err});
	t->Cleanup([denied] { (void)osChmod(denied, 0700); });

	std::map<std::string, bool> found;
	if (auto err = walk(root, true,
	                    [&](const std::string& path, bool isDir) -> Error {
		                    found[path] = isDir;
		                    return nullptr;
	                    });
	    err != nullptr) {
		t->Fatal({err});
	}
	if (found.count(denied)) {
		t->Fatalf("unreadable directory should be ignored, found %q",
		          {denied});
	}
	if (found.count(child)) {
		t->Fatalf("unreadable child should be ignored, found %q", {child});
	}
}

void testWalkDirMissingDir(T* t, walkDirFunc walk) {
	std::string dir = filepathJoin(t->TempDir(), "nonexistent");
	if (auto err = walk(dir, true, nullptr); err == nullptr) {
		t->Fatal({"expected error for missing directory"});
	}
}

void testWalkDirNotADir(T* t, walkDirFunc walk) {
	std::string f = filepathJoin(t->TempDir(), "file");
	if (auto err = osWriteFile(f, "x", 0644); err != nullptr)
		t->Fatal({err});
	if (auto err = walk(f, true, nullptr); err == nullptr) {
		t->Fatal({"expected error for non-directory"});
	}
}

void testWalkDirEntries(T* t, walkDirFunc walk) {
	realT rt{t};
	std::string root = newTmpDir(&rt);
	if (auto err =
	        osWriteFile(filepathJoin(root, "a.txt"), "a", 0644);
	    err != nullptr)
		t->Fatal({err});
	std::string sub = filepathJoin(root, "sub");
	if (auto err = osMkdir(sub, 0755); err != nullptr)
		t->Fatal({err});
	if (auto err = osWriteFile(filepathJoin(sub, "b.txt"), "b", 0644);
	    err != nullptr)
		t->Fatal({err});

	std::map<std::string, bool> found;
	if (auto err = walk(root, true,
	                    [&](const std::string& path, bool isDir) -> Error {
		                    found[path] = isDir;
		                    return nullptr;
	                    });
	    err != nullptr) {
		t->Fatal({err});
	}
	if (!found.count(filepathJoin(root, "a.txt")))
		t->Fatal({"missing a.txt"});
	if (!found.count(sub))
		t->Fatal({"missing sub/"});
	if (!found.count(filepathJoin(sub, "b.txt")))
		t->Fatal({"missing sub/b.txt"});
}

void testWalkDirCallback(T* t, walkDirFunc walk) {
	realT rt{t};
	std::string root = newTmpDir(&rt);
	std::string sub = filepathJoin(root, "sub");
	if (auto err = osMkdir(sub, 0755); err != nullptr)
		t->Fatal({err});
	if (auto err = osWriteFile(filepathJoin(sub, "f.txt"), "f", 0644);
	    err != nullptr)
		t->Fatal({err});

	std::vector<std::string> dirs, files;
	auto err =
	    walk(root, true, [&](const std::string& path, bool isDir) -> Error {
		    if (isDir)
			    dirs.push_back(path);
		    else
			    files.push_back(path);
		    return nullptr;
	    });
	if (err != nullptr)
		t->Fatal({err});
	if (dirs.size() < 2) {
		t->Fatalf("expected at least 2 dirs (root + sub), got %d",
		          {(int)dirs.size()});
	}
	if (files.size() < 1) {
		t->Fatalf("expected at least 1 file, got %d", {(int)files.size()});
	}
}

void testWalkDirCallbackError(T* t, walkDirFunc walk) {
	realT rt{t};
	std::string root = newTmpDir(&rt);
	if (auto err =
	        osWriteFile(filepathJoin(root, "a.txt"), "a", 0644);
	    err != nullptr)
		t->Fatal({err});

	auto sentinel = gostd::newError("stop");
	auto err = walk(root, true,
	                [&](const std::string& path, bool isDir) -> Error {
		                return sentinel;
	                });
	if (!gostd::errorIs(err, sentinel)) {
		t->Fatalf("expected sentinel error, got %v", {err});
	}
}

void TestWalkDirDoesNotFollowSymlinkedDir(T* t) {
	runWalkDirTest(t, testWalkDirDoesNotFollowSymlinkedDir);
}
REGISTER_UNIT_TEST("fswatch.TestWalkDirDoesNotFollowSymlinkedDir",
                   TestWalkDirDoesNotFollowSymlinkedDir);

void TestWalkDirDoesNotFollowRootSymlinkedDir(T* t) {
	runWalkDirTest(t, testWalkDirDoesNotFollowRootSymlinkedDir);
}
REGISTER_UNIT_TEST("fswatch.TestWalkDirDoesNotFollowRootSymlinkedDir",
                   TestWalkDirDoesNotFollowRootSymlinkedDir);

void TestWalkDirIgnoresUnreadableSubdir(T* t) {
	runWalkDirTest(t, testWalkDirIgnoresUnreadableSubdir);
}
REGISTER_UNIT_TEST("fswatch.TestWalkDirIgnoresUnreadableSubdir",
                   TestWalkDirIgnoresUnreadableSubdir);

void TestWalkDirMissingDir(T* t) { runWalkDirTest(t, testWalkDirMissingDir); }
REGISTER_UNIT_TEST("fswatch.TestWalkDirMissingDir", TestWalkDirMissingDir);

void TestWalkDirNotADir(T* t) { runWalkDirTest(t, testWalkDirNotADir); }
REGISTER_UNIT_TEST("fswatch.TestWalkDirNotADir", TestWalkDirNotADir);

void TestWalkDirEntries(T* t) { runWalkDirTest(t, testWalkDirEntries); }
REGISTER_UNIT_TEST("fswatch.TestWalkDirEntries", TestWalkDirEntries);

void TestWalkDirCallback(T* t) { runWalkDirTest(t, testWalkDirCallback); }
REGISTER_UNIT_TEST("fswatch.TestWalkDirCallback", TestWalkDirCallback);

void TestWalkDirCallbackError(T* t) {
	runWalkDirTest(t, testWalkDirCallbackError);
}
REGISTER_UNIT_TEST("fswatch.TestWalkDirCallbackError",
                   TestWalkDirCallbackError);

} // namespace
} // namespace tsc::fswatch
