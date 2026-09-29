// Port of tsc/internal/ast/symbol.go — Symbol + SymbolTable. Only the fields
// needed by the front end are ported; binder/checker populate them later.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/flags.h"

namespace tsc {

struct Node;

struct Symbol {
	SymbolFlags flags{};
	CheckFlags checkFlags{};
	std::string name;
	std::vector<Node*> declarations;
	Node* valueDeclaration = nullptr;
	std::unordered_map<std::string, Symbol*> members;
	std::unordered_map<std::string, Symbol*> exports;
	Symbol* parent = nullptr;
	Symbol* exportSymbol = nullptr;
};

struct FlowNode;  // defined by binder/checker later; parser only stores pointers

using SymbolTable = std::unordered_map<std::string, Symbol*>;

inline constexpr char kInternalSymbolNamePrefix = '\xFE';

}  // namespace tsc
