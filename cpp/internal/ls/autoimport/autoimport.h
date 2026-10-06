// === slice: autoimport ===
// import_adder.go / view.go / fix.go / export.go / registry.go — minimal
// autoimport declarations for the ls slice port. The sibling child package
// (autoimport slice) owns the real implementation; method bodies here are
// dep-stubs.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/modulespecifiers/types.h"
#include "internal/ast/flags.h"
#include "internal/ast/symbol.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/tspath/tspath.h"

namespace tsc {
struct SourceFile;
struct Node;
struct Symbol;
// (Go's *ast.IdentifierNode / *ast.TypeNode are *ast.Node — no separate
// C++ types.)
namespace checker {
struct Checker;
}
namespace compiler {
class SimpleProgram;
}
namespace lsconv {
struct Converters;
}
namespace autoimport {

// ProjectID — registry.go:32 (`interface { fmt.Stringer }`).
struct ProjectIDImpl {
	virtual ~ProjectIDImpl() = default;
	virtual std::string String() const = 0;
};
using ProjectID = std::shared_ptr<ProjectIDImpl>;

// QueryKind — view.go:73.
using QueryKind = int;
inline constexpr QueryKind QueryKindWordPrefix = 0;
inline constexpr QueryKind QueryKindExactMatch = 1;
inline constexpr QueryKind QueryKindCaseInsensitiveMatch = 2;

// ModuleID / ExportID / Export / ExportSyntax — export.go:16-48.
using ModuleID = std::string;
struct ExportID {
	autoimport::ModuleID ModuleID;
	std::string ExportName;
	bool operator==(const ExportID&) const = default;
};
struct ExportIDHash {
	size_t operator()(const ExportID& e) const {
		return std::hash<std::string>{}(e.ModuleID + "|" + e.ExportName);
	}
};
// ExportSyntax — export.go:24.
using ExportSyntax = int;
inline constexpr ExportSyntax ExportSyntaxNone = 0;
inline constexpr ExportSyntax ExportSyntaxModifier = 1;
inline constexpr ExportSyntax ExportSyntaxNamed = 2;
inline constexpr ExportSyntax ExportSyntaxDefaultModifier = 3;
inline constexpr ExportSyntax ExportSyntaxDefaultDeclaration = 4;
inline constexpr ExportSyntax ExportSyntaxEquals = 5;
inline constexpr ExportSyntax ExportSyntaxUMD = 6;
inline constexpr ExportSyntax ExportSyntaxStar = 7;
inline constexpr ExportSyntax ExportSyntaxCommonJSModuleExports = 8;
struct Export {
	ExportID exportID;
	std::string ModuleFileName;
	ExportSyntax Syntax;
	SymbolFlags Flags = 0;
	std::string localName;
	std::string through;
	ExportID Target;
	bool IsTypeOnly = false;
	lsutil::ScriptElementKind ScriptElementKind;
	lsutil::ScriptElementKindModifier ScriptElementKindModifiers;
	std::string Path;
	std::string PackageName;

	// Name — export.go:71.
	std::string Name() const {
		if (!localName.empty()) {
			return localName;
		}
		if (exportID.ExportName == InternalSymbolNameExportEquals) {
			return Target.ExportName;
		}
		return exportID.ExportName;
	}
	// IsRenameable — export.go:81.
	bool IsRenameable() const {
		return exportID.ExportName == InternalSymbolNameExportEquals ||
		       exportID.ExportName == InternalSymbolNameDefault;
	}
	// AmbientModuleName — export.go:85.
	std::string AmbientModuleName() const {
		if (!tspath::isExternalModuleNameRelative(exportID.ModuleID)) {
			return exportID.ModuleID;
		}
		return "";
	}
	// IsUnresolvedAlias — export.go:92.
	bool IsUnresolvedAlias() const { return Flags == SymbolFlagsAlias; }
};

// Fix — fix.go:37. Go embeds *lsproto.AutoImportFix.
struct Fix {
	lsproto::AutoImportFix* AutoImportFix = nullptr;
	modulespecifiers::ResultKind ModuleSpecifierKind;
	bool IsReExport = false;
	std::string ModuleFileName;
	Node* TypeOnlyAliasDeclaration = nullptr;

	// Edits — fix.go:555. Dep-stub.
	std::tuple<std::vector<lsproto::TextEdit*>, std::string, bool> Edits(
	    const gostd::Context& ctx, SourceFile* file,
	    const CompilerOptions* compilerOptions,
	    lsutil::FormatCodeSettings formatOptions,
	    lsconv::Converters* converters,
	    lsutil::UserPreferences preferences);
};

// ImportAdder — import_adder.go:24.
struct ImportAdder {
	virtual ~ImportAdder() = default;
	virtual bool HasFixes() = 0;
	virtual void AddImportFromExportedSymbol(Symbol* symbol,
	                                         bool isValidTypeOnlyUseSite) = 0;
	virtual void AddImportFix(Fix* fix) = 0;
	virtual std::vector<lsproto::TextEdit*> Edits() = 0;
};

// View — view.go. Dep-stub methods.
struct View {
	std::vector<Export*> Search(const std::string& query, QueryKind kind);
	std::vector<Export*> SearchByExportID(ExportID id);
	std::vector<Fix*> GetFixes(Export* export_, bool forJSX,
	                           bool isValidTypeOnlyUseSite,
	                           lsproto::Position* usagePosition);
	int CompareFixes(Fix* a, Fix* b);
	int CompareFixesForSorting(Fix* a, Fix* b);
	int CompareFixesForRanking(Fix* a, Fix* b);
};

// Registry — registry.go:328. Only the member the ls slice calls.
struct Registry {
	bool IsPreparedForImportingFile(const std::string& fileName,
	                                ProjectID projectID,
	                                lsutil::UserPreferences preferences);
};

// NewView — view.go:37.
View* NewView(Registry* registry, SourceFile* importingFile,
              ProjectID projectID, compiler::SimpleProgram* program,
              checker::Checker* typeChecker,
              modulespecifiers::UserPreferences preferences);

// NewImportAdder — import_adder.go:70.
std::unique_ptr<ImportAdder> NewImportAdder(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    checker::Checker* ch, SourceFile* file, View* view,
    lsutil::FormatCodeSettings formatOptions, lsconv::Converters* converters,
    lsutil::UserPreferences preferences);

// SymbolToExport — export.go:96.
Export* SymbolToExport(Symbol* symbol, checker::Checker* ch);

// TryGetAutoImportableReferenceFromTypeNode — import_adder.go:427.
std::pair<Node*, std::vector<Symbol*>>
TryGetAutoImportableReferenceFromTypeNode(
    Node* importTypeNode,
    const std::unordered_map<Node*, Symbol*>& idToSymbol);

} // namespace autoimport
} // namespace tsc
