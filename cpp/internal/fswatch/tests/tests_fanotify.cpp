// Port of fanotify_linux_test.go — Linux fanotify backend internals.

#include <fcntl.h>
#include <sys/statfs.h>
#include <sys/vfs.h>

#include "internal/fswatch/tests/util.h"

namespace tsc::fswatch {

namespace {

// unix.NameToHandleAt(AT_FDCWD, dir, 0) → (handleType, handleBytes).
// Returns the mount id (unused) implicitly discarded.
inline std::tuple<int32_t, std::string, Error>
nameToHandleAt(const std::string& path) {
	auto* fh = static_cast<struct file_handle*>(
	    std::malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ));
	if (fh == nullptr)
		return {0, "", gostd::newError("alloc file_handle")};
	fh->handle_bytes = MAX_HANDLE_SZ;
	int mountId = 0;
	if (::name_to_handle_at(AT_FDCWD, path.c_str(), fh, &mountId, 0) != 0) {
		auto err = osErrno("name_to_handle_at");
		std::free(fh);
		return {0, "", err};
	}
	std::tuple<int32_t, std::string, Error> out{
	    fh->handle_type,
	    std::string(reinterpret_cast<char*>(fh->f_handle),
	                fh->handle_bytes),
	    nullptr};
	std::free(fh);
	return out;
}

void TestLinuxFanotifyShutdownBeforeStart(T* t) {
	t->Parallel();
	newFanotifyBackend(false)->shutdown();
}
REGISTER_UNIT_TEST("fswatch.TestLinuxFanotifyShutdownBeforeStart",
                   TestLinuxFanotifyShutdownBeforeStart);

void TestLinuxFanotifyBackendSelection(T* t) {
	t->Parallel();
	if (!fanotifyAvailable()) {
		t->Skip({"fanotify not available"});
	}
	auto [impl, err] = fanotifyWatcher().getImpl();
	if (err != nullptr) {
		t->Fatal({err});
	}
	if (dynamic_cast<fanotifyBackend*>(impl) == nullptr) {
		t->Fatal({"fanotify watcher want *fanotifyBackend"});
	}
}
REGISTER_UNIT_TEST("fswatch.TestLinuxFanotifyBackendSelection",
                   TestLinuxFanotifyBackendSelection);

void TestLinuxFanotifySubscribeCleansUpAfterMarkFailure(T* t) {
	t->Parallel();
	realT rt{t};
	std::string dir = newTmpDir(&rt);
	auto w = newDirectWatcherShared(&rt, dir);
	std::unique_ptr<fanotifyBackend> b{newFanotifyBackend(false)};

	auto err = b->subscribe(w);
	auto werr = gostd::errorAs<dirWatchErrorObj*>(err);
	if (werr == nullptr) {
		t->Fatalf("subscribe error = %v, want *dirWatchError", {err});
	}
	if (werr->dirWatch.get() != w.get()) {
		t->Fatal({"dirWatchError dirWatch mismatch"});
	}
	if (!b->subscriptions.empty()) {
		t->Fatalf("subscriptions not cleaned up: %d remaining",
		          {(int)b->subscriptions.size()});
	}
}
REGISTER_UNIT_TEST(
    "fswatch.TestLinuxFanotifySubscribeCleansUpAfterMarkFailure",
    TestLinuxFanotifySubscribeCleansUpAfterMarkFailure);

void TestLinuxFanotifyParseDfidNameRoundTrip(T* t) {
	t->Parallel();
	realT rt{t};
	std::string dir = newTmpDir(&rt);
	auto [htype, hbytes, herr] = nameToHandleAt(dir);
	if (herr != nullptr) {
		t->Skipf("NameToHandleAt not supported: %v", {herr});
	}
	struct statfs st;
	if (::statfs(dir.c_str(), &st) != 0)
		t->Fatal({osErrno("statfs")});
	std::array<int32_t, 2> fsid{(int32_t)st.f_fsid.__val[0],
	                            (int32_t)st.f_fsid.__val[1]};
	auto key = makeFanotifyHandleKey(fsid, htype, hbytes);
	if (key.handle.empty()) {
		t->Fatal({"empty handle bytes"});
	}
	auto [htype2, hbytes2, herr2] = nameToHandleAt(dir);
	if (herr2 != nullptr) {
		t->Fatal({herr2});
	}
	auto key2 = makeFanotifyHandleKey(fsid, htype2, hbytes2);
	if (!(key == key2)) {
		t->Fatalf("handle keys differ for same path: %d vs %d bytes",
		          {(int)key.handle.size(), (int)key2.handle.size()});
	}
}
REGISTER_UNIT_TEST("fswatch.TestLinuxFanotifyParseDfidNameRoundTrip",
                   TestLinuxFanotifyParseDfidNameRoundTrip);

void TestFanotifyCrossWatcherSameFs(T* t) {
	t->Parallel();
	if (!fanotifyAvailable()) {
		t->Skip({"fanotify not available"});
	}

	t->Run("Modify", [](T* st) {
		st->Parallel();
		realT rt{st};
		std::string dirA = newTmpDir(&rt), dirB = newTmpDir(&rt);
		std::string pathA = filepathJoin(dirA, "child");
		std::string pathB = filepathJoin(dirB, "child");
		for (auto& p : {pathA, pathB}) {
			if (auto err = osWriteFile(p, "initial", 0644); err != nullptr)
				st->Fatal({err});
		}
		auto [rA, sA] = subscribeFor(&rt, dirA, Fanotify());
		auto [rB, sB] = subscribeFor(&rt, dirB, Fanotify());

		if (auto err = osWriteFile(pathA, "changed", 0644); err != nullptr)
			st->Fatal({err});
		auto gotA = rA->gather(rA->deadline(), ms(200));
		assertEventSet(&rt, gotA, {{EventKind::EventUpdate, pathA}});

		if (auto gotB = rB->drainQuiet(ms(200)); !gotB.empty()) {
			st->Fatalf("watcher B got phantom events: %d",
			           {(int)gotB.size()});
		}
	});
}
REGISTER_UNIT_TEST("fswatch.TestFanotifyCrossWatcherSameFs",
                   TestFanotifyCrossWatcherSameFs);

void TestLinuxFanotifyMaybeWrapUnsupportedFilesystem(T* t) {
	t->Parallel();

	// Errnos that indicate the filesystem cannot support fanotify FID-based
	// watching are tagged with ErrFilesystemUnsupported so higher layers
	// can fall back to inotify (issue #63646).
	for (int en : {EOPNOTSUPP, ENODEV}) {
		auto wrapped = maybeWrapUnsupportedFilesystem(
		    gostd::errorf("name_to_handle_at: %w", {errnoError(en)}));
		if (!gostd::errorIs(wrapped, ErrFilesystemUnsupported)) {
			t->Errorf("expected errno %d to be tagged "
			          "ErrFilesystemUnsupported",
			          {en});
		}
		if (!errnoIs(wrapped, en)) {
			t->Errorf("expected wrapped error to still unwrap to %d", {en});
		}
	}

	// Unrelated errnos are returned unchanged.
	auto other = maybeWrapUnsupportedFilesystem(errnoError(EACCES));
	if (gostd::errorIs(other, ErrFilesystemUnsupported)) {
		t->Errorf("EACCES should not be tagged ErrFilesystemUnsupported",
		          {});
	}
}
REGISTER_UNIT_TEST(
    "fswatch.TestLinuxFanotifyMaybeWrapUnsupportedFilesystem",
    TestLinuxFanotifyMaybeWrapUnsupportedFilesystem);

void TestLinuxFanotifyMarkENODEVTagged(T* t) {
	t->Parallel();

	// On NTFS mounted via fuseblk, fanotify_mark itself fails with ENODEV
	// ("no such device") — the bare errno, with no name_to_handle_at
	// wrapping (issue #63678). markDir passes that straight to
	// maybeWrapUnsupportedFilesystem, so it must be tagged and drive the
	// inotify fallback just like the EOPNOTSUPP case.
	auto err = maybeWrapUnsupportedFilesystem(errnoError(ENODEV));
	if (!gostd::errorIs(err, ErrFilesystemUnsupported)) {
		t->Error({"bare ENODEV from fanotify_mark should be tagged "
		          "ErrFilesystemUnsupported"});
	}
	if (!errnoIs(err, ENODEV)) {
		t->Error({"tagged error should still unwrap to ENODEV"});
	}
}
REGISTER_UNIT_TEST("fswatch.TestLinuxFanotifyMarkENODEVTagged",
                   TestLinuxFanotifyMarkENODEVTagged);

void TestLinuxFanotifyUnsupportedTagSurvivesDirWatchError(T* t) {
	t->Parallel();

	// Closes the loop between maybeWrapUnsupportedFilesystem and the
	// higher layers: the tag must survive the exact wrapping that markDir
	// + subscribe apply (fmt.Errorf with %w, then dirWatchError) so that
	// errors.Is still finds ErrFilesystemUnsupported at the
	// WatchDirectories boundary.
	auto inner = maybeWrapUnsupportedFilesystem(
	    gostd::errorf("name_to_handle_at: %w", {errnoError(EOPNOTSUPP)}));
	gostd::Error subErr = gostd::Error(std::make_shared<dirWatchErrorObj>(
	    gostd::errorf("fanotify_mark on '%s' failed: %w", {"/x", inner}),
	    nullptr));

	if (!gostd::errorIs(subErr, ErrFilesystemUnsupported)) {
		t->Error({"ErrFilesystemUnsupported did not survive dirWatchError "
		          "wrapping"});
	}
	if (!errnoIs(subErr, EOPNOTSUPP)) {
		t->Error({"underlying errno did not survive dirWatchError "
		          "wrapping"});
	}
}
REGISTER_UNIT_TEST(
    "fswatch.TestLinuxFanotifyUnsupportedTagSurvivesDirWatchError",
    TestLinuxFanotifyUnsupportedTagSurvivesDirWatchError);

} // namespace
} // namespace tsc::fswatch
