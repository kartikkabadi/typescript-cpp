// === dep stub — owned by execute/build ===
#include "internal/execute/build/build.h"

#include "internal/core/types.h"

namespace tsc::build {

Orchestrator* NewOrchestrator(Options opts) {
	TSC_UNREACHABLE("build.NewOrchestrator — owned by execute/build");
}

} // namespace tsc::build
