// checkerpool.h — checkerpool.go. The program's built-in checker pool:
// per-file checker affinity (weighted FENNEL partitioning), per-checker
// locks, and grouped parallel iteration over the pool.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/gostd/gostd.h"

namespace tsc::tracing {
class Tracing;
}
namespace tsc::checker {
class Checker;
}

namespace tsc::compiler {

class SimpleProgram;

// CheckerPool is implemented by the project system to provide checkers with
// request-scoped lifetime and reclamation. It returns a checker and a release
// function that must be called when the caller is done with the checker.
// The returned checker must not be accessed concurrently; each acquisition is exclusive.
// If file is non-nil, the pool may use it as an affinity hint to return the same
// checker for the same file across calls.
class CheckerPool {
public:
	virtual ~CheckerPool() = default;
	virtual std::pair<checker::Checker*, std::function<void()>> GetChecker(
	    const gostd::Context& ctx, SourceFile* file) = 0;
};

/*
Checker association is a balanced graph-partitioning problem:

  - A vertex is a source file.
  - An undirected edge connects two files for each resolved, in-program import
    entry between them. Multiple entries may connect the same pair and therefore
    strengthen their affinity. Self-imports and unresolved or external targets do
    not create edges.
  - A partition is a checker with its own symbol, type, and instantiation caches.

Putting related files on the same checker reduces duplicated cache construction,
but concentrating too many roots on one checker increases the parallel critical
path. We use weighted FENNEL to trade off those objectives:

  affinity(partition) - alpha * incrementalLoadPenalty(partition)

See Tsourakakis et al., "FENNEL: Streaming Graph Partitioning for Massive Scale
Graphs", WSDM 2014: https://doi.org/10.1145/2556195.2556213.

FENNEL is sensitive to stream order. This is both established in the partitioning
literature (for example, Awadelkarim and Ugander, "Prioritized Restreaming
Algorithms for Balanced Graph Partitioning", KDD 2020:
https://arxiv.org/abs/2007.03131) and pronounced in checker workloads because
semantic work is demand-driven. During calibration with four checkers,
degree-first streams produced nearly equal estimated loads but highly unequal
per-checker completion times:

  - MUI docs: approximately 0.6s, 2.8s, 15.3s, and 19.0s.
  - XState: approximately 0.04s, 0.35s, 0.67s, and 1.18s.

Seeded random-order testing also found repeatable slow orders with identical
diagnostics, including about +37% MUI docs and +59% XState compiler Check time
relative to normal order, again with four checkers. Therefore stream order is part
of the policy below, rather than an incidental implementation detail.
*/

/*
The constants below are empirical safety factors for a work proxy that cannot
observe future semantic cache construction. They were swept across representative
projects including VS Code, TypeScript, MUI docs, XState, and Bluesky, with 2, 4,
and 8 checkers:

  - 100-byte text weight divisor: among 25, 50, 75, 80, 90, 100, 110, 125, 150,
    200, and 400, this kept large literals, comments, and generated files from
    appearing artificially cheap without allowing raw byte length to dominate.
    Nearby values occasionally improved one project, but 100 was the most robust
    setting, particularly on VS Code and MUI docs.
  - 4x source-file weight: among 2x, 3x, 4x, 5x, and 8x, this best balanced
    declaration-heavy projects without losing the locality benefit.
  - 16x FENNEL penalty: 1x, 2x, 4x, 8x, 12x, 16x, 20x, 24x, 32x, and 64x were
    sampled across the experiments; 16x was the most robust balance/locality
    compromise for declaration-heavy projects.
  - 12x prioritized-source penalty: 8x, 12x, 16x, 20x, and 24x were compared;
    source-first ordering already spreads expensive roots, and 12x retained more
    locality than the stronger settings.
  - 4-checker cutoff: at 2-3 checkers the tight load cap provides enough balance;
    extra source weighting and penalty pressure regressed some projects.

These are project-independent operating points, not formulas derived by FENNEL.
Rebenchmark the vscode, self-compiler, mui-docs, and xstate-main scenarios in the
TypeScript-benchmarking repository at 2, 4, and 8 checkers before changing them.
*/
inline constexpr int checkerAssociationTextWeightDivisor = 100;
inline constexpr int checkerAssociationSourceFileWeightMultiplier = 4;
inline constexpr int checkerAssociationBalancePenaltyMultiplier = 16;
inline constexpr int checkerAssociationPrioritizedSourcePenalty = 12;
inline constexpr int checkerAssociationStrongBalanceMinCheckerCount = 4;

struct checkerAssociationPolicy {
	bool prioritizeSourceFiles = false;
	int sourceFileWeightMultiplier = 1;
	int balancePenaltyMultiplier = 1;
};

/*
getCheckerAssociationPolicy selects one of three calibrated regimes:

 1. Source-dominated, any checker count:
    source files first by descending weight; unmodified source-file weight;
    checkerAssociationPrioritizedSourcePenalty.
 2. Declaration-heavy, at least checkerAssociationStrongBalanceMinCheckerCount:
    program order; checkerAssociationSourceFileWeightMultiplier;
    checkerAssociationBalancePenaltyMultiplier.
 3. Declaration-heavy, fewer checkers:
    program order; unmodified source-file weight; unscaled adapted FENNEL
    penalty.

The source-dominated test is evaluated first intentionally: projects with very
little declaration work benefit from balancing source-file roots directly even
with a small checker pool.
*/
checkerAssociationPolicy getCheckerAssociationPolicy(
	int totalWeight, int declarationWeight, int checkerCount);

// getCheckerAssociationsInOrder partitions the import graph using a weighted adaptation
// of FENNEL's streaming graph-partitioning objective with gamma = 3/2. Each file
// is placed where it has the most already-placed neighbors, minus the incremental
// convex load penalty. The published alpha = m*sqrt(k)/n^(3/2) becomes
// m*sqrt(k)/W^(3/2), where W is total estimated checker work. penaltyMultiplier
// applies the empirical safety factor selected by getCheckerAssociationPolicy.
//
// An empty fileOrder means stable program order (Go's nil). The preferred
// maximum checker weight is the larger of the largest file and roughly 101% of
// average. If no checker can accept a file under that bound, the file is
// assigned to the least-loaded checker. The 1% slack permits discrete files to
// pack near the average while preventing affinity from deliberately creating
// meaningful estimated imbalance. Ties are deterministic.
std::vector<int> getCheckerAssociationsInOrder(
	const std::vector<int>& fileWeights,
	const std::vector<std::vector<int>>& adjacentFiles,
	const std::vector<int>& fileOrder, int checkerCount,
	int penaltyMultiplier);

// getCheckerAssociationOrder places source files before declarations and
// orders each group by descending estimated work. This exposes expensive semantic
// roots early, when all checker loads are still available. An empty return
// preserves program order without allocating an index array (Go returns nil).
// Program order is itself a locality choice: it preserves deterministic groups
// produced during program construction and was consistently safer for
// declaration-heavy projects.
std::vector<int> getCheckerAssociationOrder(
	const std::vector<int>& fileWeights,
	const std::vector<bool>& isDeclarationFile, bool prioritizeSourceFiles);

int getCheckerAssociationBaseWeight(int nodeCount, int textLength);

// shouldPrioritizeSourceFiles reports whether all declaration-file base work is at
// most half of one average checker load:
//
//	declarationWeight <= totalWeight / (2 * checkerCount)
//
// This threshold separated source-dominated projects such as VS Code from projects
// where declaration locality remained important, such as MUI docs, TypeScript, and
// XState. Delaying at most half a checker-load of declarations was the stable
// boundary in the cross-project sweeps.
bool shouldPrioritizeSourceFiles(int totalWeight, int declarationWeight,
	                             int checkerCount);

// getCheckerAssociationWeights combines local syntax work with syntactic import
// fanout. One import unit is totalBaseWeight / totalImports, so imports collectively
// contribute approximately the same vertex weight as syntax. Syntactic imports are
// deliberately broader than getImportAdjacency's resolved, in-program edges: this
// term estimates the work of processing module references, while adjacency controls
// checker affinity. Normalizing the term avoids a project-specific vertex-weight
// constant.
std::vector<int> getCheckerAssociationWeights(
	const std::vector<int>& baseWeights,
	const std::vector<int>& importCounts);

// noop — checkerpool.go:492.
void noop();

// checkerPool — checkerpool.go:27.
class checkerPool final : public CheckerPool {
public:
	SimpleProgram* program = nullptr;
	tsc::tracing::Tracing* tracing = nullptr;

