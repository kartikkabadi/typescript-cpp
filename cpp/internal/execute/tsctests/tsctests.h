// Declarations for tsc/internal/execute/tsctests — the non-test support
// code for the tscbuild/tscwatch end-to-end test harness. Mirrors
// sys.go / fs.go / mock_watch_backend.go / readablebuildinfo.go / runner.go.
#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/core/version.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/execute/tsc/compile.h"
#include "internal/execute/watchmanager/watchbackend.h"
#include "internal/fswatch/fswatch.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/locale/locale.h"
#include "internal/testutil/fsbaselineutil/fsbaselineutil.h"
#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/iovfs/iovfs.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace etsc = ::tsc::execute::tsc;

namespace tsc::execute::tsctests {

// FileMap — sys.go:30. map[string]any: string | []byte | *fstest.MapFile.
using FileMap = std::unordered_map<std::string, vfs::vfstest::MapFileInput>;

// stringWriter — Go strings.Builder when it must be an io.Writer AND a
// std::ostream. `s` is the buffer; WriteString/String/Reset/Len mirror
// Builder; operator<< appends so it can be handed out as std::ostream.
struct stringWriter : gostd::io::Writer, public std::ostream {
	struct buf : std::streambuf {
		std::string* s;
		explicit buf(std::string* str) : s(str) {}
		std::streamsize xsputn(const char* data,
		                       std::streamsize n) override {
			s->append(data, size_t(n));
			return n;
		}
		int_type overflow(int_type c) override {
			if (c != traits_type::eof()) *s += char(c);
			return c;
		}
	};
	std::string s;
	buf b{&s};
	stringWriter() : std::ostream(&b) {}

	// io.Writer — Go (Builder).Write.
	std::pair<int, gostd::Error> write(std::string_view data) override {
		s += std::string(data);
		return {int(data.size()), nullptr};
	}
	void WriteString(const std::string& str) { s += str; }
	const std::string& String() const { return s; }
	void Reset() { s.clear(); }
	int Len() const { return int(s.size()); }
};

// TestClock — sys.go:69.
struct TestClock : vfs::vfstest::Clock {
	vfs::TimePoint start;
	vfs::TimePoint now;
	std::mutex nowMu;

	vfs::TimePoint Now() override;
	vfs::Duration SinceStart() override;
};

// testFs — fs.go:19. Wraps the map fs; rewrites .tsbuildinfo versions
// in/out of FakeTSVersion and writes .readable.baseline.txt siblings.
struct testFs final : vfs::FS {
	std::shared_ptr<vfs::FS> FS;
	std::unique_ptr<collections::SyncSet<std::string>> defaultLibs;
	collections::SyncSet<std::string> writtenFiles;

	bool UseCaseSensitiveFileNames() override;
	bool FileExists(const std::string& path) override;
	std::pair<std::string, bool> ReadFile(const std::string& path) override;
	vfs::Error WriteFile(const std::string& path,
	                     const std::string& data) override;
	vfs::Error AppendFile(const std::string& path,
	                      const std::string& data) override;
	vfs::Error Remove(const std::string& path) override;
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint aTime,
	                   vfs::TimePoint mTime) override;
	bool DirectoryExists(const std::string& path) override;
	vfs::Entries GetAccessibleEntries(const std::string& path) override;
	std::shared_ptr<vfs::FileInfo> Stat(const std::string& path) override;
	std::string Realpath(const std::string& path) override;

private:
	// removeIgnoreLibPath — fs.go:24.
	void removeIgnoreLibPath(const std::string& path);
	// readFileHandlingBuildInfo — fs.go:31.
	std::pair<std::string, bool>
	readFileHandlingBuildInfo(const std::string& path);
	// writeFileHandlingBuildInfo — fs.go:52.
	vfs::Error writeFileHandlingBuildInfo(const std::string& path,
	                                      const std::string& data);
};

