// checkerpool.cpp — checkerpool.go. The program's built-in checker pool:
// checker creation, FENNEL file→checker association, per-checker locking,
// and grouped parallel iteration.
//
// Port notes:
//  - gostd::Context parameters match the interface; the built-in pool does
//    not consult them (Go also ignores ctx here).
//  - sync.OnceFunc releases are mirrored with a shared once_flag so a
//    double-called release never unlocks twice.
//  - Go's `lock.Lock()` on a non-reentrant mutex maps to std::mutex::lock —
//    a recursive acquisition deadlocks exactly like Go's sync.Mutex.

#include "internal/compiler/checkerpool.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/core/utilities.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace tsc::compiler {

namespace {

// onceFunc — sync.OnceFunc: the returned function runs fn at most once;
// subsequent calls are no-ops.
std::function<void()> onceFunc(std::function<void()> fn) {
	auto flag = std::make_shared<std::once_flag>();
	return [flag, fn = std::move(fn)]() mutable {
		std::call_once(*flag, fn);
	};
}

} // namespace

checkerAssociationPolicy getCheckerAssociationPolicy(
	int totalWeight, int declarationWeight, int checkerCount) {
	if (shouldPrioritizeSourceFiles(totalWeight, declarationWeight,
	                                checkerCount)) {
		return checkerAssociationPolicy{
		    .prioritizeSourceFiles = true,
		    .sourceFileWeightMultiplier = 1,
		    .balancePenaltyMultiplier = checkerAssociationPrioritizedSourcePenalty,
		};
	}
	if (checkerCount >= checkerAssociationStrongBalanceMinCheckerCount) {
		return checkerAssociationPolicy{
		    .prioritizeSourceFiles = false,
		    .sourceFileWeightMultiplier =
		        checkerAssociationSourceFileWeightMultiplier,
		    .balancePenaltyMultiplier =
		        checkerAssociationBalancePenaltyMultiplier,
		};
	}
	return checkerAssociationPolicy{
	    .prioritizeSourceFiles = false,
	    .sourceFileWeightMultiplier = 1,
	    .balancePenaltyMultiplier = 1,
	};
}

std::vector<int> getCheckerAssociationsInOrder(
	const std::vector<int>& fileWeights,
	const std::vector<std::vector<int>>& adjacentFiles,
	const std::vector<int>& fileOrder, int checkerCount,
	int penaltyMultiplier) {
	if (fileWeights.empty()) {
		return {};
	}

	int totalWeight = 0;
	int maxFileWeight = 0;
	int edgeCount = 0;
	for (size_t i = 0; i < fileWeights.size(); i++) {
		totalWeight += fileWeights[i];
		maxFileWeight = std::max(maxFileWeight, fileWeights[i]);
		edgeCount += static_cast<int>(adjacentFiles[i].size());
	}

	std::vector<int> associations(fileWeights.size(), -1);
	std::vector<int> checkerWeights(checkerCount, 0);
	int averageCheckerWeight = (totalWeight + checkerCount - 1) / checkerCount;
	int maxCheckerWeight =
	    std::max(maxFileWeight,
	             averageCheckerWeight + averageCheckerWeight / 100);
	double totalWeightFloat = static_cast<double>(totalWeight);
	double alpha = static_cast<double>(penaltyMultiplier) *
	               static_cast<double>(edgeCount / 2) *
	               std::sqrt(static_cast<double>(checkerCount)) /
	               (totalWeightFloat * std::sqrt(totalWeightFloat));
	std::vector<int> neighborCounts(checkerCount, 0);

	for (size_t position = 0; position < fileWeights.size(); position++) {
		int fileIndex = static_cast<int>(position);
		if (!fileOrder.empty()) {
			fileIndex = fileOrder[position];
		}

		std::fill(neighborCounts.begin(), neighborCounts.end(), 0);
		for (int adjacentFile : adjacentFiles[fileIndex]) {
			if (int checkerIndex = associations[adjacentFile];
			    checkerIndex >= 0) {
				neighborCounts[checkerIndex]++;
			}
		}

		int bestChecker = -1;
		double bestScore = -std::numeric_limits<double>::infinity();
		for (int checkerIndex = 0; checkerIndex < checkerCount;
		     checkerIndex++) {
			int checkerWeight = checkerWeights[checkerIndex];
			if (checkerWeight + fileWeights[fileIndex] >
			    maxCheckerWeight) {
				continue;
			}
			double oldWeight = static_cast<double>(checkerWeight);
			double newWeight =
			    static_cast<double>(checkerWeight + fileWeights[fileIndex]);
			double newPenalty = newWeight * std::sqrt(newWeight);
			double oldPenalty = oldWeight * std::sqrt(oldWeight);
			double penalty = alpha * (newPenalty - oldPenalty);
			double score =
			    static_cast<double>(neighborCounts[checkerIndex]) - penalty;
			if (score > bestScore ||
			    (score == bestScore &&
			     (bestChecker < 0 ||
			      checkerWeight < checkerWeights[bestChecker]))) {
				bestChecker = checkerIndex;
				bestScore = score;
			}
		}
		if (bestChecker < 0) {
			bestChecker = 0;
			// Go: `for checkerIndex, checkerWeight := range
			// checkerWeights[1:]` picks the least-loaded checker among
			// indices 1..n-1 (the +1 maps back to the original slice).
			for (int checkerIndex = 1; checkerIndex < checkerCount;
			     checkerIndex++) {
				if (checkerWeights[checkerIndex] <
				    checkerWeights[bestChecker]) {
					bestChecker = checkerIndex;
				}
			}
		}
		associations[fileIndex] = bestChecker;
		checkerWeights[bestChecker] += fileWeights[fileIndex];
	}
	return associations;
}

