#pragma once

// Port of tsc/internal/evaluator/evaluator.go — constant expression evaluation
// used by enum member values and other compile-time constant folding.

#include <functional>
#include <string>
#include <variant>

#include "internal/ast/ast.h"
#include "internal/jsnum/jsnum.h"

namespace tsc {

struct EvalResult {
	std::variant<std::monostate, std::string, Number, bool, PseudoBigInt> Value;
	bool IsSyntacticallyString{};
	bool ResolvedOtherFiles{};
	bool HasExternalReferences{};
};

inline EvalResult newEvalResult(
	std::variant<std::monostate, std::string, Number, bool, PseudoBigInt> value,
	bool isSyntacticallyString, bool resolvedOtherFiles, bool hasExternalReferences) {
	return EvalResult{std::move(value), isSyntacticallyString, resolvedOtherFiles,
				  hasExternalReferences};
}

using Evaluator = std::function<EvalResult(Node* expr, Node* location)>;

std::string evalAnyToString(
	const std::variant<std::monostate, std::string, Number, bool, PseudoBigInt>& v);

bool evalIsTruthy(
	const std::variant<std::monostate, std::string, Number, bool, PseudoBigInt>& v);

Evaluator newEvaluator(const Evaluator& evaluateEntity,
					   OuterExpressionKinds outerExpressionsToSkip);

}  // namespace tsc
