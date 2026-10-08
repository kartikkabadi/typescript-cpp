#pragma once

// tsc.h — umbrella header for the execute/tsc slice's public surface:
// compile.go (System/ExitStatus/Watcher/CommandLineResult/CommandLineTesting/
// CompileTimes/CompileAndEmitResult/NewContentMapperHost), diagnostics.go
// (reporters), emit.go (EmitInput/EmitAndReportStatistics/GetTrace...),
// extendedconfigcache.go, statistics.go. Everything here is the real port;
// included as one header for the execute/build slice.

#include "internal/execute/tsc/compile.h"
#include "internal/execute/tsc/diagnostics.h"
#include "internal/execute/tsc/emit.h"
#include "internal/execute/tsc/extendedconfigcache.h"
#include "internal/execute/tsc/statistics.h"
