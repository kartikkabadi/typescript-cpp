// Port of tsc/internal/binder — bindSourceFile entry point.
#pragma once

#include <vector>

namespace tsc {

struct SourceFile;
struct Node;

void bindSourceFile(SourceFile* file);
Node* findUseStrictPrologue(SourceFile* sourceFile,
                            const std::vector<Node*>& statements);

} // namespace tsc
