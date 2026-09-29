// precedence.go — port to C++
#pragma once
#include "internal/ast/ast.h"

namespace tsc {

// OperatorPrecedence — plain int so Go-style comparisons port 1:1.
using OperatorPrecedence = int;
inline constexpr OperatorPrecedence OperatorPrecedenceComma = 0;
inline constexpr OperatorPrecedence OperatorPrecedenceSpread = 1;
inline constexpr OperatorPrecedence OperatorPrecedenceYield = 2;
inline constexpr OperatorPrecedence OperatorPrecedenceAssignment = 3;
inline constexpr OperatorPrecedence OperatorPrecedenceConditional = 4;
inline constexpr OperatorPrecedence OperatorPrecedenceLogicalOR = 5;
inline constexpr OperatorPrecedence OperatorPrecedenceLogicalAND = 6;
inline constexpr OperatorPrecedence OperatorPrecedenceBitwiseOR = 7;
inline constexpr OperatorPrecedence OperatorPrecedenceBitwiseXOR = 8;
inline constexpr OperatorPrecedence OperatorPrecedenceBitwiseAND = 9;
inline constexpr OperatorPrecedence OperatorPrecedenceEquality = 10;
inline constexpr OperatorPrecedence OperatorPrecedenceRelational = 11;
inline constexpr OperatorPrecedence OperatorPrecedenceShift = 12;
inline constexpr OperatorPrecedence OperatorPrecedenceAdditive = 13;
inline constexpr OperatorPrecedence OperatorPrecedenceMultiplicative = 14;
inline constexpr OperatorPrecedence OperatorPrecedenceExponentiation = 15;
inline constexpr OperatorPrecedence OperatorPrecedenceUnary = 16;
inline constexpr OperatorPrecedence OperatorPrecedenceUpdate = 17;
inline constexpr OperatorPrecedence OperatorPrecedenceLeftHandSide = 18;
inline constexpr OperatorPrecedence OperatorPrecedenceOptionalChain = 19;
inline constexpr OperatorPrecedence OperatorPrecedenceMember = 20;
inline constexpr OperatorPrecedence OperatorPrecedencePrimary = 21;
inline constexpr OperatorPrecedence OperatorPrecedenceParentheses = 22;
inline constexpr OperatorPrecedence OperatorPrecedenceLowest = OperatorPrecedenceComma;
inline constexpr OperatorPrecedence OperatorPrecedenceHighest = OperatorPrecedenceParentheses;
inline constexpr OperatorPrecedence OperatorPrecedenceDisallowComma = OperatorPrecedenceYield;
inline constexpr OperatorPrecedence OperatorPrecedenceCoalesce = OperatorPrecedenceLogicalOR;
inline constexpr OperatorPrecedence OperatorPrecedenceInvalid = -1;

using OperatorPrecedenceFlags = int;
inline constexpr OperatorPrecedenceFlags OperatorPrecedenceFlagsNone = 0;
inline constexpr OperatorPrecedenceFlags OperatorPrecedenceFlagsNewWithoutArguments = 1 << 0;
inline constexpr OperatorPrecedenceFlags OperatorPrecedenceFlagsOptionalChain = 1 << 1;

Kind getOperatorOfExpression(Node* expression);
OperatorPrecedence getExpressionPrecedence(Node* expression);
OperatorPrecedence getOperatorPrecedence(Kind nodeKind, Kind operatorKind,
                                       OperatorPrecedenceFlags flags);
OperatorPrecedence getBinaryOperatorPrecedence(Kind operatorKind);
Node* getLeftmostExpression(Node* node, bool stopAtCallExpressions);

using TypePrecedence = int32_t;
inline constexpr TypePrecedence TypePrecedenceConditional = 0;
inline constexpr TypePrecedence TypePrecedenceJSDoc = 1;
inline constexpr TypePrecedence TypePrecedenceFunction = 2;
inline constexpr TypePrecedence TypePrecedenceUnion = 3;
inline constexpr TypePrecedence TypePrecedenceIntersection = 4;
inline constexpr TypePrecedence TypePrecedenceTypeOperator = 5;
inline constexpr TypePrecedence TypePrecedencePostfix = 6;
inline constexpr TypePrecedence TypePrecedenceNonArray = 7;
inline constexpr TypePrecedence TypePrecedenceLowest = TypePrecedenceConditional;
inline constexpr TypePrecedence TypePrecedenceHighest = TypePrecedenceNonArray;

TypePrecedence getTypeNodePrecedence(Node* n);

bool isOptionalChain(Node* node);

} // namespace tsc
