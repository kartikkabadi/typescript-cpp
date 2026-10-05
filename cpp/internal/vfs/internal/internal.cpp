// internal.cpp — port of tsc/internal/vfs/internal/internal.go
#include "internal/vfs/internal/internal.h"

#include <cstdint>
#include <string_view>

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/tspath/tspath.h"

namespace tsc::vfs::internal {

// decodeBytes — internal.go:157-173: strips UTF-8 BOM, decodes UTF-16 BOMs.
std::pair<std::string, bool> decodeBytes(const std::string& s);

// RootLength — internal.go:20-28
int rootLength(const std::string& p) {
	int l = tspath::getEncodedRootLength(p);
	if (l == 0) {
		TSC_UNREACHABLE(("vfs: path \"" + p + "\" is not absolute").c_str());
	} else if (l < 0) {
		return ~l;
	}
	return l;
}

// SplitPath — internal.go:30-36
std::pair<std::string, std::string> splitPath(const std::string& p) {
	auto normalized = tspath::normalizePath(p);
	int l = rootLength(normalized);
	std::string rootName = normalized.substr(0, l);
	std::string rest{tspath::removeTrailingDirectorySeparator(
	    normalized.substr(l))};
	return {rootName, rest};
}

// RootAndPath — internal.go:38-44
std::tuple<std::shared_ptr<IoFS>, std::string, std::string>
Common::RootAndPath(const std::string& path) const {
	auto [rootName, rest] = splitPath(path);
	if (rest.empty()) {
		rest = ".";
	}
	return {RootFor(rootName), rootName, rest};
}

// Stat — internal.go:46-56
std::shared_ptr<FileInfo> Common::Stat(const std::string& path) const {
	auto [fsys, rootName, rest] = RootAndPath(path);
	if (!fsys) {
		return nullptr;
	}
	auto [stat, err] = fsStat(fsys, rest);
	if (err) {
		return nullptr;
	}
	return stat;
}

// FileExists — internal.go:58-61
bool Common::FileExists(const std::string& path) const {
	auto stat = Stat(path);
	return stat && !stat->IsDir();
}

// DirectoryExists — internal.go:63-66
bool Common::DirectoryExists(const std::string& path) const {
	auto stat = Stat(path);
	return stat && stat->IsDir();
}

// GetAccessibleEntries — internal.go:68-115
Entries Common::GetAccessibleEntries(const std::string& path) const {
	Entries result;
	result.symlinks.emplace();

	auto addToResult = [&](const std::string& name, FileMode mode,
	                       bool isLink) -> bool {
		if (mode.IsDir()) {
			result.directories.push_back(name);
		} else if (mode.IsRegular()) {
			result.files.push_back(name);
		} else {
			return false;
		}

		if (isLink) {
			result.symlinks->insert(name);
		}
		return true;
	};

	for (auto& entry : getEntries(path)) {
		auto entryType = entry->Type();

		if (addToResult(entry->Name(), entryType, /*isLink*/ false)) {
			continue;
		}

		if ((entryType & ModeSymlink).v != 0) {
			// Easy case; UNIX-like system will clearly mark symlinks.
			if (auto stat = Stat(path + "/" + entry->Name()); stat) {
				addToResult(entry->Name(), stat->Mode(), true);
			}
			continue;
		}

		if ((entryType & ModeIrregular).v != 0 && IsReparsePoint) {
			// Could be a Windows junction or other reparse point.
			// Check using the OS-specific helper.
			auto fullPath = path + "/" + entry->Name();
			if (IsReparsePoint(fullPath)) {
				if (auto stat = Stat(fullPath); stat) {
					addToResult(entry->Name(), stat->Mode(), true);
				}
			}
			continue;
		}
	}

	return result;
}

// getEntries — internal.go:117-129
std::vector<std::shared_ptr<DirEntry>> Common::getEntries(
    const std::string& path) const {
	auto [fsys, rootName, rest] = RootAndPath(path);
	if (!fsys) {
		return {};
	}

	auto [entries, err] = fsReadDir(fsys, rest);
	if (err) {
		return {};
	}

	return entries;
}

// ReadFile — internal.go:131-155
std::pair<std::string, bool> Common::ReadFile(
    const std::string& path) const {
	auto [fsys, rootName, rest] = RootAndPath(path);
	if (!fsys) {
		return {"", false};
	}

	auto [b, err] = fsReadFile(fsys, rest);
	if (err) {
		return {"", false};
	}

	// An invariant of any underlying filesystem is that the bytes returned
	// are immutable, otherwise anyone using the filesystem would end up
	// with data races.
	if (b.empty()) {
		return {"", true};
	}

	return decodeBytes(b);
}

} // namespace tsc::vfs::internal

namespace {

using namespace tsc;
using tsc::vfs::Error;

// decodeUtf16 — internal.go:175-181: decode UTF-16 (per `order`) to UTF-8;
// unpaired surrogates become U+FFFD (utf16.Decode).
static std::string decodeUtf16(std::string_view s, bool littleEndian) {
	std::vector<char16_t> ints(s.size() / 2);
	for (size_t i = 0; i < ints.size(); i++) {
		auto lo = static_cast<unsigned char>(s[2 * i]);
		auto hi = static_cast<unsigned char>(s[2 * i + 1]);
		ints[i] = littleEndian ? static_cast<char16_t>(lo | (hi << 8))
		                       : static_cast<char16_t>((lo << 8) | hi);
	}

	// utf16.Decode: each surrogate pair -> rune; lone surrogate -> U+FFFD.
	std::string out;
	for (size_t i = 0; i < ints.size(); i++) {
		char16_t u = ints[i];
		char32_t r;
		if (u >= 0xD800 && u < 0xDC00 && i + 1 < ints.size() &&
		    ints[i + 1] >= 0xDC00 && ints[i + 1] < 0xE000) {
			r = 0x10000 + ((static_cast<char32_t>(u) - 0xD800) << 10) +
			    (static_cast<char32_t>(ints[i + 1]) - 0xDC00);
			i++;
		} else if (u >= 0xD800 && u < 0xE000) {
			r = 0xFFFD;
		} else {
			r = u;
		}
		char buf[4];
		int n = encodeUtf8Rune(r, buf);
		out.append(buf, static_cast<size_t>(n));
	}
	return out;
}

} // namespace

namespace tsc::vfs::internal {

// decodeBytes — internal.go:157-173: strips UTF-8 BOM, decodes UTF-16 BOMs.
std::pair<std::string, bool> decodeBytes(const std::string& s) {
	if (s.size() >= 2) {
		auto b0 = static_cast<unsigned char>(s[0]);
		auto b1 = static_cast<unsigned char>(s[1]);
		if (b0 == 0xFF && b1 == 0xFE) {
			return {decodeUtf16(std::string_view(s).substr(2),
			                    /*littleEndian*/ true),
			        true};
		}
		if (b0 == 0xFE && b1 == 0xFF) {
			return {decodeUtf16(std::string_view(s).substr(2),
			                    /*littleEndian*/ false),
			        true};
		}
	}
	if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
	    static_cast<unsigned char>(s[1]) == 0xBB &&
	    static_cast<unsigned char>(s[2]) == 0xBF) {
		return {s.substr(3), true};
	}

	return {s, true};
}

} // namespace tsc::vfs::internal
