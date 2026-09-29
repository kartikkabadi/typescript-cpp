// Port of tsc/internal/binder — bindSourceFile entry point.
#pragma once

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
                            const std::vector<Node*>& statements);

} // namespace tsc
