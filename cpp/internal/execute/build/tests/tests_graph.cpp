// tests_graph.cpp — port of
// tsc/internal/execute/build/graph_test.go.
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/core/utilities.h"
#include "internal/execute/build/build.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::build;
namespace tsctests = ::tsc::execute::tsctests;
namespace build = ::tsc::execute::build;


using gostd::testing::T;
using tsctests::FileMap;

struct buildOrderTestCase {
	std::string name;
	std::vector<std::string> projects;
	std::vector<std::string> expected;
	std::vector<std::string> expectedSchedule;
	bool circular;

	std::string configName(const std::string& project) {
		return gostd::sprintf(
		    "/home/src/workspaces/project/%s/tsconfig.json", {project});
	}

	std::string projectName(const std::string& config) {
		std::string str = config;
		const std::string prefix = "/home/src/workspaces/project/";
		const std::string suffix = "/tsconfig.json";
		if (str.rfind(prefix, 0) == 0)
			str.erase(0, prefix.size());
		if (str.size() >= suffix.size() &&
		    str.compare(str.size() - suffix.size(), suffix.size(), suffix) ==
		        0)
			str.erase(str.size() - suffix.size());
		return str;
	}

	void run(T* t) {
		t->Helper();
		std::string subName = name + " - ";
		for (size_t i = 0; i < projects.size(); i++) {
			if (i)
				subName += ",";
			subName += projects[i];
		}
		t->Run(subName, [this](T* t) {
			t->Parallel();
			FileMap files;
			std::unordered_map<std::string, std::vector<std::string>> deps{
			    {"A", {"B", "C"}},
			    {"B", {"C", "D"}},
			    {"C", {"D", "E"}},
			    {"F", {"E"}},
			    {"H", {"I"}},
			    {"I", {"J"}},
			    {"J", {"H", "E"}},
			};
			std::unordered_map<std::string, std::vector<std::string>>
			    reverseDeps;
			for (auto& [project, ds] : deps) {
				for (auto& dep : ds)
					reverseDeps[dep].push_back(project);
			}
			auto contains = [](const std::vector<std::string>& v,
			                   const std::string& s) {
				return std::find(v.begin(), v.end(), s) != v.end();
			};
			auto indexOf = [](const std::vector<std::string>& v,
			                  const std::string& s) {
				auto it = std::find(v.begin(), v.end(), s);
				return it == v.end() ? -1 : int(it - v.begin());
			};
			auto verifyDeps = [&](Orchestrator* orchestrator,
			                      const std::vector<std::string>& buildOrder,
			                      bool hasDownStream) {
				for (size_t index = 0; index < buildOrder.size(); index++) {
					const std::string& project = buildOrder[index];
					auto upstream = ::tsc::Map(
					    orchestrator->Upstream(configName(project)),
					    [this](const std::string& s) {
						    return projectName(s);
					    });
					auto expectedUpstreamIt = deps.find(project);
					static const std::vector<std::string> emptyDeps;
					const std::vector<std::string>& expectedUpstream =
					    expectedUpstreamIt != deps.end()
					        ? expectedUpstreamIt->second
					        : emptyDeps;
					if (upstream.size() > expectedUpstream.size()) {
						t->Errorf(
						    "Expected upstream for %s to be at most %d, got %d",
						    {project, (int)expectedUpstream.size(),
						     (int)upstream.size()});
					}
					for (auto& expected : expectedUpstream) {
						std::vector<std::string> before(
						    buildOrder.begin(), buildOrder.begin() + index);
						if (contains(before, expected)) {
							if (!contains(upstream, expected)) {
								t->Errorf(
								    "Expected upstream for %s to contain %s",
								    {project, expected});
							}
						} else {
							if (contains(upstream, expected)) {
								t->Errorf(
								    "Expected upstream for %s to not contain %s",
								    {project, expected});
							}
						}
					}

					auto downstream = ::tsc::Map(
					    orchestrator->Downstream(configName(project)),
					    [this](const std::string& s) {
						    return projectName(s);
					    });
					auto rdIt = reverseDeps.find(project);
					const std::vector<std::string>& expectedDownstream =
					    hasDownStream && rdIt != reverseDeps.end()
					        ? rdIt->second
					        : emptyDeps;
					if (downstream.size() > expectedDownstream.size()) {
						t->Errorf(
						    "Expected downstream for %s to be at most %d, got %d",
						    {project, (int)expectedDownstream.size(),
						     (int)downstream.size()});
					}
					for (auto& expected : expectedDownstream) {
						std::vector<std::string> after(
						    buildOrder.begin() + index + 1,
						    buildOrder.end());
						if (contains(after, expected)) {
							if (!contains(downstream, expected)) {
								t->Errorf(
								    "Expected downstream for %s to contain %s",
								    {project, expected});
							}
						} else {
							if (contains(downstream, expected)) {
								t->Errorf(
								    "Expected downstream for %s to not contain %s",
								    {project, expected});
							}
						}
					}
				}
			};
			for (const char* project :
			     {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"}) {
				files[gostd::sprintf(
				    "/home/src/workspaces/project/%s/%s.ts",
				    {project, project})] = "export {}";
				std::string referencesStr;
				if (auto it = deps.find(project); it != deps.end()) {
					std::vector<std::string> refs;
					for (auto& dep : it->second) {
						refs.push_back(gostd::sprintf(
						    "{ \"path\": \"../%s\" }", {dep}));
					}
					std::string joined;
					for (size_t i = 0; i < refs.size(); i++) {
						if (i)
							joined += ",";
						joined += refs[i];
					}
					referencesStr = gostd::sprintf(
					    ", \"references\": [%s]", {joined});
				}
				files[configName(project)] = gostd::sprintf(
				    "{\n"
				    "                \"compilerOptions\": { \"composite\": true },\n"
				    "                \"files\": [\"./%s.ts\"],\n"
				    "                %s\n"
				    "            }",
				    {project, referencesStr});
			}

			auto sys = tsctests::NewTscSystem(
			    files, true, "/home/src/workspaces/project");
			std::vector<std::string> args{"--build", "--dry"};
			for (auto& p : projects)
				args.push_back(p);
			auto* buildCommand =
			    tsoptions::ParseBuildCommandLine(args, sys.get());
			auto* orchestrator = build::NewOrchestrator(
			    build::Options{sys.get(), buildCommand, nullptr});
			orchestrator->GenerateGraph(nullptr);
			auto buildOrder = ::tsc::Map(
			    orchestrator->Order(),
			    [this](const std::string& s) { return projectName(s); });
			assertDeepEqual(t, buildOrder, expected, "buildOrder");
			verifyDeps(orchestrator, buildOrder, false);
			auto scheduleOrder = ::tsc::Map(
			    orchestrator->ScheduleOrder(),
			    [this](const std::string& s) { return projectName(s); });
			assertDeepEqual(t, scheduleOrder, expectedSchedule,
			                "scheduleOrder");
			verifyDeps(orchestrator, scheduleOrder, false);

			if (!circular) {
				for (auto& [project, projectDeps] : deps) {
					std::string child = configName(project);
					int childIndex = indexOf(buildOrder, child);
					if (childIndex == -1)
						continue;
					for (auto& dep : projectDeps) {
						std::string parent = configName(dep);
						int parentIndex = indexOf(buildOrder, parent);

						if (childIndex <= parentIndex) {
							t->Errorf(
							    "Expecting child %s to be built after parent %s",
							    {project, dep});
						}
					}
				}
			}

			orchestrator->GenerateGraphReusingOldTasks();
			auto buildOrder2 = ::tsc::Map(
			    orchestrator->Order(),
			    [this](const std::string& s) { return projectName(s); });
			assertDeepEqual(t, buildOrder2, expected, "buildOrder2");

			std::vector<std::string> argsWatch{"--build", "--watch"};
			for (auto& p : projects)
				argsWatch.push_back(p);
			auto* buildCommandWatch =
			    tsoptions::ParseBuildCommandLine(argsWatch, sys.get());
			orchestrator = build::NewOrchestrator(
			    build::Options{sys.get(), buildCommandWatch, nullptr});
			orchestrator->GenerateGraph(nullptr);
			auto buildOrder3 = ::tsc::Map(
			    orchestrator->Order(),
			    [this](const std::string& s) { return projectName(s); });
			verifyDeps(orchestrator, buildOrder3, true);
		});
	}

	static void assertDeepEqual(T* t, const std::vector<std::string>& got,
	                            const std::vector<std::string>& want,
	                            const char* what) {
		if (got == want)
			return;
		auto join = [](const std::vector<std::string>& v) {
			std::string s;
			for (size_t i = 0; i < v.size(); i++) {
				if (i)
					s += ",";
				s += v[i];
			}
			return s;
		};
		t->Errorf("%s: got [%s], want [%s]",
		          {what, join(got), join(want)});
	}
};

void TestBuildOrderGenerator(T* t) {
	t->Parallel();
	std::vector<buildOrderTestCase> testCases{
	    {"specify two roots", {"A", "G"}, {"D", "E", "C", "B", "A", "G"},
	     {"D", "E", "G", "C", "B", "A"}, false},
	    {"multiple parts of the same graph in various orders", {"A"},
	     {"D", "E", "C", "B", "A"}, {"D", "E", "C", "B", "A"}, false},
	    {"multiple parts of the same graph in various orders",
	     {"A", "C", "D"}, {"D", "E", "C", "B", "A"}, {"D", "E", "C", "B", "A"},
	     false},
	    {"multiple parts of the same graph in various orders",
	     {"D", "C", "A"}, {"D", "E", "C", "B", "A"}, {"D", "E", "C", "B", "A"},
	     false},
	    {"other orderings", {"F"}, {"E", "F"}, {"E", "F"}, false},
	    {"other orderings", {"E"}, {"E"}, {"E"}, false},
	    {"other orderings", {"F", "C", "A"},
	     {"E", "F", "D", "C", "B", "A"}, {"E", "D", "F", "C", "B", "A"},
	     false},
	    {"returns circular order", {"H"}, {"E", "J", "I", "H"},
	     {"E", "J", "I", "H"}, true},
	    {"returns circular order", {"A", "H"},
	     {"D", "E", "C", "B", "A", "J", "I", "H"},
	     {"D", "E", "C", "J", "B", "I", "A", "H"}, true},
	};
	for (auto& testcase : testCases) {
		testcase.run(t);
	}
}
REGISTER_UNIT_TEST("build.TestBuildOrderGenerator",
                   TestBuildOrderGenerator);

} // namespace
} // namespace tsc
