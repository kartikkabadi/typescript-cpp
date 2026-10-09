// Port of tsc/internal/sourcemap/source_mapper_test.go.
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/core/text.h"
#include "internal/gostd/testing.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

using tsc::gostd::testing::T;
using namespace tsc::sourcemap;
using tsc::computeECMALineStarts;
namespace assert = tsc::gotest::assert;

// sourceMapperTestHost — source_mapper_test.go:9
struct sourceMapperTestHost : Host {
	std::unordered_map<std::string, std::string> files;

	bool UseCaseSensitiveFileNames() override { return true; }

	ECMALineInfo* GetECMALineInfo(std::string_view fileName) override {
		auto it = files.find(std::string(fileName));
		if (it == files.end()) {
			return nullptr;
		}
		return CreateECMALineInfo(it->second,
		                          computeECMALineStarts(it->second));
	}

	std::pair<std::string, bool> ReadFile(
	    std::string_view fileName) override {
		auto it = files.find(std::string(fileName));
		if (it == files.end()) {
			return {"", false};
		}
		return {it->second, true};
	}
};

void TestSourceMapperPreservesEmptySourceEntries(T* t) {
	t->Parallel();

	sourceMapperTestHost host;
	host.files = {{"/project/out/out.d.ts", "generated"},
	              {"/project/src/real.ts", "source"}};
	auto* mapper = convertDocumentToSourceMapper(
	    &host,
	    "{\"version\":3,\"file\":\"out.d.ts\",\"sourceRoot\":\"../src\","
	    "\"sources\":[\"\",\"real.ts\"],\"names\":[],\"mappings\":\"ACAA\"}",
	    "/project/out/out.d.ts.map");
	assert::Assert(t, mapper != nullptr);
	auto* sourcePos = GetSourcePosition(
	    mapper,
	    new DocumentPosition{"/project/out/out.d.ts", 0});
	assert::Assert(t, sourcePos != nullptr);
	assert::Equal(t, sourcePos->FileName,
	              std::string("/project/src/real.ts"));
	assert::Equal(t, sourcePos->Pos, 0);
	auto* generatedPos = GetGeneratedPosition(
	    mapper, new DocumentPosition{"/project/src/real.ts", 0});
	assert::Assert(t, generatedPos != nullptr);
	assert::Equal(t, generatedPos->FileName,
	              std::string("/project/out/out.d.ts"));
	assert::Equal(t, generatedPos->Pos, 0);
}

void TestSourceMapperResolvesEmptySourceToSourceRoot(T* t) {
	t->Parallel();

	sourceMapperTestHost host;
	host.files = {{"/project/out/out.d.ts", "generated"},
	              {"/project/src", "source"}};
	auto* mapper = convertDocumentToSourceMapper(
	    &host,
	    "{\"version\":3,\"file\":\"out.d.ts\",\"sourceRoot\":\"../src\","
	    "\"sources\":[\"\"],\"names\":[],\"mappings\":\"AAAA\"}",
	    "/project/out/out.d.ts.map");
	assert::Assert(t, mapper != nullptr);
	auto* sourcePos = GetSourcePosition(
	    mapper,
	    new DocumentPosition{"/project/out/out.d.ts", 0});
	assert::Assert(t, sourcePos != nullptr);
	assert::Equal(t, sourcePos->FileName, std::string("/project/src"));
	assert::Equal(t, sourcePos->Pos, 0);
}

void TestSourceMapperResolvesEmptySourceToMapURLWithoutSourceRoot(T* t) {
	t->Parallel();

	sourceMapperTestHost host;
	host.files = {{"/project/out/out.d.ts", "generated"},
	              {"/project/out/out.d.ts.map", "source"}};
	auto* mapper = convertDocumentToSourceMapper(
	    &host,
	    "{\"version\":3,\"file\":\"out.d.ts\",\"sources\":[\"\"],"
	    "\"names\":[],\"mappings\":\"AAAA\"}",
	    "/project/out/out.d.ts.map");
	assert::Assert(t, mapper != nullptr);
	auto* sourcePos = GetSourcePosition(
	    mapper,
	    new DocumentPosition{"/project/out/out.d.ts", 0});
	assert::Assert(t, sourcePos != nullptr);
	assert::Equal(t, sourcePos->FileName,
	              std::string("/project/out/out.d.ts.map"));
	assert::Equal(t, sourcePos->Pos, 0);
}

