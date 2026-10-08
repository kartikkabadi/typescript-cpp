// Port of watcher_test.go — CRUD events for files, directories,
// sub-entries, and symlinks; event coalescing; multiple subscriptions;
// error handling; watch lifecycle; public API validation; and
// watcherBase/dirWatchError internals.

#include <atomic>
#include <set>
#include <thread>

#include "internal/fswatch/tests/util.h"

namespace tsc::fswatch {

namespace {

// must — `if err := op(); err != nil { t.Fatal(err) }` shorthand.
inline void must(TestI* t, Error err) {
	if (err != nullptr)
		t->Fatal({err});
}
inline void must(T* t, Error err) {
	if (err != nullptr)
		t->Fatal({err});
}

// ----- pure-comparer / path tests -------------------------------------------

void TestPathComparer(T* t) {
	t->Parallel();
	struct Case {
		std::string root;
		std::string path;
		std::string suffix;
		bool exact;
		bool ignoreCase;
	};
	std::vector<Case> tests = {
	    {"/root", "/root", "", true, true},
	    {"/root", "/root/file.ts", "/file.ts", true, true},
	    {"/root", "/ROOT", "", false, true},
	    {"/root", "/ROOT/File.ts", "/File.ts", false, true},
	    {"/root", "/ROOT/Nested/File.ts", "/Nested/File.ts", false, true},
	    {"/root", "/ROOT2/File.ts", "", false, false},
	    {"/root", "/roo", "", false, false},
	    {"/root/sub", "/ROOT", "", false, false},
	    {"/root", "/other/File.ts", "", false, false},
	    {"/root", "/ROOTish/File.ts", "", false, false},
	    {"/root/sub", "/ROOT/SUB", "", false, true},
	    {"/root/sub", "/ROOT/su", "", false, false},
	    {"/root/[", "/ROOT/{/File.ts", "", false, false},
	    {"/root/@", "/ROOT/`/File.ts", "", false, false},
	    {"/", "/File.ts", "File.ts", true, true},
	    {"/", "/", "", true, true},
	    {"", "", "", false, false},
	    {"", "/File.ts", "", false, false},
	    {"/caf\u00e9", "/CAF\u00c9/File.ts", "/File.ts", false, true},
	    {"/s", "/\u017f/File.ts", "/File.ts", false, true},
	    {"/\u017f", "/S/File.ts", "/File.ts", false, true},
	    {"/s/sub", "/\u017f/SUB/File.ts", "/File.ts", false, true},
	    {"/\u017f/sub", "/S/SUB/File.ts", "/File.ts", false, true},
	    {"/s", "/\u017foo/File.ts", "", false, false},
	    {"/k", "/\u212a/File.ts", "/File.ts", false, true},
	    {"/\u03c3", "/\u03c2/File.ts", "/File.ts", false, true},
	    {"/\u00e9", "/\u00c8/File.ts", "", false, false},
	    {"/\u00df", "/SS/File.ts", "/File.ts", false, nativePathFolding},
	    {"/root/s", "/ROOT/\u017f/File.ts", "/File.ts", false, true},
	    {"/root/\u017f", "/ROOT/S", "", false, true},
	};
	for (auto& tt : tests) {
		for (bool ignoreCase : {false, true}) {
			pathComparer comparer{ignoreCase};
			bool want = ignoreCase ? tt.ignoreCase : tt.exact;
			auto [suffix, ok] = comparer.suffix(tt.root, tt.path);
			if (ok != want || (ok && suffix != tt.suffix)) {
				t->Errorf("suffix(%q, %q), ignoreCase=%v: got (%q, %v), "
				          "want (%q, %v)",
				          {tt.root, tt.path, ignoreCase, suffix, ok,
				           tt.suffix, want});
			}
			if (comparer.contains(tt.root, tt.path) != want) {
				t->Errorf("contains(%q, %q), ignoreCase=%v: want %v",
				          {tt.root, tt.path, ignoreCase, want});
			}
			for (const char* to : {"/display", "/"}) {
				auto [rebased, ok2] = comparer.rebase(tt.path, tt.root, to);
				if (ok2 != want ||
				    (ok2 && rebased != joinPathSuffix(to, tt.suffix))) {
					t->Errorf("rebase(%q, %q, %q), ignoreCase=%v: got "
					          "(%q, %v)",
					          {tt.path, tt.root, std::string(to),
					           ignoreCase, rebased, ok2});
				}
			}
		}
	}
}
REGISTER_UNIT_TEST("fswatch.TestPathComparer", TestPathComparer);

void TestPathComparerUnicodeAlignment(T* t) {
	t->Parallel();
	std::vector<std::string> parts = {
	    "s", "S", "\u017f", "k", "K", "\u212a", "\u03c3", "\u03c2", "\u00e9",
	    "\u00c9", "\u00c8", "\U00010400", "\U00010428", "\xff", "\xfe",
	    "\xc3"};
	pathComparer comparer{true};
	for (int padding = 0; padding < 16; padding++) {
		std::string prefix = "/" + std::string(padding, 'a');
		for (auto& a : parts) {
			for (auto& b : parts) {
				for (const char* child : {"", "/child"}) {
					std::string root = prefix + a + std::string(child);
					std::string upper;
					for (char c : std::string(child))
						upper += (char)std::toupper((unsigned char)c);
					std::string path = prefix + b + upper + "/File.ts";
					bool want = equalFold(a, b);
					auto [suffix, ok] = comparer.suffix(root, path);
					if (ok != want || (ok && suffix != "/File.ts")) {
						t->Fatalf("suffix(%q, %q): got (%q, %v), want "
						          "match=%v",
						          {root, path, suffix, ok, want});
					}
				}
			}
		}
	}
}
REGISTER_UNIT_TEST("fswatch.TestPathComparerUnicodeAlignment",
                   TestPathComparerUnicodeAlignment);

void TestFileCallbackCaseSensitivity(T* t) {
	t->Parallel();
	realT rt{t};
	for (bool ignoreCase : {false, true}) {
		std::vector<Event> got;
		auto dw = newDirectWatcherShared(&rt, "/root");
		dw->setComparer(pathComparer{ignoreCase});
		dw->addCallback(
		    "/root", "/root", false,
		    [&](std::vector<Event> events, Error) {
			    for (auto& e : events)
				    got.push_back(e);
		    },
		    nullptr, "/root/file.ts");
		dw->events.update("/root/FILE.ts");
		dw->events.update("/root/other.ts");
		dw->triggerCallbacks();
		if (ignoreCase) {
			if (got.size() != 1 || got[0].path != "/root/file.ts") {
				t->Fatalf("case-insensitive callback: got %d events",
				          {(int)got.size()});
			}
		} else if (!got.empty()) {
			t->Fatalf("case-sensitive callback: got %d events",
			          {(int)got.size()});
		}
	}
}
REGISTER_UNIT_TEST("fswatch.TestFileCallbackCaseSensitivity",
                   TestFileCallbackCaseSensitivity);

void TestPathComparerExactKeys(T* t) {
	t->Parallel();
	PathComparer c;
	for (std::string path : std::vector<std::string>{"/A/File.ts",
	    "/straße/İ.ts", "/café.ts", std::string("/bad\xff.ts")}) {
		if (auto got = c.key(path); got != path) {
			t->Fatalf("Key(%q) = %q", {path, got});
		}
	}
	if (auto [_, ok] = c.rebase("/A/file.ts", "/a", "/target"); ok) {
		t->Fatal({"zero comparer must use exact matching"});
	}
	if (auto [got, ok] = c.rebase("/a/file.ts", "/a", "/target");
	    !ok || got != "/target/file.ts") {
		t->Fatalf("Rebase = %q, %v", {got, ok});
	}
}
REGISTER_UNIT_TEST("fswatch.TestPathComparerExactKeys",
                   TestPathComparerExactKeys);

void TestRebasePath(T* t) {
	t->Parallel();

	std::string root = "/";
	std::string from = filepathJoin(root, "from");
	std::string to = filepathJoin(root, "to");

	struct Case {
		const char* name;
		std::string path, from, to, want;
	};
	std::vector<Case> tests = {
	    {"exact root", from, from, to, to},
	    {"child", filepathJoin(from, "child"), from, to,
	     filepathJoin(to, "child")},
	    {"sibling", filepathJoin(root, "from-sibling", "child"), from, to,
	     filepathJoin(root, "from-sibling", "child")},
	    {"from root", filepathJoin(root, "child"), root, to,
	     filepathJoin(to, "child")},
	    {"to root", filepathJoin(from, "child"), from, root,
	     filepathJoin(root, "child")},
	};
	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* st) {
			st->Parallel();
			if (auto got = rebasePath(tt.path, tt.from, tt.to);
			    got != tt.want) {
				st->Fatalf("rebasePath(%q, %q, %q) = %q, want %q",
				           {tt.path, tt.from, tt.to, got, tt.want});
			}
		});
	}
}
REGISTER_UNIT_TEST("fswatch.TestRebasePath", TestRebasePath);

void TestPhysicalDirForResolvesSymlinkAncestor(T* t) {
	t->Parallel();
	realT rt{t};

	std::string root = t->TempDir();
	std::string target = filepathJoin(root, "target");
	must(t, osMkdirAll(filepathJoin(target, "nested"), 0755));
	std::string link = filepathJoin(root, "link");
	makeDirSymlink(&rt, target, link);

	std::string dir = filepathJoin(link, "nested");
	std::string want = physicalDirFor(filepathJoin(target, "nested"));
	if (auto got = physicalDirFor(dir); got != want) {
		t->Fatalf("physicalDirFor(%q) = %q, want %q", {dir, got, want});
	}
}
REGISTER_UNIT_TEST("fswatch.TestPhysicalDirForResolvesSymlinkAncestor",
                   TestPhysicalDirForResolvesSymlinkAncestor);

