// Port of tsc/internal/evaluator/evaluator.go.

#include "internal/evaluator/evaluator.h"

#include <sstream>

namespace tsc {

static EvalResult evaluateTemplateExpression(Node* expr, Node* location,
											 const Evaluator& evaluate) {
	std::ostringstream sb;
	sb << expr->as<TemplateExpression>()->Head->text();
	bool resolvedOtherFiles = false;
	bool hasExternalReferences = false;
	for (Node* span : expr->as<TemplateExpression>()->TemplateSpans->nodes) {
		EvalResult spanResult = evaluate(span->as<TemplateSpan>()->Expression, location);
		if (std::holds_alternative<std::monostate>(spanResult.Value)) {
			return EvalResult{std::monostate{}, true, false, false};
		}
		sb << evalAnyToString(spanResult.Value);
		sb << span->as<TemplateSpan>()->Literal->text();
		resolvedOtherFiles = resolvedOtherFiles || spanResult.ResolvedOtherFiles;
		hasExternalReferences = hasExternalReferences || spanResult.HasExternalReferences;
	}
	return EvalResult{sb.str(), true, resolvedOtherFiles, hasExternalReferences};
}

Evaluator newEvaluator(const Evaluator& evaluateEntity,
					   OuterExpressionKinds outerExpressionsToSkip) {
	// Go: `evaluate` is a recursive closure — the closure variable lives on the
	// heap, so self-copies resolve correctly. A C++ [&evaluate] capture of the
	// local would dangle once this function returns, so the recursion target is
	// a heap Evaluator*; `auto& evaluate = *self` keeps the body identical.
	auto* self = new Evaluator();
	Evaluator evaluate;
	evaluate = [evaluateEntity, outerExpressionsToSkip,
				self](Node* expr, Node* location) -> EvalResult {
		Evaluator& evaluate = *self;
		bool isSyntacticallyString = false;
		bool resolvedOtherFiles = false;
		bool hasExternalReferences = false;
		// It's unclear when/whether we should consider skipping other kinds of outer
		// expressions. Type assertions intentionally break evaluation when evaluating
		// literal types, such as:
		//     type T = `one ${"two" as any} three`; // string
		// But it's less clear whether such an assertion should break enum member
		// evaluation:
		//     enum E {
		//       A = "one" as any
		//     }
		// SatisfiesExpressions and non-null assertions seem to have even less reason
		// to break emitting enum members as literals. However, these expressions also
		// break Babel's evaluation (but not esbuild's), and the isolatedModules errors
		// we give depend on our evaluation results, so we're currently being
		// conservative so as to issue errors on code that might break Babel.
		expr = skipOuterExpressions(expr, outerExpressionsToSkip | OEKParentheses);
		switch (expr->kind) {
		case Kind::PrefixUnaryExpression: {
			EvalResult result =
				evaluate(expr->as<PrefixUnaryExpression>()->Operand, location);
			resolvedOtherFiles = result.ResolvedOtherFiles;
			hasExternalReferences = result.HasExternalReferences;
			if (auto* value = std::get_if<Number>(&result.Value)) {
				switch (expr->as<PrefixUnaryExpression>()->Operator) {
				case Kind::PlusToken:
					return EvalResult{*value, isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::MinusToken:
					return EvalResult{-*value, isSyntacticallyString,
									  resolvedOtherFiles, hasExternalReferences};
				case Kind::TildeToken:
					return EvalResult{value->bitwiseNOT(), isSyntacticallyString,
									  resolvedOtherFiles, hasExternalReferences};
				default:
					break;
				}
			}
			break;
		}
		case Kind::BinaryExpression: {
			EvalResult left = evaluate(expr->as<BinaryExpression>()->Left, location);
			EvalResult right = evaluate(expr->as<BinaryExpression>()->Right, location);
			Kind op = expr->as<BinaryExpression>()->OperatorToken->kind;
			isSyntacticallyString =
				(left.IsSyntacticallyString || right.IsSyntacticallyString) &&
				op == Kind::PlusToken;
			resolvedOtherFiles =
				left.ResolvedOtherFiles || right.ResolvedOtherFiles;
			hasExternalReferences =
				left.HasExternalReferences || right.HasExternalReferences;
			auto* leftNum = std::get_if<Number>(&left.Value);
			auto* rightNum = std::get_if<Number>(&right.Value);
			if (leftNum != nullptr && rightNum != nullptr) {
				switch (op) {
				case Kind::BarToken:
					return EvalResult{leftNum->bitwiseOR(*rightNum),
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::AmpersandToken:
					return EvalResult{leftNum->bitwiseAND(*rightNum),
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::GreaterThanGreaterThanToken:
					return EvalResult{leftNum->signedRightShift(*rightNum),
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::GreaterThanGreaterThanGreaterThanToken:
					return EvalResult{leftNum->unsignedRightShift(*rightNum),
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::LessThanLessThanToken:
					return EvalResult{leftNum->leftShift(*rightNum),
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::CaretToken:
					return EvalResult{leftNum->bitwiseXOR(*rightNum),
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::AsteriskToken:
					return EvalResult{*leftNum * *rightNum,
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::SlashToken:
					return EvalResult{*leftNum / *rightNum,
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::PlusToken:
					return EvalResult{*leftNum + *rightNum,
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::MinusToken:
					return EvalResult{*leftNum - *rightNum,
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::PercentToken:
					return EvalResult{leftNum->remainder(*rightNum),
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				case Kind::AsteriskAsteriskToken:
					return EvalResult{leftNum->exponentiate(*rightNum),
									  isSyntacticallyString, resolvedOtherFiles,
									  hasExternalReferences};
				default:
					break;
				}
			}
			auto* leftStr = std::get_if<std::string>(&left.Value);
			auto* rightStr = std::get_if<std::string>(&right.Value);
			if ((leftStr != nullptr || leftNum != nullptr) &&
				(rightStr != nullptr || rightNum != nullptr) && op == Kind::PlusToken) {
				std::string ls = leftStr != nullptr ? *leftStr : leftNum->string();
				std::string rs = rightStr != nullptr ? *rightStr : rightNum->string();
				return EvalResult{ls + rs, isSyntacticallyString, resolvedOtherFiles,
								  hasExternalReferences};
			}
			break;
		}
		case Kind::StringLiteral:
		case Kind::NoSubstitutionTemplateLiteral:
			return EvalResult{expr->text(), true, false, false};
		case Kind::TemplateExpression:
			return evaluateTemplateExpression(expr, location, evaluate);
		case Kind::NumericLiteral:
			return EvalResult{numberFromString(expr->text()), false, false, false};
		case Kind::Identifier:
			return evaluateEntity(expr, location);
		case Kind::ElementAccessExpression:
		case Kind::PropertyAccessExpression:
			if (isEntityNameExpression(expr->expression())) {
				return evaluateEntity(expr, location);
			}
			break;
		default:
			break;
		}
		return EvalResult{std::monostate{}, isSyntacticallyString, resolvedOtherFiles,
						  hasExternalReferences};
	};
	*self = evaluate;
	return evaluate;
}

std::string evalAnyToString(
	const std::variant<std::monostate, std::string, Number, bool, PseudoBigInt>& v) {
	if (auto* s = std::get_if<std::string>(&v)) {
		return *s;
	}
	if (auto* n = std::get_if<Number>(&v)) {
		return n->string();
	}
	if (auto* b = std::get_if<bool>(&v)) {
		return *b ? "true" : "false";
	}
	if (auto* p = std::get_if<PseudoBigInt>(&v)) {
		return p->string();
	}
	TSC_UNREACHABLE("Unhandled case in AnyToString");
}

bool evalIsTruthy(
	const std::variant<std::monostate, std::string, Number, bool, PseudoBigInt>& v) {
	if (auto* s = std::get_if<std::string>(&v)) {
		return !s->empty();
	}
	if (auto* n = std::get_if<Number>(&v)) {
		return *n != 0 && !n->isNaN();
	}
	if (auto* b = std::get_if<bool>(&v)) {
		return *b;
	}
	if (auto* p = std::get_if<PseudoBigInt>(&v)) {
		return !(*p == PseudoBigInt{});
	}
	TSC_UNREACHABLE("Unhandled case in IsTruthy");
}

}  // namespace tsc
