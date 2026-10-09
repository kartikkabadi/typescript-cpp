// Port of tsc/internal/transformers/tstransforms/legacydecorators.go.
#include "internal/transformers/tstransforms/tstransforms.h"

#include <functional>
#include <unordered_map>

#include "internal/binder/referenceresolver.h" // binder::ReferenceResolver

namespace tsc::transformers::tstransforms {
namespace {

// allDecorators — legacydecorators.go:652
struct allDecorators {
	std::vector<Node*> decorators;
	std::vector<std::vector<Node*>> parameters;
};

[[maybe_unused]] NodeList* elideNodes(printer::NodeFactory* f, NodeList* nodes) {
	if (nodes == nullptr) {
		return nullptr;
	}
	if (nodes->nodes.empty()) {
		return nodes;
	}
	NodeList* replacement = f->newNodeList({});
	replacement->loc = nodes->loc;
	return replacement;
}

ModifierList* elideModifiers(printer::NodeFactory* f, ModifierList* nodes) {
	if (nodes == nullptr) {
		return nullptr;
	}
	if (nodes->nodes.empty()) {
		return nodes;
	}
	ModifierList* replacement = f->newModifierList({});
	replacement->loc = nodes->loc;
	return replacement;
}

bool isClassStaticBlockDeclarationOrStaticProperty(Node* node) {
	return isClassStaticBlockDeclaration(node) ||
		   (isPropertyDeclaration(node) && hasStaticModifier(node));
}

bool isNotExportOrDefaultOrDecorator(Node* node) {
	return !(isDecorator(node) || node->kind == Kind::ExportKeyword ||
			 node->kind == Kind::DefaultKeyword);
}

bool decoratorContainsPrivateIdentifierInExpression(Node* decorator) {
	return (decorator->subtreeFacts() &
			SubtreeContainsPrivateIdentifierInExpression) != 0;
}

bool parameterDecoratorsContainPrivateIdentifierInExpression(
	const std::vector<Node*>& parameterDecorators) {
	for (Node* d : parameterDecorators) {
		if (decoratorContainsPrivateIdentifierInExpression(d)) {
			return true;
		}
	}
	return false;
}

/**
 * Gets an array of arrays of decorators for the parameters of a
 * function-like node. The offset into the result array should correspond to
 * the offset of the parameter.
 *
 * @param node The function-like node.
 */
std::vector<std::vector<Node*>> getDecoratorsOfParametersImpl(Node* node) {
	std::vector<std::vector<Node*>> decorators;
	if (node != nullptr) {
		std::vector<Node*> parameters = node->parameters();
		bool firstParameterIsThis =
			!parameters.empty() && isThisParameter(parameters[0]);
		size_t firstParameterOffset = 0;
		size_t numParameters = parameters.size();
		if (firstParameterIsThis) {
			firstParameterOffset = 1;
			numParameters = numParameters - 1;
		}
		for (size_t i = 0; i < numParameters; i++) {
			Node* p = parameters[i + firstParameterOffset];
			if (!decorators.empty() || hasDecorators(p)) {
				if (decorators.empty()) {
					decorators.assign(numParameters, {});
				}
				decorators[i] = p->decorators();
			}
		}
	}
	return decorators;
}

allDecorators* getAllDecoratorsOfProperty(Node* property) {
	std::vector<Node*> decorators = property->decorators();
	if (decorators.empty()) {
		return nullptr;
	}
	return new allDecorators{decorators, {}};
}

allDecorators* getAllDecoratorsOfMethod(Node* method,
										bool useLegacyDecorators) {
	if (method->body() == nullptr) {
		return nullptr;
	}
	std::vector<Node*> decorators = method->decorators();
	std::vector<std::vector<Node*>> parameters;
	if (useLegacyDecorators) {
		parameters = getDecoratorsOfParametersImpl(method);
	}
	if (decorators.empty() && parameters.empty()) {
		return nullptr;
	}
	return new allDecorators{decorators, parameters};
}

allDecorators* getAllDecoratorsOfAccessorsImpl(Node* accessor,
											   ClassDeclaration* parent,
											   bool useLegacyDecorators) {
	if (accessor->body() == nullptr) {
		return nullptr;
	}
	AllAccessorDeclarations decls =
		getAllAccessorDeclarations(parent->Members->nodes, accessor);
	Node* firstAccessorWithDecorators = nullptr;
	if (hasDecorators(decls.firstAccessor)) {
		firstAccessorWithDecorators = decls.firstAccessor;
	} else if (decls.secondAccessor != nullptr &&
			   hasDecorators(decls.secondAccessor)) {
		firstAccessorWithDecorators = decls.secondAccessor;
	}

	if (firstAccessorWithDecorators == nullptr ||
		accessor != firstAccessorWithDecorators) {
		return nullptr;
	}

	std::vector<Node*> decorators =
		firstAccessorWithDecorators->decorators();
	std::vector<std::vector<Node*>> parameters;
	if (useLegacyDecorators && decls.setAccessor != nullptr) {
		parameters = getDecoratorsOfParametersImpl(decls.setAccessor);
	}

	if (decorators.empty() && parameters.empty()) {
		return nullptr;
	}

	return new allDecorators{decorators, parameters};
}

/**
 * Gets an allDecorators object containing the decorators for the member and
 * its parameters.
 *
 * @param parent The class node that contains the member.
 * @param member The class member.
 *
 * @internal
 */
allDecorators* getAllDecoratorsOfClassElement(Node* member,
											  ClassDeclaration* parent,
											  bool useLegacyDecorators) {
	switch (member->kind) {
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		if (!useLegacyDecorators) {
			return getAllDecoratorsOfMethod(member, false);
		}
		return getAllDecoratorsOfAccessorsImpl(member, parent, true);
	case Kind::MethodDeclaration:
		return getAllDecoratorsOfMethod(member, useLegacyDecorators);
	case Kind::PropertyDeclaration:
		return getAllDecoratorsOfProperty(member);
	default:
		return nullptr;
	}
}

/**
 * Gets an allDecorators object containing the decorators for the class and
 * the decorators for the parameters of the constructor of the class.
 *
 * @param node The class node.
 *
 * @internal
 */
allDecorators* getAllDecoratorsOfClass(ClassDeclaration* node,
									   bool useLegacyDecorators) {
	std::vector<Node*> decorators = node->decorators();
	std::vector<std::vector<Node*>> parameters;
	if (useLegacyDecorators) {
		parameters = getDecoratorsOfParametersImpl(
			getFirstConstructorWithBody(node->asNode()));
	}
	if (decorators.empty() && parameters.empty()) {
		return nullptr;
	}
	return new allDecorators{decorators, parameters};
}

bool hasClassElementWithDecoratorContainingPrivateIdentifierInExpression(
	ClassDeclaration* node) {
	if (node->Members == nullptr || node->Members->nodes.empty()) {
		return false;
	}
	for (Node* member : node->Members->nodes) {
		if (!canHaveDecorators(member)) {
			continue;
		}
		allDecorators* allDecs =
			getAllDecoratorsOfClassElement(member, node, true);
		if (allDecs == nullptr) {
			continue;
		}
		for (Node* d : allDecs->decorators) {
			if (decoratorContainsPrivateIdentifierInExpression(d)) {
				return true;
			}
		}
		for (const auto& params : allDecs->parameters) {
			if (parameterDecoratorsContainPrivateIdentifierInExpression(
					params)) {
				return true;
			}
		}
	}
	return false;
}

/**
 * Determines whether a class member is either a static or an instance member
 * of a class that is decorated, or has parameters that are decorated.
 *
 * @param member The class member.
 */
bool isDecoratedClassElement(Node* member, bool isStaticElement,
							 ClassDeclaration* parent) {
	return isStaticElement == isStatic(member) &&
		   nodeOrChildIsDecorated(true, member, parent->asNode(), nullptr);
}

/**
 * Gets either the static or instance members of a class that are decorated,
 * or have parameters that are decorated.
 *
 * @param node The class containing the member.
 * @param isStatic A value indicating whether to retrieve static or instance
 *                 members of the class.
 */
std::vector<Node*> getDecoratedClassElements(ClassDeclaration* node,
											 bool isStatic) {
	if (node->Members == nullptr || node->Members->nodes.empty()) {
		return {};
	}
	std::vector<Node*> members;
	for (Node* member : node->Members->nodes) {
		if (isDecoratedClassElement(member, isStatic, node)) {
			members.push_back(member);
		}
	}
	return members;
}

struct LegacyDecoratorsTransformer : Transformer {
	ScriptTarget languageVersion;
	binder::ReferenceResolver* referenceResolver{};