void TestSourceMapperTreatsEmptySourceRootAsAbsent(T* t) {
	t->Parallel();

	sourceMapperTestHost host;
	host.files = {{"/project/out/out.d.ts", "generated"},
	              {"/project/out/out.d.ts.map", "map-relative empty source"},
	              {"/project/out/a.ts", "map-relative source"},
	              {"/project/src/a.ts", "parent-relative source"},
	              {"/", "unrelated root"},
	              {"/a.ts", "unrelated root source"},
	              {"/missing.ts", "unrelated root source"}};
	struct sourceCase {
		const char* name;
		const char* source;
		const char* fileName;
	};
	struct rootCase {
		const char* name;
		const char* field;
	};
	for (auto& test : std::vector<sourceCase>{
	         {"empty source", "", "/project/out/out.d.ts.map"},
	         {"relative source", "a.ts", "/project/out/a.ts"},
	         {"parent-relative source", "../src/a.ts",
	          "/project/src/a.ts"},
	         {"missing source", "missing.ts", ""},
	     }) {
		t->Run(test.name, [&](T* t) {
			t->Parallel();
			for (auto& root : std::vector<rootCase>{
			         {"absent", ""},
			         {"empty", "\"sourceRoot\":\"\","},
			     }) {
				t->Run(root.name, [&](T* t) {
					t->Parallel();
					auto* mapper = convertDocumentToSourceMapper(
					    &host,
					    std::string("{\"version\":3,\"file\":"
					                "\"out.d.ts\",") +
					        root.field + "\"sources\":[\"" +
					        test.source +
					        "\"],\"names\":[],\"mappings\":"
					        "\"AAAA\"}",
					    "/project/out/out.d.ts.map");
					assert::Assert(t, mapper != nullptr);
					auto* sourcePosition = GetSourcePosition(
					    mapper, new DocumentPosition{
					                "/project/out/out.d.ts", 0});
					if (std::string(test.fileName).empty()) {
						assert::Assert(t,
						               sourcePosition == nullptr);
						return;
					}
					assert::Assert(t, sourcePosition != nullptr);
					assert::Equal(t, sourcePosition->FileName,
					              std::string(test.fileName));
					assert::Equal(t, sourcePosition->Pos, 0);
					auto* generatedPos = GetGeneratedPosition(
					    mapper,
					    new DocumentPosition{test.fileName, 0});
					assert::Assert(t, generatedPos != nullptr);
					assert::Equal(
					    t, generatedPos->FileName,
					    std::string("/project/out/out.d.ts"));
					assert::Equal(t, generatedPos->Pos, 0);
				});
			}
		});
	}
}

void TestSourceMapperPrefixesAbsoluteSourceWithNonemptySourceRoot(T* t) {
	t->Parallel();

	sourceMapperTestHost host;
	host.files = {{"/project/out/out.d.ts", "generated"},
	              {"/project/src/actual/a.ts", "prefixed source"},
	              {"/actual/a.ts", "unprefixed source"}};
	auto* mapper = convertDocumentToSourceMapper(
	    &host,
	    "{\"version\":3,\"file\":\"out.d.ts\",\"sourceRoot\":\"../src\","
	    "\"sources\":[\"/actual/a.ts\"],\"names\":[],\"mappings\":\"AAAA\"}",
	    "/project/out/out.d.ts.map");
	assert::Assert(t, mapper != nullptr);
	auto* sourcePos = GetSourcePosition(
	    mapper,
	    new DocumentPosition{"/project/out/out.d.ts", 0});
	assert::Assert(t, sourcePos != nullptr);
	assert::Equal(t, sourcePos->FileName,
	              std::string("/project/src/actual/a.ts"));
	assert::Equal(t, sourcePos->Pos, 0);
}

