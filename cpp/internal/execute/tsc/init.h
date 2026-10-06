#pragma once

// init.go — port of tsc/internal/execute/tsc/init.go: `tsc --init`
// (tsconfig.json generation).

#include <string>

#include "internal/collections/collections.h"
#include "internal/execute/tsc/diagnostics.h"
#include "internal/locale/locale.h"
#include "internal/tsoptions/tsoptions.h"

namespace tsc::execute::tsc {

// WriteConfigFile — init.go:17.
void WriteConfigFile(System* sys, locale::Locale locale,
                     DiagnosticReporter reportDiagnostic,
                     const tsoptions::JsonObjectPtr& options);

// generateTSConfig — init.go:32.
std::string generateTSConfig(const tsoptions::JsonObjectPtr& options,
                             locale::Locale locale);

}  // namespace tsc::execute::tsc