	/**
	 * A map that keeps track of aliases created for classes with decorators
	 * to avoid issues with the double-binding behavior of classes.
	 */
	std::unordered_map<Node*, Node*> classAliases;
	std::vector<ClassDeclaration*> enclosingClasses;

	static Transformer* create(TransformOptions* opt) {
		auto* tx = new LegacyDecoratorsTransformer;
		tx->languageVersion = opt->CompilerOptions->GetEmitScriptTarget();
		tx->referenceResolver = opt->Resolver;
		return tx->newTransformer(
			[tx](Node* node) -> Node* { return tx->visit(node); },
			opt->Context);
	}

	Node* visit(Node* node) {
		// we have to visit all identifiers in classes, just in case they
		// require substitution
		if ((node->subtreeFacts() & SubtreeContainsDecorators) == 0 &&
			enclosingClasses.empty()) {
			return node;
		}

		switch (node->kind) {
		case Kind::Identifier:
			return visitIdentifier(node);
		case Kind::PropertyAccessExpression:
			return visitPropertyAccessExpression(
				node->as<PropertyAccessExpression>());
		case Kind::Decorator:
			// Decorators are elided. They will be emitted as part of
			// `visitClassDeclaration`.
			return nullptr;
		case Kind::ClassDeclaration:
			return visitClassDeclaration(node->as<ClassDeclaration>());
		case Kind::ClassExpression:
			return visitClassExpression(node->as<ClassExpression>());
		case Kind::Constructor:
			return visitConstructorDeclaration(
				node->as<ConstructorDeclaration>());
		case Kind::MethodDeclaration:
			return visitMethodDeclaration(node->as<MethodDeclaration>());
		case Kind::SetAccessor:
			return visitSetAccessorDeclaration(
				node->as<SetAccessorDeclaration>());
		case Kind::GetAccessor:
			return visitGetAccessorDeclaration(
				node->as<GetAccessorDeclaration>());
		case Kind::PropertyDeclaration:
			return visitPropertyDeclaration(
				node->as<PropertyDeclaration>());
		case Kind::Parameter:
			return visitParamerDeclaration(
				node->as<ParameterDeclaration>());
		case Kind::SourceFile: {
			classAliases.clear();
			enclosingClasses.clear();
			Node* result = visitor()->visitEachChild(node);
			for (printer::EmitHelper* helper : emitContext()->readEmitHelpers()) {
				emitContext()->addEmitHelper(result, helper);
			}
			classAliases.clear();
			enclosingClasses.clear();
			return result;
		}
		default:
			return visitor()->visitEachChild(node);
		}
	}

