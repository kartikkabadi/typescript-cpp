// === slice: ls-coreC ===
// documenthighlights.cpp — documenthighlights.go: document highlights (semantic
// via find-all-references + syntactic keyword highlights).
#include "internal/ls/ls.h"

#include "internal/astnav/tokens.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls {

namespace {

// forward decls (Go file order)
std::vector<::tsc::Node*> getIfElseKeywords(IfStatement* ifStatement,
										  SourceFile* sourceFile);
std::vector<::tsc::Node*> getSwitchCaseDefaultOccurrences(
	::tsc::Node* node, SourceFile* sourceFile);
std::vector<::tsc::Node*> getLoopBreakContinueOccurrences(
	::tsc::Node* node, SourceFile* sourceFile);

// ast.go:805 — Node.TagName (not yet in cpp/internal/ast)
::tsc::Node* tagName(::tsc::Node* n) {
	switch (n->kind) {
	case Kind::JsxOpeningElement:
		return n->as<JsxOpeningElement>()->TagName;
	case Kind::JsxClosingElement:
		return n->as<JsxClosingElement>()->TagName;
	case Kind::JsxSelfClosingElement:
		return n->as<JsxSelfClosingElement>()->TagName;
	case Kind::JSDocUnknownTag:
		return n->as<JSDocUnknownTag>()->TagName;
	case Kind::JSDocAugmentsTag:
		return n->as<JSDocAugmentsTag>()->TagName;
	case Kind::JSDocImplementsTag:
		return n->as<JSDocImplementsTag>()->TagName;
	case Kind::JSDocDeprecatedTag:
		return n->as<JSDocDeprecatedTag>()->TagName;
	case Kind::JSDocPublicTag:
		return n->as<JSDocPublicTag>()->TagName;
	case Kind::JSDocPrivateTag:
		return n->as<JSDocPrivateTag>()->TagName;
	case Kind::JSDocProtectedTag:
		return n->as<JSDocProtectedTag>()->TagName;
	case Kind::JSDocReadonlyTag:
		return n->as<JSDocReadonlyTag>()->TagName;
	case Kind::JSDocOverrideTag:
		return n->as<JSDocOverrideTag>()->TagName;
	case Kind::JSDocCallbackTag:
		return n->as<JSDocCallbackTag>()->TagName;
	case Kind::JSDocOverloadTag:
		return n->as<JSDocOverloadTag>()->TagName;
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
		return n->as<JSDocParameterOrPropertyTag>()->TagName;
	case Kind::JSDocReturnTag:
		return n->as<JSDocReturnTag>()->TagName;
	case Kind::JSDocThisTag:
		return n->as<JSDocThisTag>()->TagName;
	case Kind::JSDocTypeTag:
		return n->as<JSDocTypeTag>()->TagName;
	case Kind::JSDocTemplateTag:
		return n->as<JSDocTemplateTag>()->TagName;
	case Kind::JSDocTypedefTag:
		return n->as<JSDocTypedefTag>()->TagName;
	case Kind::JSDocSeeTag:
		return n->as<JSDocSeeTag>()->TagName;
	case Kind::JSDocSatisfiesTag:
		return n->as<JSDocSatisfiesTag>()->TagName;
	case Kind::JSDocThrowsTag:
		return n->as<JSDocThrowsTag>()->TagName;
	case Kind::JSDocImportTag:
		return n->as<JSDocImportTag>()->TagName;
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in Node.TagName");
}

// utilities.go:2336 — IsBreakOrContinueStatement (not yet in cpp/internal/ast)
bool isBreakOrContinueStatement(::tsc::Node* node) {
	return node->kind == Kind::BreakStatement ||
		   node->kind == Kind::ContinueStatement;
}

// utilities.go:1157 — ForEachReturnStatement (not yet in cpp/internal/ast)
bool forEachReturnStatement(::tsc::Node* body,
							std::function<bool(::tsc::Node*)> visitor) {
	std::function<bool(::tsc::Node*)> traverse =
		[&](::tsc::Node* node) -> bool {
		switch (node->kind) {
		case Kind::ReturnStatement:
			return visitor(node);
		case Kind::CaseBlock:
		case Kind::Block:
		case Kind::IfStatement:
		case Kind::DoStatement:
		case Kind::WhileStatement:
		case Kind::ForStatement:
		case Kind::ForInStatement:
		case Kind::ForOfStatement:
		case Kind::WithStatement:
		case Kind::SwitchStatement:
		case Kind::CaseClause:
		case Kind::DefaultClause:
		case Kind::LabeledStatement:
		case Kind::TryStatement:
		case Kind::CatchClause:
			return node->forEachChild(traverse);
		default:
			return false;
		}
	};
	return traverse(body);
}

// documenthighlights.go:442 — flatMapChildren
template <typename T>
std::vector<T> flatMapChildren(
	::tsc::Node* node, SourceFile* sourceFile,
	std::function<std::vector<T>(::tsc::Node*, SourceFile*)> cb) {
	std::vector<T> result;
	node->forEachChild([&](::tsc::Node* child) {
		std::vector<T> value = cb(child, sourceFile);
		if (!value.empty()) {
			result.insert(result.end(), value.begin(), value.end());
		}
		return false; // continue traversal
	});
	return result;
}

// documenthighlights.go:402 — aggregateOwnedThrowStatements
std::vector<::tsc::Node*> aggregateOwnedThrowStatements(::tsc::Node* node,
													  SourceFile* sourceFile) {
	if (isThrowStatement(node)) {
		return {node};
	}
	if (isTryStatement(node)) {
		// Exceptions thrown within a try block lacking a catch clause are "owned" in the current context.
		TryStatement* statement = node->as<TryStatement>();
		::tsc::Node* tryBlock = statement->TryBlock;
		::tsc::Node* catchClause = statement->CatchClause;
		::tsc::Node* finallyBlock = statement->FinallyBlock;

		std::vector<::tsc::Node*> result;
		if (catchClause != nullptr) {
			result = aggregateOwnedThrowStatements(catchClause, sourceFile);
		} else if (tryBlock != nullptr) {
			result = aggregateOwnedThrowStatements(tryBlock, sourceFile);
		}
		if (finallyBlock != nullptr) {
			auto fb = aggregateOwnedThrowStatements(finallyBlock, sourceFile);
			result.insert(result.end(), fb.begin(), fb.end());
		}
		return result;
	}
	// Do not cross function boundaries.
	if (isFunctionLike(node)) {
		return {};
	}
	return flatMapChildren<::tsc::Node*>(node, sourceFile,
										 aggregateOwnedThrowStatements);
}

// documenthighlights.go:474 — getThrowStatementOwner
// For lack of a better name, this function takes a throw statement and returns the
// nearest ancestor that is a try-block (whose try statement has a catch clause),
// function-block, or source file.
::tsc::Node* getThrowStatementOwner(::tsc::Node* throwStatement) {
	::tsc::Node* child = throwStatement;
	while (child->parent != nullptr) {
		::tsc::Node* parent = child->parent;

		if (lsutil::isFunctionBlock(parent) ||
			parent->kind == Kind::SourceFile) {
			return parent;
		}

		// A throw-statement is only owned by a try-statement if the try-statement has
		// a catch clause, and if the throw-statement occurs within the try block.
		if (isTryStatement(parent)) {
			TryStatement* tryStatement = parent->as<TryStatement>();
			if (tryStatement->TryBlock == child &&
				tryStatement->CatchClause != nullptr) {
				return child;
			}
		}

		child = parent;
	}
	return nullptr;
}

// Whether or not a 'node' is preceded by a label of the given string.
// Note: 'node' cannot be a SourceFile.
// documenthighlights.go:601
bool isLabeledBy(::tsc::Node* node, std::string_view labelName) {
	return findAncestorOrQuit(
			   node->parent, [&](::tsc::Node* owner) -> FindAncestorResult {
				   if (!isLabeledStatement(owner)) {
					   return FindAncestorResult::Quit;
				   }
				   if (owner->label()->text() == labelName) {
					   return FindAncestorResult::True;
				   }
				   return FindAncestorResult::False;
			   }) != nullptr;
}

// documenthighlights.go:577 — getBreakOrContinueOwner
::tsc::Node* getBreakOrContinueOwner(::tsc::Node* statement) {
	return findAncestorOrQuit(
		statement, [&](::tsc::Node* node) -> FindAncestorResult {
			switch (node->kind) {
			case Kind::SwitchStatement:
				if (statement->kind == Kind::ContinueStatement) {
					return FindAncestorResult::False;
				}
				[[fallthrough]];
			case Kind::ForStatement:
			case Kind::ForInStatement:
			case Kind::ForOfStatement:
			case Kind::WhileStatement:
			case Kind::DoStatement:
				// If the statement is labeled, check if the node is labeled by the statement's label.
				if (statement->label() == nullptr ||
					isLabeledBy(node, statement->label()->text())) {
					return FindAncestorResult::True;
				}
				return FindAncestorResult::False;
			default:
				// Don't cross function boundaries.
				if (isFunctionLike(node)) {
					return FindAncestorResult::Quit;
				}
				return FindAncestorResult::False;
			}
		});
}

// documenthighlights.go:564 — aggregateAllBreakAndContinueStatements
std::vector<::tsc::Node*> aggregateAllBreakAndContinueStatements(
	::tsc::Node* node, SourceFile* sourceFile) {
	if (isBreakOrContinueStatement(node)) {
		return {node};
	}
	if (isFunctionLike(node)) {
		return {};
	}
	return flatMapChildren<::tsc::Node*>(
		node, sourceFile, aggregateAllBreakAndContinueStatements);
}

// documenthighlights.go:572 — ownsBreakOrContinueStatement
bool ownsBreakOrContinueStatement(::tsc::Node* owner,
								  ::tsc::Node* statement) {
	::tsc::Node* actualOwner = getBreakOrContinueOwner(statement);
	if (actualOwner == nullptr) {
		return false;
	}
	return actualOwner == owner;
}

// documenthighlights.go:684 — traverseWithoutCrossingFunction
void traverseWithoutCrossingFunction(
	::tsc::Node* node, SourceFile* sourceFile,
	std::function<void(::tsc::Node*)> cb) {
	cb(node);
	if (!isFunctionLike(node) && !isClassLike(node) &&
		!isInterfaceDeclaration(node) && !isModuleDeclaration(node) &&
		!isTypeAliasDeclaration(node) && !isTypeNode(node)) {
		node->forEachChild([&](::tsc::Node* child) {
			traverseWithoutCrossingFunction(child, sourceFile, cb);
			return false; // continue traversal
		});
	}
}

// documenthighlights.go:722 — getNodesToSearchForModifier
std::vector<::tsc::Node*> getNodesToSearchForModifier(::tsc::Node* declaration,
													ModifierFlags modifierFlag) {
	std::vector<::tsc::Node*> result;

	::tsc::Node* container = declaration->parent;
	if (container == nullptr) {
		return {};
	}

	// Types of node whose children might have modifiers.
	switch (container->kind) {
	case Kind::ModuleBlock:
	case Kind::SourceFile:
	case Kind::Block:
	case Kind::CaseClause:
	case Kind::DefaultClause:
		// Container is either a class declaration or the declaration is a classDeclaration
		if ((modifierFlag & ModifierFlagsAbstract) != 0 &&
			isClassDeclaration(declaration)) {
			for (auto* m : declaration->members()) {
				result.push_back(m);
			}
			result.push_back(declaration);
			return result;
		} else {
			for (auto* s : container->statements()) {
				result.push_back(s);
			}
			return result;
		}
	case Kind::Constructor:
	case Kind::MethodDeclaration:
	case Kind::FunctionDeclaration:
		// Parameters and, if inside a class, also class members
		for (auto* p : container->parameters()) {
			result.push_back(p);
		}
		if (isClassLike(container->parent)) {
			for (auto* m : container->parent->members()) {
				result.push_back(m);
			}
		}
		return result;
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::InterfaceDeclaration:
	case Kind::TypeLiteral: {
		std::vector<::tsc::Node*> nodes = container->members();
		result.insert(result.end(), nodes.begin(), nodes.end());
		// If we're an accessibility modifier, we're in an instance member and should search
		// the constructor's parameter list for instance members as well.
		if ((modifierFlag & (ModifierFlagsAccessibilityModifier |
							 ModifierFlagsReadonly)) != 0) {
			::tsc::Node* constructor = nullptr;
			for (auto* member : nodes) {
				if (isConstructorDeclaration(member)) {
					constructor = member;
					break;
				}
			}
			if (constructor != nullptr) {
				for (auto* p : constructor->parameters()) {
					result.push_back(p);
				}
			}
		} else if ((modifierFlag & ModifierFlagsAbstract) != 0) {
			result.push_back(container);
		}
		return result;
	}
	default:
		// Syntactically invalid positions or unsupported containers
		return {};
	}
}

// documenthighlights.go:788 — findModifier
::tsc::Node* findModifier(::tsc::Node* node, Kind kind) {
	for (auto* modifier : node->modifierNodes()) {
		if (modifier->kind == kind) {
			return modifier;
		}
	}
	return nullptr;
}

// documenthighlights.go:711 — getModifierOccurrences
std::vector<::tsc::Node*> getModifierOccurrences(Kind kind, ::tsc::Node* node,
											   SourceFile* sourceFile) {
	std::vector<::tsc::Node*> result;
	std::vector<::tsc::Node*> nodesToSearch =
		getNodesToSearchForModifier(node, modifierToFlag(kind));
	for (auto* n : nodesToSearch) {
		if (::tsc::Node* modifier = findModifier(n, kind);
			modifier != nullptr) {
			result.push_back(modifier);
		}
	}
	return result;
}

// documenthighlights.go:390 — getReturnOccurrences
std::vector<::tsc::Node*> getReturnOccurrences(::tsc::Node* node,
											 SourceFile* sourceFile) {
	::tsc::Node* funcNode = findAncestor(node->parent, isFunctionLike);
	if (funcNode == nullptr) {
		return {};
	}

	std::vector<::tsc::Node*> keywords;
	::tsc::Node* body = funcNode->body();
	if (body != nullptr) {
		forEachReturnStatement(body, [&](::tsc::Node* ret) -> bool {
			::tsc::Node* keyword = astnav::findChildOfKind(
				ret, Kind::ReturnKeyword, sourceFile);
			if (keyword != nullptr) {
				keywords.push_back(keyword);
			}
			return false; // continue traversal
		});

		// Get all throw statements not in a try block
		auto throwStatements =
			aggregateOwnedThrowStatements(body, sourceFile);
		for (auto* throw_ : throwStatements) {
			::tsc::Node* keyword = astnav::findChildOfKind(
				throw_, Kind::ThrowKeyword, sourceFile);
			if (keyword != nullptr) {
				keywords.push_back(keyword);
			}
		}
	}
	return keywords;
}

// documenthighlights.go:456 — getThrowOccurrences
std::vector<::tsc::Node*> getThrowOccurrences(::tsc::Node* node,
											SourceFile* sourceFile) {
	::tsc::Node* owner = getThrowStatementOwner(node);
	if (owner == nullptr) {
		return {};
	}

	std::vector<::tsc::Node*> keywords;

	// Aggregate all throw statements "owned" by this owner.
	auto throwStatements = aggregateOwnedThrowStatements(owner, sourceFile);
	for (auto* throw_ : throwStatements) {
		::tsc::Node* keyword = astnav::findChildOfKind(
			throw_, Kind::ThrowKeyword, sourceFile);
		if (keyword != nullptr) {
			keywords.push_back(keyword);
		}
	}

	// If the "owner" is a function, then we equate 'return' and 'throw' statements in their
	// ability to "jump out" of the function, and include occurrences for both
	if (lsutil::isFunctionBlock(owner)) {
		forEachReturnStatement(owner, [&](::tsc::Node* ret) -> bool {
			::tsc::Node* keyword = astnav::findChildOfKind(
				ret, Kind::ReturnKeyword, sourceFile);
			if (keyword != nullptr) {
				keywords.push_back(keyword);
			}
			return false; // continue traversal
		});
	}

	return keywords;
}

// documenthighlights.go:496 — getTryCatchFinallyOccurrences
std::vector<::tsc::Node*> getTryCatchFinallyOccurrences(::tsc::Node* node,
													  SourceFile* sourceFile) {
	TryStatement* tryStatement = node->as<TryStatement>();

	std::vector<::tsc::Node*> keywords;
	::tsc::Node* token = lsutil::GetFirstToken(node, sourceFile);
	if (token != nullptr && token->kind == Kind::TryKeyword) {
		keywords.push_back(token);
	}

	if (tryStatement->CatchClause != nullptr) {
		if (::tsc::Node* catchToken = astnav::findChildOfKind(
				node, Kind::CatchKeyword, sourceFile);
			catchToken != nullptr) {
			keywords.push_back(catchToken);
		}
	}

	if (tryStatement->FinallyBlock != nullptr) {
		if (::tsc::Node* finallyKeyword = astnav::findChildOfKind(
				node, Kind::FinallyKeyword, sourceFile);
			finallyKeyword != nullptr) {
			keywords.push_back(finallyKeyword);
		}
	}

	return keywords;
}

// documenthighlights.go:521 — getSwitchCaseDefaultOccurrences
std::vector<::tsc::Node*> getSwitchCaseDefaultOccurrences(
	::tsc::Node* node, SourceFile* sourceFile) {
	SwitchStatement* switchStatement = node->as<SwitchStatement>();

	std::vector<::tsc::Node*> keywords;
	::tsc::Node* token = lsutil::GetFirstToken(node, sourceFile);
	if (token->kind == Kind::SwitchKeyword) {
		keywords.push_back(token);
	}

	NodeList* clauses = switchStatement->CaseBlock->as<CaseBlock>()->Clauses;
	for (auto* clause : clauses->nodes) {
		::tsc::Node* clauseToken =
			lsutil::GetFirstToken(clause->asNode(), sourceFile);
		if (clauseToken->kind == Kind::CaseKeyword ||
			clauseToken->kind == Kind::DefaultKeyword) {
			keywords.push_back(clauseToken);
		}

		auto breakAndContinueStatements =
			aggregateAllBreakAndContinueStatements(clause, sourceFile);
		for (auto* statement : breakAndContinueStatements) {
			if (statement->kind == Kind::BreakStatement &&
				ownsBreakOrContinueStatement(switchStatement->asNode(),
											 statement)) {
				keywords.push_back(
					lsutil::GetFirstToken(statement, sourceFile));
			}
		}
	}

	return keywords;
}

// documenthighlights.go:610 — getBreakOrContinueStatementOccurrences
std::vector<::tsc::Node*> getBreakOrContinueStatementOccurrences(
	::tsc::Node* node, SourceFile* sourceFile) {
	if (::tsc::Node* owner = getBreakOrContinueOwner(node); owner != nullptr) {
		switch (owner->kind) {
		case Kind::ForStatement:
		case Kind::ForInStatement:
		case Kind::ForOfStatement:
		case Kind::DoStatement:
		case Kind::WhileStatement:
			return getLoopBreakContinueOccurrences(owner, sourceFile);
		case Kind::SwitchStatement:
			return getSwitchCaseDefaultOccurrences(owner, sourceFile);
		default:
			break;
		}
	}
	return {};
}

// documenthighlights.go:622 — getLoopBreakContinueOccurrences
std::vector<::tsc::Node*> getLoopBreakContinueOccurrences(
	::tsc::Node* node, SourceFile* sourceFile) {
	std::vector<::tsc::Node*> keywords;

	::tsc::Node* token = lsutil::GetFirstToken(node, sourceFile);
	if (token->kind == Kind::ForKeyword || token->kind == Kind::DoKeyword ||
		token->kind == Kind::WhileKeyword) {
		keywords.push_back(token);
		if (node->kind == Kind::DoStatement) {
			auto loopTokens =
				getChildrenFromNonJSDocNode(node, sourceFile);
			for (auto it = loopTokens.rbegin(); it != loopTokens.rend();
				 ++it) {
				if ((*it)->kind == Kind::WhileKeyword) {
					keywords.push_back(*it);
					break;
				}
			}
		}
	}

	auto breakAndContinueStatements =
		aggregateAllBreakAndContinueStatements(node, sourceFile);
	for (auto* statement : breakAndContinueStatements) {
		::tsc::Node* t = lsutil::GetFirstToken(statement, sourceFile);
		if (ownsBreakOrContinueStatement(node, statement) &&
			(t->kind == Kind::BreakKeyword ||
			 t->kind == Kind::ContinueKeyword)) {
			keywords.push_back(t);
		}
	}

	return keywords;
}

// documenthighlights.go:649 — getAsyncAndAwaitOccurrences
std::vector<::tsc::Node*> getAsyncAndAwaitOccurrences(::tsc::Node* node,
													SourceFile* sourceFile) {
	::tsc::Node* fun = getContainingFunction(node);
	if (fun == nullptr) {
		return {};
	}

	std::vector<::tsc::Node*> keywords;

	for (auto* modifier : fun->modifierNodes()) {
		if (modifier->kind == Kind::AsyncKeyword) {
			keywords.push_back(modifier);
		}
	}

	fun->forEachChild([&](::tsc::Node* child) {
		traverseWithoutCrossingFunction(
			child, sourceFile, [&](::tsc::Node* child) {
				if (isAwaitExpression(child)) {
					::tsc::Node* token =
						lsutil::GetFirstToken(child, sourceFile);
					if (token->kind == Kind::AwaitKeyword) {
						keywords.push_back(token);
					}
				}
			});
		return false; // continue traversal
	});

	return keywords;
}

// documenthighlights.go:671 — getYieldOccurrences
std::vector<::tsc::Node*> getYieldOccurrences(::tsc::Node* node,
											SourceFile* sourceFile) {
	::tsc::Node* parentFunc = findAncestor(node->parent, isFunctionLike);
	if (parentFunc == nullptr) {
		return {};
	}

	std::vector<::tsc::Node*> keywords;

	parentFunc->forEachChild([&](::tsc::Node* child) {
		traverseWithoutCrossingFunction(
			child, sourceFile, [&](::tsc::Node* child) {
				if (isYieldExpression(child)) {
					::tsc::Node* token =
						lsutil::GetFirstToken(child, sourceFile);
					if (token->kind == Kind::YieldKeyword) {
						keywords.push_back(token);
					}
				}
			});
		return false; // continue traversal
	});

	return keywords;
}

// documenthighlights.go:104 — combineMultiDocumentHighlights
lsp::lsproto::MultiDocumentHighlightsOrNull combineMultiDocumentHighlights(
	const std::vector<lsp::lsproto::MultiDocumentHighlightsOrNull>& results) {
	std::unordered_map<lsp::lsproto::DocumentUri,
					   lsp::lsproto::MultiDocumentHighlight*>
		byURI;
	std::unordered_map<lsp::lsproto::DocumentUri,
					   collections::Set<lsp::lsproto::Range>>
		seen;
	std::vector<lsp::lsproto::MultiDocumentHighlight*> combinedDocuments;
	for (auto& result : results) {
		if (result.MultiDocumentHighlights == nullptr) {
			continue;
		}
		for (auto* document : *result.MultiDocumentHighlights) {
			lsp::lsproto::MultiDocumentHighlight* combinedDocument = nullptr;
			auto it = byURI.find(document->Uri);
			if (it == byURI.end()) {
				combinedDocument = new lsp::lsproto::MultiDocumentHighlight;
				combinedDocument->Uri = document->Uri;
				byURI[document->Uri] = combinedDocument;
				combinedDocuments.push_back(combinedDocument);
			} else {
				combinedDocument = it->second;
			}
			auto& ranges = seen[document->Uri];
			for (auto* highlight : document->Highlights) {
				if (ranges.AddIfAbsent(highlight->Range)) {
					combinedDocument->Highlights.push_back(highlight);
				}
			}
		}
	}
	lsp::lsproto::MultiDocumentHighlightsOrNull out;
	out.MultiDocumentHighlights =
		new std::vector<lsp::lsproto::MultiDocumentHighlight*>(
			std::move(combinedDocuments));
	return out;
}

} // namespace

// ============================================================================
// documenthighlights.go — ProvideDocumentHighlights /
// ProvideMultiDocumentHighlights / workers
// ============================================================================
// documenthighlights.go:19
lsp::lsproto::DocumentHighlightResponse LanguageService::ProvideDocumentHighlights(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentUri,
	lsp::lsproto::Position documentPosition) {
	auto [result, err] = provideDocumentHighlightsWorker(
		ctx, documentUri, documentPosition, {});
	if (err != nullptr) {
		return lsp::lsproto::DocumentHighlightsOrNull{};
	}
	// Extract highlights for the current file only.
	std::vector<lsp::lsproto::DocumentHighlight*> documentHighlights;
	if (result.MultiDocumentHighlights != nullptr) {
		for (auto* mh : *result.MultiDocumentHighlights) {
			if (mh->Uri == documentUri) {
				documentHighlights.insert(documentHighlights.end(),
										  mh->Highlights.begin(),
										  mh->Highlights.end());
			}
		}
	}
	lsp::lsproto::DocumentHighlightsOrNull out;
	out.DocumentHighlights =
		new std::vector<lsp::lsproto::DocumentHighlight*>(
			std::move(documentHighlights));
	return out;
}

// documenthighlights.go:35
lsp::lsproto::CustomMultiDocumentHighlightResponse
LanguageService::ProvideMultiDocumentHighlights(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentUri,
	lsp::lsproto::Position documentPosition,
	std::vector<lsp::lsproto::DocumentUri> filesToSearch) {
	auto [result, err] = provideDocumentHighlightsWorker(
		ctx, documentUri, documentPosition, std::move(filesToSearch));
	return result;
}

// documenthighlights.go:39
std::pair<lsp::lsproto::MultiDocumentHighlightsOrNull, gostd::Error>
LanguageService::provideDocumentHighlightsWorker(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentUri,
	lsp::lsproto::Position documentPosition,
	std::vector<lsp::lsproto::DocumentUri> filesToSearch) {
	auto [program, sourceFile] = getProgramAndFile(documentUri);
	auto positions = converters->FromLSPPositionForSourceFile(
		sourceFile, documentPosition,
		spanmap::FeatureDocumentHighlights);
	std::vector<lsp::lsproto::MultiDocumentHighlightsOrNull> results;
	results.reserve(positions.size());
	for (auto& mapped : positions) {
		if (mapped.Fidelity.IsSingleSegment()) {
			results.push_back(provideDocumentHighlightsAtPosition(
				ctx, documentUri, int(mapped.Position), program,
				mapped.Script, filesToSearch));
		}
	}
	return {combineMultiDocumentHighlights(results), nullptr};
}

// documenthighlights.go:51
lsp::lsproto::MultiDocumentHighlightsOrNull
LanguageService::provideDocumentHighlightsAtPosition(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentUri, int position,
	compiler::SimpleProgram* program, SourceFile* sourceFile,
	std::vector<lsp::lsproto::DocumentUri> filesToSearch) {
	::tsc::Node* node =
		astnav::getTouchingPropertyName(sourceFile, position);

	// Cheap JSX check before resolving files to search.
	if (node->parent != nullptr &&
		(node->parent->kind == Kind::JsxClosingElement ||
		 (node->parent->kind == Kind::JsxOpeningElement &&
		  tagName(node->parent) == node))) {
		::tsc::Node* openingElement = nullptr;
		::tsc::Node* closingElement = nullptr;
		if (isJsxElement(node->parent->parent)) {
			openingElement =
				node->parent->parent->as<JsxElement>()->OpeningElement;
			closingElement =
				node->parent->parent->as<JsxElement>()->ClosingElement;
		}
		std::vector<lsp::lsproto::DocumentHighlight*> highlights;
		auto* kind = new lsp::lsproto::DocumentHighlightKind(
			lsp::lsproto::DocumentHighlightKindRead);
		if (openingElement != nullptr) {
			if (auto [lspRange, fidelity] =
					createLspRangeFromNodeForFeature(
						openingElement, sourceFile,
						spanmap::FeatureDocumentHighlights);
				!fidelity.IsNone()) {
				auto* dh = new lsp::lsproto::DocumentHighlight;
				dh->Range = lspRange;
				dh->Kind = kind;
				highlights.push_back(dh);
			}
		}
		if (closingElement != nullptr) {
			if (auto [lspRange, fidelity] =
					createLspRangeFromNodeForFeature(
						closingElement, sourceFile,
						spanmap::FeatureDocumentHighlights);
				!fidelity.IsNone()) {
				auto* dh = new lsp::lsproto::DocumentHighlight;
				dh->Range = lspRange;
				dh->Kind = kind;
				highlights.push_back(dh);
			}
		}
		auto* multiHighlights =
			new std::vector<lsp::lsproto::MultiDocumentHighlight*>;
		auto* mh = new lsp::lsproto::MultiDocumentHighlight;
		mh->Uri = documentUri;
		mh->Highlights = std::move(highlights);
		multiHighlights->push_back(mh);
		lsp::lsproto::MultiDocumentHighlightsOrNull out;
		out.MultiDocumentHighlights = multiHighlights;
		return out;
	}

	// Resolve the source files to search, deduplicating by file name.
	std::vector<SourceFile*> sourceFiles;
	collections::Set<std::string> seenFiles;
	for (auto& uri : filesToSearch) {
		std::string fileName = uri.FileName();
		if (!seenFiles.AddIfAbsent(fileName)) {
			continue;
		}
		if (SourceFile* sf = program->GetSourceFile(fileName); sf != nullptr) {
			sourceFiles.push_back(sf);
		}
	}
	if (sourceFiles.empty()) {
		sourceFiles = {sourceFile};
	}

	auto multiHighlights = getSemanticDocumentHighlights(
		ctx, position, node, program, sourceFiles);
	if (multiHighlights.empty()) {
		// Fall back to syntactic highlights for the current file only.
		auto syntacticHighlights =
			getSyntacticDocumentHighlights(node, sourceFile);
		if (!syntacticHighlights.empty()) {
			auto* mh = new lsp::lsproto::MultiDocumentHighlight;
			mh->Uri = documentUri;
			mh->Highlights = std::move(syntacticHighlights);
			multiHighlights = {mh};
		}
	}
	lsp::lsproto::MultiDocumentHighlightsOrNull out;
	out.MultiDocumentHighlights =
		new std::vector<lsp::lsproto::MultiDocumentHighlight*>(
			std::move(multiHighlights));
	return out;
}

// documenthighlights.go:137
std::vector<lsp::lsproto::MultiDocumentHighlight*>
LanguageService::getSemanticDocumentHighlights(
	gostd::Context ctx, int position, ::tsc::Node* node,
	compiler::SimpleProgram* program,
	std::vector<SourceFile*> sourceFiles) {
	// findallreferences.go — refOptions{use: referenceUseNone} (dep stub:
	// GetReferencedSymbolsForNode is owned by ls-coreB)
	auto referenceEntries =
		GetReferencedSymbolsForNode(ctx, position, node, sourceFiles);
	if (referenceEntries.empty()) {
		return {};
	}

	// Group highlights by file
	std::unordered_map<std::string,
					   std::vector<lsp::lsproto::DocumentHighlight*>>
		fileHighlights;
	for (auto* entry : referenceEntries) {
		for (auto* ref : entry->references) {
			auto [fileName, highlight] = toDocumentHighlight(ref);
			if (highlight == nullptr) {
				continue;
			}
			fileHighlights[fileName].push_back(highlight);
		}
	}

	std::vector<lsp::lsproto::MultiDocumentHighlight*> result;
	for (auto* sf : sourceFiles) {
		std::string fileName = sf->OriginalFileName();
		auto it = fileHighlights.find(fileName);
		if (it != fileHighlights.end()) {
			auto* mh = new lsp::lsproto::MultiDocumentHighlight;
			mh->Uri = lsconv::FileNameToDocumentURI(fileName);
			mh->Highlights = it->second;
			result.push_back(mh);
		}
	}
	return result;
}

// documenthighlights.go:164
std::pair<std::string, lsp::lsproto::DocumentHighlight*>
LanguageService::toDocumentHighlight(ReferenceEntry* entry) {
	entry = resolveEntry(entry);
	std::string fileName = entry->sourceFile->OriginalFileName();

	auto* kind =
		new lsp::lsproto::DocumentHighlightKind(
			lsp::lsproto::DocumentHighlightKindRead);
	auto [lspRange, ok] = getRangeOfEntryForFeature(
		entry, spanmap::FeatureDocumentHighlights);
	if (!ok) {
		return {fileName, nullptr};
	}
	if (entry->kind == entryKindRange) {
		auto* dh = new lsp::lsproto::DocumentHighlight;
		dh->Range = lspRange;
		dh->Kind = kind;
		return {fileName, dh};
	}

	// Determine write access for node references.
	if (isWriteAccessForReference(entry->node)) {
		*kind = lsp::lsproto::DocumentHighlightKindWrite;
	}

	auto* dh = new lsp::lsproto::DocumentHighlight;
	dh->Range = lspRange;
	dh->Kind = kind;

	return {fileName, dh};
}

// documenthighlights.go:193
std::vector<lsp::lsproto::DocumentHighlight*>
LanguageService::getSyntacticDocumentHighlights(::tsc::Node* node,
											  SourceFile* sourceFile) {
	switch (node->kind) {
	case Kind::IfKeyword:
	case Kind::ElseKeyword:
		if (isIfStatement(node->parent)) {
			return getIfElseOccurrences(node->parent->as<IfStatement>(),
									  sourceFile);
		}
		return {};
	case Kind::ReturnKeyword:
		return useParent(node->parent, isReturnStatement,
						 getReturnOccurrences, sourceFile);
	case Kind::ThrowKeyword:
		return useParent(node->parent, isThrowStatement,
						 getThrowOccurrences, sourceFile);
	case Kind::TryKeyword:
	case Kind::CatchKeyword:
	case Kind::FinallyKeyword: {
		::tsc::Node* tryStatement = nullptr;
		if (node->kind == Kind::CatchKeyword) {
			tryStatement = node->parent->parent;
		} else {
			tryStatement = node->parent;
		}
		return useParent(tryStatement, isTryStatement,
						 getTryCatchFinallyOccurrences, sourceFile);
	}
	case Kind::SwitchKeyword:
		return useParent(node->parent, isSwitchStatement,
						 getSwitchCaseDefaultOccurrences, sourceFile);
	case Kind::CaseKeyword:
	case Kind::DefaultKeyword:
		if (isDefaultClause(node->parent) || isCaseClause(node->parent)) {
			return useParent(node->parent->parent->parent,
							 isSwitchStatement,
							 getSwitchCaseDefaultOccurrences, sourceFile);
		}
		return {};
	case Kind::BreakKeyword:
	case Kind::ContinueKeyword:
		return useParent(node->parent, isBreakOrContinueStatement,
						 getBreakOrContinueStatementOccurrences,
						 sourceFile);
	case Kind::ForKeyword:
	case Kind::WhileKeyword:
	case Kind::DoKeyword:
		return useParent(
			node->parent,
			[](::tsc::Node* n) { return isIterationStatement(n, true); },
			getLoopBreakContinueOccurrences, sourceFile);
	case Kind::ConstructorKeyword:
		return getFromAllDeclarations(isConstructorDeclaration,
									  {Kind::ConstructorKeyword}, node,
									  sourceFile);
	case Kind::GetKeyword:
	case Kind::SetKeyword:
		return getFromAllDeclarations(
			isAccessor, {Kind::GetKeyword, Kind::SetKeyword}, node,
			sourceFile);
	case Kind::AwaitKeyword:
		return useParent(node->parent, isAwaitExpression,
						 getAsyncAndAwaitOccurrences, sourceFile);
	case Kind::AsyncKeyword:
		return highlightSpans(getAsyncAndAwaitOccurrences(node, sourceFile),
							  sourceFile);
	case Kind::YieldKeyword:
		return highlightSpans(getYieldOccurrences(node, sourceFile),
							  sourceFile);
	case Kind::InKeyword:
	case Kind::OutKeyword:
		return {};
	default:
		if (isModifierKind(node->kind) &&
			(isDeclaration(node->parent) ||
			 isVariableStatement(node->parent))) {
			return highlightSpans(
				getModifierOccurrences(node->kind, node->parent,
									   sourceFile),
				sourceFile);
		}
		return {};
	}
}

// documenthighlights.go:250
std::vector<lsp::lsproto::DocumentHighlight*> LanguageService::useParent(
	::tsc::Node* node, std::function<bool(::tsc::Node*)> nodeTest,
	std::function<std::vector<::tsc::Node*>(::tsc::Node*, SourceFile*)>
		getNodes,
	SourceFile* sourceFile) {
	if (nodeTest(node)) {
		return highlightSpans(getNodes(node, sourceFile), sourceFile);
	}
	return {};
}

// documenthighlights.go:257
std::vector<lsp::lsproto::DocumentHighlight*>
LanguageService::highlightSpans(std::vector<::tsc::Node*> nodes,
								SourceFile* sourceFile) {
	if (nodes.empty()) {
		return {};
	}
	std::vector<lsp::lsproto::DocumentHighlight*> highlights;
	auto* kind = new lsp::lsproto::DocumentHighlightKind(
		lsp::lsproto::DocumentHighlightKindRead);
	for (auto* node : nodes) {
		if (node != nullptr) {
			if (auto [lspRange, fidelity] =
					createLspRangeFromNodeForFeature(
						node, sourceFile,
						spanmap::FeatureDocumentHighlights);
				!fidelity.IsNone()) {
				auto* dh = new lsp::lsproto::DocumentHighlight;
				dh->Range = lspRange;
				dh->Kind = kind;
				highlights.push_back(dh);
			}
		}
	}
	return highlights;
}

// documenthighlights.go:273
std::vector<lsp::lsproto::DocumentHighlight*>
LanguageService::getFromAllDeclarations(
	std::function<bool(::tsc::Node*)> nodeTest, std::vector<Kind> keywords,
	::tsc::Node* node, SourceFile* sourceFile) {
	return useParent(
		node->parent, nodeTest,
		[&](::tsc::Node* decl,
			SourceFile* sf) -> std::vector<::tsc::Node*> {
			std::vector<::tsc::Node*> symbolDecls;
			if (canHaveSymbol(decl)) {
				if (Symbol* symbol = decl->symbol(); symbol != nullptr) {
					for (auto* d : symbol->declarations) {
						if (nodeTest(d)) {
							for (auto* c :
								 getChildrenFromNonJSDocNode(d, sourceFile)) {
								bool found = false;
								for (auto k : keywords) {
									if (c->kind == k) {
										symbolDecls.push_back(c);
										found = true;
										break;
									}
								}
								if (found) {
									break;
								}
							}
						}
					}
				}
			}
			return symbolDecls;
		},
		sourceFile);
}

// documenthighlights.go:301
std::vector<lsp::lsproto::DocumentHighlight*>
LanguageService::getIfElseOccurrences(IfStatement* ifStatement,
									SourceFile* sourceFile) {
	std::vector<::tsc::Node*> keywords =
		getIfElseKeywords(ifStatement, sourceFile);
	auto* kind = new lsp::lsproto::DocumentHighlightKind(
		lsp::lsproto::DocumentHighlightKindRead);
	std::vector<lsp::lsproto::DocumentHighlight*> highlights;

	// We'd like to highlight else/ifs together if they are only separated by whitespace
	// (i.e. the keywords are separated by no comments, no newlines).
	for (size_t i = 0; i < keywords.size(); i++) {
		if (keywords[i]->kind == Kind::ElseKeyword &&
			i < keywords.size() - 1) {
			::tsc::Node* elseKeyword = keywords[i];
			::tsc::Node* ifKeyword = keywords[i + 1]; // this *should* always be an 'if' keyword.
			bool shouldCombine = true;

			// Avoid recalculating getStart() by iterating backwards.
			int ifTokenStart = int(tsc::getTokenPosOfNode(
				ifKeyword, sourceFile, false));
			if (ifTokenStart < 0) {
				ifTokenStart = int(ifKeyword->pos());
			}
			for (int j = ifTokenStart - 1;
				 j >= int(elseKeyword->end()); j--) {
				if (!isWhiteSpaceSingleLine(
						char32_t(sourceFile->Text()[size_t(j)]))) {
					shouldCombine = false;
					break;
				}
			}
			if (shouldCombine) {
				auto [lspRange, fidelity] = createLspRangeFromBounds(
					int(tsc::skipTrivia(sourceFile->Text(),
											int(elseKeyword->pos()))),
					int(ifKeyword->end()), sourceFile);
				if (!fidelity.IsNone()) {
					auto* dh = new lsp::lsproto::DocumentHighlight;
					dh->Range = lspRange;
					dh->Kind = kind;
					highlights.push_back(dh);
				}
				i++; // skip the next keyword
				continue;
			}
		}
		// Ordinary case: just highlight the keyword.
		if (auto [lspRange, fidelity] =
				createLspRangeFromNodeForFeature(
					keywords[i], sourceFile,
					spanmap::FeatureDocumentHighlights);
			!fidelity.IsNone()) {
			auto* dh = new lsp::lsproto::DocumentHighlight;
			dh->Range = lspRange;
			dh->Kind = kind;
			highlights.push_back(dh);
		}
	}
	return highlights;
}

namespace {

// documenthighlights.go:338 — getIfElseKeywords
std::vector<::tsc::Node*> getIfElseKeywords(IfStatement* ifStatement,
										  SourceFile* sourceFile) {
	// We may be at an if statement like those in the range below:
	//
	//   ```
	//   if (...) {
	//   } else [|if (...) {}|]
	//   ````
	//
	// Traverse upwards through all parent if-statements linked by their else-branches.
	while (isIfStatement(ifStatement->parent)) {
		// See if the parent's `else` is actually the current `if` statement.
		IfStatement* parentingIf = ifStatement->parent->as<IfStatement>();
		::tsc::Node* elseStatement = parentingIf->ElseStatement;
		if (elseStatement != ifStatement->asNode()) {
			break;
		}
		ifStatement = parentingIf;
	}

	std::vector<::tsc::Node*> keywords;

	// Traverse back down through the else branches, aggregating if/else keywords of if-statements.
	for (;;) {
		auto children =
			getChildrenFromNonJSDocNode(ifStatement->asNode(), sourceFile);
		if (!children.empty() &&
			children[0]->kind == Kind::IfKeyword) {
			keywords.push_back(children[0]);
		}
		// Generally the 'else' keyword is second-to-last, so traverse backwards.
		for (auto it = children.rbegin(); it != children.rend(); ++it) {
			if ((*it)->kind == Kind::ElseKeyword) {
				keywords.push_back(*it);
				break;
			}
		}
		::tsc::Node* elseStatement = ifStatement->ElseStatement;
		if (elseStatement == nullptr ||
			!isIfStatement(elseStatement)) {
			break;
		}
		ifStatement = elseStatement->as<IfStatement>();
	}
	return keywords;
}

} // namespace

} // namespace tsc::ls
