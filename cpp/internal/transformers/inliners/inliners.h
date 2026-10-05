// Port of tsc/internal/transformers/inliners — constenum.go.
#pragma once

#include "internal/transformers/transformers.h"

namespace tsc::transformers::inliners {

// NewConstEnumInliningTransformer — constenum.go:22
Transformer* newConstEnumInliningTransformer(TransformOptions* opt);

}  // namespace tsc::transformers::inliners
