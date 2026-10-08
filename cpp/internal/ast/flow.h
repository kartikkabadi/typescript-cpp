// Port of tsc/internal/ast/flow.go — control-flow graph types.
#pragma once

#include <cstdint>

namespace tsc {

struct Node;

using FlowFlags = uint32_t;
inline constexpr FlowFlags FlowFlagsUnreachable = 1 << 0;
inline constexpr FlowFlags FlowFlagsStart = 1 << 1;
inline constexpr FlowFlags FlowFlagsBranchLabel = 1 << 2;
inline constexpr FlowFlags FlowFlagsLoopLabel = 1 << 3;
inline constexpr FlowFlags FlowFlagsAssignment = 1 << 4;
inline constexpr FlowFlags FlowFlagsTrueCondition = 1 << 5;
inline constexpr FlowFlags FlowFlagsFalseCondition = 1 << 6;
inline constexpr FlowFlags FlowFlagsSwitchClause = 1 << 7;
inline constexpr FlowFlags FlowFlagsArrayMutation = 1 << 8;
inline constexpr FlowFlags FlowFlagsCall = 1 << 9;
inline constexpr FlowFlags FlowFlagsReduceLabel = 1 << 10;
inline constexpr FlowFlags FlowFlagsReferenced = 1 << 11;
inline constexpr FlowFlags FlowFlagsShared = 1 << 12;
inline constexpr FlowFlags FlowFlagsLabel =
	FlowFlagsBranchLabel | FlowFlagsLoopLabel;
inline constexpr FlowFlags FlowFlagsCondition =
	FlowFlagsTrueCondition | FlowFlagsFalseCondition;

struct FlowList;

struct FlowNode {
	FlowFlags flags = 0;
	Node* node = nullptr;
	FlowNode* antecedent = nullptr;
	FlowList* antecedents = nullptr; // linked list of antecedents (FlowLabel)
};

struct FlowList {
	FlowNode* flow = nullptr;
	FlowList* next = nullptr;
};

using FlowLabel = FlowNode;

} // namespace tsc

#include "internal/ast/ast.h"

namespace tsc {

// Synthetic Node payloads stored on FlowNode.node (Go: NodeBase embedding
// with KindUnknown).
struct FlowSwitchClauseData : Node {
	Node* switchStatement = nullptr;
	int32_t clauseStart = 0;
	int32_t clauseEnd = 0;
	bool isEmpty() const { return clauseStart == clauseEnd; }
};

struct FlowReduceLabelData : Node {
	FlowLabel* target = nullptr;
	FlowList* antecedents = nullptr;
};

} // namespace tsc
