// Port of tsc/internal/transformers/tstransforms/utilities.go.
#include "internal/transformers/tstransforms/tstransforms.h"

namespace tsc::transformers::tstransforms {

// constantExpression — utilities.go:9 (string case)
Node* constantExpression(std::string_view value,
                         printer::NodeFactory* factory) {
	return factory->newStringLiteral(std::string(value), TokenFlagsNone);
}

// constantExpression — utilities.go:9 (jsnum.Number case)
Node* constantExpression(Number value, printer::NodeFactory* factory) {
	if (value.isInf()) {
		if (Number(0) < value) {
			return factory->newIdentifier("Infinity");
		}
		return factory->newPrefixUnaryExpression(
			Kind::MinusToken, factory->newIdentifier("Infinity"));
	}
	if (value.isNaN()) {
		return factory->newIdentifier("NaN");
	}
	if (value < Number(0)) {
		return factory->newPrefixUnaryExpression(
			Kind::MinusToken, constantExpression(-value, factory));
	}
	return factory->newNumericLiteral(value.string(), TokenFlagsNone);
}

}  // namespace tsc::transformers::tstransforms
