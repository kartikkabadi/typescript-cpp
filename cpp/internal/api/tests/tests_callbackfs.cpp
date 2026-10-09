// Port of tsc/internal/api/callbackfs_test.go (package api).
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/api/callbackfs.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace api = tsc::api;
namespace gostd = tsc::gostd;
namespace ipc = tsc::ipc;
namespace json = tsc::json;
namespace vfstest = tsc::vfs::vfstest;

// callbackTestConn — callbackfs_test.go:15.
struct callbackTestConn : ipc::Conn {
	std::unordered_map<std::string, json::Value> responses;

	gostd::Error Run(gostd::Context) override { return nullptr; }

	std::pair<json::Value, gostd::Error>
	Call(gostd::Context, std::string_view method,
	     const json::Value&) override {
		auto it = responses.find(std::string(method));
		if (it == responses.end()) {
			return {json::Value{}, nullptr};
		}
		return {it->second, nullptr};
	}

	gostd::Error Notify(gostd::Context, std::string_view,
	                    const json::Value&) override {
		return nullptr;
	}
};

// nilError — assert.NilError for vfs::Error (nil means success).
void nilError(T* t, const tsc::vfs::Error& err) {
	t->Helper();
	if (err) {
		t->Fatalf("assert.NilError failed: %s", {err.str()});
	}
}

// assertPanicsWith — callbackfs_test.go:264.
void assertPanicsWith(T* t, const char* expected,
                      const std::function<void()>& cb) {
	try {
		cb();
	} catch (const std::exception& e) {
		if (std::string(e.what()).find(expected) == std::string::npos) {
			t->Fatalf("panic = %q, want substring %q",
			          {e.what(), expected});
		}
		return;
	}
	t->Fatal({"expected panic"});
}

} // namespace

// TestCallbackFSDefaults — callbackfs_test.go:31.
void TestCallbackFSDefaults(T* t) {
	t->Parallel();
	auto base = vfstest::FromMap({{"/file.ts", "content"}}, false);
	auto fs = api::newCallbackFS(
	    base, {"realpath:identity", "stat:fakeStat"}, true);

	assert::Assert(t, fs->UseCaseSensitiveFileNames(),
	               "expected configured case sensitivity");
	assert::Equal<std::string>(t, fs->Realpath("/file.ts"), "/file.ts");
	auto info = fs->Stat("/file.ts");
	assert::Assert(t, info != nullptr && !info->IsDir() && info->Size() == 0,
	               "Stat(file) should be an inferred file");
	info = fs->Stat("/");
	assert::Assert(t, info != nullptr && info->IsDir(),
	               "Stat(directory) should be an inferred directory");
	assert::Equal(t, fs->Stat("/missing") == nullptr, true);
}

// TestCallbackFSStatAndEntries — callbackfs_test.go:57.
void TestCallbackFSStatAndEntries(T* t) {
	t->Parallel();
	auto base = vfstest::FromMap({}, true);
	auto fs = api::newCallbackFS(base, {"stat", "getAccessibleEntries"},
	                           std::nullopt);
	auto conn = std::make_shared<callbackTestConn>();
	conn->responses = {
	    {api::callbackStat,
	     json::Value(std::string(R"({"kind":"value","value":{"mode":33060,"size":12,"mtime":"2024-01-02T03:04:05.000Z"}})"))},
	    {api::callbackGetAccessibleEntries,
	     json::Value(std::string(R"({"kind":"value","value":{"files":["link.ts"],"directories":["pkg"],"symlinks":["link.ts","pkg"]}})"))},
	};
	fs->SetConnection(gostd::contextBackground(), conn);

	auto info = fs->Stat("/link.ts");
	assert::Assert(t, info != nullptr && !info->IsDir() && info->Size() == 12,
	               "Stat() should return callback file metadata");
	assert::Equal(t, info->Mode().v, 0444u);
	// 2024-01-02T03:04:05Z
	assert::Equal(t, info->ModTime() ==
	                     tsc::vfs::TimePoint(std::chrono::seconds(1704164645)),
	              true);

	conn->responses[api::callbackStat] =
	    json::Value(std::string(R"({"kind":"missing"})"));
	assert::Equal(t, fs->Stat("/missing.ts") == nullptr, true);

	auto entries = fs->GetAccessibleEntries("/");
	assert::Assert(t, entries.symlinks.has_value() &&
	                      entries.symlinks->contains("link.ts") &&
	                      entries.symlinks->contains("pkg"),
	               "expected symlink metadata for file and directory");

	conn->responses[api::callbackGetAccessibleEntries] = json::Value(
	    std::string(R"({"kind":"value","value":{"files":[],"directories":["src"],"symlinks":[]}})"));
	entries = fs->GetAccessibleEntries("/");
	assert::Assert(t, entries.symlinks.has_value(),
	               "explicitly empty symlink metadata was treated as "
	               "unavailable");
}

