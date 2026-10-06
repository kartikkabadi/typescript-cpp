// === slice: ls-coreC ===
// selectionranges.cpp — selectionranges.go: smart selection ranges.
#include "internal/ls/ls.h"

namespace tsc::ls {

namespace {

// core.NewTextRange
TextRange newTextRange(int pos, int end) {
	return TextRange{static_cast<TextPos>(pos), static_cast<TextPos>(end)};
}

constexpr int maxSelectionRangeDepth = 1000;

// selectionranges.go:16 — selectionRangeBuilder
struct selectionRangeBuilder {
	std::vector<lsp::lsproto::Range> ranges;
	int oldestIndex = 0;

	// selectionranges.go:27
	void push(lsp::lsproto::Range selectionRange) {
		if (int(ranges.size()) < int(ranges.capacity())) {
			ranges.push_back(selectionRange);
			return;
		}
		ranges[oldestIndex] = selectionRange;
		oldestIndex = (oldestIndex + 1) % int(ranges.size());
	}

	// selectionranges.go:37
	lsp::lsproto::SelectionRange* build(lsp::lsproto::SelectionRange* result) {
		for (size_t i = 0; i < ranges.size(); i++) {
			size_t index = (size_t(oldestIndex) + i) % ranges.size();
			auto* r = new lsp::lsproto::SelectionRange;
			r->Range = ranges[index];
			r->Parent = result;
			result = r;
		}
		return result;
	}
};

selectionRangeBuilder* newSelectionRangeBuilder(int capacity) {
	auto* b = new selectionRangeBuilder;
	b->ranges.reserve(capacity);
	return b;
}

// fwd decls — defined below in the Go order.
std::vector<::tsc::Node*> groupChildren(NodeFactory* factory,
										std::vector<::tsc::Node*> children,
										std::function<bool(::tsc::Node*)> groupOn);
std::vector<::tsc::Node*> splitChildren(NodeFactory* factory,
										std::vector<::tsc::Node*> children,
										std::function<bool(::tsc::Node*)> pivotOn,
										bool separateTrailingSemicolon);
::tsc::Node* createSyntaxList(NodeFactory* factory, std::vector<::tsc::Node*> children);

// selectionranges.go:69 — getSelectionChildren
std::vector<::tsc::Node*> getSelectionChildren(NodeFactory* factory, ::tsc::Node* node,
											 SourceFile* sourceFile) {
	if (!isMappedTypeNode(node)) {
		return getChildrenFromNonJSDocNode(node, sourceFile);
	}

	std::vector<::tsc::Node*> children = getChildrenFromNonJSDocNode(node, sourceFile);
	if (children.size() < 2) {
		return children;
	}

	::tsc::Node* openBraceToken = children.front();
	::tsc::Node* closeBraceToken = children.back();
	if (openBraceToken->kind != Kind::OpenBraceToken ||
		closeBraceToken->kind != Kind::CloseBraceToken) {
		return children;
	}

	MappedTypeNode* mappedType = node->as<MappedTypeNode>();
	children = std::vector<::tsc::Node*>(children.begin() + 1, children.end() - 1);

	// Group `-/+readonly` and `-/+?`.
	std::vector<::tsc::Node*> groupedWithPlusMinusTokens =
		groupChildren(factory, children, [&](::tsc::Node* child) {
			return child == mappedType->ReadonlyToken ||
				   child->kind == Kind::ReadonlyKeyword ||
				   child == mappedType->QuestionToken ||
				   child->kind == Kind::QuestionToken;
		});

	// Group the type parameter with its surrounding brackets.
	std::vector<::tsc::Node*> groupedWithBrackets =
		groupChildren(factory, groupedWithPlusMinusTokens, [](::tsc::Node* child) {
			return child->kind == Kind::OpenBracketToken ||
				   child->kind == Kind::TypeParameter ||
				   child->kind == Kind::CloseBracketToken;
		});

	// Go exposes the trailing semicolon directly, so keep it in the right-hand
	// group to produce the same effective selection tree as Strada.
	return {openBraceToken,
			createSyntaxList(factory, splitChildren(factory, groupedWithBrackets,
													[](::tsc::Node* child) {
														return child->kind == Kind::ColonToken;
													},
													false)),
			closeBraceToken};
}

// selectionranges.go:114 — groupChildren
std::vector<::tsc::Node*> groupChildren(NodeFactory* factory,
										std::vector<::tsc::Node*> children,
										std::function<bool(::tsc::Node*)> groupOn) {
	std::vector<::tsc::Node*> result;
	std::vector<::tsc::Node*> group;
	for (auto* child : children) {
		if (groupOn(child)) {
			group.push_back(child);
		} else {
			if (!group.empty()) {
				result.push_back(createSyntaxList(factory, group));
				group.clear();
			}
			result.push_back(child);
		}
	}
	if (!group.empty()) {
		result.push_back(createSyntaxList(factory, group));
	}
	return result;
}

// selectionranges.go:134 — splitChildren
std::vector<::tsc::Node*> splitChildren(NodeFactory* factory,
										std::vector<::tsc::Node*> children,
										std::function<bool(::tsc::Node*)> pivotOn,
										bool separateTrailingSemicolon) {
	if (children.size() < 2) {
		return children;
	}

	int splitTokenIndex = -1;
	for (size_t i = 0; i < children.size(); i++) {
		if (pivotOn(children[i])) {
			splitTokenIndex = int(i);
			break;
		}
	}
	if (splitTokenIndex == -1) {
		return children;
	}

	std::vector<::tsc::Node*> leftChildren(children.begin(), children.begin() + splitTokenIndex);
	::tsc::Node* splitToken = children[splitTokenIndex];
	::tsc::Node* lastToken = children.back();
	bool separateLastToken =
		separateTrailingSemicolon && lastToken->kind == Kind::SemicolonToken;
	size_t rightEnd = children.size();
	if (separateLastToken) {
		rightEnd--;
	}
	std::vector<::tsc::Node*> rightChildren(children.begin() + splitTokenIndex + 1,
										  children.begin() + rightEnd);

	std::vector<::tsc::Node*> result;
	result.reserve(4);
	if (!leftChildren.empty()) {
		result.push_back(createSyntaxList(factory, leftChildren));
	}
	result.push_back(splitToken);
	if (!rightChildren.empty()) {
		result.push_back(createSyntaxList(factory, rightChildren));
	}
	if (separateLastToken) {
		result.push_back(lastToken);
	}
	return result;
}

// selectionranges.go:179 — createSyntaxList
::tsc::Node* createSyntaxList(NodeFactory* factory, std::vector<::tsc::Node*> children) {
	::tsc::Node* list = factory->newSyntaxList(children);
	list->loc = newTextRange(int(children.front()->pos()), int(children.back()->end()));
	return list;
}

// selectionranges.go:185 — getSmartSelectionRange
lsp::lsproto::SelectionRange* getSmartSelectionRange(LanguageService* l, SourceFile* sourceFile,
												   int pos) {
	NodeFactory factory;
	// Traversal discovers ranges from broadest to most specific, so retain the newest ranges nearest to the cursor
	selectionRangeBuilder* ranges = newSelectionRangeBuilder(maxSelectionRangeDepth - 1);
	lsp::lsproto::SelectionRange* root = nullptr;
	lsp::lsproto::Range lastRange{};
	if (sourceFile->ContentMapper().empty()) {
		auto [fullRange, _f] =
			l->converters->ToLSPRange(sourceFile,
									  newTextRange(int(sourceFile->pos()), int(sourceFile->end())));
		root = new lsp::lsproto::SelectionRange;
		root->Range = fullRange;
		lastRange = fullRange;
	}

	auto nodeContainsPosition = [&](::tsc::Node* node) {
		if (node == nullptr) {
			return false;
		}
		int start = tsc::getTokenPosOfNode(node, sourceFile, true /*includeJSDoc*/);
		int end = node->end();
		return start <= pos && pos < end;
	};

	auto positionShouldSnapToNode = [&](::tsc::Node* node) {
		if (pos < int(node->end())) {
			return true;
		}
		if (int(node->end()) == pos) {
			::tsc::Node* touchingPropertyName = astnav::getTouchingPropertyName(sourceFile, pos);
			return touchingPropertyName != nullptr &&
				   touchingPropertyName->pos() < node->end();
		}
		return false;
	};

	auto pushSelectionRange = [&](int start, int end) {
		if (start == end) {
			return;
		}

		if (!(start <= pos && pos <= end)) {
			return;
		}

		auto [lspRange, fidelity] =
			l->converters->ToLSPRangeForFeature(sourceFile, newTextRange(start, end),
											  spanmap::FeatureSelectionRanges);
		if (fidelity.IsNone()) {
			return;
		}

		if (lastRange == lspRange) {
			return;
		}
		lastRange = lspRange;

		ranges->push(lspRange);
	};

	auto pushSelectionCommentRange = [&](int start, int end) {
		pushSelectionRange(start, end);

		int commentPos = start;
		const std::string& text = sourceFile->Text();
		while (commentPos < end && commentPos < int(text.size()) && text[commentPos] == '/') {
			commentPos++;
		}
		pushSelectionRange(commentPos, end);
	};

	auto positionsAreOnSameLine = [&](int pos1, int pos2) {
		if (pos1 == pos2) {
			return true;
		}
		auto& lineStarts = sourceFile->ecmaLineMap();
		return tsc::computeLineOfPosition(lineStarts, pos1) ==
			   tsc::computeLineOfPosition(lineStarts, pos2);
	};

	auto shouldSkipNode = [](::tsc::Node* node, ::tsc::Node* parent) {
		if (isBlock(node)) {
			return true;
		}

		if (isTemplateSpan(node) || isTemplateHead(node) || isTemplateTail(node)) {
			return true;
		}

		if (parent != nullptr && isVariableDeclarationList(node) &&
			isVariableStatement(parent)) {
			return true;
		}

		// Skip lone variable declarations
		if (parent != nullptr && isVariableDeclaration(node) &&
			isVariableDeclarationList(parent)) {
			VariableDeclarationList* decl = parent->as<VariableDeclarationList>();
			if (decl != nullptr && decl->Declarations->nodes.size() == 1) {
				return true;
			}
		}

		if (isJSDocTypeExpression(node) || isJSDocSignature(node) ||
			isJSDocTypeLiteral(node)) {
			return true;
		}

		return false;
	};

	::tsc::Node* current = sourceFile;
	for (; current != nullptr;) {
		::tsc::Node* next = nullptr;
		::tsc::Node* parent = current;

		auto visit = [&](::tsc::Node* node) -> ::tsc::Node* {
			if (node != nullptr && next == nullptr) {
				CommentRange* foundComment = nullptr;
				CommentRange found;
				bool have = false;
				tsc::getTrailingCommentRanges(sourceFile->Text(), int(node->end()),
												  [&](const CommentRange& r) {
													  found = r;
													  have = true;
													  return false;
												  });
				if (have) {
					foundComment = &found;
				}
				if (foundComment != nullptr &&
					foundComment->kind == Kind::SingleLineCommentTrivia) {
					pushSelectionCommentRange(int(foundComment->pos()), int(foundComment->end()));
				}

				if (nodeContainsPosition(node)) {
					// Add range for multi-line function bodies before skipping the block
					if (isBlock(node) && isFunctionLikeDeclaration(parent)) {
						if (!positionsAreOnSameLine(
								astnav::getStartOfNode(node, sourceFile, false),
								int(node->end()))) {
							int start = astnav::getStartOfNode(node, sourceFile, false);
							int end = node->end();
							pushSelectionRange(start, end);
						}
					}

					// Synthesize a stop for '${ ... }' since '${' and '}' actually belong to siblings.
					if (isTemplateSpan(parent)) {
						TemplateSpan* templateSpan = parent->as<TemplateSpan>();
						if (templateSpan->Literal != nullptr) {
							// Start from just before the '${' and end after the '}'
							// The '${' is 2 characters before the expression start
							int spanStart = int(node->pos()) - 2;
							// The '}' is the first character of the template literal (middle or tail)
							int spanEnd = astnav::getStartOfNode(templateSpan->Literal, sourceFile,
															   false) +
										  1;
							// Validate the positions are reasonable
							const std::string& text = sourceFile->Text();
							if (spanStart >= 0 && spanEnd <= int(text.size()) &&
								spanStart < spanEnd) {
								pushSelectionRange(spanStart, spanEnd);
							}
						}
					}

					if (!shouldSkipNode(node, parent)) {
						int start = astnav::getStartOfNode(node, sourceFile, false);
						int end = node->end();
						pushSelectionRange(start, end);

						if (isMappedTypeNode(node)) {
							for (::tsc::Node* selectionParent = node;;) {
								::tsc::Node* selectionChild = nullptr;
								for (auto* child : getSelectionChildren(&factory, selectionParent,
																		sourceFile)) {
									int childStart = tsc::getTokenPosOfNode(
										child, sourceFile, true /*includeJSDoc*/);
									if (childStart > pos) {
										break;
									}
									if (positionShouldSnapToNode(child)) {
										pushSelectionRange(childStart, int(child->end()));
										selectionChild = child;
										break;
									}
								}
								if (selectionChild == nullptr ||
									!isSyntaxList(selectionChild)) {
									break;
								}
								selectionParent = selectionChild;
							}
						}

						// String literals should have a stop both inside and outside their quotes.
						if (isStringLiteral(node) ||
							node->kind == Kind::TemplateExpression ||
							node->kind == Kind::NoSubstitutionTemplateLiteral) {
							// Only add inner content range if there's actually content (handles unterminated literals)
							if (start + 1 < end - 1) {
								pushSelectionRange(start + 1, end - 1);
							}
						}
					}

					next = node;
				}
			}
			return node;
		};

		auto visitNodes = [&](NodeList* nodes, NodeVisitor* v) -> NodeList* {
			if (nodes != nullptr && !nodes->nodes.empty()) {
				bool shouldSkipList = parent != nullptr &&
									  (isVariableDeclarationList(parent) ||
									   isTemplateExpression(parent));

				if (!shouldSkipList) {
					int start = astnav::getStartOfNode(nodes->nodes.front(), sourceFile, false);
					int end = nodes->nodes.back()->end();

					if (start <= pos && pos < end) {
						pushSelectionRange(start, end);
					}
				}
			}
			return v->visitNodes(nodes);
		};

		// Visit JSDoc nodes first if they exist
		for (auto* jsdoc : current->jsDoc(sourceFile)) {
			visit(jsdoc);
		}

		NodeVisitorHooks hooks;
		hooks.visitNodes = visitNodes;
		NodeVisitor* tempVisitor = newNodeVisitor(visit, &factory, hooks);

		current->visitEachChild(*tempVisitor);
		current = next;
	}
	return ranges->build(root);
}

} // namespace

// ============================================================================
// selectionranges.go — ProvideSelectionRanges
// ============================================================================
// selectionranges.go:48
lsp::lsproto::SelectionRangeResponse LanguageService::ProvideSelectionRanges(
	gostd::Context ctx, lsp::lsproto::SelectionRangeParams* params) {
	SourceFile* sourceFile = getProgramAndFile(params->TextDocument.Uri).second;
	if (sourceFile == nullptr) {
		return lsp::lsproto::SelectionRangesOrNull{};
	}

	std::vector<lsp::lsproto::SelectionRange*> results;
	results.reserve(params->Positions.size());
	for (auto& position : params->Positions) {
		auto positions = converters->FromLSPPositionForSourceFile(sourceFile, position,
																spanmap::FeatureSelectionRanges);
		if (positions.size() != 1 || !positions[0].Fidelity.IsSingleSegment()) {
			return lsp::lsproto::SelectionRangesOrNull{};
		}
		lsp::lsproto::SelectionRange* selectionRange = getSmartSelectionRange(
			this, positions[0].Script, int(positions[0].Position));
		if (selectionRange != nullptr) {
			results.push_back(selectionRange);
		}
	}

	lsp::lsproto::SelectionRangesOrNull res;
	res.SelectionRanges = new std::vector<lsp::lsproto::SelectionRange*>(std::move(results));
	return res;
}

} // namespace tsc::ls
