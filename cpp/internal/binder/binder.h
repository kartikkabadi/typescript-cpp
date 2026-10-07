// Port of tsc/internal/binder — bindSourceFile entry point.
#pragma once

#include <span>
#include <vector>
#include "internal/ast/symbol.h"

namespace tsc {

struct SourceFile;
struct Node;

void bindSourceFile(SourceFile* file);
void setValueDeclaration(Symbol* symbol, Node* node);
bool isAssignmentDeclaration(Node* decl);
bool isEffectiveModuleDeclaration(Node* node);
Node* findUseStrictPrologue(SourceFile* sourceFile,
                            std::span<Node* const> statements);

// binder.go: ContainerFlags
using ContainerFlags = int32_t;
inline constexpr ContainerFlags ContainerFlagsNone = 0;
inline constexpr ContainerFlags ContainerFlagsIsContainer = 1 << 0;
inline constexpr ContainerFlags ContainerFlagsIsBlockScopedContainer = 1 << 1;
inline constexpr ContainerFlags ContainerFlagsIsControlFlowContainer = 1 << 2;
inline constexpr ContainerFlags ContainerFlagsIsFunctionLike = 1 << 3;
inline constexpr ContainerFlags ContainerFlagsIsFunctionExpression = 1 << 4;
inline constexpr ContainerFlags ContainerFlagsHasLocals = 1 << 5;
inline constexpr ContainerFlags ContainerFlagsIsInterface = 1 << 6;
inline constexpr ContainerFlags ContainerFlagsIsObjectLiteralOrClassExpressionMethodOrAccessor = 1 << 7;
inline constexpr ContainerFlags ContainerFlagsIsThisContainer = 1 << 8;
inline constexpr ContainerFlags ContainerFlagsPropagatesThisKeyword = 1 << 9;

// binder.go: GetContainerFlags
ContainerFlags getContainerFlags(Node* node);

} // namespace tsc