void TestIsInDirectoryOrSelf(T* t) {
	t->Parallel();

	std::string root = "/";
	std::string parent = filepathJoin(root, "parent");
	std::string child = filepathJoin(parent, "child");
	std::string nested = filepathJoin(child, "nested");
	std::string siblingPrefix = filepathJoin(root, "parent-sibling");

	struct Case {
		const char* name;
		std::string dir, path;
		bool want;
	};
	std::vector<Case> tests = {
	    {"exact", parent, parent, true},
	    {"child", parent, child, true},
	    {"nested", parent, nested, true},
	    {"sibling prefix", parent, siblingPrefix, false},
	    {"root self", root, root, true},
	    {"root child", root, filepathJoin(root, "child"), true},
	    {"empty dir", "", child, false},
	};
	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* st) {
			st->Parallel();
			if (auto got = isInDirectoryOrSelf(tt.dir, tt.path);
			    got != tt.want) {
				st->Fatalf("isInDirectoryOrSelf(%q, %q) = %v, want %v",
				           {tt.dir, tt.path, got, tt.want});
			}
		});
	}
}
REGISTER_UNIT_TEST("fswatch.TestIsInDirectoryOrSelf",
                   TestIsInDirectoryOrSelf);

// ----- files ---------------------------------------------------------------

void TestWatchFileCreate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "hello", 0644));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestWatchFileCreate", TestWatchFileCreate);

void TestWatchFileUpdate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f = subPath(dir);
		// Mirror upstream JS: create file AFTER subscribe so the create
		// event populates the watcher's internal tree, then update it.
		must(t, osWriteFile(f, "v1", 0644));
		(void)r->waitForEvent(r->deadline(),
		                      [](const Event&) { return true; });
		must(t, osWriteFile(f, "v2-longer", 0644));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestWatchFileUpdate", TestWatchFileUpdate);

