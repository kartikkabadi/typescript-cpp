// === slice: ls-coreC ===
// inlay_hints.cpp — inlay_hints.go: inlay hints (parameter names + types).
#include "internal/ls/ls.h"

#include "internal/astnav/tokens.h"
#include "internal/debug/debug.h"
#include "internal/evaluator/evaluator.h"
#include "internal/nodebuilder/types.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls {

namespace {

// inlay_hints.go:388 — shouldShowParameterNameHints
bool shouldShowParameterNameHints(
	const lsutil::InlayHintsPreferences& preferences) {
	return (preferences.IncludeInlayParameterNameHints ==
				lsutil::IncludeInlayParameterNameHintsLiterals ||
			preferences.IncludeInlayParameterNameHints ==
				lsutil::IncludeInlayParameterNameHintsAll);
}

// inlay_hints.go:393 — shouldShowLiteralParameterNameHintsOnly
bool shouldShowLiteralParameterNameHintsOnly(
	const lsutil::InlayHintsPreferences& preferences) {
	return preferences.IncludeInlayParameterNameHints ==
		   lsutil::IncludeInlayParameterNameHintsLiterals;
}

// node is FunctionDeclaration | ArrowFunction | FunctionExpression | MethodDeclaration | GetAccessor
// inlay_hints.go:398
bool isSignatureSupportingReturnAnnotation(::tsc::Node* node) {
	return isArrowFunction(node) || isFunctionExpression(node) ||
		   isFunctionDeclaration(node) || isMethodDeclaration(node) ||
		   isGetAccessorDeclaration(node);
}

// inlay_hints.go:425 — isHintableLiteral
bool isHintableLiteral(::tsc::Node* node) {
	switch (node->kind) {
	case Kind::PrefixUnaryExpression: {
		::tsc::Node* operand = node->as<PrefixUnaryExpression>()->Operand;
		return isLiteralExpression(operand) ||
			   (isIdentifier(operand) &&
				isInfinityOrNaNString(operand->text()));
	}
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::NullKeyword:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::TemplateExpression:
		return true;
	case Kind::Identifier: {
		const std::string& name = node->text();
		return name == "undefined" || isInfinityOrNaNString(name);
	}
	default:
		break;
	}
	return isLiteralExpression(node);
}

// inlay_hints.go:415 — isHintableDeclaration
bool isHintableDeclaration(::tsc::Node* node) {
	if ((isPartOfParameterDeclaration(node) ||
		 (isVariableDeclaration(node) && isVarConst(node))) &&
		node->initializer() != nullptr) {
		::tsc::Node* initializer = skipParentheses(node->initializer());
		return !(isHintableLiteral(initializer) ||
				 isNewExpression(initializer) ||
				 isObjectLiteralExpression(initializer) ||
				 isAssertionExpression(initializer));
	}
	return true;
}

// inlay_hints.go:437 — isModuleReferenceType
bool isModuleReferenceType(checker::Type* t) {
	::tsc::Symbol* symbol = t->symbol;
	return symbol != nullptr &&
		   (symbol->flags & SymbolFlagsModule) != 0;
}

// utilities.go:4238 — ast.HasContextSensitiveParameters (not yet in
// cpp/internal/ast; checker's file-local copy lives in
// checker_expressions_a.cpp)
bool hasContextSensitiveParameters(::tsc::Node* node) {
	// Functions with type parameters are not context sensitive.
	if (node->typeParameters().empty()) {
		// Functions with any parameters that lack type annotations are context sensitive.
		for (auto* p : node->parameters()) {
			if (p->type() == nullptr) {
				return true;
			}
		}
		if (!isArrowFunction(node)) {
			// If the first parameter is not an explicit 'this' parameter, then the function has
			// an implicit 'this' parameter which is subject to contextual typing.
			auto params = node->parameters();
			::tsc::Node* parameter = params.empty() ? nullptr : params[0];
			if (parameter == nullptr || !isThisParameter(parameter)) {
				return (node->flags & NodeFlagsContainsThis) != 0;
			}
		}
	}
	return false;
}

// inlay_hints.go:858 — getParameterDeclarationIdentifier
::tsc::Node* getParameterDeclarationIdentifier(::tsc::Symbol* symbol) {
	if (symbol->data->valueDeclaration != nullptr &&
		isParameterDeclaration(symbol->data->valueDeclaration) &&
		isIdentifier(symbol->data->valueDeclaration->name())) {
		return symbol->data->valueDeclaration->name();
	}
	return nullptr;
}

// inlay_hints.go:865 — identifierOrAccessExpressionPostfixMatchesParameterName
bool identifierOrAccessExpressionPostfixMatchesParameterName(
	::tsc::Node* expr, std::string_view parameterName) {
	if (isIdentifier(expr)) {
		return expr->text() == parameterName;
	}
	if (isPropertyAccessExpression(expr)) {
		return expr->name()->text() == parameterName;
	}
	return false;
}

// Go core.TextRange.Intersects — text.go:70 — closed-interval: touching
// endpoints intersect (NOT the same as TextRange::intersects, which is strict).
bool textRangeIntersects(TextRange a, TextRange b) {
	return std::max(a.pos(), b.pos()) <= std::min(a.end(), b.end());
}

// inlay_hints.go:810 — parameterInfo
struct parameterInfo {
	::tsc::Node* parameter = nullptr;
	std::string name;
	bool isRestParameter = false;
};

// inlay_hints.go:63 — inlayHintState
struct inlayHintState {
	gostd::Context ctx;
	TextRange span;
	lsutil::InlayHintsPreferences preferences;
	lsutil::QuotePreference quotePreference;
	SourceFile* file = nullptr;
	checker::Checker* checker = nullptr;
	lsconv::Converters* converters = nullptr;
	std::vector<std::shared_ptr<lsp::lsproto::InlayHint>> result;

