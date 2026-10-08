// Port of tsc/internal/transformers/estransforms/esdecorator.go — TC39
// (esDecorators) class/member decorator transform.
#include "internal/transformers/estransforms/estransforms.h"

#include "internal/collections/collections.h"
#include "internal/scanner/scanner.h"

namespace tsc::transformers::estransforms {

// Class/Decorator evaluation order, as it pertains to this transformer:
//
// 1. Class decorators are evaluated outside of the private name scope of the
//    class.
//    - 15.8.20 RS: BindingClassDeclarationEvaluation
//    - 15.8.21 RS: Evaluation
//    - 8.3.5 RS: NamedEvaluation
// 2. ClassHeritage clause is evaluated outside of the private name scope of
//    the class.
//    - 15.8.19 RS: ClassDefinitionEvaluation, Step 8.c.
// 3. The name of the class is assigned.
// 4. For each member:
//    a. Member Decorators are evaluated.
//       - 15.8.19 RS: ClassDefinitionEvaluation, Step 23.
//       - Probably 15.7.13 RS: ClassElementEvaluation, but it's missing from
//         spec text.
//    b. Computed Property name is evaluated
//       - 15.8.19 RS: ClassDefinitionEvaluation, Step 23.
//       - 15.8.15 RS: ClassFieldDefinitionEvaluation, Step 1.
//       - 15.4.5 RS: MethodDefinitionEvaluation, Step 1.
// 5. Static non-field (method/getter/setter/auto-accessor) element decorators
//    are applied
// 6. Non-static non-field (method/getter/setter/auto-accessor) element
//    decorators are applied
// 7. Static field (excl. auto-accessor) element decorators are applied
// 8. Non-static field (excl. auto-accessor) element decorators are applied
// 9. Class decorators are applied
// 10. Class binding is initialized
// 11. Static method extra initializers are evaluated
// 12. Static fields are initialized (incl. extra initializers) and static
//     blocks are evaluated
// 13. Class extra initializers are evaluated
//
// Class constructor evaluation order, as it pertains to this transformer:
//
// 1. Instance method extra initializers are evaluated
// 2. For each instance field/auto-accessor:
//    a. The field is initialized and defined on the instance.
//    b. Extra initializers for the field are evaluated.

namespace {

void debugFail_(const char* msg) { (void)msg; TSC_UNREACHABLE(msg); }

void debugAssert_(bool cond, const char* msg) {
	if (!cond) TSC_UNREACHABLE(msg);
}

void debugAssert_(bool cond) {
	if (!cond) TSC_UNREACHABLE("debug.Assert failed");
}

// ast.utilities.go:112 — IsCompoundAssignment (file-local replica; the
// checker's copy is TU-local in checker_expressions_c.cpp).
bool isCompoundAssignment_(Kind kind) {
	return kind >= KindFirstCompoundAssignment && kind <= KindLastCompoundAssignment;
}

// ast.utilities.go:140 — IsObjectBindingOrAssignmentElement.
bool isObjectBindingOrAssignmentElement(Node* node) {
	switch (node->kind) {
	case Kind::BindingElement:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::SpreadAssignment:
		return true;
	default:
		break;
	}
	return false;
}

// ast.utilities.go:150 — IsArrayBindingOrAssignmentElement.
bool isObjectLiteralElement(Node* element) {
	switch (element->kind) {
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::SpreadAssignment:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return true;
	default:
		return false;
	}
}

bool isArrayBindingOrAssignmentElement(Node* node) {
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
		break;
	}
	return isAssignmentExpression(node, true /*excludeCompoundAssignment*/);
}

// lexicalEntryKind discriminates the kind of lexical scope entry.
enum class lexicalEntryKind {
	Class,
	ClassElement,
	Name,
	Other,
};

struct classInfo;

// lexicalEntry represents a single entry in the lexical scope stack used to
// track nested class declarations and their state during transformation.
struct lexicalEntry {
	lexicalEntryKind kind;
	lexicalEntry* next = nullptr;
	classInfo* classInfoData = nullptr;
	std::vector<Node*> savedPendingExpressions;
	Node* classThisData = nullptr;
	Node* classSuperData = nullptr;
	int depth = 0;
};

// memberInfo stores decoration-related data for a single class element.
struct memberInfo {
	Node* memberDecoratorsName = nullptr;        // used in class definition step 4.a
	Node* memberInitializersName = nullptr;      // used in class definition step 12 and constructor evaluation step 2.a
	Node* memberExtraInitializersName = nullptr; // used in class definition step 12 and constructor evaluation step 2.b
	Node* memberDescriptorName = nullptr;
};

// classInfo stores all transformation data for a single decorated class.
struct classInfo {
	Node* class_ = nullptr;
	Node* classDecoratorsName = nullptr;        // used in class definition step 2
	Node* classDescriptorName = nullptr;        // used in class definition step 10
	Node* classExtraInitializersName = nullptr; // used in class definition step 13
	Node* classThis = nullptr;                  // `_classThis`, if needed.
	Node* classSuper = nullptr;                 // `_classSuper`, if needed.
	Node* metadataReference = nullptr;
	collections::OrderedMap<Node*, memberInfo*>
		memberInfos;                          // used in class definition step 4.a, 12, and constructor evaluation
	Node* instanceMethodExtraInitializersName =
		nullptr;                              // used in constructor evaluation step 1
	Node* staticMethodExtraInitializersName =
		nullptr;                              // used in class definition step 11
	std::vector<Node*> staticNonFieldDecorationStatements;
	std::vector<Node*> nonStaticNonFieldDecorationStatements;
	std::vector<Node*> staticFieldDecorationStatements;
	std::vector<Node*> nonStaticFieldDecorationStatements;
	bool hasStaticInitializers = false;
	bool hasNonAmbientInstanceFields = false;
	bool hasStaticPrivateClassElements = false;
	std::vector<Node*> pendingStaticInitializers;
	std::vector<Node*> pendingInstanceInitializers;
};

struct partialResult {
	ModifierList* modifiers = nullptr;
	Node* referencedName = nullptr;
	Node* name = nullptr;
	Node* initializersName = nullptr;
	Node* extraInitializersName = nullptr;
	Node* descriptorName = nullptr;
	Node* thisArg = nullptr;
};

// createDescriptorFunc — esdecorator.go:1242
using createDescriptorFunc = std::function<Node*(Node*, ModifierList*)>;

struct esDecoratorTransformer : Transformer {
	const CompilerOptions* compilerOptions = nullptr;
	lexicalEntry* top = nullptr;
	classInfo* classInfoStack = nullptr;
	Node* classThis = nullptr;
	Node* classSuper = nullptr;
	std::vector<Node*> pendingExpressions;
	Node* outerThis = nullptr;
	bool shouldTransformPrivateStaticElementsInFile = false;
	NodeVisitor* outerThisVisitor = nullptr;
	NodeVisitor* discardedVisitor = nullptr;
	NodeVisitor* modifierVisitor = nullptr;
	NodeVisitor* exportStrippingModifierVisitor = nullptr;
	NodeVisitor* classElementVisitor = nullptr;
	NodeVisitor* nonConstructorClassElementVisitor = nullptr;
	NodeVisitor* constructorClassElementVisitor = nullptr;
	NodeVisitor* arrayAssignmentVisitor = nullptr;
	NodeVisitor* objectAssignmentVisitor = nullptr;
	NodeVisitor* staticOnlyModifierVisitor = nullptr;
	NodeVisitor* asyncOnlyModifierVisitor = nullptr;
	NodeVisitor* accessorStrippingModifierVisitor = nullptr;

	void updateState();
	void enterClass(classInfo* ci);
	void exitClass();
	void enterClassElement(Node* node);
	void exitClassElement();
	void enterName();
	void exitName();
	void enterOther();
	void exitOther();

	Node* visitSourceFile(SourceFile* node);
	Node* outerThisVisit(Node* n);
	bool shouldVisitNode(Node* node);
	Node* visit(Node* node);
	Node* modifierVisitorVisit(Node* node);
	Node* classElementVisitorVisit(Node* node);
	Node* discardedValueVisit(Node* node);
	Node* nonConstructorClassElementVisit(Node* node);
	Node* constructorClassElementVisit(Node* node);
	Node* exportStrippingModifierVisit(Node* node);

	Node* createHelperVariable(Node* node, const std::string& suffix);
	Node* createLet(Node* name, Node* initializer);
	classInfo* createClassInfo(Node* node);
	Node* transformClassLike(Node* node);
	std::vector<Node*> emitMemberInfoDeclarations(classInfo* ci,
	                                              bool isStatic_);
	Node* visitClassDeclaration(ClassDeclaration* node);
	Node* visitClassExpression(ClassExpression* node);
	std::vector<Node*> prepareConstructor(classInfo* ci);
	std::vector<Node*> transformConstructorBodyWorker(
		std::vector<Node*> statementsOut, std::vector<Node*> statementsIn,
		int statementOffset, std::vector<int> superPath, int superPathDepth,
		const std::vector<Node*>& initializerStatements);
	Node* visitConstructorDeclaration(Node* node);
	Node* finishClassElement(Node* updated, Node* original);
	partialResult partialTransformClassElement(
		Node* member, classInfo* ci, const createDescriptorFunc& createDescriptor);
	void appendDecorationStatement(classInfo* ci, Node* member, Node* stmt);
	Node* visitMethodDeclaration(Node* node);
	Node* visitGetAccessorDeclaration(Node* node);
	Node* visitSetAccessorDeclaration(Node* node);
	Node* visitClassStaticBlockDeclaration(Node* node);
	Node* visitPropertyDeclaration(Node* node);
	Node* visitThisExpression(Node* node);
	Node* visitCallExpression(Node* node);
	Node* visitTaggedTemplateExpression(Node* node);
	Node* visitPropertyAccessExpression(Node* node);
	Node* visitElementAccessExpression(Node* node);
	Node* visitParameterDeclaration(ParameterDeclaration* node);
	Node* visitNamedEvaluationSite(Node* node, Node* classExpr);
	Node* visitForStatement(Node* node);
	Node* visitExpressionStatement(Node* node);
	Node* visitBinaryExpression(Node* node, bool discarded);
	Node* visitPreOrPostfixUnaryExpression(Node* node, bool discarded);
	std::pair<Node*, Node*> visitReferencedPropertyName(Node* node);
	Node* visitPropertyName(Node* node);
	Node* visitComputedPropertyName(Node* node);
	Node* visitDestructuringAssignmentTarget(Node* node);
	Node* visitAssignmentElement(Node* node);
	Node* visitAssignmentRestElement(Node* node);
	Node* visitArrayAssignmentElement(Node* node);
	Node* visitAssignmentPropertyNode(Node* node);
	Node* visitShorthandAssignmentProperty(Node* node);
	Node* visitAssignmentRestProperty(Node* node);
	Node* visitObjectAssignmentElement(Node* node);
	Node* visitAssignmentPattern(Node* node);
	Node* visitExportAssignment(Node* node);
	Node* visitParenthesizedExpression(Node* node, bool discarded);
	Node* visitPartiallyEmittedExpression(Node* node, bool discarded);
	Node* prependExpressions(std::vector<Node*> pending, Node* expression);
	Node* injectPendingExpressions(Node* expression);
	Node* injectPendingInitializers(classInfo* ci, bool isStatic_,
	                                Node* expression);
	std::vector<Node*> transformAllDecoratorsOfDeclaration(
		const std::vector<Node*>& decorators);
	Node* transformDecorator(Node* decorator);
	std::pair<Node*, Node*> createCallBinding(Node* expression);
	bool shouldBeCapturedInTempVariable(Node* node);
	Node* createDescriptorMethod(Node* original, Node* name,
	                           ModifierList* modifiers, Node* asteriskToken,
	                           const std::string& kind, NodeList* parameters,
	                           Node* body);
	Node* createMethodDescriptorObject(Node* member, ModifierList* modifiers);
	Node* createGetAccessorDescriptorObject(Node* member,
	                                        ModifierList* modifiers);
	Node* createSetAccessorDescriptorObject(Node* member,
	                                        ModifierList* modifiers);
	Node* createAccessorPropertyDescriptorObject(Node* member,
	                                             ModifierList* modifiers);
	Node* createMethodDescriptorForwarder(ModifierList* modifiers, Node* name,
	                                      Node* descriptorName);
	Node* createGetAccessorDescriptorForwarder(ModifierList* modifiers,
	                                           Node* name,
	                                           Node* descriptorName);
	Node* createSetAccessorDescriptorForwarder(ModifierList* modifiers,
	                                           Node* name,
	                                           Node* descriptorName);
	Node* createMetadata(Node* name, Node* classSuper);
	Node* createSymbolMetadata(Node* target, Node* value);
	Node* createSymbolMetadataReference(Node* classSuper);
};

}  // namespace

// Forward declarations for package-level helpers defined further below.
Node* injectClassThisAssignmentIfMissing(printer::EmitContext* ec,
                                         printer::NodeFactory* f, Node* node,
                                         Node* classThis);
bool isAnonymousClassNeedingAssignedName(Node* node);
bool canIgnoreEmptyStringLiteralInAssignedName(Node* node);

// newESDecoratorTransformer — esdecorator.go:124
Transformer* newESDecoratorTransformer(TransformOptions* opts) {
	// When experimentalDecorators is set, the legacy decorator transformer
	// handles all decorators. When targeting ESNext with
	// useDefineForClassFields, there's nothing to transform. In either case
	// every node would be returned unchanged, so skip entirely.
	if (tristateIsTrue(opts->CompilerOptions->ExperimentalDecorators) ||
		(opts->CompilerOptions->GetEmitScriptTarget() >=
			 ScriptTarget::ESNext &&
		 opts->CompilerOptions->GetUseDefineForClassFields())) {
		return nullptr;
	}
	auto* tx = new esDecoratorTransformer();
	tx->compilerOptions = opts->CompilerOptions;
	Transformer* result = tx->newTransformer(
		[tx](Node* node) { return tx->visit(node); }, opts->Context);
	printer::EmitContext* ec = tx->emitContext();
	tx->outerThisVisitor = ec->newNodeVisitor(
		[tx](Node* node) { return tx->outerThisVisit(node); });
	tx->discardedVisitor = ec->newNodeVisitor(
		[tx](Node* node) { return tx->discardedValueVisit(node); });
	tx->modifierVisitor = ec->newNodeVisitor(
		[tx](Node* node) { return tx->modifierVisitorVisit(node); });
	tx->exportStrippingModifierVisitor = ec->newNodeVisitor(
		[tx](Node* node) { return tx->exportStrippingModifierVisit(node); });
	tx->classElementVisitor = ec->newNodeVisitor(
		[tx](Node* node) { return tx->classElementVisitorVisit(node); });
	tx->nonConstructorClassElementVisitor = ec->newNodeVisitor(
		[tx](Node* node) { return tx->nonConstructorClassElementVisit(node); });
	tx->constructorClassElementVisitor = ec->newNodeVisitor(
		[tx](Node* node) { return tx->constructorClassElementVisit(node); });
	tx->arrayAssignmentVisitor = ec->newNodeVisitor(
		[tx](Node* node) { return tx->visitArrayAssignmentElement(node); });
	tx->objectAssignmentVisitor = ec->newNodeVisitor(
		[tx](Node* node) { return tx->visitObjectAssignmentElement(node); });
	tx->staticOnlyModifierVisitor =
		ec->newNodeVisitor([](Node* node) -> Node* {
			if (node->kind == Kind::StaticKeyword) {
				return node;
			}
			return nullptr;
		});
	tx->asyncOnlyModifierVisitor = ec->newNodeVisitor([](Node* node) -> Node* {
		if (node->kind == Kind::AsyncKeyword) {
			return node;
		}
		return nullptr;
	});
	tx->accessorStrippingModifierVisitor =
		ec->newNodeVisitor([](Node* node) -> Node* {
			if (node->kind == Kind::AccessorKeyword) {
				return nullptr;
			}
			return node;
		});
	return result;
}

// updateState — esdecorator.go:165
void esDecoratorTransformer::updateState() {
	classInfoStack = nullptr;
	classThis = nullptr;
	classSuper = nullptr;
	if (top == nullptr) {
		return;
	}
	switch (top->kind) {
	case lexicalEntryKind::Class:
		classInfoStack = top->classInfoData;
		break;
	case lexicalEntryKind::ClassElement:
		classInfoStack = top->next->classInfoData;
		classThis = top->classThisData;
		classSuper = top->classSuperData;
		break;
	case lexicalEntryKind::Name: {
		lexicalEntry* grandparent = top->next->next->next;
		if (grandparent != nullptr &&
			grandparent->kind == lexicalEntryKind::ClassElement) {
			classInfoStack = grandparent->next->classInfoData;
			classThis = grandparent->classThisData;
			classSuper = grandparent->classSuperData;
		}
		break;
	}
	case lexicalEntryKind::Other:
		break;
	}
}

// enterClass — esdecorator.go:189
void esDecoratorTransformer::enterClass(classInfo* ci) {
	auto* entry = new lexicalEntry();
	entry->kind = lexicalEntryKind::Class;
	entry->next = top;
	entry->classInfoData = ci;
	entry->savedPendingExpressions = pendingExpressions;
	top = entry;
	pendingExpressions.clear();
	updateState();
}

