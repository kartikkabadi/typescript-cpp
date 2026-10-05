// Port of tsc/internal/ast/symbol.go — Symbol + SymbolTable.
#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/flags.h"
#include "internal/ast/symbolflags.h"

namespace tsc {

struct Node;

using SymbolId = uint64_t;

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
	std::atomic<SymbolId> id{0};

	bool isExternalModule() const;
	bool isStatic() const;
	SymbolFlags combinedLocalAndExportSymbolFlags() const {
		return exportSymbol ? flags | exportSymbol->flags : flags;
	}
};

struct FlowNode;

using SymbolTable = std::unordered_map<std::string, Symbol*>;

// Go map-read semantics: returns the value or nullptr without inserting.
// operator[] must never be used to READ a SymbolTable — it inserts a null
// entry on miss, which later poisons table iteration (e.g. getExportsOfModule).
inline Symbol* getSymbolFromTable(const SymbolTable& t, const std::string& k) {
	auto it = t.find(k);
	return it != t.end() ? it->second : nullptr;
}

inline constexpr char kInternalSymbolNamePrefix = '\xFE';
inline const std::string InternalSymbolNameCall{"\xFE" "call"};
inline const std::string InternalSymbolNameConstructor{"\xFE" "constructor"};
inline const std::string InternalSymbolNameNew{"\xFE" "new"};
inline const std::string InternalSymbolNameIndex{"\xFE" "index"};
inline const std::string InternalSymbolNameExportStar{"\xFE" "export"};
inline const std::string InternalSymbolNameGlobal{"\xFE" "global"};
inline const std::string InternalSymbolNameMissing{"\xFE" "missing"};
inline const std::string InternalSymbolNameType{"\xFE" "type"};
inline const std::string InternalSymbolNameObject{"\xFE" "object"};
inline const std::string InternalSymbolNameJSXAttributes{"\xFE" "jsxAttributes"};
inline const std::string InternalSymbolNameClass{"\xFE" "class"};
inline const std::string InternalSymbolNameFunction{"\xFE" "function"};
inline const std::string InternalSymbolNameComputed{"\xFE" "computed"};
inline const std::string InternalSymbolNameAssignmentDeclaration{"\xFE" "assignment"};
inline const std::string InternalSymbolNameInstantiationExpression{"\xFE" "instantiationExpression"};
inline const std::string InternalSymbolNameImportAttributes{"\xFE" "importAttributes"};
inline const std::string InternalSymbolNameExportEquals{"export="};
inline const std::string InternalSymbolNameDefault{"default"};
inline const std::string InternalSymbolNameThis{"this"};
inline const std::string InternalSymbolNameModuleExports{"module.exports"};

std::string symbolName(const Symbol* symbol);
std::string escapeAllInternalSymbolNames(std::string_view name);
std::string escapeInternalSymbolName(std::string_view name);
std::string escapeSymbolName(std::string_view name);

} // namespace tsc
