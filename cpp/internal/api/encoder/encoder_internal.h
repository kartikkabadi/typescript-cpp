// api/encoder/encoder_internal.h — shared internals between encoder.cpp,
// encoder_generated.cpp, decoder.cpp and decoder_generated.cpp: the string
// table and the generated-table entry points.
#pragma once

#include "internal/api/encoder/encoder.h"

#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace tsc::api::encoder {

// stringTable — stringtable.go.
struct stringTable {
	std::string_view fileText;
	std::string otherStrings; // Go strings.Builder
	// offsets are pos/end pairs
	std::vector<uint32_t> offsets;

	uint32_t add(std::string_view text, Kind kind, int pos, int end) {
		uint32_t index = static_cast<uint32_t>(offsets.size());
		if (kind == Kind::SourceFile) {
			offsets.push_back(static_cast<uint32_t>(pos));
			offsets.push_back(static_cast<uint32_t>(end));
			return index;
		}
		size_t length = text.size();
		if (end - pos > 0 && static_cast<size_t>(end) <= fileText.size()) {
			// pos includes leading trivia, but we can usually infer the actual
			// start of the string from the kind and end
			int endOffset = 0;
			if (kind == Kind::StringLiteral || kind == Kind::TemplateTail ||
			    kind == Kind::NoSubstitutionTemplateLiteral) {
				endOffset = 1;
			}
			end = end - endOffset;
			int64_t start =
			    static_cast<int64_t>(end) - static_cast<int64_t>(length);
			if (start >= 0 &&
			    fileText.substr(static_cast<size_t>(start), length) == text) {
				offsets.push_back(static_cast<uint32_t>(start));
				offsets.push_back(static_cast<uint32_t>(end));
				return index;
			}
		}
		// no exact match, so we need to add it to the string table
		size_t offset = fileText.size() + otherStrings.size();
		otherStrings.append(text);
		offsets.push_back(static_cast<uint32_t>(offset));
		offsets.push_back(static_cast<uint32_t>(offset + length));
		return index;
	}

	std::vector<uint8_t> encode() const {
		std::vector<uint8_t> result;
		result.reserve(encodedLength());
		for (uint32_t off : offsets) {
			result.push_back(static_cast<uint8_t>(off));
			result.push_back(static_cast<uint8_t>(off >> 8));
			result.push_back(static_cast<uint8_t>(off >> 16));
			result.push_back(static_cast<uint8_t>(off >> 24));
		}
		result.insert(result.end(), fileText.begin(), fileText.end());
		result.insert(result.end(), otherStrings.begin(), otherStrings.end());
		return result;
	}

	size_t stringLength() const {
		return fileText.size() + otherStrings.size();
	}

	size_t encodedLength() const {
		return offsets.size() * 4 + fileText.size() + otherStrings.size();
	}
};

inline stringTable newStringTable(std::string_view fileText, int stringCount) {
	stringTable t;
	t.fileText = fileText;
	t.offsets.reserve(static_cast<size_t>(stringCount) * 2);
	return t;
}

// encoder.go:735
inline int boolToByte(bool b) { return b ? 1 : 0; }

// encoder.go:743 — hasModifiers returns true if the modifier list is non-nil
// and has at least one modifier.
inline bool hasModifiers(const ModifierList* modifiers) {
	return modifiers != nullptr && !modifiers->nodes.empty();
}

// Generated table entry points (encoder_generated.cpp). uint32 Node-data word
// helpers; see encoder.go for the format.
uint32_t getNodeDataType(Node* node);
uint8_t getChildrenPropertyMask(Node* node);
uint32_t getNodeCommonData(Node* node);
uint32_t recordNodeStrings(Node* node, stringTable* strs);
uint32_t recordExtendedData(Node* node, stringTable* strs,
                            PositionMap* positionMap,
                            std::vector<uint8_t>& extendedData,
                            std::vector<uint8_t>& structuredData);

// Hand-written commonData / extended-data encoders (encoder.cpp).
uint32_t getNodeCommonData_SyntheticExpression(Node* node);
void recordExtendedData_StringLiteral(Node* node, stringTable* strs,
                                      PositionMap* positionMap,
                                      std::vector<uint8_t>& extendedData,
                                      std::vector<uint8_t>& structuredData);
void recordExtendedData_NumericLiteral(Node* node, stringTable* strs,
                                       PositionMap* positionMap,
                                       std::vector<uint8_t>& extendedData,
                                       std::vector<uint8_t>& structuredData);