// TestNodeFileModeToGoFileMode — callbackfs_test.go:101.
void TestNodeFileModeToGoFileMode(T* t) {
	t->Parallel();
	struct tc {
		const char* name;
		uint32_t node;
		uint32_t goMode;
	};
	const tc tests[] = {
	    {"directory", 0040755u, tsc::vfs::FileMode::kDir | 0755u},
	    {"regular", 0100644u, 0644u},
	    {"symlink", 0120777u, tsc::vfs::FileMode::kSymlink | 0777u},
	    {"fifo", 0010600u, tsc::vfs::FileMode::kNamedPipe | 0600u},
	    {"socket", 0140600u, tsc::vfs::FileMode::kSocket | 0600u},
	    {"setuid", 0104755u, tsc::vfs::FileMode::kSetuid | 0755u},
	};
	for (const auto& test : tests) {
		t->Run(test.name, [test](T* t) {
			t->Parallel();
			auto got = api::nodeFileModeToGoFileMode(test.node);
			assert::Equal(t, got.v, test.goMode);
		});
	}
}

// TestCallbackFSWriteFilePassthrough — callbackfs_test.go:126.
void TestCallbackFSWriteFilePassthrough(T* t) {
	t->Parallel();
	auto base = vfstest::FromMap({}, true);
	auto fs = api::newCallbackFS(base, {"writeFile"}, std::nullopt);
	auto conn = std::make_shared<callbackTestConn>();
	conn->responses = {{api::callbackWriteFile,
	                    json::Value(std::string(R"({"kind":"useOS"})"))}};
	fs->SetConnection(gostd::contextBackground(), conn);

	nilError(t, fs->WriteFile("/use-os.ts", "content"));
	auto [content, ok] = base->ReadFile("/use-os.ts");
	assert::Assert(t, ok && content == "content",
	               "want OS filesystem content");

	conn->responses[api::callbackWriteFile] =
	    json::Value(std::string(R"({"kind":"value"})"));
	nilError(t, fs->WriteFile("/handled.ts", "content"));
	assert::Equal(t, base->ReadFile("/handled.ts").second, false);

	conn->responses[api::callbackWriteFile] =
	    json::Value(std::string(R"({"kind":"noop"})"));
	nilError(t, fs->WriteFile("/noop.ts", "content"));
	assert::Equal(t, base->ReadFile("/noop.ts").second, false);
}

// TestCallbackFSWriteFileNoop — callbackfs_test.go:158.
void TestCallbackFSWriteFileNoop(T* t) {
	t->Parallel();
	auto base = vfstest::FromMap({}, true);
	auto fs = api::newCallbackFS(base, {"writeFile:noop"}, std::nullopt);
	nilError(t, fs->WriteFile("/ignored.ts", "content"));
	assert::Equal(t, base->ReadFile("/ignored.ts").second, false);
}

// TestCallbackFSPerCallFakeStat — callbackfs_test.go:172.
void TestCallbackFSPerCallFakeStat(T* t) {
	t->Parallel();
	auto base = vfstest::FromMap({}, true);
	auto fs = api::newCallbackFS(
	    base, {"stat", "directoryExists", "fileExists"}, std::nullopt);
	auto conn = std::make_shared<callbackTestConn>();
	conn->responses = {
	    {api::callbackStat,
	     json::Value(std::string(R"({"kind":"fakeStat"})"))},
	    {api::callbackDirectoryExists,
	     json::Value(std::string(R"({"kind":"value","value":false})"))},
	    {api::callbackFileExists,
	     json::Value(std::string(R"({"kind":"value","value":true})"))},
	};
	fs->SetConnection(gostd::contextBackground(), conn);

	auto info = fs->Stat("/virtual.ts");
	assert::Assert(t, info != nullptr && !info->IsDir(),
	               "Stat() should return fake file stat");
}