// exitClass — esdecorator.go:200
void esDecoratorTransformer::exitClass() {
	debugAssert_(top != nullptr && top->kind == lexicalEntryKind::Class,
	             "Incorrect value for top.kind. Expected top.kind to be "
	             "'class'.");
	pendingExpressions = top->savedPendingExpressions;
	top = top->next;
	updateState();
}

// enterClassElement — esdecorator.go:207
void esDecoratorTransformer::enterClassElement(Node* node) {
	debugAssert_(top != nullptr && top->kind == lexicalEntryKind::Class,
	             "Incorrect value for top.kind. Expected top.kind to be "
	             "'class'.");
	auto* entry = new lexicalEntry();
	entry->kind = lexicalEntryKind::ClassElement;
	entry->next = top;
	top = entry;
	if (isClassStaticBlockDeclaration(node) ||
		(isPropertyDeclaration(node) && hasStaticModifier(node))) {
		if (top->next->classInfoData != nullptr) {
			top->classThisData = top->next->classInfoData->classThis;
			top->classSuperData = top->next->classInfoData->classSuper;
		}
	}
	updateState();
}

// exitClassElement — esdecorator.go:222
void esDecoratorTransformer::exitClassElement() {
	debugAssert_(top != nullptr && top->kind == lexicalEntryKind::ClassElement,
	             "Incorrect value for top.kind. Expected top.kind to be "
	             "'class-element'.");
	debugAssert_(top->next != nullptr &&
	                 top->next->kind == lexicalEntryKind::Class,
	             "Incorrect value for top.next.kind. Expected top.next.kind to "
	             "be 'class'.");
	top = top->next;
	updateState();
}

// enterName — esdecorator.go:229
void esDecoratorTransformer::enterName() {
	debugAssert_(top != nullptr && top->kind == lexicalEntryKind::ClassElement,
	             "Incorrect value for top.kind. Expected top.kind to be "
	             "'class-element'.");
	auto* entry = new lexicalEntry();
	entry->kind = lexicalEntryKind::Name;
	entry->next = top;
	top = entry;
	updateState();
}

// exitName — esdecorator.go:238
void esDecoratorTransformer::exitName() {
	debugAssert_(top != nullptr && top->kind == lexicalEntryKind::Name,
	             "Incorrect value for top.kind. Expected top.kind to be "
	             "'name'.");
	top = top->next;
	updateState();
}

// enterOther — esdecorator.go:244
void esDecoratorTransformer::enterOther() {
	if (top != nullptr && top->kind == lexicalEntryKind::Other) {
		debugAssert_(pendingExpressions.empty());
		top->depth++;
	} else {
		auto* entry = new lexicalEntry();
		entry->kind = lexicalEntryKind::Other;
		entry->next = top;
		entry->savedPendingExpressions = pendingExpressions;
		top = entry;
		pendingExpressions.clear();
		updateState();
	}
}

// exitOther — esdecorator.go:259
void esDecoratorTransformer::exitOther() {
	debugAssert_(top != nullptr && top->kind == lexicalEntryKind::Other,
	             "Incorrect value for top.kind. Expected top.kind to be "
	             "'other'.");
	if (top->depth > 0) {
		debugAssert_(pendingExpressions.empty());
		top->depth--;
	} else {
		pendingExpressions = top->savedPendingExpressions;
		top = top->next;
		updateState();
	}
}

// visitSourceFile — esdecorator.go:271
Node* esDecoratorTransformer::visitSourceFile(SourceFile* node) {
	top = nullptr;
	shouldTransformPrivateStaticElementsInFile = false;
	Node* visited = visitor()->visitEachChild(node->asNode());
	for (printer::EmitHelper* helper :
		 emitContext()->readEmitHelpers()) {
		emitContext()->addEmitHelper(visited, helper);
	}
	if (shouldTransformPrivateStaticElementsInFile) {
		emitContext()->addEmitFlags(visited,
		                            printer::EFTransformPrivateStaticElements);
		shouldTransformPrivateStaticElementsInFile = false;
	}
	return visited;
}

// outerThisVisit — esdecorator.go:283
Node* esDecoratorTransformer::outerThisVisit(Node* n) {
	if ((n->subtreeFacts() & SubtreeContainsLexicalThis) == 0 &&
		n->kind != Kind::ThisKeyword) {
		return n;
	}
	if (n->kind == Kind::ThisKeyword) {
		if (outerThis == nullptr) {
			outerThis = factory()->newUniqueName(
				"_outerThis",
				printer::AutoGenerateOptions{
					printer::GeneratedIdentifierFlagsOptimistic, "", ""});
		}
		return outerThis;
	}
	return outerThisVisitor->visitEachChild(n);
}

// shouldVisitNode — esdecorator.go:298
bool esDecoratorTransformer::shouldVisitNode(Node* node) {
	return (node->subtreeFacts() & SubtreeContainsDecorators) != 0 ||
		(classThis != nullptr &&
		 (node->subtreeFacts() & SubtreeContainsLexicalThis) != 0) ||
		(classThis != nullptr && classSuper != nullptr &&
		 (node->subtreeFacts() & SubtreeContainsLexicalSuper) != 0);
}

// visit — esdecorator.go:304
Node* esDecoratorTransformer::visit(Node* node) {
	if (node->kind == Kind::SourceFile) {
		return visitSourceFile(node->as<SourceFile>());
	}
	if (!shouldVisitNode(node)) {
		return node;
	}
	switch (node->kind) {
	case Kind::Decorator:
		// Decorators are elided. In Strada, a separate `modifierVisitor` drops
		// decorators before they reach `visitor` via visitEachChild. Here,
		// `visit` serves as both visitors, so decorators from modifier lists
		// reach it directly.
		return nullptr;
	case Kind::ClassDeclaration:
		return visitClassDeclaration(node->as<ClassDeclaration>());
	case Kind::ClassExpression:
		return visitClassExpression(node->as<ClassExpression>());
	case Kind::Constructor:
	case Kind::PropertyDeclaration:
	case Kind::ClassStaticBlockDeclaration:
		debugFail_("Not supported outside of a class. Use "
		           "'classElementVisitor' instead.");
		return nullptr;
	case Kind::Parameter:
		return visitParameterDeclaration(node->as<ParameterDeclaration>());
	// Support NamedEvaluation to ensure the correct class name for class
	// expressions.
	case Kind::BinaryExpression:
		return visitBinaryExpression(node, false /*discarded*/);
	case Kind::PropertyAssignment:
	case Kind::VariableDeclaration:
	case Kind::BindingElement:
		return visitNamedEvaluationSite(node, node->initializer());
	case Kind::ExportAssignment:
		return visitExportAssignment(node);
	case Kind::ThisKeyword:
		return visitThisExpression(node);
	case Kind::ForStatement:
		return visitForStatement(node);
	case Kind::ExpressionStatement:
		return visitExpressionStatement(node);
	case Kind::ParenthesizedExpression:
		return visitParenthesizedExpression(node, false /*discarded*/);
	case Kind::PartiallyEmittedExpression:
		return visitPartiallyEmittedExpression(node, false /*discarded*/);
	case Kind::CallExpression:
		return visitCallExpression(node);
	case Kind::TaggedTemplateExpression:
		return visitTaggedTemplateExpression(node);
	case Kind::PrefixUnaryExpression:
	case Kind::PostfixUnaryExpression:
		return visitPreOrPostfixUnaryExpression(node, false /*discarded*/);
	case Kind::PropertyAccessExpression:
		return visitPropertyAccessExpression(node);
	case Kind::ElementAccessExpression:
		return visitElementAccessExpression(node);
	case Kind::ComputedPropertyName:
		return visitComputedPropertyName(node);
	case Kind::MethodDeclaration:
	case Kind::SetAccessor:
	case Kind::GetAccessor:
	case Kind::FunctionExpression:
	case Kind::FunctionDeclaration: {
		enterOther();
		Node* result = visitor()->visitEachChild(node);
		exitOther();
		return result;
	}
	default:
		return visitor()->visitEachChild(node);
	}
}

// modifierVisitorVisit — esdecorator.go:369
Node* esDecoratorTransformer::modifierVisitorVisit(Node* node) {
	if (node->kind == Kind::Decorator) {
		return nullptr;
	}
	return node;
}

// classElementVisitorVisit — esdecorator.go:376
Node* esDecoratorTransformer::classElementVisitorVisit(Node* node) {
	switch (node->kind) {
	case Kind::Constructor:
		return visitConstructorDeclaration(node);
	case Kind::MethodDeclaration:
		return visitMethodDeclaration(node);
	case Kind::GetAccessor:
		return visitGetAccessorDeclaration(node);
	case Kind::SetAccessor:
		return visitSetAccessorDeclaration(node);
	case Kind::PropertyDeclaration:
		return visitPropertyDeclaration(node);
	case Kind::ClassStaticBlockDeclaration:
		return visitClassStaticBlockDeclaration(node);
	default:
		return visit(node);
	}
}

// discardedValueVisit — esdecorator.go:395
Node* esDecoratorTransformer::discardedValueVisit(Node* node) {
	switch (node->kind) {
	case Kind::PrefixUnaryExpression:
	case Kind::PostfixUnaryExpression:
		return visitPreOrPostfixUnaryExpression(node, true /*discarded*/);
	case Kind::BinaryExpression:
		return visitBinaryExpression(node, true /*discarded*/);
	case Kind::ParenthesizedExpression:
		return visitParenthesizedExpression(node, true /*discarded*/);
	case Kind::PartiallyEmittedExpression:
		return visitPartiallyEmittedExpression(node, true /*discarded*/);
	default:
		return visit(node);
	}
}

// nonConstructorClassElementVisit — esdecorator.go:410
Node* esDecoratorTransformer::nonConstructorClassElementVisit(Node* node) {
	if (isConstructorDeclaration(node)) {
		return node;  // skip constructors in pass 1
	}
	return classElementVisitorVisit(node);
}

// constructorClassElementVisit — esdecorator.go:417
Node* esDecoratorTransformer::constructorClassElementVisit(Node* node) {
	if (isConstructorDeclaration(node)) {
		return classElementVisitorVisit(node);
	}
	return node;
}

// exportStrippingModifierVisit — esdecorator.go:424
Node* esDecoratorTransformer::exportStrippingModifierVisit(Node* node) {
	if (node->kind == Kind::ExportKeyword) {
		return nullptr;
	}
	return modifierVisitorVisit(node);
}

// getHelperVariableName — esdecorator.go:431
std::string getHelperVariableName(printer::EmitContext* ec, Node* node) {
	Node* name = node->name();
	std::string declarationName;
	if (name != nullptr && isIdentifier(name) &&
		!isGeneratedIdentifier(ec, name)) {
		declarationName = name->text();
	} else if (name != nullptr && isPrivateIdentifier(name) &&
	           !ec->hasAutoGenerateInfo(name)) {
		std::string text = name->text();
		if (text.length() > 1) {
			declarationName = text.substr(1);
		}
	} else if (name != nullptr && isStringLiteral(name) &&
	           isIdentifierText(name->text(),
	                         LanguageVariant::Standard)) {
		declarationName = name->text();
	} else if (isClassLike(node)) {
		declarationName = "class";
	} else {
		declarationName = "member";
	}

	if (isGetAccessorDeclaration(node)) {
		declarationName = "get_" + declarationName;
	}
	if (isSetAccessorDeclaration(node)) {
		declarationName = "set_" + declarationName;
	}
	if (name != nullptr && isPrivateIdentifier(name)) {
		declarationName = "private_" + declarationName;
	}
	if (isStatic(node)) {
		declarationName = "static_" + declarationName;
	}
	return "_" + declarationName;
}

// createHelperVariable — esdecorator.go:464
Node* esDecoratorTransformer::createHelperVariable(Node* node,
                                                   const std::string& suffix) {
	return factory()->newUniqueName(
		getHelperVariableName(emitContext(), node) + "_" + suffix,
		printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsOptimistic |
			printer::GeneratedIdentifierFlagsReservedInNestedScopes, "", ""});
}

// createLet — esdecorator.go:471
Node* esDecoratorTransformer::createLet(Node* name, Node* initializer) {
	printer::NodeFactory* f = factory();
	return f->newVariableStatement(
		nullptr,
		f->newVariableDeclarationList(
			f->newNodeList({f->newVariableDeclaration(name, nullptr, nullptr,
			                                          initializer)}),
			NodeFlagsLet));
}

// createClassInfo — esdecorator.go:483
classInfo* esDecoratorTransformer::createClassInfo(Node* node) {
	printer::NodeFactory* f = factory();
	auto* ci = new classInfo();
	ci->class_ = node;
	ci->metadataReference = f->newUniqueName(
		"_metadata",
		printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsOptimistic |
			printer::GeneratedIdentifierFlagsFileLevel, "", ""});

	// Before visiting we perform a first pass to collect information we'll
	// need as we descend.

	// If the class itself is decorated, create a _classThis binding
	if (nodeIsDecorated(false, node, nullptr, nullptr)) {
		bool needsUniqueClassThis = false;
		for (Node* member : node->members()) {
			if ((isPrivateIdentifierClassElementDeclaration(member) ||
			     isAutoAccessorPropertyDeclaration(member)) &&
				hasStaticModifier(member)) {
				needsUniqueClassThis = true;
				break;
			}
		}
		// We do not mark _classThis as FileLevel if it may be reused by class
		// private fields, which requires the ability access the captured
		// `_classThis` of outer scopes.
		printer::GeneratedIdentifierFlags flags =
			printer::GeneratedIdentifierFlagsOptimistic |
			printer::GeneratedIdentifierFlagsFileLevel;
		if (needsUniqueClassThis) {
			flags = printer::GeneratedIdentifierFlagsOptimistic |
				printer::GeneratedIdentifierFlagsReservedInNestedScopes;
		}
		ci->classThis =
			f->newUniqueName("_classThis", printer::AutoGenerateOptions{flags, "", ""});
	}

	for (Node* member : node->members()) {
		if (isMethodOrAccessor(member) &&
			nodeOrChildIsDecorated(false, member, node, nullptr)) {
			if (hasStaticModifier(member)) {
				if (ci->staticMethodExtraInitializersName == nullptr) {
					ci->staticMethodExtraInitializersName = f->newUniqueName(
						"_staticExtraInitializers",
						printer::AutoGenerateOptions{
							printer::GeneratedIdentifierFlagsOptimistic |
							printer::GeneratedIdentifierFlagsFileLevel, "", ""});
					Node* renamedClassThis;
					if (ci->classThis != nullptr) {
						renamedClassThis = ci->classThis;
					} else {
						renamedClassThis = f->newThisExpression();
					}
					Node* initializer = f->newRunInitializersHelper(
						renamedClassThis,
						ci->staticMethodExtraInitializersName, nullptr);
					Node* nameRange = node->name();
					if (nameRange != nullptr) {
						emitContext()->setSourceMapRange(initializer,
						                                 nameRange->loc);
					} else {
						emitContext()->setSourceMapRange(
							initializer, moveRangePastDecorators(node));
					}
					ci->pendingStaticInitializers.push_back(initializer);
				}
			} else {
				if (ci->instanceMethodExtraInitializersName == nullptr) {
					ci->instanceMethodExtraInitializersName = f->newUniqueName(
						"_instanceExtraInitializers",
						printer::AutoGenerateOptions{
							printer::GeneratedIdentifierFlagsOptimistic |
							printer::GeneratedIdentifierFlagsFileLevel, "", ""});
					Node* initializer = f->newRunInitializersHelper(
						f->newThisExpression(),
						ci->instanceMethodExtraInitializersName, nullptr);
					Node* nameRange = node->name();
					if (nameRange != nullptr) {
						emitContext()->setSourceMapRange(initializer,
						                                 nameRange->loc);
					} else {
						emitContext()->setSourceMapRange(
							initializer, moveRangePastDecorators(node));
					}
					ci->pendingInstanceInitializers.push_back(initializer);
				}
			}
		}

		if (isClassStaticBlockDeclaration(member)) {
			if (!isClassNamedEvaluationHelperBlock(emitContext(), member)) {
				ci->hasStaticInitializers = true;
			}
		} else if (isPropertyDeclaration(member)) {
			if (hasStaticModifier(member)) {
				ci->hasStaticInitializers =
					ci->hasStaticInitializers ||
					member->initializer() != nullptr || hasDecorators(member);
			} else {
				ci->hasNonAmbientInstanceFields =
					ci->hasNonAmbientInstanceFields ||
					!hasSyntacticModifier(member, ModifierFlagsAmbient);
			}
		}

		if ((isPrivateIdentifierClassElementDeclaration(member) ||
		     isAutoAccessorPropertyDeclaration(member)) &&
			hasStaticModifier(member)) {
			ci->hasStaticPrivateClassElements = true;
		}

		// exit early if possible
		if (ci->staticMethodExtraInitializersName != nullptr &&
			ci->instanceMethodExtraInitializersName != nullptr &&
			ci->hasStaticInitializers && ci->hasNonAmbientInstanceFields &&
			ci->hasStaticPrivateClassElements) {
			break;
		}
	}

	return ci;
}