	// inlay_hints.go:74 — visit
	bool visit(::tsc::Node* node) {
		if (node == nullptr || int(node->end()) - int(node->pos()) == 0 ||
			(node->flags & NodeFlagsReparsed) != 0) {
			return false;
		}

		switch (node->kind) {
		case Kind::ModuleDeclaration:
		case Kind::ClassDeclaration:
		case Kind::InterfaceDeclaration:
		case Kind::FunctionDeclaration:
		case Kind::ClassExpression:
		case Kind::FunctionExpression:
		case Kind::MethodDeclaration:
		case Kind::ArrowFunction:
			if (gostd::ctxErr(ctx) != nullptr) {
				return true;
			}
			break;
		default:
			break;
		}

		if (!textRangeIntersects(span, node->posEnd())) {
			return false;
		}

		if (isTypeNode(node) && !isExpressionWithTypeArguments(node)) {
			return false;
		}

		if (tristateIsTrue(
				preferences.IncludeInlayVariableTypeHints) &&
			isVariableDeclaration(node)) {
			visitVariableLikeDeclaration(node);
		} else if (tristateIsTrue(
					   preferences
						   .IncludeInlayPropertyDeclarationTypeHints) &&
				   isPropertyDeclaration(node)) {
			visitVariableLikeDeclaration(node);
		} else if (tristateIsTrue(
					   preferences.IncludeInlayEnumMemberValueHints) &&
				   isEnumMember(node)) {
			visitEnumMember(node);
		} else if (shouldShowParameterNameHints(preferences) &&
				   (isCallExpression(node) || isNewExpression(node))) {
			visitCallOrNewExpression(node);
		} else {
			if (tristateIsTrue(
					preferences.IncludeInlayFunctionParameterTypeHints) &&
				isFunctionLikeDeclaration(node) &&
				hasContextSensitiveParameters(node)) {
				visitFunctionLikeForParameterType(node);
			}
			if (tristateIsTrue(
					preferences
						.IncludeInlayFunctionLikeReturnTypeHints) &&
				isSignatureSupportingReturnAnnotation(node)) {
				visitFunctionDeclarationLikeForReturnType(node);
			}
		}
		return node->forEachChild([&](::tsc::Node* child) {
			return visit(child);
		});
	}

	// inlay_hints.go:110 — visitFunctionDeclarationLikeForReturnType
	// FunctionDeclaration | MethodDeclaration | GetAccessor | FunctionExpression | ArrowFunction
	void visitFunctionDeclarationLikeForReturnType(
		::tsc::Node* decl) {
		if (isArrowFunction(decl)) {
			if (astnav::findChildOfKind(decl, Kind::OpenParenToken,
										file) == nullptr) {
				return;
			}
		}

		::tsc::Node* typeAnnotation = decl->type();
		if (typeAnnotation != nullptr || decl->body() == nullptr) {
			return;
		}

		checker::Signature* signature =
			checker->GetSignatureFromDeclaration(decl);
		if (signature == nullptr) {
			return;
		}

		checker::TypePredicate* typePredicate =
			checker->GetTypePredicateOfSignature(signature);

		if (typePredicate != nullptr && typePredicate->t != nullptr) {
			auto hintParts = typePredicateToInlayHintParts(typePredicate);
			addTypeHints(hintParts, getTypeAnnotationPosition(decl));
			return;
		}

		checker::Type* returnType =
			checker->GetReturnTypeOfSignature(signature);
		if (isModuleReferenceType(returnType)) {
			return;
		}

		auto hintParts = typeToInlayHintParts(returnType);
		addTypeHints(hintParts, getTypeAnnotationPosition(decl));
	}

	// inlay_hints.go:148 — visitCallOrNewExpression
	void visitCallOrNewExpression(::tsc::Node* expr) {
		auto args = expr->arguments();
		if (args.empty()) {
			return;
		}

		checker::Signature* signature = checker->GetResolvedSignature(expr);
		if (signature == nullptr) {
			return;
		}

		int signatureParamPos = 0;
		for (auto* originalArg : args) {
			::tsc::Node* arg = skipParentheses(originalArg);
			if (shouldShowLiteralParameterNameHintsOnly(preferences) &&
				!isHintableLiteral(arg)) {
				signatureParamPos++;
				continue;
			}

			int spreadArgs = 0;
			if (isSpreadElement(arg)) {
				checker::Type* spreadType =
					checker->GetTypeAtLocation(arg->expression());
				if (checker::IsTupleType(spreadType)) {
					checker::TupleType* tuple =
						spreadType->Target()->AsTupleType();
					int32_t fixedLength = tuple->fixedLength;
					if (fixedLength == 0) {
						continue;
					}
					// slices.IndexFunc(elementFlags, f&ElementFlagsRequired == 0)
					int firstOptionalIndex = -1;
					for (size_t ei = 0; ei < tuple->elementInfos.size();
						 ei++) {
						if ((tuple->elementInfos[ei].flags &
							 checker::ElementFlagsRequired) == 0) {
							firstOptionalIndex = int(ei);
							break;
						}
					}
					int requiredArgs = firstOptionalIndex < 0
										   ? fixedLength
										   : firstOptionalIndex;
					if (requiredArgs > 0) {
						spreadArgs = requiredArgs;
					}
				}
			}

			parameterInfo* identifierInfo =
				getParameterIdentifierInfoAtPosition(signature,
													 signatureParamPos);
			signatureParamPos = signatureParamPos +
								(spreadArgs > 0 ? spreadArgs : 1);
			if (identifierInfo == nullptr) {
				return;
			}

			::tsc::Node* parameter = identifierInfo->parameter;
			std::string parameterName = identifierInfo->name;
			bool isFirstVariadicArgument =
				identifierInfo->isRestParameter;
			bool parameterNameNotSameAsArgument =
				tristateIsTrue(
					preferences
						.IncludeInlayParameterNameHintsWhenArgumentMatchesName) ||
				!identifierOrAccessExpressionPostfixMatchesParameterName(
					arg, parameterName);
			if (!parameterNameNotSameAsArgument && !isFirstVariadicArgument) {
				continue;
			}

			if (leadingCommentsContainsParameterName(arg, parameterName)) {
				continue;
			}

			addParameterHints(parameterName, parameter,
							  astnav::getStartOfNode(originalArg, file,
													 false /*includeJSDoc*/),
							  isFirstVariadicArgument);
		}
	}

