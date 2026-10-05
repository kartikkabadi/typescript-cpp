// Port of tsc/internal/checker/grammarchecks.go — the per-node grammar
// validation pass (the checkGrammar* family) plus the checker utilities it
// calls. Functions are ported in file order; unported dependencies are marked
// TSC_UNREACHABLE("name — ported with the <slice> slice").
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/checker/checker.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"

namespace tsc {
namespace checker {

// ---------------------------------------------------------------------------
// File-local helpers — checker-package free functions (utilities.go /
// checker.go / relater.go) used by grammarchecks.go, plus small core.* shims.
// ---------------------------------------------------------------------------

// core.Find: first element matching pred, or nullptr.
template <typename T, typename Pred>
static T* findInSlice(const std::vector<T*>& slice, Pred pred) {
	auto it = std::find_if(slice.begin(), slice.end(), pred);
	return it == slice.end() ? nullptr : *it;
}

// core.LastOrNil: last element, or nullptr for an empty slice.
template <typename T>
static T* lastOrNil(const std::vector<T*>& slice) {
	return slice.empty() ? nullptr : slice.back();
}

// core.Some / core.Every on slices.
template <typename T, typename Pred>
static bool someInSlice(const std::vector<T*>& slice, Pred pred) {
	return std::any_of(slice.begin(), slice.end(), pred);
}

template <typename T, typename Pred>
static bool everyInSlice(const std::vector<T*>& slice, Pred pred) {
	return std::all_of(slice.begin(), slice.end(), pred);
}

// utilities.go: hasAsyncModifier
static bool hasAsyncModifier(Node* node) {
	return hasSyntacticModifier(node, ModifierFlagsAsync);
}

// utilities.go: hasReadonlyModifier
static bool hasReadonlyModifier(Node* node) {
	return hasModifier(node, ModifierFlagsReadonly);
}

// utilities.go: isOptionalDeclaration
static bool isOptionalDeclaration(Node* declaration) {
	return hasQuestionToken(declaration);
}

// utilities.go: isDeclarationReadonly
static bool isDeclarationReadonly(Node* declaration) {
	return (getCombinedModifierFlags(declaration) & ModifierFlagsReadonly) != 0 &&
	       !isParameterPropertyDeclaration(declaration, declaration->parent);
}

// utilities.go: getContainingFunctionOrClassStaticBlock
static Node* getContainingFunctionOrClassStaticBlock(Node* node) {
	return findAncestor(node->parent, isFunctionLikeOrClassStaticBlockDeclaration);
}

// utilities.go: isVariableDeclarationInVariableStatement
static bool isVariableDeclarationInVariableStatement(Node* node) {
	return isVariableDeclarationList(node->parent) && isVariableStatement(node->parent->parent);
}

// utilities.go: GetSetAccessorValueParameter
static Node* GetSetAccessorValueParameter(Node* accessor) {
	auto parameters = accessor->parameters();
	if (!parameters.empty()) {
		bool hasThis = parameters.size() == 2 && isThisParameter(parameters[0]);
		return parameters[hasThis ? 1 : 0];
	}
	return nullptr;
}

// checker.go: isRestParameter
static bool isRestParameter(Node* param) {
	return param->as<ParameterDeclaration>()->DotDotDotToken != nullptr;
}

// checker.go: someType
template <typename F>
static bool someType(Type* t, F f) {
	if ((t->flags & TypeFlagsUnion) != 0) {
		return someInSlice(t->types(), f);
	}
	return f(t);
}

// checker.go: everyType
template <typename F>
static bool everyType(Type* t, F f) {
	if ((t->flags & TypeFlagsUnion) != 0) {
		return everyInSlice(t->types(), f);
	}
	return f(t);
}

// relater.go: visibilityToString
static std::string visibilityToString(ModifierFlags flags) {
	if (flags == ModifierFlagsPrivate) {
		return "private";
	}
	if (flags == ModifierFlagsProtected) {
		return "protected";
	}
	return "public";
}

// checker.go: getVerbatimModuleSyntaxErrorMessage
static const DiagnosticMessage* getVerbatimModuleSyntaxErrorMessage(Node* node) {
	SourceFile* sourceFile = getSourceFileOfNode(node);
	std::string_view fileName = sourceFile->FileName();

	// Check if the file is .cts or .cjs (CommonJS-specific extensions)
	if (tspath::fileExtensionIsOneOf(fileName, {tspath::extensionCts, tspath::extensionCjs})) {
		return ECMAScript_imports_and_exports_cannot_be_written_in_a_CommonJS_file_under_verbatimModuleSyntax;
	}
	// For .ts, .tsx, .js, etc.
	return ECMAScript_imports_and_exports_cannot_be_written_in_a_CommonJS_file_under_verbatimModuleSyntax_Adjust_the_type_field_in_the_nearest_package_json_to_make_this_file_an_ECMAScript_module_or_adjust_your_verbatimModuleSyntax_module_and_moduleResolution_settings_in_TypeScript;
}

// ---------------------------------------------------------------------------
// grammarchecks.go
// ---------------------------------------------------------------------------

bool Checker::grammarErrorOnFirstToken(Node* node, const DiagnosticMessage* message,
									   std::vector<std::string> args) {
	SourceFile* sourceFile = getSourceFileOfNode(node);
	if (!hasParseDiagnostics(sourceFile)) {
		TextRange span = getRangeOfTokenAtPosition(sourceFile, node->pos());
		addDiagnostic(newDiagnostic(sourceFile, span, message, args));
		return true;
	}
	return false;
}

bool Checker::grammarErrorAtPos(Node* nodeForSourceFile, int start, int length,
								const DiagnosticMessage* message,
								std::vector<std::string> args) {
	SourceFile* sourceFile = getSourceFileOfNode(nodeForSourceFile);
	if (!hasParseDiagnostics(sourceFile)) {
		addDiagnostic(newDiagnostic(sourceFile, TextRange{start, start + length}, message, args));
		return true;
	}
	return false;
}

bool Checker::grammarErrorOnNode(Node* node, const DiagnosticMessage* message,
								 std::vector<std::string> args) {
	SourceFile* sourceFile = getSourceFileOfNode(node);
	if (!hasParseDiagnostics(sourceFile)) {
		error(node, message, std::move(args));
		return true;
	}
	return false;
}

bool Checker::grammarErrorOnNodeSkippedOnNoEmit(Node* node, const DiagnosticMessage* message,
												std::vector<std::string> args) {
	SourceFile* sourceFile = getSourceFileOfNode(node);
	if (!hasParseDiagnostics(sourceFile)) {
		Diagnostic* d = NewDiagnosticForNode(node, message, args);
		d->SetSkippedOnNoEmit();
		addDiagnostic(d);
		return true;
	}
	return false;
}

// checker.go: hasParseDiagnostics
bool Checker::hasParseDiagnostics(SourceFile* sourceFile) {
	return !sourceFile->diagnostics.empty();
}

// grammarchecks.go: isInitializerStringOrNumberLiteralExpression
static bool isInitializerStringOrNumberLiteralExpression(Node* expr);

// grammarchecks.go: isInitializerBigIntLiteralExpression
static bool isInitializerBigIntLiteralExpression(Node* expr);

// grammarchecks.go: getIdentifierFromEntityNameExpression
[[maybe_unused]] static Node* getIdentifierFromEntityNameExpression(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
		return node;
	case Kind::PropertyAccessExpression:
		return node->as<PropertyAccessExpression>()->name;
	default:
		return nullptr;
	}
}

bool Checker::checkGrammarRegularExpressionLiteral(RegularExpressionLiteral* node) {
	SourceFile* sourceFile = getSourceFileOfNode(node);
	if (!hasParseDiagnostics(sourceFile)) {
		Diagnostic* lastError = nullptr;
		if (!regExpScanner.has_value()) {
			regExpScanner.emplace();
		}
		regExpScanner->setScriptTarget(languageVersion);
		regExpScanner->setLanguageVariant(sourceFile->LanguageVariant);
		regExpScanner->setOnError(
			[this, sourceFile, &lastError](const DiagnosticMessage* message, int start,
										   int length, const std::vector<std::string>& args) {
				if (message->category == DiagnosticCategory::Message && lastError != nullptr &&
					start == lastError->Pos() && length == lastError->Len()) {
					// For providing spelling suggestions.
					Diagnostic* err = newDiagnostic(nullptr, TextRange{start, start + length}, message, args);
					lastError->AddRelatedInfo(err);
				} else if (lastError == nullptr || start != lastError->Pos()) {
					lastError = newDiagnostic(sourceFile, TextRange{start, start + length}, message, args);
					lastError = addDiagnostic(lastError);
				}
			});
		regExpScanner->setText(sourceFile->text);
		regExpScanner->resetTokenState(node->pos());
		regExpScanner->scan();
		[[maybe_unused]] bool tokenIsRegularExpressionLiteral =
			regExpScanner->reScanSlashToken(true) == Kind::RegularExpressionLiteral;
		regExpScanner->setText("");
		regExpScanner->setOnError(nullptr);
		TSC_ASSERT(tokenIsRegularExpressionLiteral, "Expected a regular expression literal");
		return lastError != nullptr;
	}
	return false;
}

bool Checker::checkGrammarPrivateIdentifierExpression(PrivateIdentifier* privId) {
	Node* privIdAsNode = privId->asNode();
	if (getContainingClass(privId->asNode()) == nullptr) {
		return grammarErrorOnNode(privId->asNode(), Private_identifiers_are_not_allowed_outside_class_bodies);
	}

	if (!isForInStatement(privId->parent)) {
		if (!isExpressionNode(privIdAsNode)) {
			return grammarErrorOnNode(privIdAsNode, Private_identifiers_are_only_allowed_in_class_bodies_and_may_only_be_used_as_part_of_a_class_member_declaration_property_access_or_on_the_left_hand_side_of_an_in_expression);
		}

		bool isInOperation = isBinaryExpression(privId->parent) &&
							 privId->parent->as<BinaryExpression>()->OperatorToken->kind == Kind::InKeyword;
		if (getSymbolForPrivateIdentifierExpression(privIdAsNode) == nullptr && !isInOperation) {
			return grammarErrorOnNode(privIdAsNode, Cannot_find_name_0, {privId->Text});
		}
	}

	return false;
}

// checker.go: getSymbolForPrivateIdentifierExpression
Symbol* Checker::getSymbolForPrivateIdentifierExpression(Node* node) {
	SymbolNodeLinks* links = symbolNodeLinks.Get(node);
	if (links->resolvedSymbol == nullptr) {
		links->resolvedSymbol = lookupSymbolForPrivateIdentifierDeclaration(node->text(), node);
	}
	return links->resolvedSymbol;
}

bool Checker::checkGrammarMappedType(MappedTypeNode* node) {
	if (!node->Members->nodes.empty()) {
		return grammarErrorOnNode(node->Members->nodes[0], A_mapped_type_may_not_declare_properties_or_methods);
	}
	return false;
}

bool Checker::checkGrammarDecorator(Decorator* decorator) {
	SourceFile* sourceFile = getSourceFileOfNode(decorator->asNode());
	if (!hasParseDiagnostics(sourceFile)) {
		Node* node = decorator->Expression;

		// DecoratorParenthesizedExpression :
		//   `(` Expression `)`

		if (isParenthesizedExpression(node)) {
			return false;
		}

		bool canHaveCallExpression = true;
		Node* errorNode = nullptr;
		for (;;) {
			// Allow TS syntax such as non-null assertions and instantiation expressions
			if (isExpressionWithTypeArguments(node) || isNonNullExpression(node)) {
				node = node->expression();
				continue;
			}

			// DecoratorCallExpression :
			//   DecoratorMemberExpression Arguments

			if (isCallExpression(node)) {
				CallExpression* callExpr = node->as<CallExpression>();
				if (!canHaveCallExpression) {
					errorNode = node;
				}
				if (callExpr->QuestionDotToken != nullptr) {
					// Even if we already have an error node, error at the `?.` token since it appears earlier.
					errorNode = callExpr->QuestionDotToken;
				}
				node = callExpr->Expression;
				canHaveCallExpression = false;
				continue;
			}

			// DecoratorMemberExpression :
			//   IdentifierReference
			//   DecoratorMemberExpression `.` IdentifierName
			//   DecoratorMemberExpression `.` PrivateIdentifier

			if (isPropertyAccessExpression(node)) {
				PropertyAccessExpression* propertyAccessExpr = node->as<PropertyAccessExpression>();
				if (propertyAccessExpr->QuestionDotToken != nullptr) {
					// Even if we already have an error node, error at the `?.` token since it appears earlier.
					errorNode = propertyAccessExpr->QuestionDotToken;
				}
				node = propertyAccessExpr->Expression;
				canHaveCallExpression = false;
				continue;
			}

			if (!isIdentifier(node)) {
				// Even if we already have an error node, error at this node since it appears earlier.
				errorNode = node;
			}

			break;
		}

		if (errorNode != nullptr) {
			Diagnostic* err = error(decorator->Expression, Expression_must_be_enclosed_in_parentheses_to_be_used_as_a_decorator);
			err->AddRelatedInfo(createDiagnosticForNode(errorNode, Invalid_syntax_in_decorator));
			return true;
		}
	}

	return false;
}

bool Checker::checkGrammarExportDeclaration(ExportDeclaration* node) {
	if (node->IsTypeOnly && node->ExportClause != nullptr && node->ExportClause->kind == Kind::NamedExports) {
		return checkGrammarTypeOnlyNamedImportsOrExports(node->ExportClause);
	}
	return false;
}

bool Checker::checkGrammarModuleElementContext(Node* node, const DiagnosticMessage* errorMessage) {
	bool isInAppropriateContext = node->parent->kind == Kind::SourceFile ||
								  node->parent->kind == Kind::ModuleBlock ||
								  node->parent->kind == Kind::ModuleDeclaration;
	if (!isInAppropriateContext) {
		grammarErrorOnFirstToken(node, errorMessage);
	}
	return !isInAppropriateContext;
}

bool Checker::checkGrammarModifiers(Node* node /*Union[HasModifiers, HasDecorators, HasIllegalModifiers, HasIllegalDecorators]*/) {
	if (node->modifiers() == nullptr) {
		return false;
	}
	if (reportObviousDecoratorErrors(node) || reportObviousModifierErrors(node)) {
		return true;
	}
	if (isThisParameter(node)) {
		return grammarErrorOnFirstToken(node, Neither_decorators_nor_modifiers_may_be_applied_to_this_parameters);
	}
	NodeFlags blockScopeKind = NodeFlagsNone;
	if (isVariableStatement(node)) {
		blockScopeKind = node->as<VariableStatement>()->DeclarationList->flags & NodeFlagsBlockScoped;
	}
	Node* lastStatic = nullptr;
	Node* lastDeclare = nullptr;
	Node* lastAsync = nullptr;
	Node* lastOverride = nullptr;
	Node* firstDecorator = nullptr;
	ModifierFlags flags = ModifierFlagsNone;
	bool sawExportBeforeDecorators = false;
	// We parse decorators and modifiers in four contiguous chunks:
	// [...leadingDecorators, ...leadingModifiers, ...trailingDecorators, ...trailingModifiers]. It is an error to
	// have both leading and trailing decorators.
	bool hasLeadingDecorators = false;
	std::vector<Node*> modifiers = node->modifierNodes();
	for (Node* modifier : modifiers) {
		if (isDecorator(modifier)) {
			if (!nodeCanBeDecorated(legacyDecorators, node, node->parent, node->parent->parent)) {
				if (node->kind == Kind::MethodDeclaration && !nodeIsPresent(node->body())) {
					return grammarErrorOnFirstToken(node, A_decorator_can_only_decorate_a_method_implementation_not_an_overload);
				} else {
					return grammarErrorOnFirstToken(node, Decorators_are_not_valid_here);
				}
			} else if (legacyDecorators && (node->kind == Kind::GetAccessor || node->kind == Kind::SetAccessor)) {
				AllAccessorDeclarations accessors =
					getAllAccessorDeclarationsForDeclaration(node, getSymbolOfDeclaration(node)->declarations);
				if (hasDecorators(accessors.firstAccessor) && node == accessors.secondAccessor) {
					return grammarErrorOnFirstToken(node, Decorators_cannot_be_applied_to_multiple_get_Slashset_accessors_of_the_same_name);
				}
			}

			// if we've seen any modifiers aside from `export`, `default`, or another decorator, then this is an invalid position
			if ((flags & ~(ModifierFlagsExportDefault | ModifierFlagsDecorator)) != 0) {
				return grammarErrorOnNode(modifier, Decorators_are_not_valid_here);
			}

			// if we've already seen leading decorators and leading modifiers, then trailing decorators are an invalid position
			if (hasLeadingDecorators && (flags & ModifierFlagsModifier) != 0) {
				if (firstDecorator == nullptr) {
					TSC_UNREACHABLE("Expected firstDecorator to be set");
				}
				SourceFile* sourceFile = getSourceFileOfNode(modifier);
				if (!hasParseDiagnostics(sourceFile)) {
					Diagnostic* err = error(modifier, Decorators_may_not_appear_after_export_or_export_default_if_they_also_appear_before_export);
					err->AddRelatedInfo(createDiagnosticForNode(firstDecorator, Decorator_used_before_export_here));
					return true;
				}
				return false;
			}

			flags |= ModifierFlagsDecorator;

			// if we have not yet seen a modifier, then these are leading decorators
			if ((flags & ModifierFlagsModifier) == 0) {
				hasLeadingDecorators = true;
			} else if ((flags & ModifierFlagsExport) != 0) {
				sawExportBeforeDecorators = true;
			}

			if (firstDecorator == nullptr) {
				firstDecorator = modifier;
			}
		} else {
			if (modifier->kind != Kind::ReadonlyKeyword) {
				if (node->kind == Kind::PropertySignature || node->kind == Kind::MethodSignature) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_type_member, {std::string(tokenToString(modifier->kind))});
				}
				if (node->kind == Kind::IndexSignature && (modifier->kind != Kind::StaticKeyword || !isClassLike(node->parent))) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_an_index_signature, {std::string(tokenToString(modifier->kind))});
				}
			}
			if (modifier->kind != Kind::InKeyword && modifier->kind != Kind::OutKeyword && modifier->kind != Kind::ConstKeyword) {
				if (node->kind == Kind::TypeParameter) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_type_parameter, {std::string(tokenToString(modifier->kind))});
				}
			}
			switch (modifier->kind) {
			case Kind::ConstKeyword:
				if (node->kind != Kind::EnumDeclaration && node->kind != Kind::TypeParameter) {
					return grammarErrorOnNode(node, A_class_member_cannot_have_the_0_keyword, {std::string(tokenToString(Kind::ConstKeyword))});
				}
				{
					Node* parent = node->parent;
					if (node->kind == Kind::TypeParameter) {
						if (!(isFunctionLikeDeclaration(parent) || isClassLike(parent) ||
							  isFunctionTypeNode(parent) || isConstructorTypeNode(parent) ||
							  isCallSignatureDeclaration(parent) || isConstructSignatureDeclaration(parent) ||
							  isMethodSignatureDeclaration(parent))) {
							return grammarErrorOnNode(modifier, X_0_modifier_can_only_appear_on_a_type_parameter_of_a_function_method_or_class, {std::string(tokenToString(modifier->kind))});
						}
					}
				}
				break;
			case Kind::OverrideKeyword:
				// If node.kind === SyntaxKind.Parameter, checkParameter reports an error if it's not a parameter property.
				if ((flags & ModifierFlagsOverride) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_already_seen, {"override"});
				} else if ((flags & ModifierFlagsAmbient) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_1_modifier, {"override", "declare"});
				} else if ((flags & ModifierFlagsReadonly) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"override", "readonly"});
				} else if ((flags & ModifierFlagsAccessor) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"override", "accessor"});
				} else if ((flags & ModifierFlagsAsync) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"override", "async"});
				}
				flags |= ModifierFlagsOverride;
				lastOverride = modifier;
				break;

			case Kind::PublicKeyword:
			case Kind::ProtectedKeyword:
			case Kind::PrivateKeyword:
				{
					std::string text = visibilityToString(modifierToFlag(modifier->kind));

					if ((flags & ModifierFlagsAccessibilityModifier) != 0) {
						return grammarErrorOnNode(modifier, Accessibility_modifier_already_seen);
					} else if ((flags & ModifierFlagsOverride) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {text, "override"});
					} else if ((flags & ModifierFlagsStatic) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {text, "static"});
					} else if ((flags & ModifierFlagsAccessor) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {text, "accessor"});
					} else if ((flags & ModifierFlagsReadonly) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {text, "readonly"});
					} else if ((flags & ModifierFlagsAsync) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {text, "async"});
					} else if (node->parent->kind == Kind::ModuleBlock || node->parent->kind == Kind::SourceFile) {
						return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_module_or_namespace_element, {text});
					} else if ((flags & ModifierFlagsAbstract) != 0) {
						if (modifier->kind == Kind::PrivateKeyword) {
							return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_1_modifier, {text, "abstract"});
						} else if ((modifier->flags & NodeFlagsReparsed) == 0) {
							return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {text, "abstract"});
						}
					} else if (isPrivateIdentifierClassElementDeclaration(node)) {
						return grammarErrorOnNode(modifier, An_accessibility_modifier_cannot_be_used_with_a_private_identifier);
					}
					flags |= modifierToFlag(modifier->kind);
				}
				break;
			case Kind::StaticKeyword:
				if ((flags & ModifierFlagsStatic) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_already_seen, {"static"});
				} else if ((flags & ModifierFlagsReadonly) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"static", "readonly"});
				} else if ((flags & ModifierFlagsAsync) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"static", "async"});
				} else if ((flags & ModifierFlagsAccessor) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"static", "accessor"});
				} else if (node->parent->kind == Kind::ModuleBlock || node->parent->kind == Kind::SourceFile) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_module_or_namespace_element, {"static"});
				} else if (node->kind == Kind::Parameter) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_parameter, {"static"});
				} else if ((flags & ModifierFlagsAbstract) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_1_modifier, {"static", "abstract"});
				} else if ((flags & ModifierFlagsOverride) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"static", "override"});
				}
				flags |= ModifierFlagsStatic;
				lastStatic = modifier;
				break;
			case Kind::AccessorKeyword:
				if ((flags & ModifierFlagsAccessor) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_already_seen, {"accessor"});
				} else if ((flags & ModifierFlagsReadonly) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_1_modifier, {"accessor", "readonly"});
				} else if ((flags & ModifierFlagsAmbient) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_1_modifier, {"accessor", "declare"});
				} else if (node->kind != Kind::PropertyDeclaration) {
					return grammarErrorOnNode(modifier, X_accessor_modifier_can_only_appear_on_a_property_declaration);
				}

				flags |= ModifierFlagsAccessor;
				break;
			case Kind::ReadonlyKeyword:
				if ((flags & ModifierFlagsReadonly) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_already_seen, {"readonly"});
				} else if (node->kind != Kind::PropertyDeclaration && node->kind != Kind::PropertySignature && node->kind != Kind::IndexSignature && node->kind != Kind::Parameter) {
					// If node.kind === SyntaxKind.Parameter, checkParameter reports an error if it's not a parameter property.
					return grammarErrorOnNode(modifier, X_readonly_modifier_can_only_appear_on_a_property_declaration_or_index_signature);
				} else if ((flags & ModifierFlagsAccessor) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_1_modifier, {"readonly", "accessor"});
				}
				flags |= ModifierFlagsReadonly;
				break;
			case Kind::ExportKeyword:
				if (compilerOptions->VerbatimModuleSyntax == Tristate::True && (node->flags & NodeFlagsAmbient) == 0 && node->kind != Kind::TypeAliasDeclaration && node->kind != Kind::InterfaceDeclaration && node->kind != Kind::ModuleDeclaration && node->parent->kind == Kind::SourceFile && program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node)) == ModuleKind::CommonJS) {
					return grammarErrorOnNode(modifier, A_top_level_export_modifier_cannot_be_used_on_value_declarations_in_a_CommonJS_module_when_verbatimModuleSyntax_is_enabled);
				}
				if ((flags & ModifierFlagsExport) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_already_seen, {"export"});
				} else if ((flags & ModifierFlagsAmbient) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"export", "declare"});
				} else if ((flags & ModifierFlagsAbstract) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"export", "abstract"});
				} else if ((flags & ModifierFlagsAsync) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"export", "async"});
				} else if (isClassLike(node->parent) && !isJSTypeAliasDeclaration(node)) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_class_elements_of_this_kind, {"export"});
				} else if (node->kind == Kind::Parameter) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_parameter, {"export"});
				} else if (blockScopeKind == NodeFlagsUsing) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_using_declaration, {"export"});
				} else if (blockScopeKind == NodeFlagsAwaitUsing) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_an_await_using_declaration, {"export"});
				}
				flags |= ModifierFlagsExport;
				break;
			case Kind::DefaultKeyword:
				{
					Node* container;
					if (node->parent->kind == Kind::SourceFile) {
						container = node->parent;
					} else {
						container = node->parent->parent;
					}
					if (container->kind == Kind::ModuleDeclaration && !isAmbientModule(container)) {
						return grammarErrorOnNode(modifier, A_default_export_can_only_be_used_in_an_ECMAScript_style_module);
					} else if (blockScopeKind == NodeFlagsUsing) {
						return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_using_declaration, {"default"});
					} else if (blockScopeKind == NodeFlagsAwaitUsing) {
						return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_an_await_using_declaration, {"default"});
					} else if ((flags & ModifierFlagsExport) == 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"export", "default"});
					} else if (sawExportBeforeDecorators) {
						return grammarErrorOnNode(firstDecorator, Decorators_are_not_valid_here);
					}

					flags |= ModifierFlagsDefault;
				}
				break;
			case Kind::DeclareKeyword:
				if ((flags & ModifierFlagsAmbient) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_already_seen, {"declare"});
				} else if ((flags & ModifierFlagsAsync) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_in_an_ambient_context, {"async"});
				} else if ((flags & ModifierFlagsOverride) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_in_an_ambient_context, {"override"});
				} else if (isClassLike(node->parent) && !isPropertyDeclaration(node)) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_class_elements_of_this_kind, {"declare"});
				} else if (node->kind == Kind::Parameter) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_parameter, {"declare"});
				} else if (blockScopeKind == NodeFlagsUsing) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_using_declaration, {"declare"});
				} else if (blockScopeKind == NodeFlagsAwaitUsing) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_an_await_using_declaration, {"declare"});
				} else if ((node->parent->flags & NodeFlagsAmbient) != 0 && node->parent->kind == Kind::ModuleBlock) {
					return grammarErrorOnNode(modifier, A_declare_modifier_cannot_be_used_in_an_already_ambient_context);
				} else if (isPrivateIdentifierClassElementDeclaration(node)) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_a_private_identifier, {"declare"});
				} else if ((flags & ModifierFlagsAccessor) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_1_modifier, {"declare", "accessor"});
				}
				flags |= ModifierFlagsAmbient;
				lastDeclare = modifier;
				break;
			case Kind::AbstractKeyword:
				if ((flags & ModifierFlagsAbstract) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_already_seen, {"abstract"});
				}
				if (node->kind != Kind::ClassDeclaration && node->kind != Kind::ConstructorType) {
					if (node->kind != Kind::MethodDeclaration && node->kind != Kind::PropertyDeclaration && node->kind != Kind::GetAccessor && node->kind != Kind::SetAccessor) {
						return grammarErrorOnNode(modifier, X_abstract_modifier_can_only_appear_on_a_class_method_or_property_declaration);
					}
					if (!(node->parent->kind == Kind::ClassDeclaration && hasSyntacticModifier(node->parent, ModifierFlagsAbstract))) {
						const DiagnosticMessage* message;
						if (node->kind == Kind::PropertyDeclaration) {
							message = Abstract_properties_can_only_appear_within_an_abstract_class;
						} else {
							message = Abstract_methods_can_only_appear_within_an_abstract_class;
						}
						return grammarErrorOnNode(modifier, message);
					}
					if ((flags & ModifierFlagsStatic) != 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_1_modifier, {"static", "abstract"});
					}
					if ((flags & ModifierFlagsPrivate) != 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_1_modifier, {"private", "abstract"});
					}
					if ((flags & ModifierFlagsAsync) != 0 && lastAsync != nullptr) {
						return grammarErrorOnNode(lastAsync, X_0_modifier_cannot_be_used_with_1_modifier, {"async", "abstract"});
					}
					if ((flags & ModifierFlagsOverride) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"abstract", "override"});
					}
					if ((flags & ModifierFlagsAccessor) != 0 && (modifier->flags & NodeFlagsReparsed) == 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"abstract", "accessor"});
					}
				}
				if (Node* name = node->name(); name != nullptr && name->kind == Kind::PrivateIdentifier) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_a_private_identifier, {"abstract"});
				}

				flags |= ModifierFlagsAbstract;
				break;
			case Kind::AsyncKeyword:
				if ((flags & ModifierFlagsAsync) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_already_seen, {"async"});
				} else if ((flags & ModifierFlagsAmbient) != 0 || (node->parent->flags & NodeFlagsAmbient) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_in_an_ambient_context, {"async"});
				} else if (node->kind == Kind::Parameter) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_appear_on_a_parameter, {"async"});
				}
				if ((flags & ModifierFlagsAbstract) != 0) {
					return grammarErrorOnNode(modifier, X_0_modifier_cannot_be_used_with_1_modifier, {"async", "abstract"});
				}
				flags |= ModifierFlagsAsync;
				lastAsync = modifier;
				break;
			case Kind::InKeyword:
			case Kind::OutKeyword:
				{
					ModifierFlags inOutFlag;
					if (modifier->kind == Kind::InKeyword) {
						inOutFlag = ModifierFlagsIn;
					} else {
						inOutFlag = ModifierFlagsOut;
					}
					std::string inOutText;
					if (modifier->kind == Kind::InKeyword) {
						inOutText = "in";
					} else {
						inOutText = "out";
					}
					Node* parent = node->parent;
					if (node->kind != Kind::TypeParameter || (parent != nullptr && !(isInterfaceDeclaration(parent) || isClassLike(parent) || isTypeOrJSTypeAliasDeclaration(parent)))) {
						return grammarErrorOnNode(modifier, X_0_modifier_can_only_appear_on_a_type_parameter_of_a_class_interface_or_type_alias, {inOutText});
					}
					if ((flags & inOutFlag) != 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_already_seen, {inOutText});
					}
					if ((inOutFlag & ModifierFlagsIn) != 0 && (flags & ModifierFlagsOut) != 0) {
						return grammarErrorOnNode(modifier, X_0_modifier_must_precede_1_modifier, {"in", "out"});
					}
					flags |= inOutFlag;
				}
				break;
			default:
				break;
			}
		}
	}

	if (node->kind == Kind::Constructor) {
		if ((flags & ModifierFlagsStatic) != 0) {
			return grammarErrorOnNode(lastStatic, X_0_modifier_cannot_appear_on_a_constructor_declaration, {"static"});
		}
		if ((flags & ModifierFlagsOverride) != 0) {
			return grammarErrorOnNode(lastOverride, X_0_modifier_cannot_appear_on_a_constructor_declaration, {"override"});
		}
		if ((flags & ModifierFlagsAsync) != 0) {
			return grammarErrorOnNode(lastAsync, X_0_modifier_cannot_appear_on_a_constructor_declaration, {"async"});
		}
		return false;
	} else if ((node->kind == Kind::ImportDeclaration || node->kind == Kind::JSImportDeclaration || node->kind == Kind::ImportEqualsDeclaration) && (flags & ModifierFlagsAmbient) != 0) {
		return grammarErrorOnNode(lastDeclare, A_0_modifier_cannot_be_used_with_an_import_declaration, {"declare"});
	} else if (node->kind == Kind::Parameter && (flags & ModifierFlagsParameterPropertyModifier) != 0 && isBindingPattern(node->name())) {
		return grammarErrorOnNode(node, A_parameter_property_may_not_be_declared_using_a_binding_pattern);
	} else if (node->kind == Kind::Parameter && (flags & ModifierFlagsParameterPropertyModifier) != 0 && node->as<ParameterDeclaration>()->DotDotDotToken != nullptr) {
		return grammarErrorOnNode(node, A_parameter_property_cannot_be_declared_using_a_rest_parameter);
	}
	if ((flags & ModifierFlagsAsync) != 0) {
		return checkGrammarAsyncModifier(node, lastAsync);
	}
	return false;
}