	Node* visitIdentifier(Node* node) {
		// takes the place of `substituteIdentifier` in the strada transform
		for (ClassDeclaration* d : enclosingClasses) {
			if (classAliases.find(d->asNode()) != classAliases.end() &&
				referenceResolver->GetReferencedValueDeclaration(
					emitContext()->mostOriginal(node)) ==
					emitContext()->mostOriginal(d->asNode())) {
				return classAliases[d->asNode()];
			}
		}
		return node;
	}

	Node* visitPropertyAccessExpression(
		PropertyAccessExpression* node) {
		// Visit the expression but not the name, since property access
		// names should not be substituted.
		// Strada's onSubstituteNode only fires for EmitHint.Expression,
		// which excludes the .name of PropertyAccessExpression.
		Node* expression = visitor()->visitNode(node->Expression);
		if (expression != node->Expression) {
			return factory()->updatePropertyAccessExpression(
				node, expression, node->QuestionDotToken, node->name,
				node->flags);
		}
		return node->asNode();
	}

	Node* finishClassElement(Node* updated, Node* original) {
		if (updated != original) {
			// While we emit the source map for the node after skipping
			// decorators and modifiers, we need to emit the comments for the
			// original range.
			emitContext()->setCommentRange(updated, original->loc);
			emitContext()->setSourceMapRange(
				updated, moveRangePastModifiers(original));
		}
		return updated;
	}

	Node* visitParamerDeclaration(ParameterDeclaration* node) {
		Node* updated = factory()->updateParameterDeclaration(
			node, elideModifiers(factory(), node->modifiers),
			node->DotDotDotToken, visitor()->visitNode(node->name),
			nullptr, nullptr,
			visitor()->visitNode(node->Initializer));
		if (updated != node->asNode()) {
			// While we emit the source map for the node after skipping
			// decorators and modifiers, we need to emit the comments for
			// the original range.
			emitContext()->setCommentRange(updated, node->loc);
			TextRange newLoc = moveRangePastModifiers(node->asNode());
			updated->loc = newLoc;
			emitContext()->setSourceMapRange(updated, newLoc);
			emitContext()->setEmitFlags(updated->name(),
										printer::EFNoTrailingSourceMap);
		}
		return updated;
	}

	// visitPropertyNameOfClassElement visits the property name of a class
	// element, for use when emitting property initializers. For a computed
	// property on a node with decorators, a temporary value is stored for
	// later use.
	Node* visitPropertyNameOfClassElement(Node* member) {
		Node* name = member->name();
		if (isComputedPropertyName(name) && hasDecorators(member)) {
			Node* expression = visitor()->visitNode(
				name->as<ComputedPropertyName>()->Expression);
			Node* innerExpression =
				skipPartiallyEmittedExpressions(expression);
			if (!isSimpleInlineableExpression(innerExpression)) {
				Node* generatedName =
					factory()->newGeneratedNameForNode(name);
				emitContext()->addVariableDeclaration(generatedName);
				return factory()->updateComputedPropertyName(
					name->as<ComputedPropertyName>(),
					factory()->newAssignmentExpression(generatedName,
													   expression));
			}
		}
		return visitor()->visitNode(name);
	}

	Node* visitPropertyDeclaration(PropertyDeclaration* node) {
		if ((node->flags & NodeFlagsAmbient) != 0) {
			return nullptr;
		}
		if (hasSyntacticModifier(node->asNode(),
								 ModifierFlagsAmbient |
									 ModifierFlagsAbstract)) {
			return nullptr;
		}

		return finishClassElement(
			factory()->updatePropertyDeclaration(
				node, visitor()->visitModifiers(node->modifiers),
				visitPropertyNameOfClassElement(node->asNode()), nullptr,
				nullptr, visitor()->visitNode(node->Initializer)),
			node->asNode());
	}

