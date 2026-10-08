// Port of tsc/internal/project/overlayfs_test.go.
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "internal/gostd/testing.h"
#include "internal/project/filechange.h"
#include "internal/project/overlayfs.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
using tsc::gostd::testing::T;

// Helper to create test overlayFS.
project::overlayFS* createOverlayFS() {
	// newOverlayFS stores a raw host pointer; the test FS is intentionally
	// leaked so it outlives the overlayFS for the test's duration.
	auto* testFS = new std::shared_ptr<tsc::vfs::FS>(
	    tsc::vfs::vfstest::FromMap(
	        std::unordered_map<std::string, tsc::vfs::vfstest::MapFileInput>{
	            {"/test1.ts", std::string("// existing content")},
	            {"/test2.ts", std::string("// existing content")},
	            {"/script", std::string("// extensionless content")},
	        },
	        false /* useCaseSensitiveFileNames */));
	return project::newOverlayFS(
	    testFS->get(),
	    std::unordered_map<tsc::tspath::Path, project::Overlay*>{},
	    lsproto::PositionEncodingKindUTF16,
	    [](const std::string& fileName) -> tsc::tspath::Path {
		    return tsc::tspath::Path(fileName);
	    });
}

void TestProcessChanges(T* t) {
	t->Parallel();

	// Test URI constants
	const lsproto::DocumentUri testURI1 = "file:///test1.ts";
	const lsproto::DocumentUri testURI2 = "file:///test2.ts";

	t->Run("multiple opens should panic", [&](T* t) {
		t->Parallel();
		auto* fs = createOverlayFS();

		std::vector<project::FileChange> changes{
		    {
		        .Kind = project::FileChangeKindOpen,
		        .URI = testURI1,
		        .Version = 1,
		        .Content = "const x = 1;",
		        .LanguageKind = lsproto::LanguageKindTypeScript,
		    },
		    {
		        .Kind = project::FileChangeKindOpen,
		        .URI = testURI2,
		        .Version = 1,
		        .Content = "const y = 2;",
		        .LanguageKind = lsproto::LanguageKindTypeScript,
		    },
		};

		// Go panics are untrappable in the C++ port: tscUnreachable exits
		// with code 2, matching `go test`'s panic contract. Fork a child to
		// observe the death.
		std::fflush(nullptr);
		pid_t pid = fork();
		bool panicked;
		if (pid == 0) {
			fs->processChanges(changes);
			_exit(0);
		}
		int status = 0;
		waitpid(pid, &status, 0);
		panicked = !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		assert::Assert(t, panicked);
	});

	t->Run("watch create then delete becomes nothing", [&](T* t) {
		t->Parallel();
		auto* fs = createOverlayFS();

		std::vector<project::FileChange> changes{
		    {.Kind = project::FileChangeKindWatchCreate, .URI = testURI1},
		    {.Kind = project::FileChangeKindWatchDelete, .URI = testURI1},
		};

		auto [result, _] = fs->processChanges(changes);
		assert::Assert(t, result.IsEmpty());
	});

	t->Run("watch delete then create becomes change", [&](T* t) {
		t->Parallel();
		auto* fs = createOverlayFS();

		std::vector<project::FileChange> changes{
		    {.Kind = project::FileChangeKindWatchDelete, .URI = testURI1},
		    {.Kind = project::FileChangeKindWatchCreate, .URI = testURI1},
		};

		auto [result, _] = fs->processChanges(changes);

		assert::Equal(t, result.Created.Len(), 0);
		assert::Equal(t, result.Deleted.Len(), 0);
		assert::Assert(t, result.Changed.Has(testURI1));
	});

	t->Run("multiple watch changes deduplicated", [&](T* t) {
		t->Parallel();
		auto* fs = createOverlayFS();

		std::vector<project::FileChange> changes{
		    {.Kind = project::FileChangeKindWatchChange, .URI = testURI1},
		    {.Kind = project::FileChangeKindWatchChange, .URI = testURI1},
		    {.Kind = project::FileChangeKindWatchChange, .URI = testURI1},
		};

		auto [result, _] = fs->processChanges(changes);

		assert::Assert(t, result.Changed.Has(testURI1));
		assert::Equal(t, result.Changed.Len(), 1);
	});

	t->Run("save marks overlay as matching disk", [&](T* t) {
		t->Parallel();
		auto* fs = createOverlayFS();

		// First create an overlay
		fs->processChanges(std::vector<project::FileChange>{
		    {
		        .Kind = project::FileChangeKindOpen,
		        .URI = testURI1,
		        .Version = 1,
		        .Content = "const x = 1;",
		        .LanguageKind = lsproto::LanguageKindTypeScript,
		    },
		});
		// Then save
		auto [result, _] = fs->processChanges(
		    std::vector<project::FileChange>{
		        {
		            .Kind = project::FileChangeKindSave,
		            .URI = testURI1,
		        },
		    });
		// We don't observe saves for snapshot changes,
		// so they're not included in the summary
		assert::Assert(t, result.IsEmpty());

		// Check that the overlay is marked as matching disk text
		auto* fh = fs->GetFile(lsproto::documentUriFileName(testURI1));
		assert::Assert(t, fh != nullptr);
		assert::Assert(t, fh->MatchesDiskText());
	});

	t->Run("open falls back to file extension for unknown language kind",
	       [](T* t) {
		       t->Parallel();
		       auto* fs = createOverlayFS();
		       lsproto::DocumentUri uri = "file:///test1.mts";

		       fs->processChanges(std::vector<project::FileChange>{
		           {
		               .Kind = project::FileChangeKindOpen,
		               .URI = uri,
		               .Version = 1,
		               .Content = "export const x = 1;",
		               .LanguageKind = lsproto::LanguageKind("mts"),
		           },
		       });

		       auto* fh = fs->GetFile(lsproto::documentUriFileName(uri));
		       assert::Assert(t, fh != nullptr);
		       assert::Equal(t, fh->Kind(), tsc::ScriptKind::TS);
	       });

	t->Run("open extensionless file preserves unknown script kind",
	       [](T* t) {
		       t->Parallel();
		       auto* fs = createOverlayFS();
		       lsproto::DocumentUri uri = "file:///script";

		       fs->processChanges(std::vector<project::FileChange>{
		           {
		               .Kind = project::FileChangeKindOpen,
		               .URI = uri,
		               .Version = 1,
		               .Content = "const x = 1;",
		               .LanguageKind = lsproto::LanguageKind("plaintext"),
		           },
		       });

		       auto* fh = fs->GetFile(lsproto::documentUriFileName(uri));
		       assert::Assert(t, fh != nullptr);
		       assert::Equal(t, fh->Kind(), tsc::ScriptKind::Unknown);
	       });

	t->Run("extensionless disk file preserves unknown script kind",
	       [](T* t) {
		       t->Parallel();
		       auto* fs = createOverlayFS();

		       auto* fh = fs->GetFile("/script");
		       assert::Assert(t, fh != nullptr);
		       assert::Equal(t, fh->Kind(), tsc::ScriptKind::Unknown);
	       });

	t->Run("watch change on overlay marks as not matching disk", [&](T* t) {
		t->Parallel();
		auto* fs = createOverlayFS();

		// First create an overlay
		fs->processChanges(std::vector<project::FileChange>{
		    {
		        .Kind = project::FileChangeKindOpen,
		        .URI = testURI1,
		        .Version = 1,
		        .Content = "const x = 1;",
		        .LanguageKind = lsproto::LanguageKindTypeScript,
		    },
		});
		assert::Assert(
		    t,
		    !fs->GetFile(lsproto::documentUriFileName(testURI1))
		         ->MatchesDiskText());

		// Then save
		fs->processChanges(std::vector<project::FileChange>{
		    {
		        .Kind = project::FileChangeKindSave,
		        .URI = testURI1,
		    },
		});
		assert::Assert(
		    t, fs->GetFile(lsproto::documentUriFileName(testURI1))
		           ->MatchesDiskText());

		// Now process a watch change
		fs->processChanges(std::vector<project::FileChange>{
		    {
		        .Kind = project::FileChangeKindWatchChange,
		        .URI = testURI1,
		    },
		});
		assert::Assert(
		    t,
		    !fs->GetFile(lsproto::documentUriFileName(testURI1))
		         ->MatchesDiskText());
	});

	t->Run("save without overlay should not panic", [&](T* t) {
		t->Parallel();
		auto* fs = createOverlayFS();

		// Save a file that was never opened (no overlay exists).
		// This can happen when an editor sends didSave for a file
		// that is not managed by the LSP server (e.g., package.json).
		auto [result, _] = fs->processChanges(
		    std::vector<project::FileChange>{
		        {
		            .Kind = project::FileChangeKindSave,
		            .URI = testURI1,
		        },
		    });
		// Should be treated as a disk change
		assert::Assert(t, result.Changed.Has(testURI1));
	});

	t->Run("close and change without overlay should not panic", [&](T* t) {
		t->Parallel();
		auto* fs = createOverlayFS();

		fs->processChanges(std::vector<project::FileChange>{
		    {
		        .Kind = project::FileChangeKindOpen,
		        .URI = testURI1,
		        .Version = 1,
		        .Content = "const x = 1;",
		        .LanguageKind = lsproto::LanguageKindTypeScript,
		    },
		});
		fs->processChanges(std::vector<project::FileChange>{
		    {
		        .Kind = project::FileChangeKindClose,
		        .URI = testURI1,
		    },
		});

		auto [result, _] = fs->processChanges(
		    std::vector<project::FileChange>{
		        {
		            .Kind = project::FileChangeKindClose,
		            .URI = testURI1,
		        },
		    });

		assert::Assert(t, result.IsEmpty());

		lsproto::TextDocumentContentChangePartialOrWholeDocument wholeDoc;
		wholeDoc.WholeDocument = std::make_shared<
		    lsproto::TextDocumentContentChangeWholeDocument>();
		wholeDoc.WholeDocument->Text = "const x = 1;";
		auto [result2, _2] = fs->processChanges(
		    std::vector<project::FileChange>{
		        {
		            .Kind = project::FileChangeKindChange,
		            .URI = testURI1,
		            .Version = 2,
		            .Changes = {wholeDoc},
		        },
		    });

		assert::Assert(t, result2.IsEmpty());
	});

	t->Run("close then open in same batch marks as changed", [&](T* t) {
		t->Parallel();
		auto* fs = createOverlayFS();

		// First create an overlay
		fs->processChanges(std::vector<project::FileChange>{
		    {
		        .Kind = project::FileChangeKindOpen,
		        .URI = testURI1,
		        .Version = 1,
		        .Content = "const x = 1;",
		        .LanguageKind = lsproto::LanguageKindTypeScript,
		    },
		});

		// Now close and reopen in the same batch (like Neovim does for file
		// reload)
		auto [result, _] = fs->processChanges(
		    std::vector<project::FileChange>{
		        {
		            .Kind = project::FileChangeKindClose,
		            .URI = testURI1,
		        },
		        {
		            .Kind = project::FileChangeKindOpen,
		            .URI = testURI1,
		            .Version = 0,
		            .Content = "const x = 2;",
		            .LanguageKind = lsproto::LanguageKindTypeScript,
		        },
		    });

		// Should not be marked as opened since it was already open
		assert::Assert(t, result.Opened.empty(),
		               "close then open should not mark as opened");
		// Should also be marked as changed since it was closed and reopened
		assert::Assert(t, result.Changed.Has(testURI1),
		               "close then open should mark as changed");
		// Should have the new content
		auto* fh = fs->GetFile(lsproto::documentUriFileName(testURI1));
		assert::Equal(t, fh->Content(), std::string("const x = 2;"));
	});
}

