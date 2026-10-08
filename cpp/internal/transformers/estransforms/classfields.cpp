// Port of tsc/internal/transformers/estransforms/classfields.go — the class
// fields transformer (the largest single transformer file): private identifiers
// → WeakMap/WeakSet storage + brand checks, field initializers → constructor
// assignments or Object.defineProperty, class static blocks → IIFEs, `this`/
// `super` in static initializers → captured temps, `accessor` fields → backing
// fields + get/set pairs, destructuring targets for private names.
#include "internal/transformers/estransforms/estransforms.h"

#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/scanner/scanner.h"

namespace tsc::transformers::estransforms {

// anonymousFunctionDefinition — namedevaluation.go:15. ClassExpression |
// FunctionExpression | ArrowFunction (all *ast.Node in this port).
using anonymousFunctionDefinition = Node;

// Dep-stub declarations live in estransforms.h (owning header); TSC_UNREACHABLE
// bodies live at the bottom of this file — removed when the esdecorator-cluster
// slice lands.

namespace {

// debug.Assert — debug.go. Non-fatal in Go; fatal here like other slices.
[[noreturn]] inline void debugAssertFail() {
	TSC_UNREACHABLE("debug.Assert");
}
inline void debugAssert(bool cond) {
	if (!cond) {
		debugAssertFail();
	}
}
inline void debugAssert(bool cond, const char*) {
	if (!cond) {
		debugAssertFail();
	}
}

// ast.IsCompoundAssignment — utilities.go:2007
inline bool isCompoundAssignment(Kind token) {
	return token >= KindFirstCompoundAssignment && token <= KindLastCompoundAssignment;
}

// ast.IsInitializedProperty — utilities.go:1144
inline bool isInitializedProperty(Node* member) {
	return member->kind == Kind::PropertyDeclaration && member->initializer() != nullptr;
}

// ast.IsObjectBindingOrAssignmentElement — utilities.go:3404
inline bool isObjectBindingOrAssignmentElement(Node* node) {
	switch (node->kind) {
	case Kind::BindingElement:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::SpreadAssignment:
		return true;
	default:
		return false;
	}
}

// ast.IsArrayBindingOrAssignmentElement — utilities.go:3381
inline bool isArrayBindingOrAssignmentElement(Node* node) {
	switch (node->kind) {
	case Kind::BindingElement:
	case Kind::OmittedExpression:
	case Kind::SpreadElement:
	case Kind::ArrayLiteralExpression:
	case Kind::ObjectLiteralExpression:
	case Kind::Identifier:
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
		return true;
	default:
		return isAssignmentExpression(node, /*excludeCompoundAssignment*/ true);
	}
}

// Forward declarations for file-local helpers defined at the bottom of this file.
Node* createPrivateStaticFieldInitializer(printer::NodeFactory* factory, Node* variableName, Node* initializer);
Node* createPrivateInstanceFieldInitializer(printer::NodeFactory* factory, Node* receiver, Node* initializer, Node* weakMapName);
Node* createPrivateInstanceMethodInitializer(printer::NodeFactory* factory, Node* receiver, Node* weakSetName);
bool isStaticPropertyDeclarationOrClassStaticBlock(Node* node);
bool isNonStaticMethodOrAccessorWithPrivateName(Node* member);
Node* createMemberAccessForPropertyName(printer::NodeFactory* factory, printer::EmitContext* emitContext, Node* receiver, Node* name, Node* location);
bool shouldBeCapturedInTempVariable(Node* node);
void flattenCommaList(Node* node, const std::function<bool(Node*)>& yield);
bool flattenCommaListWorker(Node* node, const std::function<bool(Node*)>& yield);

// ast.CreateModifiersFromModifierFlags — utilities.go:3291
std::vector<Node*> createModifiersFromModifierFlags(
	ModifierFlags flags,
	const std::function<Node*(Kind)>& newModifier) {
	std::vector<Node*> result;
	if ((flags & ModifierFlagsExport) != 0) {
		result.push_back(newModifier(Kind::ExportKeyword));
	}
	if ((flags & ModifierFlagsAmbient) != 0) {
		result.push_back(newModifier(Kind::DeclareKeyword));
	}
	if ((flags & ModifierFlagsDefault) != 0) {
		result.push_back(newModifier(Kind::DefaultKeyword));
	}
	if ((flags & ModifierFlagsConst) != 0) {
		result.push_back(newModifier(Kind::ConstKeyword));
	}
	if ((flags & ModifierFlagsPublic) != 0) {
		result.push_back(newModifier(Kind::PublicKeyword));
	}
	if ((flags & ModifierFlagsPrivate) != 0) {
		result.push_back(newModifier(Kind::PrivateKeyword));
	}
	if ((flags & ModifierFlagsProtected) != 0) {
		result.push_back(newModifier(Kind::ProtectedKeyword));
	}
	if ((flags & ModifierFlagsAbstract) != 0) {
		result.push_back(newModifier(Kind::AbstractKeyword));
	}
	if ((flags & ModifierFlagsStatic) != 0) {
		result.push_back(newModifier(Kind::StaticKeyword));
	}
	if ((flags & ModifierFlagsOverride) != 0) {
		result.push_back(newModifier(Kind::OverrideKeyword));
	}
	if ((flags & ModifierFlagsReadonly) != 0) {
		result.push_back(newModifier(Kind::ReadonlyKeyword));
	}
	if ((flags & ModifierFlagsAccessor) != 0) {
		result.push_back(newModifier(Kind::AccessorKeyword));
	}
	if ((flags & ModifierFlagsAsync) != 0) {
		result.push_back(newModifier(Kind::AsyncKeyword));
	}
	if ((flags & ModifierFlagsIn) != 0) {
		result.push_back(newModifier(Kind::InKeyword));
	}
	if ((flags & ModifierFlagsOut) != 0) {
		result.push_back(newModifier(Kind::OutKeyword));
	}
	return result;
}

}  // namespace

// classFacts tracks various facts about a class being transformed.
using classFacts = int;
inline constexpr classFacts classFactsNone = 0;
inline constexpr classFacts classFactsClassWasDecorated = 1 << 0;
inline constexpr classFacts classFactsNeedsClassConstructorReference = 1 << 1;
inline constexpr classFacts classFactsNeedsClassSuperReference = 1 << 2;
inline constexpr classFacts classFactsNeedsSubstitutionForThisInClassStaticField = 1 << 3;
inline constexpr classFacts classFactsWillHoistInitializersToConstructor = 1 << 4;

// privateIdentifierInfo stores information about a private identifier during transformation.
struct privateIdentifierInfo {
	printer::PrivateIdentifierKind kind = printer::PrivateIdentifierKind::Untransformed;
	// brandCheckIdentifier can contain:
	//  - For instance field: The WeakMap that will be the storage for the field.
	//  - For instance methods or accessors: The WeakSet that will be used for brand checking.
	//  - For static members: The constructor that will be used for brand checking.
	Node* brandCheckIdentifier = nullptr;
	// isStatic stores if the identifier is static or not.
	bool isStatic = false;
	// isValid stores if the identifier declaration is valid or not. Reserved names (e.g. #constructor)
	// or duplicate identifiers are considered invalid.
	bool isValid = false;
	// variableName contains the variable that will serve as the storage for a static field.
	Node* variableName = nullptr;
	// methodName is the identifier for a variable that will contain the private method implementation.
	Node* methodName = nullptr;
	// getterName is the identifier for a variable that will contain the private get accessor implementation, if any.
	Node* getterName = nullptr;
	// setterName is the identifier for a variable that will contain the private set accessor implementation, if any.
	Node* setterName = nullptr;
};

// privateEnvironmentData stores class-scoped environment data for private identifiers.
struct privateEnvironmentData {
	// className is used for prefixing generated variable names.
	Node* className = nullptr;
	// weakSetName is used for brand check on private methods.
	Node* weakSetName = nullptr;
};

// privateEnvironment stores a map of private identifier names to their transform info.
// Like Strada, it uses two separate maps: one for non-generated identifiers (keyed by text)
// and one for generated identifiers (keyed by original AST node). This prevents collisions
// when different auto-accessors produce generated backing field names with the same text.
struct privateEnvironment {
	privateEnvironmentData data;
	std::unordered_map<std::string, privateIdentifierInfo*> members;
	std::unordered_map<Node*, privateIdentifierInfo*> generatedIdentifiers;
};

// classLexicalEnvironment stores information about the lexical environment of a class.
struct classLexicalEnvironment {
	classFacts facts = classFactsNone;
	// classConstructor is used for brand checks on static members, and `this` references in static initializers.
	Node* classConstructor = nullptr;
	Node* classThis = nullptr;
	// superClassReference is used for `super` references in static initializers.
	Node* superClassReference = nullptr;
};

// classLexicalEnv is a linked list of class lexical environments.
struct classLexicalEnv {
	classLexicalEnv* previous = nullptr;
	classLexicalEnvironment* data = nullptr;
	privateEnvironment* privateEnv = nullptr;
};

struct classFieldsTransformer : Transformer {
	const CompilerOptions* compilerOptions = nullptr;
	binder::ReferenceResolver* resolver = nullptr;

	// Computed configuration flags
	bool shouldTransformInitializersUsingSet = false;
	bool shouldTransformInitializersUsingDefine = false;
	bool shouldTransformInitializers = false;
	bool shouldTransformPrivateElementsOrClassStaticBlocks = false;
	bool shouldTransformAutoAccessors = false;
	bool shouldTransformThisInStaticInitializers = false;
	bool shouldTransformSuperInStaticInitializers = false;
	bool shouldTransformPrivateStaticElementsInFile = false;
	bool legacyDecorators = false;

	// pendingExpressions tracks what computed name expressions originating from elided names
	// must be inlined at the next execution site, in document order.
	std::vector<Node*> pendingExpressions;
	// pendingStatements tracks what computed name expression statements and static property
	// initializers must be emitted at the next execution site, in document order (for decorated classes).
	std::vector<Node*> pendingStatements;
	classLexicalEnv* lexicalEnvironment = nullptr;
	Node* currentClassContainer = nullptr;
	Node* currentClassElement = nullptr;
	// classAliases maps class declarations to alias identifiers for substituting class name
	// references in static initializers. Replaces Strada's onSubstituteNode/trySubstituteClassAlias.
	std::unordered_map<Node*, Node*> classAliases;
	collections::Set<Node*> enclosingClassDeclarations;
	bool inIterationStatement = false;
	// insideComputedPropertyName replaces Strada's onEmitNode for ComputedPropertyName, which
	// switches to the outer lexical environment. Used by visitThisExpression() to apply
	// the outer environment's substitution without requiring currentClassElement to be static.
	bool insideComputedPropertyName = false;
	Node* parentNode = nullptr;
	Node* currentNode = nullptr;

	// Visitors
	NodeVisitor* modifierVisitor = nullptr;
	NodeVisitor* discardedValueVisitor = nullptr;
	NodeVisitor* heritageClauseVisitor = nullptr;
	NodeVisitor* assignmentTargetVisitor = nullptr;
	NodeVisitor* classElementVisitor = nullptr;
	NodeVisitor* accessorFieldResultVisitor = nullptr;
	NodeVisitor* arrayAssignmentElementVisitor = nullptr;
	NodeVisitor* objectAssignmentElementVisitor = nullptr;
	NodeVisitor* substitutionVisitor = nullptr;

	// Pre-bound callbacks to avoid repeated closure allocation.
	std::function<bool(anonymousFunctionDefinition*)> isAnonymousClassNeedingAssignedName;

	// Method-pointer shorthand mirroring Go's `func(tx *classFieldsTransformer, node *ast.Node) *ast.Node`.
	using TxVisitor = Node* (classFieldsTransformer::*)(Node*);
	using TxFactsVisitor = Node* (classFieldsTransformer::*)(Node*, classFacts);