void TestWatchFileRename(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f1 = subPath(dir), f2 = subPath(dir);
		must(t, osWriteFile(f1, "x", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);
		must(t, osRename(f1, f2));
		expectEventSet(t, r,
		               {{EventKind::EventDelete, f1},
		                {EventKind::EventUpdate, f2}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestWatchFileRename", TestWatchFileRename);

void TestWatchFileRenameExisting(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f1 = subPath(dir);
		must(t, osWriteFile(f1, "hi", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f2 = subPath(dir);
		must(t, osRename(f1, f2));
		expectEventSet(t, r,
		               {{EventKind::EventDelete, f1},
		                {EventKind::EventUpdate, f2}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestWatchFileRenameExisting",
                   TestWatchFileRenameExisting);

void TestWatchFileDelete(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "x", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);
		must(t, osRemove(f));
		expectEventSequence(t, r, {{EventKind::EventDelete, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestWatchFileDelete", TestWatchFileDelete);

// ----- directories ---------------------------------------------------------

void TestSubscribeDirCreate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f = subPath(dir);
		must(t, osMkdir(f, 0755));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeDirCreate",
                   TestSubscribeDirCreate);

void TestSubscribeNonASCIIPath(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string parent = newTmpDir(t);
		// "café" + "résumé"; both precomposed NFC.
		std::string dir = filepathJoin(parent, "caf\u00e9-dir");
		must(t, osMkdir(dir, 0755));
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string child = filepathJoin(dir, "r\u00e9sum\u00e9.txt");
		must(t, osWriteFile(child, "hi", 0644));
		expectEventSequence(t, r, {{EventKind::EventUpdate, child}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeNonASCIIPath",
                   TestSubscribeNonASCIIPath);

void TestSubscribeDirRename(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f1 = subPath(dir);
		must(t, osMkdir(f1, 0755));
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f2 = subPath(dir);
		must(t, osRename(f1, f2));
		expectEventSet(t, r,
		               {{EventKind::EventDelete, f1},
		                {EventKind::EventUpdate, f2}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeDirRename",
                   TestSubscribeDirRename);

void TestSubscribeDirDelete(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = subPath(dir);
		must(t, osMkdir(f, 0755));
		auto [r, sub] = subscribeFor(t, dir, w);
		must(t, osRemoveAll(f));
		expectEventSequence(t, r, {{EventKind::EventDelete, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeDirDelete",
                   TestSubscribeDirDelete);

void TestSubscribeWatchedDirDeleted(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);
		must(t, osRemoveAll(dir));
		expectEventSequence(t, r, {{EventKind::EventDelete, dir}});

		// Give the backend a moment to surface ErrWatchTerminated
		// alongside the delete; some backends batch the error into a
		// later debounce tick than the event itself.
		auto deadline =
		    std::chrono::steady_clock::now() + r->deadline();
		while (std::chrono::steady_clock::now() < deadline) {
			size_t n;
			{
				std::lock_guard<std::mutex> lk(r->mu);
				n = r->errs.size();
			}
			if (n > 0)
				break;
			sleepFor(ms(20));
		}
		std::vector<Error> errs;
		{
			std::lock_guard<std::mutex> lk(r->mu);
			errs = std::move(r->errs);
			r->errs.clear();
		}
		bool sawTerminated = false;
		for (auto& e : errs) {
			if (gostd::errorIs(e, ErrWatchTerminated)) {
				sawTerminated = true;
				break;
			}
		}
		if (!sawTerminated) {
			t->Fatalf("expected ErrWatchTerminated after watched dir "
			          "delete, got %d errs",
			          {(int)errs.size()});
		}

		// Re-create; should not emit events for a now-stale watch.
		must(t, osMkdirAll(dir, 0755));
		auto extra = r->drainQuiet(ms(200));
		if (!extra.empty()) {
			t->Fatalf("expected no follow-up events, got %d",
			          {(int)extra.size()});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeWatchedDirDeleted",
                   TestSubscribeWatchedDirDeleted);

// ----- sub-files -----------------------------------------------------------

void TestSubscribeSubfileCreate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);

		std::string subDir = subPath(dir);
		must(t, osMkdir(subDir, 0755));
		expectContains(t, r, EventKind::EventUpdate, subDir);
		// Wait for the inotify watcher to finish setting up the watch on
		// the new dir before mutating it.
		sleepFor(ms(100));

		std::string f = subPath(subDir);
		must(t, osWriteFile(f, "hi", 0644));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeSubfileCreate",
                   TestSubscribeSubfileCreate);

void TestSubscribeSubfileUpdate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = subPath(dir);
		must(t, osMkdir(subDir, 0755));
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f = subPath(subDir);
		// WatchDirectory-then-create so the create event populates the
		// watcher's tree before the modify arrives.
		must(t, osWriteFile(f, "v1", 0644));
		(void)r->waitForEvent(r->deadline(),
		                      [](const Event&) { return true; });
		must(t, osWriteFile(f, "v2-longer", 0644));
		expectContains(t, r, EventKind::EventUpdate, f);
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeSubfileUpdate",
                   TestSubscribeSubfileUpdate);

void TestSubscribeSubfileRename(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = subPath(dir);
		must(t, osMkdir(subDir, 0755));
		std::string f1 = subPath(subDir);
		must(t, osWriteFile(f1, "x", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f2 = subPath(subDir);
		must(t, osRename(f1, f2));
		// Wait for both events to arrive before checking the set.
		std::vector<wantEvent> want{{EventKind::EventDelete, f1},
		                          {EventKind::EventUpdate, f2}};
		auto got = r->waitForAll(r->deadline(), want);
		auto filtered = filterEventsForPaths(got, {f1, f2});
		assertEventSet(t, filtered, want);
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeSubfileRename",
                   TestSubscribeSubfileRename);

void TestSubscribeSubfileDelete(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = subPath(dir);
		must(t, osMkdir(subDir, 0755));
		std::string f = subPath(subDir);
		must(t, osWriteFile(f, "x", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);
		must(t, osRemove(f));
		std::vector<wantEvent> want{{EventKind::EventDelete, f}};
		auto got = r->waitForAll(r->deadline(), want);
		auto filtered = filterEventsForPaths(got, {f});
		assertEventSequence(t, filtered, want);
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeSubfileDelete",
                   TestSubscribeSubfileDelete);

// ----- sub-directories -----------------------------------------------------

void TestSubscribeSubdirCreate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = subPath(dir);
		must(t, osMkdir(subDir, 0755));
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string nested = subPath(subDir);
		must(t, osMkdir(nested, 0755));
		std::vector<wantEvent> want{{EventKind::EventUpdate, nested}};
		auto got = r->waitForAll(r->deadline(), want);
		auto filtered = filterEventsForPaths(got, {nested});
		assertEventSequence(t, filtered, want);
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeSubdirCreate",
                   TestSubscribeSubdirCreate);

void TestSubscribeSubdirDeleteWithFiles(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = subPath(dir);
		must(t, osMkdir(subDir, 0755));
		std::string child = subPath(subDir);
		must(t, osWriteFile(child, "x", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);
		must(t, osRemoveAll(subDir));
		expectEventSet(t, r,
		               {{EventKind::EventDelete, subDir},
		                {EventKind::EventDelete, child}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeSubdirDeleteWithFiles",
                   TestSubscribeSubdirDeleteWithFiles);

// ----- symlinks ------------------------------------------------------------

void TestSubscribeSymlinkCreate(T* t) {
	t->Parallel();
	// DragonFlyBSD kqueue doesn't fire NOTE_WRITE on symlink creation —
	// GOOS-gated in Go, never taken here.
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f1 = subPath(dir);
		must(t, osWriteFile(f1, "x", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f2 = subPath(dir);
		must(t, osSymlink(f1, f2));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f2}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeSymlinkCreate",
                   TestSubscribeSymlinkCreate);

void TestSubscribeSymlinkDelete(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f1 = subPath(dir), f2 = subPath(dir);
		must(t, osWriteFile(f1, "x", 0644));
		must(t, osSymlink(f1, f2));
		auto [r, sub] = subscribeFor(t, dir, w);
		must(t, osRemove(f2));
		expectEventSequence(t, r, {{EventKind::EventDelete, f2}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeSymlinkDelete",
                   TestSubscribeSymlinkDelete);

void TestSubscribeSymlinkedDirectoryRebasesTargetEvents(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string target = filepathJoin(dir, "target");
		must(t, osMkdir(target, 0755));
		std::string link = filepathJoin(dir, "link");
		makeDirSymlink(t, target, link);

		auto [r, sub] = subscribeFor(t, link, w);
		std::string targetChild = filepathJoin(target, "child");
		must(t, osWriteFile(targetChild, "x", 0644));
		expectContains(t, r, EventKind::EventUpdate,
		               filepathJoin(link, "child"));
	});
}
REGISTER_UNIT_TEST(
    "fswatch.TestSubscribeSymlinkedDirectoryRebasesTargetEvents",
    TestSubscribeSymlinkedDirectoryRebasesTargetEvents);

void TestRecursiveSubscribeSymlinkedDirectoryDoesNotFollowDescendantSymlink(
    T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		if (w->hasFastRecursiveBackend()) {
			t->Skip({"fast recursive backends do not use the userspace "
			         "recursive walk"});
		}
		std::string dir = newTmpDir(t);
		std::string target = filepathJoin(dir, "target");
		must(t, osMkdir(target, 0755));
		std::string link = filepathJoin(dir, "link");
		makeDirSymlink(t, target, link);

		std::string descendantTarget =
		    filepathJoin(dir, "descendant-target");
		must(t, osMkdir(descendantTarget, 0755));
		std::string descendantLink =
		    filepathJoin(target, "descendant-link");
		makeDirSymlink(t, descendantTarget, descendantLink);

		auto [r, sub] = subscribeFor(t, link, w);
		std::string logicalGrandchild =
		    filepathJoin(link, "descendant-link", "grandchild");
		std::string physicalGrandchild =
		    filepathJoin(descendantTarget, "grandchild");
		must(t, osWriteFile(logicalGrandchild, "x", 0644));

		std::string marker = filepathJoin(target, "marker");
		must(t, osWriteFile(marker, "flush", 0644));

		auto got = expectContains(t, r, EventKind::EventUpdate,
		                          filepathJoin(link, "marker"));
		auto more = r->drainQuiet(2 * minWaitTime);
		got.insert(got.end(), more.begin(), more.end());
		assertNoEventsForPath(t, got, logicalGrandchild,
		                      "expected no events through descendant "
		                      "symlink");
		assertNoEventsForPath(t, got, physicalGrandchild,
		                      "expected no events for descendant symlink "
		                      "target");
	});
}
REGISTER_UNIT_TEST(
    "fswatch.TestRecursiveSubscribeSymlinkedDirectoryDoesNotFollow"
    "DescendantSymlink",
    TestRecursiveSubscribeSymlinkedDirectoryDoesNotFollowDescendantSymlink);

// ----- event coalescing ----------------------------------------------------

void TestSubscribeCoalesceCreateUpdate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "v1", 0644));
		must(t, osWriteFile(f, "v2", 0644));
		// The two writes should net to one update. Under host load the
		// debounce may split them across batches, so check the coalesced
		// effect via replayEventList rather than insisting on a single
		// delivered event.
		auto got = r->gatherUntilQuiet(r->deadline(), 3 * maxWaitTime);
		auto net =
		    replayEventList(filterEventsForPaths(got, {f}));
		assertEventSet(t, net, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeCoalesceCreateUpdate",
                   TestSubscribeCoalesceCreateUpdate);

void TestSubscribeCoalesceDeleteCreateAsUpdate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "v1", 0644));
		(void)r->waitForEvent(r->deadline(),
		                      [](const Event&) { return true; });
		must(t, osRemove(f));
		must(t, osWriteFile(f, "v2", 0644));
		// Net: delete+create coalesces to update.
		auto got = r->gatherUntilQuiet(r->deadline(), 3 * maxWaitTime);
		auto net =
		    replayEventList(filterEventsForPaths(got, {f}));
		assertEventSet(t, net, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeCoalesceDeleteCreateAsUpdate",
                   TestSubscribeCoalesceDeleteCreateAsUpdate);

void TestSubscribeCoalesceCreateThenDelete(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f1 = subPath(dir), f2 = subPath(dir);
		must(t, osWriteFile(f1, "x", 0644));
		must(t, osWriteFile(f2, "x", 0644));
		must(t, osRemove(f2));
		// Whether all three operations land in one debounce batch
		// (perfect coalescing → just [update f1]) or split across
		// batches depends on kernel timing. Either is correct as long
		// as the *net effect* leaves only [update f1].
		auto got = r->gatherUntilQuiet(r->deadline(), 3 * maxWaitTime);
		auto net = replayEventList(got);
		assertEventSet(t, net, {{EventKind::EventUpdate, f1}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeCoalesceCreateThenDelete",
                   TestSubscribeCoalesceCreateThenDelete);

void TestSubscribeCoalesceMultipleUpdates(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "v1", 0644));
		(void)r->waitForEvent(r->deadline(),
		                      [](const Event&) {
			                      return true;
		                    }); // consume initial update
		for (auto v : {"v2", "v3", "v4"}) {
			must(t, osWriteFile(f, v, 0644));
		}
		auto got = r->gatherUntilQuiet(r->deadline(), 3 * maxWaitTime);
		auto net =
		    replayEventList(filterEventsForPaths(got, {f}));
		assertEventSet(t, net, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeCoalesceMultipleUpdates",
                   TestSubscribeCoalesceMultipleUpdates);

void TestSubscribeCoalesceUpdateDelete(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);
		std::string f = subPath(dir);
		// Upstream's debouncer (by design) fires the first event in a
		// quiet window immediately. To exercise the coalescing path, we
		// create the file post-subscribe and consume that initial event.
		must(t, osWriteFile(f, "v1", 0644));
		(void)r->waitForEvent(r->deadline(),
		                      [](const Event&) { return true; });
		must(t, osWriteFile(f, "v2", 0644));
		must(t, osRemove(f));
		auto got = r->gatherUntilQuiet(r->deadline(), 3 * maxWaitTime);
		auto net =
		    replayEventList(filterEventsForPaths(got, {f}));
		assertEventSet(t, net, {{EventKind::EventDelete, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeCoalesceUpdateDelete",
                   TestSubscribeCoalesceUpdateDelete);

// ----- multiple subscriptions ----------------------------------------------

void TestSubscribeMultipleSameDir(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		// Let the backend register the freshly-created tmpDir before we
		// subscribe.
		sleepFor(ms(50));

		auto* r1 = newRecorder(t);
		auto [s1, err1] = w->watchDirectory(dir, r1->callback);
		if (err1 != nullptr)
			t->Fatal({err1});
		auto s1k = s1;
		t->Cleanup([s1k] { (void)s1k->close(); });

		auto* r2 = newRecorder(t);
		auto [s2, err2] = w->watchDirectory(dir, r2->callback);
		if (err2 != nullptr)
			t->Fatal({err2});
		auto s2k = s2;
		t->Cleanup([s2k] { (void)s2k->close(); });

		sleepFor(ms(100));
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "hi", 0644));
		assertEventSequence(t, r1->next(r1->deadline()),
		                    {{EventKind::EventUpdate, f}});
		assertEventSequence(t, r2->next(r2->deadline()),
		                    {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeMultipleSameDir",
                   TestSubscribeMultipleSameDir);

void TestSubscribeMultipleDifferentDirs(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir1 = newTmpDir(t), dir2 = newTmpDir(t);
		auto [r1, s1] = subscribeFor(t, dir1, w);
		auto [r2, s2] = subscribeFor(t, dir2, w);

		std::string f1 = subPath(dir1), f2 = subPath(dir2);
		must(t, osWriteFile(f1, "a", 0644));
		must(t, osWriteFile(f2, "b", 0644));
		assertEventSequence(t, r1->next(r1->deadline()),
		                    {{EventKind::EventUpdate, f1}});
		assertEventSequence(t, r2->next(r2->deadline()),
		                    {{EventKind::EventUpdate, f2}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeMultipleDifferentDirs",
                   TestSubscribeMultipleDifferentDirs);

void TestWatchDirectoriesBatch(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir1 = newTmpDir(t), dir2 = newTmpDir(t);
		auto* r1 = newRecorder(t);
		r1->watcher = w;
		auto* r2 = newRecorder(t);
		r2->watcher = w;

		auto [watches0, err] = w->watchDirectories({
		    {dir1, r1->callback, {WithRecursive()}},
		    {dir2, r2->callback, {WithRecursive()}},
		});
		if (err != nullptr)
			t->Fatal({err});
		auto watches = watches0;
		t->Cleanup([watches] {
			for (auto& watch : watches)
				(void)watch->close();
		});
		sleepFor(settleSleep(w));

		std::string f1 = subPath(dir1), f2 = subPath(dir2);
		must(t, osWriteFile(f1, "a", 0644));
		must(t, osWriteFile(f2, "b", 0644));
		assertEventSequence(t, r1->next(r1->deadline()),
		                    {{EventKind::EventUpdate, f1}});
		assertEventSequence(t, r2->next(r2->deadline()),
		                    {{EventKind::EventUpdate, f2}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestWatchDirectoriesBatch",
                   TestWatchDirectoriesBatch);

// ----- consolidation internals ---------------------------------------------

struct countingWatcherImpl : watcherBase {
	std::vector<std::shared_ptr<dirWatch>> subscribed;
	std::vector<std::shared_ptr<dirWatch>> closed;

	gostd::Error start() override {
		notifyStarted();
		return nullptr;
	}
	gostd::Error subscribe(std::shared_ptr<dirWatch> w) override {
		subscribed.push_back(w);
		return nullptr;
	}
	gostd::Error closeWatch(std::shared_ptr<dirWatch> w) override {
		closed.push_back(w);
		return nullptr;
	}
};

inline countingWatcherImpl* newCountingWatcherImpl() {
	auto* impl = new countingWatcherImpl();
	impl->init();
	return impl;
}

void TestFastRecursiveWatcherConsolidatesSiblingDirectories(T* t) {
	t->Parallel();

	std::string root = t->TempDir();
	std::string parent = filepathJoin(root, "node_modules", ".bun");
	must(t, osMkdirAll(parent, 0755));

	countingWatcherImpl* impl = nullptr;
	auto* wimpl = new watcher{"fsevents"};
	wimpl->factory = [&]() -> watcherImpl* {
		impl = newCountingWatcherImpl();
		return impl;
	};

	std::vector<std::shared_ptr<Watch>> subs;
	for (int i = 0; i < recursiveConsolidateThreshold + 2; i++) {
		std::string dir = filepathJoin(
		    parent, gostd::sprintf("pkg%d", {i}));
		must(t, osMkdirAll(dir, 0755));
		auto [sub, err] = wimpl->watchDirectory(
		    dir, [](std::vector<Event>, Error) {});
		if (err != nullptr)
			t->Fatal({err});
		subs.push_back(sub);
	}
	t->Cleanup([subs] {
		for (auto& sub : subs)
			(void)sub->close();
	});

	if (impl->subscribed.size() != recursiveConsolidateThreshold) {
		t->Fatalf("expected %d subscriptions after consolidation, got %d",
		          {recursiveConsolidateThreshold,
		           (int)impl->subscribed.size()});
	}
	auto consolidated = impl->subscribed.back();
	if (consolidated->dir != parent || !consolidated->recursive) {
		t->Fatalf("expected consolidated recursive watch on %s, got "
		          "dir=%s recursive=%v",
		          {parent, consolidated->dir, consolidated->recursive});
	}

	wimpl->mu.lock();
	bool hasPkgWatch = wimpl->dirWatches.count(
	    wimpl->keyForDirWatch(filepathJoin(parent, "pkg11"), false));
	wimpl->mu.unlock();
	if (hasPkgWatch) {
		t->Fatal({"expected later package watch to reuse consolidated "
		          "parent instead of creating its own stream"});
	}
}
REGISTER_UNIT_TEST(
    "fswatch.TestFastRecursiveWatcherConsolidatesSiblingDirectories",
    TestFastRecursiveWatcherConsolidatesSiblingDirectories);

void TestFastRecursiveWatcherDoesNotConsolidateSymlinkOutsideRoot(T* t) {
	t->Parallel();
	realT rt{t};

	std::string root = t->TempDir();
	std::string parent = filepathJoin(root, "node_modules", ".bun");
	must(t, osMkdirAll(parent, 0755));

	countingWatcherImpl* impl = nullptr;
	auto* wimpl = new watcher{"fsevents"};
	wimpl->factory = [&]() -> watcherImpl* {
		impl = newCountingWatcherImpl();
		return impl;
	};

	std::vector<std::shared_ptr<Watch>> subs;
	for (int i = 0; i < recursiveConsolidateThreshold; i++) {
		std::string dir = filepathJoin(
		    parent, gostd::sprintf("pkg%d", {i}));
		must(t, osMkdirAll(dir, 0755));
		auto [sub, err] = wimpl->watchDirectory(
		    dir, [](std::vector<Event>, Error) {});
		if (err != nullptr)
			t->Fatal({err});
		subs.push_back(sub);
	}
	t->Cleanup([subs] {
		for (auto& sub : subs)
			(void)sub->close();
	});

	auto consolidated = impl->subscribed.back();
	if (consolidated->dir != parent || !consolidated->recursive) {
		t->Fatalf("expected consolidated recursive watch on %s, got "
		          "dir=%s recursive=%v",
		          {parent, consolidated->dir, consolidated->recursive});
	}

	std::string target = filepathJoin(root, "outside");
	must(t, osMkdirAll(target, 0755));
	std::string link = filepathJoin(parent, "linked");
	makeDirSymlink(&rt, target, link);

	auto [sub, err] = wimpl->watchDirectory(
	    link, [](std::vector<Event>, Error) {});
	if (err != nullptr)
		t->Fatal({err});
	auto subk = sub;
	t->Cleanup([subk] { (void)subk->close(); });

	auto* wptr = dynamic_cast<watch*>(sub.get());
	if (wptr == nullptr || wptr->dw == consolidated) {
		t->Fatal({"symlink outside consolidated physical root should keep "
		          "its own watch"});
	}
	if (wptr->dw->dir != link ||
	    wptr->dw->physicalDir != physicalDirFor(link)) {
		t->Fatalf("expected watch on symlink root %s (%s), got %s (%s)",
		          {link, physicalDirFor(link), wptr->dw->dir,
		           wptr->dw->physicalDir});
	}
}
REGISTER_UNIT_TEST(
    "fswatch.TestFastRecursiveWatcherDoesNotConsolidateSymlinkOutsideRoot",
    TestFastRecursiveWatcherDoesNotConsolidateSymlinkOutsideRoot);

void TestConsolidatedSymlinkChildMapsSharedLogicalPath(T* t) {
	t->Parallel();
	realT rt{t};

	std::string root = t->TempDir();
	std::string physicalParent = filepathJoin(root, "physical-parent");
	must(t, osMkdirAll(filepathJoin(physicalParent, "target"), 0755));
	std::string logicalParent = filepathJoin(root, "logical-parent");
	makeDirSymlink(&rt, physicalParent, logicalParent);
	std::string link = filepathJoin(logicalParent, "link");
	makeDirSymlink(&rt, filepathJoin(physicalParent, "target"), link);

	callback cb;
	cb.dir = link;
	cb.physicalDir = physicalDirFor(link);
	cb.watchDir = logicalParent;
	cb.watchPhysicalDir = physicalDirFor(logicalParent);
	Event event{EventKind::EventUpdate,
	            filepathJoin(logicalParent, "target", "file.ts"), false};
	auto got = cb.mapEvent(event);
	std::string want = filepathJoin(link, "file.ts");
	if (got.path != want) {
		t->Fatalf("mapEvent path = %q, want %q", {got.path, want});
	}
}
REGISTER_UNIT_TEST(
    "fswatch.TestConsolidatedSymlinkChildMapsSharedLogicalPath",
    TestConsolidatedSymlinkChildMapsSharedLogicalPath);

void TestConsolidatedSymlinkChildTerminatesFromSharedLogicalPath(T* t) {
	t->Parallel();
	realT rt{t};

	std::string root = t->TempDir();
	std::string physicalParent = filepathJoin(root, "physical-parent");
	must(t, osMkdirAll(filepathJoin(physicalParent, "target"), 0755));
	std::string logicalParent = filepathJoin(root, "logical-parent");
	makeDirSymlink(&rt, physicalParent, logicalParent);
	std::string link = filepathJoin(logicalParent, "link");
	makeDirSymlink(&rt, filepathJoin(physicalParent, "target"), link);

	auto dw = newDirectWatcherShared(&rt, logicalParent);
	dw->physicalDir = physicalDirFor(logicalParent);
	auto [id, _] = dw->watch(link, physicalDirFor(link), true,
	                         [](std::vector<Event>, Error) {}, nullptr);
	auto err = gostd::newError("terminated");
	if (!dw->terminateCallbacksForDeletedRoot(
	        filepathJoin(logicalParent, "target"), 1, err)) {
		t->Fatal({"expected symlink child callback to terminate"});
	}
	std::lock_guard<std::mutex> lk(dw->mu);
	for (auto& cb : dw->callbacks) {
		if (cb.id == id && !gostd::errorIs(cb.terminal, err)) {
			t->Fatalf("terminal error = %v, want %v", {cb.terminal, err});
		}
	}
}
REGISTER_UNIT_TEST(
    "fswatch.TestConsolidatedSymlinkChildTerminatesFromSharedLogicalPath",
    TestConsolidatedSymlinkChildTerminatesFromSharedLogicalPath);

void TestConsolidatedChildWatchFiltersAgainstRequestedDir(T* t) {
	t->Parallel();
	realT rt{t};

	std::string parent = filepathJoin(t->TempDir(), "parent");
	std::string child = filepathJoin(parent, "child");
	std::string sibling = filepathJoin(parent, "sibling");
	auto dw = newDirectWatcherShared(&rt, parent);

	std::vector<Event> got;
	dw->watch(child, child, false,
	          [&](std::vector<Event> events, Error err) {
		          if (err != nullptr)
			          t->Fatal({err});
		          for (auto& e : events)
			          got.push_back(e);
	          },
	          nullptr);
	dw->events.updateWatchRootAt(child, 1);
	dw->events.update(filepathJoin(child, "file.ts"));
	dw->events.update(filepathJoin(child, "nested", "file.ts"));
	dw->events.update(filepathJoin(sibling, "file.ts"));
	dw->triggerCallbacks();

	auto gotW = toWantEvents(got);
	std::vector<wantEvent> want{
	    {EventKind::EventUpdate, child},
	    {EventKind::EventUpdate, filepathJoin(child, "file.ts")},
	};
	auto cmpEvents = [](const wantEvent& a, const wantEvent& b) {
		if (a.kind != b.kind)
			return (int)a.kind < (int)b.kind;
		return a.path < b.path;
	};
	std::sort(gotW.begin(), gotW.end(), cmpEvents);
	std::sort(want.begin(), want.end(), cmpEvents);
	if (!(gotW == want)) {
		t->Fatalf("event mismatch\n got: %d want: %d",
		          {(int)gotW.size(), (int)want.size()});
	}
}
REGISTER_UNIT_TEST(
    "fswatch.TestConsolidatedChildWatchFiltersAgainstRequestedDir",
    TestConsolidatedChildWatchFiltersAgainstRequestedDir);

void TestConsolidatedChildWatchIgnoresEventsBeforeSubscribe(T* t) {
	t->Parallel();
	realT rt{t};

	std::string parent = filepathJoin(t->TempDir(), "parent");
	std::string child = filepathJoin(parent, "child");
	auto dw = newDirectWatcherShared(&rt, parent);

	dw->events.update(child);
	std::vector<Event> got;
	dw->watch(child, child, true,
	          [&](std::vector<Event> events, Error err) {
		          if (err != nullptr)
			          t->Fatal({err});
		          for (auto& e : events)
			          got.push_back(e);
	          },
	          nullptr);
	dw->events.remove(child);
	dw->triggerCallbacks();

	assertEventSequence(&rt, got, {{EventKind::EventDelete, child}});
}
REGISTER_UNIT_TEST(
    "fswatch.TestConsolidatedChildWatchIgnoresEventsBeforeSubscribe",
    TestConsolidatedChildWatchIgnoresEventsBeforeSubscribe);

void TestRecursiveWatchWithIgnoreDoesNotFilterByLogicalRoot(T* t) {
	t->Parallel();
	realT rt{t};

	std::string dir = filepathJoin(t->TempDir(), "root");
	auto dw = newDirectWatcherShared(&rt, dir);

	std::vector<Event> got;
	std::string outsidePath =
	    filepathJoin(t->TempDir(), "outside", "pkg", "index.ts");
	dw->watch(dir, dir, true,
	          [&](std::vector<Event> events, Error err) {
		          if (err != nullptr)
			          t->Fatal({err});
		          for (auto& e : events)
			          got.push_back(e);
	          },
	          [](const std::string&) { return false; });
	dw->events.update(outsidePath);
	dw->triggerCallbacks();

	assertEventSequence(&rt, got, {{EventKind::EventUpdate, outsidePath}});
}
REGISTER_UNIT_TEST(
    "fswatch.TestRecursiveWatchWithIgnoreDoesNotFilterByLogicalRoot",
    TestRecursiveWatchWithIgnoreDoesNotFilterByLogicalRoot);

// ----- errors --------------------------------------------------------------

void TestSubscribeMissingDirError(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string bogus =
		    filepathJoin(newTmpDir(t), "definitely-not-here");
		auto [_, err] = w->watchDirectory(
		    bogus, [](std::vector<Event>, Error) {});
		if (err == nullptr) {
			t->Fatal({"expected error subscribing to non-existent dir"});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeMissingDirError",
                   TestSubscribeMissingDirError);

void TestSubscribeNotADirError(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "x", 0644));
		auto [_, err] =
		    w->watchDirectory(f, [](std::vector<Event>, Error) {});
		if (err == nullptr) {
			t->Fatal({"expected error subscribing to a file"});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeNotADirError",
                   TestSubscribeNotADirError);

void TestSubscribeRejectsNilCallback(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		auto [_, err] = w->watchDirectory(t->TempDir(), nullptr);
		if (err == nullptr) {
			t->Fatal({"WatchDirectory(nil callback) should return an "
			          "error"});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeRejectsNilCallback",
                   TestSubscribeRejectsNilCallback);

void TestSubscribeRejectsRelativePath(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		auto [_, err] = w->watchDirectory(
		    "relative/path", [](std::vector<Event>, Error) {});
		if (err == nullptr) {
			t->Fatal({"WatchDirectory with relative path should return "
			          "an error"});
		}
		auto [_2, err2] = w->watchFile(
		    "relative/path/file.txt",
		    [](std::vector<Event>, Error) {});
		if (err2 == nullptr) {
			t->Fatal({"WatchFile with relative path should return an "
			          "error"});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeRejectsRelativePath",
                   TestSubscribeRejectsRelativePath);

// ----- watch lifecycle -----------------------------------------------------

void TestSubscribeUnsubscribeIdempotent(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto* r = newRecorder(t);
		auto [sub, err] = w->watchDirectory(dir, r->callback);
		if (err != nullptr)
			t->Fatal({err});
		if (auto e = sub->close(); e != nullptr)
			t->Fatal({e});
		if (auto e = sub->close(); e != nullptr) {
			t->Fatalf("second Close should be a no-op, got %v", {e});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeUnsubscribeIdempotent",
                   TestSubscribeUnsubscribeIdempotent);

// TestSubscribeCloseThenReSubscribe verifies that Close fully tears down
// any kernel-side resources before returning, so a follow-on subscribe
// on the same path immediately afterwards observes events from a fresh
// watch.
void TestSubscribeCloseThenReSubscribe(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);

		auto* r1 = newRecorder(t);
		auto [s1, err] = w->watchDirectory(dir, r1->callback);
		if (err != nullptr)
			t->Fatal({err});
		if (auto e = s1->close(); e != nullptr)
			t->Fatal({e});

		auto* r2 = newRecorder(t);
		auto [s2, err2] = w->watchDirectory(dir, r2->callback);
		if (err2 != nullptr) {
			t->Fatalf("re-WatchDirectory after Close: %v", {err2});
		}
		auto s2k = s2;
		t->Cleanup([s2k] { (void)s2k->close(); });

		sleepFor((w == FSEvents() || w == Kqueue()) ? ms(300) : ms(60));

		std::string f = subPath(dir);
		must(t, osWriteFile(f, "hi", 0644));
		expectEventSequence(t, r2, {{EventKind::EventUpdate, f}});

		// The first recorder must not have seen the event meant for r2.
		auto stale = r1->drainQuiet(ms(50));
		if (!stale.empty()) {
			t->Fatalf("closed watch saw events: %d",
			          {(int)stale.size()});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeCloseThenReSubscribe",
                   TestSubscribeCloseThenReSubscribe);

// runtime.NumGoroutine analogue: count threads from /proc/self/status.
// Debounce/backend workers in the C++ port are real threads.
inline int numWorkerThreads() {
	std::ifstream in("/proc/self/status");
	std::string line;
	while (std::getline(in, line)) {
		if (line.rfind("Threads:", 0) == 0)
			return std::atoi(line.c_str() + 8);
	}
	return -1;
}

void TestSubscribeNoGoroutineLeak(T* t) {
	// No t->Parallel(): worker counting requires sequential execution.
	realT rt{t};
	for (auto* b : availableWatchers()) {
		t->Run(b->name(), [b](T* st) {
			realT srt{st};
			std::string dir = newTmpDir(&srt);
			// Warm up: trigger any lazy singleton init (backend,
			// debouncer) so it doesn't inflate the post-loop count.
			auto [warmup, werr] = b->watchDirectory(
			    dir, [](std::vector<Event>, Error) {});
			if (werr != nullptr)
				st->Fatal({werr});
			if (auto e = warmup->close(); e != nullptr)
				st->Fatal({e});
			sleepFor(ms(100));

			int baseline = numWorkerThreads();
			for (int i = 0; i < 8; i++) {
				auto* r = newRecorder(&srt);
				auto [sub, err] = b->watchDirectory(dir, r->callback);
				if (err != nullptr)
					st->Fatal({err});
				if (auto e = sub->close(); e != nullptr)
					st->Fatal({e});
			}
			// Allow lazy backend/debounce shutdown to settle.
			auto deadline =
			    std::chrono::steady_clock::now() + sec(2);
			while (std::chrono::steady_clock::now() < deadline) {
				if (numWorkerThreads() <= baseline + 2)
					return;
				sleepFor(ms(50));
			}
			st->Fatalf("goroutine leak: baseline=%d now=%d",
			           {baseline, numWorkerThreads()});
		});
	}
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeNoGoroutineLeak",
                   TestSubscribeNoGoroutineLeak);

// ----- additional coverage -------------------------------------------------

void TestSubscribeDeepNestedCreate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);

		// Create a/b/c one level at a time so the watcher can keep up.
		std::string a = filepathJoin(dir, "a");
		std::string b = filepathJoin(a, "b");
		std::string c = filepathJoin(b, "c");
		for (auto& d : {a, b, c}) {
			must(t, osMkdir(d, 0755));
			sleepFor(ms(150));
		}
		std::string f = filepathJoin(c, "deep.txt");
		must(t, osWriteFile(f, "deep", 0644));
		std::vector<wantEvent> want{{EventKind::EventUpdate, a},
		                          {EventKind::EventUpdate, f}};
		auto got = r->waitForAll(r->deadline(), want);
		for (auto& wl : want) {
			if (!containsEvent(got, wl.kind, wl.path)) {
				t->Fatalf("expected %d for %s, got %d events",
				          {(int)wl.kind, wl.path, (int)got.size()});
			}
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeDeepNestedCreate",
                   TestSubscribeDeepNestedCreate);

void TestSubscribeManyFilesAtOnce(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);

		constexpr int count = 50;
		std::vector<std::string> paths(count);
		for (int i = 0; i < count; i++) {
			paths[i] = subPath(dir);
			must(t, osWriteFile(paths[i], "x", 0644));
		}

		std::vector<wantEvent> want;
		for (auto& p : paths)
			want.push_back({EventKind::EventUpdate, p});
		// Some kernels coalesce dir events under load and miss a few.
		// Retry the missing files up to a couple of times before
		// declaring failure.
		auto got = r->waitForAll(r->deadline(), want);
		for (int attempt = 0; attempt < 3 && !haveAll(got, want);
		     attempt++) {
			for (auto& p : paths) {
				if (!containsEvent(got, EventKind::EventUpdate, p))
					(void)osWriteFile(p, "x", 0644);
			}
			auto more = r->waitForAll(r->deadline(), want);
			got.insert(got.end(), more.begin(), more.end());
		}
		for (auto& p : paths) {
			if (!containsEvent(got, EventKind::EventUpdate, p)) {
				t->Fatalf("missing create for %s (got %d events total)",
				          {p, (int)got.size()});
			}
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeManyFilesAtOnce",
                   TestSubscribeManyFilesAtOnce);

void TestSubscribeTruncateFile(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "hello world", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);
		must(t, osTruncate(f, 0));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeTruncateFile",
                   TestSubscribeTruncateFile);

void TestSubscribeConcurrentSubscribeUnsubscribe(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::atomic<int> done{0};
		for (int i = 0; i < 8; i++) {
			std::thread([&, w] {
				recordingWatcher rec(t);
				auto [sub, err] = w->watchDirectory(dir, rec.callback);
				if (err == nullptr)
					(void)sub->close();
				done.fetch_add(1);
			}).detach();
		}
		while (done.load() < 8)
			sleepFor(ms(1));
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeConcurrentSubscribeUnsubscribe",
                   TestSubscribeConcurrentSubscribeUnsubscribe);

void TestSubscribeRenameDir(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = filepathJoin(dir, "before");
		must(t, osMkdir(subDir, 0755));
		std::string child = filepathJoin(subDir, "file.txt");
		must(t, osWriteFile(child, "x", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);

		std::string after = filepathJoin(dir, "after");
		must(t, osRename(subDir, after));
		std::vector<wantEvent> want{{EventKind::EventUpdate, after},
		                          {EventKind::EventDelete, subDir}};
		auto got = r->waitForAll(r->deadline(), want);
		for (auto& wl : want) {
			if (!containsEvent(got, wl.kind, wl.path)) {
				t->Fatalf("expected %d for %s, got %d events",
				          {(int)wl.kind, wl.path, (int)got.size()});
			}
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeRenameDir",
                   TestSubscribeRenameDir);

void TestSubscribeReplaceFileWithDir(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string target = subPath(dir);
		must(t, osWriteFile(target, "file", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);

		must(t, osRemove(target));
		must(t, osMkdir(target, 0755));
		// Should see at least one event for target (delete and/or update).
		auto got = r->waitForEvent(r->deadline(), [&](const Event& e) {
			return e.path == target;
		});
		if (!containsEvent(got, EventKind::EventDelete, target) &&
		    !containsEvent(got, EventKind::EventUpdate, target)) {
			t->Fatalf("expected events for file-to-dir replacement, got "
			          "%d",
			          {(int)got.size()});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeReplaceFileWithDir",
                   TestSubscribeReplaceFileWithDir);

void TestSubscribeAppendToFile(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "initial", 0644));
		auto [r, sub] = subscribeFor(t, dir, w);

		int fd = ::open(f.c_str(), O_APPEND | O_WRONLY);
		if (fd < 0)
			t->Fatal({osErrno("open")});
		const char* s = " appended";
		(void)::write(fd, s, strlen(s));
		(void)::close(fd);

		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeAppendToFile",
                   TestSubscribeAppendToFile);

void TestSubscribeNoEventsAfterUnsubscribe(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, sub] = subscribeFor(t, dir, w);
		if (auto e = sub->close(); e != nullptr)
			t->Fatal({e});
		// Create a file after closeWatch; should produce nothing.
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "x", 0644));
		auto got = r->drainQuiet(ms(500));
		if (!got.empty()) {
			t->Fatalf("expected no events after closeWatch, got %d",
			          {(int)got.size()});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeNoEventsAfterUnsubscribe",
                   TestSubscribeNoEventsAfterUnsubscribe);

// ----- watcherBase / dirWatchError internals --------------------------------

struct failingBackend : watcherBase {
	gostd::Error err;
	gostd::Error start() override { return err; }
	gostd::Error subscribe(std::shared_ptr<dirWatch>) override {
		return nullptr;
	}
	gostd::Error closeWatch(std::shared_ptr<dirWatch>) override {
		return nullptr;
	}
};

inline failingBackend* newFailingBackend(gostd::Error err) {
	auto* b = new failingBackend();
	b->err = err;
	b->init();
	return b;
}

void TestBackendRunReturnsStartError(T* t) {
	t->Parallel();
	auto want = gostd::newError("startup failed");
	std::unique_ptr<failingBackend> b{newFailingBackend(want)};
	if (auto err = b->run(); !gostd::errorIs(err, want)) {
		t->Fatalf("run() error = %v, want %v", {err, want});
	}
}
REGISTER_UNIT_TEST("fswatch.TestBackendRunReturnsStartError",
                   TestBackendRunReturnsStartError);

void TestDirWatchErrorImplementsError(T* t) {
	t->Parallel();
	gostd::Error err = gostd::Error(std::make_shared<dirWatchErrorObj>(
	    gostd::newError("boom"), nullptr));
	if (err->Error() != "boom") {
		t->Fatalf("dirWatchError.Error want boom, got %q",
		          {err->Error()});
	}
}
REGISTER_UNIT_TEST("fswatch.TestDirWatchErrorImplementsError",
                   TestDirWatchErrorImplementsError);

void TestFileCallbackForwardsErrAlongsideEvents(T* t) {
	t->Parallel();
	realT rt{t};
	std::string target = "/abs/dir/target.txt";
	std::string other = "/abs/dir/sibling.txt";
	auto overflow = gostd::newError("overflow");

	struct call {
		std::vector<Event> events;
		gostd::Error err;
	};
	std::vector<call> got;
	auto dw = newDirectWatcherShared(&rt, "/abs/dir");
	dw->addCallback(
	    "/abs/dir", "/abs/dir", false,
	    [&](std::vector<Event> events, Error err) {
		    got.push_back({std::move(events), err});
	    },
	    nullptr, target);
	auto cb = [&](std::vector<Event> events, Error err) {
		for (auto& e : events) {
			if (e.kind == EventKind::EventDelete)
				dw->events.remove(e.path);
			else
				dw->events.update(e.path);
		}
		if (err != nullptr)
			dw->events.setError(err);
		dw->triggerCallbacks();
	};

	// Plain events: only target events pass through, sibling dropped.
	cb({{EventKind::EventUpdate, target, false},
	    {EventKind::EventUpdate, other, false}},
	   nullptr);
	if (got.size() != 1 || got[0].events.size() != 1 ||
	    got[0].events[0].path != target || got[0].err != nullptr) {
		t->Fatalf("plain delivery: got %d calls", {(int)got.size()});
	}

	// Err only, no matching events: still forwarded with empty slice.
	got.clear();
	cb({{EventKind::EventUpdate, other, false}}, overflow);
	if (got.size() != 1 || !got[0].events.empty() ||
	    !gostd::errorIs(got[0].err, overflow)) {
		t->Fatalf("err-only delivery: got %d calls", {(int)got.size()});
	}

	// Err with matching events: deliver both the filtered events and err.
	got.clear();
	cb({{EventKind::EventDelete, target, false},
	    {EventKind::EventUpdate, other, false}},
	   overflow);
	if (got.size() != 1 || got[0].events.size() != 1 ||
	    got[0].events[0].path != target ||
	    got[0].events[0].kind != EventKind::EventDelete ||
	    !gostd::errorIs(got[0].err, overflow)) {
		t->Fatalf("combined delivery: got %d calls", {(int)got.size()});
	}

	// No events, no err: callback not invoked at all.
	got.clear();
	cb({}, nullptr);
	if (!got.empty()) {
		t->Fatalf("no-op delivery: got %d calls", {(int)got.size()});
	}
}
REGISTER_UNIT_TEST("fswatch.TestFileCallbackForwardsErrAlongsideEvents",
                   TestFileCallbackForwardsErrAlongsideEvents);

// TestRenameDirOutOfTreeNoStaleEvents pins the cross-backend contract:
// once a subdirectory is renamed out of the watched root, modifications
// to files at its new location must not surface against the old paths.
void TestRenameDirOutOfTreeNoStaleEvents(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string watched = newTmpDir(t);
		std::string outside = newTmpDir(t); // separate watch root

		std::string subDir = filepathJoin(watched, "sub");
		std::string inner = filepathJoin(subDir, "inner");
		must(t, osMkdirAll(inner, 0755));
		std::string nested = filepathJoin(inner, "leaf.txt");
		must(t, osWriteFile(nested, "v1", 0644));

		auto [r, s] = subscribeForOpts(t, watched, w, {WithRecursive()});

		std::string dest = filepathJoin(outside, "moved");
		must(t, osRename(subDir, dest));
		(void)r->drainQuiet(ms(500));

		std::string movedNested = filepathJoin(dest, "inner", "leaf.txt");
		must(t, osWriteFile(movedNested, "v2-longer", 0644));

		auto extra = r->drainQuiet(ms(800));
		std::string oldPrefix = subDir + "/";
		for (auto& e : extra) {
			if (e.path == subDir || e.path.rfind(oldPrefix, 0) == 0) {
				t->Fatalf("stale event for moved-out path %s\n",
				          {e.path});
			}
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestRenameDirOutOfTreeNoStaleEvents",
                   TestRenameDirOutOfTreeNoStaleEvents);

// ----- platform-specific ----------------------------------------------------

void TestDefaultBackendMatchesPlatform(T* t) {
	t->Parallel();
	auto* d = Default();
	// runtime.GOOS == "linux":
	std::string wantName =
	    Fanotify()->available() ? "fanotify" : "inotify";
	if (!d->available()) {
		t->Fatalf("Default() should be available on linux", {});
	}
	if (d->name() != wantName) {
		t->Fatalf("Default().Name() = %q, want %q", {d->name(), wantName});
	}
}
REGISTER_UNIT_TEST("fswatch.TestDefaultBackendMatchesPlatform",
                   TestDefaultBackendMatchesPlatform);

void TestUnavailableBackendReturnsError(T* t) {
	t->Parallel();
	realT rt{t};
	Watcher* unavailable = nullptr;
	for (auto* w : AllWatchers()) {
		if (!w->available()) {
			unavailable = w;
			break;
		}
	}
	if (unavailable == nullptr) {
		t->Skip({"all watchers are available on this platform"});
	}
	std::string dir = newTmpDir(&rt);
	auto [_, err] = unavailable->watchDirectory(
	    dir, [](std::vector<Event>, Error) {});
	if (!gostd::errorIs(err, ErrUnavailable)) {
		t->Fatalf("expected ErrUnavailable from %s, got %v",
		          {unavailable->name(), err});
	}
}
REGISTER_UNIT_TEST("fswatch.TestUnavailableBackendReturnsError",
                   TestUnavailableBackendReturnsError);

void TestSubscribeNestedDirDeletionCleansDescendants(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = filepathJoin(dir, "parent");
		std::string nested = filepathJoin(subDir, "child");
		must(t, osMkdirAll(nested, 0755));
		std::string childFile = filepathJoin(nested, "file.txt");
		must(t, osWriteFile(childFile, "x", 0644));

		auto [r, s] = subscribeFor(t, dir, w);

		must(t, osRemoveAll(subDir));

		expectContains(t, r, EventKind::EventDelete, subDir);
	});
}
REGISTER_UNIT_TEST("fswatch.TestSubscribeNestedDirDeletionCleansDescendants",
                   TestSubscribeNestedDirDeletionCleansDescendants);

// ----- non-recursive tests --------------------------------------------------

void TestNonRecursiveFileCreate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, s] = subscribeForOpts(t, dir, w);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "hello", 0644));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestNonRecursiveFileCreate",
                   TestNonRecursiveFileCreate);

void TestNonRecursiveFileUpdate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, s] = subscribeForOpts(t, dir, w);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "v1", 0644));
		(void)r->waitForEvent(r->deadline(),
		                      [](const Event&) { return true; });
		must(t, osWriteFile(f, "v2-longer", 0644));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestNonRecursiveFileUpdate",
                   TestNonRecursiveFileUpdate);

void TestNonRecursiveFileDelete(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = subPath(dir);
		must(t, osWriteFile(f, "x", 0644));
		auto [r, s] = subscribeForOpts(t, dir, w);
		must(t, osRemove(f));
		expectEventSequence(t, r, {{EventKind::EventDelete, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestNonRecursiveFileDelete",
                   TestNonRecursiveFileDelete);

void TestNonRecursiveDirCreate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, s] = subscribeForOpts(t, dir, w);
		std::string subDir = subPath(dir);
		must(t, osMkdir(subDir, 0755));
		expectContains(t, r, EventKind::EventUpdate, subDir);
	});
}
REGISTER_UNIT_TEST("fswatch.TestNonRecursiveDirCreate",
                   TestNonRecursiveDirCreate);

void TestNonRecursiveGrandchildIgnored(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = filepathJoin(dir, "child");
		must(t, osMkdir(subDir, 0755));

		auto [r, s] = subscribeForOpts(t, dir, w);

		std::string grandchild = subPath(subDir);
		must(t, osWriteFile(grandchild, "deep", 0644));

		std::string marker = subPath(dir);
		must(t, osWriteFile(marker, "flush", 0644));

		auto got = expectContains(t, r, EventKind::EventUpdate, marker);
		auto more = r->drainQuiet(2 * maxWaitTime);
		got.insert(got.end(), more.begin(), more.end());
		assertNoEventsForPath(t, got, grandchild,
		                      "expected no events for grandchild");
	});
}
REGISTER_UNIT_TEST("fswatch.TestNonRecursiveGrandchildIgnored",
                   TestNonRecursiveGrandchildIgnored);

void TestNonRecursiveNewSubdirContentIgnored(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		auto [r, s] = subscribeForOpts(t, dir, w);

		std::string subDir = subPath(dir);
		must(t, osMkdir(subDir, 0755));
		expectContains(t, r, EventKind::EventUpdate, subDir);

		std::string grandchild = subPath(subDir);
		must(t, osWriteFile(grandchild, "nested", 0644));

		std::string marker = subPath(dir);
		must(t, osWriteFile(marker, "flush", 0644));

		auto got = expectContains(t, r, EventKind::EventUpdate, marker);
		auto more = r->drainQuiet(2 * maxWaitTime);
		got.insert(got.end(), more.begin(), more.end());
		assertNoEventsForPath(t, got, grandchild,
		                      "expected no events for nested file");
	});
}
REGISTER_UNIT_TEST("fswatch.TestNonRecursiveNewSubdirContentIgnored",
                   TestNonRecursiveNewSubdirContentIgnored);

void TestNonRecursiveAndRecursiveSameDir(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = filepathJoin(dir, "child");
		must(t, osMkdir(subDir, 0755));

		auto [rNonRec, s1] = subscribeForOpts(t, dir, w);
		auto [rRec, s2] =
		    subscribeForOpts(t, dir, w, {WithRecursive()});

		std::string grandchild = subPath(subDir);
		must(t, osWriteFile(grandchild, "deep", 0644));
		std::string marker = subPath(dir);
		must(t, osWriteFile(marker, "flush", 0644));

		expectContains(t, rRec, EventKind::EventUpdate, grandchild);

		auto gotNonRec =
		    expectContains(t, rNonRec, EventKind::EventUpdate, marker);
		auto more = rNonRec->drainQuiet(2 * maxWaitTime);
		gotNonRec.insert(gotNonRec.end(), more.begin(), more.end());
		assertNoEventsForPath(t, gotNonRec, grandchild,
		                      "non-recursive: expected no events for");
	});
}
REGISTER_UNIT_TEST("fswatch.TestNonRecursiveAndRecursiveSameDir",
                   TestNonRecursiveAndRecursiveSameDir);

void TestNonRecursiveWithDeniedSubdir(T* t) {
	t->Parallel();
	// runtime.GOOS == "windows" skip is a no-op on Linux.
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);

		std::string denied = filepathJoin(dir, "denied");
		must(t, osMkdir(denied, 0755));
		must(t, osChmod(denied, 0));
		t->Cleanup([denied] { (void)osChmod(denied, 0700); });

		auto [r, s] = subscribeForOpts(t, dir, w);

		std::string f = subPath(dir);
		must(t, osWriteFile(f, "hello", 0644));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestNonRecursiveWithDeniedSubdir",
                   TestNonRecursiveWithDeniedSubdir);

// ----- file watch tests ------------------------------------------------------

void TestFileWatchCreate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = filepathJoin(dir, "target.txt");
		auto [r, s] = subscribeFileFor(t, f, w);
		must(t, osWriteFile(f, "hello", 0644));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestFileWatchCreate", TestFileWatchCreate);

void TestFileWatchUpdate(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = filepathJoin(dir, "target.txt");
		must(t, osWriteFile(f, "v1", 0644));
		auto [r, s] = subscribeFileFor(t, f, w);
		must(t, osWriteFile(f, "v2-longer", 0644));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestFileWatchUpdate", TestFileWatchUpdate);

void TestFileWatchDelete(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = filepathJoin(dir, "target.txt");
		must(t, osWriteFile(f, "x", 0644));
		auto [r, s] = subscribeFileFor(t, f, w);
		must(t, osRemove(f));
		expectEventSequence(t, r, {{EventKind::EventDelete, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestFileWatchDelete", TestFileWatchDelete);

void TestFileWatchIgnoresSiblings(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string target = filepathJoin(dir, "target.txt");
		std::string sibling = filepathJoin(dir, "sibling.txt");

		auto [r, s] = subscribeFileFor(t, target, w);
		auto [witness, s2] = subscribeForOpts(t, dir, w);

		must(t, osWriteFile(sibling, "noise", 0644));
		expectContains(t, witness, EventKind::EventUpdate, sibling);
		expectNoBufferedEvents(t, r, "expected no events for sibling");
	});
}
REGISTER_UNIT_TEST("fswatch.TestFileWatchIgnoresSiblings",
                   TestFileWatchIgnoresSiblings);

void TestFileWatchMultipleSameDir(T* t) {
	// Not parallel (mirrors Go): multi-WatchFile-share path stays
	// predictable when run serially.
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f1 = filepathJoin(dir, "a.txt");
		std::string f2 = filepathJoin(dir, "b.txt");

		auto [r1, s1] = subscribeFileFor(t, f1, w);
		auto [r2, s2] = subscribeFileFor(t, f2, w);

		must(t, osWriteFile(f1, "hello", 0644));
		auto got1 = r1->next(r1->deadline());
		assertEventSequence(t, got1, {{EventKind::EventUpdate, f1}});

		expectNoBufferedEvents(t, r2, "r2 should not see f1 events");

		must(t, osWriteFile(f2, "world", 0644));
		auto got2 = r2->next(r2->deadline());
		assertEventSequence(t, got2, {{EventKind::EventUpdate, f2}});

		expectNoBufferedEvents(t, r1, "r1 should not see f2 events");
	});
}
REGISTER_UNIT_TEST("fswatch.TestFileWatchMultipleSameDir",
                   TestFileWatchMultipleSameDir);

void TestFileWatchDeleteAndRecreate(T* t) {
	// Not parallel (mirrors Go).
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = filepathJoin(dir, "config.json");
		must(t, osWriteFile(f, "{\"v\":1}", 0644));

		auto [r, s] = subscribeFileFor(t, f, w);

		must(t, osRemove(f));
		expectEventSequence(t, r, {{EventKind::EventDelete, f}});

		must(t, osWriteFile(f, "{\"v\":2}", 0644));
		expectContains(t, r, EventKind::EventUpdate, f);
	});
}
REGISTER_UNIT_TEST("fswatch.TestFileWatchDeleteAndRecreate",
                   TestFileWatchDeleteAndRecreate);

void TestFileWatchNonExistentTarget(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string f = filepathJoin(dir, "doesnotexist.txt");

		// File doesn't exist; subscribe should still succeed (watches
		// the parent dir).
		auto [r, s] = subscribeFileFor(t, f, w);

		must(t, osWriteFile(f, "appeared", 0644));
		expectEventSequence(t, r, {{EventKind::EventUpdate, f}});
	});
}
REGISTER_UNIT_TEST("fswatch.TestFileWatchNonExistentTarget",
                   TestFileWatchNonExistentTarget);

// TestRecursiveMoveInPrePopulated verifies that moving a pre-populated
// directory tree into a recursive watch detects changes in nested subdirs.
void TestRecursiveMoveInPrePopulated(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string outside = newTmpDir(t);

		std::string nested =
		    filepathJoin(outside, "a", "b", "c");
		must(t, osMkdirAll(nested, 0755));

		auto [r, s] = subscribeForOpts(t, dir, w, {WithRecursive()});

		std::string dest = filepathJoin(dir, "tree");
		must(t, osRename(outside, dest));
		(void)r->drainQuiet(ms(500));

		std::string nestedDir = filepathJoin(dest, "a", "b", "c");
		auto deadline = std::chrono::steady_clock::now() + r->deadline();
		std::vector<Event> allSeen;
		for (int attempt = 0;
		     std::chrono::steady_clock::now() < deadline; attempt++) {
			std::string f = filepathJoin(
			    nestedDir,
			    gostd::sprintf("deep-%d.txt", {attempt}));
			must(t, osWriteFile(f, "hello", 0644));
			auto more = r->waitForEvent(ms(750), [&](const Event& e) {
				return e.kind == EventKind::EventUpdate &&
				       e.path.rfind(nestedDir + "/", 0) == 0;
			});
			allSeen.insert(allSeen.end(), more.begin(), more.end());
			for (auto& e : more) {
				if (e.kind == EventKind::EventUpdate &&
				    e.path.rfind(nestedDir + "/", 0) == 0) {
					return;
				}
			}
		}
		t->Fatalf("expected update for a file inside moved-in tree, "
		          "got %d events",
		          {(int)allSeen.size()});
	});
}
REGISTER_UNIT_TEST("fswatch.TestRecursiveMoveInPrePopulated",
                   TestRecursiveMoveInPrePopulated);

