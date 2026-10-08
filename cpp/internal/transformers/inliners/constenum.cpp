// Port of tsc/internal/transformers/inliners/constenum.go.
#include "internal/transformers/inliners/inliners.h"

#include "internal/checker/checker.h" // checker::EmitResolver, checker::LiteralValue
#include "internal/scanner/scanner.h"

namespace tsc::transformers::inliners {
namespace {

struct ConstEnumInliningTransformer : Transformer {
	const CompilerOptions* compilerOptions = nullptr;
	SourceFile* currentSourceFile = nullptr;
	checker::EmitResolver* emitResolver = nullptr;

	// NewConstEnumInliningTransformer — constenum.go:22
	static Transformer* create(TransformOptions* opt) {
		const CompilerOptions* compilerOptions = opt->CompilerOptions;
		printer::EmitContext* emitContext = opt->Context;
		if (compilerOptions->GetIsolatedModules()) {
			TSC_UNREACHABLE(
				"const enums are not inlined under isolated modules");
		}
		auto* tx = new ConstEnumInliningTransformer;
		tx->compilerOptions = compilerOptions;
		tx->emitResolver = opt->EmitResolver;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, emitContext);
	}

	// visit — constenum.go:32
	Node* visit(Node* node) {
		switch (node->kind) {
		case Kind::PropertyAccessExpression:
		case Kind::ElementAccessExpression: {
			Node* parse = emitContext()->parseNode(node);
			if (parse == nullptr) {
				return visitor()->visitEachChild(node);
			}
			checker::LiteralValue value =
				emitResolver->GetConstantValue(parse);
			if (!std::holds_alternative<std::monostate>(value)) {
				Node* replacement = nullptr;
				if (const Number* v = std::get_if<Number>(&value)) {
					if (v->isInf()) {
						if (v->abs() == *v) {
							replacement = factory()->newIdentifier(
								"Infinity");
						} else {
							replacement =
								factory()->newPrefixUnaryExpression(
									Kind::MinusToken,
									factory()->newIdentifier(
										"Infinity"));
						}
					} else if (v->isNaN()) {
						replacement = factory()->newIdentifier("NaN");
					} else if (v->abs() == *v) {
						replacement = factory()->newNumericLiteral(
							v->string(), TokenFlagsNone);
					} else {
						replacement =
							factory()->newPrefixUnaryExpression(
								Kind::MinusToken,
								factory()->newNumericLiteral(
									v->abs().string(),
									TokenFlagsNone));
					}
				} else if (const std::string* v =
							   std::get_if<std::string>(&value)) {
					replacement =
						factory()->newStringLiteral(*v, TokenFlagsNone);
				} else if (const PseudoBigInt* v =
							   std::get_if<PseudoBigInt>(
								   &value)) { // technically not supported
											  // by strada, and issues a
											  // checker error, handled here
											  // for completeness
					if (*v == PseudoBigInt{}) {
						replacement = factory()->newBigIntLiteral(
							"0", TokenFlagsNone);
					} else if (!v->negative) {
						replacement = factory()->newBigIntLiteral(
							v->base10Value, TokenFlagsNone);
					} else {
						replacement =
							factory()->newPrefixUnaryExpression(
								Kind::MinusToken,
								factory()->newBigIntLiteral(
									v->base10Value, TokenFlagsNone));
					}
				}

				if (tristateIsFalseOrUnknown(
						compilerOptions->RemoveComments)) {
					Node* original = emitContext()->mostOriginal(node);
					if (original != nullptr &&
						!nodeIsSynthesized(original)) {
						std::string originalText =
							getTextOfNode(original);
						std::string escapedText =
							safeMultiLineComment(originalText);
						emitContext()->addSyntheticTrailingComment(
							replacement, Kind::MultiLineCommentTrivia,
							escapedText, false);
					}
				}
				return replacement;
			}
			return visitor()->visitEachChild(node);
		}
		default:
			break;
		}
		return visitor()->visitEachChild(node);
	}

	// safeMultiLineComment — constenum.go:86
	static std::string safeMultiLineComment(const std::string& textIn) {
		std::string b;
		b.reserve(textIn.size() + 2);
		std::string_view text = textIn;
		b.push_back(' ');
		for (;;) {
			size_t i = text.find("*/");
			if (i == std::string_view::npos) {
				break;
			}
			b.append(text.substr(0, i));
			b.append("*_/");
			text = text.substr(i + 2);
		}
		b.append(text);
		b.push_back(' ');
		return b;
	}
};

} // namespace

Transformer* NewConstEnumInliningTransformer(TransformOptions* opt) {
	return ConstEnumInliningTransformer::create(opt);
}

}  // namespace tsc::transformers::inliners
