// tests_generator.cpp — port of tsc/internal/sourcemap/generator_test.go
// (package sourcemap — exercises the generator's public surface only).
#include <optional>
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

void assertError(T* t, const gostd::Error& err,
                 const std::string& message) {
	t->Helper();
	assert::Assert(t, err != nullptr,
	               "expected error: " + message);
	assert::Equal(t, err->Error(), message);
}

sourcemap::RawSourceMap expectSourceMap(
    const std::string& file, const std::string& sourceRoot,
    std::vector<std::string> sources, std::vector<std::string> names,
    const std::string& mappings,
    std::optional<std::vector<std::optional<std::string>>> sourcesContent) {
	sourcemap::RawSourceMap m;
	m.Version = 3;
	m.File = file;
	m.SourceRoot = sourceRoot;
	m.Sources = std::move(sources);
	m.Names = std::move(names);
	m.Mappings = mappings;
	m.SourcesContent = std::move(sourcesContent);
	return m;
}

void TestSourceMapGenerator_Empty(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto* sourceMap = gen->RawSourceMap();
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {}, {}, "",
	                                   std::nullopt));
}

void TestSourceMapGenerator_Empty_Serialized(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto actual = gen->String();
	std::string expected =
	    "{\"version\":3,\"file\":\"main.js\",\"sourceRoot\":\"/\",\"sources\":"
	    "[],\"names\":[],\"mappings\":\"\"}";
	assert::Equal(t, actual, expected);
}

void TestSourceMapGenerator_AddSource(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	auto* sourceMap = gen->RawSourceMap();
	assert::Equal(t, (int)sourceIndex, 0);
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {"main.ts"}, {}, "",
	                                   std::nullopt));
}

void TestSourceMapGenerator_SetSourceContent(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	std::string sourceContent = "foo";
	assert::NilError(t, gen->SetSourceContent(sourceIndex, sourceContent));
	auto* sourceMap = gen->RawSourceMap();
	assert::Equal(t, (int)sourceIndex, 0);
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {"main.ts"}, {}, "",
	                                   {{sourceContent}}));
}

void TestSourceMapGenerator_SetSourceContent_ForSecondSourceOnly(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	gen->AddSource("/skipped.ts");
	auto sourceIndex = gen->AddSource("/main.ts");
	std::string sourceContent = "foo";
	assert::NilError(t, gen->SetSourceContent(sourceIndex, sourceContent));
	auto* sourceMap = gen->RawSourceMap();
	assert::Equal(t, (int)sourceIndex, 1);
	assert::Assert(
	    t, *sourceMap == expectSourceMap(
	                         "main.js", "/", {"skipped.ts", "main.ts"}, {}, "",
	                         std::vector<std::optional<std::string>>{
	                             std::nullopt, sourceContent}));
}

void TestSourceMapGenerator_SetSourceContent_SourceIndexOutOfRange(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	assertError(t, gen->SetSourceContent(-1, ""),
	            "sourceIndex is out of range");
	assertError(t, gen->SetSourceContent(0, ""),
	            "sourceIndex is out of range");
}

void TestSourceMapGenerator_SetSourceContent_ForSecondSourceOnly_Serialized(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	gen->AddSource("/skipped.ts");
	auto sourceIndex = gen->AddSource("/main.ts");
	std::string sourceContent = "foo";
	assert::NilError(t, gen->SetSourceContent(sourceIndex, sourceContent));
	auto actual = gen->String();
	std::string expected =
	    "{\"version\":3,\"file\":\"main.js\",\"sourceRoot\":\"/\",\"sources\":"
	    "[\"skipped.ts\",\"main.ts\"],\"names\":[],\"mappings\":\"\","
	    "\"sourcesContent\":[null,\"foo\"]}";
	assert::Equal(t, actual, expected);
}

void TestSourceMapGenerator_AddName(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto nameIndex = gen->AddName("foo");
	auto* sourceMap = gen->RawSourceMap();
	assert::Equal(t, (int)nameIndex, 0);
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {}, {"foo"}, "",
	                                   std::nullopt));
}

void TestSourceMapGenerator_AddGeneratedMapping(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	assert::NilError(t, gen->AddGeneratedMapping(0, 0));
	auto* sourceMap = gen->RawSourceMap();
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {}, {}, "A",
	                                   std::nullopt));
}

void TestSourceMapGenerator_AddGeneratedMapping_ReplacesPendingSourceMapping(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assert::NilError(t, gen->AddSourceMapping(0, 0, sourceIndex, 0, 0));
	assert::NilError(t, gen->AddGeneratedMapping(0, 0));
	auto* sourceMap = gen->RawSourceMap();
	assert::Equal(t, sourceMap->Mappings, "A");
}

