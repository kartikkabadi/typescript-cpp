// Port of tsc/internal/transformers/estransforms/optionalchain.go.
#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {
namespace {

// flattenResult — optionalchain.go:126
struct flattenResult {
	Node* expression = nullptr;
	std::vector<Node*> chain;
};

// isNonNullChain — optionalchain.go:131
bool isNonNullChain(Node* node) {
	return isNonNullExpression(node) &&
		   (node->flags & NodeFlagsOptionalChain) != 0;
}

// flattenChain — optionalchain.go:135
flattenResult flattenChain(Node* chain) {
	TSC_ASSERT(!isNonNullChain(chain), "");
	std::vector<Node*> links{chain};
	while (!isTaggedTemplateExpression(chain) &&
		   chain->questionDotToken() == nullptr) {
		chain = skipPartiallyEmittedExpressions(chain->expression());
		TSC_ASSERT(!isNonNullChain(chain), "");
		links.insert(links.begin(), chain);
	}
	return flattenResult{chain->expression(), links};
}

// isCallChain — optionalchain.go:146
bool isCallChain(Node* node) {
	return isCallExpression(node) &&
		   (node->flags & NodeFlagsOptionalChain) != 0;
}

struct OptionalChainTransformer : Transformer {

	// visit — optionalchain.go:14
	Node* visit(Node* node) {
		if ((node->subtreeFacts() & SubtreeContainsOptionalChaining) == 0) {
			return node;
		}
		switch (node->kind) {
		case Kind::CallExpression:
			return visitCallExpression(node->as<CallExpression>(), false);
		case Kind::PropertyAccessExpression:
		case Kind::ElementAccessExpression:
			if ((node->flags & NodeFlagsOptionalChain) != 0) {
				return visitOptionalExpression(node, false, false);
			}
			return visitor()->visitEachChild(node);
		case Kind::DeleteExpression:
			return visitDeleteExpression(node->as<DeleteExpression>());
		default:
			return visitor()->visitEachChild(node);
		}
	}