// transformClassLike — esdecorator.go:577
Node* esDecoratorTransformer::transformClassLike(Node* node) {
	printer::NodeFactory* f = factory();
	printer::EmitContext* ec = emitContext();

	ec->startVariableEnvironment();

	// When a class has class decorators we end up transforming it into a
	// statement that would otherwise give it an assigned name. If the class
	// doesn't have an assigned name, we'll give it an assigned name of `""`.
	if (!classHasDeclaredOrExplicitlyAssignedName(ec, node) &&
		classOrConstructorParameterIsDecorated(false, node)) {
		node = injectClassNamedEvaluationHelperBlockIfMissing(
			ec, node, f->newStringLiteral("", 0), nullptr);
	}

	Node* classReference = f->getLocalName(node, printer::AssignedNameOptions{});
	classInfo* ci = createClassInfo(node);
	std::vector<Node*> classDefinitionStatements;
	std::vector<Node*> leadingBlockStatements;
	std::vector<Node*> trailingBlockStatements;
	Node* syntheticConstructor = nullptr;
	NodeList* heritageClauses = nullptr;
	bool shouldTransformPrivateStaticElementsInClass = false;

	// 1. Class decorators are evaluated outside the private name scope of the
	// class.
	//
	// - Since class decorators don't have privileged access to private names
	//   defined inside the class, they must be evaluated outside of the class
	//   body.
	// - Since a class decorator can replace the class constructor, we must
	//   define a variable to keep track of the mutated class.
	// - Since a class decorator can add extra initializers, we must define a
	//   variable to keep track of extra initializers.
	std::vector<Node*> classDecorators =
		transformAllDecoratorsOfDeclaration(node->decorators());
	if (!classDecorators.empty()) {
		debugAssert_(ci->classThis != nullptr);

		ci->classDecoratorsName = f->newUniqueName(
			"_classDecorators",
			printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsOptimistic |
				printer::GeneratedIdentifierFlagsFileLevel, "", ""});
		ci->classDescriptorName = f->newUniqueName(
			"_classDescriptor",
			printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsOptimistic |
				printer::GeneratedIdentifierFlagsFileLevel, "", ""});
		ci->classExtraInitializersName = f->newUniqueName(
			"_classExtraInitializers",
			printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsOptimistic |
				printer::GeneratedIdentifierFlagsFileLevel, "", ""});

		Node* decoratorsArray = f->newArrayLiteralExpression(
			f->newNodeList(classDecorators), false);
		classDefinitionStatements.push_back(
			createLet(ci->classDecoratorsName, decoratorsArray));
		classDefinitionStatements.push_back(
			createLet(ci->classDescriptorName, nullptr));
		classDefinitionStatements.push_back(
			createLet(ci->classExtraInitializersName,
			          f->newArrayLiteralExpression(f->newNodeList({}), false)));
		classDefinitionStatements.push_back(createLet(ci->classThis, nullptr));

		if (!classDecorators.empty() && ci->hasStaticPrivateClassElements) {
			shouldTransformPrivateStaticElementsInClass = true;
			shouldTransformPrivateStaticElementsInFile = true;
		}
	}

	// 2. ClassHeritage clause is evaluated outside of the private name scope
	// of the class.
	Node* extendsClause = getHeritageClause(node, Kind::ExtendsKeyword);
	Node* extendsElement = nullptr;
	if (extendsClause != nullptr) {
		HeritageClause* hc = extendsClause->as<HeritageClause>();
		if (hc->Types != nullptr && !hc->Types->nodes.empty()) {
			extendsElement = hc->Types->nodes[0];
		}
	}
	Node* extendsExpression = nullptr;
	if (extendsElement != nullptr) {
		extendsExpression = visitor()->visitNode(
			extendsElement->as<ExpressionWithTypeArguments>()->Expression);
	}

	if (extendsExpression != nullptr) {
		// Rewrite `super` in static initializers so that we can use the
		// correct `this`.
		ci->classSuper = f->newUniqueName(
			"_classSuper",
			printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsOptimistic |
				printer::GeneratedIdentifierFlagsFileLevel, "", ""});

		// Ensure we do not give the class or function an assigned name due to
		// the variable by prefixing it with `0, `.
		Node* unwrapped = skipOuterExpressions(extendsExpression, OEKAll);
		Node* safeExtendsExpression = extendsExpression;
		if ((isClassExpression(unwrapped) && unwrapped->name() == nullptr) ||
			(isFunctionExpression(unwrapped) &&
			 unwrapped->name() == nullptr) ||
			isArrowFunction(unwrapped)) {
			safeExtendsExpression = f->newCommaExpression(
				f->newNumericLiteral("0", 0), extendsExpression);
		}
		classDefinitionStatements.push_back(
			createLet(ci->classSuper, safeExtendsExpression));

		Node* updatedExtendsElement = f->updateExpressionWithTypeArguments(
			extendsElement->as<ExpressionWithTypeArguments>(), ci->classSuper,
			nullptr);
		HeritageClause* hc = extendsClause->as<HeritageClause>();
		Node* updatedExtendsClause = f->updateHeritageClause(
			hc, hc->Token, f->newNodeList({updatedExtendsElement}));
		heritageClauses = f->newNodeList({updatedExtendsClause});
	}

	Node* renamedClassThis;
	if (ci->classThis != nullptr) {
		renamedClassThis = ci->classThis;
	} else {
		renamedClassThis = f->newThisExpression();
	}

	// 3. The name of the class is assigned.
	//
	// If the class did not have a name, the caller should have performed
	// injectClassNamedEvaluationHelperBlockIfMissing prior to calling this
	// function if a name was needed.

	// 4. For each member:
	//    a. Member Decorators are evaluated
	//    b. Computed Property Name is evaluated, if present
	//
	// We visit members in two passes:
	// - The first pass visits methods, accessors, and fields to collect
	//   decorators and computed property names.
	// - The second pass visits the constructor to add instance initializers.
	//
	// NOTE: If there are no constructors, but there are instance initializers,
	// a synthetic constructor is added.
	enterClass(ci);

	Node* metadataStmt =
		createMetadata(ci->metadataReference, ci->classSuper);
	leadingBlockStatements.push_back(metadataStmt);

	// Since the constructor can appear anywhere in the class body and its
	// transform depends on other class elements, we must first visit all
	// non-constructor members, then visit the constructor, all while
	// maintaining document order.
	NodeList* members =
		nonConstructorClassElementVisitor->visitNodes(node->memberList());
	members = constructorClassElementVisitor->visitNodes(members);

	// Handle pending expressions (computed property names and decorator
	// evaluations)
	if (!pendingExpressions.empty()) {
		// If a pending expression contains a lexical `this`, we'll need to
		// capture the lexical `this` of the container and transform it in the
		// expression. This ensures we use the correct `this` in the resulting
		// class `static` block. We don't use substitution here because the
		// size of the tree we are visiting is likely to be small and doesn't
		// justify the complexity of introducing substitution.
		outerThis = nullptr;
		for (Node* expr : pendingExpressions) {
			// If a pending expression contains lexical `this`, capture it
			if ((expr->subtreeFacts() & SubtreeContainsLexicalThis) != 0) {
				expr = outerThisVisitor->visitNode(expr);
			}
			Node* statement = f->newExpressionStatement(expr);
			leadingBlockStatements.push_back(statement);
		}
		if (outerThis != nullptr) {
			classDefinitionStatements.insert(
				classDefinitionStatements.begin(),
				createLet(outerThis, f->newThisExpression()));
		}
		pendingExpressions.clear();
	}
	exitClass();

	// If there are instance initializers but no constructor, synthesize one
	if (!ci->pendingInstanceInitializers.empty() &&
		getFirstConstructorWithBody(node) == nullptr) {
		std::vector<Node*> initializerStatements = prepareConstructor(ci);
		if (!initializerStatements.empty()) {
			bool isDerivedClass =
				extendsElement != nullptr &&
				skipOuterExpressions(
					extendsElement->as<ExpressionWithTypeArguments>()
						->Expression,
					OEKAll)
						->kind != Kind::NullKeyword;
			std::vector<Node*> constructorStatements;
			if (isDerivedClass) {
				Node* spreadArguments =
					f->newSpreadElement(f->newIdentifier("arguments"));
				Node* superCall = f->newCallExpression(
					f->newKeywordExpression(Kind::SuperKeyword), nullptr,
					nullptr, f->newNodeList({spreadArguments}),
					NodeFlagsNone);
				constructorStatements.push_back(
					f->newExpressionStatement(superCall));
			}
			constructorStatements.insert(constructorStatements.end(),
			                             initializerStatements.begin(),
			                             initializerStatements.end());
			Node* constructorBody =
				f->newBlock(f->newNodeList(constructorStatements), true);
			syntheticConstructor =
				f->newConstructorDeclaration(nullptr, nullptr,
			                                 f->newNodeList({}), nullptr,
			                                 nullptr, constructorBody);
		}
	}

	// Used in class definition steps 5,7,11
	if (ci->staticMethodExtraInitializersName != nullptr) {
		classDefinitionStatements.push_back(
			createLet(ci->staticMethodExtraInitializersName,
			          f->newArrayLiteralExpression(f->newNodeList({}),
			                                       false)));
	}

	// Used in class definition steps 6,8, and construction
	if (ci->instanceMethodExtraInitializersName != nullptr) {
		classDefinitionStatements.push_back(
			createLet(ci->instanceMethodExtraInitializersName,
			          f->newArrayLiteralExpression(f->newNodeList({}),
			                                       false)));
	}

	// Used in class definition steps 7, 8, 12, and construction.
	// Emit member info variable declarations; the reference implementation
	// emits static member vars first, then non-static.
	if (ci->memberInfos.Size() > 0) {
		std::vector<Node*> staticDecls =
			emitMemberInfoDeclarations(ci, true /*isStatic*/);
		classDefinitionStatements.insert(classDefinitionStatements.end(),
		                                 staticDecls.begin(),
		                                 staticDecls.end());
		std::vector<Node*> nonStaticDecls =
			emitMemberInfoDeclarations(ci, false /*isStatic*/);
		classDefinitionStatements.insert(classDefinitionStatements.end(),
		                                 nonStaticDecls.begin(),
		                                 nonStaticDecls.end());
	}

	// 5. Static non-field element decorators are applied
	leadingBlockStatements.insert(leadingBlockStatements.end(),
	                              ci->staticNonFieldDecorationStatements.begin(),
	                              ci->staticNonFieldDecorationStatements.end());

	// 6. Non-static non-field element decorators are applied
	leadingBlockStatements.insert(
		leadingBlockStatements.end(),
		ci->nonStaticNonFieldDecorationStatements.begin(),
		ci->nonStaticNonFieldDecorationStatements.end());

	// 7. Static field element decorators are applied
	leadingBlockStatements.insert(leadingBlockStatements.end(),
	                              ci->staticFieldDecorationStatements.begin(),
	                              ci->staticFieldDecorationStatements.end());

	// 8. Non-static field element decorators are applied
	leadingBlockStatements.insert(
		leadingBlockStatements.end(),
		ci->nonStaticFieldDecorationStatements.begin(),
		ci->nonStaticFieldDecorationStatements.end());

	// 9. Class decorators are applied
	// 10. Class binding is initialized
	//
	// produces:
	//   __esDecorate(null, _classDescriptor = { value: this },
	//   _classDecorators, { kind: "class", name: this.name, metadata }, null,
	//   _classExtraInitializers);
	if (ci->classDescriptorName != nullptr && ci->classDecoratorsName != nullptr &&
		ci->classExtraInitializersName != nullptr && ci->classThis != nullptr) {
		Node* valueProperty =
			f->newPropertyAssignment(nullptr, f->newIdentifier("value"),
			                         nullptr, nullptr, renamedClassThis);
		Node* classDescriptor = f->newObjectLiteralExpression(
			f->newNodeList({valueProperty}), false);
		Node* classDescriptorAssignment = f->newAssignmentExpression(
			ci->classDescriptorName, classDescriptor);
		Node* classNameReference = f->newPropertyAccessExpression(
			renamedClassThis, nullptr, f->newIdentifier("name"),
			NodeFlagsNone);

		Node* contextObj = f->newESDecorateClassContextObject(
			classNameReference, ci->metadataReference);

		Node* esDecorateHelper = f->newESDecorateHelper(
			f->newToken(Kind::NullKeyword),
			classDescriptorAssignment,
			ci->classDecoratorsName,
			contextObj,
			f->newToken(Kind::NullKeyword),
			ci->classExtraInitializersName);
		Node* esDecorateStatement = f->newExpressionStatement(esDecorateHelper);
		ec->setSourceMapRange(esDecorateStatement,
		                      moveRangePastDecorators(node));
		leadingBlockStatements.push_back(esDecorateStatement);

		// produces:
		//   C = _classThis = _classDescriptor.value;
		Node* classDescriptorValueRef = f->newPropertyAccessExpression(
			ci->classDescriptorName, nullptr, f->newIdentifier("value"),
			NodeFlagsNone);
		Node* classThisAssignment = f->newAssignmentExpression(
			ci->classThis, classDescriptorValueRef);
		Node* classReferenceAssignment = f->newAssignmentExpression(
			classReference, classThisAssignment);
		leadingBlockStatements.push_back(
			f->newExpressionStatement(classReferenceAssignment));
	}

	// produces:
	//   if (metadata) Object.defineProperty(C, Symbol.metadata,
	//   { configurable: true, writable: true, value: metadata });
	leadingBlockStatements.push_back(
		createSymbolMetadata(renamedClassThis, ci->metadataReference));

	// 11. Static extra initializers
	// 12. Static fields are initialized
	if (!ci->pendingStaticInitializers.empty()) {
		for (Node* initializer : ci->pendingStaticInitializers) {
			Node* initializerStatement =
				f->newExpressionStatement(initializer);
			ec->setSourceMapRange(initializerStatement,
			                      ec->sourceMapRange(initializer));
			trailingBlockStatements.push_back(initializerStatement);
		}
		ci->pendingStaticInitializers.clear();
	}

	// 13. Class extra initializers
	if (ci->classExtraInitializersName != nullptr) {
		Node* runClassInitializersHelper = f->newRunInitializersHelper(
			renamedClassThis, ci->classExtraInitializersName, nullptr);
		Node* runClassInitializersStatement =
			f->newExpressionStatement(runClassInitializersHelper);
		if (node->name() != nullptr) {
			ec->setSourceMapRange(runClassInitializersStatement,
			                      node->name()->loc);
		} else {
			ec->setSourceMapRange(runClassInitializersStatement,
			                      moveRangePastDecorators(node));
		}
		trailingBlockStatements.push_back(runClassInitializersStatement);
	}

	// If there are no other static initializers to run, combine the leading
	// and trailing block statements
	if (!leadingBlockStatements.empty() && !trailingBlockStatements.empty() &&
		!ci->hasStaticInitializers) {
		leadingBlockStatements.insert(leadingBlockStatements.end(),
		                              trailingBlockStatements.begin(),
		                              trailingBlockStatements.end());
		trailingBlockStatements.clear();
	}

	// prepare a leading `static {}` block, if necessary
	//
	// produces:
	//   class C {
	//       static { ... }
	//       ...
	//   }
	Node* leadingStaticBlock = nullptr;
	if (!leadingBlockStatements.empty()) {
		leadingStaticBlock = f->newClassStaticBlockDeclaration(
			nullptr,
			f->newBlock(f->newNodeList(leadingBlockStatements), true));
	}

	if (leadingStaticBlock != nullptr &&
		shouldTransformPrivateStaticElementsInClass) {
		// We use EFTransformPrivateStaticElements as a marker on a class
		// static block to inform the classFields transform that it shouldn't
		// rename `this` to `_classThis` in the transformed class static block.
		ec->setEmitFlags(leadingStaticBlock,
		                 printer::EFTransformPrivateStaticElements);
	}

	// prepare a trailing `static {}` block, if necessary
	//
	// produces:
	//   class C {
	//       ...
	//       static { ... }
	//   }
	Node* trailingStaticBlock = nullptr;
	if (!trailingBlockStatements.empty()) {
		trailingStaticBlock = f->newClassStaticBlockDeclaration(
			nullptr,
			f->newBlock(f->newNodeList(trailingBlockStatements), true));
	}

	// Assemble new members list
	if (leadingStaticBlock != nullptr || syntheticConstructor != nullptr ||
		trailingStaticBlock != nullptr) {
		std::vector<Node*> newMembers;
		newMembers.reserve(members->nodes.size() + 3);

		// Find the existing NamedEvaluation helper block index
		int existingNamedEvaluationHelperBlockIndex = -1;
		for (size_t i = 0; i < members->nodes.size(); i++) {
			if (isClassNamedEvaluationHelperBlock(ec, members->nodes[i])) {
				existingNamedEvaluationHelperBlockIndex =
					static_cast<int>(i);
				break;
			}
		}

		// add the leading `static {}` block
		if (leadingStaticBlock != nullptr) {
			// add the `static {}` block after any existing NamedEvaluation
			// helper block, if one exists.
			newMembers.insert(
				newMembers.end(), members->nodes.begin(),
				members->nodes.begin() +
					existingNamedEvaluationHelperBlockIndex + 1);
			newMembers.push_back(leadingStaticBlock);
			newMembers.insert(
				newMembers.end(),
				members->nodes.begin() +
					existingNamedEvaluationHelperBlockIndex + 1,
				members->nodes.end());
		} else {
			newMembers.insert(newMembers.end(), members->nodes.begin(),
			                  members->nodes.end());
		}

		// append the synthetic constructor, if necessary
		if (syntheticConstructor != nullptr) {
			newMembers.push_back(syntheticConstructor);
		}

		// append a trailing `static {}` block, if necessary
		if (trailingStaticBlock != nullptr) {
			newMembers.push_back(trailingStaticBlock);
		}

		NodeList* membersList = f->newNodeList(newMembers);
		membersList->loc = members->loc;
		members = membersList;
	}

	std::vector<Node*> lexicalEnvironment = ec->endVariableEnvironment();

	Node* classExpression;
	if (!classDecorators.empty()) {
		classExpression =
			f->newClassExpression(nullptr, nullptr, nullptr, heritageClauses,
			                      members);
		ec->setOriginal(classExpression, node);
		if (ci->classThis != nullptr) {
			classExpression = injectClassThisAssignmentIfMissing(
				ec, f, classExpression, ci->classThis);
		}

		// We use `var` instead of `let` so we can leverage NamedEvaluation to
		// define the class name and still be able to ensure it is initialized
		// prior to any use in `static {}`.

		// produces:
		//   (() => {
		//       let _classDecorators = [...];
		//       let _classDescriptor;
		//       let _classExtraInitializers = [];
		//       let _classThis;
		//       ...
		//       var C = class {
		//           static {
		//               __esDecorate(null, _classDescriptor = { value: this },
		//               _classDecorators, ...);
		//               C = _classThis = _classDescriptor.value;
		//           }
		//           static x = 1;
		//           static y = C.x; // `C` will already be defined here.
		//           static { ... }
		//       };
		//       return C;
		//   })();

		Node* classReferenceDeclaration = f->newVariableDeclaration(
			classReference, nullptr, nullptr, classExpression);
		Node* classReferenceVarDeclList = f->newVariableDeclarationList(
			f->newNodeList({classReferenceDeclaration}), NodeFlagsNone);
		Node* returnExpr;
		if (ci->classThis != nullptr) {
			returnExpr =
				f->newAssignmentExpression(classReference, ci->classThis);
		} else {
			returnExpr = classReference;
		}
		classDefinitionStatements.push_back(
			f->newVariableStatement(nullptr, classReferenceVarDeclList));
		classDefinitionStatements.push_back(f->newReturnStatement(returnExpr));
	} else {
		// produces:
		//   return <classExpression>;
		classExpression =
			f->newClassExpression(nullptr, node->name(), nullptr,
			                      heritageClauses, members);
		ec->setOriginal(classExpression, node);
		classDefinitionStatements.push_back(
			f->newReturnStatement(classExpression));
	}

	if (shouldTransformPrivateStaticElementsInClass) {
		ec->addEmitFlags(classExpression,
		                 printer::EFTransformPrivateStaticElements);
		for (Node* member : classExpression->members()) {
			if ((isPrivateIdentifierClassElementDeclaration(member) ||
			     isAutoAccessorPropertyDeclaration(member)) &&
				hasStaticModifier(member)) {
				ec->addEmitFlags(member,
				                 printer::EFTransformPrivateStaticElements);
			}
		}
	}

	std::vector<Node*> mergedStatements =
		ec->mergeEnvironment(classDefinitionStatements, lexicalEnvironment);
	return f->newImmediatelyInvokedArrowFunction(mergedStatements);
}

