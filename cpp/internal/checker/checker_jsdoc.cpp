// Port of tsc/internal/checker/jsdoc.go — the JSDoc-parameter checking glue.
#include "internal/checker/checker.h"

#include <unordered_set>

#include "internal/diagnostics/messages_generated.h"

namespace tsc::checker {

// ---------------------------------------------------------------------------
// Helpers ported file-locally until their own slices land:
//   nodeStartsNewLexicalEnvironment — utilities.go:1763
//   GetNextJSDocCommentLocation     — ast/utilities.go:4100
// ---------------------------------------------------------------------------

// utilities.go:1763
bool nodeStartsNewLexicalEnvironment(Node* node) {
	switch (node->kind) {
		case Kind::Constructor:
		case Kind::FunctionExpression:
		case Kind::FunctionDeclaration:
		case Kind::ArrowFunction:
		case Kind::MethodDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::ModuleDeclaration:
		case Kind::SourceFile:
			return true;
		default:
			break;
	}
	return false;
}

// ast/utilities.go:4100
static Node* getNextJSDocCommentLocation(Node* node) {
	if (Node* parent = node->parent) {
		switch (parent->kind) {
			case Kind::PropertyAssignment:
			case Kind::ExportAssignment:
			case Kind::PropertyDeclaration:
			case Kind::VariableDeclaration:
			case Kind::SatisfiesExpression:
			case Kind::ReturnStatement:
			case Kind::VariableStatement:
			case Kind::ExpressionStatement:
				return parent;
			case Kind::VariableDeclarationList:
				if (parent->as<VariableDeclarationList>()->Declarations->nodes[0] == node) {
					return parent;
				}
				break;
			default:
				break;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// jsdoc.go — checker methods
// ---------------------------------------------------------------------------

// jsdoc.go:9
void Checker::checkUnmatchedJSDocParameters(Node* node) {
	std::vector<Node*> jsdocParameters;
	for (Node* tag : getAllJSDocTags(node)) {
		if (tag->kind == Kind::JSDocParameterTag) {
			Node* name = tag->as<JSDocParameterOrPropertyTag>()->name;
			if (name->kind == Kind::Identifier && name->text().empty()) {
				continue;
			}
			jsdocParameters.push_back(tag);
		}
	}

	if (jsdocParameters.empty()) {
		return;
	}

	bool isJs = isInJSFile(node);
	std::unordered_set<std::string> parameters;
	std::unordered_set<int> excludedParameters;

	int i = 0;
	for (Node* param : node->parameters()) {
		Node* name = param->as<ParameterDeclaration>()->name;
		if (name->kind == Kind::Identifier) {
			parameters.insert(name->text());
		}
		if (isBindingPattern(name)) {
			excludedParameters.insert(i);
		}
		i++;
	}
	if (containsArgumentsReference(node)) {
		if (isJs) {
			int lastJSDocParamIndex = static_cast<int>(jsdocParameters.size()) - 1;
			JSDocParameterOrPropertyTag* lastJSDocParam =
				jsdocParameters[lastJSDocParamIndex]->as<JSDocParameterOrPropertyTag>();
			if (lastJSDocParam == nullptr ||
				lastJSDocParam->name->kind != Kind::Identifier) {
				return;
			}
			if (excludedParameters.count(lastJSDocParamIndex) != 0 ||
				parameters.count(lastJSDocParam->name->text()) != 0) {
				return;
			}
			if (lastJSDocParam->TypeExpression == nullptr ||
				lastJSDocParam->TypeExpression->type() == nullptr) {
				return;
			}
			if (isArrayType(getTypeFromTypeNode(lastJSDocParam->TypeExpression->type()))) {
				return;
			}
			error(lastJSDocParam->name,
				  JSDoc_param_tag_has_name_0_but_there_is_no_parameter_with_that_name_It_would_match_arguments_if_it_had_an_array_type,
				  {lastJSDocParam->name->text()});
		}
	} else {
		int index = 0;
		for (Node* tag : jsdocParameters) {
			Node* name = tag->as<JSDocParameterOrPropertyTag>()->name;
			bool isNameFirst = tag->as<JSDocParameterOrPropertyTag>()->IsNameFirst;

			if (excludedParameters.count(index) != 0 ||
				(name->kind == Kind::Identifier &&
				 parameters.count(name->text()) != 0)) {
				index++;
				continue;
			}

			if (name->kind == Kind::QualifiedName) {
				if (isJs) {
					error(name,
						  Qualified_name_0_is_not_allowed_without_a_leading_param_object_1,
						  {entityNameToString(name),
						   entityNameToString(name->as<QualifiedName>()->Left)});
				}
			} else {
				if (!isNameFirst) {
					errorOrSuggestion(
						isJs, name,
						JSDoc_param_tag_has_name_0_but_there_is_no_parameter_with_that_name,
						{name->text()});
				}
			}
			index++;
		}
	}
}

// jsdoc.go:86
std::vector<Node*> getAllJSDocTags(Node* node) {
	if ((node->flags & NodeFlagsJSDoc) == 0) {
		for (Node* current = node; current != nullptr;
			 current = getNextJSDocCommentLocation(current)) {
			std::vector<Node*> jsdocs = current->jsDoc();
			if (jsdocs.empty()) {
				continue;
			}
			JSDoc* lastJSDoc = jsdocs.back()->as<JSDoc>();
			if (lastJSDoc->Tags != nullptr) {
				return lastJSDoc->Tags->nodes;
			}
		}
	}
	return {};
}

// ---------------------------------------------------------------------------
// checker.go / services.go / utilities.go deps needed by the JSDoc path.
// ---------------------------------------------------------------------------

// checker.go:23938
bool Checker::isArrayType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
		(t->Target() == globalArrayType || t->Target() == globalReadonlyArrayType);
}

// checker.go:32611
// (deduped: containsArgumentsReference real def in checker_services.cpp)

// services.go:240 — originally from services.ts
bool Checker::IsArgumentsSymbol(Symbol* symbol) {
	return symbol == argumentsSymbol;
}

} // namespace tsc::checker