	Node* visitGetAccessorDeclaration(GetAccessorDeclaration* node) {
		return finishClassElement(
			factory()->updateGetAccessorDeclaration(
				node, visitor()->visitModifiers(node->modifiers),
				visitPropertyNameOfClassElement(node->asNode()), nullptr,
				visitor()->visitNodes(node->Parameters), nullptr, nullptr,
				visitor()->visitNode(node->Body)),
			node->asNode());
	}

	Node* visitSetAccessorDeclaration(SetAccessorDeclaration* node) {
		return finishClassElement(
			factory()->updateSetAccessorDeclaration(
				node, visitor()->visitModifiers(node->modifiers),
				visitPropertyNameOfClassElement(node->asNode()), nullptr,
				visitor()->visitNodes(node->Parameters), nullptr, nullptr,
				visitor()->visitNode(node->Body)),
			node->asNode());
	}

	Node* visitMethodDeclaration(MethodDeclaration* node) {
		return finishClassElement(
			factory()->updateMethodDeclaration(
				node, visitor()->visitModifiers(node->modifiers),
				node->AsteriskToken,
				visitPropertyNameOfClassElement(node->asNode()), nullptr,
				nullptr, visitor()->visitNodes(node->Parameters), nullptr,
				nullptr, visitor()->visitNode(node->Body)),
			node->asNode());
	}

	Node* visitConstructorDeclaration(ConstructorDeclaration* node) {
		return factory()->updateConstructorDeclaration(
			node, visitor()->visitModifiers(node->modifiers), nullptr,
			visitor()->visitNodes(node->Parameters), nullptr, nullptr,
			visitor()->visitNode(node->Body));
	}

	Node* visitClassExpression(ClassExpression* node) {
		// Legacy decorators were not supported on class expressions
		return factory()->updateClassExpression(
			node, visitor()->visitModifiers(node->modifiers),
			node->name, nullptr,
			visitor()->visitNodes(node->HeritageClauses),
			visitor()->visitNodes(node->Members));
	}

	Node* visitClassDeclaration(ClassDeclaration* node) {
		bool decorated =
			classOrConstructorParameterIsDecorated(true, node->asNode());
		if (!(decorated ||
			  childIsDecorated(true, node->asNode(), nullptr))) {
			return visitor()->visitEachChild(node->asNode());
		}

		if (decorated) {
			return transformClassDeclarationWithClassDecorators(
				node, node->name);
		}
		return transformClassDeclarationWithoutClassDecorators(
			node, node->name);
	}

	/**
	 * Transforms a non-decorated class declaration.
	 *
	 * @param node A ClassDeclaration node.
	 * @param name The name of the class.
	 */
	Node* transformClassDeclarationWithoutClassDecorators(
		ClassDeclaration* node, Node* name) {
		//  ${modifiers} class ${name} ${heritageClauses} {
		//      ${members}
		//  }
		ModifierList* modifiers =
			visitor()->visitModifiers(node->modifiers);
		NodeList* heritageClauses =
			visitor()->visitNodes(node->HeritageClauses);
		NodeList* initialMembers = visitor()->visitNodes(node->Members);
		auto pair = transformDecoratorsOfClassElements(node, initialMembers);
		NodeList* members = pair.first;
		std::vector<Node*> decorationStatements = std::move(pair.second);

		if (name == nullptr && !decorationStatements.empty()) {
			name = factory()->newGeneratedNameForNode(node->asNode());
		}

		Node* updated = factory()->updateClassDeclaration(
			node, modifiers, name, nullptr, heritageClauses, members);

		if (decorationStatements.empty()) {
			return updated;
		}
		std::vector<Node*> all{updated};
		for (Node* s : decorationStatements) {
			all.push_back(s);
		}
		return factory()->newSyntaxList(std::move(all));
	}

	void popEnclosingClass() { enclosingClasses.pop_back(); }
	void pushEnclosingClass(ClassDeclaration* cls) {
		enclosingClasses.push_back(cls);
	}