// emitMemberInfoDeclarations — esdecorator.go:993. Generates let declarations
// for member decorator info variables, filtered by static/non-static.
std::vector<Node*> esDecoratorTransformer::emitMemberInfoDeclarations(
	classInfo* ci, bool isStatic_) {
	printer::NodeFactory* f = factory();
	std::vector<Node*> stmts;
	for (Node* member : ci->memberInfos.Keys()) {
		memberInfo* mi = ci->memberInfos.GetOrZero(member);
		if (isStatic(member) != isStatic_) {
			continue;
		}
		stmts.push_back(createLet(mi->memberDecoratorsName, nullptr));
		if (mi->memberInitializersName != nullptr) {
			stmts.push_back(createLet(mi->memberInitializersName,
			                          f->newArrayLiteralExpression(
			                              f->newNodeList({}), false)));
		}
		if (mi->memberExtraInitializersName != nullptr) {
			stmts.push_back(createLet(mi->memberExtraInitializersName,
			                          f->newArrayLiteralExpression(
			                              f->newNodeList({}), false)));
		}
		if (mi->memberDescriptorName != nullptr) {
			stmts.push_back(createLet(mi->memberDescriptorName, nullptr));
		}
	}
	return stmts;
}

// isDecoratedClassLike — esdecorator.go:1014
bool isDecoratedClassLike(Node* node) {
	return classOrConstructorParameterIsDecorated(false, node) ||
		childIsDecorated(false, node, nullptr);
}

// visitClassDeclaration — esdecorator.go:1019
Node* esDecoratorTransformer::visitClassDeclaration(ClassDeclaration* node) {
	if (isDecoratedClassLike(node->asNode())) {
		printer::NodeFactory* f = factory();
		printer::EmitContext* ec = emitContext();
		std::vector<Node*> statements;

		Node* originalClass = ec->mostOriginal(node->asNode());
		if (!isClassLike(originalClass)) {
			originalClass = node->asNode();
		}
		Node* className;
		if (originalClass->name() != nullptr) {
			className = f->newStringLiteralFromNode(originalClass->name());
		} else {
			className = f->newStringLiteral("default", 0);
		}

		bool isExport =
			hasSyntacticModifier(node->asNode(), ModifierFlagsExport);
		bool isDefault =
			hasSyntacticModifier(node->asNode(), ModifierFlagsDefault);

		Node* classNode = node->asNode();
		if (node->name == nullptr) {
			classNode = injectClassNamedEvaluationHelperBlockIfMissing(
				ec, classNode, className, nullptr);
		}

		if (isExport && isDefault) {
			Node* iife = transformClassLike(classNode);
			if (classNode->name() != nullptr) {
				// produces:
				//   let C = (() => { ... })();
				//   export default C;
				Node* varDecl = f->newVariableDeclaration(
					f->getLocalName(classNode), nullptr, nullptr, iife);
				ec->setOriginal(varDecl, classNode);
				Node* varDecls = f->newVariableDeclarationList(
					f->newNodeList({varDecl}), NodeFlagsLet);
				Node* varStatement =
					f->newVariableStatement(nullptr, varDecls);
				statements.push_back(varStatement);

				Node* exportStatement =
					f->newExportDefault(f->getDeclarationName(classNode));
				ec->setOriginal(exportStatement, classNode);
				ec->assignCommentRange(exportStatement, classNode);
				ec->setSourceMapRange(exportStatement,
				                      moveRangePastDecorators(classNode));
				statements.push_back(exportStatement);
			} else {
				// produces:
				//   export default (() => { ... })();
				Node* exportStatement = f->newExportDefault(iife);
				ec->setOriginal(exportStatement, classNode);
				ec->assignCommentRange(exportStatement, classNode);
				ec->setSourceMapRange(exportStatement,
				                      moveRangePastDecorators(classNode));
				statements.push_back(exportStatement);
			}
		} else {
			debugAssert_(
				classNode->name() != nullptr,
				"A class declaration that is not a default export must have "
				"a name.");
			// produces:
			//   let C = (() => { ... })();
			Node* iife = transformClassLike(classNode);
			ModifierList* modifiers =
				exportStrippingModifierVisitor->visitModifiers(
					classNode->modifiers());

			Node* declName = f->getLocalName(
				classNode,
				printer::AssignedNameOptions{false, true, false});
			Node* varDecl =
				f->newVariableDeclaration(declName, nullptr, nullptr, iife);
			ec->setOriginal(varDecl, classNode);
			Node* varDecls = f->newVariableDeclarationList(
				f->newNodeList({varDecl}), NodeFlagsLet);
			Node* varStatement =
				f->newVariableStatement(modifiers, varDecls);
			ec->setOriginal(varStatement, classNode);
			ec->assignCommentRange(varStatement, classNode);
			statements.push_back(varStatement);

			if (isExport) {
				// produces:
				//   export { C };
				Node* exportStatement =
					f->newExternalModuleExport(declName);
				ec->setOriginal(exportStatement, classNode);
				statements.push_back(exportStatement);
			}
		}

		return singleOrMany(statements, f);
	}

	// Non-decorated class
	ModifierList* modifiers =
		modifierVisitor->visitModifiers(node->modifiers);
	NodeList* heritageClauses =
		visitor()->visitNodes(node->HeritageClauses);
	enterClass(nullptr);
	NodeList* members = classElementVisitor->visitNodes(node->Members);
	exitClass();
	return factory()->updateClassDeclaration(node, modifiers, node->name,
	                                         nullptr, heritageClauses, members);
}

// visitClassExpression — esdecorator.go:1107
Node* esDecoratorTransformer::visitClassExpression(ClassExpression* node) {
	if (isDecoratedClassLike(node->asNode())) {
		Node* iife = transformClassLike(node->asNode());
		emitContext()->setOriginal(iife, node->asNode());
		return iife;
	}

	ModifierList* modifiers =
		modifierVisitor->visitModifiers(node->modifiers);
	NodeList* heritageClauses =
		visitor()->visitNodes(node->HeritageClauses);
	enterClass(nullptr);
	NodeList* members = classElementVisitor->visitNodes(node->Members);
	exitClass();
	return factory()->updateClassExpression(node, modifiers, node->name,
	                                        nullptr, heritageClauses, members);
}

// prepareConstructor — esdecorator.go:1122
std::vector<Node*> esDecoratorTransformer::prepareConstructor(classInfo* ci) {
	// Decorated instance members can add "extra" initializers to the instance.
	// If a class contains any instance fields, we'll inject the
	// `__runInitializers()` call for these extra initializers into the
	// initializer of the first class member that will be initialized. However,
	// if the class does not contain any fields that we can piggyback on, we
	// need to synthesize a `__runInitializers()` call in the constructor
	// instead.
	if (ci->pendingInstanceInitializers.empty()) {
		return {};
	}
	printer::NodeFactory* f = factory();
	std::vector<Node*> statements{
		f->newExpressionStatement(
			f->inlineExpressions(ci->pendingInstanceInitializers))};
	ci->pendingInstanceInitializers.clear();
	return statements;
}

// transformConstructorBodyWorker — esdecorator.go:1138
std::vector<Node*> esDecoratorTransformer::transformConstructorBodyWorker(
	std::vector<Node*> statementsOut, std::vector<Node*> statementsIn,
	int statementOffset, std::vector<int> superPath, int superPathDepth,
	const std::vector<Node*>& initializerStatements) {
	int superStatementIndex = superPath[superPathDepth];
	// Visit statements before super
	if (superStatementIndex > statementOffset) {
		for (int i = statementOffset; i < superStatementIndex; i++) {
			statementsOut.push_back(visitor()->visitNode(statementsIn[i]));
		}
	}

	Node* superStatement = statementsIn[superStatementIndex];
	if (isTryStatement(superStatement)) {
		// Recurse into try block
		Node* tryBlockNode =
			superStatement->as<TryStatement>()->TryBlock;
		Block* tryBlock = tryBlockNode->as<Block>();
		std::vector<Node*> tryBlockStatements =
			transformConstructorBodyWorker({}, tryBlock->Statements->nodes, 0,
			                             superPath, superPathDepth + 1,
			                             initializerStatements);

		Node* newTryBlock = factory()->newBlock(
			factory()->newNodeList(tryBlockStatements), true);
		// Use the original try block's range even though the statements may
		// differ due to injected initializer statements. This preserves
		// source map fidelity for the enclosing try statement.
		newTryBlock->loc = tryBlockNode->loc;

		Node* catchClause = nullptr;
		if (superStatement->as<TryStatement>()->CatchClause != nullptr) {
			catchClause = visitor()->visitNode(
				superStatement->as<TryStatement>()->CatchClause);
		}
		Node* finallyBlock = nullptr;
		if (superStatement->as<TryStatement>()->FinallyBlock != nullptr) {
			finallyBlock = visitor()->visitNode(
				superStatement->as<TryStatement>()->FinallyBlock);
		}
		Node* updated = factory()->updateTryStatement(
			superStatement->as<TryStatement>(), newTryBlock, catchClause,
			finallyBlock);
		statementsOut.push_back(updated);
	} else {
		statementsOut.push_back(visitor()->visitNode(superStatement));
		statementsOut.insert(statementsOut.end(),
		                     initializerStatements.begin(),
		                     initializerStatements.end());
	}

	// Visit statements after super
	if (superStatementIndex + 1 < static_cast<int>(statementsIn.size())) {
		for (size_t i = superStatementIndex + 1; i < statementsIn.size();
		     i++) {
			statementsOut.push_back(visitor()->visitNode(statementsIn[i]));
		}
	}
	return statementsOut;
}

// visitConstructorDeclaration — esdecorator.go:1184
Node* esDecoratorTransformer::visitConstructorDeclaration(Node* node) {
	enterClassElement(node);
	ModifierList* modifiers =
		modifierVisitor->visitModifiers(node->modifiers());
	NodeList* parameters = visitor()->visitNodes(node->parameterList());

	Node* body = nullptr;
	ConstructorDeclaration* ctor = node->as<ConstructorDeclaration>();
	if (ctor->Body != nullptr && classInfoStack != nullptr) {
		// If there are instance extra initializers we need to add them to the
		// body along with any field initializers
		std::vector<Node*> initializerStatements =
			prepareConstructor(classInfoStack);
		if (!initializerStatements.empty()) {
			std::vector<Node*> stmts;
			auto prologueRest = factory()->splitStandardPrologue(
				ctor->Body->as<Block>()->Statements->nodes);
			stmts.insert(stmts.end(), prologueRest.first.begin(),
			             prologueRest.first.end());

			std::vector<int> superStatementIndices =
				findSuperStatementIndexPath(prologueRest.second, 0);
			if (!superStatementIndices.empty()) {
				stmts = transformConstructorBodyWorker(
					stmts, prologueRest.second, 0, superStatementIndices, 0,
					initializerStatements);
			} else {
				stmts.insert(stmts.end(), initializerStatements.begin(),
				             initializerStatements.end());
				auto visitedSlice =
					visitor()->visitSlice(prologueRest.second);
				stmts.insert(stmts.end(), visitedSlice.first.begin(),
				             visitedSlice.first.end());
			}

			body = factory()->newBlock(factory()->newNodeList(stmts), true);
			emitContext()->setOriginal(body, ctor->Body);
			body->loc = ctor->Body->loc;
		}
	}

	if (body == nullptr) {
		body = visitor()->visitNode(ctor->Body);
	}
	exitClassElement();
	return factory()->updateConstructorDeclaration(ctor, modifiers, nullptr,
	                                               parameters, nullptr,
	                                               nullptr, body);
}

// finishClassElement — esdecorator.go:1222
Node* esDecoratorTransformer::finishClassElement(Node* updated,
                                                 Node* original) {
	if (updated != original) {
		// While we emit the source map for the node after skipping decorators
		// and modifiers, we need to emit the comments for the original range.
		emitContext()->assignCommentRange(updated, original);
		emitContext()->setSourceMapRange(updated,
		                                 moveRangePastDecorators(original));
	}
	return updated;
}