bool Checker::reportObviousModifierErrors(Node* node) {
	Node* modifier = findFirstIllegalModifier(node);
	if (modifier == nullptr) {
		return false;
	}
	return grammarErrorOnFirstToken(modifier, Modifiers_cannot_appear_here);
}

Node* Checker::findFirstModifierExcept(Node* node, Kind allowedModifier) {
	Node* modifier = findInSlice(node->modifierNodes(), isModifier);
	if (modifier != nullptr && modifier->kind != allowedModifier) {
		return modifier;
	}
	return nullptr;
}

Node* Checker::findFirstIllegalModifier(Node* node) {
	switch (node->kind) {
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::Constructor:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::IndexSignature:
	case Kind::ModuleDeclaration:
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ImportEqualsDeclaration:
	case Kind::ExportDeclaration:
	case Kind::ExportAssignment:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::Parameter:
	case Kind::TypeParameter:
	case Kind::JSTypeAliasDeclaration:
		return nullptr;
	case Kind::ClassStaticBlockDeclaration:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::NamespaceExportDeclaration:
	case Kind::MissingDeclaration:
		return findInSlice(node->modifierNodes(), isModifier);
	default:
		if (node->parent->kind == Kind::ModuleBlock || node->parent->kind == Kind::SourceFile) {
			return nullptr;
		}
		switch (node->kind) {
		case Kind::FunctionDeclaration:
			return findFirstModifierExcept(node, Kind::AsyncKeyword);
		case Kind::ClassDeclaration:
		case Kind::ConstructorType:
			return findFirstModifierExcept(node, Kind::AbstractKeyword);
		case Kind::ClassExpression:
		case Kind::InterfaceDeclaration:
		case Kind::TypeAliasDeclaration:
			return findInSlice(node->modifierNodes(), isModifier);
		case Kind::VariableStatement:
			if ((node->as<VariableStatement>()->DeclarationList->flags & NodeFlagsUsing) != 0) {
				return findFirstModifierExcept(node, Kind::AwaitKeyword);
			}
			return findInSlice(node->modifierNodes(), isModifier);
		case Kind::EnumDeclaration:
			return findFirstModifierExcept(node, Kind::ConstKeyword);
		default:
			TSC_UNREACHABLE("Unhandled case in findFirstIllegalModifier.");
		}
	}
}