	/**
	 * Transforms a decorated class declaration and appends the resulting
	 * statements. If the class requires an alias to avoid issues with
	 * double-binding, the alias is returned.
	 */
	Node* transformClassDeclarationWithClassDecorators(
		ClassDeclaration* node, Node* name) {
		// When we emit an ES6 class that has a class decorator, we must
		// tailor the emit to certain specific cases.
		//
		// In the simplest case, we emit the class declaration as a let
		// declaration, and evaluate decorators after the close of the class
		// body:
		//
		//  [Example 1]
		//  -----------------------------------------------------------------
		//  TypeScript                      | Javascript
		//  -----------------------------------------------------------------
		//  @dec                            | let C = class C {
		//  class C {                       | }
		//  }                               | C = __decorate([dec], C);
		//  -----------------------------------------------------------------
		//  @dec                            | let C = class C {
		//  export class C {                | }
		//  }                               | C = __decorate([dec], C);
		//                                  | export { C };
		//  -----------------------------------------------------------------
		//
		// If a class declaration contains a reference to itself *inside* of
		// the class body, this introduces two bindings to the class: One
		// outside of the class body, and one inside of the class body. If we
		// apply decorators as in [Example 1] above, there is the possibility
		// that the decorator `dec` will return a new value for the
		// constructor, which would result in the binding inside of the class
		// no longer pointing to the same reference as the binding outside of
		// the class.
		//
		// As a result, we must instead rewrite all references to the class
		// *inside* of the class body to instead point to a local temporary
		// alias for the class:
		//
		//  [Example 2]
		//  -----------------------------------------------------------------
		//  TypeScript                      | Javascript
		//  -----------------------------------------------------------------
		//  @dec                            | let C = C_1 = class C {
		//  class C {                       |   static x() { return C_1.y; }
		//    static x() { return C.y; }    | }
		//    static y = 1;                 | C.y = 1;
		//  }                               | C = C_1 = __decorate([dec], C);
		//                                  | var C_1;
		//  -----------------------------------------------------------------
		//  @dec                            | let C = class C {
		//  export class C {                |   static x() { return C_1.y; }
		//    static x() { return C.y; }    | }
		//    static y = 1;                 | C.y = 1;
		//  }                               | C = C_1 = __decorate([dec], C);
		//                                  | export { C };
		//                                  | var C_1;
		//  -----------------------------------------------------------------
		//
		// If a class declaration is the default export of a module, we
		// instead emit the export after the decorated declaration:
		//
		//  [Example 3]
		//  -----------------------------------------------------------------
		//  TypeScript                      | Javascript
		//  -----------------------------------------------------------------
		//  @dec                            | let default_1 = class {
		//  export default class {          | }
		//  }                               | default_1 = __decorate([dec],
		//                                  |     default_1);
		//                                  | export default default_1;
		//  -----------------------------------------------------------------
		//  @dec                            | let C = class C {
		//  export default class C {        | }
		//  }                               | C = __decorate([dec], C);
		//                                  | export default C;
		//  -----------------------------------------------------------------
		//
		// If the class declaration is the default export and a reference to
		// itself inside of the class body, we must emit both an alias for
		// the class *and* move the export after the declaration:
		//
		//  [Example 4]
		//  -----------------------------------------------------------------
		//  TypeScript                      | Javascript
		//  -----------------------------------------------------------------
		//  @dec                            | let C = class C {
		//  export default class C {        |   static x() { return C_1.y; }
		//    static x() { return C.y; }    | }
		//    static y = 1;                 | C.y = 1;
		//  }                               | C = C_1 = __decorate([dec], C);
		//                                  | export default C;
		//                                  | var C_1;
		//  -----------------------------------------------------------------
		//

		bool isExport =
			hasSyntacticModifier(node->asNode(), ModifierFlagsExport);
		bool isDefault =
			hasSyntacticModifier(node->asNode(), ModifierFlagsDefault);
		ModifierList* modifiers = nullptr;
		if (node->modifiers != nullptr &&
			!node->modifiers->nodes.empty()) {
			std::vector<Node*> modifierNodes;
			for (Node* m : node->modifiers->nodes) {
				if (isNotExportOrDefaultOrDecorator(m)) {
					modifierNodes.push_back(m);
				}
			}
			if (modifierNodes.size() != node->modifiers->nodes.size()) {
				modifiers = factory()->newModifierList(modifierNodes);
				modifiers->loc = node->modifiers->loc;
			} else {
				modifiers = node->modifiers;
			}
		}

		TextRange location = moveRangePastModifiers(node->asNode());
		Node* classAlias = getClassAliasIfNeeded(node);
		struct enclosingGuard {
			LegacyDecoratorsTransformer* tx;
			bool active;
			~enclosingGuard() {
				if (active) {
					tx->popEnclosingClass();
				}
			}
		} guard{this, classAlias != nullptr};
		if (classAlias != nullptr) {
			pushEnclosingClass(node);
		}

		// When we used to transform to ES5/3 this would be moved inside an
		// IIFE and should reference the name without any block-scoped
		// variable collision handling - but we don't support that anymore,
		// so we always use the local name for the class
		Node* declName = factory()->getLocalName(
			node->asNode(),
			printer::AssignedNameOptions{false, true, false});

		//  ... = class ${name} ${heritageClauses} {
		//      ${members}
		//  }
		NodeList* heritageClauses =
			visitor()->visitNodes(node->HeritageClauses);
		NodeList* members = visitor()->visitNodes(node->Members);

		auto pair = transformDecoratorsOfClassElements(node, members);
		members = pair.first;
		std::vector<Node*> decorationStatements = std::move(pair.second);

		// If we're emitting to ES2022 or later then we need to reassign the
		// class alias before static initializers are evaluated.
		bool hasStaticInit = false;
		if (members != nullptr) {
			for (Node* m : members->nodes) {
				if (isClassStaticBlockDeclarationOrStaticProperty(m)) {
					hasStaticInit = true;
					break;
				}
			}
		}
		bool assignClassAliasInStaticBlock =
			languageVersion >= ScriptTarget::ES2022 &&
			classAlias != nullptr && members != nullptr &&
			!members->nodes.empty() && hasStaticInit;
		if (assignClassAliasInStaticBlock) {
			std::vector<Node*> memberList;
			memberList.push_back(factory()->newClassStaticBlockDeclaration(
				nullptr,
				factory()->newBlock(
					factory()->newNodeList(
						{factory()->newExpressionStatement(
							factory()->newAssignmentExpression(
								classAlias,
								factory()->newKeywordExpression(
									Kind::ThisKeyword)))}),
					false)));
			for (Node* m : members->nodes) {
				memberList.push_back(m);
			}
			NodeList* newList = factory()->newNodeList(memberList);
			newList->loc = members->loc;
			members = newList;
		}

		Node* exprName = name;
		if (name != nullptr &&
			isGeneratedIdentifier(emitContext(), name)) {
			exprName = nullptr;
		}
		Node* classExpression = factory()->newClassExpression(
			modifiers, exprName, nullptr, heritageClauses, members);

		emitContext()->setOriginal(classExpression, node->asNode());
		classExpression->loc = location;

		//  let ${name} = ${classExpression} where name is either
		//  declaredName if the class doesn't contain self-reference or
		//  decoratedClassAlias if the class contain self-reference.
		Node* varInitializer = classExpression;
		if (classAlias != nullptr && !assignClassAliasInStaticBlock) {
			varInitializer = factory()->newAssignmentExpression(
				classAlias, classExpression);
		}
		Node* varDecl = factory()->newVariableDeclaration(
			declName, nullptr, nullptr, varInitializer);
		emitContext()->setOriginal(varDecl, node->asNode());

		Node* varDeclList = factory()->newVariableDeclarationList(
			factory()->newNodeList({varDecl}), NodeFlagsLet);
		Node* varStatement =
			factory()->newVariableStatement(nullptr, varDeclList);
		emitContext()->setOriginal(varStatement, node->asNode());
		varStatement->loc = location;
		emitContext()->setCommentRange(varStatement, node->loc);

		std::vector<Node*> statements{varStatement};
		for (Node* s : decorationStatements) {
			statements.push_back(s);
		}
		statements.push_back(getConstructorDecorationStatement(node));

		if (isExport) {
			Node* exportStatement;
			if (isDefault) {
				exportStatement = factory()->newExportDefault(declName);
			} else {
				exportStatement = factory()->newExternalModuleExport(
					factory()->getDeclarationName(node->asNode()));
			}
			statements.push_back(exportStatement);
		}

		if (statements.size() == 1) {
			return statements[0];
		}
		return factory()->newSyntaxList(std::move(statements));
	}

