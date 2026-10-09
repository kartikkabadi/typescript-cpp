// api/encoder/encoder.h — tsc::api::encoder: the binary AST wire format
// (encoder.go, decoder.go, stringtable.go + generated tables).
//
// Source File Binary Format — see encoder.go for the full spec: 64-byte header,
// string offsets, string data, extended node data, structured (msgpack) data,
// then 28-byte node records.
#pragma once

#include "internal/ast/ast.h"
#include "internal/gostd/gostd.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tsc::api::encoder {

// ---------------------------------------------------------------------------
// Layout constants (encoder.go)
// ---------------------------------------------------------------------------

inline constexpr int NodeOffsetKind = 0 * 4;
inline constexpr int NodeOffsetPos = 1 * 4;
inline constexpr int NodeOffsetEnd = 2 * 4;
inline constexpr int NodeOffsetNext = 3 * 4;
inline constexpr int NodeOffsetParent = 4 * 4;
inline constexpr int NodeOffsetData = 5 * 4;
inline constexpr int NodeOffsetFlags = 6 * 4;
// NodeSize is the number of bytes that represents a single node in the encoded format.
inline constexpr int NodeSize = 7 * 4;

inline constexpr uint32_t NodeDataTypeChildren = 0u << 30;
inline constexpr uint32_t NodeDataTypeString = 1u << 30;
inline constexpr uint32_t NodeDataTypeExtendedData = 2u << 30;

inline constexpr uint32_t NodeDataTypeMask = 0xc0000000u;
inline constexpr uint32_t NodeDataChildMask = 0x000000ffu;
inline constexpr uint32_t NodeDataStringIndexMask = 0x00ffffffu;

inline constexpr uint32_t SyntaxKindNodeList = 0xffffffffu;

inline constexpr int HeaderOffsetMetadata = 0 * 4;
inline constexpr int HeaderOffsetHashLo0 = 1 * 4;
inline constexpr int HeaderOffsetHashLo1 = 2 * 4;
inline constexpr int HeaderOffsetHashHi0 = 3 * 4;
inline constexpr int HeaderOffsetHashHi1 = 4 * 4;
inline constexpr int HeaderOffsetParseOptions = 5 * 4;
inline constexpr int HeaderOffsetStringOffsets = 6 * 4;
inline constexpr int HeaderOffsetStringData = 7 * 4;
inline constexpr int HeaderOffsetExtendedData = 8 * 4;
inline constexpr int HeaderOffsetStructuredData = 9 * 4;
inline constexpr int HeaderOffsetNodes = 10 * 4;
inline constexpr int HeaderOffsetSourceFileID = 11 * 4;
// (12 * 4 is part of the uint64 source file ID)
inline constexpr int HeaderOffsetSourceFileLease = 13 * 4;
// (14 * 4 is part of the uint64 lease)
inline constexpr int HeaderOffsetBinderData = 15 * 4;
inline constexpr int HeaderSize = 16 * 4;

inline constexpr uint8_t ProtocolVersion = 9;

// An offset of 0xFFFFFFFF indicates no data (empty array) in structured data.
inline constexpr uint32_t noStructuredData = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// NodeIndexTable — encoder.go:330. Maps between AST nodes and their encoder
// indices for O(1) node handle resolution.
// ---------------------------------------------------------------------------

struct NodeIndexTable {
	std::vector<Node*> Nodes;    // index → node (for resolution)
	OnceFlag sortedOnce;         // Go sync.Once
	std::vector<uint32_t> sortedIdx; // indices into Nodes, sorted by node ID; built lazily

	// GetIndex returns the encoder index for the given node.
	// On the first call the sortedIdx array is built (O(n log n) sort on a flat
	// []uint32), then subsequent calls use binary search (O(log n)). This turns
	// out to be much faster than building a map[*ast.Node]uint32 and not
	// significantly slower for lookups.
	uint32_t GetIndex(Node* node);
};

// encoder.go:336 — ast.NewSourceFileDataKey[*NodeIndexTable]().
extern SourceFileDataKey nodeIndexTableKey;

// encoder.go:370 — eagerly create index tables for files that need node handles
// before getSourceFile is called. The indices produced are guaranteed to match
// those from EncodeSourceFile.
NodeIndexTable* BuildNodeIndexTable(SourceFile* sourceFile);

// encoder.go:416
NodeIndexTable* GetNodeIndexTable(SourceFile* sourceFile);

// encoder.go:312 — 128-bit content hash for a source file as a hex string.
std::string SourceFileHash(SourceFile* sourceFile);

// encoder.go:422 — encodes an entire source file AST into the binary format.
// Returns the encoded bytes and a NodeIndexTable mapping encoder indices to AST
// nodes.
std::tuple<std::vector<uint8_t>, NodeIndexTable*, gostd::Error>
EncodeSourceFile(SourceFile* sourceFile);

// encoder.go:434 — sets the session-scoped lease ID in an encoded source file.
void SetSourceFileLease(std::vector<uint8_t>& data, uint64_t lease);
void SetSourceFileID(std::vector<uint8_t>& data, uint64_t id);

// encoder.go:442 — encodes an arbitrary AST node and its descendants into the
// binary format. The sourceFile is needed to provide the source text for
// efficient string encoding. When encoding a non-SourceFile node, the header
// hash and parse options fields will be zero.
std::tuple<std::vector<uint8_t>, NodeIndexTable*, gostd::Error>
EncodeNode(Node* node, SourceFile* sourceFile);

// decoder.go:33 — decodes binary-encoded data into an *ast.SourceFile.
std::tuple<SourceFile*, gostd::Error> DecodeSourceFile(const std::vector<uint8_t>& data);
std::tuple<SourceFile*, gostd::Error> DecodeSourceFile(std::string_view data);

// decoder.go:45 — decodes binary-encoded AST data into a tree of *ast.Node
// objects.
std::tuple<Node*, gostd::Error> DecodeNodes(const std::vector<uint8_t>& data);
std::tuple<Node*, gostd::Error> DecodeNodes(std::string_view data);

}  // namespace tsc::api::encoder
