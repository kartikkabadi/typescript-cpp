// fuzz_tsconfig — libFuzzer driver for the tsconfig parsing path.
//
// Exercises the public parse-config entries the real compiler uses:
//   NewTsconfigSourceFileFromFilePath  — parse-config-text-to-json
//     (the tsoptions entry named in the task; JSON SourceFile creation)
//   ParseJsonSourceFileConfigFileContent — full option conversion:
//     "extends" chains, wildcard include/exclude matching, enum and
//     option validation, references.
//
// The ParseConfigHost is backed by an in-memory FS that serves the SAME
// fuzz bytes for every file read — an "extends" chain therefore resolves
// to the input itself, exercising circularity detection and recursion
// guards without touching the real filesystem.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tsoptions/tsoptionstest/tsoptionstest.h"
#include "internal/vfs/vfs.h"

#include "cmd/fuzz/fuzz_common.h"

namespace {

// Every file "exists" and reads back the fuzz bytes; every directory
// exists; directory listings return a small fixed set so include/exclude
// wildcard matching has real input to chew on.
struct FuzzFS : tsc::vfs::FS {
	std::string contents;

	bool UseCaseSensitiveFileNames() override { return true; }
	// Bounded existence: with every path "existing", node_modules-style
	// extends resolution produces a fresh deeper path at every level and
	// the recursion never terminates — an artifact of the fake FS, not a
	// product path. In a real FS the chain ends at the first miss.
	static bool plausibleExistingPath(const std::string& path) {
		if (path.size() > 64 || path.empty() || path[0] != '/')
			return false;
		int depth = 0;
		for (char c : path)
			depth += (c == '/');
		return depth <= 6;
	}
	bool FileExists(const std::string& path) override {
		return plausibleExistingPath(path);
	}
	std::pair<std::string, bool> ReadFile(const std::string& path) override {
		if (!plausibleExistingPath(path))
			return {"", false};
		return {contents, true};
	}
	tsc::vfs::Error WriteFile(const std::string&,
	                          const std::string&) override {
		return {};
	}
	tsc::vfs::Error AppendFile(const std::string&,
	                           const std::string&) override {
		return {};
	}
	tsc::vfs::Error Remove(const std::string&) override { return {}; }
	tsc::vfs::Error Chtimes(const std::string&, tsc::vfs::TimePoint,
	                        tsc::vfs::TimePoint) override {
		return {};
	}
	bool DirectoryExists(const std::string& path) override {
		return plausibleExistingPath(path);
	}
	// Leaf-only listings: returning a subdirectory for every directory
	// makes glob walks (include/exclude matching) descend an infinite
	// tree — a driver artifact, not a product path.
	tsc::vfs::Entries GetAccessibleEntries(
	    const std::string&) override {
		return {.files = {"index.ts", "lib.d.ts", "f.json"},
		        .directories = std::vector<std::string>{},
		        .symlinks = std::unordered_set<std::string>{}};
	}
	std::shared_ptr<tsc::vfs::FileInfo> Stat(
	    const std::string&) override {
		return nullptr;
	}
	std::string Realpath(const std::string& path) override { return path; }
};

void freeDiagnostics(const std::vector<tsc::Diagnostic*>& ds) {
	for (auto* d : ds)
		delete d;
}

void run(const uint8_t* data, size_t size) {
	std::string_view text(reinterpret_cast<const char*>(data), size);
	FuzzFS fs;
	fs.contents = std::string(text);
	tsc::tsoptions::tsoptionstest::VfsParseConfigHost host;
	host.Vfs = std::shared_ptr<tsc::vfs::FS>(&fs, [](tsc::vfs::FS*) {});
	host.CurrentDirectory = "/project";

	auto* sf = tsc::tsoptions::NewTsconfigSourceFileFromFilePath(
	    "/project/tsconfig.json", tsc::tspath::Path{"/project/tsconfig.json"},
	    text);
	auto* parsed = tsc::tsoptions::ParseJsonSourceFileConfigFileContent(
	    sf, &host, "/project", nullptr, nullptr, "/project/tsconfig.json",
	    /*resolutionStack*/ {}, /*extendedConfigCache*/ nullptr);
	if (parsed != nullptr) {
		freeDiagnostics(parsed->Errors);
		delete parsed;
	}
	fuzz::releaseSourceFile(sf->SourceFile);
	delete sf;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size == 0) {
		return 0;
	}
	fuzz::runOnBigStack([](const uint8_t* d, size_t n) { run(d, n); }, data,
	                  size);
	return 0;
}
