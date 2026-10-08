// Port of tsc/internal/binder/nameresolver.go
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/symbol.h"
#include "internal/ast/symbolflags.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"

namespace tsc {
namespace binder {

struct NameResolver {
	CompilerOptions* compilerOptions = nullptr;
	std::function<Symbol*(Node*)> getSymbolOfDeclaration;
	std::function<Diagnostic*(Node*, const DiagnosticMessage*, const std::vector<std::string>&)> error;
	SymbolTable* globals = nullptr;
	Symbol* argumentsSymbol = nullptr;
	Symbol* requireSymbol = nullptr;
	std::function<Symbol*(SymbolTable*, std::string_view, SymbolFlags)> lookup;
	std::function<void(Symbol*, SymbolFlags)> symbolReferenced;
	std::function<void(Node*, Tristate)> setRequiresScopeChangeCache;
	std::function<Tristate(Node*)> getRequiresScopeChangeCache;
	std::function<bool(Node*, const std::string&, Node*, Symbol*)> onPropertyWithInvalidInitializer;
	std::function<void(Node*, const std::string&, SymbolFlags, const DiagnosticMessage*)> onFailedToResolveSymbol;
	std::function<void(Node*, Symbol*, SymbolFlags, Node*, Node*, bool)> onSuccessfullyResolvedSymbol;

	Symbol* resolve(Node* location, std::string_view name, SymbolFlags meaning,
		const DiagnosticMessage* nameNotFoundMessage, bool isUse, bool excludeGlobals);
	bool useOuterVariableScopeInParameter(Symbol* result, Node* location, Node* lastLocation);
	bool requiresScopeChange(Node* node);
	bool requiresScopeChangeWorker(Node* node);
	Diagnostic* reportError(Node* location, const DiagnosticMessage* message,
		const std::vector<std::string>& args = {});
	Symbol* getSymbolOfDeclarationOrDefault(Node* node);
	Symbol* lookupOrDefault(SymbolTable* symbols, std::string_view name, SymbolFlags meaning);
	Symbol* getArgumentsSymbol();
};

Symbol* getLocalSymbolForExportDefault(Symbol* symbol);
bool isExportDefaultSymbol(Symbol* symbol);

}  // namespace binder
}  // namespace tsc
