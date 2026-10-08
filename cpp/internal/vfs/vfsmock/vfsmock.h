// vfsmock.h — port of tsc/internal/vfs/vfsmock/mock_generated.go (a moq
// mock of vfs.FS) + wrapper.go (Wrap).
#pragma once

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/vfs/vfs.h"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

namespace tsc::vfs::vfsmock {

// FSMock is a mock implementation of vfs.FS. Call recording mirrors moq:
// calls.<Method> holds one record per call, guarded by a per-method lock.
struct FSMock final : vfs::FS {
	// Call-record types (Go's anonymous structs).
	struct PathDataCall {
		std::string Path;
		std::string Data;
	};
	struct ChtimesCall {
		std::string Path;
		TimePoint ATime;
		TimePoint MTime;
	};
	struct PathCall {
		std::string Path;
	};
	struct EmptyCall {};

	// AppendFileFunc mocks the AppendFile method.
	std::function<Error(const std::string& path, const std::string& data)>
	    AppendFileFunc;
	// ChtimesFunc mocks the Chtimes method.
	std::function<Error(const std::string& path, TimePoint aTime,
	                    TimePoint mTime)>
	    ChtimesFunc;
	// DirectoryExistsFunc mocks the DirectoryExists method.
	std::function<bool(const std::string& path)> DirectoryExistsFunc;
	// FileExistsFunc mocks the FileExists method.
	std::function<bool(const std::string& path)> FileExistsFunc;
	// GetAccessibleEntriesFunc mocks the GetAccessibleEntries method.
	std::function<Entries(const std::string& path)>
	    GetAccessibleEntriesFunc;
	// ReadFileFunc mocks the ReadFile method.
	std::function<std::pair<std::string, bool>(const std::string& path)>
	    ReadFileFunc;
	// RealpathFunc mocks the Realpath method.
	std::function<std::string(const std::string& path)> RealpathFunc;
	// RemoveFunc mocks the Remove method.
	std::function<Error(const std::string& path)> RemoveFunc;
	// StatFunc mocks the Stat method.
	std::function<std::shared_ptr<FileInfo>(const std::string& path)>
	    StatFunc;
	// UseCaseSensitiveFileNamesFunc mocks the UseCaseSensitiveFileNames
	// method.
	std::function<bool()> UseCaseSensitiveFileNamesFunc;
	// WriteFileFunc mocks the WriteFile method.
	std::function<Error(const std::string& path, const std::string& data)>
	    WriteFileFunc;

	// calls tracks calls to the methods.
	struct Calls {
		std::vector<PathDataCall> AppendFile;
		std::vector<ChtimesCall> Chtimes;
		std::vector<PathCall> DirectoryExists;
		std::vector<PathCall> FileExists;
		std::vector<PathCall> GetAccessibleEntries;
		std::vector<PathCall> ReadFile;
		std::vector<PathCall> Realpath;
		std::vector<PathCall> Remove;
		std::vector<PathCall> Stat;
		std::vector<EmptyCall> UseCaseSensitiveFileNames;
		std::vector<PathDataCall> WriteFile;
	};
	Calls calls;

	mutable std::shared_mutex lockAppendFile;
	mutable std::shared_mutex lockChtimes;
	mutable std::shared_mutex lockDirectoryExists;
	mutable std::shared_mutex lockFileExists;
	mutable std::shared_mutex lockGetAccessibleEntries;
	mutable std::shared_mutex lockReadFile;
	mutable std::shared_mutex lockRealpath;
	mutable std::shared_mutex lockRemove;
	mutable std::shared_mutex lockStat;
	mutable std::shared_mutex lockUseCaseSensitiveFileNames;
	mutable std::shared_mutex lockWriteFile;

	// AppendFile calls AppendFileFunc.
	Error AppendFile(const std::string& path,
	                 const std::string& data) override {
		if (!AppendFileFunc) {
			TSC_UNREACHABLE(
			    "FSMock.AppendFileFunc: method is nil but "
			    "FS.AppendFile was just called");
		}
		{
			std::unique_lock lock(lockAppendFile);
			calls.AppendFile.push_back({path, data});
		}
		return AppendFileFunc(path, data);
	}

	// AppendFileCalls gets all the calls that were made to AppendFile.
	std::vector<PathDataCall> AppendFileCalls() const {
		std::shared_lock lock(lockAppendFile);
		return calls.AppendFile;
	}