	// inlay_hints.go:203 — visitEnumMember
	void visitEnumMember(::tsc::Node* member) {
		if (member->initializer() != nullptr) {
			return;
		}

		auto enumValue = checker->GetConstantValue(member);
		if (!std::holds_alternative<std::monostate>(enumValue)) {
			addEnumMemberValueHints(evalAnyToString(enumValue),
									int(member->end()));
		}
	}

	// inlay_hints.go:211 — visitVariableLikeDeclaration
	void visitVariableLikeDeclaration(::tsc::Node* decl) {
		if ((decl->initializer() == nullptr &&
				!(isPropertyDeclaration(decl) &&
				  (checker->GetTypeAtLocation(decl)->flags &
				   checker::TypeFlagsAny) == 0)) ||
			isBindingPattern(decl->name()) ||
			(isVariableDeclaration(decl) &&
			 !isHintableDeclaration(decl))) {
			return;
		}

		::tsc::Node* typeAnnotation = decl->type();
		if (typeAnnotation != nullptr) {
			return;
		}

		checker::Type* declarationType =
			checker->GetTypeAtLocation(decl);
		if (isModuleReferenceType(declarationType)) {
			return;
		}

		auto hintParts = typeToInlayHintParts(declarationType);
		std::string hintText;
		if (hintParts.String != nullptr) {
			hintText = *hintParts.String;
		} else if (hintParts.InlayHintLabelParts != nullptr) {
			for (auto& part : **hintParts.InlayHintLabelParts) {
				hintText += part->Value;
			}
		}
		if (!tristateIsTrue(
				preferences
					.IncludeInlayVariableTypeHintsWhenTypeMatchesName) &&
			!isComputedPropertyName(decl->name()) &&
			stringutil::EquateStringCaseInsensitive(decl->name()->text(),
												  hintText)) {
			return;
		}
		addTypeHints(hintParts, int(decl->name()->end()));
	}

	// inlay_hints.go:244 — visitFunctionLikeForParameterType
	void visitFunctionLikeForParameterType(::tsc::Node* node) {
		checker::Signature* signature =
			checker->GetSignatureFromDeclaration(node);
		if (signature == nullptr) {
			return;
		}

		int pos = 0;
		for (auto* param : node->parameters()) {
			if (isHintableDeclaration(param)) {
				::tsc::Symbol* symbol = nullptr;
				if (isThisParameter(param)) {
					symbol = signature->thisParameter;
				} else {
					symbol = signature->parameters[pos];
				}
				addParameterTypeHint(param, symbol);
			}
			if (isThisParameter(param)) {
				continue;
			}
			pos++;
		}
	}

	// inlay_hints.go:263 — addParameterTypeHint
	void addParameterTypeHint(::tsc::Node* node, ::tsc::Symbol* symbol) {
		::tsc::Node* typeAnnotation = node->type();
		if (typeAnnotation != nullptr || symbol == nullptr) {
			return;
		}
		auto* typeHints = getParameterDeclarationTypeHints(symbol);
		if (typeHints == nullptr) {
			return;
		}
		int pos = 0;
		if (node->questionToken() != nullptr) {
			pos = int(node->questionToken()->end());
		} else {
			pos = int(node->name()->end());
		}
		addTypeHints(*typeHints, pos);
	}

	// inlay_hints.go:277 — getParameterDeclarationTypeHints
	lsp::lsproto::StringOrInlayHintLabelParts*
	getParameterDeclarationTypeHints(::tsc::Symbol* symbol) {
		::tsc::Node* valueDeclaration = symbol->data->valueDeclaration;
		if (valueDeclaration == nullptr ||
			!isParameterDeclaration(valueDeclaration)) {
			return nullptr;
		}

		checker::Type* signatureParamType =
			checker->GetTypeOfSymbolAtLocation(symbol, valueDeclaration);
		if (isModuleReferenceType(signatureParamType)) {
			return nullptr;
		}

		return new lsp::lsproto::StringOrInlayHintLabelParts(
			typeToInlayHintParts(signatureParamType));
	}

	// inlay_hints.go:290 — typeToInlayHintParts
	lsp::lsproto::StringOrInlayHintLabelParts typeToInlayHintParts(
		checker::Type* t) {
		nodebuilder::Flags flags = nodebuilder::FlagsIgnoreErrors |
								   nodebuilder::FlagsAllowUniqueESSymbolType |
								   nodebuilder::
									   FlagsUseAliasDefinedOutsideCurrentScope;
		auto* idToSymbol = new std::unordered_map<::tsc::Node*, ::tsc::Symbol*>();
		// !!! Avoid type node reuse so we collect identifier symbols.
		::tsc::Node* typeNode = checker->TypeToTypeNode(
			t, nullptr /*enclosingDeclaration*/, flags, idToSymbol);
		debug::assert(typeNode != nullptr, "should always get typenode");
		lsp::lsproto::StringOrInlayHintLabelParts out;
		out.InlayHintLabelParts =
			std::make_shared<lsp::lsproto::Slice<
				std::shared_ptr<lsp::lsproto::InlayHintLabelPart>>>(
				getInlayHintLabelParts(typeNode, *idToSymbol));
		return out;
	}