bool Checker::reportObviousDecoratorErrors(Node* node) {
	Node* decorator = findFirstIllegalDecorator(node);
	if (decorator == nullptr) {
		return false;
	}
	return grammarErrorOnFirstToken(decorator, Decorators_are_not_valid_here);
}

Node* Checker::findFirstIllegalDecorator(Node* node) {
	if (canHaveIllegalDecorators(node)) {
		Node* decorator = findInSlice(node->modifierNodes(), isDecorator);
		return decorator;
	} else {
		return nullptr;
	}
}

bool Checker::checkGrammarAsyncModifier(Node* node, Node* asyncModifier) {
	switch (node->kind) {
	case Kind::MethodDeclaration:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
		return false;
	default:
		break;
	}

	return grammarErrorOnNode(asyncModifier, X_0_modifier_cannot_be_used_here, {"async"});
}

bool Checker::checkGrammarForDisallowedTrailingComma(NodeList* list, const DiagnosticMessage* diag) {
	if (list != nullptr && list->hasTrailingComma()) {
		return grammarErrorAtPos(list->nodes[0], list->end() - 1 /*len(",")*/, 1 /*len(",")*/, diag);
	}
	return false;
}

bool Checker::checkGrammarTypeParameterList(NodeList* typeParameters, SourceFile* file) {
	if (typeParameters != nullptr && typeParameters->nodes.empty()) {
		int start = typeParameters->pos() - 1 /*len("<")*/;
		int end = skipTrivia(file->text, typeParameters->end()) + 1 /*len(">")*/;
		return grammarErrorAtPos(file->asNode(), start, end - start, Type_parameter_list_cannot_be_empty);
	}
	return false;
}

bool Checker::checkGrammarParameterList(NodeList* parameters) {
	bool seenOptionalParameter = false;
	int parameterCount = static_cast<int>(parameters->nodes.size());

	for (int i = 0; i < parameterCount; i++) {
		ParameterDeclaration* parameter = parameters->nodes[i]->as<ParameterDeclaration>();
		if (parameter->DotDotDotToken != nullptr) {
			if (i != parameterCount - 1) {
				return grammarErrorOnNode(parameter->DotDotDotToken, A_rest_parameter_must_be_last_in_a_parameter_list);
			}
			if ((parameter->flags & NodeFlagsAmbient) == 0) {
				checkGrammarForDisallowedTrailingComma(parameters, A_rest_parameter_or_binding_pattern_may_not_have_a_trailing_comma);
			}

			if (parameter->QuestionToken != nullptr) {
				return grammarErrorOnNode(parameter->QuestionToken, A_rest_parameter_cannot_be_optional);
			}

			if (parameter->Initializer != nullptr) {
				return grammarErrorOnNode(parameter->name, A_rest_parameter_cannot_have_an_initializer);
			}
		} else if (isOptionalDeclaration(parameter->asNode())) {
			seenOptionalParameter = true;
			// A reparsed '?' token indicates a bracketed name in @param tag
			if (parameter->QuestionToken != nullptr && (parameter->QuestionToken->flags & NodeFlagsReparsed) == 0 && parameter->Initializer != nullptr) {
				return grammarErrorOnNode(parameter->name, Parameter_cannot_have_question_mark_and_initializer);
			}
		} else if (seenOptionalParameter && parameter->Initializer == nullptr) {
			return grammarErrorOnNode(parameter->name, A_required_parameter_cannot_follow_an_optional_parameter);
		}
	}

	return false;
}

bool Checker::checkGrammarForUseStrictSimpleParameterList(Node* node) {
	if (languageVersion >= ScriptTarget::ES2016) {
		Node* body = node->body();
		Node* useStrictDirective = nullptr;
		if (body != nullptr && isBlock(body)) {
			useStrictDirective = findUseStrictPrologue(getSourceFileOfNode(node), body->statements());
		}
		if (useStrictDirective != nullptr) {
			std::vector<Node*> nonSimpleParameters;
			for (Node* n : node->parameters()) {
				ParameterDeclaration* parameter = n->as<ParameterDeclaration>();
				if (parameter->Initializer != nullptr || isBindingPattern(parameter->name) || isRestParameter(parameter->asNode())) {
					nonSimpleParameters.push_back(n);
				}
			}
			if (!nonSimpleParameters.empty()) {
				for (Node* parameter : nonSimpleParameters) {
					Diagnostic* err = error(parameter, This_parameter_is_not_allowed_with_use_strict_directive);
					err->AddRelatedInfo(createDiagnosticForNode(useStrictDirective, X_use_strict_directive_used_here));
				}

				Diagnostic* err = error(useStrictDirective, X_use_strict_directive_cannot_be_used_with_non_simple_parameter_list);
				for (size_t index = 0; index < nonSimpleParameters.size(); index++) {
					Node* parameter = nonSimpleParameters[index];
					const DiagnosticMessage* relatedMessage;
					if (index == 0) {
						relatedMessage = Non_simple_parameter_declared_here;
					} else {
						relatedMessage = X_and_here;
					}
					err->AddRelatedInfo(createDiagnosticForNode(parameter, relatedMessage));
				}

				return true;
			}
		}
	}
	return false;
}

bool Checker::checkGrammarFunctionLikeDeclaration(Node* node) {
	// Prevent cascading error by short-circuit
	SourceFile* file = getSourceFileOfNode(node);
	auto funcData = node->functionLikeData();
	return checkGrammarModifiers(node) || checkGrammarTypeParameterList(*funcData.typeParameters, file) ||
		   checkGrammarParameterList(*funcData.parameters) || checkGrammarArrowFunction(node, file) ||
		   (isFunctionLikeDeclaration(node) && checkGrammarForUseStrictSimpleParameterList(node));
}

bool Checker::checkGrammarClassLikeDeclaration(Node* node) {
	SourceFile* file = getSourceFileOfNode(node);
	return checkGrammarClassDeclarationHeritageClauses(node, file) || checkGrammarTypeParameterList(node->typeParameterList(), file);
}

bool Checker::checkGrammarArrowFunction(Node* node, SourceFile* file) {
	if (!isArrowFunction(node)) {
		return false;
	}

	ArrowFunction* arrowFunc = node->as<ArrowFunction>();
	NodeList* typeParameters = arrowFunc->TypeParameters;
	if (typeParameters != nullptr) {
		const std::vector<Node*>& typeParamNodes = typeParameters->nodes;
		bool hasConstraint = !typeParamNodes.empty() && typeParamNodes[0]->as<TypeParameterDeclaration>()->Constraint != nullptr;
		if (!(typeParamNodes.size() > 1 || typeParameters->hasTrailingComma() || hasConstraint)) {
			if (tspath::fileExtensionIsOneOf(file->FileName(), {tspath::extensionMts, tspath::extensionCts})) {
				// TODO(danielr): should we return early here?
				grammarErrorOnNode(typeParameters->nodes[0], This_syntax_is_reserved_in_files_with_the_mts_or_cts_extension_Add_a_trailing_comma_or_explicit_constraint);
			}
		}
	}

	Node* equalsGreaterThanToken = arrowFunc->EqualsGreaterThanToken;
	std::string_view arrowFullText(file->text.data() + equalsGreaterThanToken->pos(),
								   equalsGreaterThanToken->end() - equalsGreaterThanToken->pos());
	return std::any_of(arrowFullText.begin(), arrowFullText.end(), isLineBreak) &&
		   grammarErrorOnNode(equalsGreaterThanToken, Line_terminator_not_permitted_before_arrow);
}

bool Checker::checkGrammarIndexSignatureParameters(IndexSignatureDeclaration* node) {
	const std::vector<Node*>& paramNodes = node->Parameters->nodes;

	if (paramNodes.empty()) {
		return grammarErrorOnNode(node->asNode(), An_index_signature_must_have_exactly_one_parameter);
	}

	ParameterDeclaration* parameter = paramNodes[0]->as<ParameterDeclaration>();
	if (paramNodes.size() != 1) {
		return grammarErrorOnNode(parameter->name, An_index_signature_must_have_exactly_one_parameter);
	}

	checkGrammarForDisallowedTrailingComma(node->Parameters, An_index_signature_cannot_have_a_trailing_comma);
	if (parameter->DotDotDotToken != nullptr) {
		return grammarErrorOnNode(parameter->DotDotDotToken, An_index_signature_cannot_have_a_rest_parameter);
	}
	if (parameter->modifiers != nullptr) {
		return grammarErrorOnNode(parameter->name, An_index_signature_parameter_cannot_have_an_accessibility_modifier);
	}
	if (parameter->QuestionToken != nullptr) {
		return grammarErrorOnNode(parameter->QuestionToken, An_index_signature_parameter_cannot_have_a_question_mark);
	}
	if (parameter->Initializer != nullptr) {
		return grammarErrorOnNode(parameter->name, An_index_signature_parameter_cannot_have_an_initializer);
	}
	Node* typeNode = parameter->Type;
	if (typeNode == nullptr) {
		return grammarErrorOnNode(parameter->name, An_index_signature_parameter_must_have_a_type_annotation);
	}
	Type* t = getTypeFromTypeNode(typeNode);
	if (someType(t, [](Type* t) {
			return (t->flags & TypeFlagsStringOrNumberLiteralOrUnique) != 0;
		}) || isGenericType(t)) {
		return grammarErrorOnNode(parameter->name, An_index_signature_parameter_type_cannot_be_a_literal_type_or_generic_type_Consider_using_a_mapped_object_type_instead);
	}
	if (!everyType(t, [this](Type* t) {
			return isValidIndexKeyType(t);
		})) {
		return grammarErrorOnNode(parameter->name, An_index_signature_parameter_type_must_be_string_number_symbol_or_a_template_literal_type);
	}
	if (node->Type == nullptr) {
		return grammarErrorOnNode(node->asNode(), An_index_signature_must_have_a_type_annotation);
	}
	return false;
}

