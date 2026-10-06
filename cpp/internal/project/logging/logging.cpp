// logging — dep-stubs for the ls-autoimport slice.
#include "internal/project/logging/logging.h"

namespace tsc::logging {

// === dep stubs — removed when owner slice lands ===

void LogTree::Logf(std::string_view) {
	TSC_UNREACHABLE("LogTree::Logf — owned by project");
}

LogTree* LogTree::Fork() {
	TSC_UNREACHABLE("LogTree::Fork — owned by project");
}

} // namespace tsc::logging
