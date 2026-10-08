// Port of tsc/internal/compiler/checkerpool_test.go (package compiler —
// in-package test; the association helpers are declared in checkerpool.h).
#include <vector>

#include "internal/compiler/checkerpool.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;
using tsc::compiler::checkerAssociationBalancePenaltyMultiplier;
using tsc::compiler::checkerAssociationPolicy;
using tsc::compiler::checkerAssociationPrioritizedSourcePenalty;
using tsc::compiler::checkerAssociationSourceFileWeightMultiplier;
using tsc::compiler::getCheckerAssociationBaseWeight;
using tsc::compiler::getCheckerAssociationOrder;
using tsc::compiler::getCheckerAssociationPolicy;
using tsc::compiler::getCheckerAssociationWeights;
using tsc::compiler::getCheckerAssociationsInOrder;
using tsc::compiler::shouldPrioritizeSourceFiles;

static bool operator==(const checkerAssociationPolicy& a,
					   const checkerAssociationPolicy& b) {
	return a.prioritizeSourceFiles == b.prioritizeSourceFiles &&
		a.sourceFileWeightMultiplier == b.sourceFileWeightMultiplier &&
		a.balancePenaltyMultiplier == b.balancePenaltyMultiplier;
}

static void TestGetCheckerAssociationBaseWeight(T* t) {
	t->Parallel();
	if (int got = getCheckerAssociationBaseWeight(100, 2500); got != 125) {
		t->Fatalf("getCheckerAssociationBaseWeight() = %d, want 125", {got});
	}
}

static void TestShouldPrioritizeSourceFiles(T* t) {
	t->Parallel();
	if (!shouldPrioritizeSourceFiles(1000, 100, 4)) {
		t->Fatal({"shouldPrioritizeSourceFiles() = false, want true"});
	}
	if (!shouldPrioritizeSourceFiles(1000, 125, 4)) {
		t->Fatal(
			{"shouldPrioritizeSourceFiles() = false at boundary, want true"});
	}
	if (shouldPrioritizeSourceFiles(1000, 126, 4)) {
		t->Fatal({"shouldPrioritizeSourceFiles() = true, want false"});
	}
}

static void TestGetCheckerAssociationPolicy(T* t) {
	t->Parallel();
	struct TestCase {
		const char* name;
		int totalWeight;
		int declarationWeight;
		int checkerCount;
		checkerAssociationPolicy want;
	};
	static const TestCase tests[] = {
		{
			"source dominated at any checker count",
			1000,
			100,
			2,
			checkerAssociationPolicy{
				/*.prioritizeSourceFiles =*/ true,
				/*.sourceFileWeightMultiplier =*/ 1,
				/*.balancePenaltyMultiplier =*/
				checkerAssociationPrioritizedSourcePenalty,
			},
		},
		{
			"declaration heavy with few checkers",
			1000,
			400,
			2,
			checkerAssociationPolicy{
				/*.prioritizeSourceFiles =*/ false,
				/*.sourceFileWeightMultiplier =*/ 1,
				/*.balancePenaltyMultiplier =*/ 1,
			},
		},
		{
			"source dominated with many checkers",
			1000,
			50,
			8,
			checkerAssociationPolicy{
				/*.prioritizeSourceFiles =*/ true,
				/*.sourceFileWeightMultiplier =*/ 1,
				/*.balancePenaltyMultiplier =*/
				checkerAssociationPrioritizedSourcePenalty,
			},
		},
		{
			"declaration heavy with many checkers",
			1000,
			400,
			4,
			checkerAssociationPolicy{
				/*.prioritizeSourceFiles =*/ false,
				/*.sourceFileWeightMultiplier =*/
				checkerAssociationSourceFileWeightMultiplier,
				/*.balancePenaltyMultiplier =*/
				checkerAssociationBalancePenaltyMultiplier,
			},
		},
	};
	for (auto& test : tests) {
		t->Run(test.name, [&test](T* t) {
			t->Parallel();
			if (checkerAssociationPolicy got = getCheckerAssociationPolicy(
					test.totalWeight, test.declarationWeight, test.checkerCount);
				!(got == test.want)) {
				t->Fatalf(
					"getCheckerAssociationPolicy() = {%v %d %d}, want {%v %d "
					"%d}",
					{got.prioritizeSourceFiles, got.sourceFileWeightMultiplier,
					 got.balancePenaltyMultiplier,
					 test.want.prioritizeSourceFiles,
					 test.want.sourceFileWeightMultiplier,
					 test.want.balancePenaltyMultiplier});
			}
		});
	}
}

static void TestGetCheckerAssociationOrder(T* t) {
	t->Parallel();
	if (std::vector<int> got = getCheckerAssociationOrder(
			{5, 10, 7, 2}, {true, false, false, true}, true);
		got != std::vector<int>{1, 2, 0, 3}) {
		t->Fatalf("getCheckerAssociationOrder() = %v, want [1 2 0 3]",
				  {(int)got.size()});
	}
	if (std::vector<int> got =
			getCheckerAssociationOrder({5}, {false}, false);
		!got.empty()) {
		t->Fatalf("getCheckerAssociationOrder() = %v, want nil",
				  {(int)got.size()});
	}
}