	OnceFlag createCheckersOnce;
	std::vector<std::unique_ptr<checker::Checker>> checkers;
	std::vector<std::unique_ptr<std::mutex>> locks;
	std::unordered_map<SourceFile*, checker::Checker*> fileAssociations;

	// GetChecker implements CheckerPool. When file is non-nil, returns the checker
	// associated with that file; otherwise returns the first checker.
	std::pair<checker::Checker*, std::function<void()>> GetChecker(
	    const gostd::Context& ctx, SourceFile* file) override;

	// getCheckerForFileNonExclusive returns the checker for the given file without locking.
	// This is only safe when the caller guarantees no concurrent access to the same checker,
	// e.g. for read-only operations like obtaining an emit resolver.
	std::pair<checker::Checker*, std::function<void()>>
	getCheckerForFileNonExclusive(SourceFile* file);

	std::pair<checker::Checker*, std::function<void()>>
	getCheckerForFileExclusive(const gostd::Context& ctx, SourceFile* file);

	// getCheckerNonExclusive returns the first checker without locking.
	std::pair<checker::Checker*, std::function<void()>>
	getCheckerNonExclusive();

	void createCheckers();

	// getImportAdjacency returns an undirected import graph represented by file
	// index. A directed import from A to B makes both files adjacent because either
	// file can benefit from sharing checker caches with the other.
	std::vector<std::vector<int>> getImportAdjacency();

	// Runs `cb` for each checker in the pool concurrently, locking and unlocking checker mutexes as it goes,
	// making it safe to call `forEachCheckerParallel` from many threads simultaneously.
	void forEachCheckerParallel(
	    const std::function<void(int, checker::Checker*)>& cb);

	std::vector<Diagnostic*> GetGlobalDiagnostics();

	// forEachCheckerGroupDo runs one task per checker in parallel. Each task iterates
	// the provided files, processing only those assigned to its checker. Within each
	// checker's set, files are visited in their original order.
	void forEachCheckerGroupDo(
	    const gostd::Context& ctx, const std::vector<SourceFile*>& files,
	    bool singleThreaded,
	    const std::function<void(checker::Checker*, int, SourceFile*)>& cb);
};

checkerPool* newCheckerPool(SimpleProgram* program);
checkerPool* newCheckerPoolWithTracing(SimpleProgram* program,
                                       tsc::tracing::Tracing* tr);

} // namespace tsc::compiler
