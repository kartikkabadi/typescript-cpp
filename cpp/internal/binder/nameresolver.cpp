// Port of tsc/internal/binder/nameresolver.go
#include "internal/binder/nameresolver.h"

#include "internal/diagnostics/messages_generated.h"
#include "internal/scanner/scanner.h"

namespace tsc {
namespace binder {

static bool isTypeParameterSymbolDeclaredInContainer(Symbol* symbol, Node* container);
static bool isSelfReferenceLocation(Node* node, Node* lastLocation);
static bool getIsDeferredContext(Node* location, Node* lastLocation);

Symbol* NameResolver::resolve(Node* location, std::string_view name, SymbolFlags meaning,
	const DiagnosticMessage* nameNotFoundMessage, bool isUse, bool excludeGlobals) {
	std::string scratch;
	Symbol* result = nullptr;
	Node* lastLocation = nullptr;
	Node* lastSelfReferenceLocation = nullptr;
	Node* propertyWithInvalidInitializer = nullptr;
	Node* associatedDeclarationForContainingInitializerOrBindingName = nullptr;
	bool withinDeferredContext = false;
	Node* grandparent = nullptr;
	Node* originalLocation = location; // needed for did-you-mean error reporting
	const bool nameIsConst = name == "const";
	bool done = false;

	while (location != nullptr && !done) {
		if (nameIsConst && isConstAssertion(location)) {
			// `const` in an `as const` has no symbol, but issues no error because there is no *actual* lookup of the type
			return nullptr;
		}
		if (isModuleOrEnumDeclaration(location) && lastLocation != nullptr && location->name() == lastLocation) {
			// If lastLocation is the name of a namespace or enum, skip the parent since it will have its own locals
			// that could conflict.
			lastLocation = location;
			location = location->parent;
		}
		bool isModuleAttributes = isModuleDeclaration(location) &&
			location->as<ModuleDeclaration>()->Attributes != nullptr &&
			lastLocation == location->as<ModuleDeclaration>()->Attributes;
		SymbolTable* locals = location->locals();
		// Locals of a source file are not in scope (because they get merged into the global symbol table)
		if (locals != nullptr && !isGlobalSourceFile(location)) {
			result = lookupOrDefault(locals, name, meaning);
			if (result != nullptr) {
				bool useResult = true;
				if (isModuleAttributes) {
					useResult = false;
				} else if (isFunctionLike(location) && lastLocation != nullptr && lastLocation != location->body()) {
					// symbol lookup restrictions for function-like declarations
					// - Type parameters of a function are in scope in the entire function declaration, including the parameter
					//   list and return type. However, local types are only in scope in the function body.
					// - parameters are only in the scope of function body
					// This restriction does not apply to JSDoc comment types because they are parented
					// at a higher level than type parameters would normally be
					if ((meaning & result->flags & SymbolFlagsType) != 0 && lastLocation->kind != Kind::JSDoc) {
						// type parameters are visible in parameter list, return type and type parameter list.
						// Synthetic fake scopes are added for signatures so type parameters are accessible from them.
						useResult = (result->flags & SymbolFlagsTypeParameter) != 0 &&
							((lastLocation->flags & NodeFlagsSynthesized) != 0 ||
								lastLocation == location->type() ||
								lastLocation->kind == Kind::Parameter ||
								lastLocation->kind == Kind::JSDocParameterTag ||
								lastLocation->kind == Kind::JSDocReturnTag ||
								lastLocation->kind == Kind::TypeParameter);
					}
					if ((meaning & result->flags & SymbolFlagsVariable) != 0) {
						// expression inside parameter will lookup as normal variable scope when targeting es2015+
						if (useOuterVariableScopeInParameter(result, location, lastLocation)) {
							useResult = false;
						} else if ((result->flags & SymbolFlagsFunctionScopedVariable) != 0) {
							// parameters are visible only inside function body, parameter list and return type
							useResult = lastLocation->kind == Kind::Parameter ||
								(lastLocation->flags & NodeFlagsSynthesized) != 0 ||
								(lastLocation == location->type() &&
									findAncestor(result->valueDeclaration, isParameterDeclaration) != nullptr);
						}
					}
				} else if (location->kind == Kind::ConditionalType) {
					// A type parameter declared using 'infer T' in a conditional type is visible only in
					// the true branch of the conditional type.
					useResult = lastLocation == location->as<ConditionalTypeNode>()->TrueType;
				}
				if (useResult) {
					done = true;
					break;
				}
				result = nullptr;
			}
		}
		withinDeferredContext = withinDeferredContext || getIsDeferredContext(location, lastLocation);
		switch (location->kind) {
		case Kind::SourceFile:
			if (!isExternalOrCommonJSModule(location->as<SourceFile>())) {
				break;
			}
			[[fallthrough]];
		case Kind::ModuleDeclaration: {
			if (isModuleAttributes) {
				break;
			}
			Symbol* moduleSymbol = getSymbolOfDeclarationOrDefault(location);
			if (moduleSymbol == nullptr) {
				break;
			}
			auto& moduleExports = moduleSymbol->exports;
			if (isSourceFile(location) || (isModuleDeclaration(location) &&
				(location->flags & NodeFlagsAmbient) != 0 && !isGlobalScopeAugmentation(location))) {
				// It's an external module. First see if the module has an export default and if the local
				// name of that export default matches.
				auto it = moduleExports.find(InternalSymbolNameDefault);
				result = it != moduleExports.end() ? it->second : nullptr;
				if (result != nullptr) {
					Symbol* localSymbol = getLocalSymbolForExportDefault(result);
					if (localSymbol != nullptr && (result->flags & meaning) != 0 && localSymbol->name == name) {
						done = true;
						break;
					}
					result = nullptr;
				}
				// Because of module/namespace merging, a module's exports are in scope,
				// yet we never want to treat an export specifier as putting a member in scope.
				Symbol* moduleExport = getSymbolFromTableView(moduleExports, name);
				if (moduleExport != nullptr && moduleExport->flags == SymbolFlagsAlias &&
					(getDeclarationOfKind(moduleExport, Kind::ExportSpecifier) != nullptr ||
						getDeclarationOfKind(moduleExport, Kind::NamespaceExport) != nullptr)) {
					break;
				}
			}
			if (name != InternalSymbolNameDefault) {
				result = lookupOrDefault(&moduleExports, name, meaning & SymbolFlagsModuleMember);
				if (result != nullptr) {
					if (isSourceFile(location) && location->as<SourceFile>()->CommonJSModuleIndicator != nullptr &&
						(result->flags & SymbolFlagsType) == 0) {
						result = nullptr;
					} else {
						done = true;
						break;
					}
				}
			}
			break;
		}
		case Kind::EnumDeclaration: {
			Symbol* enumSymbol = getSymbolOfDeclarationOrDefault(location);
			if (enumSymbol == nullptr) {
				break;
			}
			result = lookupOrDefault(&enumSymbol->exports, name, meaning & SymbolFlagsEnumMember);
			if (result != nullptr) {
				if (nameNotFoundMessage != nullptr && compilerOptions->GetIsolatedModules() &&
					(location->flags & NodeFlagsAmbient) == 0 &&
					getSourceFileOfNode(location) != getSourceFileOfNode(result->valueDeclaration)) {
					std::string isolatedModulesLikeFlagName =
						compilerOptions->VerbatimModuleSyntax == Tristate::True
						? "verbatimModuleSyntax" : "isolatedModules";
					reportError(originalLocation,
						Cannot_access_0_from_another_file_without_qualification_when_1_is_enabled_Use_2_instead,
						{std::string(name), isolatedModulesLikeFlagName,
						 enumSymbol->name + "." + std::string(name)});
				}
				done = true;
				break;
			}
			break;
		}
		case Kind::PropertyDeclaration:
			if (!isStatic(location)) {
				Node* ctor = findConstructorDeclaration(location->parent);
				if (ctor != nullptr && ctor->locals() != nullptr) {
					if (lookupOrDefault(ctor->locals(), name, meaning & SymbolFlagsValue) != nullptr) {
						// Remember the property node, it will be used later to report appropriate error
						propertyWithInvalidInitializer = location;
					}
				}
			}
			break;
		case Kind::ClassDeclaration:
		case Kind::ClassExpression:
		case Kind::InterfaceDeclaration: {
			Symbol* declSymbol = getSymbolOfDeclarationOrDefault(location);
			result = declSymbol != nullptr
				? lookupOrDefault(&declSymbol->members, name, meaning & SymbolFlagsType)
				: nullptr;
			if (result != nullptr) {
				if (!isTypeParameterSymbolDeclaredInContainer(result, location)) {
					// ignore type parameters not declared in this container
					result = nullptr;
					break;
				}
				if (lastLocation != nullptr && isStatic(lastLocation)) {
					// TypeScript 1.0 spec (April 2014): 3.4.1
					// The scope of a type parameter extends over the entire declaration with which the type
					// parameter list is associated, with the exception of static member declarations in classes.
					if (nameNotFoundMessage != nullptr) {
						reportError(originalLocation, Static_members_cannot_reference_class_type_parameters);
					}
					return nullptr;
				}
				done = true;
				break;
			}
			if (isClassExpression(location) && (meaning & SymbolFlagsClass) != 0) {
				Node* className = location->name();
				if (className != nullptr && name == className->textView(scratch)) {
					result = location->symbol();
					done = true;
					break;
				}
			}
			break;
		}
		case Kind::ExpressionWithTypeArguments: {
			if (lastLocation == location->expression() && isHeritageClause(location->parent) &&
				location->parent->as<HeritageClause>()->Token == Kind::ExtendsKeyword) {
				Node* container = location->parent->parent;
				if (isClassLike(container)) {
					Symbol* declSymbol = getSymbolOfDeclarationOrDefault(container);
					result = declSymbol != nullptr
						? lookupOrDefault(&declSymbol->members, name, meaning & SymbolFlagsType)
						: nullptr;
					if (result != nullptr) {
						if (nameNotFoundMessage != nullptr) {
							reportError(originalLocation, Base_class_expressions_cannot_reference_class_type_parameters);
						}
						return nullptr;
					}
				}
			}
			break;
		}
		// It is not legal to reference a class's own type parameters from a computed property name that
		// belongs to the class.
		case Kind::ComputedPropertyName:
			grandparent = location->parent->parent;
			if (isClassLike(grandparent) || isInterfaceDeclaration(grandparent)) {
				Symbol* declSymbol = getSymbolOfDeclarationOrDefault(grandparent);
				result = declSymbol != nullptr
					? lookupOrDefault(&declSymbol->members, name, meaning & SymbolFlagsType)
					: nullptr;
				if (result != nullptr) {
					if (nameNotFoundMessage != nullptr) {
						reportError(originalLocation, A_computed_property_name_cannot_reference_a_type_parameter_from_its_containing_type);
					}
					return nullptr;
				}
			}
			break;
		case Kind::MethodDeclaration:
		case Kind::Constructor:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::FunctionDeclaration:
			if ((meaning & SymbolFlagsVariable) != 0 && name == "arguments") {
				result = getArgumentsSymbol();
				done = true;
			}
			break;
		case Kind::FunctionExpression: {
			if ((meaning & SymbolFlagsVariable) != 0 && name == "arguments") {
				result = getArgumentsSymbol();
				done = true;
				break;
			}
			if ((meaning & SymbolFlagsFunction) != 0) {
				Node* functionName = location->name();
				if (functionName != nullptr && name == functionName->textView(scratch)) {
					result = location->symbol();
					done = true;
					break;
				}
			}
			break;
		}
		case Kind::Decorator:
			// Decorators are resolved at the class declaration. Resolving at the parameter
			// or member would result in looking up locals in the method.
			if (location->parent != nullptr && location->parent->kind == Kind::Parameter) {
				location = location->parent;
			}
			if (location->parent != nullptr &&
				(isClassElement(location->parent) || location->parent->kind == Kind::ClassDeclaration)) {
				location = location->parent;
			}
			break;
		case Kind::Parameter: {
			auto* parameterDeclaration = location->as<ParameterDeclaration>();
			if (lastLocation != nullptr &&
				(lastLocation == parameterDeclaration->Initializer ||
					(lastLocation == parameterDeclaration->name && isBindingPattern(lastLocation)))) {
				if (associatedDeclarationForContainingInitializerOrBindingName == nullptr) {
					associatedDeclarationForContainingInitializerOrBindingName = location;
				}
			}
			break;
		}
		case Kind::BindingElement: {
			auto* bindingElement = location->as<BindingElement>();
			if (lastLocation != nullptr &&
				(lastLocation == bindingElement->Initializer ||
					(lastLocation == bindingElement->name && isBindingPattern(lastLocation)))) {
				if (isPartOfParameterDeclaration(location) &&
					associatedDeclarationForContainingInitializerOrBindingName == nullptr) {
					associatedDeclarationForContainingInitializerOrBindingName = location;
				}
			}
			break;
		}
		case Kind::InferType:
			if ((meaning & SymbolFlagsTypeParameter) != 0) {
				Node* parameterName = location->as<InferTypeNode>()->TypeParameter->name();
				if (parameterName != nullptr && name == parameterName->textView(scratch)) {
					result = location->as<InferTypeNode>()->TypeParameter->symbol();
					done = true;
				}
			}
			break;
		case Kind::ExportSpecifier: {
			auto* exportSpecifier = location->as<ExportSpecifier>();
			if (lastLocation != nullptr && lastLocation == exportSpecifier->PropertyName &&
				location->parent->parent->moduleSpecifier() != nullptr) {
				location = location->parent->parent->parent;
			}
			break;
		}
		default:
			break;
		}
		if (done) {
			break;
		}
		if (isSelfReferenceLocation(location, lastLocation)) {
			lastSelfReferenceLocation = location;
		}
		lastLocation = location;
		location = location->parent;
	}

	// We just climbed up parents looking for the name. If `result == lastSelfReferenceLocation.symbol`,
	// this is a self-reference of `lastLocation` and shouldn't count as a use.
	if (isUse && result != nullptr &&
		(lastSelfReferenceLocation == nullptr || result != lastSelfReferenceLocation->symbol())) {
		if (symbolReferenced) {
			symbolReferenced(result, meaning);
		}
	}
	if (result == nullptr && !excludeGlobals && globals != nullptr) {
		result = lookupOrDefault(globals, name, meaning | SymbolFlagsGlobalLookup);
	}
	if (result == nullptr) {
		if (originalLocation != nullptr && isInJSFile(originalLocation) && originalLocation->parent != nullptr) {
			if (isRequireCall(originalLocation->parent, false /*requireStringLiteralLikeArgument*/)) {
				return requireSymbol;
			}
		}
	}
	if (nameNotFoundMessage != nullptr) {
		if (propertyWithInvalidInitializer != nullptr && onPropertyWithInvalidInitializer &&
			onPropertyWithInvalidInitializer(originalLocation, std::string(name),
				propertyWithInvalidInitializer, result)) {
			return nullptr;
		}
		if (result == nullptr) {
			if (onFailedToResolveSymbol) {
				onFailedToResolveSymbol(originalLocation, std::string(name), meaning,
					nameNotFoundMessage);
			}
		} else {
			if (onSuccessfullyResolvedSymbol) {
				onSuccessfullyResolvedSymbol(originalLocation, result, meaning, lastLocation,
					associatedDeclarationForContainingInitializerOrBindingName, withinDeferredContext);
			}
		}
	}
	return result;
}

bool NameResolver::useOuterVariableScopeInParameter(Symbol* result, Node* location, Node* lastLocation) {
	if (isParameterDeclaration(lastLocation)) {
		Node* body = location->body();
		if (body != nullptr && result->valueDeclaration != nullptr &&
			result->valueDeclaration->pos() >= body->pos() &&
			result->valueDeclaration->end() <= body->end()) {
			// check for several cases where we introduce temporaries that require moving the
			// name/initializer of the parameter to the body:
			// - static field in a class expression
			// - optional chaining pre-es2020
			// - nullish coalesce pre-es2020
			// - spread assignment in binding pattern pre-es2017
			Node* functionLocation = location;
			Tristate declarationRequiresScopeChange = Tristate::Unknown;
			if (getRequiresScopeChangeCache) {
				declarationRequiresScopeChange = getRequiresScopeChangeCache(functionLocation);
			}
			if (declarationRequiresScopeChange == Tristate::Unknown) {
				bool any = false;
				for (Node* p : functionLocation->parameters()) {
					if (requiresScopeChange(p)) {
						any = true;
						break;
					}
				}
				declarationRequiresScopeChange = any ? Tristate::True : Tristate::False;
				if (setRequiresScopeChangeCache) {
					setRequiresScopeChangeCache(functionLocation, declarationRequiresScopeChange);
				}
			}
			return declarationRequiresScopeChange != Tristate::True;
		}
	}
	return false;
}

bool NameResolver::requiresScopeChange(Node* node) {
	auto* d = node->as<ParameterDeclaration>();
	return requiresScopeChangeWorker(d->name) ||
		(d->Initializer != nullptr && requiresScopeChangeWorker(d->Initializer));
}

bool NameResolver::requiresScopeChangeWorker(Node* node) {
	switch (node->kind) {
	case Kind::ArrowFunction:
	case Kind::FunctionExpression:
	case Kind::FunctionDeclaration:
	case Kind::Constructor:
		return false;
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::PropertyAssignment:
		return requiresScopeChangeWorker(node->name());
	case Kind::PropertyDeclaration:
		if (hasStaticModifier(node)) {
			return !compilerOptions->GetEmitStandardClassFields();
		}
		return requiresScopeChangeWorker(node->name());
	default:
		if (isNullishCoalesce(node) || isOptionalChain(node)) {
			return compilerOptions->GetEmitScriptTarget() < ScriptTarget::ES2020;
		}
		if (isBindingElement(node) && node->as<BindingElement>()->DotDotDotToken != nullptr &&
			isObjectBindingPattern(node->parent)) {
			return compilerOptions->GetEmitScriptTarget() < ScriptTarget::ES2017;
		}
		if (isTypeNode(node)) {
			return false;
		}
		return node->forEachChild([this](Node* child) { return requiresScopeChangeWorker(child); });
	}
}

Diagnostic* NameResolver::reportError(Node* location, const DiagnosticMessage* message,
	const std::vector<std::string>& args) {
	if (error) {
		return error(location, message, args);
	}
	return nullptr; // Default implementation does not report errors
}

Symbol* NameResolver::getSymbolOfDeclarationOrDefault(Node* node) {
	if (getSymbolOfDeclaration) {
		return getSymbolOfDeclaration(node);
	}
	// Default implementation does not support merged symbols
	return node->symbol();
}

Symbol* NameResolver::lookupOrDefault(SymbolTable* symbols, std::string_view name, SymbolFlags meaning) {
	if (lookup) {
		return lookup(symbols, name, meaning);
	}
	// Default implementation does not support following aliases or merged symbols
	if (meaning != 0) {
		Symbol* symbol = getSymbolFromTableView(*symbols, name);
		if (symbol != nullptr && (symbol->flags & meaning) != 0) {
			return symbol;
		}
	}
	return nullptr;
}

Symbol* NameResolver::getArgumentsSymbol() {
	if (argumentsSymbol == nullptr) {
		// Default implementation synthesizes a transient symbol for `arguments`
		argumentsSymbol = new Symbol();
		argumentsSymbol->name = "arguments";
		argumentsSymbol->flags = SymbolFlagsProperty | SymbolFlagsTransient;
	}
	return argumentsSymbol;
}

Symbol* getLocalSymbolForExportDefault(Symbol* symbol) {
	if (!isExportDefaultSymbol(symbol) || symbol->declarations.empty()) {
		return nullptr;
	}
	for (Node* decl : symbol->declarations) {
		Symbol* localSymbol = decl->localSymbol();
		if (localSymbol != nullptr) {
			return localSymbol;
		}
	}
	return nullptr;
}

bool isExportDefaultSymbol(Symbol* symbol) {
	return symbol != nullptr && !symbol->declarations.empty() &&
		hasSyntacticModifier(symbol->declarations[0], ModifierFlagsDefault);
}

static bool getIsDeferredContext(Node* location, Node* lastLocation) {
	if (location->kind != Kind::ArrowFunction && location->kind != Kind::FunctionExpression) {
		// initializers in instance property declaration of class like entities are executed in
		// constructor and thus deferred. A name is evaluated within the enclosing scope - so it
		// shouldn't count as deferred
		return isTypeQueryNode(location) ||
			((isFunctionLikeDeclaration(location) ||
				(location->kind == Kind::PropertyDeclaration && !isStatic(location))) &&
				(lastLocation == nullptr || lastLocation != location->name()));
	}
	if (lastLocation != nullptr && lastLocation == location->name()) {
		return false;
	}
	// generator functions and async functions are not inlined in control flow when immediately invoked
	if ((location->bodyData().asteriskToken != nullptr && *location->bodyData().asteriskToken != nullptr) ||
		hasSyntacticModifier(location, ModifierFlagsAsync)) {
		return true;
	}
	return getImmediatelyInvokedFunctionExpression(location) == nullptr;
}

static bool isTypeParameterSymbolDeclaredInContainer(Symbol* symbol, Node* container) {
	for (Node* decl : symbol->declarations) {
		if (decl->kind == Kind::TypeParameter) {
			Node* parent = decl->parent;
			if (parent == container) {
				return true;
			}
		}
	}
	return false;
}

static bool isSelfReferenceLocation(Node* node, Node* lastLocation) {
	switch (node->kind) {
	case Kind::Parameter:
		return lastLocation != nullptr && lastLocation == node->name();
	case Kind::FunctionDeclaration:
	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::EnumDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::ModuleDeclaration: // For `namespace N { N; }`
		return true;
	default:
		return false;
	}
}

}  // namespace binder
}  // namespace tsc
