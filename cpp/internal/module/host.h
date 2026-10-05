// module/host.h — std::filesystem-backed ResolutionHost (replaces Go's
// vfs.FS for the real-system resolver).
#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "internal/module/types.h"
#include "internal/tspath/tspath.h"

namespace tsc::module {

// SystemResolutionHost — ResolutionHost over the real filesystem.
// Case sensitivity mirrors vfs's host detection: case-sensitive on Linux
// ext4 (the default; pass `false` for case-insensitive filesystems).
class SystemResolutionHost : public ResolutionHost {
public:
	explicit SystemResolutionHost(bool useCaseSensitiveFileNames = true)
	    : useCaseSensitiveFileNames_(useCaseSensitiveFileNames) {}

	bool FileExists(std::string_view path) override {
		std::error_code ec;
		// vfs.FileExists: path exists and is a regular file (symlinks
		// followed).
		return std::filesystem::is_regular_file(
		    std::filesystem::path(path), ec);
	}

	bool DirectoryExists(std::string_view path) override {
		std::error_code ec;
		return std::filesystem::is_directory(
		    std::filesystem::path(path), ec);
	}

	std::optional<std::string> ReadFile(std::string_view path) override {
		std::ifstream f(std::string{path}, std::ios::binary);
		if (!f) {
			return std::nullopt;
		}
		std::ostringstream ss;
		ss << f.rdbuf();
		if (f.bad()) {
			return std::nullopt;
		}
		return ss.str();
	}

	std::string Realpath(std::string_view path) override {
		std::error_code ec;
		auto p = std::filesystem::weakly_canonical(
		    std::filesystem::path(path), ec);
		if (ec || p.empty()) {
			// vfs falls back to the input when it can't resolve.
			return std::string{path};
		}
		return p.generic_string();
	}

	std::string GetCurrentDirectory() override {
		std::error_code ec;
		auto p = std::filesystem::current_path(ec);
		if (ec) {
			return "";
		}
		return tspath::normalizePath(p.generic_string());
	}

	bool UseCaseSensitiveFileNames() override {
		return useCaseSensitiveFileNames_;
	}

	AccessibleEntries GetAccessibleEntries(
	    std::string_view path) override {
		AccessibleEntries entries;
		std::error_code ec;
		auto dir = std::filesystem::path(path);
		std::filesystem::directory_iterator it(dir, ec);
		if (ec) {
			return entries;
		}
		// Track symlinks (vfs reports them when it can).
		entries.symlinks.emplace();
		std::filesystem::directory_iterator end;
		for (; it != end; it.increment(ec)) {
			if (ec) break;
			const auto& de = *it;
			auto name = de.path().filename().generic_string();
			std::error_code sec;
			if (de.is_symlink()) {
				entries.symlinks->insert(name);
			}
			// is_directory/is_regular_file follow symlinks, matching
			// vfs.GetAccessibleEntries ("if any entry is a symlink, it
			// will be followed").
			if (de.is_directory(sec)) {
				entries.directories.push_back(name);
			} else if (de.is_regular_file(sec)) {
				entries.files.push_back(name);
			}
		}
		return entries;
	}

private:
	bool useCaseSensitiveFileNames_;
};

}  // namespace tsc::module
