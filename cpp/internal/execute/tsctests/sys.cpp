// sys.cpp — port of tsctests/sys.go: the synthetic System + clock + output
// sanitizer the tscbuild/tscwatch e2e tests run against.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/execute/execute.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/testutil/stringtestutil/stringtestutil.h"

namespace tsc::execute::tsctests {

namespace {

// strconv.Atoi + core.Must — sys.go:239 (invalid input is a test bug).
int mustAtoi(const std::string& s) {
	size_t pos = 0;
	int v = 0;
	try {
		v = std::stoi(s, &pos);
	} catch (...) {
		TSC_UNREACHABLE("strconv.Atoi: invalid syntax");
	}
	if (pos != s.size()) {
		TSC_UNREACHABLE("strconv.Atoi: invalid syntax");
	}
	return v;
}

// strings.Split(s, "\n").
std::vector<std::string> splitLines(std::string_view s) {
	std::vector<std::string> out;
	size_t start = 0;
	for (;;) {
		auto pos = s.find('\n', start);
		if (pos == std::string_view::npos) {
			out.emplace_back(s.substr(start));
			return out;
		}
		out.emplace_back(s.substr(start, pos - start));
		start = pos + 1;
	}
}

// strings.Join(lines, "\n").
std::string joinLines(const std::vector<std::string>& v) {
	std::string out;
	for (size_t i = 0; i < v.size(); i++) {
		if (i) out += '\n';
		out += v[i];
	}
	return out;
}

// strings.ReplaceAll.
std::string replaceAll(std::string_view s, std::string_view old_,
                       std::string_view new_) {
	if (old_.empty()) return std::string(s);
	std::string out;
	size_t start = 0;
	for (;;) {
		auto pos = s.find(old_, start);
		if (pos == std::string_view::npos) {
			out.append(s.substr(start));
			return out;
		}
		out.append(s.substr(start, pos - start));
		out.append(new_);
		start = pos + old_.size();
	}
}

// strings.Replace(s, old, new, 1).
std::string replaceOnce(std::string_view s, std::string_view old_,
                        std::string_view new_) {
	auto pos = s.find(old_);
	if (pos == std::string_view::npos) return std::string(s);
	return std::string(s.substr(0, pos)) + std::string(new_) +
	       std::string(s.substr(pos + old_.size()));
}

}  // namespace

// tscDefaultLibContent — sys.go:36 (stringtestutil.Dedent literal).
const std::string tscDefaultLibContent = testutil::stringtestutil::Dedent(R"_(/// <reference no-default-lib="true"/>
interface Boolean {}
interface Function {}
interface CallableFunction {}
interface NewableFunction {}
interface IArguments {}
interface Number { toExponential: any; }
interface Object {}
interface RegExp {}
interface String { charAt: any; }
interface Array<T> { length: number; [n: number]: T; }
interface ReadonlyArray<T> {}
interface SymbolConstructor {
    (desc?: string | number): symbol;
    for(name: string): symbol;
    readonly toStringTag: symbol;
}
declare var Symbol: SymbolConstructor;
interface Symbol {
    readonly [Symbol.toStringTag]: string;
}
declare const console: { log(msg: any): void; };
)_");

// package vars — sys.go:469-474.
const std::string englishVersion =
    localize(locale::Default, Version_0, "", {std::string(version())});
const std::string fakeEnglishVersion = localize(
    locale::Default, Version_0, "", {std::string(testutil::harnessutil::FakeTSVersion)});
const locale::Locale czech = locale::parse("cs").first;
const std::string czechVersion =
    localize(czech, Version_0, "", {std::string(version())});
const std::string fakeCzechVersion =
    localize(czech, Version_0, "", {std::string(testutil::harnessutil::FakeTSVersion)});

// getTestLibPathFor — sys.go:63.
std::string getTestLibPathFor(std::string_view libName) {
	std::string libFile;
	if (auto it = tsoptions::LibMap.find(libName);
	    it != tsoptions::LibMap.end()) {
		libFile = std::string(it->second);
	} else {
		libFile = "lib." + std::string(libName) + ".d.ts";
	}
	return std::string(tscLibPath) + "/" + libFile;
}

// TestClock — sys.go:73.
vfs::TimePoint TestClock::Now() {
	// sys.go:79.
	std::lock_guard<std::mutex> lock(nowMu);
	if (now == vfs::TimePoint{}) {
		now = start;
	}
	now += std::chrono::seconds(1); // Simulate some time passing
	return now;
}

vfs::Duration TestClock::SinceStart() {
	// sys.go:89.
	return std::chrono::duration_cast<vfs::Duration>(Now() - start);
}

// NewTscSystem — sys.go:93.
std::unique_ptr<TestSys> NewTscSystem(const FileMap& files,
                                      bool useCaseSensitiveFileNames,
                                      const std::string& cwd) {
	auto clock = std::make_shared<TestClock>();
	clock->start = std::chrono::system_clock::now();
	auto sys = std::make_unique<TestSys>();
	auto fs = std::make_shared<testFs>();
	fs->FS = vfs::vfstest::FromMapWithClock(files, useCaseSensitiveFileNames,
	                                   clock);
	sys->fs_ = fs;
	sys->cwd = cwd;
	sys->outputIsTTY = true;
	sys->clock = std::move(clock);
	return sys;
}

// newTestSys — sys.go:119.
std::unique_ptr<TestSys> newTestSys(tscInput* tscInput,
                                    bool forIncrementalCorrectness) {
	std::string cwd = tscInput->cwd;
	if (cwd.empty()) {
		cwd = "/home/src/workspaces/project";
	}
	std::string libPath{tscLibPath};
	if (!tscInput->windowsStyleRoot.empty()) {
		libPath = tscInput->windowsStyleRoot + libPath.substr(1);
	}
	auto sys = NewTscSystem(tscInput->files, !tscInput->ignoreCase, cwd);
	sys->defaultLibraryPath = libPath;
	sys->currentWrite = std::make_unique<stringWriter>();
	if (tscInput->outputIsTTY) {
		sys->outputIsTTY = *tscInput->outputIsTTY;
	}
	sys->tracer = std::unique_ptr<testutil::harnessutil::TracerForBaselining>(
	    testutil::harnessutil::NewTracerForBaselining(
	        tspath::ComparePathsOptions{/*UseCaseSensitiveFileNames=*/
	                                    !tscInput->ignoreCase,
	                                    /*CurrentDirectory=*/cwd},
	        &sys->currentWrite->s));
	sys->env = tscInput->env;
	sys->forIncrementalCorrectness = forIncrementalCorrectness;
	sys->mockWatchBackend =
	    std::unique_ptr<MockWatchBackend>(NewMockWatchBackend());
	auto innerFs = sys->fs_->FS;
	// Go: sys.fs.FS.DirectoryExists — method value on the inner fs.
	sys->mockWatchBackend->DirectoryExists =
	    [innerFs](const std::string& dir) {
		    return innerFs->DirectoryExists(dir);
	    };
	sys->mockWatchBackend->UseCaseSensitiveFileNames = !tscInput->ignoreCase;
	auto fsWithSys = std::dynamic_pointer_cast<vfs::iovfs::FsWithSys>(innerFs);
	if (!fsWithSys) {
		// Go: sys.fs.FS.(iovfs.FsWithSys) — type-assert panic.
		TSC_UNREACHABLE("newTestSys: FS is not an iovfs.FsWithSys");
	}
	sys->fsDiffer = std::make_unique<testutil::fsbaselineutil::FSDiffer>();
	sys->fsDiffer->FS = fsWithSys;
	testFs* fsPtr = sys->fs_.get();
	sys->fsDiffer->DefaultLibs =
	    [fsPtr]() { return fsPtr->defaultLibs.get(); };
	sys->fsDiffer->WrittenFiles = &sys->fs_->writtenFiles;

	// Ensure the default library file is present
	sys->ensureLibPathExists("lib.d.ts");
	for (const auto& [target, libFile] : tsoptions::targetToLibMap) {
		sys->ensureLibPathExists(std::string(libFile));
	}
	for (auto libFile : tsoptions::LibFilesSet) {
		sys->ensureLibPathExists(std::string(libFile));
	}
	return sys;
}

// GetFileMapWithBuild — sys.go:104.
FileMap GetFileMapWithBuild(FileMap files,
                            const std::vector<std::string>& commandLineArgs_) {
	tscInput input;
	input.files = files; // maps.Clone(files) — a copy, like Go.
	auto sys = newTestSys(&input, false);
	execute::CommandLine(gostd::contextBackground(), sys.get(),
	                     commandLineArgs_, sys.get());
	sys->fs_->writtenFiles.Range([&](const std::string& key) {
		if (auto [text, ok] = sys->fsFromFileMap()->ReadFile(key); ok) {
			files[key] = text;
		}
		return true;
	});
	return files;
}

// === TestSys ===

// Now / SinceStart — sys.go:183/187.
vfs::TimePoint TestSys::Now() { return clock->Now(); }
gostd::Duration TestSys::SinceStart() {
	return std::chrono::duration_cast<gostd::Duration>(
	    clock->SinceStart());
}

// FS — sys.go:191 (`fs` field is `fs_`; FS() is the method).
std::shared_ptr<vfs::FS> TestSys::fs() { return fs_; }

// fsFromFileMap — sys.go:195.
std::shared_ptr<vfs::iovfs::FsWithSys> TestSys::fsFromFileMap() {
	return fsDiffer->FS;
}

// mapFs — sys.go:199.
std::shared_ptr<vfs::vfstest::MapFS> TestSys::mapFs() { return fsDiffer->MapFs(); }

// ensureLibPathExists — sys.go:203.
void TestSys::ensureLibPathExists(const std::string& path_) {
	std::string path = defaultLibraryPath + "/" + path_;
	if (auto [_, ok] = fsFromFileMap()->ReadFile(path); !ok) {
		if (!fs_->defaultLibs) {
			fs_->defaultLibs =
			    std::make_unique<collections::SyncSet<std::string>>();
		}
		fs_->defaultLibs->Add(path);
		if (auto err = fsFromFileMap()->WriteFile(path,
		                                        tscDefaultLibContent);
		    err) {
			TSC_UNREACHABLE(
			    ("Failed to write default library file: " + err.str())
			        .c_str());
		}
	}
}

// DefaultLibraryPath — sys.go:217.
std::string TestSys::DefaultLibraryPath() { return defaultLibraryPath; }

// GetCurrentDirectory — sys.go:221.
std::string TestSys::GetCurrentDirectory() { return cwd; }

// Writer / ErrorWriter — sys.go:225/229.
std::ostream* TestSys::Writer() { return currentWrite.get(); }
std::ostream* TestSys::ErrorWriter() { return currentWrite.get(); }

// WriteOutputIsTTY — sys.go:233.
bool TestSys::WriteOutputIsTTY() { return outputIsTTY; }

// GetWidthOfTerminal — sys.go:237.
int TestSys::GetWidthOfTerminal() {
	if (auto [widthStr, _] = GetEnvironmentVariable("TS_TEST_TERMINAL_WIDTH");
	    !widthStr.empty()) {
		return mustAtoi(widthStr);
	}
	return 0;
}

// GetEnvironmentVariable — sys.go:244.
std::pair<std::string, bool>
TestSys::GetEnvironmentVariable(std::string_view name) {
	if (auto it = env.find(std::string(name)); it != env.end()) {
		return {it->second, true};
	}
	return {"", false};
}

// Spawn — sys.go:252. Serves the fake content mappers in-process,
// selecting the implementation by the exec command the mapper package
// declares (see internal/testutil/contentmappertest).
std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
TestSys::Spawn(const std::vector<std::string>& command,
               const std::string& dir, gostd::io::Writer* stderr) {
	return testutil::contentmappertest::NewSpawner()->Spawn(command, dir, stderr);
}

// OnEmittedFiles — sys.go:256.
void TestSys::OnEmittedFiles(
    compiler::EmitResult* result,
    collections::SyncMap<tspath::Path, std::filesystem::file_time_type>*
        mTimesCache_) {
	if (result != nullptr) {
		for (const auto& file : result->EmittedFiles) {
			vfs::TimePoint modTime = mapFs()->GetModTime(file);
			if (auto* serializedDiff = fsDiffer->SerializedDiff();
			    serializedDiff != nullptr) {
				if (auto it = serializedDiff->Snap.find(file);
				    it != serializedDiff->Snap.end() &&
				    it->second->MTime == modTime) {
					// Even though written, timestamp was reverted
					continue;
				}
			}

			// Ensure that the timestamp for emitted files is in the order
			vfs::TimePoint now = Now();
			if (auto err =
			        fsFromFileMap()->Chtimes(file, vfs::TimePoint{}, now);
			    err) {
				TSC_UNREACHABLE(
				    ("Failed to change time for emitted file: " + file +
				     ": " + err.str())
				        .c_str());
			}
			// Update the mTime cache in --b mode to store the updated
			// timestamp so tests will behave deteministically when
			// finding newest output
			if (mTimesCache_ != nullptr) {
				tspath::Path path = tspath::toPath(
				    file, GetCurrentDirectory(),
				    fs()->UseCaseSensitiveFileNames());
				if (auto [_, found] = mTimesCache_->Load(path); found) {
					mTimesCache_->Store(
					    path,
					    std::chrono::file_clock::from_sys(now));
				}
			}
		}
	}
}

// OnListFilesStart/End — sys.go:283/287.
void TestSys::OnListFilesStart(std::ostream* w) {
	*w << listFileStart << '\n';
}
void TestSys::OnListFilesEnd(std::ostream* w) {
	*w << listFileEnd << '\n';
}

// OnStatisticsStart/End — sys.go:291/295.
void TestSys::OnStatisticsStart(std::ostream* w) {
	*w << statisticsStart << '\n';
}
void TestSys::OnStatisticsEnd(std::ostream* w) {
	*w << statisticsEnd << '\n';
}

// OnBuildStatusReportStart/End — sys.go:299/303.
void TestSys::OnBuildStatusReportStart(std::ostream* w) {
	*w << buildStatusReportStart << '\n';
}
void TestSys::OnBuildStatusReportEnd(std::ostream* w) {
	*w << buildStatusReportEnd << '\n';
}

// OnWatchStatusReportStart/End — sys.go:307/311.
void TestSys::OnWatchStatusReportStart() {
	*Writer() << watchStatusReportStart << '\n';
}
void TestSys::OnWatchStatusReportEnd() {
	*Writer() << watchStatusReportEnd << '\n';
}

// GetTrace — sys.go:315.
etsc::TraceFn TestSys::GetTrace(std::ostream* w, locale::Locale locale_) {
	return [w, locale_, this](const DiagnosticMessage* msg,
	                          const std::vector<std::string>& args) {
		*w << traceStart << '\n';
		struct traceEndGuard {
			std::ostream* w;
			~traceEndGuard() { *w << traceEnd << '\n'; }
		} guard{w};
		// With tsc -b building projects in parallel we cannot serialize
		// the package.json lookup trace so trace as if it wasnt cached
		std::string str = localize(locale_, msg, "", args);
		tracer->TraceWithWriter(w, str, w == Writer());
	};
}

// writeHeaderToBaseline — sys.go:326.
void TestSys::writeHeaderToBaseline(stringWriter* builder,
                                    incremental::Program* program) {
	if (builder->Len() != 0) {
		builder->WriteString("\n");
	}

	if (const std::string& configFilePath =
	        program->Options()->ConfigFilePath;
	    !configFilePath.empty()) {
		builder->WriteString(tspath::getRelativePathFromDirectory(
		    cwd, configFilePath,
		    tspath::ComparePathsOptions{
		        fs()->UseCaseSensitiveFileNames(),
		        GetCurrentDirectory()}));
		builder->WriteString("::\n");
	}
}

// WatchBackend — sys.go:340.
watchmanager::WatchBackend* TestSys::WatchBackend() {
	return mockWatchBackend.get();
}

// OnProgram — sys.go:344.
void TestSys::OnProgram(incremental::Program* program) {
	writeHeaderToBaseline(&programBaselines, program);

	auto* testingData = program->GetTestingData();
	programBaselines.WriteString("SemanticDiagnostics::\n");
	for (auto* file : program->GetProgram()->GetSourceFiles()) {
		if (auto [diagnostics, ok] =
		        testingData->SemanticDiagnosticsPerFile->Load(file->Path());
		    ok) {
			if (auto [oldDiagnostics, oldOk] =
			        testingData->OldProgramSemanticDiagnosticsPerFile->Load(
			            file->Path());
			    !oldOk || oldDiagnostics != diagnostics) {
				programBaselines.WriteString("*refresh*    ");
				programBaselines.WriteString(file->FileName());
				programBaselines.WriteString("\n");
			}
		} else {
			programBaselines.WriteString("*not cached* ");
			programBaselines.WriteString(file->FileName());
			programBaselines.WriteString("\n");
		}
	}

	// Write signature updates
	programBaselines.WriteString("Signatures::\n");
	for (auto* file : program->GetProgram()->GetSourceFiles()) {
		if (auto it = testingData->UpdatedSignatureKinds.find(file->Path());
		    it != testingData->UpdatedSignatureKinds.end()) {
			switch (it->second) {
			case incremental::SignatureUpdateKind::ComputedDts:
				programBaselines.WriteString("(computed .d.ts) ");
				programBaselines.WriteString(file->FileName());
				programBaselines.WriteString("\n");
				break;
			case incremental::SignatureUpdateKind::StoredAtEmit:
				programBaselines.WriteString("(stored at emit) ");
				programBaselines.WriteString(file->FileName());
				programBaselines.WriteString("\n");
				break;
			case incremental::SignatureUpdateKind::UsedVersion:
				programBaselines.WriteString("(used version)   ");
				programBaselines.WriteString(file->FileName());
				programBaselines.WriteString("\n");
				break;
			}
		}
	}

	std::vector<std::string> filesWithoutIncludeReason;
	std::vector<std::string> fileNotInProgramWithIncludeReason;
	const auto& includeReasons =
	    program->GetProgram()->GetIncludeReasonsMap();
	for (auto* file : program->GetProgram()->GetSourceFiles()) {
		if (includeReasons.find(file->Path()) == includeReasons.end()) {
			filesWithoutIncludeReason.push_back(file->Path());
		}
	}
	for (const auto& [path, reasons] : includeReasons) {
		if (program->GetProgram()->GetSourceFileByPath(path) == nullptr &&
		    !program->GetProgram()->IsMissingPath(path)) {
			fileNotInProgramWithIncludeReason.push_back(path);
		}
	}
	if (!filesWithoutIncludeReason.empty() ||
	    !fileNotInProgramWithIncludeReason.empty()) {
		writeHeaderToBaseline(&programIncludeBaselines, program);
		programIncludeBaselines.WriteString(
		    "!!! Expected all files to have include "
		    "reasons\nfilesWithoutIncludeReason::\n");
		for (const auto& file : filesWithoutIncludeReason) {
			programIncludeBaselines.WriteString("  ");
			programIncludeBaselines.WriteString(file);
			programIncludeBaselines.WriteString("\n");
		}
		programIncludeBaselines.WriteString(
		    "filesNotInProgramWithIncludeReason::\n");
		for (const auto& file : fileNotInProgramWithIncludeReason) {
			programIncludeBaselines.WriteString("  ");
			programIncludeBaselines.WriteString(file);
			programIncludeBaselines.WriteString("\n");
		}
	}
}

// baselinePrograms — sys.go:414.
std::string TestSys::baselinePrograms(stringWriter* baseline,
                                      const std::string& header) {
	baseline->WriteString(programBaselines.String());
	programBaselines.Reset();
	std::string result;
	if (programIncludeBaselines.Len() > 0) {
		result += gostd::sprintf(
		    "\n\n%s\n!!! Include reasons expectations don't match pls "
		    "review!!!\n",
		    {header});
		result += programIncludeBaselines.String();
		programIncludeBaselines.Reset();
		baseline->WriteString(result);
	}
	return result;
}

// serializeState — sys.go:427.
void TestSys::serializeState(stringWriter* baseline) {
	baselineOutput(baseline);
	baselineFSwithDiff(baseline);
	// todo watch
	// this.serializeWatches(baseline);
	// this.timeoutCallbacks.serialize(baseline);
	// this.immediateCallbacks.serialize(baseline);
	// this.pendingInstalls.serialize(baseline);
	// this.service?.baseline();
}

// baselineOutput — sys.go:456.
void TestSys::baselineOutput(gostd::io::Writer* baseline) {
	baseline->write("\nOutput::\n");
	std::string output = getOutput(false);
	baseline->write(output);
}

// baselineFSwithDiff — sys.go:562.
void TestSys::baselineFSwithDiff(gostd::io::Writer* baseline) {
	fsDiffer->BaselineFSwithDiff(baseline);
}

// outputSanitizer — sys.go:462.
struct outputSanitizer {
	bool forComparing = false;
	std::vector<std::string> lines;
	int index = 0;
	std::vector<std::string> outputLines;