	// inlay_hints.go:301 — typePredicateToInlayHintParts
	lsp::lsproto::StringOrInlayHintLabelParts typePredicateToInlayHintParts(
		checker::TypePredicate* typePredicate) {
		nodebuilder::Flags flags = nodebuilder::FlagsIgnoreErrors |
								   nodebuilder::FlagsAllowUniqueESSymbolType |
								   nodebuilder::
									   FlagsUseAliasDefinedOutsideCurrentScope;
		auto* idToSymbol = new std::unordered_map<::tsc::Node*, ::tsc::Symbol*>();
		// !!! Avoid type node reuse so we collect identifier symbols.
		::tsc::Node* typeNode =
			checker->TypePredicateToTypePredicateNode(
				typePredicate, nullptr /*enclosingDeclaration*/, flags,
				idToSymbol);
		debug::assert(typeNode != nullptr,
					  "should always get typePredicateNode");
		lsp::lsproto::StringOrInlayHintLabelParts out;
		out.InlayHintLabelParts =
			std::make_shared<lsp::lsproto::Slice<
				std::shared_ptr<lsp::lsproto::InlayHintLabelPart>>>(
				getInlayHintLabelParts(typeNode, *idToSymbol));
		return out;
	}

	// inlay_hints.go:312 — addTypeHints
	void addTypeHints(lsp::lsproto::StringOrInlayHintLabelParts hint,
					  int position) {
		auto [lspPosition, fidelity] =
			converters->ToLSPPositionForFeature(
				file, static_cast<TextPos>(position),
				spanmap::FeatureInlayHints);
		if (fidelity.IsNone()) {
			return;
		}
		if (hint.String != nullptr) {
			hint.String =
				std::make_shared<std::string>(": " + *hint.String);
		} else {
			std::vector<std::shared_ptr<lsp::lsproto::InlayHintLabelPart>>
			    parts;
			parts.push_back(std::make_shared<
			                lsp::lsproto::InlayHintLabelPart>(
			    lsp::lsproto::InlayHintLabelPart{.Value = ": "}));
			if (hint.InlayHintLabelParts != nullptr) {
				parts.insert(parts.end(),
				             (*hint.InlayHintLabelParts)->begin(),
				             (*hint.InlayHintLabelParts)->end());
			}
			hint.InlayHintLabelParts =
				std::make_shared<lsp::lsproto::Slice<std::shared_ptr<
				    lsp::lsproto::InlayHintLabelPart>>>(std::move(parts));
		}
		auto resultHint = std::make_shared<lsp::lsproto::InlayHint>();
		resultHint->Label = std::move(hint);
		resultHint->Position = lspPosition;
		resultHint->Kind = std::make_shared<lsp::lsproto::InlayHintKind>(
			lsp::lsproto::InlayHintKindType);
		resultHint->PaddingLeft = true;
		result.push_back(resultHint);
	}

	// inlay_hints.go:325 — addEnumMemberValueHints
	void addEnumMemberValueHints(std::string_view text, int position) {
		auto [lspPosition, fidelity] =
			converters->ToLSPPositionForFeature(
				file, static_cast<TextPos>(position),
				spanmap::FeatureInlayHints);
		if (fidelity.IsNone()) {
			return;
		}
		auto resultHint = std::make_shared<lsp::lsproto::InlayHint>();
		resultHint->Label.String = std::make_shared<std::string>(
		    "= " + std::string(text));
		resultHint->Position = lspPosition;
		resultHint->PaddingLeft = true;
		result.push_back(resultHint);
	}

	// inlay_hints.go:337 — addParameterHints
	void addParameterHints(std::string_view text, ::tsc::Node* parameter,
						   int position, bool isFirstVariadicArgument) {
		auto [lspPosition, fidelity] =
			converters->ToLSPPositionForFeature(
				file, static_cast<TextPos>(position),
				spanmap::FeatureInlayHints);
		if (fidelity.IsNone()) {
			return;
		}
		std::string hintText =
			(isFirstVariadicArgument ? "..." : "") + std::string(text);
		std::vector<std::shared_ptr<lsp::lsproto::InlayHintLabelPart>>
		    displayPartsVec;
		auto* displayParts = &displayPartsVec;
		displayParts->push_back(
			getNodeDisplayPart(hintText, parameter));
		displayParts->push_back(
		    std::make_shared<lsp::lsproto::InlayHintLabelPart>(
		        lsp::lsproto::InlayHintLabelPart{.Value = ":"}));
		lsp::lsproto::StringOrInlayHintLabelParts labelParts;
		labelParts.InlayHintLabelParts =
			std::make_shared<lsp::lsproto::Slice<
			    std::shared_ptr<lsp::lsproto::InlayHintLabelPart>>>(
			    std::move(*displayParts));

		auto resultHint = std::make_shared<lsp::lsproto::InlayHint>();
		resultHint->Label = labelParts;
		resultHint->Position = lspPosition;
		resultHint->Kind = std::make_shared<lsp::lsproto::InlayHintKind>(
			lsp::lsproto::InlayHintKindParameter);
		resultHint->PaddingRight = true;
		result.push_back(resultHint);
	}

