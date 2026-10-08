// api/encoder/encoder.cpp — encoder.go + stringtable.go: the AST binary
// encoder, string table, NodeIndexTable, and msgpack helpers for the
// structured data section.

#include "internal/api/encoder/encoder_internal.h"

#include "internal/ast/visitor.h"
#include "internal/core/types.h"
#include "internal/spanmap/spanmap.h"
#include "internal/tspath/tspath.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <tuple>

namespace tsc::api::encoder {

static_assert(static_cast<uint32_t>(KindLastUnaryOperator) <= 0x3f,
              "KindLastUnaryOperator exceeds the 6-bit commonData capacity "
              "(max 63)");

// ---------------------------------------------------------------------------
// little-endian byte writer (appendUint32s — encoder.go:655)
// ---------------------------------------------------------------------------

static void appendUint8(std::vector<uint8_t>& buf, uint8_t v) {
	buf.push_back(v);
}

static void appendLE32(std::vector<uint8_t>& buf, uint32_t v) {
	buf.push_back(static_cast<uint8_t>(v));
	buf.push_back(static_cast<uint8_t>(v >> 8));
	buf.push_back(static_cast<uint8_t>(v >> 16));
	buf.push_back(static_cast<uint8_t>(v >> 24));
}

static void appendUint32s(std::vector<uint8_t>& buf,
                          std::initializer_list<uint32_t> values) {
	for (uint32_t v : values) {
		appendLE32(buf, v);
	}
}

static void writeLE32(std::vector<uint8_t>& buf, size_t offset, uint32_t v) {
	buf[offset + 0] = static_cast<uint8_t>(v);
	buf[offset + 1] = static_cast<uint8_t>(v >> 8);
	buf[offset + 2] = static_cast<uint8_t>(v >> 16);
	buf[offset + 3] = static_cast<uint8_t>(v >> 24);
}

static void writeLE64(std::vector<uint8_t>& buf, size_t offset, uint64_t v) {
	writeLE32(buf, offset, static_cast<uint32_t>(v));
	writeLE32(buf, offset + 4, static_cast<uint32_t>(v >> 32));
}


// ---------------------------------------------------------------------------
// msgpack writers for the structured data section (encoder.go:865-907)
// ---------------------------------------------------------------------------

static void msgpackWriteArrayHeader(std::vector<uint8_t>& buf, size_t length) {
	if (length <= 0x0f) {
		appendUint8(buf, static_cast<uint8_t>(0x90 | length));
		return;
	}
	if (length <= 0xffff) {
		appendUint8(buf, 0xdc);
		appendUint8(buf, static_cast<uint8_t>(length >> 8));
		appendUint8(buf, static_cast<uint8_t>(length));
		return;
	}
	appendUint8(buf, 0xdd);
	appendUint8(buf, static_cast<uint8_t>(length >> 24));
	appendUint8(buf, static_cast<uint8_t>(length >> 16));
	appendUint8(buf, static_cast<uint8_t>(length >> 8));
	appendUint8(buf, static_cast<uint8_t>(length));
}

static void msgpackWriteUint(std::vector<uint8_t>& buf, uint32_t value) {
	if (value <= 0x7f) {
		appendUint8(buf, static_cast<uint8_t>(value));
		return;
	}
	if (value <= 0xff) {
		appendUint8(buf, 0xcc);
		appendUint8(buf, static_cast<uint8_t>(value));
		return;
	}
	if (value <= 0xffff) {
		appendUint8(buf, 0xcd);
		appendUint8(buf, static_cast<uint8_t>(value >> 8));
		appendUint8(buf, static_cast<uint8_t>(value));
		return;
	}
	appendUint8(buf, 0xce);
	appendUint8(buf, static_cast<uint8_t>(value >> 24));
	appendUint8(buf, static_cast<uint8_t>(value >> 16));
	appendUint8(buf, static_cast<uint8_t>(value >> 8));
	appendUint8(buf, static_cast<uint8_t>(value));
}

static void msgpackWriteString(std::vector<uint8_t>& buf, std::string_view s) {
	size_t n = s.size();
	if (n <= 0x1f) {
		appendUint8(buf, static_cast<uint8_t>(0xa0 | n));
	} else if (n <= 0xff) {
		appendUint8(buf, 0xd9);
		appendUint8(buf, static_cast<uint8_t>(n));
	} else if (n <= 0xffff) {
		appendUint8(buf, 0xda);
		appendUint8(buf, static_cast<uint8_t>(n >> 8));
		appendUint8(buf, static_cast<uint8_t>(n));
	} else {
		appendUint8(buf, 0xdb);
		appendUint8(buf, static_cast<uint8_t>(n >> 24));
		appendUint8(buf, static_cast<uint8_t>(n >> 16));
		appendUint8(buf, static_cast<uint8_t>(n >> 8));
		appendUint8(buf, static_cast<uint8_t>(n));
	}
	buf.insert(buf.end(), s.begin(), s.end());
}

static void msgpackWriteBool(std::vector<uint8_t>& buf, bool value) {
	appendUint8(buf, value ? 0xc3 : 0xc2);
}

// ---------------------------------------------------------------------------
// structured-data encoders (encoder.go:750-861)
// ---------------------------------------------------------------------------

// encodeFileReferences encodes a slice of FileReferences as a msgpack array of
// tuples into the structured data buffer. Returns the byte offset into the
// buffer, or noStructuredData (0xFFFFFFFF) if the slice is empty.
static uint32_t encodeFileReferences(const std::vector<FileReference*>& refs,
                                     PositionMap* positionMap,
                                     std::vector<uint8_t>& buf) {
	if (refs.empty()) {
		return noStructuredData;
	}
	uint32_t offset = static_cast<uint32_t>(buf.size());
	msgpackWriteArrayHeader(buf, refs.size());
	for (FileReference* ref : refs) {
		// Each entry is a 5-element tuple: [pos, end, fileName, resolutionMode,
		// preserve]
		msgpackWriteArrayHeader(buf, 5);
		msgpackWriteUint(buf, static_cast<uint32_t>(positionMap->UTF8ToUTF16(ref->pos())));
		msgpackWriteUint(buf, static_cast<uint32_t>(positionMap->UTF8ToUTF16(ref->end())));
		msgpackWriteString(buf, ref->FileName);
		msgpackWriteUint(buf, static_cast<uint32_t>(ref->ResolutionMode));
		msgpackWriteBool(buf, ref->Preserve);
	}
	return offset;
}

// encodeNodeIndexArray encodes a slice of LiteralLikeNodes as a msgpack array
// of uint node indices. Returns the byte offset into the buffer, or
// noStructuredData if the slice is empty.
static uint32_t encodeNodeIndexArray(
    const std::vector<Node*>& nodes,
    const std::unordered_map<Node*, uint32_t>* indexMap,
    std::vector<uint8_t>& buf) {
	if (nodes.empty()) {
		return noStructuredData;
	}
	uint32_t offset = static_cast<uint32_t>(buf.size());
	msgpackWriteArrayHeader(buf, nodes.size());
	for (Node* node : nodes) {
		msgpackWriteUint(buf, indexMap->at(node));
	}
	return offset;
}

// encodeModuleAugmentations encodes a slice of ModuleName nodes as a msgpack
// array of uint node indices. Returns the byte offset into the buffer, or
// noStructuredData if the slice is empty.
static uint32_t encodeModuleAugmentations(
    const std::vector<Node*>& nodes,
    const std::unordered_map<Node*, uint32_t>* indexMap,
    std::vector<uint8_t>& buf) {
	if (nodes.empty()) {
		return noStructuredData;
	}
	uint32_t offset = static_cast<uint32_t>(buf.size());
	msgpackWriteArrayHeader(buf, nodes.size());
	for (Node* node : nodes) {
		msgpackWriteUint(buf, indexMap->at(node));
	}
	return offset;
}

// encodeStringArray encodes a slice of strings as a msgpack array of strings.
// Returns the byte offset into the buffer, or noStructuredData if the slice is
// empty.
static uint32_t encodeStringArray(const std::vector<std::string>& strs,
                                  std::vector<uint8_t>& buf) {
	if (strs.empty()) {
		return noStructuredData;
	}
	uint32_t offset = static_cast<uint32_t>(buf.size());
	msgpackWriteArrayHeader(buf, strs.size());
	for (const std::string& s : strs) {
		msgpackWriteString(buf, s);
	}
	return offset;
}

static uint32_t encodeSpanMap(spanmap::SpanMap* m, PositionMap* virtualPositions,
                              PositionMap* originalPositions,
                              std::vector<uint8_t>& buf) {
	if (m == nullptr) {
		return noStructuredData;
	}
	auto segments = spanmap::Segments(m);
	uint32_t offset = static_cast<uint32_t>(buf.size());
	msgpackWriteArrayHeader(buf, segments.size());
	for (const auto& segment : segments) {
		int tupleLength = 5;
		if (segment.Features != spanmap::FeatureAll) {
			tupleLength = 6;
		}
		msgpackWriteArrayHeader(buf, tupleLength);
		int virtualStart = virtualPositions->UTF8ToUTF16(segment.VirtualStart);
		int virtualEnd = virtualPositions->UTF8ToUTF16(segment.VirtualEnd);
		int originalStart = originalPositions->UTF8ToUTF16(segment.OriginalStart);
		int originalEnd = originalPositions->UTF8ToUTF16(segment.OriginalEnd);
		msgpackWriteUint(buf, static_cast<uint32_t>(virtualStart));
		msgpackWriteUint(buf, static_cast<uint32_t>(virtualEnd - virtualStart));
		msgpackWriteUint(buf, static_cast<uint32_t>(originalStart));
		msgpackWriteUint(buf, static_cast<uint32_t>(originalEnd - originalStart));
		msgpackWriteUint(buf, static_cast<uint32_t>(segment.Kind));
		if (tupleLength == 6) {
			msgpackWriteUint(buf, static_cast<uint32_t>(segment.Features));
		}
	}
	return offset;
}

static uint32_t encodeDiagnosticDirectives(
    const std::vector<MappedDiagnosticDirective>* directives,
    PositionMap* virtualPositions, PositionMap* originalPositions,
    std::vector<uint8_t>& buf) {
	if (directives == nullptr || directives->empty()) {
		return noStructuredData;
	}
	uint32_t offset = static_cast<uint32_t>(buf.size());
	msgpackWriteArrayHeader(buf, directives->size());
	for (const auto& directive : *directives) {
		msgpackWriteArrayHeader(buf, 6);
		int originalStart = originalPositions->UTF8ToUTF16(directive.OriginalRange.pos());
		int originalEnd = originalPositions->UTF8ToUTF16(directive.OriginalRange.end());
		int virtualStart = virtualPositions->UTF8ToUTF16(directive.VirtualRange.pos());
		int virtualEnd = virtualPositions->UTF8ToUTF16(directive.VirtualRange.end());
		msgpackWriteUint(buf, static_cast<uint32_t>(originalStart));
		msgpackWriteUint(buf, static_cast<uint32_t>(originalEnd - originalStart));
		msgpackWriteUint(buf, static_cast<uint32_t>(virtualStart));
		msgpackWriteUint(buf, static_cast<uint32_t>(virtualEnd - virtualStart));
		msgpackWriteUint(buf, static_cast<uint32_t>(directive.Policy));
		msgpackWriteUint(buf, static_cast<uint32_t>(directive.UnusedCode));
	}
	return offset;
}

// ---------------------------------------------------------------------------
// forward declarations — getNodeData + encodeTree are defined below; the
// generated tables live in encoder_generated.cpp.
// ---------------------------------------------------------------------------

static uint32_t getNodeData(Node* node, stringTable* strs,
                            PositionMap* positionMap,
                            std::vector<uint8_t>& extendedData,
                            std::vector<uint8_t>& structuredData);

static std::tuple<std::vector<uint8_t>, NodeIndexTable*, gostd::Error>
encodeTree(Node* rootNode, SourceFile* sourceFile);

// encoder.go:318 — per-file ExternalModuleIndicatorOptions as a uint32 bitmask.
static uint32_t encodeParseOptions(ExternalModuleIndicatorOptions opts) {
	uint32_t bits = 0;
	if (opts.JSX != JsxEmit::None) {
		bits |= 1;
	}
	if (opts.Force) {
		bits |= 2;
	}
	return bits;
}

// ---------------------------------------------------------------------------
// encoder.go
// ---------------------------------------------------------------------------

// SourceFileHash returns the 128-bit content hash for a source file as a hex
// string.
std::string SourceFileHash(SourceFile* sourceFile) {
	Uint128 h = sourceFile->Hash;
	char buf[33];
	snprintf(buf, sizeof(buf), "%016llx%016llx",
	         static_cast<unsigned long long>(h.hi),
	         static_cast<unsigned long long>(h.lo));
	return std::string(buf, 32);
}

SourceFileDataKey nodeIndexTableKey = newSourceFileDataKey();

uint32_t NodeIndexTable::GetIndex(Node* node) {
	sortedOnce.run([this] {
		std::vector<uint32_t> idx;
		idx.reserve(Nodes.size());
		for (size_t i = 0; i < Nodes.size(); i++) {
			if (Nodes[i] != nullptr) {
				idx.push_back(static_cast<uint32_t>(i));
			}
		}
		std::vector<Node*>& nodes = Nodes;
		std::sort(idx.begin(), idx.end(), [&nodes](uint32_t a, uint32_t b) {
			return getNodeId(nodes[a]) < getNodeId(nodes[b]);
		});
		sortedIdx = std::move(idx);
	});
	NodeId target = getNodeId(node);
	auto [i, found] = binarySearchUniqueFunc(
	    sortedIdx, [this, target](int, uint32_t el) {
		    NodeId a = getNodeId(Nodes[el]);
		    return a < target ? -1 : (a > target ? 1 : 0);
	    });
	if (found) {
		return sortedIdx[i];
	}
	return 0;
}

// BuildNodeIndexTable walks the AST in the same order as encodeTree and builds
// a NodeIndexTable without performing the full binary encoding. This is used to
// eagerly create index tables for files that need node handles before
// getSourceFile is called. The indices produced are guaranteed to match those
// from EncodeSourceFile.
NodeIndexTable* BuildNodeIndexTable(SourceFile* sourceFile) {
	uint32_t nodeCount = 0;
	std::vector<Node*> nodeTable;
	// index 0 = nil sentinel
	nodeTable.reserve(sourceFile->NodeCount + 1);
	nodeTable.push_back(nullptr);

	auto* visitor = new NodeVisitor();
	NodeVisitor* v = visitor;
	v->factory = new NodeFactory();
	v->hooks.visitNodes = [&nodeCount, &nodeTable](NodeList* nodeList,
	                                              NodeVisitor* visitor) {
		if (nodeList == nullptr) {
			return nodeList;
		}
		nodeCount++;
		nodeTable.push_back(nullptr); // NodeLists are not *ast.Node
		visitor->visitSlice(nodeList->nodes);
		return nodeList;
	};
	v->hooks.visitModifiers = [](ModifierList* modifiers,
	                             NodeVisitor* visitor) {
		if (modifiers != nullptr && !modifiers->nodes.empty()) {
			NodeList nl;
			nl.loc = modifiers->loc;
			nl.nodes = modifiers->nodes;
			visitor->hooks.visitNodes(&nl, visitor);
		}
		return modifiers;
	};
	v->visit = [v, &nodeCount, &nodeTable, sourceFile](Node* node) {
		nodeCount++;
		nodeTable.push_back(node);
		v->visitEachChild(node);
		for (Node* jsdoc : node->jsDoc(sourceFile)) {
			v->visit(jsdoc);
		}
		return node;
	};

	Node* rootNode = sourceFile->asNode();
	// Index 1 = root node (matches encodeTree)
	nodeCount++;
	nodeTable.push_back(rootNode);

	v->visitEachChild(rootNode);
	for (Node* jsdoc : rootNode->jsDoc(sourceFile)) {
		v->visit(jsdoc);
	}

	return new NodeIndexTable{std::move(nodeTable), {}, {}};
}

NodeIndexTable* GetNodeIndexTable(SourceFile* sourceFile) {
	return sourceFile->GetOrComputeData<NodeIndexTable>(nodeIndexTableKey,
	                                                  BuildNodeIndexTable);
}

// EncodeSourceFile encodes an entire source file AST into the binary format.
// Returns the encoded bytes and a NodeIndexTable mapping encoder indices to
// AST nodes.
std::tuple<std::vector<uint8_t>, NodeIndexTable*, gostd::Error>
EncodeSourceFile(SourceFile* sourceFile) {
	auto encoded = encodeTree(sourceFile->asNode(), sourceFile);
	NodeIndexTable* nodeTable = std::get<1>(encoded);
	auto err = std::get<2>(encoded);
	if (err != nullptr) {
		return {{}, nullptr, err};
	}
	nodeTable = sourceFile->GetOrComputeData<NodeIndexTable>(
	    nodeIndexTableKey,
	    [nodeTable](SourceFile*) { return nodeTable; });
	return {std::move(std::get<0>(encoded)), nodeTable, nullptr};
}

// SetSourceFileLease sets the session-scoped lease ID in an encoded source
// file.
void SetSourceFileLease(std::vector<uint8_t>& data, uint64_t lease) {
	writeLE64(data, HeaderOffsetSourceFileLease, lease);
}

// EncodeNode encodes an arbitrary AST node and its descendants into the binary
// format. The sourceFile is needed to provide the source text for efficient
// string encoding. When encoding a non-SourceFile node, the header hash and
// parse options fields will be zero. Returns the encoded bytes and a
// NodeIndexTable mapping encoder indices to AST nodes.
std::tuple<std::vector<uint8_t>, NodeIndexTable*, gostd::Error>
EncodeNode(Node* node, SourceFile* sourceFile) {
	return encodeTree(node, sourceFile);
}

static std::tuple<std::vector<uint8_t>, NodeIndexTable*, gostd::Error>
encodeTree(Node* rootNode, SourceFile* sourceFile) {
	uint32_t parentIndex = 0, nodeCount = 0, prevIndex = 0;
	std::vector<uint8_t> extendedData;
	std::vector<uint8_t> structuredData;
	stringTable strs;
	PositionMap* positionMap = nullptr;
	if (rootNode->kind == Kind::SourceFile) {
		strs = newStringTable(sourceFile->Text(), sourceFile->TextCount);
		positionMap = sourceFile->GetPositionMap();
	} else {
		strs = newStringTable("", 0);
		if (sourceFile != nullptr) {
			positionMap = sourceFile->GetPositionMap();
		}
	}
	if (positionMap == nullptr) {
		positionMap = computePositionMap("");
	}
	auto utf16 = [positionMap](int pos) -> uint32_t {
		return static_cast<uint32_t>(positionMap->UTF8ToUTF16(pos));
	};
	int initialNodeCount = 0;
	if (sourceFile != nullptr) {
		initialNodeCount = sourceFile->NodeCount;
	}
	std::vector<uint8_t> nodes;
	nodes.reserve(static_cast<size_t>(initialNodeCount + 1) * NodeSize);

	// Build node index table for O(1) handle resolution.
	// Index 0 is a nil sentinel; real nodes start at index 1.
	std::vector<Node*> nodeTable;
	nodeTable.reserve(initialNodeCount + 1);
	nodeTable.push_back(nullptr); // index 0 = nil sentinel

	// Build a small map of nodes we need to track indices for (imports +
	// moduleAugmentations). Values start at 0 and are filled in during the
	// walk.
	std::unique_ptr<std::unordered_map<Node*, uint32_t>> nodeIndexMap;
	size_t sfExtendedDataOffset = 0; // byte offset in extendedData where SourceFile fields start
	if (rootNode->kind == Kind::SourceFile) {
		SourceFile* sf = rootNode->as<SourceFile>();
		size_t total = sf->imports.size() + sf->ModuleAugmentations.size();
		if (sf->ExternalModuleIndicator != nullptr &&
		    sf->ExternalModuleIndicator != rootNode) {
			total++;
		}
		if (total > 0) {
			nodeIndexMap = std::make_unique<std::unordered_map<Node*, uint32_t>>();
			nodeIndexMap->reserve(total);
			for (Node* imp : sf->imports) {
				(*nodeIndexMap)[imp] = 0;
			}
			for (Node* aug : sf->ModuleAugmentations) {
				(*nodeIndexMap)[aug] = 0;
			}
			if (sf->ExternalModuleIndicator != nullptr &&
			    sf->ExternalModuleIndicator != rootNode) {
				(*nodeIndexMap)[sf->ExternalModuleIndicator] = 0;
			}
		}
	}

	auto* visitor = new NodeVisitor();
	NodeVisitor* v = visitor;
	v->factory = new NodeFactory();
	v->hooks.visitNodes = [v, &nodeCount, &nodeTable, &nodes, &prevIndex,
	                       &parentIndex, &utf16](NodeList* nodeList,
	                                              NodeVisitor*) {
		if (nodeList == nullptr) {
			return nodeList;
		}

		nodeCount++;
		nodeTable.push_back(nullptr); // NodeLists are not *ast.Node
		if (prevIndex != 0) {
			// this is the next sibling of `prevNode`
			writeLE32(nodes, static_cast<size_t>(prevIndex) * NodeSize +
			                     NodeOffsetNext,
			          nodeCount);
		}

		appendUint32s(nodes,
		              {SyntaxKindNodeList, utf16(nodeList->pos()),
		               utf16(nodeList->end()), 0, parentIndex,
		               static_cast<uint32_t>(nodeList->nodes.size()),
		               static_cast<uint32_t>(boolToByte(
		                   nodeList->hasTrailingComma()))});

		uint32_t saveParentIndex = parentIndex;

		uint32_t currentIndex = nodeCount;
		prevIndex = 0;
		parentIndex = currentIndex;
		v->visitSlice(nodeList->nodes);
		prevIndex = currentIndex;
		parentIndex = saveParentIndex;

		return nodeList;
	};
	v->hooks.visitModifiers = [](ModifierList* modifiers, NodeVisitor* visitor) {
		if (modifiers != nullptr && !modifiers->nodes.empty()) {
			NodeList nl;
			nl.loc = modifiers->loc;
			nl.nodes = modifiers->nodes;
			visitor->hooks.visitNodes(&nl, visitor);
		}
		return modifiers;
	};
	v->visit = [v, sourceFile, &nodeCount, &nodeTable, &nodes, &prevIndex,
	            &parentIndex, &utf16, &strs, &extendedData, &structuredData,
	            positionMap, &nodeIndexMap](Node* node) {
		nodeCount++;
		nodeTable.push_back(node);
		if (prevIndex != 0) {
			// this is the next sibling of `prevNode`
			writeLE32(nodes, static_cast<size_t>(prevIndex) * NodeSize +
			                     NodeOffsetNext,
			          nodeCount);
		}

		appendUint32s(nodes,
		              {static_cast<uint32_t>(node->kind), utf16(node->pos()),
		               utf16(node->end()), 0, parentIndex,
		               getNodeData(node, &strs, positionMap, extendedData,
		                           structuredData),
		               static_cast<uint32_t>(node->flags)});

		if (nodeIndexMap != nullptr) {
			auto it = nodeIndexMap->find(node);
			if (it != nodeIndexMap->end()) {
				it->second = nodeCount;
			}
		}

		uint32_t saveParentIndex = parentIndex;

		uint32_t currentIndex = nodeCount;
		prevIndex = 0;
		parentIndex = currentIndex;
		v->visitEachChild(node);
		if (sourceFile != nullptr) {
			for (Node* jsdoc : node->jsDoc(sourceFile)) {
				v->visit(jsdoc);
			}
		}
		prevIndex = currentIndex;
		parentIndex = saveParentIndex;
		return node;
	};

	appendUint32s(nodes, {0, 0, 0, 0, 0, 0, 0});

	nodeCount++;
	parentIndex++;
	nodeTable.push_back(rootNode); // index 1 = root node

	sfExtendedDataOffset = extendedData.size();
	appendUint32s(nodes,
	              {static_cast<uint32_t>(rootNode->kind), utf16(rootNode->pos()),
	               utf16(rootNode->end()), 0, 0,
	               getNodeData(rootNode, &strs, positionMap, extendedData,
	                           structuredData),
	               static_cast<uint32_t>(rootNode->flags)});

	v->visitEachChild(rootNode);
	if (sourceFile != nullptr) {
		for (Node* jsdoc : rootNode->jsDoc(sourceFile)) {
			v->visit(jsdoc);
		}
	}

	Uint128 hash;
	uint32_t parseOpts = 0;
	if (rootNode->kind == Kind::SourceFile) {
		hash = sourceFile->Hash;
		parseOpts = encodeParseOptions(
		    sourceFile->ParseOptions().ExternalModuleIndicatorOptions);

		// Encode imports, moduleAugmentations, and ambientModuleNames into
		// structured data, and patch the placeholder offsets in the
		// SourceFile extended data.
		SourceFile* sf = rootNode->as<SourceFile>();
		uint32_t importsOffset =
		    encodeNodeIndexArray(sf->imports, nodeIndexMap.get(), structuredData);
		uint32_t moduleAugmentationsOffset = encodeModuleAugmentations(
		    sf->ModuleAugmentations, nodeIndexMap.get(), structuredData);
		uint32_t ambientModuleNamesOffset =
		    encodeStringArray(sf->AmbientModuleNames, structuredData);
		// Patch the 3 placeholder uint32s at sfExtendedDataOffset + 32, 36, 40
		writeLE32(extendedData, sfExtendedDataOffset + 32, importsOffset);
		writeLE32(extendedData, sfExtendedDataOffset + 36,
		          moduleAugmentationsOffset);
		writeLE32(extendedData, sfExtendedDataOffset + 40,
		          ambientModuleNamesOffset);
		// Patch externalModuleIndicator node index at offset 44
		uint32_t externalModuleIndicatorIndex = 0;
		if (sf->ExternalModuleIndicator != nullptr) {
			if (sf->ExternalModuleIndicator == rootNode) {
				externalModuleIndicatorIndex = 1; // root node index
			} else {
				externalModuleIndicatorIndex =
				    nodeIndexMap->at(sf->ExternalModuleIndicator);
			}
		}
		writeLE32(extendedData, sfExtendedDataOffset + 44,
		          externalModuleIndicatorIndex);
	}

	uint32_t metadata = static_cast<uint32_t>(ProtocolVersion) << 24;
	size_t offsetStringTableOffsets = HeaderSize;
	size_t offsetStringTableData = HeaderSize + strs.offsets.size() * 4;
	size_t offsetExtendedData = offsetStringTableData + strs.stringLength();
	size_t offsetStructuredData = offsetExtendedData + extendedData.size();
	size_t offsetNodes = offsetStructuredData + structuredData.size();

	std::vector<uint8_t> header;
	appendUint32s(header,
	              {metadata,
	               static_cast<uint32_t>(hash.lo),
	               static_cast<uint32_t>(hash.lo >> 32),
	               static_cast<uint32_t>(hash.hi),
	               static_cast<uint32_t>(hash.hi >> 32),
	               parseOpts,
	               static_cast<uint32_t>(offsetStringTableOffsets),
	               static_cast<uint32_t>(offsetStringTableData),
	               static_cast<uint32_t>(offsetExtendedData),
	               static_cast<uint32_t>(offsetStructuredData),
	               static_cast<uint32_t>(offsetNodes),
	               0, 0, // source file ID
	               0, 0, // source file lease ID
	               0}); // binder data offset

	std::vector<uint8_t> strsBytes = strs.encode();

	std::vector<uint8_t> result;
	result.reserve(header.size() + strsBytes.size() + extendedData.size() +
	               structuredData.size() + nodes.size());
	result.insert(result.end(), header.begin(), header.end());
	result.insert(result.end(), strsBytes.begin(), strsBytes.end());
	result.insert(result.end(), extendedData.begin(), extendedData.end());
	result.insert(result.end(), structuredData.begin(), structuredData.end());
	result.insert(result.end(), nodes.begin(), nodes.end());
	return {std::move(result), new NodeIndexTable{std::move(nodeTable), {}, {}},
	        nullptr};
}

static uint32_t getNodeData(Node* node, stringTable* strs,
                            PositionMap* positionMap,
                            std::vector<uint8_t>& extendedData,
                            std::vector<uint8_t>& structuredData) {
	uint32_t t = getNodeDataType(node);
	switch (t) {
	case NodeDataTypeChildren:
		return t | getNodeCommonData(node) |
		       static_cast<uint32_t>(getChildrenPropertyMask(node));
	case NodeDataTypeString:
		return t | getNodeCommonData(node) | recordNodeStrings(node, strs);
	case NodeDataTypeExtendedData:
		return t | getNodeCommonData(node) |
		       recordExtendedData(node, strs, positionMap, extendedData,
		                          structuredData);
	default:
		TSC_UNREACHABLE("unreachable");
	}
}

// ---------------------------------------------------------------------------
// Hand-written extended data encoding functions for literal nodes that were
// previously string-type but whose TokenFlags/TemplateFlags cannot fit in
// 6 bits. (encoder.go:920-951)
// ---------------------------------------------------------------------------

void recordExtendedData_StringLiteral(Node* node, stringTable* strs,
                                             PositionMap*,
                                             std::vector<uint8_t>& extendedData,
                                             std::vector<uint8_t>&) {
	auto* n = node->as<StringLiteral>();
	uint32_t textIndex = strs->add(n->Text, node->kind, node->pos(), node->end());
	appendUint32s(extendedData, {textIndex, static_cast<uint32_t>(n->TokenFlags)});
}

void recordExtendedData_NumericLiteral(Node* node, stringTable* strs,
                                              PositionMap*,
                                              std::vector<uint8_t>& extendedData,
                                              std::vector<uint8_t>&) {
	auto* n = node->as<NumericLiteral>();
	uint32_t textIndex = strs->add(n->Text, node->kind, node->pos(), node->end());
	appendUint32s(extendedData, {textIndex, static_cast<uint32_t>(n->TokenFlags)});
}

void recordExtendedData_BigIntLiteral(Node* node, stringTable* strs,
                                             PositionMap*,
                                             std::vector<uint8_t>& extendedData,
                                             std::vector<uint8_t>&) {
	auto* n = node->as<BigIntLiteral>();
	uint32_t textIndex = strs->add(n->Text, node->kind, node->pos(), node->end());
	appendUint32s(extendedData, {textIndex, static_cast<uint32_t>(n->TokenFlags)});
}

void recordExtendedData_RegularExpressionLiteral(
    Node* node, stringTable* strs, PositionMap*,
    std::vector<uint8_t>& extendedData, std::vector<uint8_t>&) {
	auto* n = node->as<RegularExpressionLiteral>();
	uint32_t textIndex = strs->add(n->Text, node->kind, node->pos(), node->end());
	appendUint32s(extendedData, {textIndex, static_cast<uint32_t>(n->TokenFlags)});
}

void recordExtendedData_NoSubstitutionTemplateLiteral(
    Node* node, stringTable* strs, PositionMap*,
    std::vector<uint8_t>& extendedData, std::vector<uint8_t>&) {
	auto* n = node->as<NoSubstitutionTemplateLiteral>();
	uint32_t textIndex = strs->add(n->Text, node->kind, node->pos(), node->end());
	appendUint32s(extendedData,
	              {textIndex, static_cast<uint32_t>(n->TemplateFlags)});
}

void recordExtendedData_TemplateHead(Node* node, stringTable* strs,
                                            PositionMap*,
                                            std::vector<uint8_t>& extendedData,
                                            std::vector<uint8_t>&) {
	auto* n = node->as<TemplateHead>();
	uint32_t textIndex = strs->add(n->Text, node->kind, node->pos(), node->end());
	uint32_t rawTextIndex =
	    strs->add(n->RawText, node->kind, node->pos(), node->end());
	appendUint32s(extendedData,
	              {textIndex, rawTextIndex, static_cast<uint32_t>(n->TemplateFlags)});
}

void recordExtendedData_TemplateMiddle(Node* node, stringTable* strs,
                                              PositionMap*,
                                              std::vector<uint8_t>& extendedData,
                                              std::vector<uint8_t>&) {
	auto* n = node->as<TemplateMiddle>();
	uint32_t textIndex = strs->add(n->Text, node->kind, node->pos(), node->end());
	uint32_t rawTextIndex =
	    strs->add(n->RawText, node->kind, node->pos(), node->end());
	appendUint32s(extendedData,
	              {textIndex, rawTextIndex, static_cast<uint32_t>(n->TemplateFlags)});
}

void recordExtendedData_TemplateTail(Node* node, stringTable* strs,
                                            PositionMap*,
                                            std::vector<uint8_t>& extendedData,
                                            std::vector<uint8_t>&) {
	auto* n = node->as<TemplateTail>();
	uint32_t textIndex = strs->add(n->Text, node->kind, node->pos(), node->end());
	uint32_t rawTextIndex =
	    strs->add(n->RawText, node->kind, node->pos(), node->end());
	appendUint32s(extendedData,
	              {textIndex, rawTextIndex, static_cast<uint32_t>(n->TemplateFlags)});
}

void recordExtendedData_SourceFile(Node* node, stringTable* strs,
                                          PositionMap* positionMap,
                                          std::vector<uint8_t>& extendedData,
                                          std::vector<uint8_t>& structuredData) {
	SourceFile* sf = node->as<SourceFile>();
	uint32_t textIndex =
	    strs->add(sf->Text(), sf->kind, sf->pos(), sf->end());
	uint32_t originalTextIndex = textIndex;
	if (sf->OriginalText() != sf->Text()) {
		originalTextIndex = strs->add(sf->OriginalText(), Kind::Unknown, 0, 0);
	}
	uint32_t fileNameIndex = strs->add(sf->FileName(), Kind::Unknown, 0, 0);
	uint32_t pathIndex = strs->add(sf->Path(), Kind::Unknown, 0, 0);
	uint32_t referencedFilesOffset =
	    encodeFileReferences(sf->ReferencedFiles, positionMap, structuredData);
	uint32_t typeRefDirectivesOffset = encodeFileReferences(
	    sf->TypeReferenceDirectives, positionMap, structuredData);
	uint32_t libRefDirectivesOffset = encodeFileReferences(
	    sf->LibReferenceDirectives, positionMap, structuredData);
	uint32_t spanMapOffset = noStructuredData;
	if (spanmap::SpanMap* spanMap = sf->SpanMap(); spanMap != nullptr) {
		spanMapOffset = encodeSpanMap(spanMap, positionMap,
		                              computePositionMap(sf->OriginalText()),
		                              structuredData);
	}
	std::vector<std::string> supplementalFileNames;
	if (const auto* supplemental = sf->SupplementalSourceFiles();
	    supplemental != nullptr) {
		supplementalFileNames =
		    mapVec<std::string>(*supplemental,
		                        [](SourceFile* file) { return file->FileName(); });
	}
	uint32_t supplementalFileNamesOffset =
	    encodeStringArray(supplementalFileNames, structuredData);
	uint32_t canonicalFileNameIndex = noStructuredData;
	if (SourceFile* canonical = sf->CanonicalSourceFile();
	    canonical != nullptr) {
		canonicalFileNameIndex =
		    strs->add(canonical->FileName(), Kind::Unknown, 0, 0);
	}
	uint32_t contentMapperIndex = noStructuredData;
	if (std::string contentMapper = sf->ContentMapper();
	    !contentMapper.empty()) {
		contentMapperIndex = strs->add(contentMapper, Kind::Unknown, 0, 0);
	}
	uint32_t virtualFileNameIndex = noStructuredData;
	if (std::string virtualFileName = sf->VirtualFileName();
	    !virtualFileName.empty()) {
		virtualFileNameIndex = strs->add(virtualFileName, Kind::Unknown, 0, 0);
	}
	uint32_t diagnosticDirectivesOffset = encodeDiagnosticDirectives(
	    sf->DiagnosticDirectives(), positionMap,
	    computePositionMap(sf->OriginalText()), structuredData);
	// imports, moduleAugmentations, ambientModuleNames offsets are
	// placeholders; they will be patched after the tree walk when node indices
	// are known.
	appendUint32s(extendedData,
	              {textIndex, fileNameIndex, pathIndex,
	               static_cast<uint32_t>(sf->LanguageVariant),
	               static_cast<uint32_t>(sf->ScriptKind), referencedFilesOffset,
	               typeRefDirectivesOffset, libRefDirectivesOffset,
	               noStructuredData, noStructuredData, noStructuredData, 0,
	               originalTextIndex, spanMapOffset,
	               supplementalFileNamesOffset, canonicalFileNameIndex,
	               contentMapperIndex, virtualFileNameIndex,
	               diagnosticDirectivesOffset});
}

// Hand-written commonData encoding functions for nodes whose non-bool data
// members cannot be automatically encoded by the generator. Each function
// packs relevant fields into the 6-bit commonData area (bits 24-29) of the
// 32-bit node data word. (encoder.go:909-918)

uint32_t getNodeCommonData_SyntheticExpression(Node*) {
	// SyntheticExpression is an internal compiler node that is never part of a
	// parsed AST. It should never be encoded.
	TSC_UNREACHABLE("SyntheticExpression should never be encoded");
}

}  // namespace tsc::api::encoder
