// export.go — Export record + symbol→export lookup.
#include "internal/ls/autoimport/autoimport.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls::autoimport {

// Export.Name — export.go:71
std::string Export::Name() const {
	if (!localName.empty()) {
		return localName;
	}
	if (exportID.ExportName == InternalSymbolNameExportEquals) {
		return Target.ExportName;
	}
	return exportID.ExportName;
}

// Export.IsRenameable — export.go:81
bool Export::IsRenameable() const {
	return exportID.ExportName == InternalSymbolNameExportEquals ||
	       exportID.ExportName == InternalSymbolNameDefault;
}

// Export.AmbientModuleName — export.go:85
std::string Export::AmbientModuleName() const {
	if (!tspath::isExternalModuleNameRelative(exportID.ModuleID)) {
		return exportID.ModuleID;
	}
	return "";
}

// Export.IsUnresolvedAlias — export.go:92
bool Export::IsUnresolvedAlias() const {
	return Flags == SymbolFlagsAlias;
}

// SymbolToExport — export.go:96
std::unique_ptr<Export> SymbolToExport(Symbol* symbol, checker::Checker* ch) {
	if (symbol->data->parent != nullptr &&
	    checker::isExternalModuleSymbol(symbol->data->parent)) {
		if (auto res = tryGetModuleIDAndFileNameOfModuleSymbol(symbol->data->parent)) {
			auto& [moduleID, moduleFileName] = *res;
			return extractFirstExport(
			    symbol, ch, moduleID, moduleFileName,
			    getSourceFileOfModule(symbol->data->parent));
		}
		return nullptr;
	}

	Node* declaration =
	    symbol->data->declarations.empty() ? nullptr : symbol->data->declarations.front();
	if (declaration == nullptr) {
		return nullptr;
	}

	SourceFile* file = getSourceFileOfNode(declaration);
	if (file->Symbol == nullptr) {
		return nullptr;
	}

	Symbol* moduleSymbol = ch->GetMergedSymbol(file->Symbol);
	ModuleID moduleID = file->Path();
	std::string moduleFileName = file->FileName();
	Symbol* target = ch->GetMergedSymbol(ch->SkipAlias(symbol));

	if (auto e = tryGetModuleExport(InternalSymbolNameDefault, target,
	                                moduleSymbol, ch, moduleID, moduleFileName,
	                                file)) {
		return e;
	}
	if (auto e = tryGetModuleExport(InternalSymbolNameExportEquals, target,
	                                moduleSymbol, ch, moduleID, moduleFileName,
	                                file)) {
		return e;
	}
	return tryGetModuleExport(symbol->data->name, target, moduleSymbol, ch, moduleID,
	                          moduleFileName, file);
}

// tryGetModuleExport — export.go:128
std::unique_ptr<Export> tryGetModuleExport(
    const std::string& exportName, Symbol* target, Symbol* moduleSymbol,
    checker::Checker* ch, const ModuleID& moduleID,
    const std::string& moduleFileName, SourceFile* file) {
	Symbol* exported =
	    ch->TryGetMemberInModuleExportsAndProperties(exportName, moduleSymbol);
	if (exported != nullptr &&
	    ch->GetMergedSymbol(ch->SkipAlias(exported)) == target) {
		return extractFirstExport(exported, ch, moduleID, moduleFileName, file);
	}
	return nullptr;
}

// extractFirstExport — export.go:136
std::unique_ptr<Export> extractFirstExport(
    Symbol* symbol, checker::Checker* ch, const ModuleID& moduleID,
    const std::string& moduleFileName, SourceFile* file) {
	std::vector<std::shared_ptr<Export>> exports;
	auto extractor = newSymbolExtractor("", ch, nullptr, nullptr);
	extractor->extractFromSymbol(symbol->data->name, symbol, moduleID, moduleFileName,
	                             file, &exports);
	if (exports.empty()) {
		return nullptr;
	}
	// Clone shares ownership with the extractor-local vector; the unique_ptr
	// result keeps the first export alive after `exports` is destroyed.
	return std::make_unique<Export>(*exports.front());
}

// === export_stringer_generated.go ===

namespace {
const char* exportSyntaxNames[] = {
    "ExportSyntaxNone",
    "ExportSyntaxModifier",
    "ExportSyntaxNamed",
    "ExportSyntaxDefaultModifier",
    "ExportSyntaxDefaultDeclaration",
    "ExportSyntaxEquals",
    "ExportSyntaxUMD",
    "ExportSyntaxStar",
    "ExportSyntaxCommonJSModuleExports",
    "ExportSyntaxCommonJSExportsProperty",
};
}  // namespace

// ExportSyntax.String — export_stringer_generated.go:27
std::string_view exportSyntaxString(ExportSyntax i) {
	int idx = static_cast<int>(i);
	if (idx < 0 ||
	    idx >= static_cast<int>(std::size(exportSyntaxNames))) {
		TSC_UNREACHABLE("ExportSyntax out of range");
	}
	return exportSyntaxNames[idx];
}

}  // namespace tsc::ls::autoimport