void recordExtendedData_BigIntLiteral(Node* node, stringTable* strs,
                                      PositionMap* positionMap,
                                      std::vector<uint8_t>& extendedData,
                                      std::vector<uint8_t>& structuredData);
void recordExtendedData_RegularExpressionLiteral(
    Node* node, stringTable* strs, PositionMap* positionMap,
    std::vector<uint8_t>& extendedData, std::vector<uint8_t>& structuredData);
void recordExtendedData_NoSubstitutionTemplateLiteral(
    Node* node, stringTable* strs, PositionMap* positionMap,
    std::vector<uint8_t>& extendedData, std::vector<uint8_t>& structuredData);
void recordExtendedData_TemplateHead(Node* node, stringTable* strs,
                                     PositionMap* positionMap,
                                     std::vector<uint8_t>& extendedData,
                                     std::vector<uint8_t>& structuredData);
void recordExtendedData_TemplateMiddle(Node* node, stringTable* strs,
                                       PositionMap* positionMap,
                                       std::vector<uint8_t>& extendedData,
                                       std::vector<uint8_t>& structuredData);
void recordExtendedData_TemplateTail(Node* node, stringTable* strs,
                                     PositionMap* positionMap,
                                     std::vector<uint8_t>& extendedData,
                                     std::vector<uint8_t>& structuredData);
void recordExtendedData_SourceFile(Node* node, stringTable* strs,
                                   PositionMap* positionMap,
                                   std::vector<uint8_t>& extendedData,
                                   std::vector<uint8_t>& structuredData);

// readLE32 — decoder.go:343. Out-of-range reads return 0 (Go reads would
// panic on a malformed buffer; callers validate offsets up front).
inline uint32_t readLE32(std::string_view data, int64_t offset) {
	if (offset < 0 || static_cast<size_t>(offset) + 4 > data.size()) {
		return 0;
	}
	return static_cast<uint32_t>(static_cast<uint8_t>(data[offset])) |
	       (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 1])) << 8) |
	       (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 2])) << 16) |
	       (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 3])) << 24);
}

// astDecoder (decoder.go) — shared by decoder.cpp and decoder_generated.cpp.
struct astDecoder {
	std::string_view raw;
	uint32_t strTable = 0;
	uint32_t strData = 0;
	uint32_t extData = 0;
	uint32_t nodeOff = 0;
	int nodeCount = 0;
	std::unique_ptr<NodeFactory> factory;
	std::vector<int> childBuf;
	// Single string covering all string data; substrings are zero-alloc slices.
	std::string_view allStringData;
	// Arena for batch-allocating Node* vectors used by NodeLists.
	std::vector<Node*> nodeArena;
	// Results
	std::vector<Node*> nodes;
	std::vector<NodeList*> nodeLists;

	// allocNodeSlice returns a zero-length slice with the given capacity,
	// backed by the pre-allocated nodeArena. This avoids a heap allocation per
	// NodeList. (In C++ the reserved region is copied out by NodeList anyway;
	// we keep the arena to mirror Go's reservation accounting.)
	std::vector<Node*> allocNodeSlice(size_t capacity) {
		std::vector<Node*> v;
		v.reserve(capacity);
		return v;
	}

	// nodeField reads a uint32 field from node i at the given field offset.
	uint32_t nodeField(int i, int field) const {
		return readLE32(raw, nodeOff + i * NodeSize + field);
	}

	std::string_view getString(uint32_t idx) const {
		size_t offBase = strTable + static_cast<size_t>(idx) * 4;
		uint32_t start = readLE32(raw, offBase);
		uint32_t end = readLE32(raw, offBase + 4);
		if (end < start || end > allStringData.size()) {
			return {};
		}
		return allStringData.substr(start, end - start);
	}

	// collectChildren returns indices of direct children of node i.
	// The returned vector is reused across calls; callers must not retain it.
	std::vector<int>& collectChildren(int i) {
		childBuf.clear();
		if (i + 1 >= nodeCount) {
			return childBuf;
		}
		int firstChild = i + 1;
		if (nodeField(firstChild, NodeOffsetParent) != static_cast<uint32_t>(i)) {
			return childBuf;
		}
		childBuf.push_back(firstChild);
		uint32_t next = nodeField(firstChild, NodeOffsetNext);
		while (next != 0) {
			childBuf.push_back(static_cast<int>(next));
			next = nodeField(static_cast<int>(next), NodeOffsetNext);
		}
		return childBuf;
	}

