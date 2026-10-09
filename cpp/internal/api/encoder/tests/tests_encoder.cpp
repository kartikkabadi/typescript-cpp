// Port of tsc/internal/api/encoder/encoder_test.go (package encoder_test).
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "internal/api/encoder/encoder.h"
#include "internal/ast/ast.h"
#include "internal/ast/kind.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/parser/parser.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::api::encoder {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace baseline = tsc::testutil::baseline;

uint32_t readUint32(const std::vector<uint8_t>& buf, int offset) {
	return uint32_t(buf[offset]) | (uint32_t(buf[offset + 1]) << 8) |
	       (uint32_t(buf[offset + 2]) << 16) |
	       (uint32_t(buf[offset + 3]) << 24);
}

std::string encodedString(const std::vector<uint8_t>& buf, uint32_t index) {
	uint32_t stringOffsets = readUint32(buf, HeaderOffsetStringOffsets);
	uint32_t stringData = readUint32(buf, HeaderOffsetStringData);
	uint32_t start = readUint32(buf, int(stringOffsets) + int(index) * 4);
	uint32_t end = readUint32(buf, int(stringOffsets) + int(index) * 4 + 4);
	return std::string(reinterpret_cast<const char*>(
	                       buf.data() + stringData + start),
	                   end - start);
}

std::string formatEncodedSourceFile(const std::vector<uint8_t>& encoded) {
	std::ostringstream result;
	uint32_t offsetNodes = readUint32(encoded, HeaderOffsetNodes);
	uint32_t offsetStringOffsets =
	    readUint32(encoded, HeaderOffsetStringOffsets);
	uint32_t offsetStrings = readUint32(encoded, HeaderOffsetStringData);
	std::function<std::string(uint32_t)> getIndent =
	    [&](uint32_t parentIndex) -> std::string {
		if (parentIndex == 0) {
			return "";
		}
		return "  " +
		       getIndent(readUint32(
		           encoded, int(offsetNodes) + int(parentIndex) * NodeSize +
		                        NodeOffsetParent));
	};
	int j = 1;
	for (size_t i = size_t(offsetNodes) + NodeSize; i < encoded.size();
	     i += NodeSize) {
		uint32_t kind = readUint32(encoded, int(i) + NodeOffsetKind);
		uint32_t pos = readUint32(encoded, int(i) + NodeOffsetPos);
		uint32_t end = readUint32(encoded, int(i) + NodeOffsetEnd);
		uint32_t parentIndex =
		    readUint32(encoded, int(i) + NodeOffsetParent);
		result << getIndent(parentIndex);
		if (kind == SyntaxKindNodeList) {
			result << "NodeList";
		} else {
			result << "Kind" << kindToString(Kind(kind));
		}
		uint32_t data = readUint32(encoded, int(i) + NodeOffsetData);
		uint32_t dataType = data & NodeDataTypeMask;
		if (Kind(kind) == Kind::Identifier ||
		    dataType == NodeDataTypeString) {
			uint32_t stringIndex = data & NodeDataStringIndexMask;
			uint32_t strStart = readUint32(
			    encoded, int(offsetStringOffsets) + int(stringIndex) * 4);
			uint32_t strEnd =
			    readUint32(encoded, int(offsetStringOffsets) +
			                            int(stringIndex) * 4 + 4);
			std::string str(
			    reinterpret_cast<const char*>(
			        encoded.data() + offsetStrings + strStart),
			    strEnd - strStart);
			result << " \"" << str << "\"";
		}
		result << " [" << pos << ", " << end << "), i=" << j
		       << ", next=" << int(encoded[i + NodeOffsetNext]) << "\n";
		j++;
	}
	return result.str();
}

void TestEncodeSourceFile(T* t) {
	t->Parallel();
	assert::Equal(t, (int)HeaderSize, 64);
	assert::Equal(t, (int)NodeSize, 28);;
	SourceFile* sourceFile = parseSourceFile(
	    SourceFileParseOptions{
	        .FileName = "/test.ts",
	        .Path = "/test.ts",
	    },
	    "import { bar } from \"bar\";\nexport function foo<T, U>(a: string, "
	    "b: string): any {}\nfoo();",
	    ScriptKind::TS);
	t->Run("baseline", [sourceFile](T* t) {
		t->Parallel();
		auto [buf, table, err] = EncodeSourceFile(sourceFile);
		assert::NilError(t, err);

		std::string str = formatEncodedSourceFile(buf);
		baseline::Run(t, "encodeSourceFile.txt", str,
		              baseline::Options{.Subfolder = "api"});
	});
}
REGISTER_UNIT_TEST("encoder.TestEncodeSourceFile", TestEncodeSourceFile);