	bool hasInternalStaticReference(ClassDeclaration* node) {
		Node* classNode = emitContext()->mostOriginal(node->asNode());
		std::function<bool(Node*)> isOrContainsStaticSelfReference =
			[&](Node* n) -> bool {
			if (isIdentifier(n) &&
				referenceResolver->GetReferencedValueDeclaration(
					emitContext()->mostOriginal(n)) == classNode) {
				return true;
			}
			// For PropertyAccessExpression, only check the expression, not
			// the name. The .Name() is a property access name, not a value
			// reference to the class.
			if (isPropertyAccessExpression(n)) {
				return isOrContainsStaticSelfReference(n->expression());
			}
			return n->forEachChild(isOrContainsStaticSelfReference);
		};
		for (Node* member : node->Members->nodes) {
			if (member->forEachChild(isOrContainsStaticSelfReference)) {
				return true;
			}
		}
		return false;
	}

	/**
	 * Gets a local alias for a class declaration if it is a decorated class
	 * with an internal reference to the static side of the class. This is
	 * necessary to avoid issues with double-binding semantics for the
	 * class name.
	 */
	Node* getClassAliasIfNeeded(ClassDeclaration* node) {
		if (!hasInternalStaticReference(node)) {
			return nullptr;
		}
		std::string nameText = "default";
		if (node->name != nullptr &&
			!isGeneratedIdentifier(emitContext(), node->name)) {
			nameText = node->name->text();
		}

		Node* classAlias = factory()->newUniqueName(nameText);
		emitContext()->addVariableDeclaration(classAlias);
		classAliases[node->asNode()] = classAlias;

		return classAlias;
	}

	/**
	 * Generates a __decorate helper call for a class constructor.
	 *
	 * @param node The class node.
	 */
	Node* getConstructorDecorationStatement(ClassDeclaration* node) {
		Node* expression = generateConstructorDecorationExpression(node);
		if (expression != nullptr) {
			Node* result = factory()->newExpressionStatement(expression);
			emitContext()->setOriginal(result, node->asNode());
			return result;
		}
		return nullptr;
	}