// MockWatch — mock_watch_backend.go:46. The returned io.Closer.
struct MockWatch final : gostd::io::Closer {
	std::string Path;
	fswatch::WatchCallback Callback;
	bool Recursive = false;
	std::function<bool(const std::string&)> Ignore;
	bool Closed = false;

	// Close — mock_watch_backend.go:54.
	gostd::Error close() override;
};

// MockWatchBackend — mock_watch_backend.go:30.
struct MockWatchBackend final : watchmanager::WatchBackend {
	std::mutex mu;
	// shared_ptr: the map owns the watches; the returned io.Closers borrow
	// them (Close just flips Closed, the watch stays registered like Go).
	std::unordered_map<std::string, std::shared_ptr<MockWatch>> Dirs;
	std::function<bool(const std::string&)> DirectoryExists;
	bool UseCaseSensitiveFileNames = false;

	// WatchDirectory — mock_watch_backend.go:59.
	std::pair<std::unique_ptr<gostd::io::Closer>, gostd::Error>
	WatchDirectory(const std::string& dir, const fswatch::WatchCallback& fn,
	               bool recursive,
	               std::function<bool(const std::string&)> ignore) override;
	// WatchDirectories — mock_watch_backend.go:52.
	std::pair<std::vector<std::unique_ptr<gostd::io::Closer>>, gostd::Error>
	WatchDirectories(
	    const std::vector<watchmanager::WatchDirectoryRequest>& requests)
	    override;
	// SendEvents — mock_watch_backend.go:89.
	void SendEvents(const std::vector<fswatch::Event>& events);
	// SendOverflow — mock_watch_backend.go:119.
	void SendOverflow();
	// SendChangedPaths — mock_watch_backend.go:131.
	void SendChangedPaths(
	    const std::vector<testutil::fsbaselineutil::FileChange>& changes);
	// HasWatches — mock_watch_backend.go:39.
	bool HasWatches();
	// WatchState — mock_watch_backend.go:209.
	std::string WatchState();
};

// NewMockWatchBackend — mock_watch_backend.go:32.
MockWatchBackend* NewMockWatchBackend();

struct TestSys;

// pathIsUnder — mock_watch_backend.go:184.
bool pathIsUnder(const std::string& eventPath, const std::string& dir,
                 bool recursive, bool useCaseSensitiveFileNames);

// tscEdit — runner.go:18.
struct tscEdit {
	std::string caption;
	std::optional<std::vector<std::string>> commandLineArgs;
	std::function<void(TestSys*)> edit;
	std::string expectedDiff;
};

// noChange / noChangeOnlyEdit — runner.go:25/29.
inline tscEdit* noChange = new tscEdit{"no change", std::nullopt, nullptr, ""};
inline std::vector<tscEdit*> noChangeOnlyEdit{noChange};

// tscInput — runner.go:39.
struct tscInput {
	std::string subScenario;
	std::vector<std::string> commandLineArgs;
	FileMap files;
	std::string cwd;
	std::vector<tscEdit*> edits;
	std::unordered_map<std::string, std::string> env;
	std::optional<bool> outputIsTTY;
	bool ignoreCase = false;
	std::string windowsStyleRoot;

	// run — runner.go:92.
	void run(gostd::testing::T* t, const std::string& scenario);
	// executeCommand — runner.go:71.
	etsc::CommandLineResult executeCommand(
	    gostd::Context ctx, TestSys* sys, stringWriter* baselineBuilder,
	    const std::vector<std::string>& commandLineArgs);

private:
	// getBaselineSubFolder — runner.go:195.
	std::string getBaselineSubFolder();
};