bool Checker::checkGrammarIndexSignature(IndexSignatureDeclaration* node) {
	// Prevent cascading error by short-circuit
	return checkGrammarModifiers(node->asNode()) || checkGrammarIndexSignatureParameters(node);
}

bool Checker::checkGrammarForAtLeastOneTypeArgument(Node* node, NodeList* typeArguments) {
	if (typeArguments != nullptr && typeArguments->nodes.empty()) {
		SourceFile* sourceFile = getSourceFileOfNode(node);
		int start = typeArguments->pos() - 1 /*len("<")*/;
		int end = skipTrivia(sourceFile->text, typeArguments->end()) + 1 /*len(">")*/;
		return grammarErrorAtPos(sourceFile->asNode(), start, end - start, Type_argument_list_cannot_be_empty);
	}
	return false;
}

bool Checker::checkGrammarTypeArguments(Node* node, NodeList* typeArguments) {
	return checkGrammarForDisallowedTrailingComma(typeArguments, Trailing_comma_not_allowed) || checkGrammarForAtLeastOneTypeArgument(node, typeArguments);
}

bool Checker::checkGrammarTaggedTemplateChain(TaggedTemplateExpression* node) {
	if (node->QuestionDotToken != nullptr || (node->flags & NodeFlagsOptionalChain) != 0) {
		return grammarErrorOnNode(node->Template, Tagged_template_expressions_are_not_permitted_in_an_optional_chain);
	}
	return false;
}

bool Checker::checkGrammarHeritageClause(HeritageClause* node) {
	NodeList* types = node->Types;
	if (checkGrammarForDisallowedTrailingComma(types, Trailing_comma_not_allowed)) {
		return true;
	}
	if (types != nullptr && types->nodes.empty()) {
		std::string listType(tokenToString(node->Token));
		// TODO(danielr): why not error on the token?
		return grammarErrorAtPos(node->asNode(), types->pos(), 0, X_0_list_cannot_be_empty, {listType});
	}

	for (Node* heritageTypeNode : types->nodes) { //nolint:modernize
		if (checkGrammarExpressionWithTypeArguments(heritageTypeNode)) {
			return true;
		}
	}
	return false;
}

bool Checker::checkGrammarExpressionWithTypeArguments(Node* node /*Union[ExpressionWithTypeArguments, TypeQuery]*/) {
	if (isExpressionWithTypeArguments(node) && node->expression()->kind == Kind::ImportKeyword && node->typeArgumentList() != nullptr) {
		return grammarErrorOnNode(node, This_use_of_import_is_invalid_import_calls_can_be_written_but_they_must_have_parentheses_and_cannot_have_type_arguments);
	}
	return checkGrammarTypeArguments(node, node->typeArgumentList());
}

bool Checker::checkGrammarClassDeclarationHeritageClauses(Node* node /*ClassLikeDeclaration*/, SourceFile* file) {
	bool seenExtendsClause = false;
	bool seenImplementsClause = false;

	auto classLikeData = node->classLikeData();

	if (!checkGrammarModifiers(node) && classLikeData.heritageClauses != nullptr &&
	    *classLikeData.heritageClauses != nullptr) {
		for (Node* heritageClauseNode : (*classLikeData.heritageClauses)->nodes) {
			HeritageClause* heritageClause = heritageClauseNode->as<HeritageClause>();
			if (heritageClause->Token == Kind::ExtendsKeyword) {
				if (seenExtendsClause) {
					return grammarErrorOnFirstToken(heritageClauseNode, X_extends_clause_already_seen);
				}

				if (seenImplementsClause) {
					return grammarErrorOnFirstToken(heritageClauseNode, X_extends_clause_must_precede_implements_clause);
				}

				const std::vector<Node*>& typeNodes = heritageClause->Types->nodes;
				if (typeNodes.size() > 1) {
					return grammarErrorOnFirstToken(typeNodes[1], Classes_can_only_extend_a_single_class);
				}

				seenExtendsClause = true;
			} else {
				if (heritageClause->Token != Kind::ImplementsKeyword) {
					TSC_UNREACHABLE("Unexpected heritage clause token");
				}
				if (seenImplementsClause) {
					return grammarErrorOnFirstToken(heritageClauseNode, X_implements_clause_already_seen);
				}

				seenImplementsClause = true;
			}

			// Grammar checking heritageClause inside class declaration
			checkGrammarHeritageClause(heritageClause);
		}
	}

	return false;
}

bool Checker::checkGrammarInterfaceDeclaration(InterfaceDeclaration* node) {
	if (node->HeritageClauses != nullptr) {
		bool seenExtendsClause = false;
		for (Node* heritageClauseNode : node->HeritageClauses->nodes) {
			HeritageClause* heritageClause = heritageClauseNode->as<HeritageClause>();

			switch (heritageClause->Token) {
			case Kind::ExtendsKeyword:
				if (seenExtendsClause) {
					return grammarErrorOnFirstToken(heritageClauseNode, X_extends_clause_already_seen);
				}
				seenExtendsClause = true;
				break;
			case Kind::ImplementsKeyword:
				return grammarErrorOnFirstToken(heritageClauseNode, Interface_declaration_cannot_have_implements_clause);
			default:
				TSC_UNREACHABLE("Unexpected heritage clause token");
			}

			// Grammar checking heritageClause inside class declaration
			checkGrammarHeritageClause(heritageClause);
		}
	}

	return false;
}

bool Checker::checkGrammarComputedPropertyName(Node* node) {
	// If node is not a computedPropertyName, just skip the grammar checking
	if (node->kind != Kind::ComputedPropertyName) {
		return false;
	}

	ComputedPropertyName* computedPropertyName = node->as<ComputedPropertyName>();
	if (computedPropertyName->Expression->kind == Kind::BinaryExpression && computedPropertyName->Expression->as<BinaryExpression>()->OperatorToken->kind == Kind::CommaToken) {
		return grammarErrorOnNode(computedPropertyName->Expression, A_comma_expression_is_not_allowed_in_a_computed_property_name);
	}
	return false;
}

bool Checker::checkGrammarForGenerator(Node* node) {
	auto bodyData = node->bodyData();
	if (bodyData.asteriskToken != nullptr && *bodyData.asteriskToken != nullptr) {
		if (node->kind != Kind::FunctionDeclaration && node->kind != Kind::FunctionExpression && node->kind != Kind::MethodDeclaration) {
			TSC_UNREACHABLE("Unexpected node kind in checkGrammarForGenerator");
		}
		if ((node->flags & NodeFlagsAmbient) != 0) {
			return grammarErrorOnNode(*bodyData.asteriskToken, Generators_are_not_allowed_in_an_ambient_context);
		}
		if (*bodyData.body == nullptr) {
			return grammarErrorOnNode(*bodyData.asteriskToken, An_overload_signature_cannot_be_declared_as_a_generator);
		}
	}

	return false;
}

bool Checker::checkGrammarForInvalidQuestionMark(Node* postfixToken, const DiagnosticMessage* message) {
	return postfixToken != nullptr && postfixToken->kind == Kind::QuestionToken && grammarErrorOnNode(postfixToken, message);
}

bool Checker::checkGrammarForInvalidExclamationToken(Node* postfixToken, const DiagnosticMessage* message) {
	return postfixToken != nullptr && postfixToken->kind == Kind::ExclamationToken && grammarErrorOnNode(postfixToken, message);
}

bool Checker::checkGrammarObjectLiteralExpression(ObjectLiteralExpression* node, bool inDestructuring) {
	std::unordered_map<std::string, DeclarationMeaning> seen;

	std::vector<Node*> properties;
	if (node->Properties != nullptr) {
		properties = node->Properties->nodes;
	}
	for (Node* prop : properties) {
		if (prop->kind == Kind::SpreadAssignment) {
			SpreadAssignment* spreadAssignment = prop->as<SpreadAssignment>();
			if (inDestructuring) {
				// a rest property cannot be destructured any further
				Node* expression = skipParentheses(spreadAssignment->Expression);
				if (isArrayLiteralExpression(expression) || isObjectLiteralExpression(expression)) {
					return grammarErrorOnNode(spreadAssignment->Expression, A_rest_element_cannot_contain_a_binding_pattern);
				}
			}
			continue;
		}
		Node* name = prop->name();
		if (name->kind == Kind::ComputedPropertyName) {
			// If the name is not a ComputedPropertyName, the grammar checking will skip it
			checkGrammarComputedPropertyName(name);
		}

		if (prop->kind == Kind::ShorthandPropertyAssignment && !inDestructuring) {
			ShorthandPropertyAssignment* shorthandProp = prop->as<ShorthandPropertyAssignment>();
			if (shorthandProp->ObjectAssignmentInitializer != nullptr) {
				// having objectAssignmentInitializer is only valid in an ObjectAssignmentPattern.
				// Outside of destructuring, it is a syntax error.

				// Try to grab the last node prior to the initializer,
				// then error on the first token following (which should be the `=` token).
				Node* lastNodeBeforeInitializer = nullptr;
				shorthandProp->forEachChild([&](Node* child) -> bool {
					if (child != shorthandProp->ObjectAssignmentInitializer) {
						lastNodeBeforeInitializer = child;
						return false;
					}
					return true;
				});

				grammarErrorOnFirstToken(lastNodeBeforeInitializer, Did_you_mean_to_use_a_Colon_An_can_only_follow_a_property_name_when_the_containing_object_literal_is_part_of_a_destructuring_pattern);
			}
		}

		if (name->kind == Kind::PrivateIdentifier) {
			grammarErrorOnNode(name, Private_identifiers_are_not_allowed_outside_class_bodies);
		}

		// Modifiers are never allowed on properties except for 'async' on a method declaration
		if (std::vector<Node*> modifiers = prop->modifierNodes(); !modifiers.empty()) {
			if (canHaveModifiers(prop)) {
				for (Node* mod : modifiers) {
					if (isModifier(mod) && (mod->kind != Kind::AsyncKeyword || prop->kind != Kind::MethodDeclaration)) {
						grammarErrorOnNode(mod, X_0_modifier_cannot_be_used_here, {getTextOfNode(mod)});
					}
				}
			} else if (canHaveIllegalModifiers(prop)) {
				for (Node* mod : modifiers) {
					if (isModifier(mod)) {
						grammarErrorOnNode(mod, X_0_modifier_cannot_be_used_here, {getTextOfNode(mod)});
					}
				}
			}
		}

		// ECMA-262 11.1.5 Object Initializer
		// If previous is not undefined then throw a SyntaxError exception if any of the following conditions are true
		// a.This production is contained in strict code and IsDataDescriptor(previous) is true and
		// IsDataDescriptor(propId.descriptor) is true.
		//    b.IsDataDescriptor(previous) is true and IsAccessorDescriptor(propId.descriptor) is true.
		//    c.IsAccessorDescriptor(previous) is true and IsDataDescriptor(propId.descriptor) is true.
		//    d.IsAccessorDescriptor(previous) is true and IsAccessorDescriptor(propId.descriptor) is true
		// and either both previous and propId.descriptor have[[Get]] fields or both previous and propId.descriptor have[[Set]] fields
		DeclarationMeaning currentKind;
		switch (prop->kind) {
		case Kind::ShorthandPropertyAssignment:
		case Kind::PropertyAssignment:
			{
				// Grammar checking for computedPropertyName and shorthandPropertyAssignment
				Node* postfixToken;
				if (prop->kind == Kind::ShorthandPropertyAssignment) {
					postfixToken = prop->as<ShorthandPropertyAssignment>()->PostfixToken;
				} else {
					postfixToken = prop->as<PropertyAssignment>()->PostfixToken;
				}

				checkGrammarForInvalidExclamationToken(postfixToken, A_definite_assignment_assertion_is_not_permitted_in_this_context);
				checkGrammarForInvalidQuestionMark(postfixToken, An_object_member_cannot_be_declared_optional);

				if (name->kind == Kind::NumericLiteral) {
					checkGrammarNumericLiteral(name->as<NumericLiteral>());
				}

				if (name->kind == Kind::BigIntLiteral) {
					addErrorOrSuggestion(true, createDiagnosticForNode(name, A_bigint_literal_cannot_be_used_as_a_property_name));
				}

				currentKind = DeclarationMeaningPropertyAssignment;
			}
			break;
		case Kind::MethodDeclaration:
			currentKind = DeclarationMeaningMethod;
			break;
		case Kind::GetAccessor:
			currentKind = DeclarationMeaningGetAccessor;
			break;
		case Kind::SetAccessor:
			currentKind = DeclarationMeaningSetAccessor;
			break;
		default:
			TSC_UNREACHABLE("Unexpected node kind in checkGrammarObjectLiteralExpression");
		}

		if (!inDestructuring) {
			auto [effectiveName, ok] = getEffectivePropertyNameForPropertyNameNode(name);
			if (!ok) {
				continue;
			}

			DeclarationMeaning existingKind = seen[effectiveName];
			if (existingKind == 0) {
				seen[effectiveName] = currentKind;
			} else {
				if ((currentKind & DeclarationMeaningMethod) != 0 && (existingKind & DeclarationMeaningMethod) != 0) {
					grammarErrorOnNode(name, Duplicate_identifier_0, {getTextOfNode(name)});
				} else if ((currentKind & DeclarationMeaningPropertyAssignment) != 0 && (existingKind & DeclarationMeaningPropertyAssignment) != 0) {
					grammarErrorOnNode(name, An_object_literal_cannot_have_multiple_properties_with_the_same_name, {getTextOfNode(name)});
				} else if ((currentKind & DeclarationMeaningGetOrSetAccessor) != 0 && (existingKind & DeclarationMeaningGetOrSetAccessor) != 0) {
					if (existingKind != DeclarationMeaningGetOrSetAccessor && currentKind != existingKind) {
						seen[effectiveName] = currentKind | existingKind;
					} else {
						return grammarErrorOnNode(name, An_object_literal_cannot_have_multiple_get_Slashset_accessors_with_the_same_name);
					}
				} else {
					return grammarErrorOnNode(name, An_object_literal_cannot_have_property_and_accessor_with_the_same_name);
				}
			}
		}
	}

	return false;
}

