// repo — paths.go:1-52
//
// runtime.Caller(0) → __FILE__ of this translation unit.
// Layout adaptation: the Go file lives at tsc/internal/repo/paths.go, so
// walking up finds tsc/go.mod. The C++ file lives at cpp/internal/repo/,
// so in addition to "<dir>/go.mod" (the faithful check) we also accept
// "<dir>/tsc/go.mod" and return "<dir>/tsc" — which yields the same
// repository root the Go code resolves to.

#include <filesystem>
#include <string>
#include <string_view>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/repo/paths.h"

namespace tsc::repo {

namespace {

namespace fs = std::filesystem;

std::string computeRootPath() {
    std::string filename = __FILE__; // runtime.Caller always returns forward slashes

    if (filename.rfind("github.com/", 0) == 0) {
        tscUnreachable("repo root cannot be found when built with -trimpath");
    }

    fs::path filePath(filename);
    if (!filePath.is_absolute()) {
        // filepath.IsAbs check → absolutize via the CWD (the Go build always
        // embeds absolute paths; keep the panic reachable via no absolute
        // resolution rather than aborting).
        std::error_code ec;
        fs::path abs = fs::absolute(filePath, ec);
        if (ec || !abs.is_absolute()) {
            tscUnreachable((filename + " is not an absolute path").c_str());
        }
        filePath = abs;
    }
    filePath = filePath.lexically_normal();

    const fs::path root = filePath.root_path();

    fs::path dir = filePath.parent_path();
    for (;;) {
        std::error_code ec;
        if (fs::exists(dir / "go.mod", ec)) {
            return dir.generic_string();
        }
        // C++-tree adaptation: tsc/go.mod marks the Go module root.
        if (fs::exists(dir / "tsc" / "go.mod", ec)) {
            return (dir / "tsc").generic_string();
        }
        if (dir == root) {
            break;
        }
        dir = dir.parent_path();
    }

    tscUnreachable(("could not find go.mod above " + filename).c_str());
}

std::string computeTestDataPath() {
    return (fs::path(rootPath()) / "testdata").generic_string();
}

} // namespace

std::string rootPath() {
    // sync.OnceValue
    static const std::string cached = computeRootPath();
    return cached;
}

std::string testDataPath() {
    // sync.OnceValue
    static const std::string cached = computeTestDataPath();
    return cached;
}

} // namespace tsc::repo