	// inlay_hints.go:442 — getInlayHintLabelParts
	std::vector<std::shared_ptr<lsp::lsproto::InlayHintLabelPart>>
	getInlayHintLabelParts(
		::tsc::Node* node,
		const std::unordered_map<::tsc::Node*, ::tsc::Symbol*>&
			idToSymbol) {
		std::vector<std::shared_ptr<lsp::lsproto::InlayHintLabelPart>>
		    parts;

		auto pushPart = [&](std::string_view v) {
			auto p =
			    std::make_shared<lsp::lsproto::InlayHintLabelPart>();
			p->Value = std::string(v);
			parts.push_back(p);
		};

		std::function<void(::tsc::Node*)> visitForDisplayParts;
		std::function<void(const std::vector<::tsc::Node*>&,
						   std::string_view)>
			visitDisplayPartList;
		std::function<void(::tsc::Node*)> visitParametersAndTypeParameters;

		visitForDisplayParts = [&](::tsc::Node* node) {
			if (node == nullptr) {
				return;
			}

			auto tokenString = tsc::tokenToString(node->kind);
			if (!tokenString.empty()) {
				pushPart(tokenString);
				return;
			}

			if (isLiteralExpression(node)) {
				pushPart(getLiteralText(node));
				return;
			}

			switch (node->kind) {
			case Kind::Identifier: {
				const std::string& identifierText = node->text();
				::tsc::Node* name = nullptr;
				auto it = idToSymbol.find(node);
				if (it != idToSymbol.end() && it->second != nullptr &&
					!it->second->data->declarations.empty()) {
					name = getNameOfDeclaration(
						it->second->data->declarations[0]);
				}
				if (name != nullptr) {
					parts.push_back(
						getNodeDisplayPart(identifierText, name));
				} else {
					pushPart(identifierText);
				}
				break;
			}
			case Kind::QualifiedName:
				visitForDisplayParts(node->as<QualifiedName>()->Left);
				pushPart(".");
				visitForDisplayParts(node->as<QualifiedName>()->Right);
				break;
			case Kind::TypePredicate: {
				auto* typePredicate = node->as<TypePredicateNode>();
				if (typePredicate->AssertsModifier != nullptr) {
					pushPart("asserts ");
				}
				visitForDisplayParts(typePredicate->ParameterName);
				if (node->type() != nullptr) {
					pushPart(" is ");
					visitForDisplayParts(node->type());
				}
				break;
			}
			case Kind::TypeReference:
				visitForDisplayParts(
					node->as<TypeReferenceNode>()->TypeName);
				if (!node->typeArguments().empty()) {
					pushPart("<");
					visitDisplayPartList(node->typeArguments(), ",");
					pushPart(">");
				}
				break;
			case Kind::TypeParameter:
				if (!node->modifierNodes().empty()) {
					visitDisplayPartList(node->modifierNodes(), "");
				}
				visitForDisplayParts(node->name());
				if (node->as<TypeParameterDeclaration>()->Constraint !=
					nullptr) {
					pushPart(" extends ");
					visitForDisplayParts(
						node->as<TypeParameterDeclaration>()
							->Constraint);
				}
				if (node->as<TypeParameterDeclaration>()->DefaultType !=
					nullptr) {
					pushPart(" = ");
					visitForDisplayParts(
						node->as<TypeParameterDeclaration>()
							->DefaultType);
				}
				break;
			case Kind::Parameter:
				if (!node->modifierNodes().empty()) {
					visitDisplayPartList(node->modifierNodes(), " ");
				}
				if (node->as<ParameterDeclaration>()->DotDotDotToken !=
					nullptr) {
					pushPart("...");
				}
				visitForDisplayParts(node->name());
				if (node->questionToken() != nullptr) {
					pushPart("?");
				}
				if (node->type() != nullptr) {
					pushPart(": ");
					visitForDisplayParts(node->type());
				}
				break;
			case Kind::ConstructorType:
				pushPart("new ");
				visitParametersAndTypeParameters(node);
				pushPart(" => ");
				visitForDisplayParts(node->type());
				break;
			case Kind::TypeQuery:
				pushPart("typeof ");
				visitForDisplayParts(node->as<TypeQueryNode>()->ExprName);
				if (!node->typeArguments().empty()) {
					pushPart("<");
					visitDisplayPartList(node->typeArguments(), ", ");
					pushPart(">");
				}
				break;
			case Kind::TypeLiteral:
				pushPart("{");
				if (!node->members().empty()) {
					pushPart(" ");
					visitDisplayPartList(node->members(), "; ");
					pushPart(" ");
				}
				pushPart("}");
				break;
			case Kind::ArrayType:
				visitForDisplayParts(
					node->as<ArrayTypeNode>()->ElementType);
				pushPart("[]");
				break;
			case Kind::TupleType:
				pushPart("[");
				visitDisplayPartList(node->elements(), ", ");
				pushPart("]");
				break;
			case Kind::NamedTupleMember:
				if (node->as<NamedTupleMember>()->DotDotDotToken !=
					nullptr) {
					pushPart("...");
				}
				visitForDisplayParts(node->name());
				if (node->questionToken() != nullptr) {
					pushPart("?");
				}
				pushPart(": ");
				visitForDisplayParts(node->type());
				break;
			case Kind::OptionalType:
				visitForDisplayParts(node->type());
				pushPart("?");
				break;
			case Kind::RestType:
				pushPart("...");
				visitForDisplayParts(node->type());
				break;
			case Kind::UnionType:
				if (node->as<UnionTypeNode>()->Types != nullptr) {
					visitDisplayPartList(
						node->as<UnionTypeNode>()->Types->nodes,
						" | ");
				}
				break;
			case Kind::IntersectionType:
				if (node->as<IntersectionTypeNode>()->Types !=
					nullptr) {
					visitDisplayPartList(
						node->as<IntersectionTypeNode>()
							->Types->nodes,
						" & ");
				}
				break;
			case Kind::ConditionalType: {
				auto* ct = node->as<ConditionalTypeNode>();
				visitForDisplayParts(ct->CheckType);
				pushPart(" extends ");
				visitForDisplayParts(ct->ExtendsType);
				pushPart(" ? ");
				visitForDisplayParts(ct->TrueType);
				pushPart(" : ");
				visitForDisplayParts(ct->FalseType);
				break;
			}
			case Kind::InferType:
				pushPart("infer ");
				visitForDisplayParts(
					node->as<InferTypeNode>()->TypeParameter);
				break;
			case Kind::ParenthesizedType:
				pushPart("(");
				visitForDisplayParts(node->type());
				pushPart(")");
				break;
			case Kind::TypeOperator:
				pushPart(tsc::tokenToString(
					node->as<TypeOperatorNode>()->Operator));
				visitForDisplayParts(node->type());
				break;
			case Kind::IndexedAccessType: {
				auto* iat = node->as<IndexedAccessTypeNode>();
				visitForDisplayParts(iat->ObjectType);
				pushPart("[");
				visitForDisplayParts(iat->IndexType);
				pushPart("]");
				break;
			}
			case Kind::MappedType: {
				auto* mt = node->as<MappedTypeNode>();
				pushPart("{ ");
				if (mt->ReadonlyToken != nullptr) {
					if (mt->ReadonlyToken->kind == Kind::PlusToken) {
						pushPart("+");
					} else if (mt->ReadonlyToken->kind ==
							   Kind::MinusToken) {
						pushPart("-");
					}
					pushPart("readonly ");
				}
				pushPart("[");
				visitForDisplayParts(mt->TypeParameter);
				if (mt->NameType != nullptr) {
					pushPart(" as ");
					visitForDisplayParts(mt->NameType);
				}
				pushPart("]");
				if (node->questionToken() != nullptr) {
					if (node->questionToken()->kind ==
						Kind::PlusToken) {
						pushPart("+");
					} else if (node->questionToken()->kind ==
							   Kind::MinusToken) {
						pushPart("-");
					}
					pushPart("?");
				}
				pushPart(": ");
				if (node->type() != nullptr) {
					visitForDisplayParts(node->type());
				}
				pushPart("; }");
				break;
			}
			case Kind::LiteralType:
				visitForDisplayParts(node->as<LiteralTypeNode>()->Literal);
				break;
			case Kind::FunctionType:
				visitParametersAndTypeParameters(node);
				pushPart(" => ");
				visitForDisplayParts(node->type());
				break;
			case Kind::ImportType: {
				auto* it = node->as<ImportTypeNode>();
				if (it->IsTypeOf) {
					pushPart("typeof ");
				}
				pushPart("import(");
				visitForDisplayParts(it->Argument);
				pushPart(")");
				if (it->Qualifier != nullptr) {
					pushPart(".");
					visitForDisplayParts(it->Qualifier);
				}
				if (!node->typeArguments().empty()) {
					pushPart("<");
					visitDisplayPartList(node->typeArguments(), ", ");
					pushPart(">");
				}
				break;
			}
			case Kind::PropertySignature:
				if (!node->modifierNodes().empty()) {
					visitDisplayPartList(node->modifierNodes(), " ");
					pushPart(" ");
				}
				visitForDisplayParts(node->name());
				if (node->postfixToken() != nullptr) {
					pushPart(tsc::tokenToString(
						node->postfixToken()->kind));
				}
				if (node->type() != nullptr) {
					pushPart(": ");
					visitForDisplayParts(node->type());
				}
				break;
			case Kind::IndexSignature:
				pushPart("[");
				visitDisplayPartList(node->parameters(), ", ");
				pushPart("]");
				if (node->type() != nullptr) {
					pushPart(": ");
					visitForDisplayParts(node->type());
				}
				break;
			case Kind::MethodSignature:
				if (!node->modifierNodes().empty()) {
					visitDisplayPartList(node->modifierNodes(), " ");
					pushPart(" ");
				}
				visitForDisplayParts(node->name());
				if (node->postfixToken() != nullptr) {
					pushPart(tsc::tokenToString(
						node->postfixToken()->kind));
				}
				visitParametersAndTypeParameters(node);
				if (node->type() != nullptr) {
					pushPart(": ");
					visitForDisplayParts(node->type());
				}
				break;
			case Kind::CallSignature:
				visitParametersAndTypeParameters(node);
				if (node->type() != nullptr) {
					pushPart(": ");
					visitForDisplayParts(node->type());
				}
				break;
			case Kind::ConstructSignature:
				pushPart("new ");
				visitParametersAndTypeParameters(node);
				if (node->type() != nullptr) {
					pushPart(": ");
					visitForDisplayParts(node->type());
				}
				break;
			case Kind::ArrayBindingPattern:
				pushPart("[");
				visitDisplayPartList(node->elements(), ", ");
				pushPart("]");
				break;
			case Kind::ObjectBindingPattern:
				pushPart("{");
				if (!node->elements().empty()) {
					pushPart(" ");
					visitDisplayPartList(node->elements(), ", ");
					pushPart(" ");
				}
				pushPart("}");
				break;
			case Kind::BindingElement:
				visitForDisplayParts(node->name());
				break;
			case Kind::PrefixUnaryExpression:
				pushPart(tsc::tokenToString(
					node->as<PrefixUnaryExpression>()->Operator));
				visitForDisplayParts(
					node->as<PrefixUnaryExpression>()->Operand);
				break;
			case Kind::TemplateLiteralType: {
				auto* tt = node->as<TemplateLiteralTypeNode>();
				visitForDisplayParts(tt->Head);
				for (auto* span : tt->TemplateSpans->nodes) {
					visitForDisplayParts(span);
				}
				break;
			}
			case Kind::TemplateHead:
				pushPart(getLiteralText(node));
				break;
			case Kind::TemplateLiteralTypeSpan:
				visitForDisplayParts(node->type());
				visitForDisplayParts(
					node->as<TemplateLiteralTypeSpan>()->Literal);
				break;
			case Kind::TemplateMiddle:
			case Kind::TemplateTail:
				pushPart(getLiteralText(node));
				break;
			case Kind::ThisType:
				pushPart("this");
				break;
			case Kind::ComputedPropertyName:
				pushPart("[");
				visitForDisplayParts(node->expression());
				pushPart("]");
				break;
			case Kind::PropertyAccessExpression:
				visitForDisplayParts(node->expression());
				pushPart(".");
				visitForDisplayParts(node->name());
				break;
			case Kind::ElementAccessExpression:
				visitForDisplayParts(node->expression());
				pushPart("[");
				visitForDisplayParts(
					node->as<ElementAccessExpression>()
						->ArgumentExpression);
				pushPart("]");
				break;
			default:
				debug::failBadSyntaxKind(node);
			}
		};

		visitDisplayPartList = [&](std::span<::tsc::Node* const> nodes,
								   std::string_view separator) {
			for (size_t i = 0; i < nodes.size(); i++) {
				if (i > 0) {
					pushPart(separator);
				}
				visitForDisplayParts(nodes[i]);
			}
		};

		visitParametersAndTypeParameters = [&](::tsc::Node* node) {
			if (!node->typeParameters().empty()) {
				pushPart("<");
				visitDisplayPartList(node->typeParameters(), ", ");
				pushPart(">");
			}
			pushPart("(");
			visitDisplayPartList(node->parameters(), ", ");
			pushPart(")");
		};

		visitForDisplayParts(node);
		return parts;
	}