// checker.go: getEffectivePropertyNameForPropertyNameNode
std::pair<std::string, bool> Checker::getEffectivePropertyNameForPropertyNameNode(Node* node) {
	std::string name = getPropertyNameForPropertyNameNode(node);
	if (name != InternalSymbolNameMissing) {
		return {name, true};
	}
	if (isComputedPropertyName(node)) {
		// This is cached so `getTypeOfExpression` isn't constantly reinvoked for every property name lookup
		ComputedNameNodeLinks* links = computedNameLinks.Get(node);
		if (links != nullptr && links->hasName != nullptr) {
			return {links->name, *links->hasName};
		}
		auto [typeName, exists] = tryGetNameFromType(getTypeOfExpression(node->expression()));
		links->name = typeName;
		links->hasName = linksArena.alloc<bool>(exists);
		return {typeName, exists};
	}
	return {"", false};
}

// checker.go: tryGetNameFromType — defined in checker_flow.cpp (flow slice).


// checker.go: getTypeOfExpression — full expression checking.
// (getTypeOfExpression dep-stub removed — walk slice defines it in checker_walk.cpp.)

bool Checker::checkGrammarJsxElement(Node* node) {
	checkGrammarJsxName(node->tagName());
	checkGrammarTypeArguments(node, node->typeArgumentList());
	std::unordered_set<std::string> seen;
	for (Node* attrNode : node->attributes()->properties()) {
		if (attrNode->kind == Kind::JsxSpreadAttribute) {
			continue;
		}
		JsxAttribute* attr = attrNode->as<JsxAttribute>();
		Node* name = attr->name;
		Node* initializer = attr->Initializer;
		std::string textOfName = name->text();
		if (seen.find(textOfName) == seen.end()) {
			seen.insert(textOfName);
		} else {
			return grammarErrorOnNode(name, JSX_elements_cannot_have_multiple_attributes_with_the_same_name);
		}
		if (initializer != nullptr && initializer->kind == Kind::JsxExpression && initializer->expression() == nullptr) {
			return grammarErrorOnNode(initializer, JSX_attributes_must_only_be_assigned_a_non_empty_expression);
		}
	}
	return false;
}

bool Checker::checkGrammarJsxName(Node* node /*JsxTagNameExpression*/) {
	if (isPropertyAccessExpression(node) && isJsxNamespacedName(node->expression())) {
		return grammarErrorOnNode(node->expression(), JSX_property_access_expressions_cannot_include_JSX_namespace_names);
	}

	if (isJsxNamespacedName(node) && compilerOptions->GetJSXTransformEnabled() && !isIntrinsicJsxName(node->as<JsxNamespacedName>()->Namespace->text())) {
		return grammarErrorOnNode(node, React_components_cannot_include_JSX_namespace_names);
	}

	return false;
}

bool Checker::checkGrammarJsxExpression(JsxExpression* node) {
	if (node->Expression != nullptr && isCommaSequence(node->Expression)) {
		return grammarErrorOnNode(node->Expression, JSX_expressions_may_not_use_the_comma_operator_Did_you_mean_to_write_an_array);
	}

	return false;
}

bool Checker::checkGrammarForInOrForOfStatement(ForInOrOfStatement* forInOrOfStatement) {
	Node* asNode = forInOrOfStatement->asNode();
	if (checkGrammarStatementInAmbientContext(asNode)) {
		return true;
	}

	if (forInOrOfStatement->kind == Kind::ForOfStatement && forInOrOfStatement->AwaitModifier != nullptr) {
		if ((forInOrOfStatement->flags & NodeFlagsAwaitContext) == 0) {
			SourceFile* sourceFile = getSourceFileOfNode(asNode);
			if (isInTopLevelContext(asNode)) {
				if (!hasParseDiagnostics(sourceFile)) {
					if (!isEffectiveExternalModule(sourceFile, compilerOptions)) {
						addDiagnostic(createDiagnosticForNode(forInOrOfStatement->AwaitModifier, X_for_await_loops_are_only_allowed_at_the_top_level_of_a_file_when_that_file_is_a_module_but_this_file_has_no_imports_or_exports_Consider_adding_an_empty_export_to_make_this_file_a_module));
					}
					switch (moduleKind) {
					case ModuleKind::Node16:
					case ModuleKind::Node18:
					case ModuleKind::Node20:
					case ModuleKind::NodeNext:
						// Go calls program.GetSourceFileMetaData(sourceFile.Path()).ImpliedNodeFormat;
						// the C++ Program surface exposes the same value via GetImpliedNodeFormatForEmit.
						if (program->GetImpliedNodeFormatForEmit(sourceFile) == ModuleKind::CommonJS) {
							addDiagnostic(createDiagnosticForNode(forInOrOfStatement->AwaitModifier, The_current_file_is_a_CommonJS_module_and_cannot_use_await_at_the_top_level));
							break;
						}
						[[fallthrough]];
					case ModuleKind::ES2022:
					case ModuleKind::ESNext:
					case ModuleKind::Preserve:
					case ModuleKind::System:
						if (languageVersion >= ScriptTarget::ES2017) {
							break;
						}
						[[fallthrough]];
					default:
						addDiagnostic(createDiagnosticForNode(forInOrOfStatement->AwaitModifier, Top_level_for_await_loops_are_only_allowed_when_the_module_option_is_set_to_es2022_esnext_system_node16_node18_node20_nodenext_or_preserve_and_the_target_option_is_set_to_es2017_or_higher));
					}
				}
			} else {
				// use of 'for-await-of' in non-async function
				if (!hasParseDiagnostics(sourceFile)) {
					Diagnostic* diagnostic = createDiagnosticForNode(forInOrOfStatement->AwaitModifier, X_for_await_loops_are_only_allowed_within_async_functions_and_at_the_top_levels_of_modules);
					Node* containingFunc = getContainingFunction(forInOrOfStatement->asNode());
					if (containingFunc != nullptr && containingFunc->kind != Kind::Constructor) {
						TSC_ASSERT((getFunctionFlags(containingFunc) & FunctionFlagsAsync) == 0, "Enclosing function should never be an async function.");
						Diagnostic* relatedInfo = createDiagnosticForNode(containingFunc, Did_you_mean_to_mark_this_function_as_async);
						diagnostic->AddRelatedInfo(relatedInfo);
					}
					addDiagnostic(diagnostic);
					return true;
				}
			}
		}
	}

	if (isForOfStatement(asNode) && (forInOrOfStatement->flags & NodeFlagsAwaitContext) == 0 && isIdentifier(forInOrOfStatement->Initializer) && forInOrOfStatement->Initializer->text() == "async") {
		grammarErrorOnNode(forInOrOfStatement->Initializer, The_left_hand_side_of_a_for_of_statement_may_not_be_async);
		return false;
	}

	if (forInOrOfStatement->Initializer->kind == Kind::VariableDeclarationList) {
		VariableDeclarationList* variableList = forInOrOfStatement->Initializer->as<VariableDeclarationList>();
		if (!checkGrammarVariableDeclarationList(variableList)) {
			NodeList* declarations = variableList->Declarations;

			// declarations.length can be zero if there is an error in variable declaration in for-of or for-in
			// See http://www.ecma-international.org/ecma-262/6.0/#sec-for-in-and-for-of-statements for details
			// For example:
			//      var let = 10;
			//      for (let of [1,2,3]) {} // this is invalid ES6 syntax
			//      for (let in [1,2,3]) {} // this is invalid ES6 syntax
			// We will then want to skip on grammar checking on variableList declaration
			if (declarations->nodes.empty()) {
				return false;
			}

			if (declarations->nodes.size() > 1) {
				const DiagnosticMessage* diagnostic;
				if (forInOrOfStatement->kind == Kind::ForInStatement) {
					diagnostic = Only_a_single_variable_declaration_is_allowed_in_a_for_in_statement;
				} else {
					diagnostic = Only_a_single_variable_declaration_is_allowed_in_a_for_of_statement;
				}
				return grammarErrorOnFirstToken(declarations->nodes[1], diagnostic);
			}

			VariableDeclaration* firstVariableDeclaration = declarations->nodes[0]->as<VariableDeclaration>();
			if (firstVariableDeclaration->Initializer != nullptr) {
				const DiagnosticMessage* diagnostic;
				if (forInOrOfStatement->kind == Kind::ForInStatement) {
					diagnostic = The_variable_declaration_of_a_for_in_statement_cannot_have_an_initializer;
				} else {
					diagnostic = The_variable_declaration_of_a_for_of_statement_cannot_have_an_initializer;
				}
				return grammarErrorOnNode(firstVariableDeclaration->name, diagnostic);
			}
			if (firstVariableDeclaration->Type != nullptr) {
				const DiagnosticMessage* diagnostic;
				if (forInOrOfStatement->kind == Kind::ForInStatement) {
					diagnostic = The_left_hand_side_of_a_for_in_statement_cannot_use_a_type_annotation;
				} else {
					diagnostic = The_left_hand_side_of_a_for_of_statement_cannot_use_a_type_annotation;
				}
				return grammarErrorOnNode(firstVariableDeclaration->asNode(), diagnostic);
			}
		}
	}

	return false;
}

bool Checker::checkGrammarAccessor(Node* accessor) {
	Node* body = accessor->body();
	if ((accessor->flags & NodeFlagsAmbient) == 0 && (accessor->parent->kind != Kind::TypeLiteral) && (accessor->parent->kind != Kind::InterfaceDeclaration)) {
		if (body == nullptr && !hasSyntacticModifier(accessor, ModifierFlagsAbstract)) {
			return grammarErrorAtPos(accessor, accessor->end() - 1, 1 /*len(";")*/, X_0_expected, {"{"});
		}
	}
	if (body != nullptr) {
		if (hasSyntacticModifier(accessor, ModifierFlagsAbstract)) {
			return grammarErrorOnNode(accessor, An_abstract_accessor_cannot_have_an_implementation);
		}
		if (accessor->parent->kind == Kind::TypeLiteral || accessor->parent->kind == Kind::InterfaceDeclaration) {
			return grammarErrorOnNode(body, An_implementation_cannot_be_declared_in_ambient_contexts);
		}
	}

	auto funcData = accessor->functionLikeData();
	NodeList* typeParameters = nullptr;
	if (funcData.typeParameters != nullptr) {
		typeParameters = *funcData.typeParameters;
	}

	if (typeParameters != nullptr) {
		return grammarErrorOnNode(accessor->name(), An_accessor_cannot_have_type_parameters);
	}
	if (!doesAccessorHaveCorrectParameterCount(accessor)) {
		return grammarErrorOnNode(accessor->name(), accessor->kind == Kind::GetAccessor ? A_get_accessor_cannot_have_parameters : A_set_accessor_must_have_exactly_one_parameter);
	}
	if (accessor->kind == Kind::SetAccessor) {
		if (*funcData.type != nullptr) {
			return grammarErrorOnNode(accessor->name(), A_set_accessor_cannot_have_a_return_type_annotation);
		}

		Node* parameterNode = GetSetAccessorValueParameter(accessor);
		if (parameterNode == nullptr) {
			TSC_UNREACHABLE("Return value does not match parameter count assertion.");
		}
		ParameterDeclaration* parameter = parameterNode->as<ParameterDeclaration>();
		if (parameter->DotDotDotToken != nullptr) {
			return grammarErrorOnNode(parameter->DotDotDotToken, A_set_accessor_cannot_have_rest_parameter);
		}
		if (parameter->QuestionToken != nullptr) {
			return grammarErrorOnNode(parameter->QuestionToken, A_set_accessor_cannot_have_an_optional_parameter);
		}
		if (parameter->Initializer != nullptr) {
			return grammarErrorOnNode(accessor->name(), A_set_accessor_parameter_cannot_have_an_initializer);
		}
	}

	return false;
}

// Does the accessor have the right number of parameters?
//
//	A `get` accessor has no parameters or a single `this` parameter.
//	A `set` accessor has one parameter or a `this` parameter and one more parameter.
bool Checker::doesAccessorHaveCorrectParameterCount(Node* accessor) {
	// `getAccessorThisParameter` returns `nullptr` if the accessor's arity is incorrect,
	// even if there is a `this` parameter declared.
	return getAccessorThisParameter(accessor) != nullptr ||
		   static_cast<int>(accessor->parameters().size()) == (accessor->kind == Kind::GetAccessor ? 0 : 1);
}

// checker.go: getAccessorThisParameter
Node* Checker::getAccessorThisParameter(Node* accessor) {
	if (static_cast<int>(accessor->parameters().size()) == (accessor->kind == Kind::GetAccessor ? 1 : 2)) {
		return getThisParameter(accessor);
	}
	return nullptr;
}

bool Checker::checkGrammarTypeOperatorNode(TypeOperatorNode* node) {
	if (node->Operator == Kind::UniqueKeyword) {
		Node* innerType = node->Type;
		if (innerType->kind != Kind::SymbolKeyword) {
			return grammarErrorOnNode(innerType, X_0_expected, {std::string(tokenToString(Kind::SymbolKeyword))});
		}
		Node* parent = walkUpParenthesizedTypes(node->parent);
		switch (parent->kind) {
		case Kind::VariableDeclaration:
			{
				VariableDeclaration* decl = parent->as<VariableDeclaration>();
				if (decl->name->kind != Kind::Identifier) {
					return grammarErrorOnNode(node->asNode(), X_unique_symbol_types_may_not_be_used_on_a_variable_declaration_with_a_binding_name);
				}
				if (!isVariableDeclarationInVariableStatement(decl->asNode())) {
					return grammarErrorOnNode(node->asNode(), X_unique_symbol_types_are_only_allowed_on_variables_in_a_variable_statement);
				}
				if ((decl->parent->flags & NodeFlagsConst) == 0) {
					return grammarErrorOnNode(parent->as<VariableDeclaration>()->name, A_variable_whose_type_is_a_unique_symbol_type_must_be_const);
				}
			}
			break;
		case Kind::PropertyDeclaration:
			if (!isStatic(parent) || !hasReadonlyModifier(parent)) {
				return grammarErrorOnNode(parent->as<PropertyDeclaration>()->name, A_property_of_a_class_whose_type_is_a_unique_symbol_type_must_be_both_static_and_readonly);
			}
			break;
		case Kind::PropertySignature:
			if (!hasSyntacticModifier(parent, ModifierFlagsReadonly)) {
				return grammarErrorOnNode(parent->as<PropertySignatureDeclaration>()->name, A_property_of_an_interface_or_type_literal_whose_type_is_a_unique_symbol_type_must_be_readonly);
			}
			break;
		default:
			return grammarErrorOnNode(node->asNode(), X_unique_symbol_types_are_not_allowed_here);
		}
	} else if (node->Operator == Kind::ReadonlyKeyword) {
		Node* innerType = node->Type;
		if (innerType->kind != Kind::ArrayType && innerType->kind != Kind::TupleType) {
			return grammarErrorOnFirstToken(node->asNode(), X_readonly_type_modifier_is_only_permitted_on_array_and_tuple_literal_types, {std::string(tokenToString(Kind::SymbolKeyword))});
		}
	}

	return false;
}