void TestSourceMapGenerator_AddGeneratedMapping_IsNotReplacedBySourceMapping(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assert::NilError(t, gen->AddGeneratedMapping(0, 0));
	assert::NilError(t, gen->AddSourceMapping(0, 0, sourceIndex, 0, 0));
	auto* sourceMap = gen->RawSourceMap();
	assert::Equal(t, sourceMap->Mappings, "A");
}

void TestSourceMapGenerator_AddGeneratedMapping_OnSecondLineOnly(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	assert::NilError(t, gen->AddGeneratedMapping(1, 0));
	auto* sourceMap = gen->RawSourceMap();
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {}, {}, ";A",
	                                   std::nullopt));
}

void TestSourceMapGenerator_AddSourceMapping(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assert::NilError(t, gen->AddSourceMapping(0, 0, sourceIndex, 0, 0));
	auto* sourceMap = gen->RawSourceMap();
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {"main.ts"}, {},
	                                   "AAAA", std::nullopt));
}

void TestSourceMapGenerator_AddSourceMapping_NextGeneratedCharacter(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assert::NilError(t, gen->AddSourceMapping(0, 0, sourceIndex, 0, 0));
	assert::NilError(t, gen->AddSourceMapping(0, 1, sourceIndex, 0, 0));
	auto* sourceMap = gen->RawSourceMap();
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {"main.ts"}, {},
	                                   "AAAA,CAAA", std::nullopt));
}

void TestSourceMapGenerator_AddSourceMapping_NextGeneratedAndSourceCharacter(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assert::NilError(t, gen->AddSourceMapping(0, 0, sourceIndex, 0, 0));
	assert::NilError(t, gen->AddSourceMapping(0, 1, sourceIndex, 0, 1));
	auto* sourceMap = gen->RawSourceMap();
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {"main.ts"}, {},
	                                   "AAAA,CAAC", std::nullopt));
}

void TestSourceMapGenerator_AddSourceMapping_NextGeneratedLine(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assert::NilError(t, gen->AddSourceMapping(0, 0, sourceIndex, 0, 0));
	assert::NilError(t, gen->AddSourceMapping(1, 0, sourceIndex, 0, 0));
	auto* sourceMap = gen->RawSourceMap();
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {"main.ts"}, {},
	                                   "AAAA;AAAA", std::nullopt));
}

void TestSourceMapGenerator_AddSourceMapping_PreviousSourceCharacter(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assert::NilError(t, gen->AddSourceMapping(0, 0, sourceIndex, 0, 1));
	assert::NilError(t, gen->AddSourceMapping(0, 1, sourceIndex, 0, 0));
	auto* sourceMap = gen->RawSourceMap();
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {"main.ts"}, {},
	                                   "AAAC,CAAD", std::nullopt));
}

void TestSourceMapGenerator_AddNamedSourceMapping(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	auto nameIndex = gen->AddName("foo");
	assert::NilError(
	    t, gen->AddNamedSourceMapping(0, 0, sourceIndex, 0, 0, nameIndex));
	auto* sourceMap = gen->RawSourceMap();
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {"main.ts"},
	                                   {"foo"}, "AAAAA", std::nullopt));
}

void TestSourceMapGenerator_AddNamedSourceMapping_WithPreviousName(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	auto nameIndex1 = gen->AddName("foo");
	auto nameIndex2 = gen->AddName("bar");
	assert::NilError(
	    t, gen->AddNamedSourceMapping(0, 0, sourceIndex, 0, 0, nameIndex2));
	assert::NilError(
	    t, gen->AddNamedSourceMapping(0, 1, sourceIndex, 0, 0, nameIndex1));
	auto* sourceMap = gen->RawSourceMap();
	assert::Assert(t, *sourceMap == expectSourceMap(
	                                   "main.js", "/", {"main.ts"},
	                                   {"foo", "bar"}, "AAAAC,CAAAD",
	                                   std::nullopt));
}

void TestSourceMapGenerator_AddGeneratedMapping_GeneratedLineCannotBacktrack(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	assert::NilError(t, gen->AddGeneratedMapping(1, 0));
	assertError(t, gen->AddGeneratedMapping(0, 0),
	            "generatedLine cannot backtrack");
}

void TestSourceMapGenerator_AddGeneratedMapping_GeneratedCharacterCannotBeNegative(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	assert::NilError(t, gen->AddGeneratedMapping(0, 0));
	assertError(t, gen->AddGeneratedMapping(0, -1),
	            "generatedCharacter cannot be negative");
}

void TestSourceMapGenerator_AddSourceMapping_GeneratedLineCannotBacktrack(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assert::NilError(t, gen->AddSourceMapping(1, 0, sourceIndex, 0, 0));
	assertError(t, gen->AddSourceMapping(0, 0, sourceIndex, 0, 0),
	            "generatedLine cannot backtrack");
}

