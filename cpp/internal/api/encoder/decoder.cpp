// api/encoder/decoder.cpp — decoder.go: astDecoder reconstructs real
// *ast.Node objects from binary-encoded data.

#include "internal/api/encoder/encoder_internal.h"

#include "internal/tspath/tspath.h"

namespace tsc::api::encoder {

// DecodeSourceFile decodes binary-encoded data into an *ast.SourceFile.
std::tuple<SourceFile*, gostd::Error>
DecodeSourceFile(const std::vector<uint8_t>& data) {
	return DecodeSourceFile(std::string_view(
	    reinterpret_cast<const char*>(data.data()), data.size()));
}

std::tuple<SourceFile*, gostd::Error> DecodeSourceFile(std::string_view data) {
	auto [node, err] = DecodeNodes(data);
	if (err != nullptr) {
		return {nullptr, err};
	}
	if (node->kind != Kind::SourceFile) {
		return {nullptr,
		        gostd::errorf("expected SourceFile root, got %d",
		                      {static_cast<int>(node->kind)})};
	}
	return {node->as<SourceFile>(), nullptr};
}

// DecodeNodes decodes binary-encoded AST data into a tree of *ast.Node
// objects.
std::tuple<Node*, gostd::Error>
DecodeNodes(const std::vector<uint8_t>& data) {
	return DecodeNodes(std::string_view(
	    reinterpret_cast<const char*>(data.data()), data.size()));
}

static std::tuple<astDecoder*, gostd::Error> newASTDecoder(
    std::string_view data);

std::tuple<Node*, gostd::Error> DecodeNodes(std::string_view data) {
	auto [d, err] = newASTDecoder(data);
	if (err != nullptr) {
		return {nullptr, err};
	}
	return d->decode();
}

static std::tuple<astDecoder*, gostd::Error> newASTDecoder(std::string_view data) {
	if (data.size() < HeaderSize) {
		return {nullptr, gostd::errorf("data too short for header: %d bytes",
		                               {static_cast<int64_t>(data.size())})};
	}
	uint8_t version = static_cast<uint8_t>(data[HeaderOffsetMetadata + 3]);
	if (version != ProtocolVersion) {
		return {nullptr, gostd::errorf(
		                     "unsupported protocol version %d (expected %d)",
		                     {static_cast<uint64_t>(version), static_cast<uint64_t>(ProtocolVersion)})};
	}

	uint32_t strTable = readLE32(data, HeaderOffsetStringOffsets);
	uint32_t strData = readLE32(data, HeaderOffsetStringData);
	uint32_t extData = readLE32(data, HeaderOffsetExtendedData);
	uint32_t nodeOff = readLE32(data, HeaderOffsetNodes);

	size_t dataLen = data.size();

	// Validate that all offsets are within the buffer.
	if (strTable > dataLen || strData > dataLen || extData > dataLen ||
	    nodeOff > dataLen) {
		return {nullptr,
		        gostd::errorf("invalid AST header offsets: offsets exceed data "
		                      "length (%d)",
		                      {static_cast<int64_t>(dataLen)})};
	}

	// Validate monotonic non-decreasing order of regions.
	if (!(strTable <= strData && strData <= extData && extData <= nodeOff)) {
		return {nullptr,
		        gostd::errorf(
		            "invalid AST header offsets: expected strTable <= strData "
		            "<= extData <= nodeOff (got %d, %d, %d, %d)",
		            {static_cast<uint64_t>(strTable), static_cast<uint64_t>(strData), static_cast<uint64_t>(extData), static_cast<uint64_t>(nodeOff)})};
	}

	auto* d = new astDecoder();
	d->raw = data;
	d->strTable = strTable;
	d->strData = strData;
	d->extData = extData;
	d->nodeOff = nodeOff;
	d->factory = std::make_unique<NodeFactory>(NodeFactoryHooks{});

	d->nodeCount = (static_cast<int>(dataLen) - static_cast<int>(d->nodeOff)) /
	               NodeSize;

	// Convert entire string data region to a single string upfront.
	// Substringing a Go string shares the backing array, so subsequent
	// getString calls produce substrings with zero allocations; C++ string_view
	// substrings are likewise zero-alloc.
	d->allStringData = data.substr(d->strData);

	return {d, nullptr};
}

std::tuple<Node*, gostd::Error> astDecoder::decode() {
	if (nodeCount < 2) {
		return {nullptr, gostd::newError("no nodes to decode")};
	}

	nodes.resize(nodeCount);
	nodeLists.resize(nodeCount);
	// Pre-allocate arena for NodeList child slices. Each node can appear as a
	// child at most once, so nodeCount is an upper bound on total child
	// pointers.
	nodeArena.reserve(nodeCount);

	// Process bottom-up so children exist before parents.
	for (int i = nodeCount - 1; i >= 1; i--) {
		uint32_t kind = nodeField(i, NodeOffsetKind);
		uint32_t pos = nodeField(i, NodeOffsetPos);
		uint32_t end = nodeField(i, NodeOffsetEnd);
		uint32_t data = nodeField(i, NodeOffsetData);
		std::vector<int>& childIndices = collectChildren(i);

		if (kind == SyntaxKindNodeList) {
			std::vector<Node*> childNodes;
			childNodes.reserve(childIndices.size());
			for (int ci : childIndices) {
				if (nodes[ci] != nullptr) {
					childNodes.push_back(nodes[ci]);
				}
			}
			NodeList* nl = factory->newNodeList(std::move(childNodes));
			nl->loc = TextRange{static_cast<TextPos>(pos),
			                    static_cast<TextPos>(end)};
			nodeLists[i] = nl;
			continue;
		}

		auto [node, err] =
		    createNode(static_cast<Kind>(kind), data, childIndices);
		if (err != nullptr) {
			return {nullptr,
			        gostd::errorf("at node %d (kind %d): %w",
			                      {i, static_cast<int>(kind), err})};
		}
		node->loc = TextRange{static_cast<TextPos>(pos),
		                      static_cast<TextPos>(end)};
		node->flags = NodeFlags(nodeField(i, NodeOffsetFlags));
		if (kind == static_cast<uint32_t>(Kind::SourceFile)) {
			node->as<SourceFile>()->IsDeclarationFile =
			    (node->flags & NodeFlagsAmbient) != 0;
		}
		nodes[i] = node;
	}

	return {nodes[1], nullptr};
}

std::tuple<Node*, gostd::Error>
astDecoder::createNode(Kind kind, uint32_t data,
                       const std::vector<int>& childIndices) {
	uint32_t dataType = data & NodeDataTypeMask;
	uint8_t commonData = static_cast<uint8_t>((data >> 24) & 0x3f);

	switch (dataType) {
	case NodeDataTypeString:
		return createStringNode(kind, data, commonData);
	case NodeDataTypeExtendedData:
		return createExtendedNode(kind, data, childIndices, commonData);
	default:
		return createChildrenNode(kind, data, childIndices, commonData);
	}
}

std::tuple<Node*, gostd::Error> astDecoder::decodeExtendedData_SourceFile(
    uint32_t data, const std::vector<int>& childIndices, uint8_t) {
	size_t extOff =
	    static_cast<size_t>(extData) + (data & NodeDataStringIndexMask);

	uint32_t textIdx = readLE32(raw, extOff);
	uint32_t fileNameIdx = readLE32(raw, extOff + 4);
	uint32_t pathIdx = readLE32(raw, extOff + 8);
	LanguageVariant languageVariant =
	    LanguageVariant(readLE32(raw, extOff + 12));
	ScriptKind scriptKind = ScriptKind(readLE32(raw, extOff + 16));
	std::string text{getString(textIdx)};
	std::string fileName{getString(fileNameIdx)};
	std::string path{getString(pathIdx)};
	if (tspath::getEncodedRootLength(fileName) == 0 ||
	    fileName != tspath::normalizePath(fileName)) {
		return {nullptr,
		        gostd::errorf("invalid source file name %q", {fileName})};
	}

	// Recover parse options from header.
	uint32_t parseOpts = readLE32(raw, HeaderOffsetParseOptions);
	SourceFileParseOptions opts;
	opts.FileName = fileName;
	opts.Path = path;
	opts.ExternalModuleIndicatorOptions.JSX =
	    (parseOpts & 1) != 0 ? JsxEmit::Preserve : JsxEmit::None;
	opts.ExternalModuleIndicatorOptions.Force = (parseOpts & 2) != 0;

	// Collect children: first is statements NodeList, second is EndOfFile.
	NodeList* stmts = nullptr;
	Node* endOfFile = nullptr;
	for (int ci : childIndices) {
		if (nodeField(ci, NodeOffsetKind) == SyntaxKindNodeList) {
			stmts = nodeListAt(ci);
		} else if (nodes[ci] != nullptr &&
		           nodes[ci]->kind == Kind::EndOfFile) {
			endOfFile = nodes[ci];
		}
	}
	if (endOfFile == nullptr) {
		endOfFile = factory->newToken(Kind::EndOfFile);
	}
	Node* node = factory->newSourceFile(opts, std::move(text), stmts, endOfFile);
	SourceFile* sourceFile = node->as<SourceFile>();
	sourceFile->LanguageVariant = languageVariant;
	sourceFile->ScriptKind = scriptKind;
	return {node, nullptr};
}

std::tuple<Node*, gostd::Error> astDecoder::decodeExtendedData_TemplateHead(
    uint32_t data, const std::vector<int>&, uint8_t) {
	size_t extOff =
	    static_cast<size_t>(extData) + (data & NodeDataStringIndexMask);
	uint32_t textIdx = readLE32(raw, extOff);
	uint32_t rawTextIdx = readLE32(raw, extOff + 4);
	uint32_t flags = readLE32(raw, extOff + 8);
	return {factory->newTemplateHead(std::string{getString(textIdx)},
	                                 std::string{getString(rawTextIdx)},
	                                 TokenFlags(flags)),
	        nullptr};
}

std::tuple<Node*, gostd::Error> astDecoder::decodeExtendedData_TemplateMiddle(
    uint32_t data, const std::vector<int>&, uint8_t) {
	size_t extOff =
	    static_cast<size_t>(extData) + (data & NodeDataStringIndexMask);
	uint32_t textIdx = readLE32(raw, extOff);
	uint32_t rawTextIdx = readLE32(raw, extOff + 4);
	uint32_t flags = readLE32(raw, extOff + 8);
	return {factory->newTemplateMiddle(std::string{getString(textIdx)},
	                                   std::string{getString(rawTextIdx)},
	                                   TokenFlags(flags)),
	        nullptr};
}

std::tuple<Node*, gostd::Error> astDecoder::decodeExtendedData_TemplateTail(
    uint32_t data, const std::vector<int>&, uint8_t) {
	size_t extOff =
	    static_cast<size_t>(extData) + (data & NodeDataStringIndexMask);
	uint32_t textIdx = readLE32(raw, extOff);
	uint32_t rawTextIdx = readLE32(raw, extOff + 4);
	uint32_t flags = readLE32(raw, extOff + 8);
	return {factory->newTemplateTail(std::string{getString(textIdx)},
	                                 std::string{getString(rawTextIdx)},
	                                 TokenFlags(flags)),
	        nullptr};
}

// Hand-written commonData decoding functions. Each extracts the original
// values from the 6-bit commonData that were packed by the corresponding
// getNodeCommonData_* function.

// decoder.go:354
std::pair<void*, bool> astDecoder::decodeNodeCommonData_SyntheticExpression(
    uint8_t) {
	TSC_UNREACHABLE("SyntheticExpression should never be decoded");
}

// Hand-written extended data decoding functions for literal nodes.

std::tuple<Node*, gostd::Error> astDecoder::decodeExtendedData_StringLiteral(
    uint32_t data, const std::vector<int>&, uint8_t) {
	size_t extOff =
	    static_cast<size_t>(extData) + (data & NodeDataStringIndexMask);
	uint32_t textIdx = readLE32(raw, extOff);
	uint32_t flags = readLE32(raw, extOff + 4);
	return {factory->newStringLiteral(std::string{getString(textIdx)},
	                                  TokenFlags(flags)),
	        nullptr};
}

std::tuple<Node*, gostd::Error> astDecoder::decodeExtendedData_NumericLiteral(
    uint32_t data, const std::vector<int>&, uint8_t) {
	size_t extOff =
	    static_cast<size_t>(extData) + (data & NodeDataStringIndexMask);
	uint32_t textIdx = readLE32(raw, extOff);
	uint32_t flags = readLE32(raw, extOff + 4);
	return {factory->newNumericLiteral(std::string{getString(textIdx)},
	                                   TokenFlags(flags)),
	        nullptr};
}

std::tuple<Node*, gostd::Error> astDecoder::decodeExtendedData_BigIntLiteral(
    uint32_t data, const std::vector<int>&, uint8_t) {
	size_t extOff =
	    static_cast<size_t>(extData) + (data & NodeDataStringIndexMask);
	uint32_t textIdx = readLE32(raw, extOff);
	uint32_t flags = readLE32(raw, extOff + 4);
	return {factory->newBigIntLiteral(std::string{getString(textIdx)},
	                                  TokenFlags(flags)),
	        nullptr};
}

std::tuple<Node*, gostd::Error>
astDecoder::decodeExtendedData_RegularExpressionLiteral(
    uint32_t data, const std::vector<int>&, uint8_t) {
	size_t extOff =
	    static_cast<size_t>(extData) + (data & NodeDataStringIndexMask);
	uint32_t textIdx = readLE32(raw, extOff);
	uint32_t flags = readLE32(raw, extOff + 4);
	return {factory->newRegularExpressionLiteral(
	            std::string{getString(textIdx)}, TokenFlags(flags)),
	        nullptr};
}

std::tuple<Node*, gostd::Error>
astDecoder::decodeExtendedData_NoSubstitutionTemplateLiteral(
    uint32_t data, const std::vector<int>&, uint8_t) {
	size_t extOff =
	    static_cast<size_t>(extData) + (data & NodeDataStringIndexMask);
	uint32_t textIdx = readLE32(raw, extOff);
	uint32_t flags = readLE32(raw, extOff + 4);
	return {factory->newNoSubstitutionTemplateLiteral(
	            std::string{getString(textIdx)}, TokenFlags(flags)),
	        nullptr};
}

}  // namespace tsc::api::encoder