	// visitCallExpression — optionalchain.go:34
	Node* visitCallExpression(CallExpression* node, bool captureThisArg) {
		if ((node->flags & NodeFlagsOptionalChain) != 0) {
			// If `node` is an optional chain, then it is the outermost chain
			// of an optional expression.
			return visitOptionalExpression(node->asNode(), captureThisArg,
										   false);
		}
		if (isParenthesizedExpression(node->Expression)) {
			Node* unwrapped = skipParentheses(node->Expression);
			if ((unwrapped->flags & NodeFlagsOptionalChain) != 0) {
				// capture thisArg for calls of parenthesized optional chains
				// like `(foo?.bar)()`
				Node* expression = visitParenthesizedExpression(
					node->Expression->as<ParenthesizedExpression>(), true,
					false);
				NodeList* args = visitor()->visitNodes(node->Arguments);
				if (isSyntheticReferenceExpression(expression)) {
					SyntheticReferenceExpression* synth =
						expression->as<SyntheticReferenceExpression>();
					Node* res = factory()->newFunctionCallCall(
						synth->Expression, synth->ThisArg, args->nodes);
					res->loc = node->loc;
					emitContext()->setOriginal(res, node->asNode());
					return res;
				}
				return factory()->updateCallExpression(
					node, expression, nullptr /*questionDotToken*/,
					nullptr /*typeArguments*/, args, node->flags);
			}
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// visitParenthesizedExpression — optionalchain.go:57
	Node* visitParenthesizedExpression(ParenthesizedExpression* node,
									   bool captureThisArg, bool isDelete) {
		Node* expr = visitNonOptionalExpression(node->Expression,
												captureThisArg, isDelete);
		if (isSyntheticReferenceExpression(expr)) {
			// `(a.b)` -> { expression `((_a = a).b)`, thisArg: `_a` }
			// `(a[b])` -> { expression `((_a = a)[b])`, thisArg: `_a` }
			SyntheticReferenceExpression* synth =
				expr->as<SyntheticReferenceExpression>();
			Node* res = factory()->newSyntheticReferenceExpression(
				factory()->updateParenthesizedExpression(node,
														 synth->Expression),
				synth->ThisArg);
			emitContext()->setOriginal(res, node->asNode());
			return res;
		}
		return factory()->updateParenthesizedExpression(node, expr);
	}

	// visitPropertyOrElementAccessExpression — optionalchain.go:70
	Node* visitPropertyOrElementAccessExpression(Node* node,
												 bool captureThisArg,
												 bool isDelete) {
		if ((node->flags & NodeFlagsOptionalChain) != 0) {
			// If `node` is an optional chain, then it is the outermost chain
			// of an optional expression.
			return visitOptionalExpression(node->asNode(), captureThisArg,
										   isDelete);
		}
		Node* expression = visitor()->visitNode(node->expression());
		TSC_ASSERT(expression == nullptr ||
					   !isSyntheticReferenceExpression(expression),
				   "");

		Node* thisArg = nullptr;
		if (captureThisArg) {
			if (!isSimpleCopiableExpression(expression)) {
				thisArg = factory()->newTempVariable();
				emitContext()->addVariableDeclaration(thisArg);
				expression = factory()->newAssignmentExpression(thisArg,
																expression);
			} else {
				thisArg = expression;
			}
		}

		if (node->kind == Kind::PropertyAccessExpression) {
			PropertyAccessExpression* p =
				node->as<PropertyAccessExpression>();
			expression = factory()->updatePropertyAccessExpression(
				p, expression, nullptr /*questionDotToken*/,
				visitor()->visitNode(p->name), p->flags);
		} else {
			ElementAccessExpression* p = node->as<ElementAccessExpression>();
			expression = factory()->updateElementAccessExpression(
				p, expression, nullptr,
				visitor()->visitNode(p->ArgumentExpression), p->flags);
		}

		if (thisArg != nullptr) {
			Node* res = factory()->newSyntheticReferenceExpression(expression,
																  thisArg);
			emitContext()->setOriginal(res, node->asNode());
			return res;
		}
		return expression;
	}

	// visitDeleteExpression — optionalchain.go:105
	Node* visitDeleteExpression(DeleteExpression* node) {
		Node* unwrapped = skipParentheses(node->Expression);
		if ((unwrapped->flags & NodeFlagsOptionalChain) != 0) {
			return visitNonOptionalExpression(node->Expression, false, true);
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// visitNonOptionalExpression — optionalchain.go:113
	Node* visitNonOptionalExpression(Node* node, bool captureThisArg,
									 bool isDelete) {
		switch (node->kind) {
		case Kind::ParenthesizedExpression:
			return visitParenthesizedExpression(
				node->as<ParenthesizedExpression>(), captureThisArg,
				isDelete);
		case Kind::ElementAccessExpression:
		case Kind::PropertyAccessExpression:
			return visitPropertyOrElementAccessExpression(node,
														  captureThisArg,
														  isDelete);
		case Kind::CallExpression:
			return visitCallExpression(node->as<CallExpression>(),
									   captureThisArg);
		default:
			return visitor()->visitNode(node->asNode());
		}
	}

	// visitOptionalExpression — optionalchain.go:150
	Node* visitOptionalExpression(Node* node, bool captureThisArg,
								  bool isDelete) {
		flattenResult r = flattenChain(node);
		Node* expression = r.expression;
		std::vector<Node*> chain = r.chain;
		Node* left = visitNonOptionalExpression(
			skipPartiallyEmittedExpressions(expression),
			isCallChain(chain[0]), false);
		Node* leftThisArg = nullptr;
		Node* capturedLeft = left;
		if (isSyntheticReferenceExpression(left)) {
			SyntheticReferenceExpression* synthLeft =
				left->as<SyntheticReferenceExpression>();
			leftThisArg = synthLeft->ThisArg;
			capturedLeft = synthLeft->Expression;
		}
		Node* leftExpression = factory()->restoreOuterExpressions(
			expression, capturedLeft, OEKPartiallyEmittedExpressions);
		if (!isSimpleCopiableExpression(capturedLeft)) {
			capturedLeft = factory()->newTempVariable();
			emitContext()->addVariableDeclaration(capturedLeft);
			leftExpression = factory()->newAssignmentExpression(capturedLeft,
																leftExpression);
		}
		Node* rightExpression = capturedLeft;
		Node* thisArg = nullptr;

		for (size_t i = 0; i < chain.size(); i++) {
			Node* segment = chain[i];
			switch (segment->kind) {
			case Kind::ElementAccessExpression:
			case Kind::PropertyAccessExpression:
				if (i == chain.size() - 1 && captureThisArg) {
					if (!isSimpleCopiableExpression(rightExpression)) {
						thisArg = factory()->newTempVariable();
						emitContext()->addVariableDeclaration(thisArg);
						rightExpression =
							factory()->newAssignmentExpression(thisArg,
															   rightExpression);
					} else {
						thisArg = rightExpression;
					}
				}
				if (segment->kind == Kind::ElementAccessExpression) {
					rightExpression = factory()->newElementAccessExpression(
						rightExpression, nullptr,
						visitor()->visitNode(
							segment->as<ElementAccessExpression>()
								->ArgumentExpression),
						NodeFlagsNone);
				} else {
					rightExpression =
						factory()->newPropertyAccessExpression(
							rightExpression, nullptr,
							visitor()->visitNode(
								segment->as<PropertyAccessExpression>()
									->name),
							NodeFlagsNone);
				}
				break;
			case Kind::CallExpression:
				if (i == 0 && leftThisArg != nullptr) {
					if (!emitContext()->hasAutoGenerateInfo(leftThisArg)) {
						leftThisArg = leftThisArg->clone(*factory());
						emitContext()->addEmitFlags(leftThisArg,
													printer::EFNoComments);
					}
					Node* callThisArg = leftThisArg;
					if (leftThisArg->kind == Kind::SuperKeyword) {
						callThisArg = factory()->newThisExpression();
					}
					rightExpression = factory()->newFunctionCallCall(
						rightExpression, callThisArg,
						visitor()
							->visitNodes(segment->argumentList())
							->nodes);
				} else {
					rightExpression = factory()->newCallExpression(
						rightExpression, nullptr, nullptr,
						visitor()->visitNodes(segment->argumentList()),
						NodeFlagsNone);
				}
				break;
			default:
				break;
			}
			emitContext()->setOriginal(rightExpression, segment);
		}

		Node* target = nullptr;
		if (isDelete) {
			target = factory()->newConditionalExpression(
				createNotNullCondition(emitContext(), leftExpression,
									   capturedLeft, true),
				factory()->newToken(Kind::QuestionToken),
				factory()->newTrueExpression(),
				factory()->newToken(Kind::ColonToken),
				factory()->newDeleteExpression(rightExpression));
		} else {
			target = factory()->newConditionalExpression(
				createNotNullCondition(emitContext(), leftExpression,
									   capturedLeft, true),
				factory()->newToken(Kind::QuestionToken),
				factory()->newVoidZeroExpression(),
				factory()->newToken(Kind::ColonToken), rightExpression);
		}
		target->loc = node->loc;
		if (thisArg != nullptr) {
			target = factory()->newSyntheticReferenceExpression(target,
															   thisArg);
		}
		emitContext()->setOriginal(target, node->asNode());
		return target;
	}

	// newOptionalChainTransformer — optionalchain.go:237
	static Transformer* create(TransformOptions* opt) {
		auto* tx = new OptionalChainTransformer;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, opt->Context);
	}
};

}  // namespace

Transformer* newOptionalChainTransformer(TransformOptions* opt) {
	return OptionalChainTransformer::create(opt);
}

}  // namespace tsc::transformers::estransforms