void TestSourceMapGenerator_AddSourceMapping_GeneratedCharacterCannotBeNegative(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assert::NilError(t, gen->AddSourceMapping(0, 0, sourceIndex, 0, 0));
	assertError(t, gen->AddSourceMapping(0, -1, sourceIndex, 0, 0),
	            "generatedCharacter cannot be negative");
}

void TestSourceMapGenerator_AddSourceMapping_SourceIndexIsOutOfRange(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	assertError(t, gen->AddSourceMapping(0, 0, -1, 0, 0),
	            "sourceIndex is out of range");
	assertError(t, gen->AddSourceMapping(0, 0, 0, 0, 0),
	            "sourceIndex is out of range");
}

void TestSourceMapGenerator_AddSourceMapping_SourceLineCannotBeNegative(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assertError(t, gen->AddSourceMapping(0, 0, sourceIndex, -1, 0),
	            "sourceLine cannot be negative");
}

void TestSourceMapGenerator_AddSourceMapping_SourceCharacterCannotBeNegative(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assertError(t, gen->AddSourceMapping(0, 0, sourceIndex, 0, -1),
	            "sourceCharacter cannot be negative");
}

void TestSourceMapGenerator_AddNamedSourceMapping_GeneratedLineCannotBacktrack(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	auto nameIndex = gen->AddName("foo");
	assert::NilError(
	    t, gen->AddNamedSourceMapping(1, 0, sourceIndex, 0, 0, nameIndex));
	assertError(t,
	            gen->AddNamedSourceMapping(0, 0, sourceIndex, 0, 0, nameIndex),
	            "generatedLine cannot backtrack");
}

void TestSourceMapGenerator_AddNamedSourceMapping_GeneratedCharacterCannotBeNegative(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	auto nameIndex = gen->AddName("foo");
	assert::NilError(
	    t, gen->AddNamedSourceMapping(0, 0, sourceIndex, 0, 0, nameIndex));
	assertError(t,
	            gen->AddNamedSourceMapping(0, -1, sourceIndex, 0, 0, nameIndex),
	            "generatedCharacter cannot be negative");
}

void TestSourceMapGenerator_AddNamedSourceMapping_SourceIndexIsOutOfRange(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto nameIndex = gen->AddName("foo");
	assertError(t, gen->AddNamedSourceMapping(0, 0, -1, 0, 0, nameIndex),
	            "sourceIndex is out of range");
	assertError(t, gen->AddNamedSourceMapping(0, 0, 0, 0, 0, nameIndex),
	            "sourceIndex is out of range");
}

void TestSourceMapGenerator_AddNamedSourceMapping_SourceLineCannotBeNegative(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto nameIndex = gen->AddName("foo");
	auto sourceIndex = gen->AddSource("/main.ts");
	assertError(
	    t, gen->AddNamedSourceMapping(0, 0, sourceIndex, -1, 0, nameIndex),
	    "sourceLine cannot be negative");
}

void TestSourceMapGenerator_AddNamedSourceMapping_SourceCharacterCannotBeNegative(
    T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto nameIndex = gen->AddName("foo");
	auto sourceIndex = gen->AddSource("/main.ts");
	assertError(
	    t, gen->AddNamedSourceMapping(0, 0, sourceIndex, 0, -1, nameIndex),
	    "sourceCharacter cannot be negative");
}

void TestSourceMapGenerator_AddNamedSourceMapping_NameIndexIsOutOfRange(T* t) {
	t->Parallel();
	auto* gen = sourcemap::NewGenerator("main.js", "/", "/",
	                                    tspath::ComparePathsOptions{});
	auto sourceIndex = gen->AddSource("/main.ts");
	assertError(t,
	            gen->AddNamedSourceMapping(0, 0, sourceIndex, 0, 0, -1),
	            "nameIndex is out of range");
	assertError(t,
	            gen->AddNamedSourceMapping(0, 0, sourceIndex, 0, 0, 0),
	            "nameIndex is out of range");
}

} // namespace

REGISTER_UNIT_TEST("sourcemap.TestSourceMapGenerator_Empty",
                   TestSourceMapGenerator_Empty);
REGISTER_UNIT_TEST("sourcemap.TestSourceMapGenerator_Empty_Serialized",
                   TestSourceMapGenerator_Empty_Serialized);
REGISTER_UNIT_TEST("sourcemap.TestSourceMapGenerator_AddSource",
                   TestSourceMapGenerator_AddSource);