// partialTransformClassElement — esdecorator.go:1244
partialResult esDecoratorTransformer::partialTransformClassElement(
	Node* member, classInfo* ci,
	const createDescriptorFunc& createDescriptor) {
	printer::NodeFactory* f = factory();
	printer::EmitContext* ec = emitContext();

	if (ci == nullptr) {
		ModifierList* modifiers =
			modifierVisitor->visitModifiers(member->modifiers());
		enterName();
		Node* name = visitPropertyName(member->name());
		exitName();
		return partialResult{modifiers, nullptr, name, nullptr, nullptr,
		                     nullptr,   nullptr};
	}

	// Member decorators require privileged access to private names. However,
	// computed property evaluation occurs interspersed with decorator
	// evaluation. This means that if we encounter a computed property name we
	// must inline decorator evaluation.

	// Collect decorators for this member. Decorator expressions evaluate
	// outside the class body, so `this` should NOT be replaced with
	// `_classThis`.
	Node* savedClassThis = classThis;
	classThis = nullptr;
	std::vector<Node*> memberDecorators =
		transformAllDecoratorsOfDeclaration(member->decorators());
	classThis = savedClassThis;
	ModifierList* modifiers =
		modifierVisitor->visitModifiers(member->modifiers());

	partialResult result;
	result.modifiers = modifiers;

	if (!memberDecorators.empty()) {
		Node* memberDecoratorsName =
			createHelperVariable(member, "decorators");
		Node* memberDecoratorsArray = f->newArrayLiteralExpression(
			f->newNodeList(memberDecorators), false);
		Node* memberDecoratorsAssignment = f->newAssignmentExpression(
			memberDecoratorsName, memberDecoratorsArray);
		auto* mi = new memberInfo();
		mi->memberDecoratorsName = memberDecoratorsName;
		ci->memberInfos.Set(member, mi);
		pendingExpressions.push_back(memberDecoratorsAssignment);

		// 5. Static non-field (method/getter/setter/auto-accessor) element
		//    decorators are applied
		// 6. Non-static non-field (method/getter/setter/auto-accessor) element
		//    decorators are applied
		// 7. Static field (excl. auto-accessor) element decorators are applied
		// 8. Non-static field (excl. auto-accessor) element decorators are
		//    applied

		// Determine decorator kind
		std::string kind;
		if (isGetAccessorDeclaration(member)) {
			kind = "getter";
		} else if (isSetAccessorDeclaration(member)) {
			kind = "setter";
		} else if (isMethodDeclaration(member)) {
			kind = "method";
		} else if (isAutoAccessorPropertyDeclaration(member)) {
			kind = "accessor";
		} else if (isPropertyDeclaration(member)) {
			kind = "field";
		} else {
			debugFail_("Unexpected class element kind.");
		}

		// Determine the property name for the context
		bool propertyNameComputed = false;
		Node* propertyNameExpr = nullptr;
		if (member->name() != nullptr &&
			(isIdentifier(member->name()) ||
			 isPrivateIdentifier(member->name()))) {
			propertyNameComputed = false;
			propertyNameExpr = member->name();
		} else if (member->name() != nullptr &&
		           isPropertyNameLiteral(member->name())) {
			propertyNameComputed = true;
			propertyNameExpr = f->newStringLiteralFromNode(member->name());
		} else if (member->name() != nullptr &&
		           isComputedPropertyName(member->name())) {
			ComputedPropertyName* cpn =
				member->name()->as<ComputedPropertyName>();
			if (isPropertyNameLiteral(cpn->Expression) &&
				!isIdentifier(cpn->Expression)) {
				propertyNameComputed = true;
				propertyNameExpr =
					f->newStringLiteralFromNode(cpn->Expression);
			} else {
				enterName();
				auto pair = visitReferencedPropertyName(member->name());
				result.referencedName = pair.first;
				result.name = pair.second;
				exitName();
				propertyNameComputed = true;
				propertyNameExpr = result.referencedName;
			}
		}

		Node* contextObj = f->newESDecorateClassElementContextObject(
			kind,
			propertyNameComputed,
			propertyNameExpr,
			isStatic(member),
			member->name() != nullptr && isPrivateIdentifier(member->name()),
			// 15.7.3 CreateDecoratorAccessObject (kind, name)
			// 2. If _kind_ is ~field~, ~method~, ~accessor~, or ~getter~, then
			//    ...
			isPropertyDeclaration(member) || isGetAccessorDeclaration(member) ||
				isMethodDeclaration(member),
			// 3. If _kind_ is ~field~, ~accessor~, or ~setter~, then ...
			isPropertyDeclaration(member) || isSetAccessorDeclaration(member),
			ci->metadataReference);

		if (isMethodOrAccessor(member)) {
			// produces (public elements):
			//   __esDecorate(this, null, _static_member_decorators, { kind:
			//   "method", name: "...", static: true, private: false, access:
			//   { ... } }, _staticExtraInitializers);
			//   __esDecorate(this, null, _member_decorators, { kind: "method",
			//   name: "...", static: false, private: false, access: { ... } },
			//   _instanceExtraInitializers);
			//   (and getter/setter variants)
			//
			// produces (private elements):
			//   __esDecorate(this, _static_member_descriptor = { value() { ...
			//   } }, _static_member_decorators, { kind: "method", name: "...",
			//   static: true, private: true, access: { ... } },
			//   _staticExtraInitializers);
			//   (and instance/getter/setter variants)
			Node* methodExtraInitializersName =
				ci->instanceMethodExtraInitializersName;
			if (isStatic(member)) {
				methodExtraInitializersName =
					ci->staticMethodExtraInitializersName;
			}
			debugAssert_(methodExtraInitializersName != nullptr,
			             "methodExtraInitializersName should be defined");

			Node* descriptorArg;
			if (isPrivateIdentifierClassElementDeclaration(member) &&
				createDescriptor != nullptr) {
				// For private members, extract the method/accessor body into
				// a descriptor object. Filter modifiers to only keep async.
				ModifierList* asyncMods =
					asyncOnlyModifierVisitor->visitModifiers(modifiers);
				Node* descriptor = createDescriptor(member, asyncMods);
				mi->memberDescriptorName =
					createHelperVariable(member, "descriptor");
				result.descriptorName = mi->memberDescriptorName;
				descriptorArg = f->newAssignmentExpression(
					mi->memberDescriptorName, descriptor);
			} else {
				descriptorArg = f->newToken(Kind::NullKeyword);
			}

			Node* esDecorateExpr = f->newESDecorateHelper(
				f->newThisExpression(),
				descriptorArg,
				memberDecoratorsName,
				contextObj,
				f->newToken(Kind::NullKeyword),
				methodExtraInitializersName);
			Node* esDecorateStatement =
				f->newExpressionStatement(esDecorateExpr);
			ec->setSourceMapRange(esDecorateStatement,
			                      moveRangePastDecorators(member));
			appendDecorationStatement(ci, member, esDecorateStatement);
		} else if (isPropertyDeclaration(member)) {
			mi->memberInitializersName =
				createHelperVariable(member, "initializers");
			mi->memberExtraInitializersName =
				createHelperVariable(member, "extraInitializers");
			result.initializersName = mi->memberInitializersName;
			result.extraInitializersName = mi->memberExtraInitializersName;
			if (isStatic(member)) {
				result.thisArg = ci->classThis;
			}

			Node* ctorArg;
			if (isAutoAccessorPropertyDeclaration(member)) {
				ctorArg = f->newThisExpression();
			} else {
				ctorArg = f->newToken(Kind::NullKeyword);
			}

			Node* descriptorArg;
			if (isPrivateIdentifierClassElementDeclaration(member) &&
				hasAccessorModifier(member) && createDescriptor != nullptr) {
				Node* descriptor = createDescriptor(member, nullptr);
				mi->memberDescriptorName =
					createHelperVariable(member, "descriptor");
				result.descriptorName = mi->memberDescriptorName;
				descriptorArg = f->newAssignmentExpression(
					mi->memberDescriptorName, descriptor);
			} else {
				descriptorArg = f->newToken(Kind::NullKeyword);
			}

			// produces:
			//   __esDecorate(null, null, _static_member_decorators, { kind:
			//   "field", name: "...", static: true, private: ..., access: {
			//   ... } }, _staticExtraInitializers);
			//   __esDecorate(null, null, _member_decorators, { kind: "field",
			//   name: "...", static: false, private: ..., access: { ... } },
			//   _instanceExtraInitializers);
			Node* esDecorateExpr = f->newESDecorateHelper(
				ctorArg,
				descriptorArg,
				memberDecoratorsName,
				contextObj,
				mi->memberInitializersName,
				mi->memberExtraInitializersName);
			Node* esDecorateStatement =
				f->newExpressionStatement(esDecorateExpr);
			ec->setSourceMapRange(esDecorateStatement,
			                      moveRangePastDecorators(member));
			appendDecorationStatement(ci, member, esDecorateStatement);
		}
	}

	if (result.name == nullptr) {
		enterName();
		result.name = visitPropertyName(member->name());
		exitName();
	}

	if ((modifiers == nullptr || modifiers->nodes.empty()) &&
		(isMethodDeclaration(member) || isPropertyDeclaration(member))) {
		// Don't emit leading comments on the name for methods and properties
		// without modifiers, otherwise we will end up printing duplicate
		// comments.
		ec->setEmitFlags(result.name, printer::EFNoLeadingComments);
	}

	return result;
}

// appendDecorationStatement — esdecorator.go:1447. Appends an __esDecorate
// statement to the appropriate decoration statement list on classInfo based
// on the member's kind and static-ness.
void esDecoratorTransformer::appendDecorationStatement(classInfo* ci,
                                                       Node* member,
                                                       Node* stmt) {
	if (isMethodOrAccessor(member) ||
		isAutoAccessorPropertyDeclaration(member)) {
		if (isStatic(member)) {
			ci->staticNonFieldDecorationStatements.push_back(stmt);
		} else {
			ci->nonStaticNonFieldDecorationStatements.push_back(stmt);
		}
	} else if (isPropertyDeclaration(member) &&
	           !isAutoAccessorPropertyDeclaration(member)) {
		if (isStatic(member)) {
			ci->staticFieldDecorationStatements.push_back(stmt);
		} else {
			ci->nonStaticFieldDecorationStatements.push_back(stmt);
		}
	} else {
		debugFail_("Unexpected class element kind.");
	}
}

// visitMethodDeclaration — esdecorator.go:1465
Node* esDecoratorTransformer::visitMethodDeclaration(Node* node) {
	enterClassElement(node);
	partialResult result = partialTransformClassElement(
		node, classInfoStack,
		[this](Node* m, ModifierList* mods) {
			return createMethodDescriptorObject(m, mods);
		});
	if (result.descriptorName != nullptr) {
		exitClassElement();
		return finishClassElement(
			createMethodDescriptorForwarder(result.modifiers, result.name,
			                                result.descriptorName),
			node);
	}
	NodeList* parameters = visitor()->visitNodes(node->parameterList());
	Node* body = visitor()->visitNode(node->body());
	exitClassElement();
	MethodDeclaration* method = node->as<MethodDeclaration>();
	return finishClassElement(
		factory()->updateMethodDeclaration(method, result.modifiers,
		                                   method->AsteriskToken, result.name,
		                                   nullptr, nullptr, parameters,
		                                   nullptr, nullptr, body),
		node);
}

// visitGetAccessorDeclaration — esdecorator.go:1482
Node* esDecoratorTransformer::visitGetAccessorDeclaration(Node* node) {
	enterClassElement(node);
	partialResult result = partialTransformClassElement(
		node, classInfoStack,
		[this](Node* m, ModifierList* mods) {
			return createGetAccessorDescriptorObject(m, mods);
		});
	if (result.descriptorName != nullptr) {
		exitClassElement();
		return finishClassElement(
			createGetAccessorDescriptorForwarder(result.modifiers, result.name,
			                                     result.descriptorName),
			node);
	}
	NodeList* parameters = visitor()->visitNodes(node->parameterList());
	Node* body = visitor()->visitNode(node->body());
	exitClassElement();
	GetAccessorDeclaration* accessor = node->as<GetAccessorDeclaration>();
	return finishClassElement(
		factory()->updateGetAccessorDeclaration(accessor, result.modifiers,
		                                        result.name, nullptr,
		                                        parameters, nullptr, nullptr,
		                                        body),
		node);
}

// visitSetAccessorDeclaration — esdecorator.go:1499
Node* esDecoratorTransformer::visitSetAccessorDeclaration(Node* node) {
	enterClassElement(node);
	partialResult result = partialTransformClassElement(
		node, classInfoStack,
		[this](Node* m, ModifierList* mods) {
			return createSetAccessorDescriptorObject(m, mods);
		});
	if (result.descriptorName != nullptr) {
		exitClassElement();
		return finishClassElement(
			createSetAccessorDescriptorForwarder(result.modifiers, result.name,
			                                     result.descriptorName),
			node);
	}
	NodeList* parameters = visitor()->visitNodes(node->parameterList());
	Node* body = visitor()->visitNode(node->body());
	exitClassElement();
	SetAccessorDeclaration* accessor = node->as<SetAccessorDeclaration>();
	return finishClassElement(
		factory()->updateSetAccessorDeclaration(accessor, result.modifiers,
		                                        result.name, nullptr,
		                                        parameters, nullptr, nullptr,
		                                        body),
		node);
}

// visitClassStaticBlockDeclaration — esdecorator.go:1516
Node* esDecoratorTransformer::visitClassStaticBlockDeclaration(Node* node) {
	enterClassElement(node);
	printer::NodeFactory* f = factory();

	Node* result;
	if (isClassNamedEvaluationHelperBlock(emitContext(), node)) {
		result = visitor()->visitEachChild(node);
		// Transfer AssignedName metadata to the new node so
		// isClassNamedEvaluationHelperBlock can still find it after visiting
		// (visiting may create a new node when this->_classThis)
		if (Node* assignedName = emitContext()->assignedNameOf(node);
			assignedName != nullptr && result != node) {
			emitContext()->setAssignedName(result, assignedName);
		}
	} else if (isClassThisAssignmentBlock(emitContext(), node)) {
		Node* savedClassThis = classThis;
		classThis = nullptr;
		result = visitor()->visitEachChild(node);
		classThis = savedClassThis;
	} else {
		// Use a nested variable environment so temp vars generated during
		// static block content transformation (e.g., super access temps) stay
		// scoped to the static block.
		printer::EmitContext* ec = emitContext();
		ec->startVariableEnvironment();
		result = visitor()->visitEachChild(node);
		std::vector<Node*> varStatements = ec->endVariableEnvironment();
		if (!varStatements.empty()) {
			// Inject var declarations at the start of the static block's body
			Block* blockBody = result->as<ClassStaticBlockDeclaration>()
			                       ->Body->as<Block>();
			std::vector<Node*> newStmts;
			newStmts.reserve(varStatements.size() +
			                 blockBody->Statements->nodes.size());
			newStmts.insert(newStmts.end(), varStatements.begin(),
			                varStatements.end());
			newStmts.insert(newStmts.end(), blockBody->Statements->nodes.begin(),
			                blockBody->Statements->nodes.end());
			result = f->newClassStaticBlockDeclaration(
				nullptr,
				f->newBlock(f->newNodeList(newStmts), blockBody->MultiLine));
		}
		if (classInfoStack != nullptr) {
			classInfoStack->hasStaticInitializers = true;
			if (!classInfoStack->pendingStaticInitializers.empty()) {
				// If we tried to inject the pending initializers into the
				// current block, we might run into variable name collisions
				// due to sharing this blocks scope. To avoid this, we inject
				// a new static block that contains the pending initializers
				// that precedes this block.
				std::vector<Node*> stmts;
				for (Node* init :
				     classInfoStack->pendingStaticInitializers) {
					Node* initStmt = f->newExpressionStatement(init);
					emitContext()->setSourceMapRange(
						initStmt, emitContext()->sourceMapRange(init));
					stmts.push_back(initStmt);
				}
				Node* body = f->newBlock(f->newNodeList(stmts), true);
				Node* staticBlock =
					f->newClassStaticBlockDeclaration(nullptr, body);
				classInfoStack->pendingStaticInitializers.clear();
				// Return both the new static block and the original
				exitClassElement();
				return singleOrMany({staticBlock, result}, f);
			}
		}
	}

	exitClassElement();
	return result;
}