// TestAtomicSave verifies that the "safe save" pattern (write tmp,
// rename over target) is detected as an update.
void TestAtomicSave(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string target = filepathJoin(dir, "config.json");
		must(t, osWriteFile(target, "{\"v\":1}", 0644));
		auto [r, s] = subscribeFor(t, dir, w);

		std::string tmp = target + ".tmp";
		must(t, osWriteFile(tmp, "{\"v\":2}", 0644));
		must(t, osRename(tmp, target));
		auto got = r->waitForEvent(r->deadline(), [&](const Event& e) {
			return e.path == target;
		});
		got = filterEventsForPaths(got, {target});
		if (got.empty()) {
			t->Fatalf("expected events for %s after atomic save, got "
			          "none",
			          {target});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestAtomicSave", TestAtomicSave);

void TestAtomicSaveFileWatch(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string target = filepathJoin(dir, "target.txt");
		must(t, osWriteFile(target, "v1", 0644));
		auto [r, s] = subscribeFileFor(t, target, w);

		std::string tmp = target + ".tmp";
		must(t, osWriteFile(tmp, "v2", 0644));
		must(t, osRename(tmp, target));
		auto got = r->waitForEvent(r->deadline(), [&](const Event& e) {
			return e.path == target;
		});
		got = filterEventsForPaths(got, {target});
		if (got.empty()) {
			t->Fatalf("expected events for %s after atomic save, got "
			          "none",
			          {target});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestAtomicSaveFileWatch",
                   TestAtomicSaveFileWatch);

void TestReplaceDirWithFile(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string child = filepathJoin(dir, "child");
		must(t, osMkdir(child, 0755));
		auto [r, s] = subscribeFor(t, dir, w);

		must(t, osRemove(child));
		must(t, osWriteFile(child, "now a file", 0644));

		auto got = r->waitForEvent(r->deadline(), [&](const Event& e) {
			return e.path == child;
		});
		got = filterEventsForPaths(got, {child});
		if (got.empty()) {
			t->Fatalf("expected events for dir->file replacement at %s, "
			          "got none",
			          {child});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestReplaceDirWithFile",
                   TestReplaceDirWithFile);

void TestRecreateSubdirAndModify(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = filepathJoin(dir, "sub");
		must(t, osMkdir(subDir, 0755));
		std::string inner = filepathJoin(subDir, "file.txt");
		must(t, osWriteFile(inner, "v1", 0644));
		auto [r, s] = subscribeFor(t, dir, w);

		must(t, osRemoveAll(subDir));
		(void)r->drainQuiet(ms(500));

		must(t, osMkdir(subDir, 0755));

		// Give the backend a chance to install its watch on the new sub
		// inode BEFORE we write inside it.
		sleepFor(ms(150));

		auto deadline =
		    std::chrono::steady_clock::now() + r->deadline() * 2;
		std::vector<Event> allSeen;
		for (int attempt = 0;
		     std::chrono::steady_clock::now() < deadline; attempt++) {
			std::string f = filepathJoin(
			    subDir, gostd::sprintf("attempt-%d.txt", {attempt}));
			must(t, osWriteFile(f, "hi", 0644));
			auto more = r->waitForEvent(ms(750), [&](const Event& e) {
				return e.kind == EventKind::EventUpdate &&
				       e.path.rfind(subDir + "/", 0) == 0;
			});
			allSeen.insert(allSeen.end(), more.begin(), more.end());
			for (auto& e : more) {
				if (e.kind == EventKind::EventUpdate &&
				    e.path.rfind(subDir + "/", 0) == 0) {
					return;
				}
			}
		}
		t->Fatalf("expected update for a file inside recreated sub, "
		          "got %d events",
		          {(int)allSeen.size()});
	});
}
REGISTER_UNIT_TEST("fswatch.TestRecreateSubdirAndModify",
                   TestRecreateSubdirAndModify);

void TestReplaceParentDirWithDifferent(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string subDir = filepathJoin(dir, "pkg");
		must(t, osMkdirAll(filepathJoin(subDir, "old"), 0755));
		must(t, osWriteFile(filepathJoin(subDir, "old", "a.txt"), "a",
		                    0644));
		auto [r, s] = subscribeFor(t, dir, w);

		must(t, osRemoveAll(subDir));
		must(t, osMkdirAll(filepathJoin(subDir, "new"), 0755));
		must(t, osWriteFile(filepathJoin(subDir, "new", "b.txt"), "b",
		                    0644));
		(void)r->drainQuiet(ms(500));

		std::string newDir = filepathJoin(subDir, "new");
		auto deadline = std::chrono::steady_clock::now() + r->deadline();
		std::vector<Event> allSeen;
		for (int attempt = 0;
		     std::chrono::steady_clock::now() < deadline; attempt++) {
			std::string f = filepathJoin(
			    newDir, gostd::sprintf("attempt-%d.txt", {attempt}));
			must(t, osWriteFile(f, "hi", 0644));
			auto more = r->waitForEvent(ms(750), [&](const Event& e) {
				return e.kind == EventKind::EventUpdate &&
				       e.path.rfind(newDir + "/", 0) == 0;
			});
			allSeen.insert(allSeen.end(), more.begin(), more.end());
			for (auto& e : more) {
				if (e.kind == EventKind::EventUpdate &&
				    e.path.rfind(newDir + "/", 0) == 0) {
					return;
				}
			}
		}
		t->Fatalf("expected update for a file inside replaced tree, "
		          "got %d events",
		          {(int)allSeen.size()});
	});
}
REGISTER_UNIT_TEST("fswatch.TestReplaceParentDirWithDifferent",
                   TestReplaceParentDirWithDifferent);