	bool requiresBlockScopedVar();
	bool classExpressionNeedsBlockScopedTemp();
	Node* visitSourceFile(SourceFile* node);
	Node* visitModifier(Node* node);
	Node* pushNode(Node* node);
	void popNode(Node* grandparentNode);
	Node* visitForSubstitution(Node* node);
	Node* visit(Node* node);
	Node* visitDiscardedValue(Node* node);
	Node* visitHeritageClause(Node* node);
	Node* visitAssignmentTarget(Node* node);
	Node* visitDestructuringAssignmentTarget(Node* node);
	Node* visitClassElement(Node* node);
	Node* visitPropertyName(Node* name);
	Node* visitAccessorFieldResult(Node* node);
	Node* visitIdentifier(Identifier* node);
	Node* visitPrivateIdentifier(Node* node);
	Node* transformPrivateIdentifierInInExpression(BinaryExpression* node);
	Node* visitPropertyAssignment(PropertyAssignment* node);
	Node* visitVariableStatement(VariableStatement* node);
	Node* visitVariableDeclaration(VariableDeclaration* node);
	Node* visitParameterDeclaration(ParameterDeclaration* node);
	Node* visitBindingElement(BindingElement* node);
	Node* visitExportAssignment(ExportAssignment* node);
	Node* injectPendingExpressions(Node* expression);
	Node* visitComputedPropertyName(ComputedPropertyName* node);
	Node* visitConstructorDeclaration(Node* node);
	bool shouldTransformClassElementToWeakMap(Node* node);
	bool shouldAlwaysTransformPrivateStaticElements(Node* node);
	bool nodeHasTransformPrivateStaticElementsFlag(Node* node);
	Node* visitMethodOrAccessorDeclaration(Node* node);
	ModifierList* extractNonStaticNonAccessorModifiers(Node* node);
	Node* setCurrentClassElementAnd(Node* classElement, TxVisitor visitor, Node* node);
	Node* visitEachChildOfNode(Node* node);
	Node* setInIterationStatementAnd(bool inIteration, TxVisitor visitor, Node* node);
	Node* clearClassElementAndVisitEachChild(Node* node);
	Node* visitFunctionExpressionOrDeclaration(Node* node);
	Node* setClassElementAndVisitEachChild(Node* node);
	Node* getHoistedFunctionName(Node* node);
	Node* tryGetClassThis();
	Node* tryGetClassThisNoContainer();
	Node* transformAutoAccessor(PropertyDeclaration* node);
	Node* transformPrivateFieldInitializer(PropertyDeclaration* node);
	Node* transformPublicFieldInitializer(PropertyDeclaration* node);
	Node* transformFieldInitializer(PropertyDeclaration* node);
	bool shouldTransformAutoAccessorsInCurrentClass();
	Node* visitPropertyDeclaration(Node* node);
	Node* createPrivateIdentifierAccess(privateIdentifierInfo* info, Node* receiver);
	Node* createPrivateIdentifierAccessHelper(privateIdentifierInfo* info, Node* receiver);
	Node* visitPropertyAccessExpression(PropertyAccessExpression* node);
	Node* visitPropertyAccessExpressionForSubstitution(PropertyAccessExpression* node);
	Node* visitElementAccessExpression(ElementAccessExpression* node);
	Node* visitPreOrPostfixUnaryExpression(Node* node, bool discarded);
	Node* visitForStatement(ForStatement* node);
	Node* visitExpressionStatement(ExpressionStatement* node);
	std::pair<Node*, Node*> createCopiableReceiverExpr(Node* receiver);
	Node* visitCallExpression(CallExpression* node);
	Node* visitTaggedTemplateExpression(TaggedTemplateExpression* node);
	Node* transformClassStaticBlockDeclaration(Node* node);
	std::vector<Node*> setCurrentClassElementAndVisitStatements(Node* classElement, std::vector<Node*> statements);
	bool isAnonymousClassNeedingAssignedNameWorker(anonymousFunctionDefinition* node);
	Node* visitBinaryExpression(BinaryExpression* node, bool discarded);
	Node* visitParenthesizedExpression(ParenthesizedExpression* node, bool discarded);
	Node* createPrivateIdentifierAssignment(privateIdentifierInfo* info, Node* receiver, Node* right, Kind op);
	std::vector<Node*> getPrivateInstanceMethodsAndAccessors(Node* node);
	bool memberContainsConstructorReference(Node* member, Node* classDecl);
	bool classContainsConstructorReference(Node* node);
	classFacts getClassFacts(Node* node);
	Node* visitExpressionWithTypeArgumentsInHeritageClause(ExpressionWithTypeArguments* node);
	Node* visitInNewClassLexicalEnvironment(Node* node, TxFactsVisitor visitor);
	Node* visitClassDeclaration(ClassDeclaration* node);
	Node* visitClassDeclarationInNewClassLexicalEnvironment(Node* node, classFacts facts);
	Node* visitClassExpression(ClassExpression* node);
	Node* visitClassExpressionInNewClassLexicalEnvironment(Node* node, classFacts facts);
	Node* visitClassStaticBlockDeclaration(Node* node);
	Node* visitThisExpression(Node* node);
	std::pair<NodeList*, Node*> transformClassMembers(Node* node);
	void createBrandCheckWeakSetForPrivateMethods();
	Node* transformConstructor(ConstructorDeclaration* constructor, Node* container);
	std::vector<Node*> transformConstructorBodyWorker(
		std::vector<Node*> statementsOut, std::vector<Node*> statementsIn,
		int statementOffset, std::vector<int> superPath, int superPathDepth,
		std::vector<Node*> initializerStatements, ConstructorDeclaration* constructor);
	Node* transformConstructorBody(Node* container, ConstructorDeclaration* constructor, bool isDerivedClass);
	std::vector<Node*> addPropertyOrClassStaticBlockStatements(std::vector<Node*> statements,
	                                                           std::vector<Node*> properties,
	                                                           Node* receiver);
	Node* transformPropertyOrClassStaticBlock(Node* property, Node* receiver);
	std::vector<Node*> generateInitializedPropertyExpressionsOrClassStaticBlock(
		std::vector<Node*> propertiesOrClassStaticBlocks, Node* receiver);
	Node* transformProperty(PropertyDeclaration* property, Node* receiver);
	Node* transformPropertyWorker(PropertyDeclaration* property, Node* receiver);
	std::vector<Node*> addInstanceMethodStatements(std::vector<Node*> statements,
	                                               std::vector<Node*> methods, Node* receiver);
	Node* visitInvalidSuperProperty(Node* node);
	Node* getPropertyNameExpressionIfNeeded(Node* name, bool shouldHoist);
	void startClassLexicalEnvironment();
	void endClassLexicalEnvironment();
	classLexicalEnvironment* getClassLexicalEnvironment();
	privateEnvironment* getPrivateIdentifierEnvironment();
	void addPendingExpressions(Node* expr);
	void addPrivateIdentifierPropertyDeclarationToEnvironment(Node* node, Node* name);
	void addPrivateIdentifierMethodToEnvironment(Node* name, classLexicalEnvironment* lex,
	                                             privateEnvironment* env, bool isStatic, bool isValid);
	void addPrivateIdentifierGetAccessorToEnvironment(Node* name, classLexicalEnvironment* lex,
	                                                  privateEnvironment* env, bool isStatic, bool isValid,
	                                                  privateIdentifierInfo* previousInfo);
	void addPrivateIdentifierSetAccessorToEnvironment(Node* name, classLexicalEnvironment* lex,
	                                                  privateEnvironment* env, bool isStatic, bool isValid,
	                                                  privateIdentifierInfo* previousInfo);
	void addPrivateIdentifierAutoAccessorToEnvironment(Node* node, Node* name,
	                                                   classLexicalEnvironment* lex,
	                                                   privateEnvironment* env,
	                                                   bool isStatic, bool isValid);
	void addPrivateIdentifierToEnvironment(Node* node);
	void setPrivateIdentifier(privateEnvironment* env, Node* name, privateIdentifierInfo* info);
	privateIdentifierInfo* getPrivateIdentifier(privateEnvironment* env, Node* name);
	Node* createHoistedVariableForClass(std::string nameText, Node* node, std::string suffix);
	Node* createHoistedVariableForClassFromNode(Node* name, std::string suffix);
	Node* createHoistedVariableForPrivateName(Node* name, std::string suffix);
	privateIdentifierInfo* accessPrivateIdentifier(Node* name);
	Node* wrapPrivateIdentifierForDestructuringTarget(Node* node);
	Node* visitAssignmentElement(Node* node);
	Node* visitAssignmentRestElement(Node* node);
	Node* visitArrayAssignmentElement(Node* node);
	Node* visitAssignmentProperty(Node* node);
	Node* visitShorthandAssignmentProperty(Node* node);
	Node* visitAssignmentRestProperty(Node* node);
	Node* visitObjectAssignmentElement(Node* node);
	Node* visitAssignmentPattern(Node* node);
	bool isReservedPrivateName(Node* node);
	std::vector<Node*> getProperties(Node* node, bool requireInitializer, bool isStatic);
	std::vector<Node*> getStaticPropertiesAndClassStaticBlock(Node* node);
	std::pair<Node*, Node*> createCallBinding(Node* node);
	Node* createAccessorPropertyGetRedirector(PropertyDeclaration* node, ModifierList* modifiers,
	                                          Node* name, Node* receiver);
	Node* createAccessorPropertySetRedirector(PropertyDeclaration* node, ModifierList* modifiers,
	                                          Node* name, Node* receiver);
};

Transformer* newClassFieldsTransformer(TransformOptions* opts) {
	ScriptTarget languageVersion = opts->CompilerOptions->GetEmitScriptTarget();
	bool useDefineForClassFields = opts->CompilerOptions->GetUseDefineForClassFields();

	// When targeting ESNext+ with useDefineForClassFields (the default), there are no class
	// field transformations to perform and no prior transform sets EFTransformPrivateStaticElements,
	// so every node would be returned unchanged. Skip entirely.
	if (languageVersion >= ScriptTarget::ESNext && useDefineForClassFields) {
		return nullptr;
	}

	auto* tx = new classFieldsTransformer{};
	tx->compilerOptions = opts->CompilerOptions;
	tx->resolver = opts->Resolver;
	tx->legacyDecorators = tristateIsTrue(opts->CompilerOptions->ExperimentalDecorators);

	// Always transform field initializers using Set semantics when `useDefineForClassFields: false`.
	tx->shouldTransformInitializersUsingSet = !useDefineForClassFields;

	// Transform field initializers using Define semantics when `useDefineForClassFields: true` and target < ES2022.
	tx->shouldTransformInitializersUsingDefine = useDefineForClassFields && languageVersion < ScriptTarget::ES2022;

	tx->shouldTransformInitializers = tx->shouldTransformInitializersUsingSet || tx->shouldTransformInitializersUsingDefine;

	// We need to transform private members and class static blocks when target < ES2022.
	tx->shouldTransformPrivateElementsOrClassStaticBlocks = languageVersion < ScriptTarget::ES2022;

	// We need to transform `accessor` fields when target < ESNext.
	// We may need to transform `accessor` fields when `useDefineForClassFields: false`
	tx->shouldTransformAutoAccessors = languageVersion < ScriptTarget::ESNext;

	// We need to transform `this` in a static initializer into a reference to the class
	// when target < ES2022 since the assignment will be moved outside of the class body.
	tx->shouldTransformThisInStaticInitializers = languageVersion < ScriptTarget::ES2022;

	// Since target is always >= ES2015, this is always the same as
	// shouldTransformThisInStaticInitializers.
	tx->shouldTransformSuperInStaticInitializers = tx->shouldTransformThisInStaticInitializers;

	Transformer* result = tx->newTransformer([tx](Node* node) { return tx->visit(node); }, opts->Context);
	tx->modifierVisitor = tx->emitContext()->newNodeVisitor([tx](Node* n) { return tx->visitModifier(n); });
	tx->discardedValueVisitor = tx->emitContext()->newNodeVisitor([tx](Node* n) { return tx->visitDiscardedValue(n); });
	tx->heritageClauseVisitor = tx->emitContext()->newNodeVisitor([tx](Node* n) { return tx->visitHeritageClause(n); });
	tx->assignmentTargetVisitor = tx->emitContext()->newNodeVisitor([tx](Node* n) { return tx->visitAssignmentTarget(n); });
	tx->classElementVisitor = tx->emitContext()->newNodeVisitor([tx](Node* n) { return tx->visitClassElement(n); });
	tx->accessorFieldResultVisitor = tx->emitContext()->newNodeVisitor([tx](Node* n) { return tx->visitAccessorFieldResult(n); });
	tx->arrayAssignmentElementVisitor = tx->emitContext()->newNodeVisitor([tx](Node* n) { return tx->visitArrayAssignmentElement(n); });
	tx->objectAssignmentElementVisitor = tx->emitContext()->newNodeVisitor([tx](Node* n) { return tx->visitObjectAssignmentElement(n); });
	tx->substitutionVisitor = tx->emitContext()->newNodeVisitor([tx](Node* n) { return tx->visitForSubstitution(n); });
	tx->isAnonymousClassNeedingAssignedName = [tx](anonymousFunctionDefinition* n) {
		return tx->isAnonymousClassNeedingAssignedNameWorker(n);
	};

	return result;
}

// requiresBlockScopedVar returns true when private field temp variables should be
// declared as block-scoped (let) rather than function-scoped (var). This occurs when
// a class expression is directly inside a loop body.
// Replaces Strada's resolver.hasNodeCheckFlag(node, NodeCheckFlags.BlockScopedBindingInLoop).
bool classFieldsTransformer::requiresBlockScopedVar() {
	return inIterationStatement && currentClassContainer != nullptr &&
		isClassExpression(currentClassContainer);
}

// classExpressionNeedsBlockScopedTemp returns true when the class expression's temp variable
// must be block-scoped. This is more specific than requiresBlockScopedVar: the class temp only
// needs to be block-scoped when the class expression has a non-static property with a computed
// property name inside a loop (matching the checker's BlockScopedBindingInLoop on the class node).
bool classFieldsTransformer::classExpressionNeedsBlockScopedTemp() {
	if (!requiresBlockScopedVar()) {
		return false;
	}
	for (Node* member : currentClassContainer->members()) {
		if (isPropertyDeclaration(member) && !hasStaticModifier(member) &&
			member->name() != nullptr && isComputedPropertyName(member->name())) {
			return true;
		}
	}
	return false;
}

Node* classFieldsTransformer::visitSourceFile(SourceFile* node) {
	if (node->IsDeclarationFile) {
		return node->asNode();
	}
	lexicalEnvironment = nullptr;
	shouldTransformPrivateStaticElementsInFile =
		(emitContext()->emitFlags(node->asNode()) & printer::EFTransformPrivateStaticElements) != 0;
	classAliases.clear();
	enclosingClassDeclarations = {};
	Node* visited = visitor()->visitEachChild(node->asNode());
	for (auto* helper : emitContext()->readEmitHelpers()) {
		emitContext()->addEmitHelper(visited, helper);
	}
	classAliases.clear();
	enclosingClassDeclarations = {};
	return visited;
}

Node* classFieldsTransformer::visitModifier(Node* node) {
	if (node->kind == Kind::AccessorKeyword) {
		if (shouldTransformAutoAccessorsInCurrentClass()) {
			return nullptr;
		}
		return node;
	}
	if (isModifier(node)) {
		return node;
	}
	return nullptr;
}

Node* classFieldsTransformer::pushNode(Node* node) {
	Node* grandparentNode = parentNode;
	parentNode = currentNode;
	currentNode = node;
	return grandparentNode;
}

void classFieldsTransformer::popNode(Node* grandparentNode) {
	currentNode = parentNode;
	parentNode = grandparentNode;
}

// visitForSubstitution visits nodes solely for class alias substitution in subtrees
// that don't contain class field or lexical this/super transforms. It substitutes
// identifiers that reference class declarations with their aliases, while skipping
// the .Name() of PropertyAccessExpressions since Strada's onSubstituteNode only
// fires for EmitHint.Expression, which excludes property access names.
Node* classFieldsTransformer::visitForSubstitution(Node* node) {
	if (node->kind == Kind::Identifier) {
		return visitIdentifier(node->as<Identifier>());
	}
	if (node->kind == Kind::PropertyAccessExpression &&
		isIdentifier(node->as<PropertyAccessExpression>()->name)) {
		return visitPropertyAccessExpressionForSubstitution(node->as<PropertyAccessExpression>());
	}
	return substitutionVisitor->visitEachChild(node);
}

// visit is the main visitor.
Node* classFieldsTransformer::visit(Node* node) {
	Node* grandparentNode = pushNode(node);
	// Go `defer tx.popNode(grandparentNode)`.
	struct PopNodeGuard {
		classFieldsTransformer* tx;
		Node* gp;
		~PopNodeGuard() { tx->popNode(gp); }
	} popGuard{this, grandparentNode};

	if ((node->subtreeFacts() & (SubtreeContainsClassFields | SubtreeContainsLexicalThisOrSuper)) == 0) {
		if (currentClassContainer != nullptr && !classAliases.empty()) {
			// Continue visiting for alias substitution even in non-class-field subtrees.
			return visitForSubstitution(node);
		}
		return node;
	}

	switch (node->kind) {
	case Kind::SourceFile:
		return visitSourceFile(node->as<SourceFile>());
	case Kind::ClassDeclaration:
		return visitClassDeclaration(node->as<ClassDeclaration>());
	case Kind::ClassExpression:
		return visitClassExpression(node->as<ClassExpression>());
	case Kind::ClassStaticBlockDeclaration:
	case Kind::PropertyDeclaration:
		TSC_UNREACHABLE("Use `classElementVisitor` instead.");
	case Kind::PropertyAssignment:
		return visitPropertyAssignment(node->as<PropertyAssignment>());
	case Kind::VariableStatement:
		return visitVariableStatement(node->as<VariableStatement>());
	case Kind::VariableDeclaration:
		return visitVariableDeclaration(node->as<VariableDeclaration>());
	case Kind::Parameter:
		return visitParameterDeclaration(node->as<ParameterDeclaration>());
	case Kind::BindingElement:
		return visitBindingElement(node->as<BindingElement>());
	case Kind::ExportAssignment:
		return visitExportAssignment(node->as<ExportAssignment>());
	case Kind::PrivateIdentifier:
		return visitPrivateIdentifier(node);
	case Kind::PropertyAccessExpression:
		return visitPropertyAccessExpression(node->as<PropertyAccessExpression>());
	case Kind::ElementAccessExpression:
		return visitElementAccessExpression(node->as<ElementAccessExpression>());
	case Kind::PrefixUnaryExpression:
	case Kind::PostfixUnaryExpression:
		return visitPreOrPostfixUnaryExpression(node, /*discarded*/ false);
	case Kind::BinaryExpression:
		return visitBinaryExpression(node->as<BinaryExpression>(), /*discarded*/ false);
	case Kind::ParenthesizedExpression:
		return visitParenthesizedExpression(node->as<ParenthesizedExpression>(), /*discarded*/ false);
	case Kind::CallExpression:
		return visitCallExpression(node->as<CallExpression>());
	case Kind::ExpressionStatement:
		return visitExpressionStatement(node->as<ExpressionStatement>());
	case Kind::TaggedTemplateExpression:
		return visitTaggedTemplateExpression(node->as<TaggedTemplateExpression>());
	case Kind::ForStatement:
		return visitForStatement(node->as<ForStatement>());
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::DoStatement:
	case Kind::WhileStatement:
		return setInIterationStatementAnd(true, &classFieldsTransformer::visitEachChildOfNode, node);
	case Kind::ThisKeyword:
		return visitThisExpression(node);
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
		return setInIterationStatementAnd(false, &classFieldsTransformer::visitFunctionExpressionOrDeclaration, node);
	case Kind::Constructor:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return setInIterationStatementAnd(false, &classFieldsTransformer::setClassElementAndVisitEachChild, node);
	default:
		return visitor()->visitEachChild(node);
	}
}

// visitDiscardedValue visits a node in an expression whose result is discarded.
Node* classFieldsTransformer::visitDiscardedValue(Node* node) {
	switch (node->kind) {
	case Kind::PrefixUnaryExpression:
	case Kind::PostfixUnaryExpression:
		return visitPreOrPostfixUnaryExpression(node, /*discarded*/ true);
	case Kind::BinaryExpression:
		return visitBinaryExpression(node->as<BinaryExpression>(), /*discarded*/ true);
	case Kind::ParenthesizedExpression:
		return visitParenthesizedExpression(node->as<ParenthesizedExpression>(), /*discarded*/ true);
	default:
		return visit(node);
	}
}

// visitHeritageClause visits a node in a HeritageClause.
Node* classFieldsTransformer::visitHeritageClause(Node* node) {
	switch (node->kind) {
	case Kind::HeritageClause:
		return heritageClauseVisitor->visitEachChild(node);
	case Kind::ExpressionWithTypeArguments:
		return visitExpressionWithTypeArgumentsInHeritageClause(node->as<ExpressionWithTypeArguments>());
	default:
		return visit(node);
	}
}

// visitAssignmentTarget visits the assignment target of a destructuring assignment.
Node* classFieldsTransformer::visitAssignmentTarget(Node* node) {
	switch (node->kind) {
	case Kind::ObjectLiteralExpression:
	case Kind::ArrayLiteralExpression:
		return visitAssignmentPattern(node);
	default:
		return visit(node);
	}
}

Node* classFieldsTransformer::visitDestructuringAssignmentTarget(Node* node) {
	if (isObjectLiteralExpression(node) || isArrayLiteralExpression(node)) {
		return visitAssignmentPattern(node);
	}
	if (isPropertyAccessExpression(node) &&
		isPrivateIdentifier(node->as<PropertyAccessExpression>()->name)) {
		return wrapPrivateIdentifierForDestructuringTarget(node);
	}
	if (shouldTransformSuperInStaticInitializers && currentClassElement != nullptr &&
		isSuperProperty(node) &&
		isStaticPropertyDeclarationOrClassStaticBlock(currentClassElement) &&
		lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr) {
		classLexicalEnvironment* data = lexicalEnvironment->data;
		if ((data->facts & classFactsClassWasDecorated) != 0) {
			return visitInvalidSuperProperty(node);
		}
		if (data->classConstructor != nullptr && data->superClassReference != nullptr) {
			Node* name = nullptr;
			if (isElementAccessExpression(node)) {
				name = visitor()->visitNode(node->as<ElementAccessExpression>()->ArgumentExpression);
			} else if (isPropertyAccessExpression(node) &&
				isIdentifier(node->as<PropertyAccessExpression>()->name)) {
				name = factory()->newStringLiteralFromNode(node->as<PropertyAccessExpression>()->name);
			}
			if (name != nullptr) {
				Node* temp = factory()->newTempVariable();
				Node* setExpr = factory()->newReflectSetCall(
					data->superClassReference,
					name,
					temp,
					data->classConstructor
				);
				return factory()->newAssignmentTargetWrapper(temp, setExpr);
			}
		}
	}
	return visitor()->visitEachChild(node);
}

// visitClassElement visits a member of a class.
Node* classFieldsTransformer::visitClassElement(Node* node) {
	switch (node->kind) {
	case Kind::Constructor:
		return setCurrentClassElementAnd(node, &classFieldsTransformer::visitConstructorDeclaration, node);
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::MethodDeclaration:
		return setCurrentClassElementAnd(node, &classFieldsTransformer::visitMethodOrAccessorDeclaration, node);
	case Kind::PropertyDeclaration:
		return setCurrentClassElementAnd(node, &classFieldsTransformer::visitPropertyDeclaration, node);
	case Kind::ClassStaticBlockDeclaration:
		return setCurrentClassElementAnd(node, &classFieldsTransformer::visitClassStaticBlockDeclaration, node);
	case Kind::ComputedPropertyName:
		return visitComputedPropertyName(node->as<ComputedPropertyName>());
	case Kind::SemicolonClassElement:
		return node;
	default:
		if (isModifierLike(node)) {
			return visitModifier(node);
		}
		return visit(node);
	}
}

// visitPropertyName visits a property name of a class member.
Node* classFieldsTransformer::visitPropertyName(Node* name) {
	if (isComputedPropertyName(name)) {
		return visitComputedPropertyName(name->as<ComputedPropertyName>());
	}
	return visitor()->visitNode(name);
}

// visitAccessorFieldResult visits the results of an auto-accessor field transformation in a second pass.
Node* classFieldsTransformer::visitAccessorFieldResult(Node* node) {
	switch (node->kind) {
	case Kind::PropertyDeclaration:
		return transformFieldInitializer(node->as<PropertyDeclaration>());
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return visitClassElement(node);
	default:
		TSC_UNREACHABLE("Expected node to either be a PropertyDeclaration, GetAccessorDeclaration, or SetAccessorDeclaration");
	}
}

// visitIdentifier replaces Strada's onSubstituteNode/trySubstituteClassAlias. Instead of
// substituting at emit time using NodeCheckFlags.ConstructorReference, we resolve the
// identifier to its declaration and check if that declaration has a registered alias.
Node* classFieldsTransformer::visitIdentifier(Identifier* node) {
	Node* declaration = resolver->GetReferencedValueDeclaration(emitContext()->mostOriginal(node->asNode()));
	if (declaration != nullptr) {
		auto it = classAliases.find(declaration);
		if (it != classAliases.end() && enclosingClassDeclarations.Has(declaration)) {
			Node* clone = it->second->clone(*factory());
			emitContext()->setSourceMapRange(clone, node->loc);
			emitContext()->setCommentRange(clone, node->loc);
			return clone;
		}
	}
	return node->asNode();
}

// visitPrivateIdentifier handles an undeclared private name. Replace it with an empty
// identifier to indicate a problem with the code.
// Note: private identifiers in statement position (e.g., `#;`) are intercepted earlier
// by visitExpressionStatement, which preserves them so the runtime throws a SyntaxError.
Node* classFieldsTransformer::visitPrivateIdentifier(Node* node) {
	if (!shouldTransformPrivateElementsOrClassStaticBlocks) {
		return node;
	}
	if (parentNode != nullptr && isStatement(parentNode)) {
		return node;
	}
	Node* result = factory()->newIdentifier("");
	emitContext()->setOriginal(result, node);
	return result;
}

// transformPrivateIdentifierInInExpression visits `#id in expr`.
Node* classFieldsTransformer::transformPrivateIdentifierInInExpression(BinaryExpression* node) {
	privateIdentifierInfo* info = accessPrivateIdentifier(node->Left);
	if (info != nullptr) {
		Node* receiver = visitor()->visitNode(node->Right);
		Node* result = factory()->newClassPrivateFieldInHelper(info->brandCheckIdentifier, receiver);
		emitContext()->setOriginal(result, node->asNode());
		return result;
	}
	// Private name has not been declared. Subsequent transformers will handle this error
	return visitor()->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::visitPropertyAssignment(PropertyAssignment* node) {
	// 13.2.5.5 RS: PropertyDefinitionEvaluation
	//   PropertyAssignment : PropertyName `:` AssignmentExpression
	//     ...
	//     5. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is *true* and _isProtoSetter_ is *false*, then
	//        a. Let _popValue_ be ? NamedEvaluation of |AssignmentExpression| with argument _propKey_.
	//     ...

	if (isNamedEvaluationAnd(emitContext(), node->asNode(), isAnonymousClassNeedingAssignedName)) {
		node = transformNamedEvaluation(emitContext(), node->asNode(), /*ignoreEmptyStringLiteral*/ false, /*assignedName*/ "")->as<PropertyAssignment>();
	}
	return visitor()->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::visitVariableStatement(VariableStatement* node) {
	std::vector<Node*> savedPendingStatements = pendingStatements;
	pendingStatements = {};

	Node* visitedNode = visitor()->visitEachChild(node->asNode());

	if (!pendingStatements.empty()) {
		std::vector<Node*> result;
		result.reserve(1 + pendingStatements.size());
		result.push_back(visitedNode);
		result.insert(result.end(), pendingStatements.begin(), pendingStatements.end());
		pendingStatements = savedPendingStatements;
		return factory()->newSyntaxList(result);
	}

	pendingStatements = savedPendingStatements;
	return visitedNode;
}

Node* classFieldsTransformer::visitVariableDeclaration(VariableDeclaration* node) {
	// 14.3.1.2 RS: Evaluation
	//   LexicalBinding : BindingIdentifier Initializer
	//     ...
	//     3. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
	//        a. Let _value_ be ? NamedEvaluation of |Initializer| with argument _bindingId_.
	//     ...
	//
	// 14.3.2.1 RS: Evaluation
	//   VariableDeclaration : BindingIdentifier Initializer
	//     ...
	//     3. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
	//        a. Let _value_ be ? NamedEvaluation of |Initializer| with argument _bindingId_.
	//     ...

	if (isNamedEvaluationAnd(emitContext(), node->asNode(), isAnonymousClassNeedingAssignedName)) {
		node = transformNamedEvaluation(emitContext(), node->asNode(), false, "")->as<VariableDeclaration>();
	}
	return visitor()->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::visitParameterDeclaration(ParameterDeclaration* node) {
	// 8.6.3 RS: IteratorBindingInitialization
	//   SingleNameBinding : BindingIdentifier Initializer?
	//     ...
	//     5. If |Initializer| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument _bindingId_.
	//     ...
	//
	// 14.3.3.3 RS: KeyedBindingInitialization
	//   SingleNameBinding : BindingIdentifier Initializer?
	//     ...
	//     4. If |Initializer| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument _bindingId_.
	//     ...

	if (isNamedEvaluationAnd(emitContext(), node->asNode(), isAnonymousClassNeedingAssignedName)) {
		node = transformNamedEvaluation(emitContext(), node->asNode(), false, "")->as<ParameterDeclaration>();
	}
	return visitor()->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::visitBindingElement(BindingElement* node) {
	// 8.6.3 RS: IteratorBindingInitialization
	//   SingleNameBinding : BindingIdentifier Initializer?
	//     ...
	//     5. If |Initializer| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument _bindingId_.
	//     ...
	//
	// 14.3.3.3 RS: KeyedBindingInitialization
	//   SingleNameBinding : BindingIdentifier Initializer?
	//     ...
	//     4. If |Initializer| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument _bindingId_.
	//     ...

	if (isNamedEvaluationAnd(emitContext(), node->asNode(), isAnonymousClassNeedingAssignedName)) {
		node = transformNamedEvaluation(emitContext(), node->asNode(), false, "")->as<BindingElement>();
	}
	return visitor()->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::visitExportAssignment(ExportAssignment* node) {
	// 16.2.3.7 RS: Evaluation
	//   ExportDeclaration : `export` `default` AssignmentExpression `;`
	//     1. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is *true*, then
	//        a. Let _value_ be ? NamedEvaluation of |AssignmentExpression| with argument `"default"`.
	//     ...

	// NOTE: Since emit for `export =` translates to `module.exports = ...`, the assigned name of the class
	// is `""`.

	if (isNamedEvaluationAnd(emitContext(), node->asNode(), isAnonymousClassNeedingAssignedName)) {
		std::string assignedName;
		if (!node->IsExportEquals) {
			assignedName = "default";
		}
		node = transformNamedEvaluation(emitContext(), node->asNode(), /*ignoreEmptyStringLiteral*/ true, assignedName)->as<ExportAssignment>();
	}
	return visitor()->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::injectPendingExpressions(Node* expression) {
	if (!pendingExpressions.empty()) {
		if (isParenthesizedExpression(expression)) {
			pendingExpressions.push_back(expression->expression());
			expression = factory()->updateParenthesizedExpression(
				expression->as<ParenthesizedExpression>(),
				factory()->inlineExpressions(pendingExpressions)
			);
		} else {
			std::vector<Node*> exprs = pendingExpressions;
			exprs.push_back(expression);
			expression = factory()->inlineExpressions(exprs);
		}
		pendingExpressions = {};
	}
	return expression;
}

Node* classFieldsTransformer::visitComputedPropertyName(ComputedPropertyName* node) {
	// Computed property names are evaluated in the enclosing scope, not the current class.
	// Replaces Strada's onEmitNode for ComputedPropertyName which switches to
	// lexicalEnvironment?.previous. We do this explicitly during transformation.
	classLexicalEnv* savedLexicalEnvironment = lexicalEnvironment;
	bool savedInsideComputedPropertyName = insideComputedPropertyName;
	insideComputedPropertyName = true;
	if (lexicalEnvironment != nullptr && lexicalEnvironment->previous != nullptr) {
		lexicalEnvironment = lexicalEnvironment->previous;
	}
	Node* expression = visitor()->visitNode(node->Expression);
	lexicalEnvironment = savedLexicalEnvironment;
	insideComputedPropertyName = savedInsideComputedPropertyName;
	return factory()->updateComputedPropertyName(node, injectPendingExpressions(expression));
}

Node* classFieldsTransformer::visitConstructorDeclaration(Node* node) {
	if (currentClassContainer != nullptr) {
		return transformConstructor(node->as<ConstructorDeclaration>(), currentClassContainer);
	}
	return visitor()->visitEachChild(node);
}

bool classFieldsTransformer::shouldTransformClassElementToWeakMap(Node* node) {
	if (shouldTransformPrivateElementsOrClassStaticBlocks) {
		return true;
	}
	return shouldAlwaysTransformPrivateStaticElements(node);
}

bool classFieldsTransformer::shouldAlwaysTransformPrivateStaticElements(Node* node) {
	return hasStaticModifier(node) &&
		(emitContext()->emitFlags(node) & printer::EFTransformPrivateStaticElements) != 0;
}

// nodeHasTransformPrivateStaticElementsFlag checks the emit flag on a class node (not a member).
// Unlike shouldAlwaysTransformPrivateStaticElements, this does not check HasStaticModifier,
// since class nodes themselves don't have a static modifier.
bool classFieldsTransformer::nodeHasTransformPrivateStaticElementsFlag(Node* node) {
	return (emitContext()->emitFlags(node) & printer::EFTransformPrivateStaticElements) != 0;
}

Node* classFieldsTransformer::visitMethodOrAccessorDeclaration(Node* node) {
	debugAssert(!hasDecorators(node));

	if (!isPrivateIdentifierClassElementDeclaration(node) || !shouldTransformClassElementToWeakMap(node)) {
		return classElementVisitor->visitEachChild(node);
	}

	// leave invalid code untransformed
	privateIdentifierInfo* info =
	    getPrivateIdentifier(getPrivateIdentifierEnvironment(), node->name());
	debugAssert(info != nullptr, "Undeclared private name for property declaration.");
	if (info->kind == printer::PrivateIdentifierKind::Untransformed ||
	    !info->isValid) {
		return node;
	}

	Node* functionName = getHoistedFunctionName(node);
	if (functionName != nullptr) {
		ModifierList* modifiers = extractNonStaticNonAccessorModifiers(node);
		emitContext()->startVariableEnvironment();
		bool saved = inIterationStatement;
		inIterationStatement = false;
		Node* body = emitContext()->visitFunctionBody(node->body(), visitor());
		NodeList* params = visitor()->visitNodes(node->parameterList());
		inIterationStatement = saved;

		Node* funcExpr = factory()->newFunctionExpression(modifiers, *node->bodyData().asteriskToken, functionName, nullptr, params, nullptr, nullptr, body);
		Node* assignment = factory()->newAssignmentExpression(functionName, funcExpr);
		addPendingExpressions(assignment);
	}

	// remove method declaration from class
	return nullptr;
}

ModifierList* classFieldsTransformer::extractNonStaticNonAccessorModifiers(Node* node) {
	return extractModifiers(emitContext(), node->modifiers(), ~(ModifierFlagsStatic | ModifierFlagsAccessor));
}

Node* classFieldsTransformer::setCurrentClassElementAnd(Node* classElement, TxVisitor visitor, Node* node) {
	if (classElement != currentClassElement) {
		Node* saved = currentClassElement;
		currentClassElement = classElement;
		Node* result = (this->*visitor)(node);
		currentClassElement = saved;
		return result;
	}
	return (this->*visitor)(node);
}

// visitEachChildOfNode just calls Visitor.VisitEachChild, but is necessary to avoid repeated closure allocations when passing as a callback.
Node* classFieldsTransformer::visitEachChildOfNode(Node* node) {
	return this->visitor()->visitEachChild(node);
}

Node* classFieldsTransformer::setInIterationStatementAnd(bool inIteration, TxVisitor visitor, Node* node) {
	if (inIterationStatement != inIteration) {
		bool saved = inIterationStatement;
		inIterationStatement = inIteration;
		Node* result = (this->*visitor)(node);
		inIterationStatement = saved;
		return result;
	}
	return (this->*visitor)(node);
}

Node* classFieldsTransformer::clearClassElementAndVisitEachChild(Node* node) {
	return setCurrentClassElementAnd(nullptr, &classFieldsTransformer::visitEachChildOfNode, node);
}

// visitFunctionExpressionOrDeclaration handles lexical environment scoping for function
// expressions and declarations, mirroring Strada's onEmitNode behavior.
//
// In Strada, onEmitNode checks whether a FunctionExpression has been registered in
// lexicalEnvironmentMap (via its original node). If found, the lexical environment is
// restored; otherwise it is cleared (since regular functions create a new `this` scope).
//
// Since Corsa performs substitution eagerly (no emit-time hooks), we replicate this by
// preserving currentClassElement for function expressions whose original node is a class
// member of the current class. This allows visitThisExpression to correctly substitute
// `this` -> `_classThis` inside synthesized functions (e.g., ES decorator descriptor
// methods for static private auto-accessors).
Node* classFieldsTransformer::visitFunctionExpressionOrDeclaration(Node* node) {
	if (currentClassElement != nullptr) {
		Node* original = emitContext()->mostOriginal(node);
		if (original != node && currentClassContainer != nullptr) {
			for (Node* member : currentClassContainer->members()) {
				if (emitContext()->mostOriginal(member) == original && isStatic(member)) {
					// The function expression originates from a static class member (e.g., a
					// descriptor method synthesized by the ES decorator transformer for a
					// static private auto-accessor). Preserve the current class element so
					// that visitThisExpression can substitute `this` with `_classThis`.
					// Non-static members must NOT preserve the class element because `this`
					// inside their descriptor functions should remain dynamic.
					return visitEachChildOfNode(node);
				}
			}
		}
	}
	return setCurrentClassElementAnd(nullptr, &classFieldsTransformer::visitEachChildOfNode, node);
}

Node* classFieldsTransformer::setClassElementAndVisitEachChild(Node* node) {
	return setCurrentClassElementAnd(node, &classFieldsTransformer::visitEachChildOfNode, node);
}

Node* classFieldsTransformer::getHoistedFunctionName(Node* node) {
	debugAssert(node->name() != nullptr && isPrivateIdentifier(node->name()));
	privateIdentifierInfo* info = accessPrivateIdentifier(node->name());
	debugAssert(info != nullptr, "Undeclared private name for property declaration.");
	if (info->kind == printer::PrivateIdentifierKind::Method) {
		return info->methodName;
	}
	if (info->kind == printer::PrivateIdentifierKind::Accessor) {
		if (isGetAccessorDeclaration(node)) {
			return info->getterName;
		}
		if (isSetAccessorDeclaration(node)) {
			return info->setterName;
		}
	}
	return nullptr;
}

Node* classFieldsTransformer::tryGetClassThis() {
	if (Node* classThis = tryGetClassThisNoContainer(); classThis != nullptr) {
		return classThis;
	}
	if (currentClassContainer != nullptr) {
		return currentClassContainer->name();
	}
	return nullptr;
}

Node* classFieldsTransformer::tryGetClassThisNoContainer() {
	classLexicalEnvironment* lex = getClassLexicalEnvironment();
	if (lex->classThis != nullptr) {
		return lex->classThis;
	}
	if (lex->classConstructor != nullptr) {
		return lex->classConstructor;
	}
	return nullptr;
}

// transformAutoAccessor transforms an auto-accessor property:
//
//	accessor x = 1;
//
// into:
//
//	#x = 1;
//	get x() { return this.#x; }
//	set x(value) { this.#x = value; }
Node* classFieldsTransformer::transformAutoAccessor(PropertyDeclaration* node) {
	TextRange commentRange = emitContext()->commentRange(node->asNode());
	TextRange sourceMapRange = emitContext()->sourceMapRange(node->asNode());

	// Since we're creating two declarations where there was previously one, cache
	// the expression for any computed property names.
	Node* name = node->name;
	Node* getterName = name;
	Node* setterName = name;
	if (isComputedPropertyName(name) && !isSimpleInlineableExpression(name->expression())) {
		BinaryExpression* cacheAssignment = findComputedPropertyNameCacheAssignment(emitContext(), name);
		if (cacheAssignment != nullptr) {
			getterName = factory()->updateComputedPropertyName(name->as<ComputedPropertyName>(), visitor()->visitNode(name->expression()));
			setterName = factory()->updateComputedPropertyName(name->as<ComputedPropertyName>(), cacheAssignment->Left);
		} else {
			Node* temp = factory()->newTempVariable();
			emitContext()->setSourceMapRange(temp, name->expression()->loc);
			emitContext()->addVariableDeclaration(temp);
			Node* expression = visitor()->visitNode(name->expression());
			Node* assignment = factory()->newAssignmentExpression(temp, expression);
			emitContext()->setSourceMapRange(assignment, name->expression()->loc);
			getterName = factory()->updateComputedPropertyName(name->as<ComputedPropertyName>(), assignment);
			setterName = factory()->updateComputedPropertyName(name->as<ComputedPropertyName>(), temp);
		}
	}

	ModifierList* modifiers = modifierVisitor->visitModifiers(node->modifiers);
	Node* backingField = createAccessorPropertyBackingField(factory(), node->asNode(), modifiers, node->Initializer);
	emitContext()->setOriginal(backingField, node->asNode());
	emitContext()->addEmitFlags(backingField, printer::EFNoComments);
	emitContext()->setSourceMapRange(backingField, sourceMapRange);

	Node* receiver;
	if (isStatic(node->asNode())) {
		receiver = tryGetClassThis();
		if (receiver == nullptr) {
			receiver = factory()->newThisExpression();
		}
	} else {
		receiver = factory()->newThisExpression();
	}

	Node* getter = createAccessorPropertyGetRedirector(node, modifiers, getterName, receiver);
	emitContext()->setOriginal(getter, node->asNode());
	emitContext()->setCommentRange(getter, commentRange);
	emitContext()->setSourceMapRange(getter, sourceMapRange);

	// create a fresh copy of the modifiers so that we don't duplicate comments
	ModifierList* setterModifiers = nullptr;
	if (modifiers != nullptr) {
		setterModifiers = factory()->newModifierList(
			createModifiersFromModifierFlags(modifiers->ModifierFlags,
				[this](Kind kind) { return factory()->newToken(kind); }));
	}
	Node* setter = createAccessorPropertySetRedirector(node, setterModifiers, setterName, receiver);
	emitContext()->setOriginal(setter, node->asNode());
	emitContext()->addEmitFlags(setter, printer::EFNoComments);
	emitContext()->setSourceMapRange(setter, sourceMapRange);

	// Visit the results in a second pass
	auto visited = accessorFieldResultVisitor->visitSlice({backingField, getter, setter});
	return factory()->newSyntaxList(visited.first);
}

Node* classFieldsTransformer::transformPrivateFieldInitializer(PropertyDeclaration* node) {
	if (shouldTransformClassElementToWeakMap(node->asNode())) {
		// If we are transforming private elements into WeakMap/WeakSet, we should elide the node.
		privateIdentifierInfo* info = getPrivateIdentifier(
		    getPrivateIdentifierEnvironment(), node->name);
		debugAssert(info != nullptr, "Undeclared private name for property declaration.");

		// Leave invalid code untransformed
		if (info->kind == printer::PrivateIdentifierKind::Untransformed ||
		    !info->isValid) {
			return node->asNode();
		}

		// If we encounter a valid private static field and we're not transforming
		// class static blocks, convert to a static block initializer.
		if (info->isStatic && !shouldTransformPrivateElementsOrClassStaticBlocks) {
			// TODO: fix
			Node* statement = transformPropertyOrClassStaticBlock(node->asNode(), factory()->newThisExpression());
			if (statement != nullptr) {
				return factory()->newClassStaticBlockDeclaration(
					nullptr, /*modifiers*/
					factory()->newBlock(factory()->newNodeList({statement}), /*multiLine*/ true)
				);
			}
		}

		return nullptr;
	}

	if (shouldTransformInitializersUsingSet && !hasStaticModifier(node->asNode()) &&
		lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr &&
		(lexicalEnvironment->data->facts & classFactsWillHoistInitializersToConstructor) != 0) {
		return factory()->updatePropertyDeclaration(
			node,
			visitor()->visitModifiers(node->modifiers),
			node->name,
			nullptr, /*postfixToken*/
			nullptr, /*typeNode*/
			nullptr /*initializer*/
		);
	}

	if (isNamedEvaluationAnd(emitContext(), node->asNode(), isAnonymousClassNeedingAssignedName)) {
		node = transformNamedEvaluation(emitContext(), node->asNode(), false, "")->as<PropertyDeclaration>();
	}

	return factory()->updatePropertyDeclaration(
		node,
		modifierVisitor->visitModifiers(node->modifiers),
		visitPropertyName(node->name),
		nullptr, /*postfixToken*/
		nullptr, /*typeNode*/
		visitor()->visitNode(node->Initializer)
	);
}

Node* classFieldsTransformer::transformPublicFieldInitializer(PropertyDeclaration* node) {
	if (shouldTransformInitializers && !isAutoAccessorPropertyDeclaration(node->asNode())) {
		// Elide the property declaration; the initializer will be moved to the constructor.
		// For computed property names, we still need to emit the expression.
		Node* expr = getPropertyNameExpressionIfNeeded(node->name, node->Initializer != nullptr || compilerOptions->GetUseDefineForClassFields());
		if (expr != nullptr) {
			flattenCommaList(expr, [this](Node* e) {
				addPendingExpressions(e);
				return true;
			});
		}

		// When target >= ES2022 (i.e., !shouldTransformPrivateElementsOrClassStaticBlocks) and we
		// still need to transform initializers (useDefineForClassFields: false), static property
		// initializers must be converted into `static { this.x = ...; }` blocks so that `this`
		// refers to the class constructor inside the static block.
		if (isStatic(node->asNode()) && !shouldTransformPrivateElementsOrClassStaticBlocks) {
			Node* initializerStatement = transformPropertyOrClassStaticBlock(node->asNode(), factory()->newThisExpression());
			if (initializerStatement != nullptr) {
				Node* staticBlock = factory()->newClassStaticBlockDeclaration(
					nullptr, /*modifiers*/
					factory()->newBlock(factory()->newNodeList({initializerStatement}), false)
				);

				emitContext()->setOriginal(staticBlock, node->asNode());
				emitContext()->setCommentRange(staticBlock, node->loc);

				emitContext()->addEmitFlags(initializerStatement, printer::EFNoComments);
				return staticBlock;
			}
		}

		return nullptr;
	}

	return factory()->updatePropertyDeclaration(
		node,
		modifierVisitor->visitModifiers(node->modifiers),
		visitPropertyName(node->name),
		nullptr, /*postfixToken*/
		nullptr, /*typeNode*/
		visitor()->visitNode(node->Initializer)
	);
}

Node* classFieldsTransformer::transformFieldInitializer(PropertyDeclaration* node) {
	debugAssert(!hasDecorators(node->asNode()), "Decorators should already have been transformed and elided.");
	if (isPrivateIdentifierClassElementDeclaration(node->asNode())) {
		return transformPrivateFieldInitializer(node);
	}
	return transformPublicFieldInitializer(node);
}

bool classFieldsTransformer::shouldTransformAutoAccessorsInCurrentClass() {
	if (shouldTransformAutoAccessors) {
		return true;
	}
	// When targeting ESNext with useDefineForClassFields: false, auto-accessors are only
	// transformed if the current class will hoist initializers to the constructor.
	return lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr &&
		(lexicalEnvironment->data->facts & classFactsWillHoistInitializersToConstructor) != 0;
}

Node* classFieldsTransformer::visitPropertyDeclaration(Node* node) {
	// If this is an auto-accessor, we defer to `transformAutoAccessor`. That function
	// will in turn call `transformFieldInitializer` as needed.
	PropertyDeclaration* propDecl = node->as<PropertyDeclaration>();
	if (isAutoAccessorPropertyDeclaration(node) && (shouldTransformAutoAccessorsInCurrentClass() ||
		(hasStaticModifier(node) && shouldAlwaysTransformPrivateStaticElements(node)))) {
		return transformAutoAccessor(propDecl);
	}
	return transformFieldInitializer(propDecl);
}

Node* classFieldsTransformer::createPrivateIdentifierAccess(privateIdentifierInfo* info, Node* receiver) {
	receiver = visitor()->visitNode(receiver);
	return createPrivateIdentifierAccessHelper(info, receiver);
}

Node* classFieldsTransformer::createPrivateIdentifierAccessHelper(privateIdentifierInfo* info, Node* receiver) {
	emitContext()->setCommentRange(receiver, TextRange{-1, receiver->end()});

	switch (info->kind) {
	case printer::PrivateIdentifierKind::Accessor:
		return factory()->newClassPrivateFieldGetHelper(
			receiver,
			info->brandCheckIdentifier,
			info->kind,
			info->getterName
		);
	case printer::PrivateIdentifierKind::Method:
		return factory()->newClassPrivateFieldGetHelper(
			receiver,
			info->brandCheckIdentifier,
			info->kind,
			info->methodName
		);
	case printer::PrivateIdentifierKind::Field: {
		Node* f = nullptr;
		if (info->isStatic) {
			f = info->variableName;
		}
		return factory()->newClassPrivateFieldGetHelper(
			receiver,
			info->brandCheckIdentifier,
			info->kind,
			f
		);
	}
	case printer::PrivateIdentifierKind::Untransformed:
		TSC_UNREACHABLE("Access helpers should not be created for untransformed private elements");
	}
	TSC_UNREACHABLE("Unknown private element type");
}

Node* classFieldsTransformer::visitPropertyAccessExpression(PropertyAccessExpression* node) {
	if (isPrivateIdentifier(node->name)) {
		privateIdentifierInfo* info = accessPrivateIdentifier(node->name);
		if (info != nullptr) {
			Node* result = createPrivateIdentifierAccess(info, node->Expression);
			emitContext()->setOriginal(result, node->asNode());
			result->loc = node->loc;
			return result;
		}
	}
	if (shouldTransformSuperInStaticInitializers && currentClassElement != nullptr &&
		isSuperProperty(node->asNode()) && isIdentifier(node->name) &&
		isStaticPropertyDeclarationOrClassStaticBlock(currentClassElement) &&
		lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr) {
		classLexicalEnvironment* data = lexicalEnvironment->data;
		if ((data->facts & classFactsClassWasDecorated) != 0) {
			return visitInvalidSuperProperty(node->asNode());
		}
		if (data->classConstructor != nullptr && data->superClassReference != nullptr) {
			// converts `super.x` into `Reflect.get(_baseTemp, "x", _classTemp)`
			Node* superProperty = factory()->newReflectGetCall(
				data->superClassReference,
				factory()->newStringLiteralFromNode(node->name),
				data->classConstructor
			);
			emitContext()->setOriginal(superProperty, node->Expression);
			superProperty->loc = node->Expression->loc;
			return superProperty;
		}
	}
	// Visit only the expression, not the name (when it's a regular identifier), to prevent
	// substitution of property names. Strada's onSubstituteNode only fires for
	// EmitHint.Expression, which excludes the .name of PropertyAccessExpression.
	// Private identifier names are still visited through VisitEachChild so they can be
	// transformed by visitPrivateIdentifier.
	if (isIdentifier(node->name)) {
		return visitPropertyAccessExpressionForSubstitution(node);
	}
	return visitor()->visitEachChild(node->asNode());
}

// visitPropertyAccessExpressionForSubstitution visits only the expression of a PropertyAccessExpression,
// leaving the name unchanged. This prevents the name from being treated as a standalone identifier
// reference and incorrectly substituted with a class alias.
Node* classFieldsTransformer::visitPropertyAccessExpressionForSubstitution(PropertyAccessExpression* node) {
	Node* expression = visitor()->visitNode(node->Expression);
	if (expression != node->Expression) {
		return factory()->updatePropertyAccessExpression(node, expression, node->QuestionDotToken, node->name, node->flags);
	}
	return node->asNode();
}

Node* classFieldsTransformer::visitElementAccessExpression(ElementAccessExpression* node) {
	if (shouldTransformSuperInStaticInitializers && currentClassElement != nullptr &&
		isSuperProperty(node->asNode()) &&
		isStaticPropertyDeclarationOrClassStaticBlock(currentClassElement) &&
		lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr) {
		classLexicalEnvironment* data = lexicalEnvironment->data;
		if ((data->facts & classFactsClassWasDecorated) != 0) {
			return visitInvalidSuperProperty(node->asNode());
		}
		if (data->classConstructor != nullptr && data->superClassReference != nullptr) {
			// converts `super[x]` into `Reflect.get(_baseTemp, x, _classTemp)`
			Node* superProperty = factory()->newReflectGetCall(
				data->superClassReference,
				visitor()->visitNode(node->ArgumentExpression),
				data->classConstructor
			);
			emitContext()->setOriginal(superProperty, node->Expression);
			superProperty->loc = node->Expression->loc;
			return superProperty;
		}
	}
	return visitor()->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::visitPreOrPostfixUnaryExpression(Node* node, bool discarded) {
	Kind operator_;
	Node* operand;
	if (isPrefixUnaryExpression(node)) {
		operator_ = node->as<PrefixUnaryExpression>()->Operator;
		operand = node->as<PrefixUnaryExpression>()->Operand;
	} else {
		operator_ = node->as<PostfixUnaryExpression>()->Operator;
		operand = node->as<PostfixUnaryExpression>()->Operand;
	}

	if (operator_ == Kind::PlusPlusToken || operator_ == Kind::MinusMinusToken) {
		Node* operandSkipped = skipParentheses(operand);

		// Private identifier property access
		if (isPropertyAccessExpression(operandSkipped) && isPrivateIdentifier(operandSkipped->name())) {
			privateIdentifierInfo* info = accessPrivateIdentifier(operandSkipped->name());
			if (info != nullptr) {
				Node* receiver = visitor()->visitNode(operandSkipped->expression());
				auto [readExpression, initializeExpression] = createCopiableReceiverExpr(receiver);

				Node* expression = createPrivateIdentifierAccessHelper(info, readExpression);
				Node* temp = nullptr;
				if (!isPrefixUnaryExpression(node) && !discarded) {
					temp = factory()->newTempVariable();
					emitContext()->addVariableDeclaration(temp);
				}
				expression = expandPreOrPostfixIncrementOrDecrementExpression(factory(), emitContext(), node, expression, temp);
				Node* assignReceiver = readExpression;
				if (initializeExpression != nullptr) {
					assignReceiver = initializeExpression;
				}
				expression = createPrivateIdentifierAssignment(info, assignReceiver, expression, Kind::EqualsToken);
				emitContext()->setOriginal(expression, node);
				expression->loc = node->loc;
				if (temp != nullptr) {
					expression = factory()->newCommaExpression(expression, temp);
					expression->loc = node->loc;
				}
				return expression;
			}
		} else if (shouldTransformSuperInStaticInitializers && currentClassElement != nullptr &&
			isSuperProperty(operandSkipped) &&
			isStaticPropertyDeclarationOrClassStaticBlock(currentClassElement) &&
			lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr) {
			// converts `++super.a` into `(Reflect.set(_baseTemp, "a", (_a = Reflect.get(_baseTemp, "a", _classTemp), _b = ++_a), _classTemp), _b)`
			// converts `++super[f()]` into `(Reflect.set(_baseTemp, _a = f(), (_b = Reflect.get(_baseTemp, _a, _classTemp), _c = ++_b), _classTemp), _c)`
			// converts `--super.a` into `(Reflect.set(_baseTemp, "a", (_a = Reflect.get(_baseTemp, "a", _classTemp), _b = --_a), _classTemp), _b)`
			// converts `--super[f()]` into `(Reflect.set(_baseTemp, _a = f(), (_b = Reflect.get(_baseTemp, _a, _classTemp), _c = --_b), _classTemp), _c)`
			// converts `super.a++` into `(Reflect.set(_baseTemp, "a", (_a = Reflect.get(_baseTemp, "a", _classTemp), _b = _a++), _classTemp), _b)`
			// converts `super[f()]++` into `(Reflect.set(_baseTemp, _a = f(), (_b = Reflect.get(_baseTemp, _a, _classTemp), _c = _b++), _classTemp), _c)`
			// converts `super.a--` into `(Reflect.set(_baseTemp, "a", (_a = Reflect.get(_baseTemp, "a", _classTemp), _b = _a--), _classTemp), _b)`
			// converts `super[f()]--` into `(Reflect.set(_baseTemp, _a = f(), (_b = Reflect.get(_baseTemp, _a, _classTemp), _c = _b--), _classTemp), _c)`
			classLexicalEnvironment* data = lexicalEnvironment->data;
			if ((data->facts & classFactsClassWasDecorated) != 0) {
				Node* visitedExpr = visitInvalidSuperProperty(operandSkipped);
				if (isPrefixUnaryExpression(node)) {
					return factory()->updatePrefixUnaryExpression(node->as<PrefixUnaryExpression>(), node->as<PrefixUnaryExpression>()->Operator, visitedExpr);
				}
				return factory()->updatePostfixUnaryExpression(node->as<PostfixUnaryExpression>(), visitedExpr, node->as<PostfixUnaryExpression>()->Operator);
			}
			if (data->classConstructor != nullptr && data->superClassReference != nullptr) {
				Node* setterName = nullptr;
				Node* getterName = nullptr;
				if (isPropertyAccessExpression(operandSkipped)) {
					if (isIdentifier(operandSkipped->name())) {
						getterName = factory()->newStringLiteralFromNode(operandSkipped->name());
						setterName = getterName;
					}
				} else if (isElementAccessExpression(operandSkipped)) {
					if (isSimpleInlineableExpression(operandSkipped->as<ElementAccessExpression>()->ArgumentExpression)) {
						getterName = operandSkipped->as<ElementAccessExpression>()->ArgumentExpression;
						setterName = getterName;
					} else {
						getterName = factory()->newTempVariable();
						emitContext()->addVariableDeclaration(getterName);
						setterName = factory()->newAssignmentExpression(getterName, visitor()->visitNode(operandSkipped->as<ElementAccessExpression>()->ArgumentExpression));
					}
				}
				if (setterName != nullptr && getterName != nullptr) {
					Node* expression = factory()->newReflectGetCall(data->superClassReference, getterName, data->classConstructor);
					expression->loc = operandSkipped->loc;

					Node* temp = nullptr;
					if (!discarded) {
						temp = factory()->newTempVariable();
						emitContext()->addVariableDeclaration(temp);
					}
					expression = expandPreOrPostfixIncrementOrDecrementExpression(factory(), emitContext(), node, expression, temp);
					expression = factory()->newReflectSetCall(data->superClassReference, setterName, expression, data->classConstructor);
					emitContext()->setOriginal(expression, node);
					expression->loc = node->loc;
					if (temp != nullptr) {
						expression = factory()->newCommaExpression(expression, temp);
						expression->loc = node->loc;
					}
					return expression;
				}
			}
		}
	}
	return visitor()->visitEachChild(node);
}

Node* classFieldsTransformer::visitForStatement(ForStatement* node) {
	Node* initializer = discardedValueVisitor->visitNode(node->Initializer);
	Node* condition = visitor()->visitNode(node->Condition);
	Node* incrementor = discardedValueVisitor->visitNode(node->Incrementor);
	bool saved = inIterationStatement;
	inIterationStatement = true;
	Node* body = emitContext()->visitIterationBody(node->Statement, visitor());
	inIterationStatement = saved;
	return factory()->updateForStatement(node, initializer, condition, incrementor, body);
}

Node* classFieldsTransformer::visitExpressionStatement(ExpressionStatement* node) {
	// Preserve private identifiers that appear directly as the expression of an
	// ExpressionStatement (e.g., `#;`). This is error-recovery output from the parser
	// for invalid syntax. Keeping it ensures the runtime throws a SyntaxError rather
	// than silently succeeding with an empty statement.
	if (isPrivateIdentifier(node->Expression) && shouldTransformPrivateElementsOrClassStaticBlocks) {
		return node->asNode();
	}
	return factory()->updateExpressionStatement(
		node,
		discardedValueVisitor->visitNode(node->Expression)
	);
}

std::pair<Node*, Node*> classFieldsTransformer::createCopiableReceiverExpr(Node* receiver) {
	Node* clone = receiver;
	if (!nodeIsSynthesized(receiver)) {
		clone = receiver->clone(*factory());
	}
	if (isSimpleInlineableExpression(receiver)) {
		return {clone, nullptr};
	}
	Node* readExpression = factory()->newTempVariable();
	emitContext()->addVariableDeclaration(readExpression);
	Node* initializeExpression = factory()->newAssignmentExpression(readExpression, clone);
	return {readExpression, initializeExpression};
}

Node* classFieldsTransformer::visitCallExpression(CallExpression* node) {
	if (isPropertyAccessExpression(node->Expression) &&
		isPrivateIdentifier(node->Expression->as<PropertyAccessExpression>()->name) &&
		accessPrivateIdentifier(node->Expression->as<PropertyAccessExpression>()->name) != nullptr) {
		// obj.#x()

		// Transform call expressions of private names to properly bind the `this` parameter.
		auto [thisArg, target] = createCallBinding(node->Expression);
		Node* visitedTarget = visitor()->visitNode(target);
		Node* visitedThisArg = visitor()->visitNode(thisArg);
		NodeList* visitedArgs = visitor()->visitNodes(node->Arguments);
		std::vector<Node*> allArgs;
		allArgs.reserve(1 + visitedArgs->nodes.size());
		allArgs.push_back(visitedThisArg);
		allArgs.insert(allArgs.end(), visitedArgs->nodes.begin(), visitedArgs->nodes.end());
		if ((node->flags & NodeFlagsOptionalChain) != 0) {
			return factory()->updateCallExpression(
				node,
				factory()->newPropertyAccessExpression(visitedTarget, node->QuestionDotToken, factory()->newIdentifier("call"), NodeFlagsOptionalChain),
				nullptr, /*questionDotToken*/
				nullptr, /*typeArguments*/
				factory()->newNodeList(allArgs),
				node->flags
			);
		}
		return factory()->updateCallExpression(
			node,
			factory()->newPropertyAccessExpression(visitedTarget, nullptr, factory()->newIdentifier("call"), NodeFlagsNone),
			nullptr, /*questionDotToken*/
			nullptr, /*typeArguments*/
			factory()->newNodeList(allArgs),
			node->flags
		);
	}

	if (shouldTransformSuperInStaticInitializers && currentClassElement != nullptr &&
		isSuperProperty(node->Expression) &&
		isStaticPropertyDeclarationOrClassStaticBlock(currentClassElement) &&
		lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr &&
		lexicalEnvironment->data->classConstructor != nullptr) {
		// super.x()
		// super[x]()

		// converts `super.f(...)` into `Reflect.get(_baseTemp, "f", _classTemp).call(_classTemp, ...)`
		Node* invocation = factory()->newFunctionCallCall(
			visitor()->visitNode(node->Expression),
			lexicalEnvironment->data->classConstructor,
			visitor()->visitNodes(node->Arguments)->nodes
		);
		emitContext()->setOriginal(invocation, node->asNode());
		invocation->loc = node->loc;
		return invocation;
	}

	return visitor()->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::visitTaggedTemplateExpression(TaggedTemplateExpression* node) {
	if (isPropertyAccessExpression(node->Tag) &&
		isPrivateIdentifier(node->Tag->as<PropertyAccessExpression>()->name) &&
		accessPrivateIdentifier(node->Tag->as<PropertyAccessExpression>()->name) != nullptr) {
		// Bind the `this` correctly for tagged template literals when the tag is a private identifier property access.
		auto [thisArg, target] = createCallBinding(node->Tag);
		Node* bindExpr = factory()->newCallExpression(
			factory()->newPropertyAccessExpression(visitor()->visitNode(target), nullptr, factory()->newIdentifier("bind"), NodeFlagsNone),
			nullptr, /*questionDotToken*/
			nullptr, /*typeArguments*/
			factory()->newNodeList({visitor()->visitNode(thisArg)}),
			NodeFlagsNone
		);
		return factory()->updateTaggedTemplateExpression(
			node,
			bindExpr,
			nullptr, /*questionDotToken*/
			nullptr, /*typeArguments*/
			visitor()->visitNode(node->Template),
			node->flags
		);
	}

	if (shouldTransformSuperInStaticInitializers && currentClassElement != nullptr &&
		isSuperProperty(node->Tag) &&
		isStaticPropertyDeclarationOrClassStaticBlock(currentClassElement) &&
		lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr &&
		lexicalEnvironment->data->classConstructor != nullptr) {
		// converts `` super.f`x` `` into `` Reflect.get(_baseTemp, "f", _classTemp).bind(_classTemp)`x` ``
		Node* invocation = factory()->newFunctionBindCall(
			visitor()->visitNode(node->Tag),
			lexicalEnvironment->data->classConstructor,
			{}
		);
		emitContext()->setOriginal(invocation, node->asNode());
		invocation->loc = node->loc;
		return factory()->updateTaggedTemplateExpression(
			node,
			invocation,
			nullptr, /*questionDotToken*/
			nullptr, /*typeArguments*/
			visitor()->visitNode(node->Template),
			node->flags
		);
	}

	return visitor()->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::transformClassStaticBlockDeclaration(Node* node) {
	if (shouldTransformPrivateElementsOrClassStaticBlocks) {
		if (isClassThisAssignmentBlock(emitContext(), node)) {
			Node* result = visitor()->visitNode(node->as<ClassStaticBlockDeclaration>()->Body->as<Block>()->Statements->nodes[0]->expression());
			// If the generated `_classThis` assignment is a noop (i.e., `_classThis = _classThis`), we can
			// eliminate the expression
			if (isAssignmentExpression(result, /*excludeCompoundAssignment*/ true)) {
				BinaryExpression* binary = result->as<BinaryExpression>();
				if (binary->Left == binary->Right) {
					return nullptr;
				}
			}
			return result;
		}

		if (isClassNamedEvaluationHelperBlock(emitContext(), node)) {
			return visitor()->visitNode(node->as<ClassStaticBlockDeclaration>()->Body->as<Block>()->Statements->nodes[0]->expression());
		}

		emitContext()->startVariableEnvironment();
		std::vector<Node*> statements = setCurrentClassElementAndVisitStatements(node, node->as<ClassStaticBlockDeclaration>()->Body->as<Block>()->Statements->nodes);
		statements = emitContext()->endAndMergeVariableEnvironment(statements);

		Node* iife = factory()->newImmediatelyInvokedArrowFunction(statements);
		Node* arrowFunction = skipParentheses(iife->expression());
		emitContext()->setOriginal(arrowFunction, node);
		emitContext()->addEmitFlags(arrowFunction, printer::EFNoLexicalArguments);
		// Preserve the statement list source range so the printer can emit detached comments
		// (e.g., `// do` inside an otherwise empty static block)
		arrowFunction->as<ArrowFunction>()->Body->as<Block>()->Statements->loc =
			node->as<ClassStaticBlockDeclaration>()->Body->as<Block>()->Statements->loc;
		emitContext()->setOriginal(iife, node);
		emitContext()->assignSourceMapRange(iife, node);
		emitContext()->addEmitFlags(arrowFunction, printer::EFNoLexicalThis);
		return iife;
	}
	return nullptr;
}

std::vector<Node*> classFieldsTransformer::setCurrentClassElementAndVisitStatements(Node* classElement, std::vector<Node*> statements) {
	Node* savedCurrentClassElement = currentClassElement;
	currentClassElement = classElement;
	auto result = visitor()->visitSlice(statements);
	currentClassElement = savedCurrentClassElement;
	return result.first;
}

bool classFieldsTransformer::isAnonymousClassNeedingAssignedNameWorker(anonymousFunctionDefinition* node) {
	if (isClassExpression(node) && node->name() == nullptr) {
		std::vector<Node*> staticPropertiesOrClassStaticBlocks = getStaticPropertiesAndClassStaticBlock(node);
		bool foundHelperBlock = false;
		for (Node* n : staticPropertiesOrClassStaticBlocks) {
			if (isClassNamedEvaluationHelperBlock(emitContext(), n)) {
				foundHelperBlock = true;
				break;
			}
		}
		if (foundHelperBlock) {
			return false;
		}
		bool hasTransformableStatics = (shouldTransformPrivateElementsOrClassStaticBlocks ||
			nodeHasTransformPrivateStaticElementsFlag(node)) && [this, &staticPropertiesOrClassStaticBlocks]() {
			for (Node* n : staticPropertiesOrClassStaticBlocks) {
				if (isClassStaticBlockDeclaration(n) ||
					isPrivateIdentifierClassElementDeclaration(n) ||
					(shouldTransformInitializers && isInitializedProperty(n))) {
					return true;
				}
			}
			return false;
		}();
		return hasTransformableStatics;
	}
	return false;
}

Node* classFieldsTransformer::visitBinaryExpression(BinaryExpression* node, bool discarded) {
	if (isDestructuringAssignment(node->asNode())) {
		// ({ x: obj.#x } = ...)
		// ({ x: super.x } = ...)
		// ({ x: super[x] } = ...)
		std::vector<Node*> savedPendingExpressions = pendingExpressions;
		pendingExpressions = {};
		Node* updated = factory()->updateBinaryExpression(
			node,
			nullptr,
			assignmentTargetVisitor->visitNode(node->Left),
			nullptr,
			node->OperatorToken,
			visitor()->visitNode(node->Right)
		);
		Node* result;
		if (!pendingExpressions.empty()) {
			std::vector<Node*> exprs = pendingExpressions;
			exprs.push_back(updated);
			result = factory()->inlineExpressions(exprs);
		} else {
			result = updated;
		}
		pendingExpressions = savedPendingExpressions;
		return result;
	}

	if (isAssignmentExpression(node->asNode(), /*excludeCompound*/ false)) {
		// 13.15.2 RS: Evaluation
		//   AssignmentExpression : LeftHandSideExpression `=` AssignmentExpression
		//     1. If |LeftHandSideExpression| is neither an |ObjectLiteral| nor an |ArrayLiteral|, then
		//        a. Let _lref_ be ? Evaluation of |LeftHandSideExpression|.
		//        b. If IsAnonymousFunctionDefinition(|AssignmentExpression|) and IsIdentifierRef of |LeftHandSideExpression| are both *true*, then
		//           i. Let _rval_ be ? NamedEvaluation of |AssignmentExpression| with argument _lref_.[[ReferencedName]].
		//     ...
		//
		//   AssignmentExpression : LeftHandSideExpression `&&=` AssignmentExpression
		//     ...
		//     5. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is *true* and IsIdentifierRef of |LeftHandSideExpression| is *true*, then
		//        a. Let _rval_ be ? NamedEvaluation of |AssignmentExpression| with argument _lref_.[[ReferencedName]].
		//     ...
		//
		//   AssignmentExpression : LeftHandSideExpression `||=` AssignmentExpression
		//     ...
		//     5. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is *true* and IsIdentifierRef of |LeftHandSideExpression| is *true*, then
		//        a. Let _rval_ be ? NamedEvaluation of |AssignmentExpression| with argument _lref_.[[ReferencedName]].
		//     ...
		//
		//   AssignmentExpression : LeftHandSideExpression `??=` AssignmentExpression
		//     ...
		//     4. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is *true* and IsIdentifierRef of |LeftHandSideExpression| is *true*, then
		//        a. Let _rval_ be ? NamedEvaluation of |AssignmentExpression| with argument _lref_.[[ReferencedName]].
		//     ...

		if (isNamedEvaluationAnd(emitContext(), node->asNode(), isAnonymousClassNeedingAssignedName)) {
			node = transformNamedEvaluation(emitContext(), node->asNode(), false, "")->as<BinaryExpression>();
			debugAssert(node->asNode() != nullptr && isAssignmentExpression(node->asNode(), false));
		}

		Node* left = skipOuterExpressions(node->Left, OEKPartiallyEmittedExpressions | OEKParentheses);
		if (isPropertyAccessExpression(left) && isPrivateIdentifier(left->name())) {
			// obj.#x = ...
			privateIdentifierInfo* info = accessPrivateIdentifier(left->name());
			if (info != nullptr) {
				Node* result = createPrivateIdentifierAssignment(info, left->expression(), node->Right, node->OperatorToken->kind);
				emitContext()->setOriginal(result, node->asNode());
				result->loc = node->loc;
				return result;
			}
		} else if (shouldTransformSuperInStaticInitializers && currentClassElement != nullptr &&
			isSuperProperty(node->Left) &&
			isStaticPropertyDeclarationOrClassStaticBlock(currentClassElement) &&
			lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr) {
			// super.x = ...
			// super[x] = ...
			// super.x += ...
			// super.x -= ...
			classLexicalEnvironment* data = lexicalEnvironment->data;
			if ((data->facts & classFactsClassWasDecorated) != 0) {
				return factory()->updateBinaryExpression(
					node,
					nullptr,
					visitInvalidSuperProperty(node->Left),
					nullptr,
					node->OperatorToken,
					visitor()->visitNode(node->Right)
				);
			}
			if (data->classConstructor != nullptr && data->superClassReference != nullptr) {
				Node* setterName = nullptr;
				if (isElementAccessExpression(node->Left)) {
					setterName = visitor()->visitNode(node->Left->as<ElementAccessExpression>()->ArgumentExpression);
				} else if (isPropertyAccessExpression(node->Left) &&
					isIdentifier(node->Left->as<PropertyAccessExpression>()->name)) {
					setterName = factory()->newStringLiteralFromNode(node->Left->as<PropertyAccessExpression>()->name);
				}
				if (setterName != nullptr) {
					// converts `super.x = 1` into `(Reflect.set(_baseTemp, "x", _a = 1, _classTemp), _a)`
					// converts `super[f()] = 1` into `(Reflect.set(_baseTemp, f(), _a = 1, _classTemp), _a)`
					// converts `super.x += 1` into `(Reflect.set(_baseTemp, "x", _a = Reflect.get(_baseTemp, "x", _classtemp) + 1, _classTemp), _a)`
					// converts `super[f()] += 1` into `(Reflect.set(_baseTemp, _a = f(), _b = Reflect.get(_baseTemp, _a, _classtemp) + 1, _classTemp), _b)`

					Node* expression = visitor()->visitNode(node->Right);
					if (isCompoundAssignment(node->OperatorToken->kind)) {
						Node* getterName = setterName;
						if (!isSimpleInlineableExpression(setterName)) {
							getterName = factory()->newTempVariable();
							emitContext()->addVariableDeclaration(getterName);
							setterName = factory()->newAssignmentExpression(getterName, setterName);
						}
						Node* superPropertyGet = factory()->newReflectGetCall(
							data->superClassReference,
							getterName,
							data->classConstructor
						);
						emitContext()->setOriginal(superPropertyGet, node->Left);
						superPropertyGet->loc = node->Left->loc;
						expression = factory()->newBinaryExpression(
							nullptr,
							superPropertyGet,
							nullptr,
							factory()->newToken(getNonAssignmentOperatorForCompoundAssignment(node->OperatorToken->kind)),
							expression
						);
						expression->loc = node->loc;
					}

					Node* temp = nullptr;
					if (!discarded) {
						temp = factory()->newTempVariable();
						emitContext()->addVariableDeclaration(temp);
					}
					if (temp != nullptr) {
						expression = factory()->newAssignmentExpression(temp, expression);
						expression->loc = node->loc;
					}

					expression = factory()->newReflectSetCall(
						data->superClassReference,
						setterName,
						expression,
						data->classConstructor
					);
					emitContext()->setOriginal(expression, node->asNode());
					expression->loc = node->loc;

					if (temp != nullptr) {
						expression = factory()->newCommaExpression(expression, temp);
						expression->loc = node->loc;
					}
					return expression;
				}
			}
		}
	}

	if (node->OperatorToken->kind == Kind::InKeyword && isPrivateIdentifier(node->Left)) {
		// #x in obj
		return transformPrivateIdentifierInInExpression(node);
	}

	return visitor()->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::visitParenthesizedExpression(ParenthesizedExpression* node, bool discarded) {
	// 8.4.5 RS: NamedEvaluation
	//   ParenthesizedExpression : `(` Expression `)`
	//     ...
	//     2. Return ? NamedEvaluation of |Expression| with argument _name_.
	if (discarded) {
		Node* expression = discardedValueVisitor->visitNode(node->Expression);
		return factory()->updateParenthesizedExpression(node, expression);
	}
	Node* expression = visitor()->visitNode(node->Expression);
	return factory()->updateParenthesizedExpression(node, expression);
}

Node* classFieldsTransformer::createPrivateIdentifierAssignment(privateIdentifierInfo* info, Node* receiver, Node* right, Kind op) {
	receiver = visitor()->visitNode(receiver);
	right = visitor()->visitNode(right);

	if (isCompoundAssignment(op)) {
		auto [readExpression, initializeExpression] = createCopiableReceiverExpr(receiver);
		if (initializeExpression != nullptr) {
			receiver = initializeExpression;
		} else {
			receiver = readExpression;
		}
		right = factory()->newBinaryExpression(
			nullptr,
			createPrivateIdentifierAccessHelper(info, readExpression),
			nullptr,
			factory()->newToken(getNonAssignmentOperatorForCompoundAssignment(op)),
			right
		);
	}

	emitContext()->setCommentRange(receiver, TextRange{-1, receiver->end()});

	switch (info->kind) {
	case printer::PrivateIdentifierKind::Accessor:
		return factory()->newClassPrivateFieldSetHelper(
			receiver,
			info->brandCheckIdentifier,
			right,
			info->kind,
			info->setterName
		);
	case printer::PrivateIdentifierKind::Method:
		return factory()->newClassPrivateFieldSetHelper(
			receiver,
			info->brandCheckIdentifier,
			right,
			info->kind,
			nullptr
		);
	case printer::PrivateIdentifierKind::Field: {
		Node* f = nullptr;
		if (info->isStatic) {
			f = info->variableName;
		}
		return factory()->newClassPrivateFieldSetHelper(
			receiver,
			info->brandCheckIdentifier,
			right,
			info->kind,
			f
		);
	}
	case printer::PrivateIdentifierKind::Untransformed:
		TSC_UNREACHABLE("Access helpers should not be created for untransformed private elements");
	}
	TSC_UNREACHABLE("Unknown private element type");
}

std::vector<Node*> classFieldsTransformer::getPrivateInstanceMethodsAndAccessors(Node* node) {
	std::vector<Node*> result;
	for (Node* member : node->members()) {
		if (isNonStaticMethodOrAccessorWithPrivateName(member)) {
			result.push_back(member);
		}
	}
	return result;
}

// memberContainsConstructorReference checks if a class member's body contains an identifier
// that resolves to the class declaration. Replaces Strada's resolver.hasNodeCheckFlag(member,
// NodeCheckFlags.ContainsConstructorReference) by walking the AST with the EmitResolver.
// Only checks member bodies (not computed property names), since computed property names
// are evaluated during class definition when the binding is still correct.
bool classFieldsTransformer::memberContainsConstructorReference(Node* member, Node* classDecl) {
	Node* classOriginal = emitContext()->mostOriginal(classDecl);
	Node* className = getNameOfDeclaration(classDecl);
	std::function<bool(Node*)> check;
	check = [&](Node* n) -> bool {
		if (isIdentifier(n) && n != className) {
			Node* decl = resolver->GetReferencedValueDeclaration(n);
			if (decl == classOriginal) {
				return true;
			}
		}
		// For PropertyAccessExpression, only check the expression, not the name.
		// The .Name() is a property access name, not a value reference to the class.
		if (isPropertyAccessExpression(n)) {
			return check(n->expression());
		}
		return n->forEachChild(check);
	};
	// Check only the body/initializer of the member, not the name (which may be
	// a computed property name that shouldn't trigger alias substitution).
	if (isClassStaticBlockDeclaration(member)) {
		Node* body = member->as<ClassStaticBlockDeclaration>()->Body;
		if (body != nullptr && check(body)) {
			return true;
		}
	} else {
		Node* body = member->body();
		if (body != nullptr && check(body)) {
			return true;
		}
	}
	if (isPropertyDeclaration(member)) {
		Node* init = member->initializer();
		if (init != nullptr && check(init)) {
			return true;
		}
	}
	return false;
}

// classContainsConstructorReference checks if any member of a class contains
// references to the class's own constructor. Replaces Strada's
// resolver.hasNodeCheckFlag(node, NodeCheckFlags.ContainsConstructorReference).
bool classFieldsTransformer::classContainsConstructorReference(Node* node) {
	for (Node* member : node->members()) {
		if (memberContainsConstructorReference(member, node)) {
			return true;
		}
	}
	return false;
}

classFacts classFieldsTransformer::getClassFacts(Node* node) {
	classFacts facts = classFactsNone;

	Node* original = emitContext()->mostOriginal(node);
	if (isClassLike(original) && classOrConstructorParameterIsDecorated(legacyDecorators /*useLegacyDecorators*/, original)) {
		facts |= classFactsClassWasDecorated;
	}

	if (shouldTransformPrivateElementsOrClassStaticBlocks &&
		(classHasClassThisAssignment(emitContext(), node) || classHasExplicitlyAssignedName(emitContext(), node))) {
		facts |= classFactsNeedsClassConstructorReference;
	}

	bool containsPublicInstanceFields = false;
	bool containsInitializedPublicInstanceFields = false;
	bool containsInstancePrivateElements = false;
	bool containsInstanceAutoAccessors = false;

	for (Node* member : node->members()) {
		if (isStatic(member)) {
			if (member->name() != nullptr && (isPrivateIdentifier(member->name()) || isAutoAccessorPropertyDeclaration(member)) &&
				shouldTransformPrivateElementsOrClassStaticBlocks) {
				facts |= classFactsNeedsClassConstructorReference;
			} else if (isAutoAccessorPropertyDeclaration(member) && shouldTransformAutoAccessors &&
				node->name() == nullptr && emitContext()->classThisOf(node) == nullptr) {
				facts |= classFactsNeedsClassConstructorReference;
			}
			if (isPropertyDeclaration(member) || isClassStaticBlockDeclaration(member)) {
				if (shouldTransformThisInStaticInitializers && (member->subtreeFacts() & SubtreeContainsLexicalThis) != 0) {
					facts |= classFactsNeedsSubstitutionForThisInClassStaticField;
					if ((facts & classFactsClassWasDecorated) == 0) {
						facts |= classFactsNeedsClassConstructorReference;
					}
				}
				if (shouldTransformSuperInStaticInitializers && (member->subtreeFacts() & SubtreeContainsLexicalSuper) != 0) {
					if ((facts & classFactsClassWasDecorated) == 0) {
						facts |= classFactsNeedsClassConstructorReference | classFactsNeedsClassSuperReference;
					}
				}
			}
		} else if (!hasAbstractModifier(emitContext()->mostOriginal(member))) {
			if (isAutoAccessorPropertyDeclaration(member)) {
				containsInstanceAutoAccessors = true;
				containsInstancePrivateElements = containsInstancePrivateElements || isPrivateIdentifierClassElementDeclaration(member);
			} else if (isPrivateIdentifierClassElementDeclaration(member)) {
				containsInstancePrivateElements = true;
				if (memberContainsConstructorReference(member, node)) {
					facts |= classFactsNeedsClassConstructorReference;
				}
			} else if (isPropertyDeclaration(member)) {
				containsPublicInstanceFields = true;
				containsInitializedPublicInstanceFields = containsInitializedPublicInstanceFields || member->initializer() != nullptr;
			}
		}
	}

	bool willHoistInitializersToConstructor = (shouldTransformInitializersUsingDefine && containsPublicInstanceFields) ||
		(shouldTransformInitializersUsingSet && containsInitializedPublicInstanceFields) ||
		(shouldTransformPrivateElementsOrClassStaticBlocks && containsInstancePrivateElements) ||
		(shouldTransformPrivateElementsOrClassStaticBlocks && containsInstanceAutoAccessors && shouldTransformAutoAccessors);

	if (willHoistInitializersToConstructor) {
		facts |= classFactsWillHoistInitializersToConstructor;
	}

	return facts;
}

Node* classFieldsTransformer::visitExpressionWithTypeArgumentsInHeritageClause(ExpressionWithTypeArguments* node) {
	classFacts facts = classFactsNone;
	if (lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr) {
		facts = lexicalEnvironment->data->facts;
	}
	if ((facts & classFactsNeedsClassSuperReference) != 0) {
		Node* temp = factory()->newTempVariable(printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsReservedInNestedScopes,
		});
		emitContext()->addVariableDeclaration(temp);
		getClassLexicalEnvironment()->superClassReference = temp;
		return factory()->updateExpressionWithTypeArguments(
			node,
			factory()->newAssignmentExpression(temp, visitor()->visitNode(node->Expression)),
			nullptr /*typeArguments*/
		);
	}
	return heritageClauseVisitor->visitEachChild(node->asNode());
}

Node* classFieldsTransformer::visitInNewClassLexicalEnvironment(Node* node, TxFactsVisitor visitor) {
	Node* savedCurrentClassContainer = currentClassContainer;
	std::vector<Node*> savedPendingExpressions = pendingExpressions;
	classLexicalEnv* savedLexicalEnvironment = lexicalEnvironment;
	currentClassContainer = node;
	pendingExpressions = {};
	startClassLexicalEnvironment();
	Node* original = emitContext()->mostOriginal(node);
	enclosingClassDeclarations.Add(original);

	if (shouldTransformPrivateElementsOrClassStaticBlocks || nodeHasTransformPrivateStaticElementsFlag(node)) {
		Node* name = getNameOfDeclaration(node);
		if (name != nullptr && isIdentifier(name)) {
			getPrivateIdentifierEnvironment()->data.className = name;
		} else if (Node* assignedName = emitContext()->assignedNameOf(node); assignedName != nullptr) {
			if (isStringLiteral(assignedName)) {
				// If the assigned name has a textSourceNode that is an identifier, use it directly.
				if (Node* textSourceNode = emitContext()->textSourceOf(assignedName); textSourceNode != nullptr && isIdentifier(textSourceNode)) {
					getPrivateIdentifierEnvironment()->data.className = textSourceNode;
				} else if (isIdentifierText(assignedName->text(), LanguageVariant::Standard)) {
					// If the text is a valid identifier, create an identifier from it.
					Node* prefixName = factory()->newIdentifier(assignedName->text());
					getPrivateIdentifierEnvironment()->data.className = prefixName;
				}
			}
		}
	}

	if (shouldTransformPrivateElementsOrClassStaticBlocks) {
		std::vector<Node*> privateInstanceMethodsAndAccessors = getPrivateInstanceMethodsAndAccessors(node);
		if (!privateInstanceMethodsAndAccessors.empty()) {
			getPrivateIdentifierEnvironment()->data.weakSetName = createHoistedVariableForClass(
				"instances",
				privateInstanceMethodsAndAccessors[0]->name(),
				""
			);
		}
	}

	classFacts facts = getClassFacts(node);
	if (facts != classFactsNone) {
		getClassLexicalEnvironment()->facts = facts;
	}

	Node* result = (this->*visitor)(node, facts);
	enclosingClassDeclarations.Delete(original);
	endClassLexicalEnvironment();
	debugAssert(lexicalEnvironment == savedLexicalEnvironment);
	currentClassContainer = savedCurrentClassContainer;
	pendingExpressions = savedPendingExpressions;
	lexicalEnvironment = savedLexicalEnvironment;
	return result;
}

Node* classFieldsTransformer::visitClassDeclaration(ClassDeclaration* node) {
	return visitInNewClassLexicalEnvironment(node->asNode(), &classFieldsTransformer::visitClassDeclarationInNewClassLexicalEnvironment);
}

Node* classFieldsTransformer::visitClassDeclarationInNewClassLexicalEnvironment(Node* node, classFacts facts) {
	ClassDeclaration* classDecl = node->as<ClassDeclaration>();
	// If a class has private static fields, or a static field has a `this` or `super` reference,
	// then we need to allocate a temp variable to hold on to that reference.
	Node* pendingClassReferenceAssignment = nullptr;
	if ((facts & classFactsNeedsClassConstructorReference) != 0) {
		// If we aren't transforming class static blocks, then we can't reuse `_classThis` since in
		// `class C { ... static { _classThis = ... } }; _classThis = C` the outer assignment would occur *after*
		// class static blocks evaluate and would overwrite the replacement constructor produced by class
		// decorators.

		// If we are transforming class static blocks, then we can reuse `_classThis` since the assignment
		// will be evaluated *before* the transformed static blocks are evaluated and thus won't overwrite
		// the replacement constructor.

		if (shouldTransformPrivateElementsOrClassStaticBlocks && emitContext()->classThisOf(node) != nullptr) {
			Node* classThis = emitContext()->classThisOf(node);
			getClassLexicalEnvironment()->classConstructor = classThis;
			pendingClassReferenceAssignment = factory()->newAssignmentExpression(
				classThis,
				factory()->getLocalName(node)
			);
		} else {
			Node* temp = factory()->newTempVariable(printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsReservedInNestedScopes,
			});
			emitContext()->addVariableDeclaration(temp);
			getClassLexicalEnvironment()->classConstructor = temp->clone(*factory());
			pendingClassReferenceAssignment = factory()->newAssignmentExpression(
				temp,
				factory()->getLocalName(node)
			);
		}
	}

	if (emitContext()->classThisOf(node) != nullptr) {
		getClassLexicalEnvironment()->classThis = emitContext()->classThisOf(node);
	}

	bool isClassWithConstructorReference = classContainsConstructorReference(node);

	// Register class alias BEFORE visiting members (Strada registers after, since its
	// onSubstituteNode runs at emit time; we substitute eagerly during transformation).
	Node* alias = getClassLexicalEnvironment()->classConstructor;
	if (isClassWithConstructorReference && alias != nullptr) {
		classAliases[emitContext()->mostOriginal(node)] = alias;
	}

	ModifierList* modifiers = modifierVisitor->visitModifiers(classDecl->modifiers);
	NodeList* heritageClauses = heritageClauseVisitor->visitNodes(classDecl->HeritageClauses);
	auto [members, membersPrologue] = transformClassMembers(node);

	std::vector<Node*> statements;

	if (pendingClassReferenceAssignment != nullptr) {
		pendingExpressions.insert(pendingExpressions.begin(), pendingClassReferenceAssignment);
	}

	// Write any pending expressions from elided or moved computed property names
	if (!pendingExpressions.empty()) {
		statements.push_back(factory()->newExpressionStatement(factory()->inlineExpressions(pendingExpressions)));
	}

	// A class declaration without a name needs a generated name if it has static
	// initialized properties, since those will be moved outside the class body and
	// need to reference the class by name.
	Node* name = classDecl->name;

	if (shouldTransformInitializersUsingSet || shouldTransformPrivateElementsOrClassStaticBlocks) {
		// Emit static property assignment. Because classDeclaration is lexically evaluated,
		// it is safe to emit static property assignment after classDeclaration
		// From ES6 specification:
		//   HasLexicalDeclaration (N) : Determines if the argument identifier has a binding in this environment record that was created using
		//                               a lexical declaration such as a LexicalDeclaration or a ClassDeclaration.
		std::vector<Node*> staticProperties = getStaticPropertiesAndClassStaticBlock(node);
		if (!staticProperties.empty()) {
			if (name == nullptr) {
				name = factory()->newGeneratedNameForNode(node);
			}
			statements = addPropertyOrClassStaticBlockStatements(statements, staticProperties, factory()->getLocalName(node));
		}
	}

	bool isExport = hasSyntacticModifier(node, ModifierFlagsExport);
	bool isDefault = hasSyntacticModifier(node, ModifierFlagsDefault);

	if (!statements.empty() && isExport && isDefault) {
		modifiers = extractModifiers(emitContext(), modifiers, ~ModifierFlagsExportDefault);
		Node* exportAssignment = factory()->newExportAssignment(nullptr, /*isExportEquals*/ false, /*typeNode*/ nullptr, factory()->getLocalName(node));
		statements.push_back(exportAssignment);
	}

	Node* updatedClass = factory()->updateClassDeclaration(
		classDecl,
		modifiers,
		name,
		nullptr, /*typeParameters*/
		heritageClauses,
		members
	);

	std::vector<Node*> result;
	result.reserve(1 + statements.size() + 1);
	if (membersPrologue != nullptr) {
		result.push_back(factory()->newExpressionStatement(membersPrologue));
	}
	result.push_back(updatedClass);
	result.insert(result.end(), statements.begin(), statements.end());
	return factory()->newSyntaxList(result);
}

Node* classFieldsTransformer::visitClassExpression(ClassExpression* node) {
	return visitInNewClassLexicalEnvironment(node->asNode(), &classFieldsTransformer::visitClassExpressionInNewClassLexicalEnvironment);
}

Node* classFieldsTransformer::visitClassExpressionInNewClassLexicalEnvironment(Node* node, classFacts facts) {
	ClassExpression* classExpr = node->as<ClassExpression>();

	// If this class expression is a transformation of a decorated class declaration,
	// then we want to output the pendingExpressions as statements, not as inlined
	// expressions with the class statement.
	//
	// In this case, we use pendingStatements to produce the same output as the
	// class declaration transformation. The VariableStatement visitor will insert
	// these statements after the class expression variable statement.
	bool isDecoratedClassDeclaration = (facts & classFactsClassWasDecorated) != 0;

	if (emitContext()->classThisOf(node) != nullptr) {
		getClassLexicalEnvironment()->classThis = emitContext()->classThisOf(node);
	}

	Node* temp = nullptr;
	if ((facts & classFactsNeedsClassConstructorReference) != 0) {
		if ((shouldTransformPrivateElementsOrClassStaticBlocks || nodeHasTransformPrivateStaticElementsFlag(node)) &&
			emitContext()->classThisOf(node) != nullptr) {
			Node* classThis = emitContext()->classThisOf(node);
			getClassLexicalEnvironment()->classConstructor = classThis;
			temp = classThis;
		} else {
			temp = factory()->newTempVariable(printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsReservedInNestedScopes,
			});
			if (classExpressionNeedsBlockScopedTemp()) {
				emitContext()->addLexicalDeclaration(temp);
			} else {
				emitContext()->addVariableDeclaration(temp);
			}
			getClassLexicalEnvironment()->classConstructor = temp->clone(*factory());
		}
	}

	std::vector<Node*> staticPropertiesOrClassStaticBlocks = getStaticPropertiesAndClassStaticBlock(node);

	// Pre-compute whether the class expression will need a temp variable wrapper.
	// Strada registers class aliases AFTER transformClassMembers (since onSubstituteNode runs
	// at emit time), but we must predict this before visiting members since we substitute
	// eagerly. This requires pre-detecting willHavePrivatePendingExpressions.
	bool isClassWithConstructorReference = false;
	bool hasTransformableStatics = false;
	bool deferTempDeclaration = false;
	if (!isDecoratedClassDeclaration) {
		isClassWithConstructorReference = classContainsConstructorReference(node);
		hasTransformableStatics = (shouldTransformPrivateElementsOrClassStaticBlocks ||
			nodeHasTransformPrivateStaticElementsFlag(node)) && [&]() {
			for (Node* n : staticPropertiesOrClassStaticBlocks) {
				if (isClassStaticBlockDeclaration(n) ||
					isPrivateIdentifierClassElementDeclaration(n) ||
					(shouldTransformInitializers && isInitializedProperty(n))) {
					return true;
				}
			}
			return false;
		}();

		// Private instance elements (fields, methods, accessors) transformed to
		// WeakMap/WeakSet will add initialization expressions to pendingExpressions
		// during transformClassMembers. Pre-detect this so we know whether the class
		// will be wrapped with a temp variable.
		bool willHavePrivatePendingExpressions = shouldTransformPrivateElementsOrClassStaticBlocks && [&]() {
			for (Node* n : node->members()) {
				if (isPrivateIdentifierClassElementDeclaration(n) && !hasStaticModifier(n) &&
					shouldTransformClassElementToWeakMap(n)) {
					return true;
				}
			}
			return false;
		}();
		bool willNeedTempWrapper = hasTransformableStatics || willHavePrivatePendingExpressions;

		// Register class alias BEFORE visiting members (Strada registers after, since its
		// onSubstituteNode runs at emit time). Only register when the class will be wrapped
		// with a temp, matching Strada's conditional registration.
		if (isClassWithConstructorReference && willNeedTempWrapper && getClassLexicalEnvironment()->classConstructor == nullptr) {
			// Create temp early so the alias is available during member visiting, even though in the Strada
			// reference the temp would be created later in the pendingExpressions branch.
			temp = factory()->newTempVariable(printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsReservedInNestedScopes,
			});
			// Defer AddVariableDeclaration to preserve Strada's variable declaration ordering.
			deferTempDeclaration = true;
			getClassLexicalEnvironment()->classConstructor = temp->clone(*factory());
		}
		if (Node* alias = getClassLexicalEnvironment()->classConstructor;
			isClassWithConstructorReference && willNeedTempWrapper && alias != nullptr) {
			classAliases[emitContext()->mostOriginal(node)] = alias;
		}
	}

	ModifierList* modifiers = modifierVisitor->visitModifiers(classExpr->modifiers);
	NodeList* heritageClauses = heritageClauseVisitor->visitNodes(classExpr->HeritageClauses);
	auto [members, membersPrologue] = transformClassMembers(node);

	if (deferTempDeclaration) {
		if (classExpressionNeedsBlockScopedTemp()) {
			emitContext()->addLexicalDeclaration(temp);
		} else {
			emitContext()->addVariableDeclaration(temp);
		}
	}

	Node* classExpression = factory()->updateClassExpression(
		classExpr,
		modifiers,
		classExpr->name,
		nullptr, /*typeParameters*/
		heritageClauses,
		members
	);

	std::vector<Node*> expressions;
	if (membersPrologue != nullptr) {
		expressions.push_back(membersPrologue);
	}

	if (!isDecoratedClassDeclaration) {
		if (hasTransformableStatics || !pendingExpressions.empty()) {
			if (temp == nullptr) {
				temp = factory()->newTempVariable(printer::AutoGenerateOptions{
					printer::GeneratedIdentifierFlagsReservedInNestedScopes,
				});
				if (classExpressionNeedsBlockScopedTemp()) {
					emitContext()->addLexicalDeclaration(temp);
				} else {
					emitContext()->addVariableDeclaration(temp);
				}
				getClassLexicalEnvironment()->classConstructor = temp->clone(*factory());
				if (isClassWithConstructorReference) {
					classAliases[emitContext()->mostOriginal(node)] = getClassLexicalEnvironment()->classConstructor;
				}
			}

			expressions.push_back(factory()->newAssignmentExpression(temp, classExpression));

			// Add any pending expressions leftover from elided or relocated computed property names
			expressions.insert(expressions.end(), pendingExpressions.begin(), pendingExpressions.end());

			std::vector<Node*> staticExprs = generateInitializedPropertyExpressionsOrClassStaticBlock(staticPropertiesOrClassStaticBlocks, temp);
			expressions.insert(expressions.end(), staticExprs.begin(), staticExprs.end());
			expressions.push_back(temp->clone(*factory()));
		} else {
			expressions.push_back(classExpression);
		}
	} else {
		// Decorated class declaration path: emit static properties as separate statements
		// via pendingStatements, matching the class declaration output structure.

		// Write any pending expressions from elided or moved computed property names
		if (!pendingExpressions.empty()) {
			for (Node* expr : pendingExpressions) {
				pendingStatements.push_back(factory()->newExpressionStatement(expr));
			}
		}

		// Emit static properties as statements (via pendingStatements) using the class's
		// internal name as the receiver, matching the class declaration output structure.
		if (!staticPropertiesOrClassStaticBlocks.empty()) {
			Node* classThisOrName = emitContext()->classThisOf(node);
			if (classThisOrName == nullptr) {
				classThisOrName = factory()->getLocalName(node);
			}
			pendingStatements = addPropertyOrClassStaticBlockStatements(pendingStatements, staticPropertiesOrClassStaticBlocks, classThisOrName);
		}

		if (temp != nullptr) {
			expressions.push_back(factory()->newAssignmentExpression(temp, classExpression));
		} else if (shouldTransformPrivateElementsOrClassStaticBlocks && emitContext()->classThisOf(node) != nullptr) {
			expressions.push_back(factory()->newAssignmentExpression(emitContext()->classThisOf(node), classExpression));
		} else {
			expressions.push_back(classExpression);
		}
	}

	if (expressions.size() > 1) {
		emitContext()->addEmitFlags(classExpression, printer::EFIndented);
		for (Node* expr : expressions) {
			emitContext()->addEmitFlags(expr, printer::EFStartOnNewLine);
		}
	}
	return factory()->inlineExpressions(expressions);
}

Node* classFieldsTransformer::visitClassStaticBlockDeclaration(Node* node) {
	if (!shouldTransformPrivateElementsOrClassStaticBlocks) {
		return visitor()->visitEachChild(node);
	}
	// ClassStaticBlockDeclaration for classes are transformed in visitClassDeclaration/visitClassExpression.
	return nullptr;
}

// visitThisExpression replaces Strada's substituteThisExpression / onSubstituteNode.
// Strada substitutes `this` at emit time; we do it eagerly during transformation.
//
// The Strada noSubstitution set (ensureDynamicThisIfNeeded) is not needed because
// transformAutoAccessor() passes the receiver directly rather than emitting `this`.
Node* classFieldsTransformer::visitThisExpression(Node* node) {
	if (insideComputedPropertyName && shouldTransformThisInStaticInitializers &&
		lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr) {
		// Don't replace `this` in computed property names for ES-decorated classes.
		// The esDecorator transformer wraps them in an arrow IIFE where `this` already
		// refers to the correct outer scope.
		if ((lexicalEnvironment->data->facts & classFactsClassWasDecorated) == 0 || legacyDecorators) {
			if (Node* classThis = tryGetClassThisNoContainer(); classThis != nullptr) {
				return classThis;
			}
		}
	}
	if (shouldTransformThisInStaticInitializers && currentClassElement != nullptr &&
		(isClassStaticBlockDeclaration(currentClassElement) ||
			(isPropertyDeclaration(currentClassElement) && hasStaticModifier(currentClassElement))) &&
		lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr) {
		if (Node* classThis = tryGetClassThisNoContainer(); classThis != nullptr) {
			return classThis;
		}
		// When the class was decorated with legacy decorators and no class constructor
		// reference is available, the decorator may replace the constructor, so `this`
		// cannot reliably point to the class. Use `(void 0)` instead.
		if ((lexicalEnvironment->data->facts & classFactsClassWasDecorated) != 0 && legacyDecorators) {
			return factory()->newParenthesizedExpression(factory()->newVoidZeroExpression());
		}
	}
	return node;
}

std::pair<NodeList*, Node*> classFieldsTransformer::transformClassMembers(Node* node) {
	bool shouldTransformPrivateStaticElementsInClass =
		(emitContext()->emitFlags(node) & printer::EFTransformPrivateStaticElements) != 0;
	Node* prologue = nullptr;

	// Declare private names
	if (shouldTransformPrivateElementsOrClassStaticBlocks || shouldTransformPrivateStaticElementsInFile) {
		for (Node* member : node->members()) {
			if (isPrivateIdentifierClassElementDeclaration(member)) {
				if (shouldTransformClassElementToWeakMap(member)) {
					addPrivateIdentifierToEnvironment(member);
				} else {
					privateEnvironment* env = getPrivateIdentifierEnvironment();
					auto* info = new privateIdentifierInfo{};
					info->kind = printer::PrivateIdentifierKind::Untransformed;
					setPrivateIdentifier(env, member->name(), info);
				}
			}
		}

		if (shouldTransformPrivateElementsOrClassStaticBlocks) {
			if (!getPrivateInstanceMethodsAndAccessors(node).empty()) {
				createBrandCheckWeakSetForPrivateMethods();
			}
		}

		if (shouldTransformAutoAccessorsInCurrentClass()) {
			for (Node* member : node->members()) {
				if (isAutoAccessorPropertyDeclaration(member)) {
					printer::AutoGenerateOptions storageOpts;
					storageOpts.Suffix = "_accessor_storage";
					Node* storageName = factory()->newGeneratedPrivateNameForNode(member->name(), storageOpts);
					if (shouldTransformPrivateElementsOrClassStaticBlocks ||
						(shouldTransformPrivateStaticElementsInClass && hasStaticModifier(member))) {
						addPrivateIdentifierPropertyDeclarationToEnvironment(member, storageName);
					} else {
						privateEnvironment* env = getPrivateIdentifierEnvironment();
						// Only register as untransformed if it hasn't already been registered
						// by the first loop (e.g., if esDecorators expanded a private auto-accessor
						// into a backing field with the same generated name).
						if (getPrivateIdentifier(env, storageName) == nullptr) {
							auto* info = new privateIdentifierInfo{};
							info->kind = printer::PrivateIdentifierKind::Untransformed;
							setPrivateIdentifier(env, storageName, info);
						}
					}
				}
			}
		}
	}

	NodeList* members = classElementVisitor->visitNodes(node->memberList());

	// Create a synthetic constructor if necessary
	Node* syntheticConstructor = nullptr;
	{
		bool hasConstructor = false;
		for (Node* m : members->nodes) {
			if (isConstructorDeclaration(m)) {
				hasConstructor = true;
				break;
			}
		}
		if (!hasConstructor) {
			syntheticConstructor = transformConstructor(nullptr, node);
		}
	}

	// If there are pending expressions create a class static block in which to evaluate them, but only if
	// class static blocks are not also being transformed. This block will be injected at the top of the class
	// to ensure that expressions from computed property names are evaluated before any other static
	// initializers.
	Node* syntheticStaticBlock = nullptr;
	if (!shouldTransformPrivateElementsOrClassStaticBlocks && !pendingExpressions.empty()) {
		Node* statement = factory()->newExpressionStatement(factory()->inlineExpressions(pendingExpressions));
		if ((statement->subtreeFacts() & SubtreeContainsLexicalThisOrSuper) != 0) {
			// If there are `this` or `super` references from computed property names, shift the expression
			// into an arrow function to be evaluated in the outer scope so that `this` and `super` are
			// properly captured.
			Node* temp = factory()->newTempVariable();
			emitContext()->addVariableDeclaration(temp);
			Node* arrow = factory()->newArrowFunction(
				nullptr,                            /*modifiers*/
				nullptr,                            /*typeParameters*/
				factory()->newNodeList({}),         /*parameters*/
				nullptr,                            /*returnType*/
				nullptr,                            /*fullSignature*/
				factory()->newToken(Kind::EqualsGreaterThanToken), /*equalsGreaterThanToken*/
				factory()->newBlock(factory()->newNodeList({statement}), /*multiline*/ false)
			);
			prologue = factory()->newAssignmentExpression(temp, arrow);
			statement = factory()->newExpressionStatement(
				factory()->newCallExpression(temp, nullptr /*questionDotToken*/, nullptr /*typeArguments*/, factory()->newNodeList({}), NodeFlagsNone)
			);
		}

		Node* block = factory()->newBlock(factory()->newNodeList({statement}), /*multiline*/ false);
		syntheticStaticBlock = factory()->newClassStaticBlockDeclaration(nullptr /*modifiers*/, block);
		pendingExpressions = {};
	}

	// If we created a synthetic constructor or class static block, add them to the visited members
	if (syntheticConstructor != nullptr || syntheticStaticBlock != nullptr) {
		std::vector<Node*> membersArray;
		membersArray.reserve(members->nodes.size() + 2);

		// Find and preserve classThis assignment block and named evaluation helper block at the top
		int classThisIdx = -1;
		for (size_t i = 0; i < members->nodes.size(); i++) {
			if (isClassThisAssignmentBlock(emitContext(), members->nodes[i])) {
				classThisIdx = static_cast<int>(i);
				break;
			}
		}
		int namedEvalIdx = -1;
		for (size_t i = 0; i < members->nodes.size(); i++) {
			if (isClassNamedEvaluationHelperBlock(emitContext(), members->nodes[i])) {
				namedEvalIdx = static_cast<int>(i);
				break;
			}
		}

		if (classThisIdx >= 0) {
			membersArray.push_back(members->nodes[classThisIdx]);
		}
		if (namedEvalIdx >= 0) {
			membersArray.push_back(members->nodes[namedEvalIdx]);
		}
		if (syntheticConstructor != nullptr) {
			membersArray.push_back(syntheticConstructor);
		}
		if (syntheticStaticBlock != nullptr) {
			membersArray.push_back(syntheticStaticBlock);
		}

		for (size_t i = 0; i < members->nodes.size(); i++) {
			Node* member = members->nodes[i];
			if (static_cast<int>(i) != classThisIdx && static_cast<int>(i) != namedEvalIdx) {
				membersArray.push_back(member);
			}
		}
		members = factory()->newNodeList(membersArray);
		members->loc = node->memberList()->loc;
	}

	return {members, prologue};
}

void classFieldsTransformer::createBrandCheckWeakSetForPrivateMethods() {
	privateEnvironment* env = getPrivateIdentifierEnvironment();
	Node* weakSetName = env->data.weakSetName;
	debugAssert(weakSetName != nullptr, "weakSetName should be set in private identifier environment");

	addPendingExpressions(
		factory()->newAssignmentExpression(
			weakSetName,
			factory()->newNewExpression(
				factory()->newIdentifier("WeakSet"),
				nullptr, /*typeArguments*/
				factory()->newNodeList({})
			)
		)
	);
}

Node* classFieldsTransformer::transformConstructor(ConstructorDeclaration* constructor, Node* container) {
	// NOTE: The Strada reference pre-visits the constructor via `visitNode(constructor, visitor)` before
	// checking WillHoistInitializersToConstructor. This is not done here because Go's variable environment
	// (StartVariableEnvironment/EndAndMergeVariableEnvironment) is scoped inside transformConstructorBody.
	// Pre-visiting would hoist variables outside that scope, causing them to appear after field initializers
	// instead of before. Instead, we visit parameters and body separately within the correct scopes.
	if (lexicalEnvironment == nullptr || lexicalEnvironment->data == nullptr ||
		(lexicalEnvironment->data->facts & classFactsWillHoistInitializersToConstructor) == 0) {
		if (constructor != nullptr) {
			return visitor()->visitEachChild(constructor->asNode());
		}
		return nullptr;
	}

	Node* extendsClauseElement = getClassExtendsHeritageElement(container);
	bool isDerivedClass = extendsClauseElement != nullptr &&
		skipOuterExpressions(extendsClauseElement->expression(), OEKAll)->kind != Kind::NullKeyword;

	NodeList* parameters = nullptr;
	if (constructor != nullptr) {
		parameters = visitor()->visitNodes(constructor->Parameters);
	}

	Node* body = transformConstructorBody(container, constructor, isDerivedClass);
	if (body == nullptr) {
		if (constructor != nullptr) {
			return visitor()->visitEachChild(constructor->asNode());
		}
		return nullptr;
	}

	if (constructor != nullptr) {
		debugAssert(parameters != nullptr);
		return factory()->updateConstructorDeclaration(
			constructor,
			nullptr, /*modifiers*/
			nullptr, /*typeParameters*/
			parameters,
			nullptr, /*returnType*/
			nullptr, /*fullSignature*/
			body
		);
	}

	if (parameters == nullptr) {
		parameters = factory()->newNodeList({});
	}

	Node* result = factory()->newConstructorDeclaration(
		nullptr, /*modifiers*/
		nullptr, /*typeParameters*/
		parameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		body
	);
	result->loc = container->loc;
	return result;
}

std::vector<Node*> classFieldsTransformer::transformConstructorBodyWorker(
	std::vector<Node*> statementsOut,
	std::vector<Node*> statementsIn,
	int statementOffset,
	std::vector<int> superPath,
	int superPathDepth,
	std::vector<Node*> initializerStatements,
	ConstructorDeclaration* constructor) {
	int superStatementIndex = superPath[superPathDepth];
	Node* superStatement = statementsIn[superStatementIndex];

	// Visit statements before super
	{
		auto visited = visitor()->visitSlice(std::vector<Node*>(
			statementsIn.begin() + statementOffset, statementsIn.begin() + superStatementIndex));
		statementsOut.insert(statementsOut.end(), visited.first.begin(), visited.first.end());
	}
	statementOffset = superStatementIndex + 1;

	if (isTryStatement(superStatement)) {
		Block* tryBlock = superStatement->as<TryStatement>()->TryBlock->as<Block>();
		std::vector<Node*> tryBlockStatements = transformConstructorBodyWorker(
			{},
			tryBlock->Statements->nodes,
			0, /*statementOffset*/
			superPath,
			superPathDepth + 1,
			initializerStatements,
			constructor
		);
		NodeList* tryStatementList = factory()->newNodeList(tryBlockStatements);
		tryStatementList->loc = tryBlock->Statements->loc;

		Node* catchClause = visitor()->visitNode(superStatement->as<TryStatement>()->CatchClause);
		Node* finallyBlock = visitor()->visitNode(superStatement->as<TryStatement>()->FinallyBlock);

		Node* updated = factory()->updateTryStatement(
			superStatement->as<TryStatement>(),
			factory()->updateBlock(tryBlock, tryStatementList, tryBlock->MultiLine),
			catchClause,
			finallyBlock
		);
		statementsOut.push_back(updated);
	} else {
		auto visited = visitor()->visitSlice(std::vector<Node*>(
			statementsIn.begin() + superStatementIndex, statementsIn.begin() + superStatementIndex + 1));
		statementsOut.insert(statementsOut.end(), visited.first.begin(), visited.first.end());

		// Add the property initializers. Transforms this:
		//
		//  public x = 1;
		//
		// Into this:
		//
		//  constructor() {
		//      this.x = 1;
		//  }
		//
		// If we do useDefineForClassFields, they'll be converted elsewhere.
		// We instead *remove* them from the transformed output at this stage.

		// parameter-property assignments should occur immediately after the prologue and `super()`,
		// so only count the statements that immediately follow.
		while (statementOffset < static_cast<int>(statementsIn.size())) {
			Node* stmt = statementsIn[statementOffset];
			Node* orig = emitContext()->mostOriginal(stmt);
			if (isParameterPropertyDeclaration(orig, constructor->asNode())) {
				statementOffset++;
			} else {
				break;
			}
		}

		statementsOut.insert(statementsOut.end(), initializerStatements.begin(), initializerStatements.end());
	}

	// Visit remaining statements
	auto visited2 = visitor()->visitSlice(std::vector<Node*>(
		statementsIn.begin() + statementOffset, statementsIn.end()));
	statementsOut.insert(statementsOut.end(), visited2.first.begin(), visited2.first.end());
	return statementsOut;
}

Node* classFieldsTransformer::transformConstructorBody(Node* container, ConstructorDeclaration* constructor, bool isDerivedClass) {
	std::vector<Node*> instanceProperties = getProperties(container, /*requireInitializer*/ false, /*isStatic*/ false);
	std::vector<Node*> properties = instanceProperties;
	if (!compilerOptions->GetUseDefineForClassFields()) {
		std::vector<Node*> filtered;
		for (Node* prop : properties) {
			if (prop->initializer() != nullptr || isPrivateIdentifier(prop->name()) || hasAccessorModifier(prop)) {
				filtered.push_back(prop);
			}
		}
		properties = filtered;
	}

	std::vector<Node*> privateMethodsAndAccessors = getPrivateInstanceMethodsAndAccessors(container);
	bool needsConstructorBody = !properties.empty() || !privateMethodsAndAccessors.empty();

	// Only generate synthetic constructor when there are property initializers to move.
	if (constructor == nullptr && !needsConstructorBody) {
		return emitContext()->visitFunctionBody(nullptr, visitor());
	}

	emitContext()->startVariableEnvironment();

	bool needsSyntheticConstructor = constructor == nullptr && isDerivedClass;
	std::vector<Node*> statements;

	// Add the property initializers. Transforms this:
	//
	//  public x = 1;
	//
	// Into this:
	//
	//  constructor() {
	//      this.x = 1;
	//  }
	//
	std::vector<Node*> initializerStatements;
	Node* receiver = factory()->newThisExpression();

	// private methods can be called in property initializers, they should execute first
	initializerStatements = addInstanceMethodStatements(initializerStatements, privateMethodsAndAccessors, receiver);

	if (constructor != nullptr) {
		std::vector<Node*> parameterProperties;
		for (Node* prop : instanceProperties) {
			if (isParameterPropertyDeclaration(emitContext()->mostOriginal(prop), constructor->asNode())) {
				parameterProperties.push_back(prop);
			}
		}
		std::vector<Node*> nonParameterProperties;
		for (Node* prop : properties) {
			if (!isParameterPropertyDeclaration(emitContext()->mostOriginal(prop), constructor->asNode())) {
				nonParameterProperties.push_back(prop);
			}
		}
		initializerStatements = addPropertyOrClassStaticBlockStatements(initializerStatements, parameterProperties, receiver);
		initializerStatements = addPropertyOrClassStaticBlockStatements(initializerStatements, nonParameterProperties, receiver);
	} else {
		initializerStatements = addPropertyOrClassStaticBlockStatements(initializerStatements, properties, receiver);
	}

	if (constructor != nullptr && constructor->Body != nullptr) {
		Block* body = constructor->Body->as<Block>();

		// Copy prologue
		for (Node* stmt : body->Statements->nodes) {
			if (isPrologueDirective(stmt)) {
				statements.push_back(stmt);
			} else {
				break;
			}
		}
		int statementOffset = static_cast<int>(statements.size());

		std::vector<int> superPath = findSuperStatementIndexPath(body->Statements->nodes, statementOffset);
		if (!superPath.empty()) {
			statements = transformConstructorBodyWorker(statements, body->Statements->nodes, statementOffset, superPath, 0, initializerStatements, constructor);
		} else {
			// parameter-property assignments should occur immediately after the prologue and `super()`,
			// so only count the statements that immediately follow.
			while (statementOffset < static_cast<int>(body->Statements->nodes.size())) {
				Node* stmt = body->Statements->nodes[statementOffset];
				Node* orig = emitContext()->mostOriginal(stmt);
				if (isParameterPropertyDeclaration(orig, constructor->asNode())) {
					statementOffset++;
				} else {
					break;
				}
			}
			statements.insert(statements.end(), initializerStatements.begin(), initializerStatements.end());
			auto visited = visitor()->visitSlice(std::vector<Node*>(
				body->Statements->nodes.begin() + statementOffset, body->Statements->nodes.end()));
			statements.insert(statements.end(), visited.first.begin(), visited.first.end());
		}
	} else {
		if (needsSyntheticConstructor) {
			// Add a synthetic `super` call:
			//
			//  super(...arguments);
			//
			Node* superCall = factory()->newExpressionStatement(
				factory()->newCallExpression(
					factory()->newKeywordExpression(Kind::SuperKeyword),
					nullptr, /*typeArguments*/
					nullptr, /*questionDotToken*/
					factory()->newNodeList({
						factory()->newSpreadElement(factory()->newIdentifier("arguments")),
					}),
					NodeFlagsNone
				)
			);
			statements.push_back(superCall);
		}
		statements.insert(statements.end(), initializerStatements.begin(), initializerStatements.end());
	}

	statements = emitContext()->endAndMergeVariableEnvironment(statements);

	if (statements.empty() && constructor == nullptr) {
		return nullptr;
	}

	bool multiLine;
	if (constructor != nullptr && constructor->Body != nullptr &&
		constructor->Body->as<Block>()->Statements->nodes.size() >= statements.size()) {
		multiLine = constructor->Body->as<Block>()->MultiLine;
	} else {
		multiLine = !statements.empty();
	}

	NodeList* statementList = factory()->newNodeList(statements);
	if (constructor != nullptr && constructor->Body != nullptr) {
		statementList->loc = constructor->Body->as<Block>()->Statements->loc;
	} else {
		statementList->loc = TextRange{container->memberList()->loc.pos(), container->memberList()->loc.end()};
	}

	Node* block = factory()->newBlock(statementList, multiLine);
	if (constructor != nullptr && constructor->Body != nullptr) {
		block->loc = constructor->Body->loc;
	}
	return block;
}

// addPropertyOrClassStaticBlockStatements generates assignment statements for property initializers.
std::vector<Node*> classFieldsTransformer::addPropertyOrClassStaticBlockStatements(std::vector<Node*> statements,
                                                                                   std::vector<Node*> properties,
                                                                                   Node* receiver) {
	for (Node* property : properties) {
		if (isStatic(property) && !shouldTransformPrivateElementsOrClassStaticBlocks) {
			continue;
		}
		Node* statement = transformPropertyOrClassStaticBlock(property, receiver);
		if (statement != nullptr) {
			statements.push_back(statement);
		}
	}
	return statements;
}

Node* classFieldsTransformer::transformPropertyOrClassStaticBlock(Node* property, Node* receiver) {
	Node* expression;
	if (isClassStaticBlockDeclaration(property)) {
		expression = setCurrentClassElementAnd(property, &classFieldsTransformer::transformClassStaticBlockDeclaration, property);
	} else {
		expression = transformProperty(property->as<PropertyDeclaration>(), receiver);
	}
	if (expression == nullptr) {
		return nullptr;
	}

	Node* statement = factory()->newExpressionStatement(expression);
	emitContext()->setOriginal(statement, property);
	emitContext()->addEmitFlags(statement, emitContext()->emitFlags(property) & printer::EFNoComments);
	emitContext()->setCommentRange(statement, property->loc);

	Node* propertyOriginalNode = emitContext()->mostOriginal(property);
	if (isParameterDeclaration(propertyOriginalNode)) {
		emitContext()->setSourceMapRange(statement, propertyOriginalNode->loc);
		emitContext()->addEmitFlags(statement, printer::EFNoComments);
	} else {
		emitContext()->setSourceMapRange(statement, moveRangePastModifiers(property));
	}

	// `setOriginalNode` *copies* the `emitNode` from `property`, so now both
	// `statement` and `expression` have a copy of the synthesized comments.
	// Drop the comments from expression to avoid printing them twice.
	emitContext()->setSyntheticLeadingComments(expression, {});
	emitContext()->setSyntheticTrailingComments(expression, {});

	// If the property was originally an auto-accessor, don't emit comments here since they will be attached to
	// the synthesized getter.
	if (hasAccessorModifier(propertyOriginalNode)) {
		emitContext()->addEmitFlags(statement, printer::EFNoComments);
	}

	return statement;
}

// generateInitializedPropertyExpressionsOrClassStaticBlock generates assignment expressions for property initializers.
std::vector<Node*> classFieldsTransformer::generateInitializedPropertyExpressionsOrClassStaticBlock(
	std::vector<Node*> propertiesOrClassStaticBlocks,
	Node* receiver) {
	std::vector<Node*> expressions;
	for (Node* property : propertiesOrClassStaticBlocks) {
		Node* expression;
		if (isClassStaticBlockDeclaration(property)) {
			expression = setCurrentClassElementAnd(property, &classFieldsTransformer::transformClassStaticBlockDeclaration, property);
		} else {
			expression = transformProperty(property->as<PropertyDeclaration>(), receiver);
		}
		if (expression == nullptr) {
			continue;
		}
		emitContext()->setOriginalEx(expression, property, /*allowOverwrite*/ true);
		emitContext()->assignCommentAndSourceMapRanges(expression, property);
		expressions.push_back(expression);
	}
	return expressions;
}

// transformProperty transforms a property initializer into an assignment expression.
Node* classFieldsTransformer::transformProperty(PropertyDeclaration* property, Node* receiver) {
	Node* savedCurrentClassElement = currentClassElement;
	Node* transformed = transformPropertyWorker(property, receiver);
	if (transformed != nullptr && hasStaticModifier(property->asNode())) {
		emitContext()->addEmitFlags(transformed, printer::EFNoLexicalThis);
	}
	if (transformed != nullptr && hasStaticModifier(property->asNode()) &&
		lexicalEnvironment != nullptr && lexicalEnvironment->data != nullptr && lexicalEnvironment->data->facts != 0) {
		// capture the lexical environment for the member
		emitContext()->setOriginal(transformed, property->asNode());
		emitContext()->setSourceMapRange(transformed, emitContext()->sourceMapRange(property->name));
	}
	currentClassElement = savedCurrentClassElement;
	return transformed;
}

Node* classFieldsTransformer::transformPropertyWorker(PropertyDeclaration* property, Node* receiver) {
	// We generate a name here in order to reuse the value cached by the relocated computed name expression (which uses the same generated name)
	bool emitAssignment = !compilerOptions->GetUseDefineForClassFields();

	if (isNamedEvaluationAnd(emitContext(), property->asNode(), isAnonymousClassNeedingAssignedName)) {
		property = transformNamedEvaluation(emitContext(), property->asNode(), false, "")->as<PropertyDeclaration>();
	}

	Node* propertyName = property->name;
	if (hasAccessorModifier(property->asNode())) {
		printer::AutoGenerateOptions storageOpts;
		storageOpts.Suffix = "_accessor_storage";
		propertyName = factory()->newGeneratedPrivateNameForNode(property->name, storageOpts);
	} else if (isComputedPropertyName(propertyName) && !isSimpleInlineableExpression(propertyName->expression())) {
		propertyName = factory()->updateComputedPropertyName(
			propertyName->as<ComputedPropertyName>(),
			factory()->newGeneratedNameForNode(propertyName)
		);
	}

	if (hasStaticModifier(property->asNode())) {
		currentClassElement = property->asNode();
	}

	if (isPrivateIdentifier(propertyName) && shouldTransformClassElementToWeakMap(property->asNode())) {
		privateIdentifierInfo* info = accessPrivateIdentifier(propertyName);
		if (info != nullptr) {
			if (info->kind == printer::PrivateIdentifierKind::Field) {
				if (!info->isStatic) {
					return createPrivateInstanceFieldInitializer(
						factory(),
						receiver,
						visitor()->visitNode(property->Initializer),
						info->brandCheckIdentifier
					);
				}
				return createPrivateStaticFieldInitializer(
					factory(),
					info->variableName,
					visitor()->visitNode(property->Initializer)
				);
			}
			return nullptr;
		} else {
			TSC_UNREACHABLE("Undeclared private name for property declaration.");
		}
	}

	if ((isPrivateIdentifier(propertyName) || hasStaticModifier(property->asNode())) && property->Initializer == nullptr) {
		return nullptr;
	}

	// TODO: can we get rid of this original checking and better coordinate with runtimesyntax?
	if (hasAbstractModifier(emitContext()->mostOriginal(property->asNode()))) {
		return nullptr;
	}

	Node* initializer = visitor()->visitNode(property->Initializer);
	Node* propertyOriginalNode = emitContext()->mostOriginal(property->asNode());
	if (isParameterPropertyDeclaration(propertyOriginalNode, propertyOriginalNode->parent) && isIdentifier(propertyName)) { //nolint:customlint // MostOriginal returns parse-tree nodes, and this parent relationship is intentional.
		// A parameter-property declaration always overrides the initializer. The only time a parameter-property
		// declaration *should* have an initializer is when decorators have added initializers that need to run before
		// any other initializer
		Node* localName = propertyName->clone(*factory());
		if (initializer != nullptr) {
			// unwrap `(__runInitializers(this, _instanceExtraInitializers), void 0)`
			if (isParenthesizedExpression(initializer) &&
				isCommaExpression(initializer->expression()) &&
				emitContext()->isCallToHelper(initializer->expression()->as<BinaryExpression>()->Left, "__runInitializers") &&
				isVoidExpression(initializer->expression()->as<BinaryExpression>()->Right) &&
				isNumericLiteral(initializer->expression()->as<BinaryExpression>()->Right->expression())) {
				initializer = initializer->expression()->as<BinaryExpression>()->Left;
			}
			initializer = factory()->inlineExpressions({initializer, localName});
		} else {
			initializer = localName;
		}
		emitContext()->addEmitFlags(propertyName, printer::EFNoComments | printer::EFNoSourceMap);
		emitContext()->setSourceMapRange(localName, propertyOriginalNode->name()->loc);
		emitContext()->addEmitFlags(localName, printer::EFNoComments);
	} else if (initializer == nullptr) {
		initializer = factory()->newVoidZeroExpression();
	}

	if (emitAssignment || isPrivateIdentifier(propertyName)) {
		Node* memberAccess = createMemberAccessForPropertyName(factory(), emitContext(), receiver, propertyName, propertyName);
		emitContext()->addEmitFlags(memberAccess, printer::EFNoLeadingComments);
		return factory()->newAssignmentExpression(memberAccess, initializer);
	}

	// useDefineForClassFields: Object.defineProperty
	Node* name;
	if (isComputedPropertyName(propertyName)) {
		name = propertyName->expression();
	} else if (isIdentifier(propertyName)) {
		name = factory()->newStringLiteral(propertyName->text(), TokenFlagsNone);
	} else {
		name = propertyName;
	}
	Node* descriptor = factory()->newObjectLiteralExpression(factory()->newNodeList({
		factory()->newPropertyAssignment(nullptr, factory()->newIdentifier("enumerable"), nullptr, nullptr, factory()->newTrueExpression()),
		factory()->newPropertyAssignment(nullptr, factory()->newIdentifier("configurable"), nullptr, nullptr, factory()->newTrueExpression()),
		factory()->newPropertyAssignment(nullptr, factory()->newIdentifier("writable"), nullptr, nullptr, factory()->newTrueExpression()),
		factory()->newPropertyAssignment(nullptr, factory()->newIdentifier("value"), nullptr, nullptr, initializer),
	}), true);
	return factory()->newObjectDefinePropertyCall(receiver, name, descriptor);
}

// addInstanceMethodStatements generates brand-check initializer for private methods.
std::vector<Node*> classFieldsTransformer::addInstanceMethodStatements(std::vector<Node*> statements,
                                                                       std::vector<Node*> methods,
                                                                       Node* receiver) {
	if (!shouldTransformPrivateElementsOrClassStaticBlocks || methods.empty()) {
		return statements;
	}

	privateEnvironment* env = getPrivateIdentifierEnvironment();
	Node* weakSetName = env->data.weakSetName;
	debugAssert(weakSetName != nullptr, "weakSetName should be set in private identifier environment");

	statements.push_back(
		factory()->newExpressionStatement(
			createPrivateInstanceMethodInitializer(factory(), receiver, weakSetName)
		)
	);
	return statements;
}

Node* classFieldsTransformer::visitInvalidSuperProperty(Node* node) {
	if (isPropertyAccessExpression(node)) {
		return factory()->updatePropertyAccessExpression(
			node->as<PropertyAccessExpression>(),
			factory()->newVoidZeroExpression(),
			nullptr,
			node->name(),
			node->flags
		);
	}
	return factory()->updateElementAccessExpression(
		node->as<ElementAccessExpression>(),
		factory()->newVoidZeroExpression(),
		nullptr,
		visitor()->visitNode(node->as<ElementAccessExpression>()->ArgumentExpression),
		node->flags
	);
}

// getPropertyNameExpressionIfNeeded transforms a computed property name, then either returns an expression
// which caches the value of the result or the expression itself if the value is either unused or safe to
// inline into multiple locations.
// shouldHoist indicates whether the expression needs to be reused (i.e., for an initializer or a decorator).
Node* classFieldsTransformer::getPropertyNameExpressionIfNeeded(Node* name, bool shouldHoist) {
	if (!isComputedPropertyName(name)) {
		return nullptr;
	}
	BinaryExpression* cacheAssignment = findComputedPropertyNameCacheAssignment(emitContext(), name);
	// Switch to outer lex env for computed property name expressions, matching
	// Strada reference's onEmitNode behavior for ComputedPropertyName.
	classLexicalEnv* savedLexicalEnvironment = lexicalEnvironment;
	bool savedInsideComputedPropertyName = insideComputedPropertyName;
	insideComputedPropertyName = true;
	if (lexicalEnvironment != nullptr && lexicalEnvironment->previous != nullptr) {
		lexicalEnvironment = lexicalEnvironment->previous;
	}
	Node* expression = visitor()->visitNode(name->expression());
	lexicalEnvironment = savedLexicalEnvironment;
	insideComputedPropertyName = savedInsideComputedPropertyName;
	Node* innerExpression = skipPartiallyEmittedExpressions(expression);
	bool inlinable = isSimpleInlineableExpression(innerExpression);
	bool alreadyTransformed = cacheAssignment != nullptr ||
		(isAssignmentExpression(innerExpression, /*excludeCompoundAssignment*/ true) &&
			isIdentifier(innerExpression->as<BinaryExpression>()->Left) &&
			isGeneratedIdentifier(emitContext(), innerExpression->as<BinaryExpression>()->Left));
	if (!alreadyTransformed && !inlinable && shouldHoist) {
		Node* generatedName = factory()->newGeneratedNameForNode(name);
		if (requiresBlockScopedVar()) {
			emitContext()->addLexicalDeclaration(generatedName);
		} else {
			emitContext()->addVariableDeclaration(generatedName);
		}
		return factory()->newAssignmentExpression(generatedName, expression);
	}
	if (inlinable || isIdentifier(innerExpression)) {
		return nullptr;
	}
	return expression;
}

void classFieldsTransformer::startClassLexicalEnvironment() {
	auto* env = new classLexicalEnv{};
	env->previous = lexicalEnvironment;
	lexicalEnvironment = env;
}

void classFieldsTransformer::endClassLexicalEnvironment() {
	lexicalEnvironment = lexicalEnvironment->previous;
}

classLexicalEnvironment* classFieldsTransformer::getClassLexicalEnvironment() {
	debugAssert(lexicalEnvironment != nullptr);
	if (lexicalEnvironment->data == nullptr) {
		lexicalEnvironment->data = new classLexicalEnvironment{};
	}
	return lexicalEnvironment->data;
}

privateEnvironment* classFieldsTransformer::getPrivateIdentifierEnvironment() {
	debugAssert(lexicalEnvironment != nullptr);
	if (lexicalEnvironment->privateEnv == nullptr) {
		lexicalEnvironment->privateEnv = new privateEnvironment{};
	}
	return lexicalEnvironment->privateEnv;
}

void classFieldsTransformer::addPendingExpressions(Node* expr) {
	pendingExpressions.push_back(expr);
}

void classFieldsTransformer::addPrivateIdentifierPropertyDeclarationToEnvironment(Node* node, Node* name) {
	classLexicalEnvironment* lex = getClassLexicalEnvironment();
	privateEnvironment* env = getPrivateIdentifierEnvironment();
	bool isStatic = hasStaticModifier(node);
	privateIdentifierInfo* previousInfo = getPrivateIdentifier(env, name);
	bool isValid = !isReservedPrivateName(name) && previousInfo == nullptr;

	if (isStatic) {
		Node* brandCheckIdentifier = lex->classThis;
		if (brandCheckIdentifier == nullptr) {
			brandCheckIdentifier = lex->classConstructor;
		}
		Node* variableName = createHoistedVariableForPrivateName(name, "");
		auto* info = new privateIdentifierInfo{};
		info->kind = printer::PrivateIdentifierKind::Field;
		info->isStatic = true;
		info->brandCheckIdentifier = brandCheckIdentifier;
		info->variableName = variableName;
		info->isValid = isValid;
		setPrivateIdentifier(env, name, info);
	} else {
		Node* weakMapName = createHoistedVariableForPrivateName(name, "");
		auto* info = new privateIdentifierInfo{};
		info->kind = printer::PrivateIdentifierKind::Field;
		info->isStatic = false;
		info->brandCheckIdentifier = weakMapName;
		info->isValid = isValid;
		setPrivateIdentifier(env, name, info);
		addPendingExpressions(
			factory()->newAssignmentExpression(
				weakMapName,
				factory()->newNewExpression(
					factory()->newIdentifier("WeakMap"),
					nullptr, /*typeArguments*/
					factory()->newNodeList({})
				)
			)
		);
	}
}

void classFieldsTransformer::addPrivateIdentifierMethodToEnvironment(Node* name, classLexicalEnvironment* lex,
                                                                     privateEnvironment* env, bool isStatic, bool isValid) {
	Node* methodName = createHoistedVariableForPrivateName(name, "");
	Node* brandCheckIdentifier;
	if (isStatic) {
		brandCheckIdentifier = lex->classThis;
		if (brandCheckIdentifier == nullptr) {
			brandCheckIdentifier = lex->classConstructor;
		}
		debugAssert(brandCheckIdentifier != nullptr, "classConstructor should be set in private identifier environment");
	} else {
		brandCheckIdentifier = env->data.weakSetName;
	}
	auto* info = new privateIdentifierInfo{};
	info->kind = printer::PrivateIdentifierKind::Method;
	info->methodName = methodName;
	info->brandCheckIdentifier = brandCheckIdentifier;
	info->isStatic = isStatic;
	info->isValid = isValid;
	setPrivateIdentifier(env, name, info);
}

void classFieldsTransformer::addPrivateIdentifierGetAccessorToEnvironment(Node* name, classLexicalEnvironment* lex,
                                                                          privateEnvironment* env, bool isStatic, bool isValid,
                                                                          privateIdentifierInfo* previousInfo) {
	Node* getterName = createHoistedVariableForPrivateName(name, "_get");
	Node* brandCheckIdentifier;
	if (isStatic) {
		brandCheckIdentifier = lex->classThis;
		if (brandCheckIdentifier == nullptr) {
			brandCheckIdentifier = lex->classConstructor;
		}
		debugAssert(brandCheckIdentifier != nullptr, "classConstructor should be set in private identifier environment");
	} else {
		brandCheckIdentifier = env->data.weakSetName;
		debugAssert(brandCheckIdentifier != nullptr, "weakSetName should be set in private identifier environment");
	}

	if (previousInfo != nullptr && previousInfo->kind == printer::PrivateIdentifierKind::Accessor &&
		previousInfo->isStatic == isStatic && previousInfo->getterName == nullptr) {
		previousInfo->getterName = getterName;
	} else {
		auto* info = new privateIdentifierInfo{};
		info->kind = printer::PrivateIdentifierKind::Accessor;
		info->getterName = getterName;
		info->brandCheckIdentifier = brandCheckIdentifier;
		info->isStatic = isStatic;
		info->isValid = isValid;
		setPrivateIdentifier(env, name, info);
	}
}

void classFieldsTransformer::addPrivateIdentifierSetAccessorToEnvironment(Node* name, classLexicalEnvironment* lex,
                                                                          privateEnvironment* env, bool isStatic, bool isValid,
                                                                          privateIdentifierInfo* previousInfo) {
	Node* setterName = createHoistedVariableForPrivateName(name, "_set");
	Node* brandCheckIdentifier;
	if (isStatic) {
		brandCheckIdentifier = lex->classThis;
		if (brandCheckIdentifier == nullptr) {
			brandCheckIdentifier = lex->classConstructor;
		}
		debugAssert(brandCheckIdentifier != nullptr, "classConstructor should be set in private identifier environment");
	} else {
		brandCheckIdentifier = env->data.weakSetName;
		debugAssert(brandCheckIdentifier != nullptr, "weakSetName should be set in private identifier environment");
	}

	if (previousInfo != nullptr && previousInfo->kind == printer::PrivateIdentifierKind::Accessor &&
		previousInfo->isStatic == isStatic && previousInfo->setterName == nullptr) {
		previousInfo->setterName = setterName;
	} else {
		auto* info = new privateIdentifierInfo{};
		info->kind = printer::PrivateIdentifierKind::Accessor;
		info->setterName = setterName;
		info->brandCheckIdentifier = brandCheckIdentifier;
		info->isStatic = isStatic;
		info->isValid = isValid;
		setPrivateIdentifier(env, name, info);
	}
}

void classFieldsTransformer::addPrivateIdentifierAutoAccessorToEnvironment(Node* node, Node* name,
                                                                           classLexicalEnvironment* lex,
                                                                           privateEnvironment* env,
                                                                           bool isStatic, bool isValid) {
	Node* getterName = createHoistedVariableForPrivateName(name, "_get");
	Node* setterName = createHoistedVariableForPrivateName(name, "_set");
	Node* brandCheckIdentifier;
	if (isStatic) {
		brandCheckIdentifier = lex->classThis;
		if (brandCheckIdentifier == nullptr) {
			brandCheckIdentifier = lex->classConstructor;
		}
		debugAssert(brandCheckIdentifier != nullptr, "classConstructor should be set in private identifier environment");
	} else {
		brandCheckIdentifier = env->data.weakSetName;
		debugAssert(brandCheckIdentifier != nullptr, "weakSetName should be set in private identifier environment");
	}

	auto* info = new privateIdentifierInfo{};
	info->kind = printer::PrivateIdentifierKind::Accessor;
	info->getterName = getterName;
	info->setterName = setterName;
	info->brandCheckIdentifier = brandCheckIdentifier;
	info->isStatic = isStatic;
	info->isValid = isValid;
	setPrivateIdentifier(env, name, info);
}

void classFieldsTransformer::addPrivateIdentifierToEnvironment(Node* node) {
	classLexicalEnvironment* lex = getClassLexicalEnvironment();
	privateEnvironment* env = getPrivateIdentifierEnvironment();
	Node* name = node->name();
	bool isStatic = hasStaticModifier(node);
	privateIdentifierInfo* previousInfo = getPrivateIdentifier(env, name);
	bool isValid = !isReservedPrivateName(name) && previousInfo == nullptr;

	if (isAutoAccessorPropertyDeclaration(node)) {
		addPrivateIdentifierAutoAccessorToEnvironment(node, name, lex, env, isStatic, isValid);
	} else if (isPropertyDeclaration(node)) {
		addPrivateIdentifierPropertyDeclarationToEnvironment(node, name);
	} else if (isMethodDeclaration(node)) {
		addPrivateIdentifierMethodToEnvironment(name, lex, env, isStatic, isValid);
	} else if (isGetAccessorDeclaration(node)) {
		addPrivateIdentifierGetAccessorToEnvironment(name, lex, env, isStatic, isValid, previousInfo);
	} else if (isSetAccessorDeclaration(node)) {
		addPrivateIdentifierSetAccessorToEnvironment(name, lex, env, isStatic, isValid, previousInfo);
	}
}

void classFieldsTransformer::setPrivateIdentifier(privateEnvironment* env, Node* name, privateIdentifierInfo* info) {
	if (emitContext()->hasAutoGenerateInfo(name)) {
		env->generatedIdentifiers[emitContext()->getNodeForGeneratedName(name)] = info;
	} else {
		env->members[name->text()] = info;
	}
}

privateIdentifierInfo* classFieldsTransformer::getPrivateIdentifier(privateEnvironment* env, Node* name) {
	if (emitContext()->hasAutoGenerateInfo(name)) {
		auto it = env->generatedIdentifiers.find(emitContext()->getNodeForGeneratedName(name));
		if (it != env->generatedIdentifiers.end()) {
			return it->second;
		}
		return nullptr;
	}
	auto it = env->members.find(name->text());
	if (it != env->members.end()) {
		return it->second;
	}
	return nullptr;
}

Node* classFieldsTransformer::createHoistedVariableForClass(std::string nameText, Node* /*node*/, std::string suffix) {
	privateEnvironment* env = getPrivateIdentifierEnvironment();
	Node* identifier;
	printer::AutoGenerateOptions opts;
	opts.Flags = printer::GeneratedIdentifierFlagsOptimistic | printer::GeneratedIdentifierFlagsReservedInNestedScopes;
	opts.Suffix = suffix;
	if (env->data.className != nullptr) {
		std::string prefix = "_" + env->data.className->text() + "_";
		identifier = factory()->newUniqueName(prefix + nameText, opts);
	} else {
		identifier = factory()->newUniqueName("_" + nameText, opts);
	}
	if (requiresBlockScopedVar()) {
		emitContext()->addLexicalDeclaration(identifier);
	} else {
		emitContext()->addVariableDeclaration(identifier);
	}
	return identifier;
}

Node* classFieldsTransformer::createHoistedVariableForClassFromNode(Node* name, std::string suffix) {
	privateEnvironment* env = getPrivateIdentifierEnvironment();
	std::string prefix;
	if (env->data.className != nullptr) {
		prefix = "_" + env->data.className->text() + "_";
	} else {
		prefix = "_";
	}
	printer::AutoGenerateOptions opts;
	opts.Flags = printer::GeneratedIdentifierFlagsOptimistic | printer::GeneratedIdentifierFlagsReservedInNestedScopes;
	opts.Prefix = prefix;
	opts.Suffix = suffix;
	Node* identifier = factory()->newGeneratedNameForNode(name, opts);
	if (requiresBlockScopedVar()) {
		emitContext()->addLexicalDeclaration(identifier);
	} else {
		emitContext()->addVariableDeclaration(identifier);
	}
	return identifier;
}

Node* classFieldsTransformer::createHoistedVariableForPrivateName(Node* name, std::string suffix) {
	// If the name is a generated identifier (e.g., auto-accessor backing field),
	// use node-based name generation so the emitter can resolve the name properly.
	if (emitContext()->hasAutoGenerateInfo(name)) {
		return createHoistedVariableForClassFromNode(name, suffix);
	}
	std::string text = name->text();
	if (text.size() >= 1 && text[0] == '#') {
		text = text.substr(1); // strip leading '#'
	}
	return createHoistedVariableForClass(text, name, suffix);
}

// accessPrivateIdentifier accesses an already defined PrivateIdentifier in the current
// PrivateIdentifierEnvironment.
privateIdentifierInfo* classFieldsTransformer::accessPrivateIdentifier(Node* name) {
	for (classLexicalEnv* env = lexicalEnvironment; env != nullptr; env = env->previous) {
		if (env->privateEnv != nullptr) {
			if (privateIdentifierInfo* info = getPrivateIdentifier(env->privateEnv, name); info != nullptr) {
				if (info->kind == printer::PrivateIdentifierKind::Untransformed) {
					return nullptr;
				}
				return info;
			}
		}
	}
	return nullptr;
}

Node* classFieldsTransformer::wrapPrivateIdentifierForDestructuringTarget(Node* node) {
	PropertyAccessExpression* prop = node->as<PropertyAccessExpression>();
	Node* parameter = factory()->newGeneratedNameForNode(node);
	privateIdentifierInfo* info = accessPrivateIdentifier(prop->name);
	if (info == nullptr) {
		return visitor()->visitEachChild(node);
	}
	Node* receiver = prop->Expression;
	// We cannot copy `this` or `super` into the function because they will be bound
	// differently inside the function.
	bool isThisOrSuperProperty = prop->Expression->kind == Kind::ThisKeyword || prop->Expression->kind == Kind::SuperKeyword;
	if (isThisOrSuperProperty || !isSimpleCopiableExpression(prop->Expression)) {
		receiver = factory()->newTempVariable(printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsReservedInNestedScopes,
		});
		emitContext()->addVariableDeclaration(receiver);
		pendingExpressions.push_back(
			factory()->newAssignmentExpression(receiver, visitor()->visitNode(prop->Expression))
		);
	}
	Node* assignExpr = createPrivateIdentifierAssignment(info, receiver, parameter, Kind::EqualsToken);
	return factory()->newAssignmentTargetWrapper(parameter, assignExpr);
}

Node* classFieldsTransformer::visitAssignmentElement(Node* node) {
	// 13.15.5.5 RS: IteratorDestructuringAssignmentEvaluation
	//   AssignmentElement : DestructuringAssignmentTarget Initializer?
	//     ...
	//     4. If |Initializer| is present and _value_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) and IsIdentifierRef of |DestructuringAssignmentTarget| are both *true*, then
	//           i. Let _v_ be ? NamedEvaluation of |Initializer| with argument _lref_.[[ReferencedName]].
	//     ...

	if (isNamedEvaluationAnd(emitContext(), node, isAnonymousClassNeedingAssignedName)) {
		node = transformNamedEvaluation(emitContext(), node, /*ignoreEmptyStringLiteral*/ false, /*assignedName*/ "");
	}
	if (isAssignmentExpression(node, /*excludeCompoundAssignment*/ true)) {
		Node* left = visitDestructuringAssignmentTarget(node->as<BinaryExpression>()->Left);
		Node* right = visitor()->visitNode(node->as<BinaryExpression>()->Right);
		return factory()->updateBinaryExpression(
			node->as<BinaryExpression>(),
			nullptr,
			left,
			nullptr,
			node->as<BinaryExpression>()->OperatorToken,
			right
		);
	}
	return visitDestructuringAssignmentTarget(node);
}

Node* classFieldsTransformer::visitAssignmentRestElement(Node* node) {
	SpreadElement* spread = node->as<SpreadElement>();
	if (isLeftHandSideExpression(spread->Expression)) {
		Node* expr = visitDestructuringAssignmentTarget(spread->Expression);
		return factory()->updateSpreadElement(spread, expr);
	}
	return visitor()->visitEachChild(node);
}

Node* classFieldsTransformer::visitArrayAssignmentElement(Node* node) {
	if (isArrayBindingOrAssignmentElement(node)) {
		if (isSpreadElement(node)) {
			return visitAssignmentRestElement(node);
		}
		if (node->kind != Kind::OmittedExpression) {
			return visitAssignmentElement(node);
		}
	}
	return visitor()->visitEachChild(node);
}

Node* classFieldsTransformer::visitAssignmentProperty(Node* node) {
	// AssignmentProperty : PropertyName `:` AssignmentElement
	// AssignmentElement : DestructuringAssignmentTarget Initializer?

	// 13.15.5.6 RS: KeyedDestructuringAssignmentEvaluation
	//   AssignmentElement : DestructuringAssignmentTarget Initializer?
	//     ...
	//     3. If |Initializer| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousfunctionDefinition(|Initializer|) and IsIdentifierRef of |DestructuringAssignmentTarget| are both *true*, then
	//           i. Let _rhsValue_ be ? NamedEvaluation of |Initializer| with argument _lref_.[[ReferencedName]].
	//     ...

	PropertyAssignment* prop = node->as<PropertyAssignment>();
	Node* name = visitor()->visitNode(prop->name);
	Node* init = prop->Initializer;
	if (isAssignmentExpression(init, /*excludeCompoundAssignment*/ true)) {
		Node* assignElem = visitAssignmentElement(init);
		return factory()->updatePropertyAssignment(prop, nullptr, name, nullptr, nullptr, assignElem);
	}
	if (isLeftHandSideExpression(init)) {
		Node* target = visitDestructuringAssignmentTarget(init);
		return factory()->updatePropertyAssignment(prop, nullptr, name, nullptr, nullptr, target);
	}
	return visitor()->visitEachChild(node);
}

Node* classFieldsTransformer::visitShorthandAssignmentProperty(Node* node) {
	// AssignmentProperty : IdentifierReference Initializer?

	// 13.15.5.3 RS: PropertyDestructuringAssignmentEvaluation
	//   AssignmentProperty : IdentifierReference Initializer?
	//     ...
	//     4. If |Initializer?| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument _P_.
	//     ...

	if (isNamedEvaluationAnd(emitContext(), node, isAnonymousClassNeedingAssignedName)) {
		node = transformNamedEvaluation(emitContext(), node, /*ignoreEmptyStringLiteral*/ false, /*assignedName*/ "");
	}
	return visitor()->visitEachChild(node);
}

Node* classFieldsTransformer::visitAssignmentRestProperty(Node* node) {
	SpreadAssignment* spread = node->as<SpreadAssignment>();
	if (isLeftHandSideExpression(spread->Expression)) {
		Node* expr = visitDestructuringAssignmentTarget(spread->Expression);
		return factory()->updateSpreadAssignment(spread, expr);
	}
	return visitor()->visitEachChild(node);
}

Node* classFieldsTransformer::visitObjectAssignmentElement(Node* node) {
	debugAssert(node != nullptr && isObjectBindingOrAssignmentElement(node));
	if (isSpreadAssignment(node)) {
		return visitAssignmentRestProperty(node);
	}
	if (isShorthandPropertyAssignment(node)) {
		return visitShorthandAssignmentProperty(node);
	}
	if (isPropertyAssignment(node)) {
		return visitAssignmentProperty(node);
	}
	return visitor()->visitEachChild(node);
}

Node* classFieldsTransformer::visitAssignmentPattern(Node* node) {
	if (isArrayLiteralExpression(node)) {
		// Transforms private names in destructuring assignment array bindings.
		// Transforms SuperProperty assignments in destructuring assignment array bindings in static initializers.
		//
		// Source:
		// ([ this.#myProp ] = [ "hello" ]);
		//
		// Transformation:
		// [ { set value(x) { this.#myProp = x; } }.value ] = [ "hello" ];
		return factory()->updateArrayLiteralExpression(
			node->as<ArrayLiteralExpression>(),
			arrayAssignmentElementVisitor->visitNodes(node->as<ArrayLiteralExpression>()->Elements),
			node->as<ArrayLiteralExpression>()->MultiLine
		);
	}
	// Transforms private names in destructuring assignment object bindings.
	// Transforms SuperProperty assignments in destructuring assignment object bindings in static initializers.
	//
	// Source:
	// ({ stringProperty: this.#myProp } = { stringProperty: "hello" });
	//
	// Transformation:
	// ({ stringProperty: { set value(x) { this.#myProp = x; } }.value }) = { stringProperty: "hello" };
	return factory()->updateObjectLiteralExpression(
		node->as<ObjectLiteralExpression>(),
		objectAssignmentElementVisitor->visitNodes(node->as<ObjectLiteralExpression>()->Properties),
		node->as<ObjectLiteralExpression>()->MultiLine
	);
}

bool classFieldsTransformer::isReservedPrivateName(Node* node) {
	return !(isPrivateIdentifier(node) && emitContext()->hasAutoGenerateInfo(node)) && node->text() == "#constructor";
}

std::vector<Node*> classFieldsTransformer::getProperties(Node* node, bool requireInitializer, bool isStatic) {
	std::vector<Node*> result;
	for (Node* member : node->members()) {
		if (isPropertyDeclaration(member) &&
			(!requireInitializer || member->initializer() != nullptr) &&
			hasStaticModifier(member) == isStatic) {
			result.push_back(member);
		}
	}
	return result;
}

std::vector<Node*> classFieldsTransformer::getStaticPropertiesAndClassStaticBlock(Node* node) {
	std::vector<Node*> result;
	for (Node* member : node->members()) {
		if (isClassStaticBlockDeclaration(member) || (isPropertyDeclaration(member) && hasStaticModifier(member))) {
			result.push_back(member);
		}
	}
	return result;
}

std::pair<Node*, Node*> classFieldsTransformer::createCallBinding(Node* node) {
	Node* thisArg;
	Node* target;
	if (isSuperProperty(node)) {
		return {factory()->newThisExpression(), node};
	}
	if (isPropertyAccessExpression(node)) {
		PropertyAccessExpression* expr = node->as<PropertyAccessExpression>();
		if (shouldBeCapturedInTempVariable(expr->Expression)) {
			thisArg = factory()->newTempVariable();
			emitContext()->addVariableDeclaration(thisArg);
			target = factory()->newPropertyAccessExpression(
				factory()->newParenthesizedExpression( // TODO: do we even need these?
					factory()->newAssignmentExpression(thisArg, expr->Expression)
				),
				nullptr,
				expr->name,
				NodeFlagsNone
			);
			return {thisArg, target};
		}
		return {expr->Expression, node};
	}
	thisArg = factory()->newVoidZeroExpression();
	target = node;
	return {thisArg, target};
}

Node* classFieldsTransformer::createAccessorPropertyGetRedirector(PropertyDeclaration* node, ModifierList* modifiers,
                                                                  Node* name, Node* receiver) {
	printer::AutoGenerateOptions storageOpts;
	storageOpts.Suffix = "_accessor_storage";
	Node* backingFieldName = factory()->newGeneratedPrivateNameForNode(node->name, storageOpts);
	Node* returnExpr = factory()->newPropertyAccessExpression(
		receiver,
		nullptr,
		backingFieldName,
		NodeFlagsNone
	);
	Node* returnStmt = factory()->newReturnStatement(returnExpr);
	Node* body = factory()->newBlock(factory()->newNodeList({returnStmt}), false);
	return factory()->newGetAccessorDeclaration(
		modifiers,
		name,
		nullptr, /*typeParameters*/
		factory()->newNodeList({}),
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		body
	);
}

Node* classFieldsTransformer::createAccessorPropertySetRedirector(PropertyDeclaration* node, ModifierList* modifiers,
                                                                  Node* name, Node* receiver) {
	printer::AutoGenerateOptions storageOpts;
	storageOpts.Suffix = "_accessor_storage";
	Node* backingFieldName = factory()->newGeneratedPrivateNameForNode(node->name, storageOpts);
	Node* valueParam = factory()->newParameterDeclaration(
		nullptr, /*modifiers*/
		nullptr, /*dotDotDotToken*/
		factory()->newIdentifier("value"),
		nullptr, /*questionToken*/
		nullptr, /*typeNode*/
		nullptr /*initializer*/
	);
	Node* assignExpr = factory()->newAssignmentExpression(
		factory()->newPropertyAccessExpression(
			receiver,
			nullptr,
			backingFieldName,
			NodeFlagsNone
		),
		factory()->newIdentifier("value")
	);
	Node* exprStmt = factory()->newExpressionStatement(assignExpr);
	Node* body = factory()->newBlock(factory()->newNodeList({exprStmt}), false);
	return factory()->newSetAccessorDeclaration(
		modifiers,
		name,
		nullptr, /*typeParameters*/
		factory()->newNodeList({valueParam}),
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		body
	);
}

namespace {

Node* createPrivateStaticFieldInitializer(printer::NodeFactory* factory, Node* variableName, Node* initializer) {
	if (initializer == nullptr) {
		initializer = factory->newVoidZeroExpression();
	}
	return factory->newAssignmentExpression(
		variableName,
		factory->newObjectLiteralExpression(
			factory->newNodeList({
				factory->newPropertyAssignment(nullptr, factory->newIdentifier("value"), nullptr, nullptr, initializer),
			}),
			false
		)
	);
}

Node* createPrivateInstanceFieldInitializer(printer::NodeFactory* factory, Node* receiver, Node* initializer, Node* weakMapName) {
	if (initializer == nullptr) {
		initializer = factory->newVoidZeroExpression();
	}
	return factory->newMethodCall(weakMapName, factory->newIdentifier("set"), {receiver, initializer});
}

Node* createPrivateInstanceMethodInitializer(printer::NodeFactory* factory, Node* receiver, Node* weakSetName) {
	return factory->newMethodCall(weakSetName, factory->newIdentifier("add"), {receiver});
}

bool isStaticPropertyDeclarationOrClassStaticBlock(Node* node) {
	return isClassStaticBlockDeclaration(node) ||
		(isPropertyDeclaration(node) && hasStaticModifier(node));
}


bool isNonStaticMethodOrAccessorWithPrivateName(Node* member) {
	return !isStatic(member) &&
		(isMethodOrAccessor(member) || isAutoAccessorPropertyDeclaration(member)) &&
		isPrivateIdentifier(member->name());
}

Node* createMemberAccessForPropertyName(printer::NodeFactory* factory, printer::EmitContext* emitContext, Node* receiver, Node* name, Node* location) {
	if (isComputedPropertyName(name)) {
		Node* expression = factory->newElementAccessExpression(receiver, nullptr, name->expression(), NodeFlagsNone);
		expression->loc = location->loc;
		return expression;
	}
	Node* expression;
	if (isIdentifier(name) || isPrivateIdentifier(name)) {
		expression = factory->newPropertyAccessExpression(receiver, nullptr, name, NodeFlagsNone);
	} else {
		// string or numeric literal
		expression = factory->newElementAccessExpression(receiver, nullptr, name, NodeFlagsNone);
	}
	emitContext->setCommentRange(expression, name->loc);
	emitContext->setSourceMapRange(expression, name->loc);
	emitContext->addEmitFlags(expression, printer::EFNoNestedSourceMaps);
	return expression;
}

bool shouldBeCapturedInTempVariable(Node* node) {
	Node* target = skipParentheses(node);
	switch (target->kind) {
	case Kind::Identifier:
	case Kind::ThisKeyword:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::StringLiteral:
		return false;
	default:
		return true;
	}
}

// flattenCommaList decomposes a comma expression tree into a sequence of expressions.
void flattenCommaList(Node* node, const std::function<bool(Node*)>& yield) {
	flattenCommaListWorker(node, yield);
}

bool flattenCommaListWorker(Node* node, const std::function<bool(Node*)>& yield) {
	if (isParenthesizedExpression(node) && nodeIsSynthesized(node)) {
		return flattenCommaListWorker(node->expression(), yield);
	} else if (isCommaExpression(node)) {
		return flattenCommaListWorker(node->as<BinaryExpression>()->Left, yield) &&
			flattenCommaListWorker(node->as<BinaryExpression>()->Right, yield);
	} else {
		return yield(node);
	}
}



}  // namespace

// classHasClassThisAssignment checks if a class has a static block that is a class-this assignment.
bool classHasClassThisAssignment(printer::EmitContext* emitContext, Node* node) {
	for (Node* member : node->members()) {
		if (isClassThisAssignmentBlock(emitContext, member)) {
			return true;
		}
	}
	return false;
}

BinaryExpression* findComputedPropertyNameCacheAssignment(printer::EmitContext* emitContext, Node* name) {
	Node* node = name->expression();
	while (true) {
		node = skipOuterExpressions(node, 0);
		if (isBinaryExpression(node) && node->as<BinaryExpression>()->OperatorToken->kind == Kind::CommaToken) {
			node = node->as<BinaryExpression>()->Right;
			continue;
		}
		if (isAssignmentExpression(node, /*excludeCompoundAssignment*/ true) && isIdentifier(node->as<BinaryExpression>()->Left)) {
			return node->as<BinaryExpression>();
		}
		break;
	}
	return nullptr;
}

Node* expandPreOrPostfixIncrementOrDecrementExpression(printer::NodeFactory* factory, printer::EmitContext* emitContext, Node* node, Node* expression, Node* resultVariable) {
	Kind operator_;
	Node* operand;
	if (isPrefixUnaryExpression(node)) {
		operator_ = node->as<PrefixUnaryExpression>()->Operator;
		operand = node->as<PrefixUnaryExpression>()->Operand;
	} else {
		operator_ = node->as<PostfixUnaryExpression>()->Operator;
		operand = node->as<PostfixUnaryExpression>()->Operand;
	}

	Node* temp = factory->newTempVariable();
	emitContext->addVariableDeclaration(temp);
	expression = factory->newAssignmentExpression(temp, expression);
	expression->loc = operand->loc;

	Node* operation;
	if (isPrefixUnaryExpression(node)) {
		operation = factory->newPrefixUnaryExpression(operator_, temp);
	} else {
		operation = factory->newPostfixUnaryExpression(temp, operator_);
	}
	operation->loc = node->loc;

	if (resultVariable != nullptr) {
		operation = factory->newAssignmentExpression(resultVariable, operation);
		operation->loc = node->loc;
	}

	expression = factory->newCommaExpression(expression, operation);
	expression->loc = node->loc;

	if (isPostfixUnaryExpression(node)) {
		expression = factory->newCommaExpression(expression, temp);
		expression->loc = node->loc;
	}

	return expression;
}

}  // namespace tsc::transformers::estransforms
