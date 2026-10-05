// Port of tsc/internal/transformers/tstransforms — package-level helpers.
#pragma once

#include <string_view>

#include "internal/jsnum/jsnum.h"
#include "internal/transformers/transformers.h"

namespace tsc::transformers::tstransforms {

// constantExpression — utilities.go:9. Go takes `any` (string | jsnum.Number);
// two overloads here.
Node* constantExpression(std::string_view value,
                         printer::NodeFactory* factory);
Node* constantExpression(Number value, printer::NodeFactory* factory);

}  // namespace tsc::transformers::tstransforms
