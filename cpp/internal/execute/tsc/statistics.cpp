// statistics.go — port of tsc/internal/execute/tsc/statistics.go.

#include "internal/execute/tsc/statistics.h"

#include <algorithm>
#include <cstdio>

namespace tsc::execute::tsc {
namespace {

// padRight — Go fmt `%*s`/`-` width verbs.
std::string padRight(std::string_view s, int width) {
	std::string out(s);
	if (static_cast<int>(out.size()) < width) {
		out.append(width - out.size(), ' ');
	}
	return out;
}

std::string padLeft(std::string_view s, int width) {
	std::string out;
	if (static_cast<int>(s.size()) < width) {
		out.append(width - s.size(), ' ');
	}
	out.append(s);
	return out;
}

}  // namespace

// table.add — statistics.go:21.
void table::add(std::string_view name, const statsValue& value) {
	if (auto* d = std::get_if<gostd::Duration>(&value)) {
		rows.push_back({std::string(name), formatDuration(*d)});
		return;
	}
	std::string text;
	if (auto* v = std::get_if<int64_t>(&value)) {
		text = std::to_string(*v);
	} else if (auto* v = std::get_if<uint64_t>(&value)) {
		text = std::to_string(*v);
	} else {
		text = std::get<std::string>(value);
	}
	rows.push_back({std::string(name), std::move(text)});
}

// table.print — statistics.go:28.
void table::print(std::ostream* w) {
	int nameWidth = 0;
	int valueWidth = 0;
	for (auto& r : rows) {
		nameWidth = std::max(nameWidth, static_cast<int>(r.name.size()));
		valueWidth = std::max(valueWidth, static_cast<int>(r.value.size()));
	}

	for (auto& r : rows) {
		// fmt.Fprintf(w, "%-*s %*s\n", nameWidth+1, r.name+":", valueWidth, r.value)
		*w << padRight(r.name + ":", nameWidth + 1) << ' '
		   << padLeft(r.value, valueWidth) << '\n';
	}
}

// formatDuration — statistics.go:40.
std::string formatDuration(gostd::Duration d) {
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.3fs",
	              static_cast<double>(d.count()) / 1e9);
	return buf;
}

// identifierCount — statistics.go:44.
int identifierCount(compiler::SimpleProgram* p) {
	int count = 0;
	for (auto* file : p->SourceFiles()) {
		count += file->IdentifierCount;
	}
	return count;
}

// statisticsFromProgram — statistics.go:66.
Statistics* statisticsFromProgram(const EmitInput& input, uint64_t memoryUsed,
                                  uint64_t memoryAllocs) {
	auto* s = new Statistics();
	s->files = static_cast<int>(input.Program->SourceFiles().size());
	s->lines = input.Program->LineCount();
	s->identifiers = input.Program->IdentifierCount();
	s->symbols = input.Program->SymbolCount();
	s->types = input.Program->TypeCount();
	s->instantiations = input.Program->InstantiationCount();
	s->memoryUsed = memoryUsed;
	s->memoryAllocs = memoryAllocs;
	s->compileTimes = input.CompileTimes;
	return s;
}