	// Chtimes calls ChtimesFunc.
	Error Chtimes(const std::string& path, TimePoint aTime,
	              TimePoint mTime) override {
		if (!ChtimesFunc) {
			TSC_UNREACHABLE(
			    "FSMock.ChtimesFunc: method is nil but "
			    "FS.Chtimes was just called");
		}
		{
			std::unique_lock lock(lockChtimes);
			calls.Chtimes.push_back({path, aTime, mTime});
		}
		return ChtimesFunc(path, aTime, mTime);
	}

	// ChtimesCalls gets all the calls that were made to Chtimes.
	std::vector<ChtimesCall> ChtimesCalls() const {
		std::shared_lock lock(lockChtimes);
		return calls.Chtimes;
	}

	// DirectoryExists calls DirectoryExistsFunc.
	bool DirectoryExists(const std::string& path) override {
		if (!DirectoryExistsFunc) {
			TSC_UNREACHABLE(
			    "FSMock.DirectoryExistsFunc: method is nil but "
			    "FS.DirectoryExists was just called");
		}
		{
			std::unique_lock lock(lockDirectoryExists);
			calls.DirectoryExists.push_back({path});
		}
		return DirectoryExistsFunc(path);
	}

	// DirectoryExistsCalls gets all calls made to DirectoryExists.
	std::vector<PathCall> DirectoryExistsCalls() const {
		std::shared_lock lock(lockDirectoryExists);
		return calls.DirectoryExists;
	}

	// FileExists calls FileExistsFunc.
	bool FileExists(const std::string& path) override {
		if (!FileExistsFunc) {
			TSC_UNREACHABLE(
			    "FSMock.FileExistsFunc: method is nil but "
			    "FS.FileExists was just called");
		}
		{
			std::unique_lock lock(lockFileExists);
			calls.FileExists.push_back({path});
		}
		return FileExistsFunc(path);
	}

	// FileExistsCalls gets all the calls that were made to FileExists.
	std::vector<PathCall> FileExistsCalls() const {
		std::shared_lock lock(lockFileExists);
		return calls.FileExists;
	}

	// GetAccessibleEntries calls GetAccessibleEntriesFunc.
	Entries GetAccessibleEntries(const std::string& path) override {
		if (!GetAccessibleEntriesFunc) {
			TSC_UNREACHABLE(
			    "FSMock.GetAccessibleEntriesFunc: method is nil "
			    "but FS.GetAccessibleEntries was just called");
		}
		{
			std::unique_lock lock(lockGetAccessibleEntries);
			calls.GetAccessibleEntries.push_back({path});
		}
		return GetAccessibleEntriesFunc(path);
	}

	// GetAccessibleEntriesCalls gets all calls made to
	// GetAccessibleEntries.
	std::vector<PathCall> GetAccessibleEntriesCalls() const {
		std::shared_lock lock(lockGetAccessibleEntries);
		return calls.GetAccessibleEntries;
	}

	// ReadFile calls ReadFileFunc.
	std::pair<std::string, bool>
	ReadFile(const std::string& path) override {
		if (!ReadFileFunc) {
			TSC_UNREACHABLE(
			    "FSMock.ReadFileFunc: method is nil but "
			    "FS.ReadFile was just called");
		}
		{
			std::unique_lock lock(lockReadFile);
			calls.ReadFile.push_back({path});
		}
		return ReadFileFunc(path);
	}

	// ReadFileCalls gets all the calls that were made to ReadFile.
	std::vector<PathCall> ReadFileCalls() const {
		std::shared_lock lock(lockReadFile);
		return calls.ReadFile;
	}

	// Realpath calls RealpathFunc.
	std::string Realpath(const std::string& path) override {
		if (!RealpathFunc) {
			TSC_UNREACHABLE(
			    "FSMock.RealpathFunc: method is nil but "
			    "FS.Realpath was just called");
		}
		{
			std::unique_lock lock(lockRealpath);
			calls.Realpath.push_back({path});
		}
		return RealpathFunc(path);
	}

	// RealpathCalls gets all the calls that were made to Realpath.
	std::vector<PathCall> RealpathCalls() const {
		std::shared_lock lock(lockRealpath);
		return calls.Realpath;
	}