// visitPropertyDeclaration — esdecorator.go:1574
Node* esDecoratorTransformer::visitPropertyDeclaration(Node* node) {
	if (isNamedEvaluationAnd(emitContext(), node,
	                         isAnonymousClassNeedingAssignedName)) {
		node = transformNamedEvaluation(
			emitContext(), node,
			canIgnoreEmptyStringLiteralInAssignedName(node->initializer()),
			"");
	}

	enterClassElement(node);

	// TODO(rbuckton): We support decorating `declare x` fields with
	//                 legacyDecorators, but we currently don't support them
	//                 with esDecorators. We need to consider whether we will
	//                 support them in the future, and how. For now, these
	//                 should be elided by the `ts` transform.
	debugAssert_(!hasSyntacticModifier(node, ModifierFlagsAmbient),
	             "Not yet implemented.");

	// 10.2.1.3 RS: EvaluateBody
	//   Initializer : `=` AssignmentExpression
	//     ...
	//     3. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is
	//        *true*, then
	//        a. Let _value_ be ? NamedEvaluation of |Initializer| with argument
	//           _functionObject_.[[ClassFieldInitializerName]].
	//     ...

	printer::NodeFactory* f = factory();
	printer::EmitContext* ec = emitContext();

	createDescriptorFunc createDescriptor;
	if (hasAccessorModifier(node)) {
		createDescriptor = [this](Node* m, ModifierList* mods) {
			return createAccessorPropertyDescriptorObject(m, mods);
		};
	}
	partialResult result =
		partialTransformClassElement(node, classInfoStack, createDescriptor);

	ec->startVariableEnvironment();

	Node* initializer = visitor()->visitNode(node->initializer());
	if (result.initializersName != nullptr) {
		Node* thisArg;
		if (result.thisArg != nullptr) {
			thisArg = result.thisArg;
		} else {
			thisArg = f->newThisExpression();
		}
		if (initializer == nullptr) {
			initializer = f->newVoidZeroExpression();
		}
		initializer = f->newRunInitializersHelper(thisArg,
		                                          result.initializersName,
		                                          initializer);
	}

	if (isStatic(node) && classInfoStack != nullptr && initializer != nullptr) {
		classInfoStack->hasStaticInitializers = true;
	}

	std::vector<Node*> declarations = ec->endVariableEnvironment();
	if (!declarations.empty()) {
		std::vector<Node*> stmts(declarations.begin(), declarations.end());
		stmts.push_back(f->newReturnStatement(initializer));
		initializer = f->newImmediatelyInvokedArrowFunction(stmts);
	}

	if (classInfoStack != nullptr) {
		if (isStatic(node)) {
			initializer =
				injectPendingInitializers(classInfoStack, true, initializer);
			if (result.extraInitializersName != nullptr) {
				Node* thisArg;
				if (classInfoStack->classThis != nullptr) {
					thisArg = classInfoStack->classThis;
				} else {
					thisArg = f->newThisExpression();
				}
				classInfoStack->pendingStaticInitializers.push_back(
					f->newRunInitializersHelper(
						thisArg, result.extraInitializersName, nullptr));
			}
		} else {
			initializer = injectPendingInitializers(classInfoStack, false,
			                                        initializer);
			if (result.extraInitializersName != nullptr) {
				classInfoStack->pendingInstanceInitializers.push_back(
					f->newRunInitializersHelper(f->newThisExpression(),
					                            result.extraInitializersName,
					                            nullptr));
			}
		}
	}

	exitClassElement();

	if (hasAccessorModifier(node) && result.descriptorName != nullptr) {
		// given:
		//  accessor #x = 1;
		//
		// emits:
		//  static {
		//      _esDecorate(null, _private_x_descriptor = { get() { return
		//      this.#x_1; }, set(value) { this.#x_1 = value; } }, ...)
		//  }
		//  ...
		//  #x_1 = 1;
		//  get #x() { return _private_x_descriptor.get.call(this); }
		//  set #x(value) { _private_x_descriptor.set.call(this, value); }

		TextRange commentRange = ec->commentRange(node);
		TextRange sourceMapRange = ec->sourceMapRange(node);

		// Since we're creating two declarations where there was previously
		// one, cache the expression for any computed property names.
		Node* propName = node->name();
		Node* getterName = result.name;
		Node* setterName = result.name;
		if (isComputedPropertyName(propName) &&
			!isSimpleInlineableExpression(propName->expression())) {
			BinaryExpression* cacheAssignment =
				findComputedPropertyNameCacheAssignment(ec, propName);
			if (cacheAssignment != nullptr) {
				getterName = f->updateComputedPropertyName(
					propName->as<ComputedPropertyName>(),
					visitor()->visitNode(propName->expression()));
				setterName = f->updateComputedPropertyName(
					propName->as<ComputedPropertyName>(),
					cacheAssignment->Left);
			} else {
				Node* temp = f->newTempVariable();
				ec->setSourceMapRange(temp, propName->expression()->loc);
				ec->addVariableDeclaration(temp);
				Node* expression =
					visitor()->visitNode(propName->expression());
				Node* assignment =
					f->newAssignmentExpression(temp, expression);
				ec->setSourceMapRange(assignment,
				                      propName->expression()->loc);
				getterName = f->updateComputedPropertyName(
					propName->as<ComputedPropertyName>(), assignment);
				setterName = f->updateComputedPropertyName(
					propName->as<ComputedPropertyName>(), temp);
			}
		}

		ModifierList* modifiersWithoutAccessor =
			accessorStrippingModifierVisitor->visitModifiers(
				result.modifiers);

		Node* backingField = createAccessorPropertyBackingField(
			f, node->as<PropertyDeclaration>(), modifiersWithoutAccessor,
			initializer);
		ec->setOriginal(backingField, node);
		ec->setEmitFlags(backingField, printer::EFNoComments);
		ec->setSourceMapRange(backingField, sourceMapRange);
		ec->setSourceMapRange(
			backingField->as<PropertyDeclaration>()->name,
			ec->sourceMapRange(node->name()));

		Node* getter = createGetAccessorDescriptorForwarder(
			modifiersWithoutAccessor, getterName, result.descriptorName);
		ec->setOriginal(getter, node);
		ec->setCommentRange(getter, commentRange);
		ec->setSourceMapRange(getter, sourceMapRange);

		Node* setter = createSetAccessorDescriptorForwarder(
			modifiersWithoutAccessor, setterName, result.descriptorName);
		ec->setOriginal(setter, node);
		ec->setEmitFlags(setter, printer::EFNoComments);
		ec->setSourceMapRange(setter, sourceMapRange);

		return singleOrMany({backingField, getter, setter}, f);
	}

	PropertyDeclaration* prop = node->as<PropertyDeclaration>();
	return finishClassElement(
		f->updatePropertyDeclaration(prop, result.modifiers, result.name,
		                             nullptr, nullptr, initializer),
		node);
}

// visitThisExpression — esdecorator.go:1724
Node* esDecoratorTransformer::visitThisExpression(Node* node) {
	if (classThis != nullptr) {
		return classThis;
	}
	return node;
}

// visitCallExpression — esdecorator.go:1731
Node* esDecoratorTransformer::visitCallExpression(Node* node) {
	CallExpression* call = node->as<CallExpression>();
	if (isSuperProperty(call->Expression) && classThis != nullptr) {
		Node* expression = visitor()->visitNode(call->Expression);
		NodeList* argumentsList = visitor()->visitNodes(call->Arguments);
		Node* invocation = factory()->newFunctionCallCall(
			expression, classThis, argumentsList->nodes);
		emitContext()->setOriginal(invocation, node);
		invocation->loc = node->loc;
		return invocation;
	}
	return visitor()->visitEachChild(node);
}

// visitTaggedTemplateExpression — esdecorator.go:1744
Node* esDecoratorTransformer::visitTaggedTemplateExpression(Node* node) {
	TaggedTemplateExpression* tte = node->as<TaggedTemplateExpression>();
	if (isSuperProperty(tte->Tag) && classThis != nullptr) {
		Node* tag = visitor()->visitNode(tte->Tag);
		Node* boundTag =
			factory()->newFunctionBindCall(tag, classThis, {});
		emitContext()->setOriginal(boundTag, node);
		boundTag->loc = node->loc;
		Node* template_ = visitor()->visitNode(tte->Template);
		return factory()->updateTaggedTemplateExpression(
			tte, boundTag, nullptr, nullptr, template_, tte->flags);
	}
	return visitor()->visitEachChild(node);
}

// visitPropertyAccessExpression — esdecorator.go:1757
Node* esDecoratorTransformer::visitPropertyAccessExpression(Node* node) {
	PropertyAccessExpression* pa = node->as<PropertyAccessExpression>();
	if (isSuperProperty(node) && isIdentifier(pa->name) &&
		classThis != nullptr && classSuper != nullptr) {
		Node* propertyName = factory()->newStringLiteralFromNode(pa->name);
		Node* superProperty = factory()->newReflectGetCall(
			classSuper, propertyName, classThis);
		emitContext()->setOriginal(superProperty, pa->Expression);
		superProperty->loc = pa->Expression->loc;
		return superProperty;
	}
	return visitor()->visitEachChild(node);
}

// visitElementAccessExpression — esdecorator.go:1769
Node* esDecoratorTransformer::visitElementAccessExpression(Node* node) {
	ElementAccessExpression* ea = node->as<ElementAccessExpression>();
	if (isSuperProperty(node) && classThis != nullptr && classSuper != nullptr) {
		Node* propertyName = visitor()->visitNode(ea->ArgumentExpression);
		Node* superProperty = factory()->newReflectGetCall(
			classSuper, propertyName, classThis);
		emitContext()->setOriginal(superProperty, ea->Expression);
		superProperty->loc = ea->Expression->loc;
		return superProperty;
	}
	return visitor()->visitEachChild(node);
}

// visitParameterDeclaration — esdecorator.go:1798.
//
// 8.6.3 RS: IteratorBindingInitialization
//
//	SingleNameBinding : BindingIdentifier Initializer?
//	  ...
//	  5. If |Initializer| is present and _v_ is *undefined*, then
//	     a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
//	        i. Set _v_ to ? NamedEvaluation of |Initializer| with argument
//	           _bindingId_.
//	  ...
//
// 14.3.3.3 RS: KeyedBindingInitialization
//
//	SingleNameBinding : BindingIdentifier Initializer?
//	  ...
//	  4. If |Initializer| is present and _v_ is *undefined*, then
//	     a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
//	        i. Set _v_ to ? NamedEvaluation of |Initializer| with argument
//	           _bindingId_.
//	  ...
Node* esDecoratorTransformer::visitParameterDeclaration(
	ParameterDeclaration* node) {
	Node* paramNode = node->asNode();
	if (isNamedEvaluationAnd(emitContext(), paramNode,
	                         isAnonymousClassNeedingAssignedName)) {
		paramNode = transformNamedEvaluation(
			emitContext(), paramNode,
			canIgnoreEmptyStringLiteralInAssignedName(
				paramNode->initializer()),
			"");
		node = paramNode->as<ParameterDeclaration>();
	}

	Node* updated = factory()->updateParameterDeclaration(
		node,
		nullptr,  // modifiers - strip all modifiers (including decorators)
		node->DotDotDotToken,
		visitor()->visitNode(node->name),
		nullptr,  // questionToken
		nullptr,  // type
		visitor()->visitNode(node->Initializer));
	if (updated != paramNode) {
		// While we emit the source map for the node after skipping decorators
		// and modifiers, we need to emit the comments for the original range.
		emitContext()->setCommentRange(updated, paramNode->loc);
		TextRange newLoc = moveRangePastModifiers(paramNode);
		updated->loc = newLoc;
		emitContext()->setSourceMapRange(updated, newLoc);
		emitContext()->setEmitFlags(updated->name(),
		                            printer::EFNoTrailingSourceMap);
	}
	return updated;
}

// visitNamedEvaluationSite — esdecorator.go:1870. Replaces Strada's
// visitPropertyAssignment, visitVariableDeclaration, and
// visitBindingElement, which all share the same logic.
//
// 13.2.5.5 RS: PropertyDefinitionEvaluation (PropertyAssignment)
//
//	PropertyAssignment : PropertyName `:` AssignmentExpression
//	  ...
//	  5. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is *true*
//	     and _isProtoSetter_ is *false*, then
//	     a. Let _popValue_ be ? NamedEvaluation of |AssignmentExpression| with
//	        argument _propKey_.
//	  ...
//
// 14.3.1.2 RS: Evaluation (VariableDeclaration)
//
//	LexicalBinding : BindingIdentifier Initializer
//	  ...
//	  3. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
//	     a. Let _value_ be ? NamedEvaluation of |Initializer| with argument
//	        _bindingId_.
//	  ...
//
// 14.3.2.1 RS: Evaluation (VariableDeclaration)
//
//	VariableDeclaration : BindingIdentifier Initializer
//	  ...
//	  3. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
//	     a. Let _value_ be ? NamedEvaluation of |Initializer| with argument
//	        _bindingId_.
//	  ...
//
// 8.6.3 RS: IteratorBindingInitialization (BindingElement)
//
//	SingleNameBinding : BindingIdentifier Initializer?
//	  ...
//	  5. If |Initializer| is present and _v_ is *undefined*, then
//	     a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
//	        i. Set _v_ to ? NamedEvaluation of |Initializer| with argument
//	           _bindingId_.
//	  ...
//
// 14.3.3.3 RS: KeyedBindingInitialization (BindingElement)
//
//	SingleNameBinding : BindingIdentifier Initializer?
//	  ...
//	  4. If |Initializer| is present and _v_ is *undefined*, then
//	     a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
//	        i. Set _v_ to ? NamedEvaluation of |Initializer| with argument
//	           _bindingId_.
//	  ...
Node* esDecoratorTransformer::visitNamedEvaluationSite(Node* node,
                                                       Node* classExpr) {
	if (isNamedEvaluationAnd(emitContext(), node,
	                         isAnonymousClassNeedingAssignedName)) {
		node = transformNamedEvaluation(
			emitContext(), node,
			canIgnoreEmptyStringLiteralInAssignedName(classExpr), "");
	}
	return visitor()->visitEachChild(node);
}

// isAnonymousClassNeedingAssignedName — esdecorator.go:1877
bool isAnonymousClassNeedingAssignedName(Node* node) {
	return isClassExpression(node) && node->name() == nullptr &&
		isDecoratedClassLike(node);
}

// canIgnoreEmptyStringLiteralInAssignedName — esdecorator.go:1885.
//
// The IIFE produced for `(@dec class {})` will result in an assigned name of
// the form `var class_1 = class { };`, and thus the empty string cannot be
// ignored. However, The IIFE produced for `(class { @dec x; })` will not
// result in an assigned name since it transforms to `return class { };`, and
// thus the empty string *can* be ignored.
bool canIgnoreEmptyStringLiteralInAssignedName(Node* node) {
	if (node == nullptr) {
		return false;
	}
	Node* innerExpression = skipOuterExpressions(node, OEKAll);
	return isClassExpression(innerExpression) &&
		innerExpression->name() == nullptr &&
		!classOrConstructorParameterIsDecorated(false, innerExpression);
}

// visitForStatement — esdecorator.go:1893
Node* esDecoratorTransformer::visitForStatement(Node* node) {
	printer::NodeFactory* f = factory();
	ForStatement* forStmt = node->as<ForStatement>();
	return f->updateForStatement(
		forStmt,
		discardedVisitor->visitNode(forStmt->Initializer),
		visitor()->visitNode(forStmt->Condition),
		discardedVisitor->visitNode(forStmt->Incrementor),
		emitContext()->visitIterationBody(forStmt->Statement, visitor()));
}

// visitExpressionStatement — esdecorator.go:1905
Node* esDecoratorTransformer::visitExpressionStatement(Node* node) {
	return discardedVisitor->visitEachChild(node);
}

// visitBinaryExpression — esdecorator.go:1909
Node* esDecoratorTransformer::visitBinaryExpression(Node* node,
                                                    bool discarded) {
	printer::NodeFactory* f = factory();
	printer::EmitContext* ec = emitContext();
	BinaryExpression* bin = node->as<BinaryExpression>();

	if (isDestructuringAssignment(node)) {
		Node* left = visitAssignmentPattern(bin->Left);
		Node* right = visitor()->visitNode(bin->Right);
		return f->updateBinaryExpression(bin, nullptr, left, nullptr,
		                                 bin->OperatorToken, right);
	}

	if (isAssignmentExpression(node, false)) {
		// 13.15.2 RS: Evaluation
		//   AssignmentExpression : LeftHandSideExpression `=`
		//   AssignmentExpression
		//     1. If |LeftHandSideExpression| is neither an |ObjectLiteral| nor
		//        an |ArrayLiteral|, then
		//        a. Let _lref_ be ? Evaluation of |LeftHandSideExpression|.
		//        b. If IsAnonymousFunctionDefinition(|AssignmentExpression|)
		//           and IsIdentifierRef of |LeftHandSideExpression| are both
		//           *true*, then
		//           i. Let _rval_ be ? NamedEvaluation of
		//              |AssignmentExpression| with argument
		//              _lref_.[[ReferencedName]].
		//     ...
		//   (and `&&=`, `||=`, `??=` variants)

		if (isNamedEvaluationAnd(ec, node,
		                         isAnonymousClassNeedingAssignedName)) {
			node = transformNamedEvaluation(
				ec, node,
				canIgnoreEmptyStringLiteralInAssignedName(bin->Right), "");
			return visitor()->visitEachChild(node);
		}

		if (isSuperProperty(bin->Left) && classThis != nullptr &&
			classSuper != nullptr) {
			Node* setterName = nullptr;
			if (isElementAccessExpression(bin->Left)) {
				setterName = visitor()->visitNode(
					bin->Left->as<ElementAccessExpression>()
						->ArgumentExpression);
			} else if (isPropertyAccessExpression(bin->Left) &&
			           isIdentifier(bin->Left->as<PropertyAccessExpression>()
			                            ->name)) {
				setterName = f->newStringLiteralFromNode(
					bin->Left->as<PropertyAccessExpression>()->name);
			}
			if (setterName != nullptr) {
				// super.x = ...
				// super.x += ...
				// super[x] = ...
				// super[x] += ...
				Node* expression = visitor()->visitNode(bin->Right);
				if (isCompoundAssignment_(bin->OperatorToken->kind)) {
					Node* getterName = setterName;
					if (!isSimpleInlineableExpression(setterName)) {
						getterName = f->newTempVariable();
						ec->addVariableDeclaration(getterName);
						setterName = f->newAssignmentExpression(
							getterName, setterName);
					}
					Node* superPropertyGet = f->newReflectGetCall(
						classSuper, getterName, classThis);
					ec->setOriginal(superPropertyGet, bin->Left);
					superPropertyGet->loc = bin->Left->loc;
					expression = f->asNodeFactory()->newBinaryExpression(
						nullptr,
						superPropertyGet,
						nullptr,
						f->newToken(
							getNonAssignmentOperatorForCompoundAssignment(
								bin->OperatorToken->kind)),
						expression);
					expression->loc = node->loc;
				}
				Node* temp = nullptr;
				if (!discarded) {
					temp = f->newTempVariable();
					ec->addVariableDeclaration(temp);
				}
				if (temp != nullptr) {
					expression =
						f->newAssignmentExpression(temp, expression);
					expression->loc = node->loc;
				}
				expression = f->newReflectSetCall(classSuper, setterName,
				                                  expression, classThis);
				ec->setOriginal(expression, node);
				expression->loc = node->loc;
				if (temp != nullptr) {
					expression =
						f->newCommaExpression(expression, temp);
					expression->loc = node->loc;
				}
				return expression;
			}
		}
	}

	if (bin->OperatorToken->kind == Kind::CommaToken) {
		Node* left = discardedVisitor->visitNode(bin->Left);
		Node* right;
		if (discarded) {
			right = discardedVisitor->visitNode(bin->Right);
		} else {
			right = visitor()->visitNode(bin->Right);
		}
		return f->updateBinaryExpression(bin, nullptr, left, nullptr,
		                                 bin->OperatorToken, right);
	}

	return visitor()->visitEachChild(node);
}

