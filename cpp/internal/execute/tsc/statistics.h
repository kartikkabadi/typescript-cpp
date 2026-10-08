#pragma once

// statistics.go — port of tsc/internal/execute/tsc/statistics.go: the
// --diagnostics statistics table (files/lines/counts, memory, compile-time
// breakdown, content mapper timings).

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "internal/compiler/program.h"
#include "internal/execute/tsc/compile.h"
#include "internal/execute/tsc/emit.h"
#include "internal/gostd/gostd.h"

// === slice: api ===
namespace tsc::json { class Encoder; }

namespace tsc::execute::tsc {

struct tableRow {
	std::string name;
	std::string value;
};

// statsValue — table.add's Go `any` parameter: int, uint64, string, or
// time.Duration.
using statsValue = std::variant<int64_t, uint64_t, std::string, gostd::Duration>;

// table — statistics.go:17.
struct table {
	std::vector<tableRow> rows;

	// add — statistics.go:21. Duration values print via formatDuration.
	void add(std::string_view name, const statsValue& value);
	// print — statistics.go:28 (`%-*s %*s` aligned columns).
	void print(std::ostream* w);
};

// formatDuration — statistics.go:40 (`%.3fs` of d.Seconds()).
std::string formatDuration(gostd::Duration d);

// identifierCount — statistics.go:44.
int identifierCount(compiler::SimpleProgram* p);

// Statistics — statistics.go:49. Go package-private fields are public here.
struct Statistics {
	bool isAggregate = false;
	int Projects = 0;
	int ProjectsBuilt = 0;
	int TimestampUpdates = 0;
	int files = 0;
	int lines = 0;
	int identifiers = 0;
	int symbols = 0;
	int types = 0;
	int instantiations = 0;
	uint64_t memoryUsed = 0;
	uint64_t memoryAllocs = 0;
	CompileTimes* compileTimes = nullptr;

	// Report — statistics.go:79.
	void Report(std::ostream* w, CommandLineTesting* testing);
	// addContentMapperStatistics — statistics.go:123.
	void addContentMapperStatistics(table* t, std::string_view prefix);
	// Aggregate — statistics.go:144.
	void Aggregate(Statistics* stat);
	// SetTotalTime — statistics.go:165.
	void SetTotalTime(gostd::Duration totalTime);

	// === slice: api ===
	// encoding/json Marshal — only the exported Go fields are tagged for the
	// api ("Projects", "ProjectsBuilt", "TimestampUpdates" — no explicit tags,
	// so capitalized field names are used). Implemented in api/proto.cpp.
	std::string marshalJSONTo(json::Encoder& enc) const;
};

// statisticsFromProgram — statistics.go:66. `memoryUsed`/`memoryAllocs` take
// the place of Go's runtime.MemStats fields.
Statistics* statisticsFromProgram(const EmitInput& input, uint64_t memoryUsed,
                                  uint64_t memoryAllocs);

}  // namespace tsc::execute::tsc