	std::tuple<Node*, gostd::Error> decode();

	// getModifierList creates a ModifierList from a child index that is a
	// NodeList.
	ModifierList* getModifierList(int ci) {
		NodeList* nl = nodeLists[ci];
		if (nl == nullptr) {
			return nullptr;
		}
		ModifierList* ml = factory->newModifierList(nl->nodes);
		ml->loc = nl->loc;
		return ml;
	}

	Node* nodeAt(int ci) {
		if (ci == 0) {
			return nullptr;
		}
		return nodes[ci];
	}

	NodeList* nodeListAt(int ci) {
		if (ci == 0) {
			return nullptr;
		}
		return nodeLists[ci];
	}

	ModifierList* modifierListAt(int ci) {
		if (ci == 0) {
			return nullptr;
		}
		return getModifierList(ci);
	}

	Node* singleChild(const std::vector<int>& childIndices) {
		if (childIndices.empty()) {
			return nullptr;
		}
		return nodes[childIndices[0]];
	}

	NodeList* singleNodeListChild(const std::vector<int>& childIndices) {
		if (childIndices.empty()) {
			return nullptr;
		}
		return nodeLists[childIndices[0]];
	}

	std::tuple<Node*, gostd::Error> createNode(Kind kind, uint32_t data,
	                                           const std::vector<int>& childIndices);

	// Generated dispatch (decoder_generated.cpp).
	std::tuple<Node*, gostd::Error> createStringNode(Kind kind, uint32_t data,
	                                                 uint8_t commonData);
	std::tuple<Node*, gostd::Error> createExtendedNode(
	    Kind kind, uint32_t data, const std::vector<int>& childIndices,
	    uint8_t commonData);
	std::tuple<Node*, gostd::Error> createChildrenNode(
	    Kind kind, uint32_t data, const std::vector<int>& childIndices,
	    uint8_t commonData);

	// Extended-data decoders (decoder.go + decoder_generated.cpp dispatch).
	std::tuple<Node*, gostd::Error> decodeExtendedData_SourceFile(
	    uint32_t data, const std::vector<int>& childIndices, uint8_t commonData);
	std::tuple<Node*, gostd::Error> decodeExtendedData_TemplateHead(
	    uint32_t data, const std::vector<int>& childIndices, uint8_t commonData);
	std::tuple<Node*, gostd::Error> decodeExtendedData_TemplateMiddle(
	    uint32_t data, const std::vector<int>& childIndices, uint8_t commonData);
	std::tuple<Node*, gostd::Error> decodeExtendedData_TemplateTail(
	    uint32_t data, const std::vector<int>& childIndices, uint8_t commonData);
	std::tuple<Node*, gostd::Error> decodeExtendedData_StringLiteral(
	    uint32_t data, const std::vector<int>& childIndices, uint8_t commonData);
	std::tuple<Node*, gostd::Error> decodeExtendedData_NumericLiteral(
	    uint32_t data, const std::vector<int>& childIndices, uint8_t commonData);
	std::tuple<Node*, gostd::Error> decodeExtendedData_BigIntLiteral(
	    uint32_t data, const std::vector<int>& childIndices, uint8_t commonData);
	std::tuple<Node*, gostd::Error>
	decodeExtendedData_RegularExpressionLiteral(
	    uint32_t data, const std::vector<int>& childIndices, uint8_t commonData);
	std::tuple<Node*, gostd::Error>
	decodeExtendedData_NoSubstitutionTemplateLiteral(
	    uint32_t data, const std::vector<int>& childIndices, uint8_t commonData);

	// decoder.go:354 — panics in Go.
	std::pair<void*, bool> decodeNodeCommonData_SyntheticExpression(
	    uint8_t commonData);
};

// childIterator helps walk through children based on a bitmask
// (decoder.go:197).
struct childIterator {
	const std::vector<int>& indices;
	size_t pos = 0;

	// next returns the index of the next child, advancing the position.
	int next() {
		if (pos >= indices.size()) {
			return 0;
		}
		return indices[pos++];
	}

	// nextIf returns the index of the next child if the corresponding mask
	// bit is set.
	int nextIf(uint8_t mask, uint8_t bit) {
		if ((mask & (1 << bit)) == 0) {
			return 0;
		}
		return next();
	}
};

}  // namespace tsc::api::encoder
