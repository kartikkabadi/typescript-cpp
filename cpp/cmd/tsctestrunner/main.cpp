// tsctestrunner — runs the self-registered ported tsctests scenarios.
//
// Each registered case maps to one Go subtest (tscInput.run). A case
// PASSes when its testing::T never failed — the baseline byte-diff vs
// tsc/testdata/baselines/reference/tsctest/<subFolder>/ is what
// flips it.
//
// Usage:
//   tsctestrunner [-list] [-run <regex>] [--dump-baselines <dir>]
//
//   -run <regex>          run only cases whose name matches (regex_search)
//   -list                 print matching case names and exit
//   --dump-baselines dir  after the run, copy the produced local
//                         baselines (tsc/testdata/baselines/local/tsctest)
//                         into <dir>/ for offline diffing
//
// Exit status: 0 iff every matched case passed.

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "internal/execute/tsctests/tests/registry.h"
#include "internal/gostd/regexp.h"
#include "internal/gostd/testing.h"
#include "internal/repo/paths.h"
#ifdef _WIN32
#include "internal/win32/w32compat.h"
#endif

namespace fs = std::filesystem;

namespace {

void dumpLocalBaselines(const std::string& destRoot) {
	fs::path src =
	    fs::path(tsc::repo::testDataPath()) / "baselines" / "local" /
	    "tsctest";
	std::error_code ec;
	if (!fs::exists(src, ec)) {
		std::cerr << "no local baselines at " << src << "\n";
		return;
	}
	fs::path dest = fs::path(destRoot) / "tsctest";
	fs::remove_all(dest, ec);
	fs::create_directories(dest, ec);
	fs::copy(src, dest,
	         fs::copy_options::recursive |
	             fs::copy_options::overwrite_existing,
	         ec);
	if (ec) {
		std::cerr << "dump-baselines failed: " << ec.message() << "\n";
	}
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
	// Byte-parity with Go: pin stdout/stderr to binary mode.
	w32::setBinaryStdio();
#endif
	std::string runPattern;
	std::string dumpDir;
	bool listOnly = false;
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		auto takeValue = [&](const char* flag) -> std::string {
			if (i + 1 >= argc) {
				std::cerr << flag << " requires a value\n";
				std::exit(2);
			}
			return argv[++i];
		};
		if (arg == "-run" || arg == "--run") {
			runPattern = takeValue("-run");
		} else if (arg.rfind("-run=", 0) == 0) {
			runPattern = arg.substr(5);
		} else if (arg == "-list" || arg == "--list") {
			listOnly = true;
		} else if (arg == "--dump-baselines") {
			dumpDir = takeValue("--dump-baselines");
		} else if (arg.rfind("--dump-baselines=", 0) == 0) {
			dumpDir = arg.substr(17);
		} else {
			std::cerr << "unknown flag: " << arg << "\n";
			return 2;
		}
	}

	std::optional<tsc::gostd::regexp::Regexp> re;
	if (!runPattern.empty()) {
		try {
			re.emplace(runPattern);
		} catch (const std::exception& e) {
			std::cerr << "invalid -run regex: " << e.what() << "\n";
			return 2;
		}
	}

	int passed = 0, failed = 0, skipped = 0;
	std::vector<std::string> failedNames;
	for (const auto& tc : tsc::execute::tsctests::tests::tscTestRegistry()) {
		if (re && !re->MatchString(tc.name)) {
			skipped++;
			continue;
		}
		if (listOnly) {
			std::cout << tc.name << "\n";
			continue;
		}
		tsc::gostd::testing::T t;
		try {
			tc.fn(&t);
		} catch (const std::exception& e) {
			t.Errorf("uncaught exception: %s", {e.what()});
		} catch (...) {
			t.Error({"uncaught non-standard exception"});
		}
		if (t.Failed()) {
			failed++;
			failedNames.push_back(tc.name);
			std::cout << "FAIL " << tc.name << "\n";
		} else {
			passed++;
			std::cout << "PASS " << tc.name << "\n";
		}
	}

	if (!listOnly) {
		std::cout << "\n"
		          << passed << " passed, " << failed << " failed, "
		          << skipped << " skipped\n";
	}

	if (!dumpDir.empty()) {
		dumpLocalBaselines(dumpDir);
	}

	return failed == 0 ? 0 : 1;
}
