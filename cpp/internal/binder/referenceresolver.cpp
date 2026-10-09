// Port of tsc/internal/binder/referenceresolver.go — referenceResolver implementation.
#include "internal/binder/referenceresolver.h"

#include <type_traits>

#include "internal/binder/nameresolver.h"

namespace tsc {
namespace binder {

namespace {

// core/core.go:228 — findLast
template <typename T, typename F>
T findLast(const std::vector<T>& slice, F f) {
	for (auto it = slice.rbegin(); it != slice.rend(); ++it) {
		if (f(*it)) {
			return *it;
		}
	}
	return T{};
}

// ast/utilities.go:2639 — getDeclarationContainer (file-local replica; each TU
// that needs it keeps its own copy)
Node* getDeclarationContainer(Node* node) {
	return findAncestor(getRootDeclaration(node), [](Node* n) -> bool {
		switch (n->kind) {
		case Kind::VariableDeclaration:
		case Kind::VariableDeclarationList:
		case Kind::ImportSpecifier:
		case Kind::NamedImports:
		case Kind::NamespaceImport:
		case Kind::ImportClause:
			return false;
		default:
			return true;
		}
	})->parent;
}

// referenceResolver — referenceresolver.go:31
struct referenceResolver : ReferenceResolver {
	NameResolver* resolver = nullptr;
	const CompilerOptions* options = nullptr;
	ReferenceResolverHooks hooks;

	Symbol* getResolvedSymbol(Node* node);
	Symbol* getMergedSymbol(Symbol* symbol);
	Symbol* getParentOfSymbol(Symbol* symbol);
	Symbol* getSymbolOfDeclaration(Node* declaration);
	Symbol* getReferencedValueSymbol(Node* reference, bool startInDeclarationContainer);
	bool isTypeOnlyAliasDeclaration(Symbol* symbol);
	Node* getDeclarationOfAliasSymbol(Symbol* symbol);
	Symbol* getExportSymbolOfValueSymbolIfExported(Symbol* symbol);

