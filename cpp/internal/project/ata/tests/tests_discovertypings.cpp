// Port of tsc/internal/project/ata/discovertypings_test.go.
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/core/version.h"
#include "internal/gostd/testing.h"
#include "internal/project/ata/ata.h"
#include "internal/project/logging/logging.h"
#include "internal/semver/semver.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace ata = tsc::ata;
namespace collections = tsc::collections;
namespace logging = tsc::logging;
namespace projecttestutil = tsc::testutil::projecttestutil;
namespace semver = tsc::semver;
namespace vfstest = tsc::vfs::vfstest;
using tsc::gostd::testing::T;

using StringMapMap =
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>>;
using TypingCache =
    collections::SyncMap<std::string,
                         std::shared_ptr<ata::CachedTyping>>;

std::shared_ptr<tsc::vfs::FS> makeFS(
    std::initializer_list<
        std::pair<const std::string, std::string>>
        files) {
	return vfstest::FromMap(
	    std::unordered_map<std::string, vfstest::MapFileInput>(
	        files.begin(), files.end()),
	    false);
}

ata::TypingsInfo makeTypingsInfo(
    collections::Set<std::string>* unresolvedImports) {
	ata::TypingsInfo info;
	info.CompilerOptions = new tsc::CompilerOptions();
	auto* ta = new tsc::TypeAcquisition();
	ta->Enable = tsc::Tristate::True;
	info.TypeAcquisition = ta;
	info.UnresolvedImports = unresolvedImports;
	return info;
}

void expectTypingNames(T* t,
                       const std::vector<std::string>& got,
                       std::initializer_list<const char*> expected) {
	collections::Set<std::string> gotSet;
	gotSet.AddRange(got);
	collections::Set<std::string> expectedSet;
	for (auto* e : expected) expectedSet.Add(std::string(e));
	if (!gotSet.Equals(expectedSet)) {
		std::string g, e;
		for (auto& x : gotSet.Keys()) g += x + " ";
		for (auto& x : expectedSet.Keys()) e += x + " ";
		t->Fatalf("newTypingNames mismatch: got {%s} expected {%s}",
		          {g, e});
	}
}