void TestSourceMapperRetainsDuplicateSourceIndices(T* t) {
	t->Parallel();

	sourceMapperTestHost host;
	host.files = {{"/project/out/out.d.ts", "generated"},
	              {"/project/src", "source"}};
	auto* mapper = convertDocumentToSourceMapper(
	    &host,
	    "{\"version\":3,\"file\":\"out.d.ts\",\"sourceRoot\":\"../src\","
	    "\"sources\":[\"\",\"\"],\"names\":[],\"mappings\":\"AAAA\"}",
	    "/project/out/out.d.ts.map");
	assert::Assert(t, mapper != nullptr);
	auto* generatedPos = GetGeneratedPosition(
	    mapper, new DocumentPosition{"/project/src", 0});
	assert::Assert(t, generatedPos != nullptr);
	assert::Equal(t, generatedPos->FileName,
	              std::string("/project/out/out.d.ts"));
	assert::Equal(t, generatedPos->Pos, 0);
}

void TestSourceMapperPreservesNullSourceEntries(T* t) {
	t->Parallel();

	sourceMapperTestHost host;
	host.files = {{"/project/out/out.d.ts", "generated"},
	              {"/project/out/real.ts", "source"}};
	auto* mapper = convertDocumentToSourceMapper(
	    &host,
	    "{\"version\":3,\"file\":\"out.d.ts\",\"sources\":[null,"
	    "\"real.ts\"],\"names\":[],\"mappings\":\"ACAA\"}",
	    "/project/out/out.d.ts.map");
	assert::Assert(t, mapper != nullptr);
	auto* sourcePos = GetSourcePosition(
	    mapper,
	    new DocumentPosition{"/project/out/out.d.ts", 0});
	assert::Assert(t, sourcePos != nullptr);
	assert::Equal(t, sourcePos->FileName,
	              std::string("/project/out/real.ts"));
	assert::Equal(t, sourcePos->Pos, 0);

	auto* nullMapper = convertDocumentToSourceMapper(
	    &host,
	    "{\"version\":3,\"file\":\"out.d.ts\",\"sources\":[null],"
	    "\"names\":[],\"mappings\":\"AAAA\"}",
	    "/project/out/out.d.ts.map");
	assert::Assert(t, nullMapper != nullptr);
	assert::Assert(t,
	               GetSourcePosition(
	                   nullMapper, new DocumentPosition{
	                                   "/project/out/out.d.ts", 0}) ==
	                   nullptr);
}

void TestSourceMapperIgnoresOutOfRangeSourceIndex(T* t) {
	t->Parallel();

	sourceMapperTestHost host;
	auto* mapper = convertDocumentToSourceMapper(
	    &host,
	    "{\"version\":3,\"file\":\"out.d.ts\",\"sources\":[\"real.ts\"],"
	    "\"names\":[],\"mappings\":\"ACAA\"}",
	    "/project/out/out.d.ts.map");
	assert::Assert(t, mapper != nullptr);
	assert::Assert(t,
	               GetSourcePosition(
	                   mapper, new DocumentPosition{
	                               "/project/out/out.d.ts", 0}) ==
	                   nullptr);
}

} // namespace

REGISTER_UNIT_TEST("sourcemap.TestSourceMapperPreservesEmptySourceEntries",
                   TestSourceMapperPreservesEmptySourceEntries);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapperResolvesEmptySourceToSourceRoot",
    TestSourceMapperResolvesEmptySourceToSourceRoot);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapperResolvesEmptySourceToMapURLWithoutSourceRoot",
    TestSourceMapperResolvesEmptySourceToMapURLWithoutSourceRoot);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapperTreatsEmptySourceRootAsAbsent",
    TestSourceMapperTreatsEmptySourceRootAsAbsent);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapperPrefixesAbsoluteSourceWithNonemptySourceRoot",
    TestSourceMapperPrefixesAbsoluteSourceWithNonemptySourceRoot);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapperRetainsDuplicateSourceIndices",
    TestSourceMapperRetainsDuplicateSourceIndices);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapperPreservesNullSourceEntries",
    TestSourceMapperPreservesNullSourceEntries);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapperIgnoresOutOfRangeSourceIndex",
    TestSourceMapperIgnoresOutOfRangeSourceIndex);