// visitPreOrPostfixUnaryExpression — esdecorator.go:2019
Node* esDecoratorTransformer::visitPreOrPostfixUnaryExpression(Node* node,
                                                               bool discarded) {
	printer::NodeFactory* f = factory();
	printer::EmitContext* ec = emitContext();

	Kind operator_;
	Node* operandNode;
	if (isPrefixUnaryExpression(node)) {
		operator_ = node->as<PrefixUnaryExpression>()->Operator;
		operandNode = node->as<PrefixUnaryExpression>()->Operand;
	} else {
		operator_ = node->as<PostfixUnaryExpression>()->Operator;
		operandNode = node->as<PostfixUnaryExpression>()->Operand;
	}

	if (operator_ == Kind::PlusPlusToken ||
		operator_ == Kind::MinusMinusToken) {
		Node* operand = skipParentheses(operandNode);
		if (isSuperProperty(operand) && classThis != nullptr &&
			classSuper != nullptr) {
			Node* setterName = nullptr;
			if (isElementAccessExpression(operand)) {
				setterName = visitor()->visitNode(
					operand->as<ElementAccessExpression>()
						->ArgumentExpression);
			} else if (isPropertyAccessExpression(operand) &&
			           isIdentifier(
			               operand->as<PropertyAccessExpression>()->name)) {
				setterName = f->newStringLiteralFromNode(
					operand->as<PropertyAccessExpression>()->name);
			}
			if (setterName != nullptr) {
				Node* getterName = setterName;
				if (!isSimpleInlineableExpression(setterName)) {
					getterName = f->newTempVariable();
					ec->addVariableDeclaration(getterName);
					setterName = f->newAssignmentExpression(getterName,
					                                        setterName);
				}

				Node* expression = f->newReflectGetCall(
					classSuper, getterName, classThis);
				ec->setOriginal(expression, node);
				expression->loc = node->loc;

				// If the result of this expression is discarded (i.e., it's
				// in a position where the result will be otherwise unused,
				// such as in an expression statement or the left side of a
				// comma), we don't need to create an extra temp variable to
				// hold the result:
				//
				//  source (discarded):
				//    super.x++;
				//  generated:
				//    _a = Reflect.get(_super, "x"), _a++,
				//    Reflect.set(_super, "x", _a);
				//
				// Above, the temp variable `_a` is used to perform the
				// correct coercion (i.e., number or bigint). Since the result
				// of the postfix unary is discarded, we don't need to capture
				// the result of the expression.
				//
				//  source (not discarded):
				//    y = super.x++;
				//  generated:
				//    y = (_a = Reflect.get(_super, "x"), _b = _a++,
				//    Reflect.set(_super, "x", _a), _b);
				//
				// When the result isn't discarded, we introduce a new temp
				// variable (`_b`) to capture the result of the operation so
				// that we can provide it to `y` when the assignment is
				// complete.
				Node* temp = nullptr;
				if (!discarded) {
					temp = f->newTempVariable();
					ec->addVariableDeclaration(temp);
				}

				expression =
					expandPreOrPostfixIncrementOrDecrementExpression(
						f, ec, node, expression, temp);

				expression = f->newReflectSetCall(classSuper, setterName,
				                                  expression, classThis);
				ec->setOriginal(expression, node);
				expression->loc = node->loc;

				if (temp != nullptr) {
					expression =
						f->newCommaExpression(expression, temp);
					expression->loc = node->loc;
				}

				return expression;
			}
		}
	}

	return visitor()->visitEachChild(node);
}

// visitReferencedPropertyName — esdecorator.go:2099
std::pair<Node*, Node*> esDecoratorTransformer::visitReferencedPropertyName(
	Node* node) {
	printer::NodeFactory* f = factory();
	if (isPropertyNameLiteral(node) || isPrivateIdentifier(node)) {
		return {f->newStringLiteralFromNode(node), visitor()->visitNode(node)};
	}

	ComputedPropertyName* cpn = node->as<ComputedPropertyName>();
	if (isPropertyNameLiteral(cpn->Expression) &&
		!isIdentifier(cpn->Expression)) {
		return {f->newStringLiteralFromNode(cpn->Expression),
		        visitor()->visitNode(node)};
	}

	Node* referencedName = f->newGeneratedNameForNode(node);
	emitContext()->addVariableDeclaration(referencedName);

	Node* key = f->newPropKeyHelper(visitor()->visitNode(cpn->Expression));
	Node* assignment = f->newAssignmentExpression(referencedName, key);
	Node* updatedName = f->updateComputedPropertyName(
		cpn, injectPendingExpressions(assignment));
	return {referencedName, updatedName};
}

// visitPropertyName — esdecorator.go:2118
Node* esDecoratorTransformer::visitPropertyName(Node* node) {
	if (isComputedPropertyName(node)) {
		return visitComputedPropertyName(node);
	}
	return visitor()->visitNode(node);
}

// visitComputedPropertyName — esdecorator.go:2125
Node* esDecoratorTransformer::visitComputedPropertyName(Node* node) {
	ComputedPropertyName* cpn = node->as<ComputedPropertyName>();
	Node* expression = visitor()->visitNode(cpn->Expression);
	if (!isSimpleInlineableExpression(expression)) {
		expression = injectPendingExpressions(expression);
	}
	return factory()->updateComputedPropertyName(cpn, expression);
}

// visitDestructuringAssignmentTarget — esdecorator.go:2134
Node* esDecoratorTransformer::visitDestructuringAssignmentTarget(Node* node) {
	if (isObjectLiteralExpression(node) || isArrayLiteralExpression(node)) {
		return visitAssignmentPattern(node);
	}

	if (isSuperProperty(node) && classThis != nullptr &&
		classSuper != nullptr) {
		printer::NodeFactory* f = factory();
		printer::EmitContext* ec = emitContext();
		Node* propertyName = nullptr;
		if (isElementAccessExpression(node)) {
			propertyName = visitor()->visitNode(
				node->as<ElementAccessExpression>()->ArgumentExpression);
		} else if (isPropertyAccessExpression(node) &&
		           isIdentifier(
		               node->as<PropertyAccessExpression>()->name)) {
			propertyName = f->newStringLiteralFromNode(
				node->as<PropertyAccessExpression>()->name);
		}
		if (propertyName != nullptr) {
			Node* paramName = f->newTempVariable();
			Node* expression = f->newAssignmentTargetWrapper(
				paramName,
				f->newReflectSetCall(
					classSuper,
					propertyName,
					paramName,
					classThis));
			ec->setOriginal(expression, node);
			expression->loc = node->loc;
			return expression;
		}
	}

	return visitor()->visitEachChild(node);
}

// visitAssignmentElement — esdecorator.go:2168
Node* esDecoratorTransformer::visitAssignmentElement(Node* node) {
	// 13.15.5.5 RS: IteratorDestructuringAssignmentEvaluation
	//   AssignmentElement : DestructuringAssignmentTarget Initializer?
	//     ...
	//     4. If |Initializer| is present and _value_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) and
	//           IsIdentifierRef of |DestructuringAssignmentTarget| are both
	//           *true*, then
	//           i. Let _v_ be ? NamedEvaluation of |Initializer| with argument
	//              _lref_.[[ReferencedName]].
	//     ...
	if (isAssignmentExpression(node, true /*excludeCompoundAssignment*/)) {
		printer::NodeFactory* f = factory();
		BinaryExpression* bin = node->as<BinaryExpression>();
		if (isNamedEvaluationAnd(emitContext(), node,
		                         isAnonymousClassNeedingAssignedName)) {
			node = transformNamedEvaluation(
				emitContext(), node,
				canIgnoreEmptyStringLiteralInAssignedName(bin->Right), "");
			bin = node->as<BinaryExpression>();
		}
		Node* assignmentTarget =
			visitDestructuringAssignmentTarget(bin->Left);
		Node* initializer = visitor()->visitNode(bin->Right);
		return f->updateBinaryExpression(bin, nullptr, assignmentTarget,
		                                 nullptr, bin->OperatorToken,
		                                 initializer);
	}
	return visitDestructuringAssignmentTarget(node);
}

// visitAssignmentRestElement — esdecorator.go:2190
Node* esDecoratorTransformer::visitAssignmentRestElement(Node* node) {
	SpreadElement* se = node->as<SpreadElement>();
	if (isLeftHandSideExpression(se->Expression)) {
		printer::NodeFactory* f = factory();
		Node* expression = visitDestructuringAssignmentTarget(se->Expression);
		return f->updateSpreadElement(se, expression);
	}
	return visitor()->visitEachChild(node);
}

// visitArrayAssignmentElement — esdecorator.go:2200
Node* esDecoratorTransformer::visitArrayAssignmentElement(Node* node) {
	debugAssert_(node != nullptr && isExpression(node));
	if (isSpreadElement(node)) {
		return visitAssignmentRestElement(node);
	}
	if (!isOmittedExpression(node)) {
		return visitAssignmentElement(node);
	}
	return visitor()->visitEachChild(node);
}

// visitAssignmentPropertyNode — esdecorator.go:2211
Node* esDecoratorTransformer::visitAssignmentPropertyNode(Node* node) {
	// AssignmentProperty : PropertyName `:` AssignmentElement
	// AssignmentElement : DestructuringAssignmentTarget Initializer?

	// 13.15.5.6 RS: KeyedDestructuringAssignmentEvaluation
	//   AssignmentElement : DestructuringAssignmentTarget Initializer?
	//     ...
	//     3. If |Initializer| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousfunctionDefinition(|Initializer|) and
	//           IsIdentifierRef of |DestructuringAssignmentTarget| are both
	//           *true*, then
	//           i. Let _rhsValue_ be ? NamedEvaluation of |Initializer| with
	//              argument _lref_.[[ReferencedName]].
	//     ...

	printer::NodeFactory* f = factory();
	PropertyAssignment* pa = node->as<PropertyAssignment>();
	Node* name = visitor()->visitNode(pa->name);
	if (isAssignmentExpression(pa->Initializer,
	                           true /*excludeCompoundAssignment*/)) {
		Node* assignmentElement = visitAssignmentElement(pa->Initializer);
		return f->updatePropertyAssignment(pa, nullptr, name, nullptr,
		                                   nullptr, assignmentElement);
	}
	if (isLeftHandSideExpression(pa->Initializer)) {
		Node* assignmentElement =
			visitDestructuringAssignmentTarget(pa->Initializer);
		return f->updatePropertyAssignment(pa, nullptr, name, nullptr,
		                                   nullptr, assignmentElement);
	}
	return visitor()->visitEachChild(node);
}

// visitShorthandAssignmentProperty — esdecorator.go:2237
Node* esDecoratorTransformer::visitShorthandAssignmentProperty(Node* node) {
	// AssignmentProperty : IdentifierReference Initializer?

	// 13.15.5.3 RS: PropertyDestructuringAssignmentEvaluation
	//   AssignmentProperty : IdentifierReference Initializer?
	//     ...
	//     4. If |Initializer?| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*,
	//           then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument
	//              _P_.
	//     ...
	if (isNamedEvaluationAnd(emitContext(), node,
	                         isAnonymousClassNeedingAssignedName)) {
		node = transformNamedEvaluation(
			emitContext(), node,
			canIgnoreEmptyStringLiteralInAssignedName(
				node->as<ShorthandPropertyAssignment>()
					->ObjectAssignmentInitializer),
			"");
	}
	return visitor()->visitEachChild(node);
}

// visitAssignmentRestProperty — esdecorator.go:2253
Node* esDecoratorTransformer::visitAssignmentRestProperty(Node* node) {
	SpreadAssignment* sa = node->as<SpreadAssignment>();
	if (isLeftHandSideExpression(sa->Expression)) {
		printer::NodeFactory* f = factory();
		Node* expression = visitDestructuringAssignmentTarget(sa->Expression);
		return f->updateSpreadAssignment(sa, expression);
	}
	return visitor()->visitEachChild(node);
}

// visitObjectAssignmentElement — esdecorator.go:2263
Node* esDecoratorTransformer::visitObjectAssignmentElement(Node* node) {
	debugAssert_(node != nullptr && isObjectLiteralElement(node));
	if (isSpreadAssignment(node)) {
		return visitAssignmentRestProperty(node);
	}
	if (isShorthandPropertyAssignment(node)) {
		return visitShorthandAssignmentProperty(node);
	}
	if (isPropertyAssignment(node)) {
		return visitAssignmentPropertyNode(node);
	}
	return visitor()->visitEachChild(node);
}

// visitAssignmentPattern — esdecorator.go:2277
Node* esDecoratorTransformer::visitAssignmentPattern(Node* node) {
	printer::NodeFactory* f = factory();
	if (isArrayLiteralExpression(node)) {
		ArrayLiteralExpression* ale = node->as<ArrayLiteralExpression>();
		NodeList* elements =
			arrayAssignmentVisitor->visitNodes(ale->Elements);
		return f->updateArrayLiteralExpression(ale, elements, ale->MultiLine);
	}
	ObjectLiteralExpression* ole = node->as<ObjectLiteralExpression>();
	NodeList* properties =
		objectAssignmentVisitor->visitNodes(ole->Properties);
	return f->updateObjectLiteralExpression(ole, properties, ole->MultiLine);
}

// visitExportAssignment — esdecorator.go:2289
Node* esDecoratorTransformer::visitExportAssignment(Node* node) {
	// 16.2.3.7 RS: Evaluation
	//   ExportDeclaration : `export` `default` AssignmentExpression `;`
	//     1. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is
	//        *true*, then
	//        a. Let _value_ be ? NamedEvaluation of |AssignmentExpression|
	//           with argument `"default"`.
	//     ...
	return visitNamedEvaluationSite(node, node->expression());
}

// visitParenthesizedExpression — esdecorator.go:2298
Node* esDecoratorTransformer::visitParenthesizedExpression(Node* node,
                                                           bool discarded) {
	// 8.4.5 RS: NamedEvaluation
	//   ParenthesizedExpression : `(` Expression `)`
	//     ...
	//     2. Return ? NamedEvaluation of |Expression| with argument _name_.

	printer::NodeFactory* f = factory();
	ParenthesizedExpression* pe = node->as<ParenthesizedExpression>();
	Node* expression;
	if (discarded) {
		expression = discardedVisitor->visitNode(pe->Expression);
	} else {
		expression = visitor()->visitNode(pe->Expression);
	}
	return f->updateParenthesizedExpression(pe, expression);
}

// visitPartiallyEmittedExpression — esdecorator.go:2315
Node* esDecoratorTransformer::visitPartiallyEmittedExpression(Node* node,
                                                              bool discarded) {
	// Emulates 8.4.5 RS: NamedEvaluation
	PartiallyEmittedExpression* pe = node->as<PartiallyEmittedExpression>();
	Node* expression;
	if (discarded) {
		expression = discardedVisitor->visitNode(pe->Expression);
	} else {
		expression = visitor()->visitNode(pe->Expression);
	}
	return factory()->updatePartiallyEmittedExpression(pe, expression);
}

// prependExpressions — esdecorator.go:2329. Prepends a list of expressions
// before a target expression, preserving parenthesization. If expression is
// nil, the pending expressions are inlined alone.
Node* esDecoratorTransformer::prependExpressions(
	std::vector<Node*> pending, Node* expression) {
	printer::NodeFactory* f = factory();
	if (pending.empty()) {
		return expression;
	}
	if (expression == nullptr) {
		return f->inlineExpressions(pending);
	}
	if (isParenthesizedExpression(expression)) {
		ParenthesizedExpression* pe =
			expression->as<ParenthesizedExpression>();
		std::vector<Node*> exprs(pending.begin(), pending.end());
		exprs.push_back(pe->Expression);
		return f->updateParenthesizedExpression(pe, f->inlineExpressions(exprs));
	}
	std::vector<Node*> exprs(pending.begin(), pending.end());
	exprs.push_back(expression);
	return f->inlineExpressions(exprs);
}

// injectPendingExpressions — esdecorator.go:2350
Node* esDecoratorTransformer::injectPendingExpressions(Node* expression) {
	Node* result = prependExpressions(pendingExpressions, expression);
	debugAssert_(result != nullptr);
	if (result != expression) {
		pendingExpressions.clear();
	}
	return result;
}

// injectPendingInitializers — esdecorator.go:2359
Node* esDecoratorTransformer::injectPendingInitializers(classInfo* ci,
                                                        bool isStatic_,
                                                        Node* expression) {
	std::vector<Node*>* pending;
	if (isStatic_) {
		pending = &ci->pendingStaticInitializers;
	} else {
		pending = &ci->pendingInstanceInitializers;
	}
	Node* result = prependExpressions(*pending, expression);
	if (result != expression) {
		pending->clear();
	}
	return result;
}

// transformAllDecoratorsOfDeclaration — esdecorator.go:2374. Transforms all
// of the decorators for a declaration into an array of expressions.
std::vector<Node*> esDecoratorTransformer::transformAllDecoratorsOfDeclaration(
	const std::vector<Node*>& decorators) {
	if (decorators.empty()) {
		return {};
	}
	std::vector<Node*> result;
	result.reserve(decorators.size());
	for (Node* d : decorators) {
		result.push_back(transformDecorator(d));
	}
	return result;
}