bool Checker::checkGrammarForInvalidDynamicName(Node* node /*DeclarationName*/, const DiagnosticMessage* message) {
	if (!isNonBindableDynamicName(node)) {
		return false;
	}
	Node* expression;
	if (isElementAccessExpression(node)) {
		expression = skipParentheses(node->as<ElementAccessExpression>()->ArgumentExpression);
	} else {
		expression = node->expression();
	}

	if (!isEntityNameExpression(expression)) {
		return grammarErrorOnNode(node, message);
	}

	return false;
}

// Indicates whether a declaration name is a dynamic name that cannot be late-bound.
bool Checker::isNonBindableDynamicName(Node* node /*DeclarationName*/) {
	return isDynamicName(node) && !isLateBindableName(node);
}

bool Checker::checkGrammarMethod(Node* node /*Union[MethodDeclaration, MethodSignature]*/) {
	if (checkGrammarFunctionLikeDeclaration(node)) {
		return true;
	}

	if (node->kind == Kind::MethodDeclaration) {
		if (node->parent->kind == Kind::ObjectLiteralExpression) {
			// We only disallow modifier on a method declaration if it is a property of object-literal-expression
			if (ModifierList* modifiers = node->modifiers(); modifiers != nullptr && !(modifiers->nodes.size() == 1 && modifiers->nodes[0]->kind == Kind::AsyncKeyword)) {
				return grammarErrorOnFirstToken(node, Modifiers_cannot_appear_here);
			}

			MethodDeclaration* methodDecl = node->as<MethodDeclaration>();
			if (checkGrammarForInvalidQuestionMark(methodDecl->PostfixToken, An_object_member_cannot_be_declared_optional)) {
				return true;
			}
			if (checkGrammarForInvalidExclamationToken(methodDecl->PostfixToken, A_definite_assignment_assertion_is_not_permitted_in_this_context)) {
				return true;
			}
			if (node->body() == nullptr) {
				return grammarErrorAtPos(node, node->end() - 1, 1 /*len(";")*/, X_0_expected, {"{"});
			}
		}
		if (checkGrammarForGenerator(node)) {
			return true;
		}
	}

	if (isClassLike(node->parent)) {
		// Technically, computed properties in ambient contexts is disallowed
		// for property declarations and accessors too, not just methods.
		// However, property declarations disallow computed names in general,
		// and accessors are not allowed in ambient contexts in general,
		// so this error only really matters for methods.
		if ((node->flags & NodeFlagsAmbient) != 0) {
			return checkGrammarForInvalidDynamicName(node->name(), A_computed_property_name_in_an_ambient_context_must_refer_to_an_expression_whose_type_is_a_literal_type_or_a_unique_symbol_type);
		} else if (node->kind == Kind::MethodDeclaration && node->body() == nullptr) {
			return checkGrammarForInvalidDynamicName(node->name(), A_computed_property_name_in_a_method_overload_must_refer_to_an_expression_whose_type_is_a_literal_type_or_a_unique_symbol_type);
		}
	} else if (node->parent->kind == Kind::InterfaceDeclaration) {
		return checkGrammarForInvalidDynamicName(node->name(), A_computed_property_name_in_an_interface_must_refer_to_an_expression_whose_type_is_a_literal_type_or_a_unique_symbol_type);
	} else if (node->parent->kind == Kind::TypeLiteral) {
		return checkGrammarForInvalidDynamicName(node->name(), A_computed_property_name_in_a_type_literal_must_refer_to_an_expression_whose_type_is_a_literal_type_or_a_unique_symbol_type);
	}

	return false;
}

bool Checker::checkGrammarBreakOrContinueStatement(Node* node) {
	Node* targetLabel = node->label();
	Node* current = node;
	while (current != nullptr) {
		if (isFunctionLikeOrClassStaticBlockDeclaration(current)) {
			return grammarErrorOnNode(node, Jump_target_cannot_cross_function_boundary);
		}

		switch (current->kind) {
		case Kind::LabeledStatement:
			if (targetLabel != nullptr && current->label()->text() == targetLabel->text()) {
				// found matching label - verify that label usage is correct
				// continue can only target labels that are on iteration statements
				bool isMisplacedContinueLabel = node->kind == Kind::ContinueStatement &&
												!isIterationStatement(current->statement(), true /*lookInLabeledStatements*/);

				if (isMisplacedContinueLabel) {
					return grammarErrorOnNode(node, A_continue_statement_can_only_jump_to_a_label_of_an_enclosing_iteration_statement);
				}

				return false;
			}
			break;
		case Kind::SwitchStatement:
			if (node->kind == Kind::BreakStatement && targetLabel == nullptr) {
				// unlabeled break within switch statement - ok
				return false;
			}
			break;
		default:
			if (isIterationStatement(current, false /*lookInLabeledStatements*/) && targetLabel == nullptr) {
				// unlabeled break or continue within iteration statement - ok
				return false;
			}
			break;
		}

		current = current->parent;
	}

	if (targetLabel != nullptr) {
		const DiagnosticMessage* message;
		if (node->kind == Kind::BreakStatement) {
			message = A_break_statement_can_only_jump_to_a_label_of_an_enclosing_statement;
		} else {
			message = A_continue_statement_can_only_jump_to_a_label_of_an_enclosing_iteration_statement;
		}

		return grammarErrorOnNode(node, message);
	} else {
		const DiagnosticMessage* message;
		if (node->kind == Kind::BreakStatement) {
			message = A_break_statement_can_only_be_used_within_an_enclosing_iteration_or_switch_statement;
		} else {
			message = A_continue_statement_can_only_be_used_within_an_enclosing_iteration_statement;
		}
		return grammarErrorOnNode(node, message);
	}
}

bool Checker::checkGrammarBindingElement(BindingElement* node) {
	if (node->DotDotDotToken != nullptr) {
		NodeList* elements = node->parent->elementList();
		if (node->asNode() != lastOrNil(elements->nodes)) {
			return grammarErrorOnNode(node->asNode(), A_rest_element_must_be_last_in_a_destructuring_pattern);
		}
		checkGrammarForDisallowedTrailingComma(elements, A_rest_parameter_or_binding_pattern_may_not_have_a_trailing_comma);

		if (node->PropertyName != nullptr) {
			return grammarErrorOnNode(node->name, A_rest_element_cannot_have_a_property_name);
		}
	}

	if (node->DotDotDotToken != nullptr && node->Initializer != nullptr) {
		// Error on equals token which immediately precedes the initializer
		return grammarErrorAtPos(node->asNode(), node->Initializer->pos() - 1, 1, A_rest_element_cannot_have_an_initializer);
	}

	return false;
}

bool Checker::checkGrammarVariableDeclaration(VariableDeclaration* node) {
	NodeFlags nodeFlags = getCombinedNodeFlagsCached(node->asNode());
	NodeFlags blockScopeKind = nodeFlags & NodeFlagsBlockScoped;
	if (isBindingPattern(node->name)) {
		switch (blockScopeKind) {
		case NodeFlagsAwaitUsing:
			return grammarErrorOnNode(node->asNode(), X_0_declarations_may_not_have_binding_patterns, {"await using"});
		case NodeFlagsUsing:
			return grammarErrorOnNode(node->asNode(), X_0_declarations_may_not_have_binding_patterns, {"using"});
		default:
			break;
		}
	}

	if (node->parent->parent->kind != Kind::ForInStatement && node->parent->parent->kind != Kind::ForOfStatement) {
		if ((nodeFlags & NodeFlagsAmbient) != 0) {
			checkAmbientInitializer(node->asNode());
		} else if (node->Initializer == nullptr) {
			if (isBindingPattern(node->name) && !isBindingPattern(node->parent)) {
				return grammarErrorOnNode(node->asNode(), A_destructuring_declaration_must_have_an_initializer);
			}
			switch (blockScopeKind) {
			case NodeFlagsAwaitUsing:
				return grammarErrorOnNode(node->asNode(), X_0_declarations_must_be_initialized, {"await using"});
			case NodeFlagsUsing:
				return grammarErrorOnNode(node->asNode(), X_0_declarations_must_be_initialized, {"using"});
			case NodeFlagsConst:
				return grammarErrorOnNode(node->asNode(), X_0_declarations_must_be_initialized, {"const"});
			default:
				break;
			}
		}
	}

	if (node->ExclamationToken != nullptr && (node->parent->parent->kind != Kind::VariableStatement || node->Type == nullptr || node->Initializer != nullptr || (nodeFlags & NodeFlagsAmbient) != 0)) {
		const DiagnosticMessage* message;
		if (node->Initializer != nullptr) {
			message = Declarations_with_initializers_cannot_also_have_definite_assignment_assertions;
		} else if (node->Type == nullptr) {
			message = Declarations_with_definite_assignment_assertions_must_also_have_type_annotations;
		} else {
			message = A_definite_assignment_assertion_is_not_permitted_in_this_context;
		}
		return grammarErrorOnNode(node->ExclamationToken, message);
	}

	if (program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node->asNode())) < ModuleKind::System && (node->parent->parent->flags & NodeFlagsAmbient) == 0 && hasSyntacticModifier(node->parent->parent, ModifierFlagsExport)) {
		checkGrammarForEsModuleMarkerInBindingName(node->name);
	}

	// 1. LexicalDeclaration : LetOrConst BindingList ;
	// It is a Syntax Error if the BoundNames of BindingList contains "let".
	// 2. ForDeclaration: ForDeclaration : LetOrConst ForBinding
	// It is a Syntax Error if the BoundNames of ForDeclaration contains "let".

	// It is a SyntaxError if a VariableDeclaration or VariableDeclarationNoIn occurs within strict code
	// and its Identifier is eval or arguments
	return blockScopeKind != 0 && checkGrammarNameInLetOrConstDeclarations(node->name);
}

bool Checker::checkGrammarForEsModuleMarkerInBindingName(Node* name) {
	if (isIdentifier(name)) {
		if (name->text() == "__esModule") {
			return grammarErrorOnNodeSkippedOnNoEmit(name, Identifier_expected_esModule_is_reserved_as_an_exported_marker_when_transforming_ECMAScript_modules);
		}
	} else {
		for (Node* element : name->elements()) {
			if (element->name() != nullptr) {
				return checkGrammarForEsModuleMarkerInBindingName(element->name());
			}
		}
	}
	return false;
}

bool Checker::checkGrammarNameInLetOrConstDeclarations(Node* name /*Union[Identifier, BindingPattern]*/) {
	if (name->kind == Kind::Identifier) {
		if (name->text() == "let") {
			return grammarErrorOnNode(name, X_let_is_not_allowed_to_be_used_as_a_name_in_let_or_const_declarations);
		}
	} else {
		for (Node* element : name->elements()) {
			BindingElement* bindingElement = element->as<BindingElement>();
			if (bindingElement->name != nullptr) {
				checkGrammarNameInLetOrConstDeclarations(bindingElement->name);
			}
		}
	}
	return false;
}

bool Checker::checkGrammarVariableDeclarationList(VariableDeclarationList* declarationList) {
	NodeList* declarations = declarationList->Declarations;
	if (checkGrammarForDisallowedTrailingComma(declarations, Trailing_comma_not_allowed)) {
		return true;
	}

	if (declarations->nodes.empty()) {
		return grammarErrorAtPos(declarationList->asNode(), declarations->pos(), declarations->end() - declarations->pos(), Variable_declaration_list_cannot_be_empty);
	}

	NodeFlags blockScopeFlags = declarationList->flags & NodeFlagsBlockScoped;
	if (blockScopeFlags == NodeFlagsUsing || blockScopeFlags == NodeFlagsAwaitUsing) {
		if (isForInStatement(declarationList->parent)) {
			return grammarErrorOnNode(declarationList->asNode(), blockScopeFlags == NodeFlagsUsing ? The_left_hand_side_of_a_for_in_statement_cannot_be_a_using_declaration : The_left_hand_side_of_a_for_in_statement_cannot_be_an_await_using_declaration);
		}
		if ((declarationList->flags & NodeFlagsAmbient) != 0) {
			return grammarErrorOnNode(declarationList->asNode(), blockScopeFlags == NodeFlagsUsing ? X_using_declarations_are_not_allowed_in_ambient_contexts : X_await_using_declarations_are_not_allowed_in_ambient_contexts);
		}
		if (isVariableStatement(declarationList->parent) && (isCaseClause(declarationList->parent->parent) || isDefaultClause(declarationList->parent->parent))) {
			return grammarErrorOnNode(declarationList->asNode(), blockScopeFlags == NodeFlagsUsing ? X_using_declarations_are_not_allowed_in_case_or_default_clauses_unless_contained_within_a_block : X_await_using_declarations_are_not_allowed_in_case_or_default_clauses_unless_contained_within_a_block);
		}
	}

	if (blockScopeFlags == NodeFlagsAwaitUsing) {
		return checkGrammarAwaitOrAwaitUsing(declarationList->asNode());
	}

	return false;
}