REGISTER_UNIT_TEST("sourcemap.TestSourceMapGenerator_SetSourceContent",
                   TestSourceMapGenerator_SetSourceContent);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_SetSourceContent_ForSecondSourceOnly",
    TestSourceMapGenerator_SetSourceContent_ForSecondSourceOnly);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_SetSourceContent_SourceIndexOutOfRange",
    TestSourceMapGenerator_SetSourceContent_SourceIndexOutOfRange);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_SetSourceContent_ForSecondSourceOnly_Serialized",
    TestSourceMapGenerator_SetSourceContent_ForSecondSourceOnly_Serialized);
REGISTER_UNIT_TEST("sourcemap.TestSourceMapGenerator_AddName",
                   TestSourceMapGenerator_AddName);
REGISTER_UNIT_TEST("sourcemap.TestSourceMapGenerator_AddGeneratedMapping",
                   TestSourceMapGenerator_AddGeneratedMapping);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddGeneratedMapping_ReplacesPendingSourceMapping",
    TestSourceMapGenerator_AddGeneratedMapping_ReplacesPendingSourceMapping);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddGeneratedMapping_IsNotReplacedBySourceMapping",
    TestSourceMapGenerator_AddGeneratedMapping_IsNotReplacedBySourceMapping);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddGeneratedMapping_OnSecondLineOnly",
    TestSourceMapGenerator_AddGeneratedMapping_OnSecondLineOnly);
REGISTER_UNIT_TEST("sourcemap.TestSourceMapGenerator_AddSourceMapping",
                   TestSourceMapGenerator_AddSourceMapping);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddSourceMapping_NextGeneratedCharacter",
    TestSourceMapGenerator_AddSourceMapping_NextGeneratedCharacter);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddSourceMapping_NextGeneratedAndSourceCharacter",
    TestSourceMapGenerator_AddSourceMapping_NextGeneratedAndSourceCharacter);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddSourceMapping_NextGeneratedLine",
    TestSourceMapGenerator_AddSourceMapping_NextGeneratedLine);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddSourceMapping_PreviousSourceCharacter",
    TestSourceMapGenerator_AddSourceMapping_PreviousSourceCharacter);
REGISTER_UNIT_TEST("sourcemap.TestSourceMapGenerator_AddNamedSourceMapping",
                   TestSourceMapGenerator_AddNamedSourceMapping);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddNamedSourceMapping_WithPreviousName",
    TestSourceMapGenerator_AddNamedSourceMapping_WithPreviousName);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddGeneratedMapping_GeneratedLineCannotBacktrack",
    TestSourceMapGenerator_AddGeneratedMapping_GeneratedLineCannotBacktrack);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddGeneratedMapping_GeneratedCharacterCannotBeNegative",
    TestSourceMapGenerator_AddGeneratedMapping_GeneratedCharacterCannotBeNegative);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddSourceMapping_GeneratedLineCannotBacktrack",
    TestSourceMapGenerator_AddSourceMapping_GeneratedLineCannotBacktrack);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddSourceMapping_GeneratedCharacterCannotBeNegative",
    TestSourceMapGenerator_AddSourceMapping_GeneratedCharacterCannotBeNegative);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddSourceMapping_SourceIndexIsOutOfRange",
    TestSourceMapGenerator_AddSourceMapping_SourceIndexIsOutOfRange);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddSourceMapping_SourceLineCannotBeNegative",
    TestSourceMapGenerator_AddSourceMapping_SourceLineCannotBeNegative);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddSourceMapping_SourceCharacterCannotBeNegative",
    TestSourceMapGenerator_AddSourceMapping_SourceCharacterCannotBeNegative);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddNamedSourceMapping_GeneratedLineCannotBacktrack",
    TestSourceMapGenerator_AddNamedSourceMapping_GeneratedLineCannotBacktrack);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddNamedSourceMapping_GeneratedCharacterCannotBeNegative",
    TestSourceMapGenerator_AddNamedSourceMapping_GeneratedCharacterCannotBeNegative);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddNamedSourceMapping_SourceIndexIsOutOfRange",
    TestSourceMapGenerator_AddNamedSourceMapping_SourceIndexIsOutOfRange);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddNamedSourceMapping_SourceLineCannotBeNegative",
    TestSourceMapGenerator_AddNamedSourceMapping_SourceLineCannotBeNegative);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddNamedSourceMapping_SourceCharacterCannotBeNegative",
    TestSourceMapGenerator_AddNamedSourceMapping_SourceCharacterCannotBeNegative);
REGISTER_UNIT_TEST(
    "sourcemap.TestSourceMapGenerator_AddNamedSourceMapping_NameIndexIsOutOfRange",
    TestSourceMapGenerator_AddNamedSourceMapping_NameIndexIsOutOfRange);