std::vector<int> getCheckerAssociationOrder(
	const std::vector<int>& fileWeights,
	const std::vector<bool>& isDeclarationFile,
	bool prioritizeSourceFiles) {
	if (!prioritizeSourceFiles) {
		return {};
	}
	std::vector<int> fileOrder(fileWeights.size());
	std::iota(fileOrder.begin(), fileOrder.end(), 0);
	std::sort(fileOrder.begin(), fileOrder.end(), [&](int left, int right) {
		if (isDeclarationFile[left] != isDeclarationFile[right]) {
			return !isDeclarationFile[left];
		}
		if (fileWeights[left] != fileWeights[right]) {
			return fileWeights[left] > fileWeights[right];
		}
		return left < right;
	});
	return fileOrder;
}

int getCheckerAssociationBaseWeight(int nodeCount, int textLength) {
	return std::max(nodeCount + textLength / checkerAssociationTextWeightDivisor,
	                1);
}

bool shouldPrioritizeSourceFiles(int totalWeight, int declarationWeight,
	                             int checkerCount) {
	return declarationWeight * checkerCount * 2 <= totalWeight;
}

std::vector<int> getCheckerAssociationWeights(
	const std::vector<int>& baseWeights,
	const std::vector<int>& importCounts) {
	int totalBaseWeight = 0;
	int totalImports = 0;
	for (size_t i = 0; i < baseWeights.size(); i++) {
		totalBaseWeight += baseWeights[i];
		totalImports += importCounts[i];
	}
	int importWeight = 0;
	if (totalImports > 0) {
		importWeight = std::max(totalBaseWeight / totalImports, 1);
	}
	std::vector<int> fileWeights(baseWeights.size());
	for (size_t i = 0; i < baseWeights.size(); i++) {
		fileWeights[i] = baseWeights[i] + importCounts[i] * importWeight;
	}
	return fileWeights;
}

checkerPool* newCheckerPool(SimpleProgram* program) {
	return newCheckerPoolWithTracing(program, nullptr);
}

checkerPool* newCheckerPoolWithTracing(SimpleProgram* program,
                                       tsc::tracing::Tracing* tr) {
	int checkerCount = 4;
	if (program->SingleThreaded()) {
		checkerCount = 1;
	} else if (const int* c = program->Options()->Checkers; c != nullptr) {
		checkerCount = *c;
	}

	checkerCount = std::max(
	    std::min({checkerCount, static_cast<int>(program->files.size()), 256}),
	    1);

	auto* pool = new checkerPool;
	pool->program = program;
	pool->checkers.resize(checkerCount);
	pool->locks.resize(checkerCount);
	pool->tracing = tr;

	return pool;
}