bool Checker::checkGrammarAwaitOrAwaitUsing(Node* node) {
	// Grammar checking
	bool hasError = false;
	Node* container = getContainingFunctionOrClassStaticBlock(node);
	if (container != nullptr && isClassStaticBlockDeclaration(container)) {
		// NOTE: We report this regardless as to whether there are parse diagnostics.
		const DiagnosticMessage* message;
		if (isAwaitExpression(node)) {
			message = X_await_expression_cannot_be_used_inside_a_class_static_block;
		} else {
			message = X_await_using_statements_cannot_be_used_inside_a_class_static_block;
		}
		error(node, message);
		hasError = true;
	} else if ((node->flags & NodeFlagsAwaitContext) == 0) {
		if (isInTopLevelContext(node)) {
			SourceFile* sourceFile = getSourceFileOfNode(node);
			if (!hasParseDiagnostics(sourceFile)) {
				TextRange span{};
				bool spanCalculated = false;
				if (!isEffectiveExternalModule(sourceFile, compilerOptions)) {
					span = getRangeOfTokenAtPosition(sourceFile, node->pos());
					spanCalculated = true;
					const DiagnosticMessage* message;
					if (isAwaitExpression(node)) {
						message = X_await_expressions_are_only_allowed_at_the_top_level_of_a_file_when_that_file_is_a_module_but_this_file_has_no_imports_or_exports_Consider_adding_an_empty_export_to_make_this_file_a_module;
					} else {
						message = X_await_using_statements_are_only_allowed_at_the_top_level_of_a_file_when_that_file_is_a_module_but_this_file_has_no_imports_or_exports_Consider_adding_an_empty_export_to_make_this_file_a_module;
					}
					Diagnostic* diagnostic = newDiagnostic(sourceFile, span, message);
					addDiagnostic(diagnostic);
					hasError = true;
				}
				switch (moduleKind) {
				case ModuleKind::Node16:
				case ModuleKind::Node18:
				case ModuleKind::Node20:
				case ModuleKind::NodeNext:
					// Go: program.GetSourceFileMetaData(sourceFile.Path()).ImpliedNodeFormat
					if (program->GetImpliedNodeFormatForEmit(sourceFile) == ModuleKind::CommonJS) {
						if (!spanCalculated) {
							span = getRangeOfTokenAtPosition(sourceFile, node->pos());
						}
						addDiagnostic(newDiagnostic(sourceFile, span, The_current_file_is_a_CommonJS_module_and_cannot_use_await_at_the_top_level));
						hasError = true;
						break;
					}
					[[fallthrough]];
				case ModuleKind::ES2022:
				case ModuleKind::ESNext:
				case ModuleKind::Preserve:
				case ModuleKind::System:
					if (languageVersion >= ScriptTarget::ES2017) {
						break;
					}
					[[fallthrough]];
				default:
					if (!spanCalculated) {
						span = getRangeOfTokenAtPosition(sourceFile, node->pos());
					}
					{
						const DiagnosticMessage* message;
						if (isAwaitExpression(node)) {
							message = Top_level_await_expressions_are_only_allowed_when_the_module_option_is_set_to_es2022_esnext_system_node16_node18_node20_nodenext_or_preserve_and_the_target_option_is_set_to_es2017_or_higher;
						} else {
							message = Top_level_await_using_statements_are_only_allowed_when_the_module_option_is_set_to_es2022_esnext_system_node16_node18_node20_nodenext_or_preserve_and_the_target_option_is_set_to_es2017_or_higher;
						}
						addDiagnostic(newDiagnostic(sourceFile, span, message));
						hasError = true;
					}
				}
			}
		} else {
			// use of 'await' in non-async function
			SourceFile* sourceFile = getSourceFileOfNode(node);
			if (!hasParseDiagnostics(sourceFile)) {
				TextRange span = getRangeOfTokenAtPosition(sourceFile, node->pos());
				const DiagnosticMessage* message;
				if (isAwaitExpression(node)) {
					message = X_await_expressions_are_only_allowed_within_async_functions_and_at_the_top_levels_of_modules;
				} else {
					message = X_await_using_statements_are_only_allowed_within_async_functions_and_at_the_top_levels_of_modules;
				}
				Diagnostic* diagnostic = newDiagnostic(sourceFile, span, message);
				if (container != nullptr && container->kind != Kind::Constructor && !hasAsyncModifier(container)) {
					Diagnostic* relatedInfo = NewDiagnosticForNode(container, Did_you_mean_to_mark_this_function_as_async, {});
					diagnostic->AddRelatedInfo(relatedInfo);
				}
				addDiagnostic(diagnostic);
				hasError = true;
			}
		}
	}

	if (isAwaitExpression(node) && isInParameterInitializerBeforeContainingFunction(node)) {
		// NOTE: We report this regardless as to whether there are parse diagnostics.
		error(node, X_await_expressions_cannot_be_used_in_a_parameter_initializer);
		hasError = true;
	}

	return hasError;
}

// checker.go: isInParameterInitializerBeforeContainingFunction
bool Checker::isInParameterInitializerBeforeContainingFunction(Node* node) {
	bool inBindingInitializer = false;
	while (node->parent != nullptr && !isFunctionLike(node->parent)) {
		if (isParameterDeclaration(node->parent)) {
			if (inBindingInitializer || node->parent->initializer() == node) {
				return true;
			}
		}

		if (isBindingElement(node->parent) && node->parent->initializer() == node) {
			inBindingInitializer = true;
		}

		node = node->parent;
	}

	return false;
}

bool Checker::checkGrammarYieldExpression(Node* node) {
	bool hasError = false;
	if ((node->flags & NodeFlagsYieldContext) == 0) {
		grammarErrorOnFirstToken(node, A_yield_expression_is_only_allowed_in_a_generator_body);
		hasError = true;
	}
	if (isInParameterInitializerBeforeContainingFunction(node)) {
		error(node, X_yield_expressions_cannot_be_used_in_a_parameter_initializer);
		hasError = true;
	}
	return hasError;
}

bool Checker::checkGrammarForDisallowedBlockScopedVariableStatement(VariableStatement* node) {
	if (!containerAllowsBlockScopedVariable(node->parent)) {
		NodeFlags blockScopeKind = getCombinedNodeFlagsCached(node->DeclarationList) & NodeFlagsBlockScoped;
		if (blockScopeKind != 0) {
			std::string keyword;
			if (blockScopeKind == NodeFlagsLet) {
				keyword = "let";
			} else if (blockScopeKind == NodeFlagsConst) {
				keyword = "const";
			} else if (blockScopeKind == NodeFlagsUsing) {
				keyword = "using";
			} else if (blockScopeKind == NodeFlagsAwaitUsing) {
				keyword = "await using";
			} else {
				TSC_UNREACHABLE("Unknown BlockScope flag");
			}
			error(node->asNode(), X_0_declarations_can_only_be_declared_inside_a_block, {keyword});
		}
	}

	return false;
}

bool Checker::containerAllowsBlockScopedVariable(Node* parent) {
	switch (parent->kind) {
	case Kind::IfStatement:
	case Kind::DoStatement:
	case Kind::WhileStatement:
	case Kind::WithStatement:
	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		return false;
	case Kind::LabeledStatement:
		return containerAllowsBlockScopedVariable(parent->parent);
	default:
		break;
	}

	return true;
}

bool Checker::checkGrammarMetaProperty(MetaProperty* node) {
	Node* nodeName = node->name;
	std::string nameText = nodeName->text();

	switch (node->KeywordToken) {
	case Kind::NewKeyword:
		if (nameText != "target") {
			return grammarErrorOnNode(nodeName, X_0_is_not_a_valid_meta_property_for_keyword_1_Did_you_mean_2, {nameText, std::string(tokenToString(node->KeywordToken)), "target"});
		}
		break;
	case Kind::ImportKeyword:
		if (nameText != "meta") {
			bool isCallee = isCallExpression(node->parent) && node->parent->expression() == node->asNode();
			if (nameText == "defer") {
				if (!isCallee) {
					return grammarErrorAtPos(node->asNode(), node->asNode()->end(), 0, X_0_expected, {"("});
				}
			} else {
				if (isCallee) {
					return grammarErrorOnNode(nodeName, X_0_is_not_a_valid_meta_property_for_keyword_import_Did_you_mean_meta_or_defer, {nameText});
				}
				return grammarErrorOnNode(nodeName, X_0_is_not_a_valid_meta_property_for_keyword_1_Did_you_mean_2, {nameText, std::string(tokenToString(node->KeywordToken)), "meta"});
			}
		}
		break;
	default:
		break;
	}

	return false;
}

bool Checker::checkGrammarConstructorTypeParameters(ConstructorDeclaration* node) {
	NodeList* range_ = node->TypeParameters;
	if (range_ != nullptr) {
		int pos;
		if (range_->pos() == range_->end()) {
			pos = range_->pos();
		} else {
			pos = skipTrivia(getSourceFileOfNode(node->asNode())->text, range_->pos());
		}
		return grammarErrorAtPos(node->asNode(), pos, range_->end() - pos, Type_parameters_cannot_appear_on_a_constructor_declaration);
	}

	return false;
}

bool Checker::checkGrammarConstructorTypeAnnotation(ConstructorDeclaration* node) {
	Node* t = node->Type;
	if (t != nullptr) {
		return grammarErrorOnNode(t, Type_annotation_cannot_appear_on_a_constructor_declaration);
	}
	return false;
}

bool Checker::checkGrammarProperty(Node* node /*Union[PropertyDeclaration, PropertySignature]*/) {
	Node* propertyName = node->name();
	if (isComputedPropertyName(propertyName) && isBinaryExpression(propertyName->expression()) && propertyName->expression()->as<BinaryExpression>()->OperatorToken->kind == Kind::InKeyword) {
		return grammarErrorOnNode(node->parent->members()[0], A_mapped_type_may_not_declare_properties_or_methods);
	}
	if (isClassLike(node->parent)) {
		if (isStringLiteral(propertyName) && propertyName->text() == "constructor") {
			return grammarErrorOnNode(propertyName, Classes_may_not_have_a_field_named_constructor);
		}
		if (checkGrammarForInvalidDynamicName(propertyName, A_computed_property_name_in_a_class_property_declaration_must_have_a_simple_literal_type_or_a_unique_symbol_type)) {
			return true;
		}
		if (isAutoAccessorPropertyDeclaration(node) && checkGrammarForInvalidQuestionMark(node->postfixToken(), An_accessor_property_cannot_be_declared_optional)) {
			return true;
		}
	} else if (isInterfaceDeclaration(node->parent)) {
		if (checkGrammarForInvalidDynamicName(propertyName, A_computed_property_name_in_an_interface_must_refer_to_an_expression_whose_type_is_a_literal_type_or_a_unique_symbol_type)) {
			return true;
		}
		if (!isPropertySignatureDeclaration(node)) {
			// Interfaces cannot contain property declarations
			TSC_UNREACHABLE("Unexpected node kind in checkGrammarProperty (interface)");
		}
		if (Node* initializer = node->initializer(); initializer != nullptr) {
			return grammarErrorOnNode(initializer, An_interface_property_cannot_have_an_initializer);
		}
	} else if (isTypeLiteralNode(node->parent)) {
		if (checkGrammarForInvalidDynamicName(node->name(), A_computed_property_name_in_a_type_literal_must_refer_to_an_expression_whose_type_is_a_literal_type_or_a_unique_symbol_type)) {
			return true;
		}
		if (!isPropertySignatureDeclaration(node)) {
			// Type literals cannot contain property declarations
			TSC_UNREACHABLE("Unexpected node kind in checkGrammarProperty (type literal)");
		}
		if (Node* initializer = node->initializer(); initializer != nullptr) {
			return grammarErrorOnNode(initializer, A_type_literal_property_cannot_have_an_initializer);
		}
	}

	if ((node->flags & NodeFlagsAmbient) != 0) {
		checkAmbientInitializer(node);
	}

	if (isPropertyDeclaration(node)) {
		PropertyDeclaration* propDecl = node->as<PropertyDeclaration>();
		Node* postfixToken = propDecl->PostfixToken;
		if (postfixToken != nullptr && postfixToken->kind == Kind::ExclamationToken) {
			if (propDecl->Initializer != nullptr) {
				return grammarErrorOnNode(postfixToken, Declarations_with_initializers_cannot_also_have_definite_assignment_assertions);
			} else if (propDecl->Type == nullptr) {
				return grammarErrorOnNode(postfixToken, Declarations_with_definite_assignment_assertions_must_also_have_type_annotations);
			} else if (!isClassLike(node->parent) || (node->flags & NodeFlagsAmbient) != 0 || isStatic(node) || hasAbstractModifier(node)) {
				return grammarErrorOnNode(postfixToken, A_definite_assignment_assertion_is_not_permitted_in_this_context);
			}
		}
	}

	return false;
}

bool Checker::checkAmbientInitializer(Node* node) {
	Node* initializer = nullptr;
	Node* typeNode = nullptr;
	switch (node->kind) {
	case Kind::VariableDeclaration:
		{
			VariableDeclaration* varDecl = node->as<VariableDeclaration>();
			initializer = varDecl->Initializer;
			typeNode = varDecl->Type;
		}
		break;
	case Kind::PropertyDeclaration:
		{
			PropertyDeclaration* propDecl = node->as<PropertyDeclaration>();
			initializer = propDecl->Initializer;
			typeNode = propDecl->Type;
		}
		break;
	case Kind::PropertySignature:
		{
			PropertySignatureDeclaration* propSig = node->as<PropertySignatureDeclaration>();
			initializer = propSig->Initializer;
			typeNode = propSig->Type;
		}
		break;
	default:
		TSC_UNREACHABLE("Unexpected node kind in checkAmbientInitializer");
	}

	if (initializer != nullptr) {
		bool isInvalidInitializer = !(isInitializerStringOrNumberLiteralExpression(initializer) || isInitializerSimpleLiteralEnumReference(initializer) || initializer->kind == Kind::TrueKeyword || initializer->kind == Kind::FalseKeyword || isInitializerBigIntLiteralExpression(initializer));
		bool isConstOrReadonly = isDeclarationReadonly(node) || (isVariableDeclaration(node) && isVarConstLike(node));
		if (isConstOrReadonly && (typeNode == nullptr)) {
			if (isInvalidInitializer) {
				return grammarErrorOnNode(initializer, A_const_initializer_in_an_ambient_context_must_be_a_string_or_numeric_literal_or_literal_enum_reference);
			}
		} else {
			return grammarErrorOnNode(initializer, Initializers_are_not_allowed_in_ambient_contexts);
		}
	}

	return false;
}

// grammarchecks.go: isInitializerStringOrNumberLiteralExpression
static bool isInitializerStringOrNumberLiteralExpression(Node* expr) {
	return isStringOrNumericLiteralLike(expr) ||
		   (expr->kind == Kind::PrefixUnaryExpression && expr->as<PrefixUnaryExpression>()->Operator == Kind::MinusToken && expr->as<PrefixUnaryExpression>()->Operand->kind == Kind::NumericLiteral);
}

// grammarchecks.go: isInitializerBigIntLiteralExpression
static bool isInitializerBigIntLiteralExpression(Node* expr) {
	if (expr->kind == Kind::BigIntLiteral) {
		return true;
	}

	if (expr->kind == Kind::PrefixUnaryExpression) {
		PrefixUnaryExpression* unaryExpr = expr->as<PrefixUnaryExpression>();
		return unaryExpr->Operator == Kind::MinusToken && unaryExpr->Operand->kind == Kind::BigIntLiteral;
	}

	return false;
}