	// Remove calls RemoveFunc.
	Error Remove(const std::string& path) override {
		if (!RemoveFunc) {
			TSC_UNREACHABLE(
			    "FSMock.RemoveFunc: method is nil but "
			    "FS.Remove was just called");
		}
		{
			std::unique_lock lock(lockRemove);
			calls.Remove.push_back({path});
		}
		return RemoveFunc(path);
	}

	// RemoveCalls gets all the calls that were made to Remove.
	std::vector<PathCall> RemoveCalls() const {
		std::shared_lock lock(lockRemove);
		return calls.Remove;
	}

	// Stat calls StatFunc.
	std::shared_ptr<FileInfo> Stat(const std::string& path) override {
		if (!StatFunc) {
			TSC_UNREACHABLE(
			    "FSMock.StatFunc: method is nil but FS.Stat was "
			    "just called");
		}
		{
			std::unique_lock lock(lockStat);
			calls.Stat.push_back({path});
		}
		return StatFunc(path);
	}

	// StatCalls gets all the calls that were made to Stat.
	std::vector<PathCall> StatCalls() const {
		std::shared_lock lock(lockStat);
		return calls.Stat;
	}

	// UseCaseSensitiveFileNames calls UseCaseSensitiveFileNamesFunc.
	bool UseCaseSensitiveFileNames() override {
		if (!UseCaseSensitiveFileNamesFunc) {
			TSC_UNREACHABLE(
			    "FSMock.UseCaseSensitiveFileNamesFunc: method is "
			    "nil but FS.UseCaseSensitiveFileNames was just "
			    "called");
		}
		{
			std::unique_lock lock(lockUseCaseSensitiveFileNames);
			calls.UseCaseSensitiveFileNames.push_back({});
		}
		return UseCaseSensitiveFileNamesFunc();
	}

	// UseCaseSensitiveFileNamesCalls gets all calls made to
	// UseCaseSensitiveFileNames.
	std::vector<EmptyCall> UseCaseSensitiveFileNamesCalls() const {
		std::shared_lock lock(lockUseCaseSensitiveFileNames);
		return calls.UseCaseSensitiveFileNames;
	}

	// WriteFile calls WriteFileFunc.
	Error WriteFile(const std::string& path,
	                const std::string& data) override {
		if (!WriteFileFunc) {
			TSC_UNREACHABLE(
			    "FSMock.WriteFileFunc: method is nil but "
			    "FS.WriteFile was just called");
		}
		{
			std::unique_lock lock(lockWriteFile);
			calls.WriteFile.push_back({path, data});
		}
		return WriteFileFunc(path, data);
	}

	// WriteFileCalls gets all the calls that were made to WriteFile.
	std::vector<PathDataCall> WriteFileCalls() const {
		std::shared_lock lock(lockWriteFile);
		return calls.WriteFile;
	}
};

// Wrap wraps a vfs.FS and returns a FSMock which calls it —
// wrapper.go.
inline std::shared_ptr<FSMock> Wrap(vfs::FS* fs) {
	auto mock = std::make_shared<FSMock>();
	mock->DirectoryExistsFunc = [fs](const std::string& p) {
		return fs->DirectoryExists(p);
	};
	mock->FileExistsFunc = [fs](const std::string& p) {
		return fs->FileExists(p);
	};
	mock->GetAccessibleEntriesFunc = [fs](const std::string& p) {
		return fs->GetAccessibleEntries(p);
	};
	mock->ReadFileFunc = [fs](const std::string& p) {
		return fs->ReadFile(p);
	};
	mock->RealpathFunc = [fs](const std::string& p) {
		return fs->Realpath(p);
	};
	mock->RemoveFunc = [fs](const std::string& p) {
		return fs->Remove(p);
	};
	mock->ChtimesFunc = [fs](const std::string& p, TimePoint a,
	                         TimePoint m) { return fs->Chtimes(p, a, m); };
	mock->StatFunc = [fs](const std::string& p) { return fs->Stat(p); };
	mock->UseCaseSensitiveFileNamesFunc = [fs] {
		return fs->UseCaseSensitiveFileNames();
	};
	mock->WriteFileFunc = [fs](const std::string& p,
	                           const std::string& d) {
		return fs->WriteFile(p, d);
	};
	mock->AppendFileFunc = [fs](const std::string& p,
	                            const std::string& d) {
		return fs->AppendFile(p, d);
	};
	return mock;
}

} // namespace tsc::vfs::vfsmock