	// inlay_hints.go:784 — getNodeDisplayPart
	std::shared_ptr<lsp::lsproto::InlayHintLabelPart> getNodeDisplayPart(
		std::string_view text, ::tsc::Node* node) {
		SourceFile* file = getSourceFileOfNode(node);
		int pos = astnav::getStartOfNode(node, file,
										 false /*includeJSDoc*/);
		int end = int(node->end());
		auto part = std::make_shared<lsp::lsproto::InlayHintLabelPart>();
		part->Value = std::string(text);
		// The location is an optional go-to target for the name. Only attach it when the name maps back to a
		// single concrete span in the original text; an approximate or synthesized mapping would point the
		// user somewhere wrong, so it is better to omit the target than to fabricate one.
		if (auto [lspRange, fidelity] =
				converters->ToLSPRangeForFeature(
					file,
					TextRange{static_cast<TextPos>(pos),
							  static_cast<TextPos>(end)},
					spanmap::FeatureInlayHints);
			fidelity.IsSingleSegment()) {
			auto loc = std::make_shared<lsp::lsproto::Location>();
			loc->Uri = lsconv::FileNameToDocumentURI(
				file->OriginalFileName());
			loc->Range = lspRange;
			part->Location = loc;
		}
		return part;
	}

	// inlay_hints.go:799 — getLiteralText
	std::string getLiteralText(::tsc::Node* node) {
		switch (node->kind) {
		case Kind::StringLiteral:
			if (quotePreference == lsutil::QuotePreferenceSingle) {
				return "'" +
					   printer::EscapeString(
						   node->text(),
						   printer::QuoteCharSingleQuote) +
					   "'";
			}
			return "\"" +
				   printer::EscapeString(
					   node->text(), printer::QuoteCharDoubleQuote) +
				   "\"";
		case Kind::TemplateHead:
		case Kind::TemplateMiddle:
		case Kind::TemplateTail: {
			std::string rawText = node->rawText();
			if (rawText.empty()) {
				rawText = printer::EscapeString(
					node->text(), printer::QuoteCharBacktick);
			}
			switch (node->kind) {
			case Kind::TemplateHead:
				return "`" + rawText + "${";
			case Kind::TemplateMiddle:
				return "}" + rawText + "${";
			case Kind::TemplateTail:
				return "}" + rawText + "`";
			default:
				break;
			}
			break;
		}
		default:
			break;
		}
		return node->text();
	}