bool Checker::isInitializerSimpleLiteralEnumReference(Node* expr) {
	if (isPropertyAccessExpression(expr)) {
		return (checkExpressionCached(expr)->flags & TypeFlagsEnumLike) != 0;
	}

	if (isElementAccessExpression(expr)) {
		ElementAccessExpression* elementAccess = expr->as<ElementAccessExpression>();

		return isInitializerStringOrNumberLiteralExpression(elementAccess->ArgumentExpression) &&
			   isEntityNameExpression(elementAccess->Expression) &&
			   (checkExpressionCached(expr)->flags & TypeFlagsEnumLike) != 0;
	}

	return false;
}

bool Checker::checkGrammarTopLevelElementForRequiredDeclareModifier(Node* node) {
	// A declare modifier is required for any top level .d.ts declaration except export=, export default, export as namespace
	// interfaces and imports categories:
	//
	//  DeclarationElement:
	//     ExportAssignment
	//     export_opt   InterfaceDeclaration
	//     export_opt   TypeAliasDeclaration
	//     export_opt   ImportDeclaration
	//     export_opt   ExternalImportDeclaration
	//     export_opt   AmbientDeclaration
	//
	// TODO: The spec needs to be amended to reflect this grammar.
	if (node->kind == Kind::InterfaceDeclaration || node->kind == Kind::TypeAliasDeclaration || node->kind == Kind::ImportDeclaration || node->kind == Kind::JSImportDeclaration || node->kind == Kind::ImportEqualsDeclaration || node->kind == Kind::ExportDeclaration || node->kind == Kind::ExportAssignment || node->kind == Kind::NamespaceExportDeclaration || hasSyntacticModifier(node, ModifierFlagsAmbient | ModifierFlagsExport | ModifierFlagsDefault)) {
		return false;
	}

	return grammarErrorOnFirstToken(node, Top_level_declarations_in_d_ts_files_must_start_with_either_a_declare_or_export_modifier);
}

bool Checker::checkGrammarTopLevelElementsForRequiredDeclareModifier(SourceFile* file) {
	for (Node* decl : file->Statements->nodes) {
		if (isDeclarationNode(decl) || decl->kind == Kind::VariableStatement) {
			if (checkGrammarTopLevelElementForRequiredDeclareModifier(decl)) {
				return true;
			}
		}
	}
	return false;
}

void Checker::checkGrammarSourceFile(SourceFile* node) {
	// grammarchecks.go returns `node.Flags&ast.NodeFlagsAmbient != 0 &&
	// checkGrammarTopLevelElementsForRequiredDeclareModifier(node)`; the C++
	// declaration is void (callers ignore the result).
	if ((node->flags & NodeFlagsAmbient) != 0) {
		checkGrammarTopLevelElementsForRequiredDeclareModifier(node);
	}
}

bool Checker::checkGrammarStatementInAmbientContext(Node* node) {
	if ((node->flags & NodeFlagsAmbient) != 0) {
		// Find containing block which is either Block, ModuleBlock, SourceFile
		NodeLinks* links = nodeLinks.Get(node);
		if (!links->hasReportedStatementInAmbientContext && (isFunctionLike(node->parent) || isAccessor(node->parent))) {
			links->hasReportedStatementInAmbientContext = grammarErrorOnFirstToken(node, An_implementation_cannot_be_declared_in_ambient_contexts);
			return links->hasReportedStatementInAmbientContext;
		}

		// We are either parented by another statement, or some sort of block.
		// If we're in a block, we only want to really report an error once
		// to prevent noisiness.  So use a bit on the block to indicate if
		// this has already been reported, and don't report if it has.
		//
		if (node->parent->kind == Kind::Block || node->parent->kind == Kind::ModuleBlock || node->parent->kind == Kind::SourceFile) {
			NodeLinks* blockLinks = nodeLinks.Get(node->parent);
			// Check if the containing block ever report this error
			if (!blockLinks->hasReportedStatementInAmbientContext) {
				blockLinks->hasReportedStatementInAmbientContext = grammarErrorOnFirstToken(node, Statements_are_not_allowed_in_ambient_contexts);
				return blockLinks->hasReportedStatementInAmbientContext;
			}
		} else {
			// We must be parented by a statement.  If so, there's no need
			// to report the error as our parent will have already done it.
			// debug.Assert(ast.IsStatement(node.Parent)) // !!! commented out in strada - fails if uncommented
		}
	}
	return false;
}

void Checker::checkGrammarNumericLiteral(NumericLiteral* node) {
	std::string nodeText = getTextOfNode(node->asNode());

	// Realism (size) checking
	// We should test against `getTextOfNode(node)` rather than `node.text`, because `node.text` for large numeric literals can contain "."
	// e.g. `node.text` for numeric literal `1100000000000000000000` is `1.1e21`.
	bool isFractional = nodeText.find('.') != std::string::npos;
	bool isScientific = (node->TokenFlags & TokenFlagsScientific) != 0;

	// Scientific notation (e.g. 2e54 and 1e00000000010) can't be converted to bigint
	// Fractional numbers (e.g. 9000000000000000.001) are inherently imprecise anyway
	if (isFractional || isScientific) {
		return;
	}

	// Here `node` is guaranteed to be a numeric literal representing an integer.
	// We need to judge whether the integer `node` represents is <= 2 ** 53 - 1, which can be accomplished by comparing to `value` defined below because:
	// 1) when `node` represents an integer <= 2 ** 53 - 1, `node.text` is its exact string representation and thus `value` precisely represents the integer.
	// 2) otherwise, although `node.text` may be imprecise string representation, its mathematical value and consequently `value` cannot be less than 2 ** 53,
	//    thus the result of the predicate won't be affected.
	Number value = numberFromString(node->Text);
	if (value.v <= Number::maxSafeInteger().v) {
		return;
	}

	addErrorOrSuggestion(false, createDiagnosticForNode(node->asNode(), Numeric_literals_with_absolute_values_equal_to_2_53_or_greater_are_too_large_to_be_represented_accurately_as_integers));
}

bool Checker::checkGrammarBigIntLiteral(BigIntLiteral* node) {
	bool literalType = isLiteralTypeNode(node->parent) || (isPrefixUnaryExpression(node->parent) && isLiteralTypeNode(node->parent->parent));
	if (!literalType) {
		// Don't error on BigInt literals in ambient contexts
		if ((node->flags & NodeFlagsAmbient) == 0 && languageVersion < ScriptTarget::ES2020) {
			if (grammarErrorOnNode(node->asNode(), BigInt_literals_are_not_available_when_targeting_lower_than_ES2020)) {
				return true;
			}
		}
	}
	return false;
}

bool Checker::checkGrammarImportClause(ImportClause* node) {
	switch (node->PhaseModifier) {
	case Kind::TypeKeyword:
		if ((node->flags & NodeFlagsJSDoc) == 0 && node->name != nullptr && node->NamedBindings != nullptr) {
			return grammarErrorOnNode(node->asNode(), A_type_only_import_can_specify_a_default_import_or_named_bindings_but_not_both);
		}
		if (node->NamedBindings != nullptr && node->NamedBindings->kind == Kind::NamedImports) {
			return checkGrammarTypeOnlyNamedImportsOrExports(node->NamedBindings);
		}
		break;
	case Kind::DeferKeyword:
		if (node->name != nullptr) {
			return grammarErrorOnNode(node->asNode(), Default_imports_are_not_allowed_in_a_deferred_import);
		}
		if (node->NamedBindings != nullptr && node->NamedBindings->kind == Kind::NamedImports) {
			return grammarErrorOnNode(node->asNode(), Named_imports_are_not_allowed_in_a_deferred_import);
		}
		if (moduleKind != ModuleKind::ESNext && moduleKind != ModuleKind::Preserve) {
			return grammarErrorOnNode(node->asNode(), Deferred_imports_are_only_supported_when_the_module_flag_is_set_to_esnext_or_preserve);
		}
		break;
	default:
		break;
	}
	return false;
}

bool Checker::checkGrammarImportAttributeValues(ImportAttributes* node) {
	bool hasError = false;
	for (Node* attribute : node->Attributes->nodes) {
		Node* value = attribute->as<ImportAttribute>()->Value;
		if (isStringLiteral(value)) {
			continue;
		}
		hasError = true;
		error(value, Import_attribute_values_must_be_string_literal_expressions);
	}
	return hasError;
}

bool Checker::checkGrammarTypeOnlyNamedImportsOrExports(Node* namedBindings) {
	NodeList* nodeList = namedBindings->elementList();
	for (Node* specifier : nodeList->nodes) {
		bool specifierIsTypeOnly;
		const DiagnosticMessage* message;
		if (specifier->kind == Kind::ImportSpecifier) {
			specifierIsTypeOnly = specifier->isTypeOnly();
			message = The_type_modifier_cannot_be_used_on_a_named_import_when_import_type_is_used_on_its_import_statement;
		} else {
			specifierIsTypeOnly = specifier->isTypeOnly();
			message = The_type_modifier_cannot_be_used_on_a_named_export_when_export_type_is_used_on_its_export_statement;
		}

		if (specifierIsTypeOnly) {
			return grammarErrorOnFirstToken(specifier, message);
		}
	}

	return false;
}

bool Checker::checkGrammarImportCallExpression(Node* node) {
	if (compilerOptions->VerbatimModuleSyntax == Tristate::True && moduleKind == ModuleKind::CommonJS) {
		return grammarErrorOnNode(node, getVerbatimModuleSyntaxErrorMessage(node));
	}

	if (node->expression()->kind == Kind::MetaProperty) {
		if (moduleKind != ModuleKind::ESNext && moduleKind != ModuleKind::Preserve) {
			return grammarErrorOnNode(node, Deferred_imports_are_only_supported_when_the_module_flag_is_set_to_esnext_or_preserve);
		}
	} else if (moduleKind == ModuleKind::ES2015) {
		return grammarErrorOnNode(node, Dynamic_imports_are_only_supported_when_the_module_flag_is_set_to_es2020_es2022_esnext_commonjs_amd_system_umd_node16_node18_node20_or_nodenext);
	}

	CallExpression* nodeAsCall = node->as<CallExpression>();
	if (nodeAsCall->TypeArguments != nullptr) {
		return grammarErrorOnNode(node, This_use_of_import_is_invalid_import_calls_can_be_written_but_they_must_have_parentheses_and_cannot_have_type_arguments);
	}

	NodeList* nodeArguments = nodeAsCall->Arguments;
	const std::vector<Node*>& argumentNodes = nodeArguments->nodes;
	if (!(ModuleKind::Node16 <= moduleKind && moduleKind <= ModuleKind::NodeNext) && moduleKind != ModuleKind::ESNext && moduleKind != ModuleKind::Preserve) {
		// We are allowed trailing comma after proposal-import-assertions.
		checkGrammarForDisallowedTrailingComma(nodeArguments, Trailing_comma_not_allowed);

		if (argumentNodes.size() > 1) {
			Node* importAttributesArgument = argumentNodes[1];
			return grammarErrorOnNode(importAttributesArgument, Dynamic_imports_only_support_a_second_argument_when_the_module_option_is_set_to_esnext_node16_node18_node20_nodenext_or_preserve);
		}
	}

	if (argumentNodes.empty() || argumentNodes.size() > 2) {
		return grammarErrorOnNode(node, Dynamic_imports_can_only_accept_a_module_specifier_and_an_optional_set_of_attributes_as_arguments);
	}

	// see: parseArgumentOrArrayLiteralElement...we use this function which parse arguments of callExpression to parse specifier for dynamic import.
	// parseArgumentOrArrayLiteralElement allows spread element to be in an argument list which is not allowed as specifier in dynamic import.
	Node* spreadElement = findInSlice(argumentNodes, isSpreadElement);
	if (spreadElement != nullptr) {
		return grammarErrorOnNode(spreadElement, Argument_of_dynamic_import_cannot_be_spread_element);
	}
	return false;
}

bool Checker::checkGrammarImportAttributesType(TypeLiteralNode* attributes) {
	NodeList* members = attributes->Members;
	if (members == nullptr) {
		return false;
	}
	for (Node* member : members->nodes) {
		if (member->kind != Kind::PropertySignature) {
			return grammarErrorOnNode(member, An_import_attributes_type_may_only_contain_property_signatures);
		}
		PropertySignatureDeclaration* propertySignature = member->as<PropertySignatureDeclaration>();
		if (ModifierList* modifiers = propertySignature->modifiers; modifiers != nullptr) {
			for (Node* modifier : modifiers->nodes) {
				if (modifier->kind == Kind::ReadonlyKeyword) {
					return grammarErrorOnNode(modifier, An_import_attributes_property_cannot_have_a_readonly_modifier);
				}
			}
		}
		if (propertySignature->Type == nullptr) {
			return grammarErrorOnNode(member, An_import_attributes_property_must_have_a_type_annotation);
		}
		if (propertySignature->questionToken() != nullptr) {
			return grammarErrorOnNode(member, An_import_attributes_property_cannot_be_optional);
		}
		Node* name = propertySignature->name;
		if (!(isStringLiteralLike(name) || isIdentifier(name))) {
			return grammarErrorOnNode(name, An_import_attributes_property_must_have_a_string_literal_or_identifier_name);
		}
		if (name->text() == "resolution-mode") {
			return grammarErrorOnNode(name, X_0_is_not_a_valid_key_for_an_import_attributes_type, {name->text()});
		}

		Node* typeNode = propertySignature->Type;
		if (!isStringLiteralLikeType(typeNode)) {
			return grammarErrorOnNode(typeNode, An_import_attributes_property_must_have_a_string_literal_type_annotation);
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Type helpers used by grammarchecks.go (checker.go)
// ---------------------------------------------------------------------------

// checker.go: isGenericType
bool Checker::isGenericType(Type* t) {
	return getGenericObjectFlags(t) != 0;
}

// checker.go: getGenericObjectFlags — needs isGenericMappedType /
// isGenericTupleType / isGenericStringLikeType, which are not ported yet.

// checker.go: isValidIndexKeyType
bool Checker::isValidIndexKeyType(Type* t) {
	return (t->flags & (TypeFlagsString | TypeFlagsNumber | TypeFlagsESSymbol)) != 0 ||
		   isPatternLiteralType(t) ||
		   ((t->flags & TypeFlagsIntersection) != 0 && !isGenericType(t) && someInSlice(t->types(), [this](Type* u) { return isValidIndexKeyType(u); }));
}

} // namespace checker
} // namespace tsc