	/**
	 * Generates a __decorate helper call for a class constructor.
	 *
	 * @param node The class node.
	 */
	Node* generateConstructorDecorationExpression(ClassDeclaration* node) {
		allDecorators* allDecs = getAllDecoratorsOfClass(node, true);
		// Decorator expressions are evaluated outside the class body, so
		// references to the class name should use the original binding, not
		// the class alias. In Strada, this is handled by
		// NodeCheckFlags.ConstructorReference which is only set for
		// identifiers inside the class body. Since Corsa lacks per-node
		// flags, we temporarily pop the enclosing class to prevent alias
		// substitution during decorator expression visiting.
		bool hasAlias =
			!enclosingClasses.empty() &&
			enclosingClasses.back() == node;
		if (hasAlias) {
			popEnclosingClass();
		}
		std::vector<Node*> decoratorExpressions =
			transformAllDecoratorsOfDeclaration(allDecs);
		if (hasAlias) {
			pushEnclosingClass(node);
		}
		if (decoratorExpressions.empty()) {
			return nullptr;
		}

		Node* classAlias = nullptr;
		auto it = classAliases.find(node->asNode());
		if (it != classAliases.end()) {
			classAlias = it->second;
		}

		// When we used to transform to ES5/3 this would be moved inside an
		// IIFE and should reference the name without any block-scoped
		// variable collision handling - but we don't support that anymore,
		// so we always use the local name for the class
		Node* localName = factory()->getDeclarationName(
			node->asNode(), printer::NameOptions{false, true});
		Node* decorate = factory()->newDecorateHelper(
			decoratorExpressions, localName, nullptr, nullptr);
		Node* assignmentTarget = decorate;
		if (classAlias != nullptr) {
			assignmentTarget =
				factory()->newAssignmentExpression(classAlias, decorate);
		}
		Node* expression =
			factory()->newAssignmentExpression(localName, assignmentTarget);
		emitContext()->setEmitFlags(expression, printer::EFNoComments);
		emitContext()->setSourceMapRange(
			expression, moveRangePastModifiers(node->asNode()));
		return expression;
	}

	std::pair<NodeList*, std::vector<Node*>>
	transformDecoratorsOfClassElements(ClassDeclaration* node,
									   NodeList* members) {
		std::vector<Node*> decorationStatements;
		for (Node* s : getClassElementDecorationStatements(node, false)) {
			decorationStatements.push_back(s);
		}
		for (Node* s : getClassElementDecorationStatements(node, true)) {
			decorationStatements.push_back(s);
		}
		if (hasClassElementWithDecoratorContainingPrivateIdentifierInExpression(
				node)) {
			std::vector<Node*> memberNodes;
			if (members != nullptr && !members->nodes.empty()) {
				memberNodes = members->nodes;
			}
			memberNodes.push_back(
				factory()->newClassStaticBlockDeclaration(
					nullptr,
					factory()->newBlock(
						factory()->newNodeList(decorationStatements),
						true)));
			members = factory()->newNodeList(memberNodes);
			decorationStatements.clear();
		}

		return {members, std::move(decorationStatements)};
	}

	/**
	 * Generates statements used to apply decorators to either the static or
	 * instance members of a class.
	 *
	 * @param node The class node.
	 * @param isStatic A value indicating whether to generate statements for
	 *                 static or instance members.
	 */
	std::vector<Node*> getClassElementDecorationStatements(
		ClassDeclaration* node, bool isStatic) {
		std::vector<Node*> exprs =
			generateClassElementDecorationExpressions(node, isStatic);
		std::vector<Node*> statements;
		for (Node* e : exprs) {
			statements.push_back(factory()->newExpressionStatement(e));
		}
		return statements;
	}

	/**
	 * Generates expressions used to apply decorators to either the static
	 * or instance members of a class.
	 *
	 * @param node The class node.
	 * @param isStatic A value indicating whether to generate expressions
	 *                 for static or instance members.
	 */
	std::vector<Node*> generateClassElementDecorationExpressions(
		ClassDeclaration* node, bool isStatic) {
		std::vector<Node*> members =
			getDecoratedClassElements(node, isStatic);
		std::vector<Node*> expressions;
		for (Node* member : members) {
			Node* expr =
				generateClassElementDecorationExpression(node, member);
			if (expr != nullptr) {
				expressions.push_back(expr);
			}
		}
		return expressions;
	}

	/**
	 * Generates an expression used to evaluate class element decorators at
	 * runtime.
	 *
	 * @param node The class node that contains the member.
	 * @param member The class member.
	 */
	Node* generateClassElementDecorationExpression(
		ClassDeclaration* node, Node* member) {
		allDecorators* allDecs =
			getAllDecoratorsOfClassElement(member, node, true);
		std::vector<Node*> decoratorExpressions =
			transformAllDecoratorsOfDeclaration(allDecs);
		if (decoratorExpressions.empty()) {
			return nullptr;
		}

		// Emit the call to __decorate. Given the following:
		//
		//   class C {
		//     @dec method(@dec2 x) {}
		//     @dec get accessor() {}
		//     @dec prop;
		//   }
		//
		// The emit for a method is:
		//
		//   __decorate([
		//       dec,
		//       __param(0, dec2),
		//       __metadata("design:type", Function),
		//       __metadata("design:paramtypes", [Object]),
		//       __metadata("design:returntype", void 0)
		//   ], C.prototype, "method", null);
		//
		// The emit for an accessor is:
		//
		//   __decorate([
		//       dec
		//   ], C.prototype, "accessor", null);
		//
		// The emit for a property is:
		//
		//   __decorate([
		//       dec
		//   ], C.prototype, "prop");
		//

		Node* prefix = getClassMemberPrefix(node, member);
		Node* memberName = getExpressionForPropertyName(
			member, (member->flags & NodeFlagsAmbient) == 0);
		Node* descriptor;
		if (isPropertyDeclaration(member) &&
			!hasAccessorModifier(member)) {
			// We emit `void 0` here to indicate to `__decorate` that it can
			// invoke `Object.defineProperty` directly, but that it should
			// not invoke `Object.getOwnPropertyDescriptor`.
			descriptor = factory()->newVoidZeroExpression();
		} else {
			// We emit `null` here to indicate to `__decorate` that it can
			// invoke `Object.getOwnPropertyDescriptor` directly. We have
			// this extra argument here so that we can inject an explicit
			// property descriptor at a later date.
			descriptor = factory()->newKeywordExpression(Kind::NullKeyword);
		}

		Node* helper = factory()->newDecorateHelper(
			decoratorExpressions, prefix, memberName, descriptor);

		emitContext()->setEmitFlags(helper, printer::EFNoComments);
		emitContext()->setSourceMapRange(
			helper, moveRangePastModifiers(member));
		return helper;
	}