static void TestGetCheckerAssociationWeights(T* t) {
	t->Parallel();

	struct TestCase {
		const char* name;
		std::vector<int> baseWeights;
		std::vector<int> importCounts;
		std::vector<int> want;
	};
	static const TestCase tests[] = {
		{
			"normalizes import work to syntax work",
			{100, 50, 25},
			{0, 1, 3},
			{100, 93, 154},
		},
		{
			"no imports preserves base weights",
			{100, 50, 25},
			{0, 0, 0},
			{100, 50, 25},
		},
	};

	for (auto& test : tests) {
		t->Run(test.name, [&test](T* t) {
			t->Parallel();
			std::vector<int> got = getCheckerAssociationWeights(
				test.baseWeights, test.importCounts);
			if (got != test.want) {
				t->Fatalf(
					"getCheckerAssociationWeights(%v, %v) = %v, want %v",
					{(int)test.baseWeights.size(),
					 (int)test.importCounts.size(), (int)got.size(),
					 (int)test.want.size()});
			}
		});
	}
}

static void TestGetCheckerAssociations(T* t) {
	t->Parallel();

	t->Run("empty", [](T* t) {
		t->Parallel();
		if (std::vector<int> got = getCheckerAssociationsInOrder(
				{}, {}, {}, 4, checkerAssociationBalancePenaltyMultiplier);
			!got.empty()) {
			t->Fatalf("getCheckerAssociationsInOrder() = %v, want nil",
					  {(int)got.size()});
		}
	});

	t->Run("balances disconnected files", [](T* t) {
		t->Parallel();
		std::vector<int> got = getCheckerAssociationsInOrder(
			{1, 1, 1, 1, 1, 1}, std::vector<std::vector<int>>(6), {}, 3, 1);
		std::vector<int> want = {0, 1, 2, 0, 1, 2};
		if (got != want) {
			t->Fatalf("getCheckerAssociationsInOrder() = %v, want %v",
					  {(int)got.size(), (int)want.size()});
		}
	});

	t->Run("uses program order", [](T* t) {
		t->Parallel();
		std::vector<int> got = getCheckerAssociationsInOrder(
			{1, 3, 2}, std::vector<std::vector<int>>(3), {}, 2, 1);
		std::vector<int> want = {0, 1, 0};
		if (got != want) {
			t->Fatalf("getCheckerAssociationsInOrder() = %v, want %v",
					  {(int)got.size(), (int)want.size()});
		}
	});

	t->Run("keeps dense components together", [](T* t) {
		t->Parallel();
		std::vector<int> got = getCheckerAssociationsInOrder(
			{1, 1, 1, 1, 1, 1},
			{
				{1, 2},
				{0, 2},
				{0, 1},
				{4, 5},
				{3, 5},
				{3, 4},
			},
			{}, 2, 1);
		std::vector<int> want = {0, 0, 0, 1, 1, 1};
		if (got != want) {
			t->Fatalf("getCheckerAssociationsInOrder() = %v, want %v",
					  {(int)got.size(), (int)want.size()});
		}
	});

	t->Run("respects weighted balance cap", [](T* t) {
		t->Parallel();
		std::vector<int> weights = {8, 7, 6, 5, 4, 3, 2, 1};
		std::vector<int> got = getCheckerAssociationsInOrder(
			weights, std::vector<std::vector<int>>(weights.size()), {}, 3, 1);
		std::vector<int> loads(3);
		for (size_t i = 0; i < got.size(); i++) {
			loads[got[i]] += weights[i];
		}
		for (size_t checkerIndex = 0; checkerIndex < loads.size();
			 checkerIndex++) {
			if (loads[checkerIndex] > 13) {
				t->Fatalf(
					"checker %d load = %d, want at most 13; associations = %v",
					{(int)checkerIndex, loads[checkerIndex],
					 (int)got.size()});
			}
		}
	});
}

REGISTER_UNIT_TEST("compiler.TestGetCheckerAssociationBaseWeight",
				   TestGetCheckerAssociationBaseWeight);
REGISTER_UNIT_TEST("compiler.TestShouldPrioritizeSourceFiles",
				   TestShouldPrioritizeSourceFiles);
REGISTER_UNIT_TEST("compiler.TestGetCheckerAssociationPolicy",
				   TestGetCheckerAssociationPolicy);
REGISTER_UNIT_TEST("compiler.TestGetCheckerAssociationOrder",
				   TestGetCheckerAssociationOrder);
REGISTER_UNIT_TEST("compiler.TestGetCheckerAssociationWeights",
				   TestGetCheckerAssociationWeights);
REGISTER_UNIT_TEST("compiler.TestGetCheckerAssociations",
				   TestGetCheckerAssociations);