// GetChecker implements CheckerPool. When file is non-nil, returns the checker
// associated with that file; otherwise returns the first checker.
std::pair<checker::Checker*, std::function<void()>> checkerPool::GetChecker(
	const gostd::Context& ctx, SourceFile* file) {
	if (file != nullptr) {
		return getCheckerForFileExclusive(ctx, file);
	}
	createCheckers();
	checker::Checker* c = checkers[0].get();
	locks[0]->lock();
	return {c, onceFunc([this] { locks[0]->unlock(); })};
}

// getCheckerForFileNonExclusive returns the checker for the given file without locking.
// This is only safe when the caller guarantees no concurrent access to the same checker,
// e.g. for read-only operations like obtaining an emit resolver.
std::pair<checker::Checker*, std::function<void()>>
checkerPool::getCheckerForFileNonExclusive(SourceFile* file) {
	createCheckers();
	checker::Checker* c = nullptr;
	if (auto it = fileAssociations.find(file); it != fileAssociations.end()) {
		c = it->second;
	}
	return {c, &noop};
}

std::pair<checker::Checker*, std::function<void()>>
checkerPool::getCheckerForFileExclusive(const gostd::Context& ctx,
	                                    SourceFile* file) {
	(void)ctx;
	createCheckers();
	checker::Checker* c = nullptr;
	if (auto it = fileAssociations.find(file); it != fileAssociations.end()) {
		c = it->second;
	}
	int idx = -1;
	for (int i = 0; i < static_cast<int>(checkers.size()); i++) {
		if (checkers[i].get() == c) {
			idx = i;
			break;
		}
	}
	if (idx < 0) {
		TSC_UNREACHABLE("checkerPool: file has no associated checker");
	}
	locks[idx]->lock();
	return {c, onceFunc([this, idx] { locks[idx]->unlock(); })};
}

// getCheckerNonExclusive returns the first checker without locking.
std::pair<checker::Checker*, std::function<void()>>
checkerPool::getCheckerNonExclusive() {
	createCheckers();
	return {checkers[0].get(), &noop};
}

void checkerPool::createCheckers() {
	createCheckersOnce.run([this] {
		int checkerCount = static_cast<int>(checkers.size());
		std::unique_ptr<workGroup> wg(
		    newWorkGroup(program->SingleThreaded()));
		for (int i = 0; i < checkerCount; i++) {
			wg->Queue([this, i] {
				auto c = std::make_unique<checker::Checker>();
				if (tracing != nullptr) {
					// checker.go:916 — the tracer must be set before the
					// intrinsic types are created; otherwise types 1..N
					// would never be RecordType'd.
					c->tracer = checker::newTracer(tracing, i);
				}
				c->init(program);
				checkers[i] = std::move(c);
				locks[i] = std::make_unique<std::mutex>();
			});
		}

		wg->RunAndWait();

		std::vector<int> associations(program->files.size(), 0);
		if (checkerCount > 1) {
			size_t fileCount = program->files.size();
			std::vector<int> baseWeights(fileCount);
			std::vector<int> importCounts(fileCount);
			std::vector<bool> isDeclarationFile(fileCount);
			int totalBaseWeight = 0;
			int declarationBaseWeight = 0;
			for (size_t i = 0; i < fileCount; i++) {
				SourceFile* file = program->files[i];
				int baseWeight = getCheckerAssociationBaseWeight(
				    file->NodeCount, static_cast<int>(file->text.size()));
				totalBaseWeight += baseWeight;
				if (file->IsDeclarationFile) {
					declarationBaseWeight += baseWeight;
				}
				baseWeights[i] = baseWeight;
				importCounts[i] = static_cast<int>(file->imports.size());
				isDeclarationFile[i] = file->IsDeclarationFile;
			}
			checkerAssociationPolicy policy = getCheckerAssociationPolicy(
			    totalBaseWeight, declarationBaseWeight, checkerCount);
			if (policy.sourceFileWeightMultiplier != 1) {
				// Apply this before import normalization. The policy intentionally
				// increases both source-file work and the normalized import unit.
				for (size_t i = 0; i < fileCount; i++) {
					if (!isDeclarationFile[i]) {
						baseWeights[i] *= policy.sourceFileWeightMultiplier;
					}
				}
			}
			std::vector<int> fileWeights =
			    getCheckerAssociationWeights(baseWeights, importCounts);
			std::vector<std::vector<int>> adjacentFiles =
			    getImportAdjacency();
			std::vector<int> fileOrder = getCheckerAssociationOrder(
			    fileWeights, isDeclarationFile, policy.prioritizeSourceFiles);
			associations = getCheckerAssociationsInOrder(
			    fileWeights, adjacentFiles, fileOrder, checkerCount,
			    policy.balancePenaltyMultiplier);
		}
		fileAssociations.reserve(program->files.size());
		for (size_t i = 0; i < program->files.size(); i++) {
			fileAssociations[program->files[i]] =
			    checkers[associations[i]].get();
		}
	});
}