// TestCallbackFSPerCallIdentityRealpath — callbackfs_test.go:191.
void TestCallbackFSPerCallIdentityRealpath(T* t) {
	t->Parallel();
	auto base = vfstest::FromMap({}, true);
	auto fs = api::newCallbackFS(base, {"realpath"}, std::nullopt);
	auto conn = std::make_shared<callbackTestConn>();
	conn->responses = {{api::callbackRealpath,
	                    json::Value(std::string(R"({"kind":"identity"})"))}};
	fs->SetConnection(gostd::contextBackground(), conn);
	assert::Equal<std::string>(t, fs->Realpath("/virtual.ts"),
	                         "/virtual.ts");
}

// TestCallbackFSError — callbackfs_test.go:207.
void TestCallbackFSError(T* t) {
	t->Parallel();
	const char* names[] = {
	    api::callbackReadFile.data(),          api::callbackFileExists.data(),
	    api::callbackDirectoryExists.data(),   api::callbackGetAccessibleEntries.data(),
	    api::callbackRealpath.data(),          api::callbackStat.data(),
	    api::callbackWriteFile.data(),         api::callbackRemoveFile.data(),
	};
	std::vector<std::string> callbacks;
	for (auto* name : names) {
		callbacks.push_back(std::string(name) + ":error");
	}
	auto base = vfstest::FromMap({}, true);
	auto fs = api::newCallbackFS(base, callbacks, std::nullopt);
	for (auto* name : names) {
		assert::Assert(t, fs->errorCallbacks.contains(name),
		               "callback was not configured to panic");
	}
	assertPanicsWith(t, "serverFS.error: readFile",
	                 [&] { fs->ReadFile("/unexpected.ts"); });

	auto callbackFs = api::newCallbackFS(base, {"fileExists"}, std::nullopt);
	auto conn = std::make_shared<callbackTestConn>();
	conn->responses = {{api::callbackFileExists,
	                    json::Value(std::string(R"({"kind":"error"})"))}};
	callbackFs->SetConnection(gostd::contextBackground(), conn);
	assertPanicsWith(t, "serverFS.error: fileExists",
	                 [&] { callbackFs->FileExists("/unexpected.ts"); });
}

// TestCallbackFSRemoveFileNoop — callbackfs_test.go:247.
void TestCallbackFSRemoveFileNoop(T* t) {
	t->Parallel();
	auto base = vfstest::FromMap({{"/retained.ts", "content"}}, true);
	auto fs = api::newCallbackFS(base, {"removeFile:noop"}, std::nullopt);
	nilError(t, fs->Remove("/retained.ts"));
	assert::Equal(t, base->ReadFile("/retained.ts").second, true);
}

REGISTER_UNIT_TEST("api.TestCallbackFSDefaults", TestCallbackFSDefaults);
REGISTER_UNIT_TEST("api.TestCallbackFSStatAndEntries",
                   TestCallbackFSStatAndEntries);
REGISTER_UNIT_TEST("api.TestNodeFileModeToGoFileMode",
                   TestNodeFileModeToGoFileMode);
REGISTER_UNIT_TEST("api.TestCallbackFSWriteFilePassthrough",
                   TestCallbackFSWriteFilePassthrough);
REGISTER_UNIT_TEST("api.TestCallbackFSWriteFileNoop",
                   TestCallbackFSWriteFileNoop);
REGISTER_UNIT_TEST("api.TestCallbackFSPerCallFakeStat",
                   TestCallbackFSPerCallFakeStat);
REGISTER_UNIT_TEST("api.TestCallbackFSPerCallIdentityRealpath",
                   TestCallbackFSPerCallIdentityRealpath);
REGISTER_UNIT_TEST("api.TestCallbackFSError", TestCallbackFSError);
REGISTER_UNIT_TEST("api.TestCallbackFSRemoveFileNoop",
                   TestCallbackFSRemoveFileNoop);
