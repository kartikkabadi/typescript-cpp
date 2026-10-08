// Internal declaration for OutputRecorderFS (recorderfs.go:10) — shared
// between recorderfs.cpp and harnessutil.cpp, which does the Go
// `fs.(*OutputRecorderFS)` type assertion as a dynamic_cast.
#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/vfs/vfs.h"

namespace tsc::testutil::harnessutil {

// OutputRecorderFS — recorderfs.go:10. Go embeds vfs.FS; C++ delegates every
// method to inner, overriding WriteFile and adding Outputs.
struct OutputRecorderFS : vfs::FS {
	std::shared_ptr<vfs::FS> inner;
	std::mutex outputsMut;
	std::unordered_map<std::string, int> outputsMap;
	std::vector<TestFile*> outputs;

	explicit OutputRecorderFS(std::shared_ptr<vfs::FS> fs)
	    : inner(std::move(fs)) {}

	bool UseCaseSensitiveFileNames() override {
		return inner->UseCaseSensitiveFileNames();
	}
	bool FileExists(const std::string& path) override {
		return inner->FileExists(path);
	}
	std::pair<std::string, bool>
	ReadFile(const std::string& path) override {
		return inner->ReadFile(path);
	}
	// WriteFile — recorderfs.go:22.
	vfs::Error WriteFile(const std::string& path,
	                     const std::string& data) override {
		auto err = inner->WriteFile(path, data);
		if (err) {
			return err;
		}
		std::string p = inner->Realpath(path);
		std::lock_guard<std::mutex> lock(outputsMut);
		auto it = outputsMap.find(p);
		if (it != outputsMap.end()) {
			outputs[it->second] = new TestFile{p, data};
		} else {
			int index = (int)outputs.size();
			outputsMap[p] = index;
			outputs.push_back(new TestFile{p, data});
		}
		return vfs::Error{};
	}
	vfs::Error AppendFile(const std::string& path,
	                      const std::string& data) override {
		return inner->AppendFile(path, data);
	}
	vfs::Error Remove(const std::string& path) override {
		return inner->Remove(path);
	}
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint aTime,
	                   vfs::TimePoint mTime) override {
		return inner->Chtimes(path, aTime, mTime);
	}
	bool DirectoryExists(const std::string& path) override {
		return inner->DirectoryExists(path);
	}
	vfs::Entries GetAccessibleEntries(const std::string& path) override {
		return inner->GetAccessibleEntries(path);
	}
	std::shared_ptr<vfs::FileInfo> Stat(const std::string& path) override {
		return inner->Stat(path);
	}
	std::string Realpath(const std::string& path) override {
		return inner->Realpath(path);
	}

	// Outputs — recorderfs.go:38 (slices.Clone).
	std::vector<TestFile*> Outputs() {
		std::lock_guard<std::mutex> lock(outputsMut);
		return outputs; // vector copy == slices.Clone
	}
};

}  // namespace tsc::testutil::harnessutil
