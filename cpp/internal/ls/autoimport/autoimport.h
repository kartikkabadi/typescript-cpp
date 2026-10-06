// === dep decls — owned by ls ===
// Decls the api slice needs from tsc/internal/ls/autoimport (registry.go,
// view.go, import_adder.go). All function bodies are stubbed in autoimport.cpp
// with TSC_UNREACHABLE. The ls slice should replace this file when it lands.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "internal/format/format.h" // lsutil::UserPreferences, FormatCodeSettings
#include "internal/gostd/gostd.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/modulespecifiers/types.h"

namespace tsc {
struct Node; struct SourceFile; struct Symbol;
namespace compiler { struct SimpleProgram; }
namespace checker { struct Checker; }
namespace lsconv { struct Converters; }
} // namespace tsc

namespace tsc::autoimport {

// ProjectID (registry.go:32) — Go's `interface{ fmt.Stringer }`. The api
// passes project.ID (a string). Kept as std::string here.
using ProjectID = std::string;

// Registry (registry.go:328) — opaque to the api slice.
struct Registry {
	// IsPreparedForImportingFile (registry.go:354) — owned by ls (stub).
	bool IsPreparedForImportingFile(const std::string& fileName,
	                                const ProjectID& projectID,
	                                const lsutil::UserPreferences& preferences);
};

// Export / Fix (opaque).
struct Export {};
struct Fix {};

// View (view.go) — owned by ls; methods stubbed.
struct View {
	virtual ~View() = default;
	virtual std::vector<Export*> Search(const std::string& query, int kind /* QueryKind */) = 0;
	virtual std::vector<Export*> SearchByExportID(uint64_t id) = 0;
};

// NewView (view.go:37) — owned by ls (stub).
View* NewView(Registry* registry, SourceFile* importingFile,
              const ProjectID& projectID, compiler::SimpleProgram* program,
              checker::Checker* typeChecker,
              const modulespecifiers::UserPreferences& preferences);

// ImportAdder (import_adder.go:24).
struct ImportAdder {
	virtual ~ImportAdder() = default;
	virtual bool HasFixes() = 0;
	virtual void AddImportFromExportedSymbol(Symbol* symbol,
	                                         bool isValidTypeOnlyUseSite) = 0;
	virtual void AddImportFix(Fix* fix) = 0;
	virtual std::vector<lsproto::TextEdit> Edits() = 0;
};

// NewImportAdder (import_adder.go:70) — owned by ls (stub).
ImportAdder* NewImportAdder(
	gostd::Context ctx, compiler::SimpleProgram* program,
	checker::Checker* checker, SourceFile* file, View* view,
	const lsutil::FormatCodeSettings& formatOptions,
	lsconv::Converters* converters,
	const lsutil::UserPreferences& preferences);

} // namespace tsc::autoimport