// TestRoundTripRename renames a file away and back within a short window.
void TestRoundTripRename(T* t) {
	t->Parallel();
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		if (w == Kqueue()) {
			t->Skip({"kqueue fd-based tracking delivers stale delete "
			         "before parent NOTE_WRITE reconciles"});
		}
		std::string dir = newTmpDir(t);
		std::string orig = filepathJoin(dir, "data.txt");
		must(t, osWriteFile(orig, "content", 0644));
		auto [r, s] = subscribeFor(t, dir, w);

		std::string tmp = filepathJoin(dir, "data.txt.bak");
		must(t, osRename(orig, tmp));
		must(t, osRename(tmp, orig));
		// Either some events or zero events (coalesced to no-op) are
		// acceptable; a transient delete is only OK if a subsequent
		// update follows.
		auto got = r->gatherUntilQuiet(r->deadline(), ms(500));
		got = filterEventsForPaths(got, {orig});
		bool hasDelete = containsEvent(got, EventKind::EventDelete, orig);
		bool hasUpdate = containsEvent(got, EventKind::EventUpdate, orig);
		if (hasDelete && !hasUpdate) {
			t->Fatalf("round-trip rename left a stale delete without "
			          "recovery for %s",
			          {orig});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestRoundTripRename", TestRoundTripRename);

void TestRecursiveWithDeniedSubdir(T* t) {
	t->Parallel();
	// runtime.GOOS == "windows" skip is a no-op on Linux.
	runForEachWatcher(t, [](TestI* t, Watcher* w) {
		std::string dir = newTmpDir(t);
		std::string accessible = filepathJoin(dir, "ok");
		must(t, osMkdir(accessible, 0755));
		std::string denied = filepathJoin(dir, "denied");
		must(t, osMkdir(denied, 0755));
		must(t, osChmod(denied, 0));
		t->Cleanup([denied] { (void)osChmod(denied, 0700); });

		auto [r, s] = subscribeFor(t, dir, w);

		std::string f = filepathJoin(accessible, "test.txt");
		must(t, osWriteFile(f, "hello", 0644));
		expectContains(t, r, EventKind::EventUpdate, f);
	});
}
REGISTER_UNIT_TEST("fswatch.TestRecursiveWithDeniedSubdir",
                   TestRecursiveWithDeniedSubdir);

} // namespace
} // namespace tsc::fswatch
