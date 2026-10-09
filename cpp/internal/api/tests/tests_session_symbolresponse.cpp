// Port of tsc/internal/api/session_symbolresponse_test.go (package api).
#include <string>

#include "internal/api/proto.h"
#include "internal/api/session.h"
#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/parser/parser.h"
#include "internal/testutil/testutil.h"
#include "internal/tspath/tspath.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;

SourceFile* parseAndBind(const std::string& fileName,
                         const std::string& text) {
	SourceFile* sourceFile = tsc::parseSourceFile(
	    SourceFileParseOptions{
	        .FileName = fileName,
	        .Path = std::string(tsc::tspath::Path(fileName)),
	    },
	    text, ScriptKind::TS);
	bindSourceFile(sourceFile);
	return sourceFile;
}

// newTestSnapshotData — session_symbolresponse_test.go:32.
snapshotData* newTestSnapshotData() {
	auto* sd = new snapshotData();
	sd->handle = 1;
	return sd;
}

void TestFileSymbolResponseSerializesDescriptorOnce(T* t) {
	t->Parallel();
	auto* sourceFile =
	    parseAndBind("/file.ts", "class C { property = 1 }");

	auto response =
	    newFileSymbolResponse(sourceFile->Statements->nodes[0]->symbol());
	auto [encoded, err] = json::marshal(*response);
	assert::Assert(t, err.empty());
	size_t count = 0;
	size_t pos = 0;
	while ((pos = encoded.find("\"contentHash\"", pos)) !=
	       std::string::npos) {
		count++;
		pos++;
	}
	assert::Equal(t, (int)count, 1);
}

void TestFileOwnedSymbolsAreNotRegisteredInSnapshot(T* t) {
	t->Parallel();
	auto* sourceFile =
	    parseAndBind("/file.ts", "export class C { property = 1 }");
	auto* sd = newTestSnapshotData();

	auto response = sd->newSymbolResponse(
	    sourceFile->Statements->nodes[0]->symbol(), "/tsconfig.json");
	assert::Assert(t, response->Reference.Kind == SymbolOwnerKind::File);
	assert::Equal(t, (int)sd->symbolRegistry.size(), 0);
	assert::Equal(t, (int)sd->symbolCanonicalProjects.size(), 0);
}

void TestTransientSymbolWithFileDeclarationIsSnapshotOwned(T* t) {
	t->Parallel();
	auto* sourceFile =
	    parseAndBind("/file.ts", "export class C { property = 1 }");
	auto* classNode = sourceFile->Statements->nodes[0];
	auto* symbol = new Symbol();
	symbol->flags = SymbolFlagsClass | SymbolFlagsTransient;
	symbol->data->name = "C";
	symbol->data->declarations = {classNode};
	auto* sd = newTestSnapshotData();

	auto response = sd->newSymbolResponse(symbol, "/tsconfig.json");
	assert::Assert(t, response->Reference.Kind == SymbolOwnerKind::Snapshot);
	auto [resolved, err] = sd->resolveSymbolHandle(response->Reference.Id);
	assert::NilError(t, err);
	assert::Assert(t, resolved == symbol);
}

void TestSymbolReferencesIdentifyOwnerWithoutDescriptor(T* t) {
	t->Parallel();
	auto* sourceFile =
	    parseAndBind("/file.ts", "export class C { property = 1 }");
	auto* classSymbol = sourceFile->Statements->nodes[0]->symbol();

	auto reference = newSymbolReference(classSymbol);
	assert::Assert(t, reference->Id == SymbolHandle(classSymbol));
	assert::Equal(t, reference->File,
	              std::to_string(sourceFileNodeID(sourceFile)));
	auto [encoded, err] = json::marshal(*reference);
	assert::Assert(t, err.empty());
	assert::Assert(t, encoded.find("contentHash") == std::string::npos);

	// A file-owned symbol's relationships are references into the same file.
	auto* member = classSymbol->data->members["property"];
	auto response = newFileSymbolResponse(member);
	assert::Assert(t, response->Parent != nullptr &&
	                      response->Parent->Id == reference->Id &&
	                      response->Parent->File == reference->File);
}

void TestContentMappedSymbolsAreSnapshotOwned(T* t) {
	t->Parallel();
	auto* sourceFile = tsc::parseSourceFile(
	    SourceFileParseOptions{
	        .FileName = "/component.vue.ts",
	        .Path = "/component.vue.ts",
	    },
	    "export class C { property = 1 }", ScriptKind::TS);
	ContentMapperSourceFileInfo mapperInfo;
	mapperInfo.ContentMapper = "mapper";
	sourceFile->SetContentMapperInfo(mapperInfo);
	bindSourceFile(sourceFile);
	auto* classSymbol = sourceFile->Statements->nodes[0]->symbol();
	auto* sd = newTestSnapshotData();

	auto response = sd->newSymbolResponse(classSymbol, "/tsconfig.json");
	assert::Assert(t, response->Reference.Kind ==
	                      SymbolOwnerKind::Snapshot);
	assert::Assert(t, response->Reference.File == nullptr);
	assert::Assert(t, response->Reference.Snapshot == SnapshotID{1});
	auto [resolved, err] = sd->resolveSymbolHandle(response->Reference.Id);
	assert::NilError(t, err);
	assert::Assert(t, resolved == classSymbol);

	auto reference = newSymbolReference(classSymbol->data->members["property"]);
	assert::Assert(t, reference->File.empty());
}

} // namespace
} // namespace tsc::api

REGISTER_UNIT_TEST("api.TestFileSymbolResponseSerializesDescriptorOnce",
                   tsc::api::TestFileSymbolResponseSerializesDescriptorOnce);
REGISTER_UNIT_TEST(
    "api.TestFileOwnedSymbolsAreNotRegisteredInSnapshot",
    tsc::api::TestFileOwnedSymbolsAreNotRegisteredInSnapshot);
REGISTER_UNIT_TEST(
    "api.TestTransientSymbolWithFileDeclarationIsSnapshotOwned",
    tsc::api::TestTransientSymbolWithFileDeclarationIsSnapshotOwned);
REGISTER_UNIT_TEST(
    "api.TestSymbolReferencesIdentifyOwnerWithoutDescriptor",
    tsc::api::TestSymbolReferencesIdentifyOwnerWithoutDescriptor);
REGISTER_UNIT_TEST("api.TestContentMappedSymbolsAreSnapshotOwned",
                   tsc::api::TestContentMappedSymbolsAreSnapshotOwned);