	// addOutputLine — sys.go:477.
	void addOutputLine(std::string_view s_) {
		std::string s = std::string(s_);
		s = replaceAll(s, "'" + std::string(version()) + "'",
		               "'" + std::string(testutil::harnessutil::FakeTSVersion) + "'");
		s = replaceAll(s, englishVersion, fakeEnglishVersion);
		s = replaceAll(s, czechVersion, fakeCzechVersion);
		s = testutil::fsbaselineutil::SanitizeInternalSymbolName(s);
		outputLines.push_back(std::move(s));
	}

	// sanitizeBuildStatusTimeStamp — sys.go:485.
	std::string sanitizeBuildStatusTimeStamp() {
		const std::string& statusLine = lines[index];
		auto hhSeparator = statusLine.find(':');
		if (hhSeparator == std::string::npos || hhSeparator < 2) {
			TSC_UNREACHABLE("Expected timestamp");
		}
		return statusLine.substr(0, hhSeparator - 2) +
		       std::string(fakeTimeStamp) +
		       statusLine.substr(hhSeparator + fakeTimeStamp.size() - 2);
	}

	// addOrSkipLinesForComparing — sys.go:520.
	bool addOrSkipLinesForComparing(
	    std::string_view lineStart, std::string_view lineEnd,
	    bool skipEvenIfNotComparing,
	    const std::function<std::string()>& sanitizeFirstLine) {
		if (lines[index] != lineStart) {
			return false;
		}
		index++;
		bool isFirstLine = true;
		for (; index < static_cast<int>(lines.size()); index++) {
			if (lines[index] == lineEnd) {
				return true;
			}
			if (!forComparing && !skipEvenIfNotComparing) {
				std::string line = lines[index];
				if (isFirstLine && sanitizeFirstLine) {
					line = sanitizeFirstLine();
					isFirstLine = false;
				}
				addOutputLine(line);
			}
		}
		TSC_UNREACHABLE(("Expected lineEnd" + std::string(lineEnd) +
		                 " not found after " + std::string(lineStart))
		                    .c_str());
	}