// TestSys — sys.go:127. Synthetic tsc.System + CommandLineTesting (+WatchBackend).
struct TestSys final : etsc::System,
                       etsc::CommandLineTesting,
                       watchmanager::CommandLineTestingWithWatchBackend {
	// Go fields — sys.go:128.
	std::unique_ptr<stringWriter> currentWrite;
	stringWriter programBaselines;
	stringWriter programIncludeBaselines;
	std::unique_ptr<testutil::harnessutil::TracerForBaselining> tracer;
	std::unique_ptr<testutil::fsbaselineutil::FSDiffer> fsDiffer;
	bool forIncrementalCorrectness = false;
	std::unique_ptr<MockWatchBackend> mockWatchBackend;
	std::shared_ptr<testFs> fs_;
	std::string defaultLibraryPath;
	std::string cwd;
	std::unordered_map<std::string, std::string> env;
	bool outputIsTTY = false;
	std::shared_ptr<TestClock> clock;
	collections::SyncMap<std::string, std::filesystem::file_time_type>
	    mTimesCache;

	// === tsc.System (incl. contentmapper::Spawner) — sys.go:219 ===
	// Now/SinceStart — sys.go:221/225 (vfs.Clock via TestClock).
	vfs::TimePoint Now() override;
	gostd::Duration SinceStart() override;
	// FS — sys.go:229. `fs` field renamed `fs_` (fs() is the method).
	std::shared_ptr<vfs::FS> fs() override;
	// fsFromFileMap — sys.go:233.
	std::shared_ptr<vfs::iovfs::FsWithSys> fsFromFileMap();
	// mapFs — sys.go:237.
	std::shared_ptr<vfs::vfstest::MapFS> mapFs();
	// ensureLibPathExists — sys.go:245.
	void ensureLibPathExists(const std::string& path);
	// DefaultLibraryPath — sys.go:261.
	std::string DefaultLibraryPath() override;
	// GetCurrentDirectory — sys.go:265 (module.ResolutionHost).
	std::string GetCurrentDirectory() override;
	// Writer / ErrorWriter — sys.go:269/273.
	std::ostream* Writer() override;
	std::ostream* ErrorWriter() override;
	// WriteOutputIsTTY / GetWidthOfTerminal / GetEnvironmentVariable —
	// sys.go:277/281/293.
	bool WriteOutputIsTTY() override;
	int GetWidthOfTerminal() override;
	std::pair<std::string, bool>
	GetEnvironmentVariable(std::string_view name) override;
	// Spawn — sys.go:304 (contentmapper.Spawner).
	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr) override;

	// === CommandLineTesting — sys.go:312 ===
	// OnEmittedFiles — sys.go:314.
	void OnEmittedFiles(
	    compiler::EmitResult* result,
	    collections::SyncMap<tspath::Path, std::filesystem::file_time_type>*
	        mTimes) override;
	// OnListFilesStart/End — sys.go:352/356.
	void OnListFilesStart(std::ostream* w) override;
	void OnListFilesEnd(std::ostream* w) override;
	// OnStatisticsStart/End — sys.go:360/364.
	void OnStatisticsStart(std::ostream* w) override;
	void OnStatisticsEnd(std::ostream* w) override;
	// OnBuildStatusReportStart/End — sys.go:368/372.
	void OnBuildStatusReportStart(std::ostream* w) override;
	void OnBuildStatusReportEnd(std::ostream* w) override;
	// OnWatchStatusReportStart/End — sys.go:307/311.
	void OnWatchStatusReportStart() override;
	void OnWatchStatusReportEnd() override;
	// GetTrace — sys.go:315.
	etsc::TraceFn GetTrace(std::ostream* w, locale::Locale locale) override;
	// writeHeaderToBaseline — sys.go:326.
	void writeHeaderToBaseline(stringWriter* baseline,
	                           incremental::Program* program);
	// WatchBackend — sys.go:403 (CommandLineTestingWithWatchBackend).
	watchmanager::WatchBackend* WatchBackend() override;
	// OnProgram — sys.go:407.
	void OnProgram(incremental::Program* program) override;
	// baselinePrograms — sys.go:478.
	std::string baselinePrograms(stringWriter* baseline,
	                             const std::string& header);
	// serializeState — sys.go:494.
	void serializeState(stringWriter* baseline);
	// baselineOutput — sys.go:501.
	void baselineOutput(gostd::io::Writer* baseline);
	// baselineFSwithDiff — sys.go:506.
	void baselineFSwithDiff(gostd::io::Writer* baseline);
	// getOutput / clearOutput — sys.go:593/598.
	std::string getOutput(bool forComparing);
	void clearOutput();

	// === test mutation helpers — sys.go:168-216 ===
	// writeFileNoError — sys.go:168.
	void writeFileNoError(const std::string& path, const std::string& content);
	// removeNoError — sys.go:173.
	void removeNoError(const std::string& path);
	// readFileNoError — sys.go:178.
	std::string readFileNoError(const std::string& path);
	// renameFileNoError — sys.go:188.
	void renameFileNoError(const std::string& oldPath,
	                       const std::string& newPath);
	// replaceFileText / replaceFileTextAll — sys.go:197/203.
	void replaceFileText(const std::string& path, const std::string& oldStr,
	                     const std::string& newStr);
	void replaceFileTextAll(const std::string& path,
	                        const std::string& oldStr,
	                        const std::string& newStr);
	// appendFile / prependFile — sys.go:208/216.
	void appendFile(const std::string& path, const std::string& content);
	void prependFile(const std::string& path, const std::string& content);
};