void TestEncodeContentMapperSourceFileMetadata(T* t) {
	t->Parallel();
	if (ProtocolVersion != 9) {
		t->Fatalf("protocol version = %d, want 9", {int(ProtocolVersion)});
	}
	SourceFile* sourceFile = parseSourceFile(
	    SourceFileParseOptions{
	        .FileName = "/component.vue",
	        .Path = "/component.vue",
	    },
	    "\xF0\x9F\x98\x80virtual", ScriptKind::TS);
	sourceFile->SetContentMapperInfo(ContentMapperSourceFileInfo{
	    .ContentMapper = "mapper@1.0.0",
	    .VirtualFileName = "/component.vue.ts",
	    .OriginalText = "\xF0\x9F\x98\x80original",
	    .DiagnosticDirectives = std::vector<MappedDiagnosticDirective>{
	        MappedDiagnosticDirective{
	            .OriginalRange = TextRange{4, 5},
	            .VirtualRange = TextRange{4, 11},
	            .Policy = MappedDiagnosticDirectivePolicy::Expect,
	            .UnusedCode = 2578,
	            .UnusedMessageText = "Unused framework directive.",
	            .Source = "mapper",
	        }},
	});

	auto [buf, table, err] = EncodeSourceFile(sourceFile);
	assert::NilError(t, err);
	uint32_t nodesOffset = readUint32(buf, HeaderOffsetNodes);
	uint32_t rootData =
	    readUint32(buf, int(nodesOffset) + NodeSize + NodeOffsetData);
	uint32_t extendedOffset =
	    readUint32(buf, HeaderOffsetExtendedData) +
	    (rootData & NodeDataStringIndexMask);
	if (int(extendedOffset) + 76 > int(buf.size())) {
		t->Fatalf("invalid extended offset %d (nodes=%d rootData=%#x "
		          "extendedData=%d len=%d)",
		          {extendedOffset, nodesOffset, rootData,
		           readUint32(buf, HeaderOffsetExtendedData),
		           int(buf.size())});
		return;
	}
	uint32_t contentMapperIndex = readUint32(buf, int(extendedOffset) + 64);
	uint32_t virtualFileNameIndex =
	    readUint32(buf, int(extendedOffset) + 68);
	uint32_t diagnosticDirectivesOffset =
	    readUint32(buf, int(extendedOffset) + 72);
	assert::Equal(t, encodedString(buf, contentMapperIndex),
	              std::string("mapper@1.0.0"));
	assert::Equal(t, encodedString(buf, virtualFileNameIndex),
	              std::string("/component.vue.ts"));
	uint32_t structuredDataOffset =
	    readUint32(buf, HeaderOffsetStructuredData);
	uint32_t directiveOffset =
	    structuredDataOffset + diagnosticDirectivesOffset;
	std::vector<uint8_t> expected{
	    0x91,              // one directive
	    0x96,              // six-element tuple
	    2,      1,         // original range [2, 3) in UTF-16
	    2,      7,         // virtual range [2, 9) in UTF-16
	    1,                 // expect policy
	    0xcd,   10,   18,  // unused diagnostic code 2578
	};
	std::vector<uint8_t> actual(buf.begin() + directiveOffset,
	                            buf.begin() + directiveOffset + 10);
	assert::DeepEqual(t, actual, expected);
}
REGISTER_UNIT_TEST("encoder.TestEncodeContentMapperSourceFileMetadata",
                   TestEncodeContentMapperSourceFileMetadata);

void TestEncodeSourceFileWithUnicodeEscapes(T* t) {
	t->Parallel();
	SourceFile* sourceFile = parseSourceFile(
	    SourceFileParseOptions{
	        .FileName = "/test.ts",
	        .Path = "/test.ts",
	    },
	    "let a = \"\xF0\x9F\x98\x83\"; let b = \"\\ud83d\\ude03\"; let c = "
	    "\"\\udc00\\ud83d\\ude03\"; let d = \"\\ud83d\\ud83d\\ude03\"",
	    ScriptKind::TS);
	t->Run("baseline", [sourceFile](T* t) {
		t->Parallel();
		auto [buf, table, err] = EncodeSourceFile(sourceFile);
		assert::NilError(t, err);

		std::string str = formatEncodedSourceFile(buf);
		baseline::Run(t, "encodeSourceFileWithUnicodeEscapes.txt", str,
		              baseline::Options{.Subfolder = "api"});
	});
}
REGISTER_UNIT_TEST("encoder.TestEncodeSourceFileWithUnicodeEscapes",
                   TestEncodeSourceFileWithUnicodeEscapes);

void TestBuildNodeIndexTableMatchesEncode(T* t) {
	t->Parallel();
	SourceFile* sourceFile = parseSourceFile(
	    SourceFileParseOptions{
	        .FileName = "/test.ts",
	        .Path = "/test.ts",
	    },
	    "import { bar } from \"bar\";\nexport function foo<T, U>(a: string, "
	    "b: string): any {}\nfoo();",
	    ScriptKind::TS);

	auto [buf, encodeTable, err] = EncodeSourceFile(sourceFile);
	assert::NilError(t, err);

	NodeIndexTable* buildTable = BuildNodeIndexTable(sourceFile);

	// Both tables should produce identical Nodes slices
	assert::Equal(t, int(buildTable->Nodes.size()),
	              int(encodeTable->Nodes.size()),
	              {"Nodes slice length mismatch"});

	// Every index should map to the same node
	for (size_t i = 0; i < encodeTable->Nodes.size(); i++) {
		assert::Equal(t, buildTable->Nodes[i], encodeTable->Nodes[i],
		              gostd::sprintf("node mismatch at index %d", {int(i)}));
	}

	// GetIndex on both tables should agree for every non-nil node
	for (size_t i = 0; i < encodeTable->Nodes.size(); i++) {
		Node* node = encodeTable->Nodes[i];
		if (node == nullptr) {
			continue;
		}
		uint32_t encIdx = encodeTable->GetIndex(node);
		uint32_t buildIdx = buildTable->GetIndex(node);
		assert::Equal(
		    t, encIdx, uint32_t(i),
		    gostd::sprintf("encodeTable.GetIndex mismatch at index %d, "
		                   "node kind=%s",
		                   {int(i),
		                    std::string(kindToString(node->kind))}));
		assert::Equal(
		    t, buildIdx, encIdx,
		    gostd::sprintf("buildTable.GetIndex mismatch for node kind=%s",
		                   {std::string(kindToString(node->kind))}));
	}
}
REGISTER_UNIT_TEST("encoder.TestBuildNodeIndexTableMatchesEncode",
                   TestBuildNodeIndexTableMatchesEncode);

}  // namespace
}  // namespace tsc::api::encoder
