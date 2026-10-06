// Port of tsc/internal/testutil/baseline: baseline.go + testmain.go.
#include "internal/testutil/baseline/baseline.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "internal/collections/collections.h"
#include "internal/patience/patience.h"
#include "internal/repo/paths.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::testutil::baseline {

namespace {

// localRoot / referenceRoot — baseline.go:75. Go resolves them via
// filepath.Join at package-init time; TestDataPath() is identical each call.
std::string localRoot() {
	return tspath::combinePaths(repo::testDataPath(), {"baselines", "local"});
}
std::string referenceRoot() {
	return tspath::combinePaths(repo::testDataPath(),
	                            {"baselines", "reference"});
}

// writeComparison — baseline.go:39.
void writeComparison(gostd::testing::T* t, const std::string& actualContent,
                     const std::string& local, const std::string& reference) {
	if (actualContent.empty()) {
		throw std::runtime_error(
		    "the generated content was \"\". Return 'baseline.NoContent' if no "
		    "baselining is required.");
	}
	std::error_code ec;
	std::filesystem::create_directories(tspath::getDirectoryPath(local), ec);
	if (ec) {
		t->Error({"failed to create directories for the local baseline file " +
		          local + ": " + ec.message()});
		return;
	}
	if (std::filesystem::exists(local, ec)) {
		std::filesystem::remove(local, ec);
		if (ec) {
			t->Error({"failed to remove the local baseline file " + local +
			          ": " + ec.message()});
			return;
		}
	}

	std::string expected{NoContent};
	bool foundExpected = false;
	{
		std::ifstream in(reference, std::ios::binary);
		if (in) {
			std::ostringstream ss;
			ss << in.rdbuf();
			expected = ss.str();
			foundExpected = true;
		}
	}
	if (expected == actualContent &&
	    !(actualContent == NoContent && foundExpected)) {
		return;
	}
	auto writeFile = [&](const std::string& path, const std::string& data) {
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out) {
			return false;
		}
		out << data;
		return out.good();
	};
	if (actualContent == NoContent) {
		if (!writeFile(local + ".delete", "")) {
			t->Error({"failed to write the local baseline file " + local +
			          ".delete"});
		}
		return;
	}
	if (!writeFile(local, actualContent)) {
		t->Error({"failed to write the local baseline file " + local});
		return;
	}
	if (!foundExpected) {
		t->Errorf("new baseline created at %s.", {local});
		return;
	}
	t->Errorf("the baseline file %s has changed. (Run `hereby "
	          "baseline-accept` if the new baseline is correct.)",
	          {reference});
}

// --- testmain.go ---

// recordedBaselines — testmain.go:18.
collections::SyncSet<std::string>& recordedBaselines() {
	static collections::SyncSet<std::string> set;
	return set;
}

// trackingInitialized — testmain.go:21.
std::atomic<bool>& trackingInitialized() {
	static std::atomic<bool> v{false};
	return v;
}

// trackingDir — testmain.go:26. Go reads the env at package init.
const std::string& trackingDir() {
	static const std::string dir = [] {
		const char* v = std::getenv("TSGO_BASELINE_TRACKING_DIR");
		return v ? std::string(v) : std::string();
	}();
	return dir;
}

// recordBaseline — testmain.go:61.
void recordBaseline(gostd::testing::T* t, const std::string& relativePath) {
	if (!trackingDir().empty()) {
		if (!trackingInitialized()) {
			t->Error({"baseline: package uses baselines but TestMain did not "
			          "call baseline.Track(). Please add a TestMain function "
			          "with: defer baseline.Track()()"});
			return;
		}
		recordedBaselines().Add(relativePath);
	}
}

// doWriteRecordedBaselines — testmain.go:87.
bool doWriteRecordedBaselines(const std::string& trackingPath) {
	std::ofstream f(trackingPath, std::ios::binary | std::ios::trunc);
	if (!f) return false;
	bool ok = true;
	recordedBaselines().Range([&](const std::string& baseline) {
		f << baseline << '\n';
		if (!f) ok = false;
		return ok;
	});
	return ok;
}

// writeRecordedBaselines — testmain.go:76.
void writeRecordedBaselines(const std::string& trackingPath) {
	if (recordedBaselines().Size() == 0) {
		return;
	}
	if (!doWriteRecordedBaselines(trackingPath)) {
		fprintf(stderr,
		        "baseline: failed to write tracking file %s: %s\n",
		        trackingPath.c_str(), "write failed");
		std::exit(1);
	}
}

// fnv64a — hash/fnv New64a.
uint64_t fnv64a(std::string_view s) {
	uint64_t h = 14695981039346656037ull;
	for (unsigned char c : s) {
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

}  // namespace

// Run — baseline.go:23.
void Run(gostd::testing::T* t, const std::string& fileName,
         const std::string& actual, const Options& opts) {
	const std::string& subfolder = opts.Subfolder;
	std::string localPath =
	    tspath::combinePaths(localRoot(), {subfolder, fileName});
	std::string referencePath =
	    tspath::combinePaths(referenceRoot(), {subfolder, fileName});
	recordBaseline(t, tspath::combinePaths(subfolder, {fileName}));
	writeComparison(t, actual, localPath, referencePath);
}

// DiffText — baseline.go:31.
std::string DiffText(const std::string& oldName, const std::string& newName,
                     const std::string& expected, const std::string& actual) {
	auto lines = patience::Diff(splitLines(expected),
	                            splitLines(actual));
	return patience::UnifiedDiffTextWithOptions(
	    lines, patience::UnifiedDiffOptions{/*.Precontext*/ 3,
	                                        /*.Postcontext*/ 3,
	                                        /*.SrcHeader*/ oldName,
	                                        /*.DstHeader*/ newName});
}

// Track — testmain.go:36. runtime.Callers' per-package call-stack hash is
// approximated: the C++ build has a single test binary, so the FNV-1a hash
// is computed over a fixed package marker instead of the caller frames.
std::function<void()> Track() {
	trackingInitialized() = true;

	if (trackingDir().empty()) {
		return []() {};
	}

	uint64_t h = fnv64a("tsc.internal.testutil.baseline.track");
	char name[17];
	snprintf(name, sizeof(name), "%016lx", h);
	std::string trackingPath =
	    tspath::combinePaths(trackingDir(), {std::string(name) + ".txt"});

	return [trackingPath]() { writeRecordedBaselines(trackingPath); };
}

}  // namespace tsc::testutil::baseline