void TestDiscoverTypings(T* t) {
	t->Parallel();

	t->Run("should use mappings from safe list", [](T* t) {
		t->Parallel();
		auto* logger = logging::newLogTree("DiscoverTypings");
		auto fs = makeFS({
		    {"/home/src/projects/project/app.js", ""},
		    {"/home/src/projects/project/jquery.js", ""},
		    {"/home/src/projects/project/chroma.min.js", ""},
		});
		TypingCache cache;
		auto infoDefault = makeTypingsInfo(nullptr);
		auto result = ata::DiscoverTypings(
		    fs.get(), logger, &infoDefault,
		    {"/home/src/projects/project/app.js",
		     "/home/src/projects/project/jquery.js",
		     "/home/src/projects/project/chroma.min.js"},
		    "/home/src/projects/project", &cache, StringMapMap{});
		assert::Assert(t, result.cachedTypingPaths.empty());
		expectTypingNames(t, result.newTypingNames, {"jquery",
		                                              "chroma-js"});
		assert::DeepEqual(
		    t, result.filesToWatch,
		    std::vector<std::string>{
		        "/home/src/projects/project/bower_components",
		        "/home/src/projects/project/node_modules"});
	});

	t->Run("should return node for core modules", [](T* t) {
		t->Parallel();
		auto* logger = logging::newLogTree("DiscoverTypings");
		auto fs = makeFS({
		    {"/home/src/projects/project/app.js", ""},
		});
		auto unresolvedImports =
		    collections::NewSetFromItems<std::string>("assert",
		                                              "somename");
		TypingCache cache;
		auto info = makeTypingsInfo(&unresolvedImports);
		auto result = ata::DiscoverTypings(
		    fs.get(), logger, &info,
		    {"/home/src/projects/project/app.js"},
		    "/home/src/projects/project", &cache, StringMapMap{});
		assert::Assert(t, result.cachedTypingPaths.empty());
		expectTypingNames(t, result.newTypingNames, {"node",
		                                              "somename"});
		assert::DeepEqual(
		    t, result.filesToWatch,
		    std::vector<std::string>{
		        "/home/src/projects/project/bower_components",
		        "/home/src/projects/project/node_modules"});
	});

	t->Run("should use cached locations", [](T* t) {
		t->Parallel();
		auto* logger = logging::newLogTree("DiscoverTypings");
		auto fs = makeFS({
		    {"/home/src/projects/project/app.js", ""},
		    {"/home/src/projects/project/node.d.ts", ""},
		});
		TypingCache cache;
		auto version =
		    std::make_shared<semver::Version>(
		        semver::MustParse("1.3.0"));
		auto typing = std::make_shared<ata::CachedTyping>();
		typing->TypingsLocation =
		    "/home/src/projects/project/node.d.ts";
		typing->Version = version;
		cache.Store("node", typing);
		auto unresolvedImports =
		    collections::NewSetFromItems<std::string>("fs", "bar");
		auto info = makeTypingsInfo(&unresolvedImports);
		auto result = ata::DiscoverTypings(
		    fs.get(), logger, &info,
		    {"/home/src/projects/project/app.js"},
		    "/home/src/projects/project", &cache,
		    StringMapMap{
		        {"node", projecttestutil::TypesRegistryConfig()},
		    });
		assert::DeepEqual(
		    t, result.cachedTypingPaths,
		    std::vector<std::string>{
		        "/home/src/projects/project/node.d.ts"});
		expectTypingNames(t, result.newTypingNames, {"bar"});
		assert::DeepEqual(
		    t, result.filesToWatch,
		    std::vector<std::string>{
		        "/home/src/projects/project/bower_components",
		        "/home/src/projects/project/node_modules"});
	});

	t->Run(
	    "should gracefully handle packages that have been removed from the types-registry",
	    [](T* t) {
		    t->Parallel();
		    auto* logger = logging::newLogTree("DiscoverTypings");
		    auto fs = makeFS({
		        {"/home/src/projects/project/app.js", ""},
		        {"/home/src/projects/project/node.d.ts", ""},
		    });
		    TypingCache cache;
		    auto version = std::make_shared<semver::Version>(
		        semver::MustParse("1.3.0"));
		    auto typing = std::make_shared<ata::CachedTyping>();
		    typing->TypingsLocation =
		        "/home/src/projects/project/node.d.ts";
		    typing->Version = version;
		    cache.Store("node", typing);
		    auto unresolvedImports =
		        collections::NewSetFromItems<std::string>("fs",
		                                                  "bar");
		    auto info = makeTypingsInfo(&unresolvedImports);
		    auto result = ata::DiscoverTypings(
		        fs.get(), logger, &info,
		        {"/home/src/projects/project/app.js"},
		        "/home/src/projects/project", &cache,
		        StringMapMap{});
		    assert::Assert(t, result.cachedTypingPaths.empty());
		    expectTypingNames(t, result.newTypingNames, {"node",
		                                                  "bar"});
		    assert::DeepEqual(
		        t, result.filesToWatch,
		        std::vector<std::string>{
		            "/home/src/projects/project/bower_components",
		            "/home/src/projects/project/node_modules"});
	    });

	t->Run("should search only 2 levels deep", [](T* t) {
		t->Parallel();
		auto* logger = logging::newLogTree("DiscoverTypings");
		auto fs = makeFS({
		    {"/home/src/projects/project/app.js", ""},
		    {"/home/src/projects/project/node_modules/a/package.json",
		     "{ \"name\": \"a\" }"},
		    {"/home/src/projects/project/node_modules/a/b/package.json",
		     "{ \"name\": \"b\" }"},
		});
		TypingCache cache;
		auto infoDefault = makeTypingsInfo(nullptr);
		auto result = ata::DiscoverTypings(
		    fs.get(), logger, &infoDefault,
		    {"/home/src/projects/project/app.js"},
		    "/home/src/projects/project", &cache, StringMapMap{});
		assert::Assert(t, result.cachedTypingPaths.empty());
		expectTypingNames(t, result.newTypingNames, {"a"});
		assert::DeepEqual(
		    t, result.filesToWatch,
		    std::vector<std::string>{
		        "/home/src/projects/project/bower_components",
		        "/home/src/projects/project/node_modules"});
	});

	t->Run("should support scoped packages", [](T* t) {
		t->Parallel();
		auto* logger = logging::newLogTree("DiscoverTypings");
		auto fs = makeFS({
		    {"/home/src/projects/project/app.js", ""},
		    {"/home/src/projects/project/node_modules/@a/b/package.json",
		     "{ \"name\": \"@a/b\" }"},
		});
		TypingCache cache;
		auto infoDefault = makeTypingsInfo(nullptr);
		auto result = ata::DiscoverTypings(
		    fs.get(), logger, &infoDefault,
		    {"/home/src/projects/project/app.js"},
		    "/home/src/projects/project", &cache, StringMapMap{});
		assert::Assert(t, result.cachedTypingPaths.empty());
		expectTypingNames(t, result.newTypingNames, {"@a/b"});
		assert::DeepEqual(
		    t, result.filesToWatch,
		    std::vector<std::string>{
		        "/home/src/projects/project/bower_components",
		        "/home/src/projects/project/node_modules"});
	});

	t->Run("should install expired typings", [](T* t) {
		t->Parallel();
		auto* logger = logging::newLogTree("DiscoverTypings");
		auto fs = makeFS({
		    {"/home/src/projects/project/app.js", ""},
		});
		TypingCache cache;
		auto nodeVersion = std::make_shared<semver::Version>(
		    semver::MustParse("1.3.0"));
		auto commanderVersion = std::make_shared<semver::Version>(
		    semver::MustParse("1.0.0"));
		auto nodeTyping = std::make_shared<ata::CachedTyping>();
		nodeTyping->TypingsLocation =
		    std::string(projecttestutil::TestTypingsLocation) +
		    "/node_modules/@types/node/index.d.ts";
		nodeTyping->Version = nodeVersion;
		cache.Store("node", nodeTyping);
		auto commanderTyping =
		    std::make_shared<ata::CachedTyping>();
		commanderTyping->TypingsLocation =
		    std::string(projecttestutil::TestTypingsLocation) +
		    "/node_modules/@types/commander/index.d.ts";
		commanderTyping->Version = commanderVersion;
		cache.Store("commander", commanderTyping);
		auto unresolvedImports =
		    collections::NewSetFromItems<std::string>("http",
		                                              "commander");
		auto info = makeTypingsInfo(&unresolvedImports);
		auto result = ata::DiscoverTypings(
		    fs.get(), logger, &info,
		    {"/home/src/projects/project/app.js"},
		    "/home/src/projects/project", &cache,
		    StringMapMap{
		        {"node", projecttestutil::TypesRegistryConfig()},
		        {"commander",
		         projecttestutil::TypesRegistryConfig()},
		    });
		assert::DeepEqual(
		    t, result.cachedTypingPaths,
		    std::vector<std::string>{
		        "/home/src/Library/Caches/typescript/node_modules/@types/node/index.d.ts"});
		expectTypingNames(t, result.newTypingNames, {"commander"});
		assert::DeepEqual(
		    t, result.filesToWatch,
		    std::vector<std::string>{
		        "/home/src/projects/project/bower_components",
		        "/home/src/projects/project/node_modules"});
	});

	t->Run(
	    "should install expired typings with prerelease version of tsserver",
	    [](T* t) {
		    t->Parallel();
		    auto* logger = logging::newLogTree("DiscoverTypings");
		    auto fs = makeFS({
		        {"/home/src/projects/project/app.js", ""},
		    });
		    TypingCache cache;
		    auto nodeVersion = std::make_shared<semver::Version>(
		        semver::MustParse("1.0.0"));
		    auto nodeTyping =
		        std::make_shared<ata::CachedTyping>();
		    nodeTyping->TypingsLocation =
		        std::string(projecttestutil::TestTypingsLocation) +
		        "/node_modules/@types/node/index.d.ts";
		    nodeTyping->Version = nodeVersion;
		    cache.Store("node", nodeTyping);
		    auto config = projecttestutil::TypesRegistryConfig();
		    config.erase("ts" +
		                 std::string(tsc::versionMajorMinor()));

		    auto unresolvedImports =
		        collections::NewSetFromItems<std::string>("http");
		    auto info = makeTypingsInfo(&unresolvedImports);
		    auto result = ata::DiscoverTypings(
		        fs.get(), logger, &info,
		        {"/home/src/projects/project/app.js"},
		        "/home/src/projects/project", &cache,
		        StringMapMap{
		            {"node", config},
		        });
		    assert::Assert(t, result.cachedTypingPaths.empty());
		    expectTypingNames(t, result.newTypingNames, {"node"});
		    assert::DeepEqual(
		        t, result.filesToWatch,
		        std::vector<std::string>{
		            "/home/src/projects/project/bower_components",
		            "/home/src/projects/project/node_modules"});
	    });

	t->Run("prerelease typings are properly handled", [](T* t) {
		t->Parallel();
		auto* logger = logging::newLogTree("DiscoverTypings");
		auto fs = makeFS({
		    {"/home/src/projects/project/app.js", ""},
		});
		TypingCache cache;
		auto nodeVersion = std::make_shared<semver::Version>(
		    semver::MustParse("1.3.0-next.0"));
		auto commanderVersion = std::make_shared<semver::Version>(
		    semver::MustParse("1.3.0-next.0"));
		auto nodeTyping = std::make_shared<ata::CachedTyping>();
		nodeTyping->TypingsLocation =
		    std::string(projecttestutil::TestTypingsLocation) +
		    "/node_modules/@types/node/index.d.ts";
		nodeTyping->Version = nodeVersion;
		cache.Store("node", nodeTyping);
		auto commanderTyping =
		    std::make_shared<ata::CachedTyping>();
		commanderTyping->TypingsLocation =
		    std::string(projecttestutil::TestTypingsLocation) +
		    "/node_modules/@types/commander/index.d.ts";
		commanderTyping->Version = commanderVersion;
		cache.Store("commander", commanderTyping);
		auto config = projecttestutil::TypesRegistryConfig();
		config["ts" + std::string(tsc::versionMajorMinor())] =
		    "1.3.0-next.1";
		auto unresolvedImports =
		    collections::NewSetFromItems<std::string>("http",
		                                              "commander");
		auto info = makeTypingsInfo(&unresolvedImports);
		auto result = ata::DiscoverTypings(
		    fs.get(), logger, &info,
		    {"/home/src/projects/project/app.js"},
		    "/home/src/projects/project", &cache,
		    StringMapMap{
		        {"node", config},
		        {"commander",
		         projecttestutil::TypesRegistryConfig()},
		    });
		assert::Assert(t, result.cachedTypingPaths.empty());
		expectTypingNames(t, result.newTypingNames, {"node",
		                                              "commander"});
		assert::DeepEqual(
		    t, result.filesToWatch,
		    std::vector<std::string>{
		        "/home/src/projects/project/bower_components",
		        "/home/src/projects/project/node_modules"});
	});
}

REGISTER_UNIT_TEST("project/ata.TestDiscoverTypings",
                   TestDiscoverTypings);

}  // namespace