	bool isSyntheticMetadataDecorator(Node* node) {
		return emitContext()->isCallToHelper(node->expression(),
											 "__metadata");
	}

	/**
	 * Transforms all of the decorators for a declaration into an array of
	 * expressions.
	 *
	 * @param allDecorators An object containing all of the decorators for
	 * the declaration.
	 */
	std::vector<Node*> transformAllDecoratorsOfDeclaration(
		allDecorators* allDecs) {
		if (allDecs == nullptr) {
			return {};
		}

		// ensure that metadata decorators are last
		// (Go: collections.GroupBy(allDecorators.decorators,
		// isSyntheticMetadataDecorator))
		std::vector<Node*> metadata;
		std::vector<Node*> decorators;
		for (Node* d : allDecs->decorators) {
			if (isSyntheticMetadataDecorator(d)) {
				metadata.push_back(d);
			} else {
				decorators.push_back(d);
			}
		}

		std::vector<Node*> decoratorExpressions;
		for (Node* e : transformDecorators(decorators)) {
			decoratorExpressions.push_back(e);
		}
		for (Node* e :
			 transformDecoratorsOfParameters(allDecs->parameters)) {
			decoratorExpressions.push_back(e);
		}
		for (Node* e : transformDecorators(metadata)) {
			decoratorExpressions.push_back(e);
		}
		return decoratorExpressions;
	}

	std::vector<Node*> transformDecoratorsOfParameters(
		const std::vector<std::vector<Node*>>& parameters) {
		std::vector<Node*> results;
		for (size_t i = 0; i < parameters.size(); i++) {
			const std::vector<Node*>& decorators = parameters[i];
			if (!decorators.empty()) {
				for (Node* decorator : decorators) {
					Node* helper = factory()->newParamHelper(
						visitor()->visitNode(decorator->expression()),
						static_cast<int>(i),
						decorator->expression()->loc);
					emitContext()->setEmitFlags(helper, printer::EFNoComments);
					results.push_back(helper);
				}
			}
		}
		return results;
	}

	/**
	 * Transforms a list of decorators into an expression.
	 *
	 * @param decorator The decorator node.
	 */
	std::vector<Node*> transformDecorators(
		const std::vector<Node*>& decorators) {
		std::vector<Node*> results;
		for (Node* d : decorators) {
			results.push_back(visitor()->visitNode(d->expression()));
		}
		return results;
	}

	Node* getClassMemberPrefix(ClassDeclaration* node, Node* member) {
		if (isStatic(member)) {
			return factory()->getDeclarationName(node->asNode());
		}
		return getClassPrototype(node);
	}

	Node* getClassPrototype(ClassDeclaration* node) {
		return factory()->newPropertyAccessExpression(
			factory()->getDeclarationName(node->asNode()), nullptr,
			factory()->newIdentifier("prototype"), NodeFlagsNone);
	}

	Node* getExpressionForPropertyName(
		Node* member, bool generateNameForComputedPropertyName) {
		Node* name = member->name();
		if (isPrivateIdentifier(name)) {
			return factory()->newIdentifier("");
		} else if (isComputedPropertyName(name)) {
			if (generateNameForComputedPropertyName &&
				!isSimpleInlineableExpression(
					name->as<ComputedPropertyName>()->Expression)) {
				return factory()->newGeneratedNameForNode(name);
			}
			return name->as<ComputedPropertyName>()->Expression;
		} else if (isIdentifier(name)) {
			return factory()->newStringLiteral(name->text(),
											   TokenFlagsNone);
		} else {
			return deepCloneNode(*factory(), name);
		}
	}
};

} // namespace

Transformer* NewLegacyDecoratorsTransformer(TransformOptions* opt) {
	return LegacyDecoratorsTransformer::create(opt);
}

// getDecoratorsOfParameters — legacydecorators.go:768
std::vector<std::vector<Node*>> getDecoratorsOfParameters(Node* node) {
	return getDecoratorsOfParametersImpl(node);
}

} // namespace tsc::transformers::tstransforms
