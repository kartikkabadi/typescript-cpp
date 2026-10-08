// tests_bfs.cpp — port of tsc/internal/core/bfs_test.go.
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/utilities.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;

namespace {

using Graph = std::map<std::string, std::vector<std::string>>;

void TestBreadthFirstSearchParallel(T* t) {
	t->Parallel();

	t->Run("basic functionality", [](T* t) {
		t->Parallel();
		// Test basic functionality with a simple DAG
		// Graph: A -> B, A -> C, B -> D, C -> D
		Graph graph = {
		    {"A", {"B", "C"}},
		    {"B", {"D"}},
		    {"C", {"D"}},
		    {"D", {}},
		};

		auto children = [&graph](const std::string& node) {
			return graph[node];
		};

		t->Run("find specific node", [&](T* t) {
			t->Parallel();
			auto result = tsc::BreadthFirstSearchParallel<std::string>(
			    "A", children, [](const std::string& node) {
				    return std::pair{node == "D", true};
			    });
			assert::Assert(t, result.Stopped,
			               "Expected search to stop at D");
			assert::Assert(t, result.Path ==
			                      std::vector<std::string>(
			                          {"D", "B", "A"}));
		});

		t->Run("visit all nodes", [&](T* t) {
			t->Parallel();
			std::mutex mu;
			std::vector<std::string> visitedNodes;
			auto result = tsc::BreadthFirstSearchParallel<std::string>(
			    "A", children, [&](const std::string& node) {
				    std::lock_guard<std::mutex> lock(mu);
				    visitedNodes.push_back(node);
				    return std::pair{false, false};
			    });

			// Should return nil since we never return true
			assert::Assert(t, !result.Stopped,
			               "Expected search to not stop early");
			assert::Assert(t, result.Path.empty(),
			               "Expected nil path when visit function "
			               "never returns true");

			// Should visit all nodes exactly once
			std::sort(visitedNodes.begin(), visitedNodes.end());
			std::vector<std::string> expected = {"A", "B", "C", "D"};
			assert::Assert(t, visitedNodes == expected);
		});
	});

	t->Run("early termination", [](T* t) {
		t->Parallel();
		// Test that nodes below the target level are not visited
		Graph graph = {
		    {"Root", {"L1A", "L1B"}},
		    {"L1A", {"L2A", "L2B"}},
		    {"L1B", {"L2C"}},
		    {"L2A", {"L3A"}},
		    {"L2B", {}},
		    {"L2C", {}},
		    {"L3A", {}},
		};

		auto children = [&graph](const std::string& node) {
			return graph[node];
		};

		tsc::collections::SyncSet<std::string> visited;
		tsc::BreadthFirstSearchOptions<std::string, std::string>
		    options;
		options.Visited = &visited;
		tsc::BreadthFirstSearchParallelEx<std::string, std::string>(
		    "Root", children,
		    [](const std::string& node) {
			    return std::pair{node == "L2B", true};
		    },
		    options, tsc::Identity<std::string>);

		assert::Assert(t, visited.Has("Root"),
		               "Expected to visit Root");
		assert::Assert(t, visited.Has("L1A"), "Expected to visit L1A");
		assert::Assert(t, visited.Has("L1B"), "Expected to visit L1B");
		assert::Assert(t, visited.Has("L2A"), "Expected to visit L2A");
		assert::Assert(t, visited.Has("L2B"), "Expected to visit L2B");
		// L2C is non-deterministic
		assert::Assert(t, !visited.Has("L3A"),
		               "Expected not to visit L3A");
	});

	t->Run("returns fallback when no other result found", [](T* t) {
		t->Parallel();
		// Test that fallback behavior works correctly
		Graph graph = {
		    {"A", {"B", "C"}},
		    {"B", {"D"}},
		    {"C", {"D"}},
		    {"D", {}},
		};

		auto children = [&graph](const std::string& node) {
			return graph[node];
		};

		tsc::collections::SyncSet<std::string> visited;
		tsc::BreadthFirstSearchOptions<std::string, std::string>
		    options;
		options.Visited = &visited;
		auto result =
		    tsc::BreadthFirstSearchParallelEx<std::string,
		                                      std::string>(
		        "A", children,
		        [](const std::string& node) {
			        return std::pair{node == "A", false};
		        },
		        options, tsc::Identity<std::string>);

		assert::Assert(t, !result.Stopped,
		               "Expected search to not stop early");
		assert::Assert(t, result.Path ==
		                      std::vector<std::string>({"A"}));
		assert::Assert(t, visited.Has("B"), "Expected to visit B");
		assert::Assert(t, visited.Has("C"), "Expected to visit C");
		assert::Assert(t, visited.Has("D"), "Expected to visit D");
	});

	t->Run("returns a stop result over a fallback", [](T* t) {
		t->Parallel();
		// Test that a stop result is preferred over a fallback
		Graph graph = {
		    {"A", {"B", "C"}},
		    {"B", {"D"}},
		    {"C", {"D"}},
		    {"D", {}},
		};

		auto children = [&graph](const std::string& node) {
			return graph[node];
		};

		auto result = tsc::BreadthFirstSearchParallel<std::string>(
		    "A", children, [](const std::string& node) {
			    if (node == "A") {
				    return std::pair{true, false};
			    }
			    if (node == "D") {
				    return std::pair{true, true};
			    }
			    return std::pair{false, false};
		    });

		assert::Assert(t, result.Stopped,
		               "Expected search to stop at D");
		assert::Assert(t, result.Path ==
		                      std::vector<std::string>(
		                          {"D", "B", "A"}));
	});
}

} // namespace

REGISTER_UNIT_TEST("core.TestBreadthFirstSearchParallel",
                   TestBreadthFirstSearchParallel);