	// ReferenceResolver
	Node* GetReferencedExportContainer(Node* node, bool prefixLocals) override;
	Node* GetReferencedImportDeclaration(Node* node) override;
	Node* GetReferencedValueDeclaration(Node* node) override;
	std::vector<Node*> GetReferencedValueDeclarations(Node* node) override;
	std::string GetElementAccessExpressionName(ElementAccessExpression* expression) override;
	Node* GetReferencedMemberValueDeclaration(Node* node) override;
};

// var _ ReferenceResolver = &referenceResolver{} — referenceresolver.go:29
static_assert(std::is_base_of_v<ReferenceResolver, referenceResolver>);

// referenceResolver::getResolvedSymbol — referenceresolver.go:44
Symbol* referenceResolver::getResolvedSymbol(Node* node) {
	if (node != nullptr) {
		if (hooks.GetResolvedSymbol) {
			return hooks.GetResolvedSymbol(node);
		}
	}
	return nullptr;
}

// referenceResolver::getMergedSymbol — referenceresolver.go:53
Symbol* referenceResolver::getMergedSymbol(Symbol* symbol) {
	if (symbol != nullptr) {
		if (hooks.GetMergedSymbol) {
			return hooks.GetMergedSymbol(symbol);
		}
		return symbol;
	}
	return nullptr;
}

// referenceResolver::getParentOfSymbol — referenceresolver.go:63
Symbol* referenceResolver::getParentOfSymbol(Symbol* symbol) {
	if (symbol != nullptr) {
		if (hooks.GetParentOfSymbol) {
			return hooks.GetParentOfSymbol(symbol);
		}
		return symbol->data->parent;
	}
	return nullptr;
}

// referenceResolver::getSymbolOfDeclaration — referenceresolver.go:73
Symbol* referenceResolver::getSymbolOfDeclaration(Node* declaration) {
	if (declaration != nullptr) {
		if (hooks.GetSymbolOfDeclaration) {
			return hooks.GetSymbolOfDeclaration(declaration);
		}
		return declaration->symbol();
	}
	return nullptr;
}

// referenceResolver::getReferencedValueSymbol — referenceresolver.go:83
Symbol* referenceResolver::getReferencedValueSymbol(Node* reference, bool startInDeclarationContainer) {
	Symbol* resolvedSymbol = getResolvedSymbol(reference);
	if (resolvedSymbol != nullptr) {
		return resolvedSymbol;
	}

	Node* location = reference;
	if (startInDeclarationContainer && reference->parent != nullptr &&
		isDeclaration(reference->parent) && reference->parent->name() == reference) {
		location = getDeclarationContainer(reference->parent);
	}

	if (hooks.ResolveName) {
		return hooks.ResolveName(location, reference->text(),
			SymbolFlagsExportValue | SymbolFlagsValue | SymbolFlagsAlias,
			nullptr /*nameNotFoundMessage*/, false /*isUse*/, false /*excludeGlobals*/);
	}

	if (resolver == nullptr) {
		resolver = new NameResolver();
		resolver->compilerOptions = const_cast<CompilerOptions*>(options);
	}

	return resolver->resolve(location, reference->text(),
		SymbolFlagsExportValue | SymbolFlagsValue | SymbolFlagsAlias,
		nullptr /*nameNotFoundMessage*/, false /*isUse*/, false /*excludeGlobals*/);
}

// referenceResolver::isTypeOnlyAliasDeclaration — referenceresolver.go:107
bool referenceResolver::isTypeOnlyAliasDeclaration(Symbol* symbol) {
	if (symbol != nullptr) {
		if (hooks.GetTypeOnlyAliasDeclaration) {
			return hooks.GetTypeOnlyAliasDeclaration(symbol, SymbolFlagsValue) != nullptr;
		}

		Node* node = getDeclarationOfAliasSymbol(symbol);
		while (node != nullptr) {
			switch (node->kind) {
			case Kind::ImportEqualsDeclaration:
			case Kind::ExportDeclaration:
				return node->isTypeOnly();
			case Kind::ImportClause:
			case Kind::ImportSpecifier:
			case Kind::ExportSpecifier:
				if (node->isTypeOnly()) {
					return true;
				}
				node = node->parent;
				continue;
			case Kind::NamedImports:
			case Kind::NamedExports:
				node = node->parent;
				continue;
			}
			break;
		}
	}
	return false;
}

// referenceResolver::getDeclarationOfAliasSymbol — referenceresolver.go:134
Node* referenceResolver::getDeclarationOfAliasSymbol(Symbol* symbol) {
	return findLast(symbol->data->declarations, isAliasSymbolDeclaration);
}

// referenceResolver::getExportSymbolOfValueSymbolIfExported — referenceresolver.go:138
Symbol* referenceResolver::getExportSymbolOfValueSymbolIfExported(Symbol* symbol) {
	if (symbol != nullptr) {
		if (hooks.GetExportSymbolOfValueSymbolIfExported) {
			return hooks.GetExportSymbolOfValueSymbolIfExported(symbol);
		}
		if ((symbol->flags & SymbolFlagsExportValue) != 0 && symbol->data->exportSymbol != nullptr) {
			symbol = symbol->data->exportSymbol;
		}
		return getMergedSymbol(symbol);
	}
	return nullptr;
}

// referenceResolver::GetReferencedExportContainer — referenceresolver.go:151
Node* referenceResolver::GetReferencedExportContainer(Node* node, bool prefixLocals) {
	// When resolving the export for the name of a module or enum
	// declaration, we need to start resolution at the declaration's container.
	// Otherwise, we could incorrectly resolve the export as the
	// declaration if it contains an exported member with the same name.
	bool startInDeclarationContainer = node->parent != nullptr &&
		(node->parent->kind == Kind::ModuleDeclaration || node->parent->kind == Kind::EnumDeclaration) &&
		node == node->parent->name();
	if (Symbol* symbol = getReferencedValueSymbol(node, startInDeclarationContainer); symbol != nullptr) {
		if ((symbol->flags & SymbolFlagsExportValue) != 0) {
			// If we reference an exported entity within the same module declaration, then whether
			// we prefix depends on the kind of entity. SymbolFlags.ExportHasLocal encompasses all the
			// kinds that we do NOT prefix.
			Symbol* exportSymbol = getMergedSymbol(symbol->data->exportSymbol);
			if (!prefixLocals && (exportSymbol->flags & SymbolFlagsExportHasLocal) != 0 &&
				(exportSymbol->flags & SymbolFlagsVariable) == 0) {
				return nullptr;
			}
			symbol = exportSymbol;
		}
		Symbol* parentSymbol = getParentOfSymbol(symbol);
		if (parentSymbol != nullptr) {
			if ((parentSymbol->flags & SymbolFlagsValueModule) != 0 &&
				parentSymbol->data->valueDeclaration != nullptr &&
				parentSymbol->data->valueDeclaration->kind == Kind::SourceFile) {
				SourceFile* symbolFile = parentSymbol->data->valueDeclaration->as<SourceFile>();
				SourceFile* referenceFile = getSourceFileOfNode(node);
				// If `node` accesses an export and that export isn't in the same file, then symbol is a namespace export, so return nil.
				bool symbolIsUmdExport = symbolFile != referenceFile;
				if (symbolIsUmdExport) {
					return nullptr;
				}
				return symbolFile->asNode();
			}
			auto isMatchingContainer = [this, parentSymbol](Node* n) -> bool {
				return (n->kind == Kind::ModuleDeclaration || n->kind == Kind::EnumDeclaration) &&
					getSymbolOfDeclaration(n) == parentSymbol;
			};
			return findAncestor(node->parent, isMatchingContainer);
		}
	}

	return nullptr;
}

// referenceResolver::GetReferencedImportDeclaration — referenceresolver.go:190
Node* referenceResolver::GetReferencedImportDeclaration(Node* node) {
	if (Symbol* symbol = getReferencedValueSymbol(node, false /*startInDeclarationContainer*/); symbol != nullptr) {
		// We should only get the declaration of an alias if there isn't a local value
		// declaration for the symbol
		if (isNonLocalAlias(symbol, SymbolFlagsValue /*excludes*/) && !isTypeOnlyAliasDeclaration(symbol)) {
			return getDeclarationOfAliasSymbol(symbol);
		}
	}

	return nullptr;
}

// referenceResolver::GetReferencedValueDeclaration — referenceresolver.go:202
Node* referenceResolver::GetReferencedValueDeclaration(Node* node) {
	if (Symbol* symbol = getReferencedValueSymbol(node, false /*startInDeclarationContainer*/); symbol != nullptr) {
		return getExportSymbolOfValueSymbolIfExported(symbol)->data->valueDeclaration;
	}
	return nullptr;
}

// referenceResolver::GetReferencedValueDeclarations — referenceresolver.go:209
std::vector<Node*> referenceResolver::GetReferencedValueDeclarations(Node* node) {
	std::vector<Node*> declarations;
	if (Symbol* symbol = getReferencedValueSymbol(node, false /*startInDeclarationContainer*/); symbol != nullptr) {
		symbol = getExportSymbolOfValueSymbolIfExported(symbol);
		for (Node* declaration : symbol->data->declarations) {
			switch (declaration->kind) {
			case Kind::VariableDeclaration:
			case Kind::Parameter:
			case Kind::BindingElement:
			case Kind::PropertyDeclaration:
			case Kind::PropertyAssignment:
			case Kind::ShorthandPropertyAssignment:
			case Kind::EnumMember:
			case Kind::ObjectLiteralExpression:
			case Kind::FunctionDeclaration:
			case Kind::FunctionExpression:
			case Kind::ArrowFunction:
			case Kind::ClassDeclaration:
			case Kind::ClassExpression:
			case Kind::EnumDeclaration:
			case Kind::MethodDeclaration:
			case Kind::GetAccessor:
			case Kind::SetAccessor:
			case Kind::ModuleDeclaration:
				declarations.push_back(declaration);
			}
		}
	}
	return declarations;
}

// referenceResolver::GetElementAccessExpressionName — referenceresolver.go:240
std::string referenceResolver::GetElementAccessExpressionName(ElementAccessExpression* expression) {
	if (expression != nullptr) {
		if (hooks.GetElementAccessExpressionName) {
			if (auto [name, ok] = hooks.GetElementAccessExpressionName(expression); ok) {
				return name;
			}
		}
	}
	return "";
}

// referenceResolver::GetReferencedMemberValueDeclaration — referenceresolver.go:251
Node* referenceResolver::GetReferencedMemberValueDeclaration(Node* node) {
	// member references are `this.something` or `this[something]`, so should always simply have a resolved symbol
	Symbol* s = getResolvedSymbol(node);
	if (s == nullptr && node->symbol() != nullptr) {
		// might be a declaration instead of a ref, get the merged declaration symbol
		s = getMergedSymbol(node->symbol());
	}
	if (s == nullptr) {
		return nullptr;
	}
	return getExportSymbolOfValueSymbolIfExported(s)->data->valueDeclaration;
}

}  // anonymous namespace

// NewReferenceResolver — referenceresolver.go:37
ReferenceResolver* NewReferenceResolver(const CompilerOptions* options, ReferenceResolverHooks hooks) {
	auto* r = new referenceResolver();
	r->options = options;
	r->hooks = std::move(hooks);
	return r;
}

}  // namespace binder
}  // namespace tsc