// transformDecorator — esdecorator.go:2386. Transforms a decorator into an
// expression.
Node* esDecoratorTransformer::transformDecorator(Node* decorator) {
	Node* expression =
		visitor()->visitNode(decorator->as<Decorator>()->Expression);
	emitContext()->setEmitFlags(expression, printer::EFNoComments);

	// preserve the 'this' binding for an access expression
	Node* innerExpression = skipOuterExpressions(expression, OEKAll);
	if (isAccessExpression(innerExpression)) {
		auto pair = createCallBinding(expression);
		Node* bindCall = factory()->newFunctionBindCall(pair.first,
		                                              pair.second, {});
		return factory()->restoreOuterExpressions(expression, bindCall,
		                                          OEKAll);
	}
	return expression;
}

// createCallBinding — esdecorator.go:2400
std::pair<Node*, Node*> esDecoratorTransformer::createCallBinding(
	Node* expression) {
	printer::NodeFactory* f = factory();
	Node* callee = skipOuterExpressions(expression, OEKAll);
	if (isSuperProperty(callee)) {
		return {callee, f->newThisExpression()};
	}
	if (callee->kind == Kind::SuperKeyword) {
		return {callee, f->newThisExpression()};
	}
	if ((emitContext()->emitFlags(callee) & printer::EFHelperName) != 0) {
		return {callee, f->newVoidZeroExpression()};
	}
	if (isPropertyAccessExpression(callee)) {
		PropertyAccessExpression* pa =
			callee->as<PropertyAccessExpression>();
		if (shouldBeCapturedInTempVariable(pa->Expression)) {
			Node* thisArg = f->newTempVariable();
			emitContext()->addVariableDeclaration(thisArg);
			Node* assign =
				f->newAssignmentExpression(thisArg, pa->Expression);
			assign->loc = pa->Expression->loc;
			Node* target = f->newPropertyAccessExpression(
				assign, nullptr, pa->name, NodeFlagsNone);
			target->loc = callee->loc;
			return {target, thisArg};
		}
		return {callee, pa->Expression};
	}
	if (isElementAccessExpression(callee)) {
		ElementAccessExpression* ea =
			callee->as<ElementAccessExpression>();
		if (shouldBeCapturedInTempVariable(ea->Expression)) {
			Node* thisArg = f->newTempVariable();
			emitContext()->addVariableDeclaration(thisArg);
			Node* assign =
				f->newAssignmentExpression(thisArg, ea->Expression);
			assign->loc = ea->Expression->loc;
			Node* target = f->newElementAccessExpression(
				assign, nullptr, ea->ArgumentExpression, NodeFlagsNone);
			target->loc = callee->loc;
			return {target, thisArg};
		}
		return {callee, ea->Expression};
	}
	return {expression, f->newVoidZeroExpression()};
}

// shouldBeCapturedInTempVariable — esdecorator.go:2441
bool esDecoratorTransformer::shouldBeCapturedInTempVariable(Node* node) {
	// This is a simplified version of the general shouldBeCapturedInTempVariable
	// from nodeFactory with cacheIdentifiers=true, since createCallBinding in
	// this transform always caches identifiers.
	Node* target = skipParentheses(node);
	switch (target->kind) {
	case Kind::Identifier:
		// cacheIdentifiers is always true for this transform's
		// createCallBinding
		return true;
	case Kind::ThisKeyword:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::StringLiteral:
		return false;
	default:
		return true;
	}
}

// createDescriptorMethod — esdecorator.go:2462. Creates a "value", "get", or
// "set" method for a pseudo-PropertyDescriptor object created for a private
// element.
Node* esDecoratorTransformer::createDescriptorMethod(
	Node* original, Node* name, ModifierList* modifiers, Node* asteriskToken,
	const std::string& kind, NodeList* parameters, Node* body) {
	printer::NodeFactory* f = factory();
	printer::EmitContext* ec = emitContext();

	if (body == nullptr) {
		body = f->newBlock(f->newNodeList({}), false);
	}

	Node* funcExpr = f->newFunctionExpression(
		modifiers,
		asteriskToken,
		nullptr,  // name
		nullptr,  // typeParameters
		parameters,
		nullptr,  // type
		nullptr,  // fullSignature
		body);
	ec->setOriginal(funcExpr, original);
	ec->setSourceMapRange(funcExpr, moveRangePastDecorators(original));
	ec->setEmitFlags(funcExpr, printer::EFNoComments);

	std::string prefix;
	if (kind == "get" || kind == "set") {
		prefix = kind;
	}
	Node* functionName = f->newStringLiteralFromNode(name);
	Node* namedFunction =
		f->newSetFunctionNameHelper(funcExpr, functionName, prefix);

	Node* method = f->newPropertyAssignment(nullptr, f->newIdentifier(kind),
	                                        nullptr, nullptr, namedFunction);
	ec->setOriginal(method, original);
	ec->setSourceMapRange(method, moveRangePastDecorators(original));
	ec->setEmitFlags(method, printer::EFNoComments);
	return method;
}

// createMethodDescriptorObject — esdecorator.go:2507. Creates a
// pseudo-PropertyDescriptor object used when decorating a private
// MethodDeclaration.
Node* esDecoratorTransformer::createMethodDescriptorObject(
	Node* member, ModifierList* modifiers) {
	printer::NodeFactory* f = factory();
	NodeList* parameters = visitor()->visitNodes(member->parameterList());
	Node* body = visitor()->visitNode(member->body());
	MethodDeclaration* method = member->as<MethodDeclaration>();
	return f->newObjectLiteralExpression(
		f->newNodeList({createDescriptorMethod(member, member->name(),
		                                       modifiers, method->AsteriskToken,
		                                       "value", parameters, body)}),
		false);
}

// createGetAccessorDescriptorObject — esdecorator.go:2521. Creates a
// pseudo-PropertyDescriptor object used when decorating a private
// GetAccessor.
Node* esDecoratorTransformer::createGetAccessorDescriptorObject(
	Node* member, ModifierList* modifiers) {
	printer::NodeFactory* f = factory();
	Node* body = visitor()->visitNode(member->body());
	return f->newObjectLiteralExpression(
		f->newNodeList({createDescriptorMethod(
			member, member->name(), modifiers, nullptr, "get",
			f->newNodeList({}), body)}),
		false);
}

// createSetAccessorDescriptorObject — esdecorator.go:2533. Creates a
// pseudo-PropertyDescriptor object used when decorating a private
// SetAccessor.
Node* esDecoratorTransformer::createSetAccessorDescriptorObject(
	Node* member, ModifierList* modifiers) {
	printer::NodeFactory* f = factory();
	NodeList* parameters = visitor()->visitNodes(member->parameterList());
	Node* body = visitor()->visitNode(member->body());
	return f->newObjectLiteralExpression(
		f->newNodeList({createDescriptorMethod(member, member->name(),
		                                       modifiers, nullptr, "set",
		                                       parameters, body)}),
		false);
}

// createAccessorPropertyDescriptorObject — esdecorator.go:2547. Creates a
// pseudo-PropertyDescriptor object used when decorating a private
// auto-accessor PropertyDeclaration. The descriptor contains get/set methods
// that access the generated backing field.
Node* esDecoratorTransformer::createAccessorPropertyDescriptorObject(
	Node* member, ModifierList* /*modifiers*/) {
	//  {
	//      get() { return this.${privateName}; },
	//      set(value) { this.${privateName} = value; },
	//  }
	printer::NodeFactory* f = factory();
	Node* backingFieldName = f->newGeneratedPrivateNameForNode(
		member->name(), printer::AutoGenerateOptions{0, "", "_accessor_storage"});
	return f->newObjectLiteralExpression(
		f->newNodeList({
			createDescriptorMethod(
				member, member->name(), nullptr, nullptr, "get",
				f->newNodeList({}),
				f->newBlock(f->newNodeList({
								f->newReturnStatement(
									f->newPropertyAccessExpression(
										f->newThisExpression(), nullptr,
										backingFieldName, NodeFlagsNone)),
							}),
				            false)),
			createDescriptorMethod(
				member, member->name(), nullptr, nullptr, "set",
				f->newNodeList({
					f->newParameterDeclaration(nullptr, nullptr,
					                           f->newIdentifier("value"), nullptr,
					                           nullptr, nullptr),
				}),
				f->newBlock(f->newNodeList({
								f->newExpressionStatement(
									f->newAssignmentExpression(
										f->newPropertyAccessExpression(
											f->newThisExpression(), nullptr,
											backingFieldName, NodeFlagsNone),
										f->newIdentifier("value"))),
							}),
				            false)),
		}),
		false);
}

// createMethodDescriptorForwarder — esdecorator.go:2585. Creates a
// MethodDeclaration that forwards its invocation to a PropertyDescriptor
// object.
Node* esDecoratorTransformer::createMethodDescriptorForwarder(
	ModifierList* modifiers, Node* name, Node* descriptorName) {
	printer::NodeFactory* f = factory();
	ModifierList* staticOnly =
		staticOnlyModifierVisitor->visitModifiers(modifiers);
	return f->newGetAccessorDeclaration(
		staticOnly,
		name,
		nullptr,  // typeParameters
		f->newNodeList({}),
		nullptr,  // type
		nullptr,  // fullSignature
		f->newBlock(f->newNodeList({
						f->newReturnStatement(
							f->newPropertyAccessExpression(
								descriptorName, nullptr,
								f->newIdentifier("value"), NodeFlagsNone)),
					}),
		            false));
}

// createGetAccessorDescriptorForwarder — esdecorator.go:2604. Creates a
// GetAccessor that forwards its invocation to a PropertyDescriptor object.
Node* esDecoratorTransformer::createGetAccessorDescriptorForwarder(
	ModifierList* modifiers, Node* name, Node* descriptorName) {
	printer::NodeFactory* f = factory();
	ModifierList* staticOnly =
		staticOnlyModifierVisitor->visitModifiers(modifiers);
	return f->newGetAccessorDeclaration(
		staticOnly,
		name,
		nullptr,  // typeParameters
		f->newNodeList({}),
		nullptr,  // type
		nullptr,  // fullSignature
		f->newBlock(f->newNodeList({
						f->newReturnStatement(f->newFunctionCallCall(
							f->newPropertyAccessExpression(
								descriptorName, nullptr,
								f->newIdentifier("get"), NodeFlagsNone),
							f->newThisExpression(), {})),
					}),
		            false));
}

// createSetAccessorDescriptorForwarder — esdecorator.go:2627. Creates a
// SetAccessor that forwards its invocation to a PropertyDescriptor object.
Node* esDecoratorTransformer::createSetAccessorDescriptorForwarder(
	ModifierList* modifiers, Node* name, Node* descriptorName) {
	printer::NodeFactory* f = factory();
	ModifierList* staticOnly =
		staticOnlyModifierVisitor->visitModifiers(modifiers);
	return f->newSetAccessorDeclaration(
		staticOnly,
		name,
		nullptr,  // typeParameters
		f->newNodeList({
			f->newParameterDeclaration(nullptr, nullptr,
			                           f->newIdentifier("value"), nullptr,
			                           nullptr, nullptr),
		}),
		nullptr,  // type
		nullptr,  // fullSignature
		f->newBlock(f->newNodeList({
						f->newReturnStatement(f->newFunctionCallCall(
							f->newPropertyAccessExpression(
								descriptorName, nullptr,
								f->newIdentifier("set"), NodeFlagsNone),
							f->newThisExpression(),
							{f->newIdentifier("value")})),
					}),
		            false));
}

// createMetadata — esdecorator.go:2651
Node* esDecoratorTransformer::createMetadata(Node* name, Node* classSuper_) {
	printer::NodeFactory* f = factory();

	Node* superMetadata;
	if (classSuper_ != nullptr) {
		superMetadata = createSymbolMetadataReference(classSuper_);
	} else {
		superMetadata = f->newToken(Kind::NullKeyword);
	}

	Node* objectCreate = f->newCallExpression(
		f->newPropertyAccessExpression(f->newIdentifier("Object"), nullptr,
		                               f->newIdentifier("create"),
		                               NodeFlagsNone),
		nullptr, nullptr,
		f->newNodeList({superMetadata}),
		NodeFlagsNone);

	Node* symbolCheck = f->newLogicalANDExpression(
		f->newTypeCheck(f->newIdentifier("Symbol"), "function"),
		f->newPropertyAccessExpression(f->newIdentifier("Symbol"), nullptr,
		                               f->newIdentifier("metadata"),
		                               NodeFlagsNone));

	Node* conditional = f->newConditionalExpression(
		symbolCheck,
		f->newToken(Kind::QuestionToken),
		objectCreate,
		f->newToken(Kind::ColonToken),
		f->newVoidZeroExpression());

	Node* varDecl =
		f->newVariableDeclaration(name, nullptr, nullptr, conditional);
	Node* varDeclList = f->newVariableDeclarationList(
		f->newNodeList({varDecl}), NodeFlagsConst);
	return f->newVariableStatement(nullptr, varDeclList);
}

// createSymbolMetadata — esdecorator.go:2686
Node* esDecoratorTransformer::createSymbolMetadata(Node* target, Node* value) {
	printer::NodeFactory* f = factory();

	// Object.defineProperty(target, Symbol.metadata, { configurable: true,
	// writable: true, enumerable: true, value })
	Node* symbolMetadata = f->newPropertyAccessExpression(
		f->newIdentifier("Symbol"), nullptr, f->newIdentifier("metadata"),
		NodeFlagsNone);

	std::vector<Node*> descriptorProps{
		f->newPropertyAssignment(nullptr, f->newIdentifier("enumerable"),
		                         nullptr, nullptr, f->newTrueExpression()),
		f->newPropertyAssignment(nullptr, f->newIdentifier("configurable"),
		                         nullptr, nullptr, f->newTrueExpression()),
		f->newPropertyAssignment(nullptr, f->newIdentifier("writable"),
		                         nullptr, nullptr, f->newTrueExpression()),
		f->newPropertyAssignment(nullptr, f->newIdentifier("value"), nullptr,
		                         nullptr, value),
	};
	Node* descriptor =
		f->newObjectLiteralExpression(f->newNodeList(descriptorProps), false);

	Node* defineProperty = f->newCallExpression(
		f->newPropertyAccessExpression(f->newIdentifier("Object"), nullptr,
		                               f->newIdentifier("defineProperty"),
		                               NodeFlagsNone),
		nullptr, nullptr,
		f->newNodeList({target, symbolMetadata, descriptor}),
		NodeFlagsNone);

	Node* ifStatement = f->newIfStatement(
		value, f->newExpressionStatement(defineProperty), nullptr);
	emitContext()->setEmitFlags(ifStatement, printer::EFSingleLine);
	return ifStatement;
}

// createSymbolMetadataReference — esdecorator.go:2712
Node* esDecoratorTransformer::createSymbolMetadataReference(
	Node* classSuper_) {
	printer::NodeFactory* f = factory();
	Node* symbolMetadata = f->newPropertyAccessExpression(
		f->newIdentifier("Symbol"), nullptr, f->newIdentifier("metadata"),
		NodeFlagsNone);
	Node* elementAccess = f->newElementAccessExpression(
		classSuper_, nullptr, symbolMetadata, NodeFlagsNone);
	return f->newBinaryExpression(nullptr, elementAccess, nullptr,
	                              f->newToken(Kind::QuestionQuestionToken),
	                              f->newToken(Kind::NullKeyword));
}

// injectClassThisAssignmentIfMissing — esdecorator.go:2719
Node* injectClassThisAssignmentIfMissing(printer::EmitContext* ec,
                                         printer::NodeFactory* f, Node* node,
                                         Node* classThis) {
	if (classHasClassThisAssignment(ec, node)) {
		return node;
	}

	// Create: static { _classThis = this; }
	Node* expression =
		f->newAssignmentExpression(classThis, f->newThisExpression());
	Node* statement = f->newExpressionStatement(expression);
	Node* body = f->newBlock(f->newNodeList({statement}), false);
	Node* staticBlock = f->newClassStaticBlockDeclaration(nullptr, body);
	ec->setClassThis(staticBlock, classThis);

	if (node->name() != nullptr) {
		ec->setSourceMapRange(statement, node->name()->loc);
	}

	std::vector<Node*> newMembers;
	newMembers.reserve(1 + node->members().size());
	newMembers.push_back(staticBlock);
	for (Node* m : node->members()) {
		newMembers.push_back(m);
	}
	NodeList* membersList = f->newNodeList(newMembers);
	membersList->loc = node->memberList()->loc;

	Node* updatedNode;
	if (isClassDeclaration(node)) {
		ClassDeclaration* cd = node->as<ClassDeclaration>();
		updatedNode = f->updateClassDeclaration(cd, cd->modifiers,
		                                        cd->name, nullptr,
		                                        cd->HeritageClauses,
		                                        membersList);
	} else {
		ClassExpression* ce = node->as<ClassExpression>();
		updatedNode = f->updateClassExpression(ce, ce->modifiers,
		                                       ce->name, nullptr,
		                                       ce->HeritageClauses,
		                                       membersList);
	}
	ec->setClassThis(updatedNode, classThis);
	return updatedNode;
}

}  // namespace tsc::transformers::estransforms