	// inlay_hints.go:824 — getParameterIdentifierInfoAtPosition
	parameterInfo* getParameterIdentifierInfoAtPosition(
		checker::Signature* signature, int pos) {
		auto& parameters = signature->parameters;
		int paramCount = int(parameters.size()) -
						 ((signature->flags &
						   checker::SignatureFlagsHasRestParameter) != 0
							  ? 1
							  : 0);
		if (pos < paramCount) {
			::tsc::Symbol* param = parameters[pos];
			::tsc::Node* paramId = getParameterDeclarationIdentifier(param);
			if (paramId == nullptr) {
				return nullptr;
			}
			return new parameterInfo{
				paramId, paramId->text(), /*isRestParameter*/ false};
		}

		::tsc::Symbol* restParameter = nullptr;
		::tsc::Node* restId = nullptr;
		if (paramCount < int(parameters.size())) {
			restParameter = parameters[paramCount];
			restId = getParameterDeclarationIdentifier(restParameter);
		}
		if (restId == nullptr) {
			return nullptr;
		}

		checker::Type* restType = checker->GetTypeOfSymbol(restParameter);
		if (checker::IsTupleType(restType)) {
			checker::TupleType* tuple = restType->Target()->AsTupleType();
			std::vector<::tsc::Node*> associatedNames;
			associatedNames.reserve(tuple->elementInfos.size());
			for (auto& elementInfo : tuple->elementInfos) {
				associatedNames.push_back(elementInfo.labeledDeclaration);
			}
			int index = pos - paramCount;
			if (index < int(associatedNames.size())) {
				::tsc::Node* associatedName = associatedNames[index];
				if (associatedName != nullptr) {
					debug::assert(
						isIdentifier(associatedName->name()));
					bool isRestTupleElement;
					if (isNamedTupleMember(associatedName)) {
						isRestTupleElement =
							associatedName->as<NamedTupleMember>()
								->DotDotDotToken != nullptr;
					} else {
						isRestTupleElement =
							associatedName->as<ParameterDeclaration>()
								->DotDotDotToken != nullptr;
					}
					return new parameterInfo{
						associatedName->name(),
						associatedName->name()->text(),
						isRestTupleElement};
				}
			}

			return nullptr;
		}

		if (pos == paramCount) {
			return new parameterInfo{restId, restParameter->data->name,
									 /*isRestParameter*/ true};
		}
		return nullptr;
	}