std::vector<std::vector<int>> checkerPool::getImportAdjacency() {
	std::unordered_map<SourceFile*, int> fileIndices;
	fileIndices.reserve(program->files.size());
	for (int i = 0; i < static_cast<int>(program->files.size()); i++) {
		fileIndices[program->files[i]] = i;
	}
	std::vector<std::vector<int>> adjacentFiles(program->files.size());
	for (int fileIndex = 0;
	     fileIndex < static_cast<int>(program->files.size()); fileIndex++) {
		SourceFile* file = program->files[fileIndex];
		auto resolvedIt = program->resolvedModules.find(file->Path());
		if (resolvedIt == program->resolvedModules.end()) {
			continue;
		}
		for (auto& [key, resolved] : resolvedIt->second) {
			(void)key;
			if (resolved == nullptr || !resolved->IsResolved()) {
				continue;
			}
			SourceFile* importedFile =
			    program->GetSourceFileForResolvedModule(
			        resolved->ResolvedFileName);
			auto indexIt = fileIndices.find(importedFile);
			if (indexIt == fileIndices.end() ||
			    indexIt->second == fileIndex) {
				continue;
			}
			int importedIndex = indexIt->second;
			adjacentFiles[fileIndex].push_back(importedIndex);
			adjacentFiles[importedIndex].push_back(fileIndex);
		}
	}
	return adjacentFiles;
}

void checkerPool::forEachCheckerParallel(
	const std::function<void(int, checker::Checker*)>& cb) {
	createCheckers();
	std::unique_ptr<workGroup> wg(
	    newWorkGroup(program->SingleThreaded()));
	for (int idx = 0; idx < static_cast<int>(checkers.size()); idx++) {
		wg->Queue([this, idx, &cb] {
			std::lock_guard<std::mutex> lock(*locks[idx]);
			cb(idx, checkers[idx].get());
		});
	}
	wg->RunAndWait();
}

std::vector<Diagnostic*> checkerPool::GetGlobalDiagnostics() {
	createCheckers();
	std::vector<std::vector<Diagnostic*>> globalDiagnostics(
	    checkers.size());
	forEachCheckerParallel(
	    [&globalDiagnostics](int idx, checker::Checker* c) {
		    globalDiagnostics[idx] = c->GetGlobalDiagnostics();
	    });
	std::vector<Diagnostic*> all;
	for (auto& diags : globalDiagnostics) {
		all.insert(all.end(), diags.begin(), diags.end());
	}
	return sortAndDeduplicateDiagnostics(std::move(all));
}

void checkerPool::forEachCheckerGroupDo(
	const gostd::Context& ctx, const std::vector<SourceFile*>& files,
	bool singleThreaded,
	const std::function<void(checker::Checker*, int, SourceFile*)>& cb) {
	(void)ctx;
	createCheckers();

	int checkerCount = static_cast<int>(checkers.size());
	std::unique_ptr<workGroup> wg(newWorkGroup(singleThreaded));
	for (int checkerIdx = 0; checkerIdx < checkerCount; checkerIdx++) {
		wg->Queue([this, checkerIdx, &files, &cb] {
			std::lock_guard<std::mutex> lock(*locks[checkerIdx]);
			checker::Checker* c = checkers[checkerIdx].get();
			for (int i = 0; i < static_cast<int>(files.size()); i++) {
				auto it = fileAssociations.find(files[i]);
				if (it != fileAssociations.end() && it->second == c) {
					cb(c, i, files[i]);
				}
			}
		});
	}
	wg->RunAndWait();
}

void noop() {}

} // namespace tsc::compiler
