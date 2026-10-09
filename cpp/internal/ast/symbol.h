// Port of tsc/internal/ast/symbol.go — Symbol + SymbolTable.
#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "internal/ast/flags.h"
#include "internal/ast/symbolflags.h"

namespace tsc {

struct Node;
struct Symbol;

using SymbolId = uint64_t;
using SymbolTable = std::unordered_map<std::string, Symbol*>;

// SymbolData — symbol.go symbolData: the shared payload of a Symbol. Instantiated
// symbols in the checker share one SymbolData with their origin; flags,
// checkFlags, and id stay unique to each Symbol instance.
struct SymbolData {
	std::string name;
	std::vector<Node*> declarations;
	Node* valueDeclaration = nullptr;
	std::unordered_map<std::string, Symbol*> members;
	std::unordered_map<std::string, Symbol*> exports;
	Symbol* parent = nullptr;
	Symbol* exportSymbol = nullptr;
};

// Symbol stores flags, checkFlags, and id uniquely for every symbol instance,
// but may share SymbolData with other symbols. Every Symbol has its data field
// initialized on construction (the equivalent of Go's ast.NewSymbol /
// SymbolWithData.Initialize contract); use setSymbolData to share another
// symbol's data.
struct Symbol {
	SymbolFlags flags{};
	CheckFlags checkFlags{};
	std::atomic<SymbolId> id{0};
	SymbolData* data;

	Symbol() : data(new SymbolData()) {}

	// setSymbolData — Symbol.SetSymbolData: point this symbol at another
	// symbol's SymbolData so both share it.
	void setSymbolData(Symbol* other) { data = other->data; }

	bool isExternalModule() const;
	bool isStatic() const;
	SymbolFlags combinedLocalAndExportSymbolFlags() const {
		return data->exportSymbol ? flags | data->exportSymbol->flags : flags;
	}
};

// SymbolWithData — ast.SymbolWithData: a Symbol paired with its own SymbolData
// in one allocation. initialize() points the Symbol at its data, mirroring
// SymbolWithData.Initialize.
struct SymbolWithData {
	Symbol s;
	SymbolData d;

	Symbol* initialize() {
		s.data = &d;
		return &s;
	}
};

// newSymbol — ast.NewSymbol.
inline Symbol* newSymbol() { return (new SymbolWithData())->initialize(); }

struct FlowNode;

// Go map-read semantics: returns the value or nullptr without inserting.
// operator[] must never be used to READ a SymbolTable — it inserts a null
// entry on miss, which later poisons table iteration (e.g. getExportsOfModule).
inline Symbol* getSymbolFromTable(const SymbolTable& t, const std::string& k) {
	auto it = t.find(k);
	return it != t.end() ? it->second : nullptr;
}

// String-view variant: libstdc++ derives std::hash<std::string_view> from the
// same _Hash_bytes digest as std::hash<std::string>, and bucket index is
// hash % bucket_count() (mod-range hashing) — so walking the computed bucket
// yields exactly the entry find(k) would return. Read-only; table order and
// contents are untouched.
inline Symbol* getSymbolFromTableView(const SymbolTable& t, std::string_view k) {
	if (t.empty()) {
		return nullptr;
	}
	size_t bucket = std::hash<std::string_view>{}(k) % t.bucket_count();
	for (auto it = t.begin(bucket), end = t.end(bucket); it != end; ++it) {
		if (it->first == k) {
			return it->second;
		}
	}
	return nullptr;
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