	// inlay_hints.go:873 — leadingCommentsContainsParameterName
	bool leadingCommentsContainsParameterName(::tsc::Node* node,
											  const std::string& name) {
		if (!tsc::isIdentifierText(name, file->LanguageVariant)) {
			return false;
		}

		const std::string& fileText = file->Text();
		bool found = false;
		for (auto& r : getLeadingCommentRangesOfNode(node, file)) {
				// strings.TrimFunc(text, unicode.IsSpace || '/' || '*')
				std::string_view commentText{
					fileText.data() + r.pos(),
					size_t(r.end() - r.pos())};
				size_t b = 0;
				while (b < commentText.size()) {
					char32_t ch = 0;
					int w = 0;
					ch = decodeUtf8Rune(
						commentText.substr(b), &w);
					if (!(isWhiteSpaceSingleLine(ch) ||
						  isLineBreak(ch) || ch == U'/' ||
						  ch == U'*')) {
						break;
					}
					b += w > 0 ? w : 1;
				}
				size_t e = commentText.size();
				while (e > b) {
					// decode backwards: trim ASCII then handle multibyte by
					// scanning tail bytes
					char c = commentText[e - 1];
					bool trim = c == ' ' || c == '\t' || c == '\n' ||
								c == '\r' || c == '/' || c == '*' ||
								c == '\v' || c == '\f';
					if (!trim && (c & 0x80) == 0) {
						break;
					}
					if ((c & 0x80) != 0) {
						// multibyte tail — decode the last rune
						size_t start = e - 1;
						while (start > b &&
							   (commentText[start] & 0xC0) == 0x80) {
							start--;
						}
						int w = 0;
						char32_t ch = decodeUtf8Rune(
							commentText.substr(start), &w);
						if (!(isWhiteSpaceSingleLine(ch) ||
							  isLineBreak(ch))) {
							break;
						}
						e = start;
						continue;
					}
					e--;
				}
				if (commentText.substr(b, e - b) == name) {
					found = true;
					break;
				}
		}
		return found;
	}

	// inlay_hints.go:891 — getTypeAnnotationPosition
	int getTypeAnnotationPosition(::tsc::Node* decl) {
		::tsc::Node* closeParenToken = astnav::findChildOfKind(
			decl, Kind::CloseParenToken, file);
		if (closeParenToken != nullptr) {
			return int(closeParenToken->end());
		}
		return int(decl->parameterList()->end());
	}
};

// inlay_hints.go:942 — isAnyInlayHintEnabled
bool isAnyInlayHintEnabled(
	const lsutil::InlayHintsPreferences& preferences) {
	return preferences.IncludeInlayParameterNameHints !=
			   lsutil::IncludeInlayParameterNameHintsNone ||
		   tristateIsTrue(
			   preferences.IncludeInlayFunctionParameterTypeHints) ||
		   tristateIsTrue(preferences.IncludeInlayVariableTypeHints) ||
		   tristateIsTrue(
			   preferences.IncludeInlayPropertyDeclarationTypeHints) ||
		   tristateIsTrue(
			   preferences.IncludeInlayFunctionLikeReturnTypeHints) ||
		   tristateIsTrue(preferences.IncludeInlayEnumMemberValueHints);
}

} // namespace

// ============================================================================
// inlay_hints.go:24 — ProvideInlayHint
// ============================================================================
lsp::lsproto::InlayHintResponse LanguageService::ProvideInlayHint(
	gostd::Context ctx, lsp::lsproto::InlayHintParams* params) {
	lsutil::UserPreferences userPreferences = UserPreferences();
	lsutil::InlayHintsPreferences inlayHintPreferences =
		userPreferences.InlayHintsPreferences;
	lsp::lsproto::InlayHintsOrNull nullResult;
	if (!isAnyInlayHintEnabled(inlayHintPreferences)) {
		nullResult.InlayHints = nullptr;
		return nullResult;
	}

	auto [program, file] = getProgramAndFile(params->TextDocument.Uri);
	lsutil::QuotePreference quotePreference =
		lsutil::GetQuotePreference(file, userPreferences);

	auto mappedRanges = converters->FromLSPRangeIntersectingForSourceFile(
		file, params->Range, spanmap::FeatureInlayHints);
	std::vector<std::shared_ptr<lsp::lsproto::InlayHint>> result;
	result.reserve(mappedRanges.size());
	for (auto& mapped : mappedRanges) {
		SourceFile* projection = mapped.Script;
		auto [checker, done] = program->GetTypeChecker(ctx);
		struct Deferred {
			std::function<void()> f;
			~Deferred() { f(); }
		} defer{done};
		inlayHintState inlayHintState;
		inlayHintState.ctx = ctx;
		inlayHintState.span = mapped.Span;
		inlayHintState.preferences = inlayHintPreferences;
		inlayHintState.quotePreference = quotePreference;
		inlayHintState.file = projection;
		inlayHintState.checker = checker;
		inlayHintState.converters = converters;
		inlayHintState.visit(projection->asNode());
		result.insert(result.end(), inlayHintState.result.begin(),
					  inlayHintState.result.end());
	}
	lsp::lsproto::InlayHintsOrNull out;
	out.InlayHints = std::make_shared<lsp::lsproto::Slice<
	    std::shared_ptr<lsp::lsproto::InlayHint>>>(std::move(result));
	return out;
}

} // namespace tsc::ls