// getDiffForIncremental — runner.go:158.
std::string getDiffForIncremental(TestSys* incrementalSys,
                                  TestSys* nonIncrementalSys);

// NewTscSystem — sys.go:82.
std::unique_ptr<TestSys> NewTscSystem(const FileMap& files,
                                      bool useCaseSensitiveFileNames,
                                      const std::string& cwd);

// GetFileMapWithBuild — sys.go:97.
FileMap GetFileMapWithBuild(FileMap files,
                            const std::vector<std::string>& commandLineArgs);

// newTestSys — sys.go:112.
std::unique_ptr<TestSys> newTestSys(tscInput* tscInput,
                                    bool forIncrementalCorrectness);

// getTestLibPathFor — sys.go:68.
std::string getTestLibPathFor(std::string_view libName);

// tscLibPath / tscDefaultLibContent — sys.go:34/36.
inline constexpr std::string_view tscLibPath = "/home/src/tslibs/TS/Lib";
// sys.go:36.
extern const std::string tscDefaultLibContent;

// toReadableBuildInfo — readablebuildinfo.go:242 (fs.go calls it to write
// <file>.readable.baseline.txt).
std::string toReadableBuildInfo(incremental::BuildInfo* buildInfo,
                                const std::string& buildInfoText);

// toReadableFileEmitKind — readablebuildinfo.go:409.
std::string toReadableFileEmitKind(incremental::FileEmitKind emitKind);

// === package vars — sys.go:33,465-476 ===
inline constexpr std::string_view buildStartingAt = "build starting at ";
inline constexpr std::string_view buildFinishedIn = "build finished in ";
inline constexpr std::string_view listFileStart = "!!! List files start";
inline constexpr std::string_view listFileEnd = "!!! List files end";
inline constexpr std::string_view statisticsStart = "!!! Statistics start";
inline constexpr std::string_view statisticsEnd = "!!! Statistics end";
inline constexpr std::string_view buildStatusReportStart =
    "!!! Build Status Report Start";
inline constexpr std::string_view buildStatusReportEnd =
    "!!! Build Status Report End";
inline constexpr std::string_view watchStatusReportStart =
    "!!! Watch Status Report Start";
inline constexpr std::string_view watchStatusReportEnd =
    "!!! Watch Status Report End";
inline constexpr std::string_view traceStart = "!!! Trace start";
inline constexpr std::string_view traceEnd = "!!! Trace end";
inline constexpr std::string_view fakeTimeStamp = "HH:MM:SS AM";
inline constexpr std::string_view fakeDuration = "d.ddds";

// englishVersion/fakeEnglishVersion/czech/czechVersion/fakeCzechVersion —
// sys.go:472-476.
extern const std::string englishVersion;
extern const std::string fakeEnglishVersion;
extern const locale::Locale czech;
extern const std::string czechVersion;
extern const std::string fakeCzechVersion;

}  // namespace tsc::execute::tsctests
