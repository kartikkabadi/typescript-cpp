// Port of tsc/internal/binder/referenceresolver.go — interface + hooks surface.
// The referenceResolver implementation is not ported yet; NewReferenceResolver
// is a dep-stub (TSC_UNREACHABLE) owned by the referenceresolver slice.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/symbol.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"

namespace tsc::binder {

// ReferenceResolver — referenceresolver.go:9
struct ReferenceResolver {
	virtual ~ReferenceResolver() = default;

	// returns *SourceFile | *ModuleDeclaration | *EnumDeclaration
	virtual Node* GetReferencedExportContainer(Node* node, bool prefixLocals) = 0;
	virtual Node* GetReferencedImportDeclaration(Node* node) = 0;
	virtual Node* GetReferencedValueDeclaration(Node* node) = 0;
	virtual std::vector<Node*> GetReferencedValueDeclarations(Node* node) = 0;
	virtual std::string GetElementAccessExpressionName(
		ElementAccessExpression* expression) = 0;
	virtual Node* GetReferencedMemberValueDeclaration(Node* node) = 0;
};

// ReferenceResolverHooks — referenceresolver.go:20
struct ReferenceResolverHooks {
	std::function<Symbol*(Node* location, std::string_view name, SymbolFlags meaning,
						  const DiagnosticMessage* nameNotFoundMessage, bool isUse,
						  bool excludeGlobals)>
		ResolveName;
	std::function<Symbol*(Node*)> GetResolvedSymbol;
	std::function<Symbol*(Symbol*)> GetMergedSymbol;
	std::function<Symbol*(Symbol*)> GetParentOfSymbol;
	std::function<Symbol*(Node*)> GetSymbolOfDeclaration;
	std::function<Node*(Symbol*, SymbolFlags)> GetTypeOnlyAliasDeclaration;
	std::function<Symbol*(Symbol*)> GetExportSymbolOfValueSymbolIfExported;
	std::function<std::pair<std::string, bool>(ElementAccessExpression*)>
		GetElementAccessExpressionName;
};

// NewReferenceResolver — referenceresolver.go:37
ReferenceResolver* NewReferenceResolver(const CompilerOptions* options,
										ReferenceResolverHooks hooks);

}  // namespace tsc::binder
