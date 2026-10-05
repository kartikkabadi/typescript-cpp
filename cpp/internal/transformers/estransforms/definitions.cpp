// Port of tsc/internal/transformers/estransforms/definitions.go —
// the ES transformer chain table and GetESTransformer.
#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {
namespace {

// definitions.go:8-22 — package-level transformer chains.
TransformerFactory esDecoratorAndClassFields =
	chain({newESDecoratorTransformer, newClassFieldsTransformer});
TransformerFactory NewESNextTransformer =
	chain({newUsingDeclarationTransformer, esDecoratorAndClassFields});
// 2026: no new downlevel syntax
// 2025: only module system syntax (import attributes, json modules),
//       untransformed regex modifiers
// 2024: no new downlevel syntax
// 2023: no new downlevel syntax
// 2022: class static blocks and class fields are handled by
//       newClassFieldsTransformer
TransformerFactory NewES2021Transformer =
	chain({NewESNextTransformer, newLogicalAssignmentTransformer});
TransformerFactory NewES2020Transformer =
	chain({NewES2021Transformer, newNullishCoalescingTransformer,
		   newOptionalChainTransformer});
TransformerFactory NewES2019Transformer =
	chain({NewES2020Transformer, newOptionalCatchTransformer});
TransformerFactory NewES2018Transformer =
	chain({NewES2019Transformer, newObjectRestSpreadTransformer,
		   newforawaitTransformer,
		   newTaggedTemplateLiftRestrictionTransformer});
TransformerFactory NewES2017Transformer =
	chain({NewES2018Transformer, newAsyncTransformer});
TransformerFactory NewES2016Transformer =
	chain({NewES2017Transformer, newExponentiationTransformer});

}  // namespace

// GetESTransformer — definitions.go:24
Transformer* GetESTransformer(TransformOptions* opts) {
	const CompilerOptions* options = opts->CompilerOptions;
	switch (options->GetEmitScriptTarget()) {
	case ScriptTarget::ESNext:
		return esDecoratorAndClassFields(opts);
	case ScriptTarget::ES2026:
	case ScriptTarget::ES2025:
	case ScriptTarget::ES2024:
	case ScriptTarget::ES2023:
	case ScriptTarget::ES2022:
	case ScriptTarget::ES2021:
		return NewESNextTransformer(opts);
	case ScriptTarget::ES2020:
		return NewES2021Transformer(opts);
	case ScriptTarget::ES2019:
		return NewES2020Transformer(opts);
	case ScriptTarget::ES2018:
		return NewES2019Transformer(opts);
	case ScriptTarget::ES2017:
		return NewES2018Transformer(opts);
	case ScriptTarget::ES2016:
		return NewES2017Transformer(opts);
	default: // other, older, option, transform maximally
		return NewES2016Transformer(opts);
	}
}

// === dep stubs — removed when owner slice lands ===

Transformer* newClassFieldsTransformer(TransformOptions* opt) {
	TSC_UNREACHABLE("newClassFieldsTransformer — owned by classfields slice");
}

Transformer* newUsingDeclarationTransformer(TransformOptions* opt) {
	TSC_UNREACHABLE("newUsingDeclarationTransformer — owned by using slice");
}

Transformer* newforawaitTransformer(TransformOptions* opt) {
	TSC_UNREACHABLE("newforawaitTransformer — owned by forawait slice");
}

Transformer* newAsyncTransformer(TransformOptions* opt) {
	TSC_UNREACHABLE("newAsyncTransformer — owned by async slice");
}

}  // namespace tsc::transformers::estransforms