void TestOverlayFSFileSystem(T* t) {
	t->Parallel();
	auto host = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string, tsc::vfs::vfstest::MapFileInput>{
	        {"/virtual", std::string("host file")},
	    },
	    false /* useCaseSensitiveFileNames */);
	auto toPath = [](const std::string& fileName) -> tsc::tspath::Path {
		return tsc::tspath::Path(fileName);
	};
	std::unordered_map<tsc::tspath::Path, project::Overlay*> overlays{
	    {tsc::tspath::Path("/virtual/nested/file.ts"),
	     project::newOverlay("/virtual/nested/file.ts", "overlay", 1,
	                         tsc::ScriptKind::TS)},
	};
	auto* fileSystem = project::newOverlayFS(
	    host.get(), std::move(overlays), lsproto::PositionEncodingKindUTF16,
	    toPath);

	assert::Assert(t, fileSystem->DirectoryExists("/virtual"));
	assert::Assert(t, !fileSystem->FileExists("/virtual"));
	assert::Assert(t, fileSystem->Stat("/virtual")->IsDir());
	auto [content, ok] = fileSystem->ReadFile("/virtual/nested/file.ts");
	assert::Assert(t, ok);
	assert::Equal(t, content, std::string("overlay"));

	auto rootEntries = fileSystem->GetAccessibleEntries("/");
	assert::Assert(t, std::find(rootEntries.directories.begin(),
	                            rootEntries.directories.end(),
	                            "virtual") != rootEntries.directories.end());
	assert::Assert(t, std::find(rootEntries.files.begin(),
	                            rootEntries.files.end(),
	                            "virtual") == rootEntries.files.end());
}

}  // namespace

REGISTER_UNIT_TEST("project.TestProcessChanges", TestProcessChanges);
REGISTER_UNIT_TEST("project.TestOverlayFSFileSystem", TestOverlayFSFileSystem);
