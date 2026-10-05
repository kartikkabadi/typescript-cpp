// --- host.go — program slice ---
// Real FS-backed CompilerHost. The `bundled:///libs/<name>` namespace reads
// from <bundledLibsRoot>/<name> (tsc/internal/bundled/libs on disk) while
// keeping the bundled:/// name on the SourceFile — matching the Go oracle's
// bundled.WrapFS naming.
#include "internal/compiler/program.h"
#include "internal/parser/parser.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace tsc::compiler {

// vfs/internal/internal.go: decodeUtf16 — UTF-16 (LE/BE) → UTF-8 with
// surrogate-pair handling, matching Go's utf16.Decode semantics.
static std::string decodeUtf16(std::string_view s, bool littleEndian) {
	std::string out;
	auto u16 = [&](size_t i) -> char32_t {
		auto lo = static_cast<unsigned char>(s[2 * i]);
		auto hi = static_cast<unsigned char>(s[2 * i + 1]);
		uint16_t v = littleEndian ? static_cast<uint16_t>(lo | (hi << 8))
		                          : static_cast<uint16_t>(hi | (lo << 8));
		return static_cast<char32_t>(v);
	};
	size_t n = s.size() / 2;
	for (size_t i = 0; i < n; i++) {
		char32_t r = u16(i);
		if (r >= 0xD800 && r <= 0xDBFF && i + 1 < n) {
			char32_t low = u16(i + 1);
			if (low >= 0xDC00 && low <= 0xDFFF) {
				r = 0x10000 + ((r - 0xD800) << 10) + (low - 0xDC00);
				i++;
			} else {
				r = 0xFFFD;
			}
		} else if (r >= 0xD800 && r <= 0xDFFF) {
			r = 0xFFFD;
		}
		out += utf8String(r);
	}
	return out;
}

// vfs/internal/internal.go: decodeBytes — strips UTF-8 BOM, decodes
// UTF-16 LE/BE BOMs.
static std::string decodeBytes(std::string s) {
	if (s.size() >= 2) {
		unsigned char b0 = s[0], b1 = s[1];
		if (b0 == 0xFF && b1 == 0xFE)
			return decodeUtf16(std::string_view(s).substr(2), true);
		if (b0 == 0xFE && b1 == 0xFF)
			return decodeUtf16(std::string_view(s).substr(2), false);
	}
	if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
	    static_cast<unsigned char>(s[1]) == 0xBB &&
	    static_cast<unsigned char>(s[2]) == 0xBF) {
		s.erase(0, 3);
	}
	return s;
}

static constexpr std::string_view kBundledLibsPrefix = "bundled:///libs/";

std::string CompilerHost::bundledPath(std::string_view fileName) const {
	if (fileName.starts_with(kBundledLibsPrefix) &&
	    !bundledLibsRoot.empty()) {
		return bundledLibsRoot + "/" +
		       std::string(fileName.substr(kBundledLibsPrefix.size()));
	}
	return {};
}

bool CompilerHost::FileExists(std::string_view fileName) {
	if (auto p = bundledPath(fileName); !p.empty()) {
		struct stat st;
		return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
	}
	struct stat st;
	return ::stat(std::string(fileName).c_str(), &st) == 0 &&
	       S_ISREG(st.st_mode);
}

bool CompilerHost::DirectoryExists(std::string_view directory) {
	struct stat st;
	return ::stat(std::string(directory).c_str(), &st) == 0 &&
	       S_ISDIR(st.st_mode);
}

std::optional<std::string> CompilerHost::ReadFile(std::string_view fileName) {
	std::string p;
	if (auto bp = bundledPath(fileName); !bp.empty())
		p = std::move(bp);
	else
		p = std::string(fileName);
	std::ifstream f(p, std::ios::binary);
	if (!f)
		return std::nullopt;
	std::ostringstream ss;
	ss << f.rdbuf();
	return decodeBytes(ss.str());
}

// vfs WriteFile — OS fs write. Go returns error; std::nullopt == nil.
std::optional<std::string> CompilerHost::WriteFile(std::string_view fileName,
                                                   std::string_view text) {
	std::ofstream f(std::string(fileName),
	                std::ios::binary | std::ios::trunc);
	if (!f) {
		return "open " + std::string(fileName) + ": " +
		       std::string(std::strerror(errno));
	}
	f.write(text.data(), static_cast<std::streamsize>(text.size()));
	if (!f) {
		return "write " + std::string(fileName) + ": " +
		       std::string(std::strerror(errno));
	}
	return std::nullopt;
}

std::string CompilerHost::Realpath(std::string_view path) {
	char buf[PATH_MAX];
	if (auto bp = bundledPath(path); !bp.empty()) {
		if (::realpath(bp.c_str(), buf) != nullptr)
			return buf;
		return bp;
	}
	if (::realpath(std::string(path).c_str(), buf) != nullptr)
		return buf;
	return std::string(path);
}

module::ResolutionHost::AccessibleEntries CompilerHost::GetAccessibleEntries(
    std::string_view directory) {
	AccessibleEntries result;
	std::string dir = std::string(directory);
	DIR* d = ::opendir(dir.c_str());
	if (!d)
		return result;
	while (dirent* e = ::readdir(d)) {
		if (e->d_name[0] == '.')
			continue;
		std::string full = dir + "/" + e->d_name;
		struct stat st;
		if (::lstat(full.c_str(), &st) == 0 && S_ISLNK(st.st_mode)) {
			if (!result.symlinks) result.symlinks.emplace();
			result.symlinks->emplace(e->d_name);
			continue;
		}
		if (::stat(full.c_str(), &st) == 0) {
			if (S_ISDIR(st.st_mode))
				result.directories.emplace_back(e->d_name);
			else if (S_ISREG(st.st_mode))
				result.files.emplace_back(e->d_name);
		}
	}
	::closedir(d);
	return result;
}

// host.go: GetSourceFile — ReadFile + ParseSourceFile.
SourceFile* CompilerHost::GetSourceFile(const SourceFileParseOptions& opts,
                                      SourceFileMetaData metaData) {
	auto text = ReadFile(opts.FileName);
	if (!text)
		return nullptr;
	SourceFile* file = parseSourceFile(
	    opts, *text, ensureScriptKindFromFileName(opts.FileName));
	(void)metaData; // metaData is tracked in the program's sourceFileMetaDatas
	return file;
}

}  // namespace tsc::compiler