	// transformLines — sys.go:494.
	std::string transformLines() {
		for (; index < static_cast<int>(lines.size()); index++) {
			const std::string& line = lines[index];
			if (line.starts_with(buildStartingAt)) {
				if (!forComparing) {
					addOutputLine(std::string(buildStartingAt) +
					              std::string(fakeTimeStamp));
				}
				continue;
			}
			if (line.starts_with(buildFinishedIn)) {
				if (!forComparing) {
					addOutputLine(std::string(buildFinishedIn) +
					              std::string(fakeDuration));
				}
				continue;
			}
			if (!addOrSkipLinesForComparing(listFileStart, listFileEnd,
			                              false, nullptr) &&
			    !addOrSkipLinesForComparing(statisticsStart, statisticsEnd,
			                                true, nullptr) &&
			    !addOrSkipLinesForComparing(traceStart, traceEnd, false,
			                                nullptr) &&
			    !addOrSkipLinesForComparing(
			        buildStatusReportStart, buildStatusReportEnd, false,
			        [this] { return sanitizeBuildStatusTimeStamp(); }) &&
			    !addOrSkipLinesForComparing(
			        watchStatusReportStart, watchStatusReportEnd, false,
			        [this] { return sanitizeBuildStatusTimeStamp(); })) {
				addOutputLine(line);
			}
		}
		return joinLines(outputLines);
	}
};

// getOutput — sys.go:547.
std::string TestSys::getOutput(bool forComparing) {
	std::vector<std::string> lines = splitLines(currentWrite->String());
	outputSanitizer transformer;
	transformer.forComparing = forComparing;
	transformer.lines = std::move(lines);
	return transformer.transformLines();
}

// clearOutput — sys.go:557.
void TestSys::clearOutput() {
	currentWrite->Reset();
	tracer->Reset();
}

// writeFileNoError — sys.go:566.
void TestSys::writeFileNoError(const std::string& path,
                               const std::string& content) {
	if (auto err = fsFromFileMap()->WriteFile(path, content); err) {
		// Go panic(err) — the error is the panic value.
		TSC_UNREACHABLE(err.str().c_str());
	}
}

// removeNoError — sys.go:572.
void TestSys::removeNoError(const std::string& path) {
	if (auto err = fsFromFileMap()->Remove(path); err) {
		TSC_UNREACHABLE(err.str().c_str());
	}
}

// readFileNoError — sys.go:578.
std::string TestSys::readFileNoError(const std::string& path) {
	auto [content, ok] = fsFromFileMap()->ReadFile(path);
	if (!ok) {
		TSC_UNREACHABLE(("File not found: " + path).c_str());
	}
	return content;
}

// renameFileNoError — sys.go:586.
void TestSys::renameFileNoError(const std::string& oldPath,
                                const std::string& newPath) {
	writeFileNoError(newPath, readFileNoError(oldPath));
	removeNoError(oldPath);
}

// replaceFileText — sys.go:591.
void TestSys::replaceFileText(const std::string& path,
                              const std::string& oldText,
                              const std::string& newText) {
	std::string content = readFileNoError(path);
	content = replaceOnce(content, oldText, newText);
	writeFileNoError(path, content);
}

// replaceFileTextAll — sys.go:597.
void TestSys::replaceFileTextAll(const std::string& path,
                                 const std::string& oldText,
                                 const std::string& newText) {
	std::string content = readFileNoError(path);
	content = replaceAll(content, oldText, newText);
	writeFileNoError(path, content);
}

// appendFile — sys.go:603.
void TestSys::appendFile(const std::string& path,
                         const std::string& content) {
	writeFileNoError(path, readFileNoError(path) + content);
}

// prependFile — sys.go:608.
void TestSys::prependFile(const std::string& path,
                          const std::string& content) {
	writeFileNoError(path, content + readFileNoError(path));
}

}  // namespace tsc::execute::tsctests
