// === dep decls — owned by ls ===
// Stubs for autoimport free functions; see autoimport.h.
#include "internal/ls/autoimport/autoimport.h"

namespace tsc::autoimport {

View* NewView(Registry* /*registry*/, SourceFile* /*importingFile*/,
              const ProjectID& /*projectID*/, compiler::SimpleProgram* /*program*/,
              checker::Checker* /*typeChecker*/,
              const modulespecifiers::UserPreferences& /*preferences*/) {
	TSC_UNREACHABLE("autoimport::NewView — owned by ls slice");
}

ImportAdder* NewImportAdder(
	gostd::Context /*ctx*/, compiler::SimpleProgram* /*program*/,
	checker::Checker* /*checker*/, SourceFile* /*file*/, View* /*view*/,
	const lsutil::FormatCodeSettings& /*formatOptions*/,
	lsconv::Converters* /*converters*/,
	const lsutil::UserPreferences& /*preferences*/) {
	TSC_UNREACHABLE("autoimport::NewImportAdder — owned by ls slice");
}

bool Registry::IsPreparedForImportingFile(
    const std::string& /*fileName*/, const ProjectID& /*projectID*/,
    const lsutil::UserPreferences& /*preferences*/) {
	TSC_UNREACHABLE(
	    "autoimport::Registry::IsPreparedForImportingFile — owned by ls slice");
}

} // namespace tsc::autoimport
