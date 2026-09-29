// Port of tsc/internal/parser/reparser.go — JSDoc reparsing: typedefs,
// callbacks, overloads, implements/augments, and comment-driven mutation of
// JS declarations.
//
// Conventions: Go `p.nodeSliceArena.NewSlice(n)` -> plain std::vector (arena
// slicing is a documented perf TODO); `asMutable().SetX` -> `x->setX`.

#include "internal/parser/parser.h"

#include <functional>
#include <string>
#include <vector>

namespace tsc {

void Parser::finishReparsedNode(Node* node, Node* locationNode) {
	node->flags = contextFlags | NodeFlagsReparsed;
	node->loc = locationNode->loc;
	overrideParentInImmediateChildren(node);
}

void Parser::finishMutatedNode(Node* node) {
	overrideParentInImmediateChildren(node);
}

// Deep-clone the given node and add the clone to the reparsed clone list.
// The list is used by ast.GetReparsedNodeForNode to locate reparsed clones
// of JSDoc nodes. Since the binder attaches symbols to reparsed nodes and
// not to JSDoc nodes, we need the mapping when obtaining symbols and types
// from JSDoc nodes.
Node* Parser::addDeepCloneReparse(Node* node) {
	Node* clone = factory.deepCloneReparse(node);
	if (clone != nullptr) {
		reparsedClones.push_back(clone);
	}
	return clone;
}

Node* Parser::addTransformedReparse(Node* newNode, Node* old) {
	finishReparsedNode(newNode, old);
	newNode->flags |= NodeFlagsReparserTransformedLiteral;
	reparsedClones.push_back(newNode);
	return newNode;
}

Node* Parser::checkNonIdentifierName(Node* name) {
	// Handles the case of anonymous functions
	if (name == nullptr) {
		return nullptr;
	}
	if (::tsc::isIdentifier(name) && !isValidIdentifier(name->text())) {
		TextRange errLoc = name->loc;
		if (errLoc.len() == 0) {
			// missing name, emit error on the character before the missing
			// name node
			errLoc = TextRange{name->loc.pos() - 1, name->loc.pos()};
		}
		parseErrorAtRange(errLoc, Identifier_expected);
	}
	return name;
}

// Hosted tags find a host and add their children to the correct location
// under the host.
// Unhosted tags add synthetic nodes to the reparse list.
void Parser::reparseTags(Node* parent, std::vector<Node*> jsDoc) {
	for (Node* j : jsDoc) {
		bool isLast = j == jsDoc.back();
		NodeList* tags = j->as<JSDoc>()->Tags;
		if (tags == nullptr) {
			continue;
		}
		for (Node* tag : tags->nodes) {
			reparseUnhosted(tag, parent, j);
			if (isLast) {
				reparseHosted(tag, parent, j);
			}
		}
	}
}

void Parser::reparseUnhosted(Node* tag, Node* parent, Node* jsDoc) {
	switch (tag->kind) {
	case Kind::JSDocTypedefTag: {
		Node* typeExpression = tag->typeExpression();
		if (typeExpression == nullptr) {
			break;
		}
		Node* fullName = tag->name();
		bool isNamespace =
			fullName != nullptr && isModuleDeclaration(fullName);
		ModifierList* modifiers = nullptr;
		if (isNamespace) {
			modifiers = createExportModifier(tag);
		}
		Node* typeAlias = factory.newJSTypeAliasDeclaration(
			modifiers,
			addDeepCloneReparse(checkNonIdentifierName(
				getInnermostNameOfJSDocNamespace(fullName))),
			nullptr, nullptr);
		typeAlias->as<TypeAliasDeclaration>()->TypeParameters =
			gatherTypeParameters(jsDoc, true /*typedefOrCallback*/);
		Node* t;
		switch (typeExpression->kind) {
		case Kind::JSDocTypeExpression:
			t = addDeepCloneReparse(typeExpression->type());
			break;
		case Kind::JSDocTypeLiteral:
			t = reparseJSDocTypeLiteral(typeExpression);
			break;
		default:
			TSC_UNREACHABLE(
				"typedef tag type expression should be a name reference "
				"or a type expression");
		}
		typeAlias->as<TypeAliasDeclaration>()->Type = t;
		finishReparsedNode(typeAlias, tag);
		jsdocInfos.push_back(JSDocInfo{typeAlias, {jsDoc}});
		typeAlias->flags |= NodeFlagsHasJSDoc;
		Node* result =
			wrapInJSDocNamespace(fullName, typeAlias, false /*nested*/);
		reparseList.push_back(result);
		break;
	}
	case Kind::JSDocCallbackTag: {
		Node* typeExpression = tag->typeExpression();
		if (typeExpression == nullptr) {
			break;
		}
		Node* fullName = tag->name();
		bool isNamespace =
			fullName != nullptr && isModuleDeclaration(fullName);
		ModifierList* modifiers = nullptr;
		if (isNamespace) {
			modifiers = createExportModifier(tag);
		}
		Node* functionType = reparseJSDocSignature(typeExpression, tag,
		                                           jsDoc, tag, nullptr);
		Node* typeAlias = factory.newJSTypeAliasDeclaration(
			modifiers,
			addDeepCloneReparse(
				getInnermostNameOfJSDocNamespace(fullName)),
			nullptr, functionType);
		typeAlias->as<TypeAliasDeclaration>()->TypeParameters =
			gatherTypeParameters(jsDoc, true /*typedefOrCallback*/);
		finishReparsedNode(typeAlias, tag);
		jsdocInfos.push_back(JSDocInfo{typeAlias, {jsDoc}});
		typeAlias->flags |= NodeFlagsHasJSDoc;
		Node* result =
			wrapInJSDocNamespace(fullName, typeAlias, false /*nested*/);
		reparseList.push_back(result);
		break;
	}
	case Kind::JSDocImportTag: {
		auto* importTag = tag->as<JSDocImportTag>();
		if (importTag->ImportClause == nullptr) {
			break;
		}
		Node* importClause =
			addDeepCloneReparse(importTag->ImportClause);
		importClause->as<ImportClause>()->PhaseModifier =
			Kind::TypeKeyword;
		Node* importDeclaration = factory.newJSImportDeclaration(
			factory.deepCloneReparseModifiers(importTag->modifiers()),
			importClause, addDeepCloneReparse(importTag->ModuleSpecifier),
			addDeepCloneReparse(importTag->Attributes));
		finishReparsedNode(importDeclaration, tag);
		reparseList.push_back(importDeclaration);
		break;
	}
	case Kind::JSDocOverloadTag:
		// Create overload signatures only for function, method, and
		// constructor declarations outside object literals
		if ((isFunctionDeclaration(parent) || isMethodDeclaration(parent) ||
		     isConstructorDeclaration(parent)) &&
		    (parsingContexts & (1 << PCObjectLiteralMembers)) == 0) {
			reparseList.push_back(reparseJSDocSignature(
				tag->as<JSDocOverloadTag>()->TypeExpression, parent,
				jsDoc, tag, parent->modifiers()));
		}
		break;
	default:
		break;
	}
}

Node* Parser::reparseJSDocSignature(Node* jsSignature, Node* fun,
                                    Node* jsDoc, Node* tag,
                                    ModifierList* modifiers) {
	Node* signature;
	ModifierList* clonedModifiers =
		factory.deepCloneReparseModifiers(modifiers);
	switch (fun->kind) {
	case Kind::FunctionDeclaration:
		signature = factory.newFunctionDeclaration(
			clonedModifiers, nullptr,
			factory.deepCloneReparse(
				checkNonIdentifierName(fun->name())),
			nullptr, nullptr, nullptr, nullptr, nullptr);
		break;
	case Kind::MethodDeclaration:
		signature = factory.newMethodDeclaration(
			clonedModifiers, nullptr,
			factory.deepCloneReparse(
				checkNonIdentifierName(fun->name())),
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		break;
	case Kind::Constructor:
		signature = factory.newConstructorDeclaration(
			clonedModifiers, nullptr, nullptr, nullptr, nullptr, nullptr);
		break;
	case Kind::JSDocCallbackTag:
		signature = factory.newFunctionTypeNode(
			nullptr, nullptr,
			factory.newKeywordTypeNode(Kind::AnyKeyword));
		break;
	default:
		TSC_UNREACHABLE("Unexpected kind in reparseJSDocSignature");
	}

	if (tag->kind != Kind::JSDocCallbackTag) {
		*signature->functionLikeData().typeParameters =
			gatherTypeParameters(jsDoc, false /*typedefOrCallback*/);
	}
	std::vector<Node*> parameters;
	auto params = jsSignature->parameters();
	for (size_t pi = 0; pi < params.size(); pi++) {
		Node* param = params[pi];
		Node* parameter = nullptr;
		if (param->kind == Kind::JSDocThisTag) {
			auto* thisTag = param->as<JSDocThisTag>();
			Node* thisIdent = factory.newIdentifier("this");
			thisIdent->loc = thisTag->loc;
			thisIdent->flags = contextFlags | NodeFlagsReparsed;
			parameter = factory.newParameterDeclaration(
				nullptr, nullptr, thisIdent, nullptr, nullptr, nullptr);
			if (thisTag->TypeExpression != nullptr) {
				parameter->as<ParameterDeclaration>()->Type =
					addDeepCloneReparse(
						thisTag->TypeExpression->type());
			}
		} else if (param->kind == Kind::JSDocParameterTag ||
		           param->kind == Kind::JSDocPropertyTag) {
			auto* jsparam = param->as<JSDocParameterOrPropertyTag>();
			// Skip sub-property parameters (e.g., @param x.y) - these
			// have QualifiedNames and describe properties of a parent
			// parameter, not standalone parameters.
			if (isQualifiedName(jsparam->name)) {
				continue;
			}
			Node* dotDotDotToken = nullptr;
			Node* paramType = nullptr;

			if (jsparam->TypeExpression != nullptr) {
				if (jsparam->TypeExpression->type()->kind ==
				    Kind::JSDocVariadicType) {
					dotDotDotToken =
						factory.newToken(Kind::DotDotDotToken);
					dotDotDotToken->loc = jsparam->loc;
					dotDotDotToken->flags =
						contextFlags | NodeFlagsReparsed;

					auto* variadicType = jsparam->TypeExpression->type()
					                         ->as<JSDocVariadicType>();
					paramType =
						reparseJSDocTypeLiteral(variadicType->Type);
				} else {
					paramType = reparseJSDocTypeLiteral(
						jsparam->TypeExpression->type());
				}
			}
			Node* name = jsparam->name;
			if (::tsc::isIdentifier(name) &&
			    !isValidIdentifier(name->text())) {
				// drop invalid chars for _, if empty, write _0, etc., so
				// we have a valid param name to emit later
				std::string result;
				bool first = true;
				for (size_t i = 0; i < name->text().size();) {
					int width;
					char32_t ch = decodeUtf8Rune(
						std::string_view(name->text()).substr(i),
						&width);
					i += width;
					if (first) {
						if (!isIdentifierStart(ch)) {
							result += '_';
						} else {
							char buf[4];
							int n = encodeUtf8Rune(ch, buf);
							result.append(buf, n);
						}
						first = false;
						continue;
					}
					if (!isIdentifierPart(ch)) {
						result += '_';
					} else {
						char buf[4];
						int n = encodeUtf8Rune(ch, buf);
						result.append(buf, n);
					}
				}
				if (result.empty()) {
					result = "_" + std::to_string(pi);
				}
				name = addTransformedReparse(
					factory.newIdentifier(std::move(result)), name);
			} else {
				name = addDeepCloneReparse(name);
			}
			parameter = factory.newParameterDeclaration(
				nullptr, dotDotDotToken, name,
				makeQuestionIfOptional(jsparam), paramType, nullptr);
		}
		finishReparsedNode(parameter, param);
		parameters.push_back(parameter);
		reparseJSDocComment(parameter, param);
	}
	*signature->functionLikeData().parameters = newNodeList(
		jsSignature->as<JSDocSignature>()->Parameters->loc, parameters);

	if (jsSignature->type() != nullptr &&
	    jsSignature->type()->typeExpression() != nullptr) {
		*signature->functionLikeData().type = addDeepCloneReparse(
			jsSignature->type()->typeExpression()->type());
	}
	Node* loc = jsSignature;
	if (tag->kind == Kind::JSDocOverloadTag) {
		loc = tag->tagName();
	}
	finishReparsedNode(signature, loc);
	return signature;
}

Node* Parser::reparseJSDocTypeLiteral(Node* t) {
	if (t == nullptr) {
		return nullptr;
	}
	if (t->kind == Kind::JSDocTypeLiteral) {
		auto* jstypeliteral = t->as<JSDocTypeLiteral>();
		bool isArrayType = jstypeliteral->IsArrayType;
		std::vector<Node*> properties;
		for (Node* prop : jstypeliteral->JSDocPropertyTags) {
			if (prop->kind != Kind::JSDocPropertyTag &&
			    prop->kind != Kind::JSDocParameterTag) {
				continue;
			}
			auto* jsprop = prop->as<JSDocParameterOrPropertyTag>();
			Node* name = prop->name();
			if (name->kind == Kind::QualifiedName) {
				name = name->as<QualifiedName>()->Right;
			}
			if (::tsc::isIdentifier(name) &&
			    !isValidIdentifier(name->text())) {
				name = addTransformedReparse(
					factory.newStringLiteral(name->text(),
					                         TokenFlagsNone),
					name);
			} else {
				name = addDeepCloneReparse(name);
			}
			Node* property = factory.newPropertySignatureDeclaration(
				nullptr, name, makeQuestionIfOptional(jsprop), nullptr,
				nullptr);
			if (jsprop->TypeExpression != nullptr) {
				property->as<PropertySignatureDeclaration>()->Type =
					reparseJSDocTypeLiteral(
						jsprop->TypeExpression->type());
			}
			finishReparsedNode(property, prop);
			properties.push_back(property);
			reparseJSDocComment(property, prop);
		}
		t = factory.newTypeLiteralNode(
			newNodeList(jstypeliteral->loc, properties));
		if (isArrayType) {
			finishReparsedNode(t, jstypeliteral->asNode());
			t = factory.newArrayTypeNode(t);
		}
		finishReparsedNode(t, jstypeliteral->asNode());
		return t;
	}
	return addDeepCloneReparse(t);
}

void Parser::reparseJSDocComment(Node* node, Node* tag) {
	if (NodeList* comment = tag->commentList(); comment != nullptr) {
		std::vector<Node*> cloned;
		cloned.reserve(comment->nodes.size());
		for (Node* n : comment->nodes) {
			cloned.push_back(factory.deepCloneReparse(n));
		}
		NodeList* newComment = factory.newNodeList(std::move(cloned));
		newComment->loc = comment->loc;
		Node* propJSDoc = factory.newJSDoc(newComment, nullptr);
		finishReparsedNode(propJSDoc, tag);
		propJSDoc->parent = node;
		jsdocInfos.push_back(JSDocInfo{node, {propJSDoc}});
		node->flags |= NodeFlagsHasJSDoc;
	}
}

NodeList* Parser::gatherTypeParameters(Node* j, bool typedefOrCallback) {
	std::vector<Node*> typeParameters;
	int pos = -1;
	int endPos = -1;
	bool firstTemplate = true;
	for (Node* tag : j->as<JSDoc>()->Tags->nodes) {
		// When a JSDoc comment contains an `@typedef` or `@callback` tag,
		// `@template` type parameter declarations apply to the type being
		// defined.
		if (!typedefOrCallback &&
		    (isJSDocTypedefTag(tag) || isJSDocCallbackTag(tag))) {
			return nullptr;
		}
		if (!isJSDocTemplateTag(tag)) {
			continue;
		}
		if (firstTemplate) {
			pos = tag->pos();
			firstTemplate = false;
		}
		endPos = tag->end();
		Node* constraint = tag->as<JSDocTemplateTag>()->Constraint;
		bool firstTypeParameter = true;
		for (Node* tp : tag->typeParameters()) {
			Node* reparse;
			if (constraint != nullptr && firstTypeParameter) {
				reparse = factory.newTypeParameterDeclaration(
					factory.deepCloneReparseModifiers(tp->modifiers()),
					addDeepCloneReparse(
						checkNonIdentifierName(tp->name())),
					addDeepCloneReparse(constraint->type()),
					nullptr,  // expression
					addDeepCloneReparse(
						tp->as<TypeParameterDeclaration>()
							->DefaultType));
				finishReparsedNode(reparse, tp);
			} else {
				reparse = addDeepCloneReparse(tp);
			}
			typeParameters.push_back(reparse);
			firstTypeParameter = false;
		}
	}
	if (typeParameters.empty()) {
		return nullptr;
	}
	return newNodeList(TextRange{pos, endPos}, typeParameters);
}

// ---------------------------------------------------------------------------
// file-local helpers (Go free functions)
// ---------------------------------------------------------------------------

static Node* skipSatisfiesExpressions(Node* node) {
	while (node != nullptr && node->kind == Kind::SatisfiesExpression) {
		node = node->expression();
	}
	return node;
}

static Node* getFunctionLikeHost(Node* host) {
	Node* fun = host;
	switch (host->kind) {
	case Kind::VariableStatement: {
		auto& nodes = host->as<VariableStatement>()
		                  ->DeclarationList->as<VariableDeclarationList>()
		                  ->Declarations->nodes;
		if (!nodes.empty()) {
			fun = nodes[0]->initializer();
		}
		break;
	}
	case Kind::PropertyAssignment:
	case Kind::PropertyDeclaration:
		fun = host->initializer();
		break;
	case Kind::ExportAssignment:
	case Kind::ReturnStatement:
		fun = host->expression();
		break;
	case Kind::ExpressionStatement:
		fun = getRightMostAssignedExpression(host->expression());
		break;
	default:
		break;
	}
	fun = skipSatisfiesExpressions(fun);
	if (isFunctionLike(fun)) {
		return fun;
	}
	return nullptr;
}

static ClassLikeDataRef getClassLikeData(Node* parent) {
	ClassLikeDataRef cls{};
	switch (parent->kind) {
	case Kind::ClassDeclaration:
		cls = parent->as<ClassDeclaration>()->classLikeData();
		break;
	case Kind::ClassExpression:
		cls = parent->as<ClassExpression>()->classLikeData();
		break;
	default:
		break;
	}
	return cls;
}

static std::pair<ParameterDeclaration*, bool> findMatchingParameter(
	Node* fun, JSDocParameterOrPropertyTag* parameterTag, Node* jsDoc) {
	int tagIndex = -1;
	int paramCount = -1;
	for (Node* tag : jsDoc->as<JSDoc>()->Tags->nodes) {
		if (tag->kind == Kind::JSDocParameterTag) {
			paramCount++;
			if (tag->as<JSDocParameterOrPropertyTag>() == parameterTag) {
				tagIndex = paramCount;
				break;
			}
		}
	}
	auto params = fun->parameters();
	for (size_t parameterIndex = 0; parameterIndex < params.size();
	     parameterIndex++) {
		Node* parameter = params[parameterIndex];
		if (parameter->name()->kind == Kind::Identifier) {
			if (parameterTag->name->kind == Kind::Identifier &&
			    (parameter->name()->text() ==
			         parameterTag->name->text() ||
			     ((int)parameterIndex == tagIndex &&
			      parameterTag->name->text().empty()))) {
				return {parameter->as<ParameterDeclaration>(), true};
			}
		} else if ((int)parameterIndex == tagIndex) {
			return {parameter->as<ParameterDeclaration>(), true};
		}
	}
	return {nullptr, false};
}

void Parser::reparseHosted(Node* tag, Node* parent, Node* jsDoc) {
	switch (tag->kind) {
	case Kind::JSDocTypeTag:
		switch (parent->kind) {
		case Kind::VariableStatement:
			if (parent->as<VariableStatement>()->DeclarationList !=
			    nullptr) {
				for (Node* declaration :
				     parent->as<VariableStatement>()
					     ->DeclarationList->as<VariableDeclarationList>()
					     ->Declarations->nodes) {
					if (declaration->type() == nullptr &&
					    tag->typeExpression() != nullptr) {
						declaration->setType(addDeepCloneReparse(
							tag->typeExpression()->type()));
						finishMutatedNode(declaration);
						return;
					}
				}
			}
			break;
		case Kind::VariableDeclaration:
		case Kind::ExportAssignment:
		case Kind::PropertyDeclaration:
		case Kind::PropertyAssignment:
		case Kind::ShorthandPropertyAssignment:
		case Kind::GetAccessor:
			if (parent->type() == nullptr &&
			    tag->typeExpression() != nullptr) {
				parent->setType(
					addDeepCloneReparse(tag->typeExpression()->type()));
				finishMutatedNode(parent);
				return;
			}
			break;
		case Kind::Parameter:
			if (parent->type() == nullptr &&
			    tag->typeExpression() != nullptr) {
				parent->setType(reparseJSDocTypeLiteral(
					tag->typeExpression()->type()));
				finishMutatedNode(parent);
				return;
			}
			break;
		case Kind::ExpressionStatement:
			if (parent->expression()->kind == Kind::BinaryExpression) {
				auto* bin = parent->expression()->as<BinaryExpression>();
				JSDeclarationKind kind =
					getAssignmentDeclarationKind(bin->asNode());
				if (kind != JSDeclarationKind::None &&
				    tag->typeExpression() != nullptr) {
					bin->setType(addDeepCloneReparse(
						tag->typeExpression()->type()));
					finishMutatedNode(bin->asNode());
					return;
				}
			}
			break;
		case Kind::ReturnStatement:
		case Kind::ParenthesizedExpression:
			if (parent->expression() != nullptr &&
			    tag->typeExpression() != nullptr) {
				parent->setExpression(makeNewCast(
					addDeepCloneReparse(tag->typeExpression()->type()),
					parent->expression(), true /*isAssertion*/));
				finishMutatedNode(parent);
				return;
			}
			break;
		default:
			break;
		}
		if (Node* fun = getFunctionLikeHost(parent); fun != nullptr) {
			bool noTypedParams = true;
			for (Node* param : fun->parameters()) {
				if (param->type() != nullptr) {
					noTypedParams = false;
					break;
				}
			}
			if (fun->typeParameterList() == nullptr &&
			    fun->type() == nullptr && noTypedParams &&
			    tag->typeExpression() != nullptr) {
				*fun->functionLikeData().fullSignature =
					addDeepCloneReparse(tag->typeExpression()->type());
				finishMutatedNode(fun);
			}
		}
		break;
	case Kind::JSDocSatisfiesTag:
		switch (parent->kind) {
		case Kind::VariableStatement:
			if (parent->as<VariableStatement>()->DeclarationList !=
			    nullptr) {
				for (Node* declaration :
				     parent->as<VariableStatement>()
					     ->DeclarationList->as<VariableDeclarationList>()
					     ->Declarations->nodes) {
					if (declaration->initializer() != nullptr &&
					    tag->typeExpression() != nullptr) {
						declaration->setInitializer(makeNewCast(
							addDeepCloneReparse(
								tag->typeExpression()->type()),
							declaration->initializer(),
							false /*isAssertion*/));
						finishMutatedNode(declaration);
						break;
					}
				}
			}
			break;
		case Kind::VariableDeclaration:
		case Kind::PropertyDeclaration:
		case Kind::PropertyAssignment:
			if (parent->initializer() != nullptr &&
			    tag->typeExpression() != nullptr) {
				parent->setInitializer(makeNewCast(
					addDeepCloneReparse(tag->typeExpression()->type()),
					parent->initializer(), false /*isAssertion*/));
				finishMutatedNode(parent);
			}
			break;
		case Kind::ShorthandPropertyAssignment: {
			auto* shorthand = parent->as<ShorthandPropertyAssignment>();
			if (shorthand->ObjectAssignmentInitializer != nullptr &&
			    tag->as<JSDocSatisfiesTag>()->TypeExpression !=
			        nullptr) {
				shorthand->ObjectAssignmentInitializer = makeNewCast(
					addDeepCloneReparse(
						tag->as<JSDocSatisfiesTag>()
							->TypeExpression->type()),
					shorthand->ObjectAssignmentInitializer,
					false /*isAssertion*/);
				finishMutatedNode(parent);
			}
			break;
		}
		case Kind::ReturnStatement:
		case Kind::ParenthesizedExpression:
		case Kind::ExportAssignment:
			if (parent->expression() != nullptr &&
			    tag->typeExpression() != nullptr) {
				parent->setExpression(makeNewCast(
					addDeepCloneReparse(tag->typeExpression()->type()),
					parent->expression(), false /*isAssertion*/));
				finishMutatedNode(parent);
			}
			break;
		case Kind::ExpressionStatement:
			if (parent->expression()->kind == Kind::BinaryExpression) {
				auto* bin = parent->expression()->as<BinaryExpression>();
				JSDeclarationKind kind =
					getAssignmentDeclarationKind(bin->asNode());
				if (kind != JSDeclarationKind::None &&
				    tag->typeExpression() != nullptr) {
					bin->Right = makeNewCast(
						addDeepCloneReparse(
							tag->typeExpression()->type()),
						bin->Right, false /*isAssertion*/);
					finishMutatedNode(bin->asNode());
				}
			}
			break;
		default:
			break;
		}
		break;
	case Kind::JSDocTemplateTag:
		if (Node* fun = getFunctionLikeHost(parent); fun != nullptr) {
			if (fun->typeParameters().empty() &&
			    *fun->functionLikeData().fullSignature == nullptr) {
				*fun->functionLikeData().typeParameters =
					gatherTypeParameters(jsDoc,
					                     false /*typedefOrCallback*/);
				finishMutatedNode(fun);
			}
		} else if (parent->kind == Kind::ClassDeclaration) {
			auto* class_ = parent->as<ClassDeclaration>();
			if (class_->TypeParameters == nullptr) {
				class_->TypeParameters = gatherTypeParameters(
					jsDoc, false /*typedefOrCallback*/);
				finishMutatedNode(parent);
			}
		} else if (parent->kind == Kind::ClassExpression) {
			auto* class_ = parent->as<ClassExpression>();
			if (class_->TypeParameters == nullptr) {
				class_->TypeParameters = gatherTypeParameters(
					jsDoc, false /*typedefOrCallback*/);
				finishMutatedNode(parent);
			}
		}
		break;
	case Kind::JSDocParameterTag:
		if (Node* fun = getFunctionLikeHost(parent);
		    fun != nullptr &&
		    *fun->functionLikeData().fullSignature == nullptr) {
			auto* parameterTag =
				tag->as<JSDocParameterOrPropertyTag>();
			if (auto [param, ok] = findMatchingParameter(
			        fun, parameterTag, jsDoc);
			    ok) {
				if (param->Type == nullptr &&
				    parameterTag->TypeExpression != nullptr) {
					param->Type = reparseJSDocTypeLiteral(
						parameterTag->TypeExpression->type());
				}
				if (param->QuestionToken == nullptr) {
					if (Node* question =
					        makeQuestionIfOptional(tag);
					    question != nullptr) {
						param->QuestionToken = question;
					}
				}
				finishMutatedNode(param->asNode());
			}
		}
		break;
	case Kind::JSDocThisTag:
		if (Node* fun = getFunctionLikeHost(parent); fun != nullptr) {
			auto params = fun->parameters();
			if (params.empty() ||
			    (params[0]->name()->kind != Kind::ThisKeyword &&
			     !isThisIdentifier(params[0]->name()))) {
				Node* thisParam = factory.newParameterDeclaration(
					nullptr, /* decorators */
					nullptr, /* modifiers */
					factory.newIdentifier("this"),
					nullptr, /* questionToken */
					nullptr, /* type */
					nullptr /* initializer */);
				if (tag->as<JSDocThisTag>()->TypeExpression !=
				    nullptr) {
					thisParam->as<ParameterDeclaration>()->Type =
						addDeepCloneReparse(
							tag->as<JSDocThisTag>()
								->TypeExpression->type());
				}
				finishReparsedNode(thisParam, tag->tagName());

				std::vector<Node*> newParams(params.size() + 1);
				newParams[0] = thisParam;
				for (size_t i = 0; i < params.size(); i++) {
					newParams[i + 1] = params[i];
				}

				*fun->functionLikeData().parameters =
					newNodeList(fun->parameterList()->loc, newParams);
				finishMutatedNode(fun);
			}
		}
		break;
	case Kind::JSDocReturnTag:
		if (Node* fun = getFunctionLikeHost(parent);
		    fun != nullptr &&
		    *fun->functionLikeData().fullSignature == nullptr) {
			if (fun->type() == nullptr &&
			    tag->typeExpression() != nullptr) {
				*fun->functionLikeData().type = addDeepCloneReparse(
					tag->typeExpression()->type());
				finishMutatedNode(fun);
			}
		}
		break;
	case Kind::JSDocReadonlyTag:
	case Kind::JSDocPrivateTag:
	case Kind::JSDocPublicTag:
	case Kind::JSDocProtectedTag:
	case Kind::JSDocOverrideTag: {
		if (parent->kind == Kind::ExpressionStatement) {
			parent = parent->expression();
		}
		switch (parent->kind) {
		case Kind::MethodDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
			// In object literals these aren't class-like members, so
			// JSDoc modifiers like @override or @readonly aren't real
			// modifiers there; reparsing them produces spurious grammar
			// errors (#4437).
			if ((parsingContexts & (1 << PCObjectLiteralMembers)) !=
			    0) {
				return;
			}
			[[fallthrough]];
		case Kind::PropertyDeclaration:
		case Kind::Constructor:
		case Kind::BinaryExpression: {
			Kind keyword = Kind::Unknown;
			switch (tag->kind) {
			case Kind::JSDocReadonlyTag:
				keyword = Kind::ReadonlyKeyword;
				break;
			case Kind::JSDocPrivateTag:
				keyword = Kind::PrivateKeyword;
				break;
			case Kind::JSDocPublicTag:
				keyword = Kind::PublicKeyword;
				break;
			case Kind::JSDocProtectedTag:
				keyword = Kind::ProtectedKeyword;
				break;
			case Kind::JSDocOverrideTag:
				keyword = Kind::OverrideKeyword;
				break;
			default:
				break;
			}
			Node* modifier = factory.newToken(keyword);
			modifier->loc = tag->loc;
			modifier->flags = contextFlags | NodeFlagsReparsed;
			std::vector<Node*> nodes;
			TextRange loc;
			if (parent->modifiers() == nullptr) {
				nodes.push_back(modifier);
				loc = tag->loc;
			} else {
				nodes = parent->modifierNodes();
				nodes.push_back(modifier);
				loc = parent->modifiers()->loc;
			}
			parent->setModifiers(newModifierList(loc, nodes));
			finishMutatedNode(parent);
			break;
		}
		default:
			break;
		}
		break;
	}
	case Kind::JSDocImplementsTag:
		if (ClassLikeDataRef cls = getClassLikeData(parent);
		    cls.heritageClauses != nullptr) {
			auto* implementsTag = tag->as<JSDocImplementsTag>();

			if (*cls.heritageClauses != nullptr) {
				Node* implementsClause = nullptr;
				for (Node* node : (*cls.heritageClauses)->nodes) {
					if (node->as<HeritageClause>()->Token ==
					    Kind::ImplementsKeyword) {
						implementsClause = node;
						break;
					}
				}
				if (implementsClause != nullptr) {
					implementsClause->as<HeritageClause>()
					    ->Types->nodes.push_back(addDeepCloneReparse(
					        implementsTag->ClassName));
					finishMutatedNode(implementsClause);
					return;
				}
			}
			NodeList* typesList = newNodeList(
				implementsTag->ClassName->loc,
				{addDeepCloneReparse(implementsTag->ClassName)});

			Node* heritageClause = factory.newHeritageClause(
				Kind::ImplementsKeyword, typesList);
			finishReparsedNode(heritageClause,
			                   implementsTag->ClassName);

			if (*cls.heritageClauses == nullptr) {
				*cls.heritageClauses =
					newNodeList(implementsTag->ClassName->loc,
					            {heritageClause});
			} else {
				(*cls.heritageClauses)->nodes.push_back(
					heritageClause);
			}
			finishMutatedNode(parent);
		}
		break;
	case Kind::JSDocAugmentsTag:
		if (ClassLikeDataRef cls = getClassLikeData(parent);
		    cls.heritageClauses != nullptr &&
		    *cls.heritageClauses != nullptr) {
			Node* extendsClause = nullptr;
			for (Node* node : (*cls.heritageClauses)->nodes) {
				if (node->as<HeritageClause>()->Token ==
				    Kind::ExtendsKeyword) {
					extendsClause = node;
					break;
				}
			}
			if (extendsClause != nullptr &&
			    extendsClause->as<HeritageClause>()
			            ->Types->nodes.size() == 1) {
				auto* target = extendsClause->as<HeritageClause>()
				                   ->Types->nodes[0]
				                   ->as<ExpressionWithTypeArguments>();
				auto* source = tag->className()
				                   ->as<ExpressionWithTypeArguments>();
				if (hasSamePropertyAccessName(target->Expression,
				                              source->Expression)) {
					if (target->TypeArguments == nullptr &&
					    source->TypeArguments != nullptr) {
						std::vector<Node*> newArguments(
							source->TypeArguments->nodes.size());
						for (size_t i = 0;
						     i < source->TypeArguments->nodes.size();
						     i++) {
							newArguments[i] = addDeepCloneReparse(
								source->TypeArguments->nodes[i]);
						}
						target->TypeArguments = newNodeList(
							source->TypeArguments->loc,
							newArguments);
						finishMutatedNode(target->asNode());
					}
				}
			}
		}
		break;
	default:
		break;
	}
}

Node* Parser::makeQuestionIfOptional(Node* parameter) {
	auto* jsparam = parameter->as<JSDocParameterOrPropertyTag>();
	Node* questionToken = nullptr;
	if (jsparam->IsBracketed ||
	    (jsparam->TypeExpression != nullptr &&
	     jsparam->TypeExpression->type()->kind == Kind::JSDocOptionalType)) {
		questionToken = factory.newToken(Kind::QuestionToken);
		questionToken->loc = parameter->loc;
		questionToken->flags = contextFlags | NodeFlagsReparsed;
	}
	return questionToken;
}

Node* Parser::makeNewCast(Node* t, Node* e, bool isAssertion) {
	Node* assert;
	if (isAssertion) {
		assert = factory.newAsExpression(e, t);
	} else {
		assert = factory.newSatisfiesExpression(e, t);
	}
	finishNodeWithEnd(assert, e->pos(), e->end());
	return assert;
}

ModifierList* Parser::createExportModifier(Node* locationNode) {
	Node* exportModifier = factory.newToken(Kind::ExportKeyword);
	exportModifier->loc = locationNode->loc;
	exportModifier->flags = contextFlags | NodeFlagsReparsed;
	return newModifierList(locationNode->loc, {exportModifier});
}

// getInnermostNameOfJSDocNamespace returns the innermost identifier from a
// JSDoc namespace chain (ModuleDeclaration). For a simple identifier, it
// returns the identifier itself. For "A.B.C", it returns the identifier "C".
Node* Parser::getInnermostNameOfJSDocNamespace(Node* fullName) {
	if (fullName == nullptr) {
		return nullptr;
	}
	while (fullName->kind == Kind::ModuleDeclaration) {
		Node* body = fullName->as<ModuleDeclaration>()->Body;
		if (body == nullptr) {
			return fullName->name();
		}
		fullName = body;
	}
	return fullName;
}

// wrapInJSDocNamespace wraps a statement (typically a type alias) in
// namespace declarations corresponding to a JSDoc dotted name. For example,
// given name "A.B.C" and a type alias for C, this produces:
//
//	namespace A { namespace B { type C = ... } }
//
// If the name is a simple identifier (not a ModuleDeclaration), it returns
// the statement as-is.
Node* Parser::wrapInJSDocNamespace(Node* fullName, Node* statement,
                                   bool nested) {
	if (fullName == nullptr || !isModuleDeclaration(fullName)) {
		return statement;
	}
	// Recursively wrap from outermost to innermost. Inner namespaces always
	// get an export modifier so members are accessible via dotted access
	// from outside. The outermost namespace is treated as exported only in
	// module files via IsImplicitlyExportedJSDocDeclaration (in the binder),
	// so it does not get an explicit export modifier here.
	Node* wrapped = wrapInJSDocNamespace(fullName->body(), statement,
	                                   true /*nested*/);
	Node* block = factory.newModuleBlock(
		newNodeList(fullName->loc, {wrapped}));
	finishReparsedNode(block, fullName);
	ModifierList* modifiers = nullptr;
	if (nested) {
		modifiers = createExportModifier(fullName);
	}
	Node* result = factory.newModuleDeclaration(
		modifiers, Kind::NamespaceKeyword,
		addDeepCloneReparse(fullName->name()), nullptr, block);
	finishReparsedNode(result, fullName);
	reparsedClones.push_back(result);
	return result;
}

}  // namespace tsc