// Report — statistics.go:79.
void Statistics::Report(std::ostream* w, CommandLineTesting* testing) {
	if (testing != nullptr) {
		testing->OnStatisticsStart(w);
	}
	table t;
	std::string prefix;

	if (isAggregate) {
		prefix = "Aggregate ";
		t.add("Projects in scope", int64_t(Projects));
		t.add("Projects built", int64_t(ProjectsBuilt));
		t.add("Timestamps only updates", int64_t(TimestampUpdates));
	}
	t.add(prefix + "Files", int64_t(files));
	t.add(prefix + "Lines", int64_t(lines));
	t.add(prefix + "Identifiers", int64_t(identifiers));
	t.add(prefix + "Symbols", int64_t(symbols));
	t.add(prefix + "Types", int64_t(types));
	t.add(prefix + "Instantiations", int64_t(instantiations));
	t.add(prefix + "Memory used",
	      std::to_string(memoryUsed / 1024) + "K");
	t.add(prefix + "Memory allocs", std::to_string(memoryAllocs));
	if (compileTimes->ConfigTime != gostd::Duration{0}) {
		t.add(prefix + "Config time", compileTimes->ConfigTime);
	}
	if (compileTimes->BuildInfoReadTime != gostd::Duration{0}) {
		t.add(prefix + "BuildInfo read time", compileTimes->BuildInfoReadTime);
	}
	t.add(prefix + "Parse time", compileTimes->ParseTime);
	if (compileTimes->bindTime != gostd::Duration{0}) {
		t.add(prefix + "Bind time", compileTimes->bindTime);
	}
	if (compileTimes->checkTime != gostd::Duration{0}) {
		t.add(prefix + "Check time", compileTimes->checkTime);
	}
	if (compileTimes->emitTime != gostd::Duration{0}) {
		t.add(prefix + "Emit time", compileTimes->emitTime);
	}
	if (compileTimes->ChangesComputeTime != gostd::Duration{0}) {
		t.add(prefix + "Changes compute time",
		      compileTimes->ChangesComputeTime);
	}
	addContentMapperStatistics(&t, prefix);
	t.add(prefix + "Total time", compileTimes->totalTime);
	t.print(w);
	if (testing != nullptr) {
		testing->OnStatisticsEnd(w);
	}
}

// addContentMapperStatistics — statistics.go:123.
void Statistics::addContentMapperStatistics(table* t,
                                            std::string_view prefix) {
	auto& timings = compileTimes->ContentMapperTimes;
	if (timings.RequestWait != gostd::Duration{0}) {
		t->add(std::string(prefix) + "Content mapper request wait time",
		       timings.RequestWait);
	}
	std::vector<std::string> identities;
	identities.reserve(timings.Mappers.size());
	for (auto& [identity, _] : timings.Mappers) {
		identities.push_back(identity);
	}
	std::sort(identities.begin(), identities.end());
	for (auto& identity : identities) {
		auto& mapper = timings.Mappers[identity];
		uint64_t initializationCount = mapper.Spawn.Count;
		if (initializationCount != 0) {
			t->add(std::string(prefix) + identity + " initialization time",
			       mapper.Spawn.Duration + mapper.Initialize.Duration);
		}
		if (mapper.Transform.Count != 0) {
			t->add(std::string(prefix) + identity + " transform time",
			       mapper.Transform.Duration);
		}
		if (mapper.OpenProject.Count != 0) {
			t->add(std::string(prefix) + identity + " openProject time",
			       mapper.OpenProject.Duration);
		}
		if (mapper.CloseProject.Count != 0) {
			t->add(std::string(prefix) + identity + " closeProject time",
			       mapper.CloseProject.Duration);
		}
	}
}

// Aggregate — statistics.go:144.
void Statistics::Aggregate(Statistics* stat) {
	isAggregate = true;
	if (compileTimes == nullptr) {
		compileTimes = new CompileTimes();
	}
	// Aggregate statistics
	files += stat->files;
	lines += stat->lines;
	identifiers += stat->identifiers;
	symbols += stat->symbols;
	types += stat->types;
	instantiations += stat->instantiations;
	memoryUsed += stat->memoryUsed;
	memoryAllocs += stat->memoryAllocs;
	compileTimes->ConfigTime += stat->compileTimes->ConfigTime;
	compileTimes->BuildInfoReadTime += stat->compileTimes->BuildInfoReadTime;
	compileTimes->ParseTime += stat->compileTimes->ParseTime;
	compileTimes->bindTime += stat->compileTimes->bindTime;
	compileTimes->checkTime += stat->compileTimes->checkTime;
	compileTimes->emitTime += stat->compileTimes->emitTime;
	compileTimes->ChangesComputeTime += stat->compileTimes->ChangesComputeTime;
}

// SetTotalTime — statistics.go:165.
void Statistics::SetTotalTime(gostd::Duration totalTime) {
	if (compileTimes == nullptr) {
		compileTimes = new CompileTimes();
	}
	compileTimes->totalTime = totalTime;
}

}  // namespace tsc::execute::tsc
