// Port of tsc/internal/transformers/jsxtransforms — the JSX factory
// transform (jsx.go). Namespace tsc::transformers::jsxtransforms.
#pragma once

#include "internal/transformers/transformers.h"

namespace tsc::transformers::jsxtransforms {

// NewJSXTransformer — jsx.go:32. Returned Transformer* ownership follows the
// TransformerFactory contract (caller wraps in unique_ptr at chain level).
Transformer* NewJSXTransformer(TransformOptions* opts);

}  // namespace tsc::transformers::jsxtransforms
