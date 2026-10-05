// Port of tsc/internal/transformers/jsxtransforms/jsx.go
#include "internal/transformers/jsxtransforms/jsxtransforms.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/printer/emitresolver.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::transformers::jsxtransforms {
namespace {

// jsx.go:19 — JSXTransformer
struct JSXTransformer : Transformer {
	const CompilerOptions* compilerOptions;
	EmitResolver* emitResolver;

	std::string importSpecifier;
	Node* filenameDeclaration = nullptr; // Declaration for the file name in --jsx react-jsxdev
	collections::OrderedMap<std::string, std::unordered_map<std::string, Node*>>
		utilizedImplicitRuntimeImports;
	bool inJsxChild = false;

	SourceFile* currentSourceFile = nullptr;
};

// Forward declarations — definitions appear below in jsx.go order.
Node* visit(JSXTransformer* tx, Node* node);
void setInChild(JSXTransformer* tx, bool v);
Node* getImplicitImportForName(JSXTransformer* tx, const std::string& name);
Node* getJsxFactoryCallee(JSXTransformer* tx, bool isStaticChildren);
Node* getImplicitJsxFragmentReference(JSXTransformer* tx);
Node* getCurrentFileNameExpression(JSXTransformer* tx);
Node* visitSourceFile(JSXTransformer* tx, SourceFile* file);
Node* visitJsxElement(JSXTransformer* tx, JsxElement* element);
Node* visitJsxSelfClosingElement(JSXTransformer* tx, JsxSelfClosingElement* element);
Node* visitJsxFragment(JSXTransformer* tx, JsxFragment* fragment);
Node* convertJsxChildrenToChildrenPropObject(JSXTransformer* tx,
                                             const std::vector<Node*>& children);
Node* convertJsxChildrenToChildrenPropAssignment(JSXTransformer* tx,
                                                 const std::vector<Node*>& children);
Node* transformJsxChildToExpression(JSXTransformer* tx, Node* node);
Node* getTagName(JSXTransformer* tx, Node* node);
Node* visitJsxOpeningLikeElementJSX(JSXTransformer* tx, Node* element,
                                    NodeList* children, TextRange location);
Node* visitJsxOpeningLikeElementCreateElement(JSXTransformer* tx, Node* element,
                                              NodeList* children, TextRange location);
Node* visitJsxOpeningLikeElementOrFragmentJSX(JSXTransformer* tx, Node* tagName,
                                             Node* object, Node* keyAttr,
                                             NodeList* children,
                                             TextRange location);
Node* visitJsxOpeningFragmentJSX(JSXTransformer* tx, JsxOpeningFragment* fragment,
                                 NodeList* children, TextRange location);
Node* visitJsxOpeningFragmentCreateElement(JSXTransformer* tx, JsxOpeningFragment* fragment,
                                           NodeList* children, TextRange location);
Node* transformJsxAttributesToObjectProps(JSXTransformer* tx,
                                          const std::vector<Node*>& attrs,
                                          Node* childrenProp);
Node* transformJsxAttributesToExpression(JSXTransformer* tx,
                                         const std::vector<Node*>& attrs,
                                         Node* childrenProp);
std::vector<Node*> transformJsxAttributesToProps(JSXTransformer* tx,
                                               const std::vector<Node*>& attrs,
                                               Node* childrenProp);
std::vector<Node*> transformJsxSpreadAttributesToProps(JSXTransformer* tx,
                                                       JsxSpreadAttribute* node);
Node* transformJsxAttributeToObjectLiteralElement(JSXTransformer* tx,
                                                  JsxAttribute* node);
Node* getAttributeName(JSXTransformer* tx, JsxAttribute* node);
Node* transformJsxAttributeInitializer(JSXTransformer* tx, Node* node);
Node* createReactNamespace(JSXTransformer* tx, std::string reactNamespace,
                           Node* parent);
Node* createJsxFactoryExpressionFromEntityName(JSXTransformer* tx, Node* e,
                                               Node* parent);
Node* createJsxPseudoFactoryExpression(JSXTransformer* tx, Node* parent, Node* e,
                                       const std::string& target);
Node* createJsxFactoryExpression(JSXTransformer* tx, Node* parent);
Node* createJsxFragmentFactoryExpression(JSXTransformer* tx, Node* parent);
Node* visitJsxText(JSXTransformer* tx, JsxText* text);
Node* visitJsxExpression(JSXTransformer* tx, JsxExpression* expression);
bool hasKeyAfterPropsSpread(Node* node);
bool shouldUseCreateElement(JSXTransformer* tx, Node* node);
bool isAnyPrologueDirective(JSXTransformer* tx, Node* node);
std::vector<Node*> insertStatementAfterCustomPrologue(JSXTransformer* tx,
                                                    std::vector<Node*> to,
                                                    Node* statement);
int sortImportSpecifiers(Node* a, Node* b);
std::vector<Node*> getSortedSpecifiers(
	const std::unordered_map<std::string, Node*>& m);
bool hasProto(ObjectLiteralExpression* obj);
void addLineOfJsxText(std::string& b, std::string_view trimmedLine,
                      bool isInitial);
std::string fixupWhitespaceAndDecodeEntities(std::string_view text);
std::string decodeEntities(std::string_view text);
bool decodeEntity(std::string_view entity, char32_t& out);

// jsx.go:32 — NewJSXTransformer
JSXTransformer* createJSXTransformer(TransformOptions* opts) {
	const CompilerOptions* compilerOptions = opts->CompilerOptions;
	printer::EmitContext* emitContext = opts->Context;
	auto* tx = new JSXTransformer();
	tx->compilerOptions = compilerOptions;
	tx->emitResolver = opts->emitResolver;
	tx->newTransformer([tx](Node* node) { return visit(tx, node); }, emitContext);
	return tx;
}

// jsx.go:42 — getCurrentFileNameExpression
Node* getCurrentFileNameExpression(JSXTransformer* tx) {
	if (tx->filenameDeclaration != nullptr) {
		return tx->filenameDeclaration->as<VariableDeclaration>()->name;
	}
	Node* d = tx->factory()->newVariableDeclaration(
		tx->factory()->newUniqueName(
			"_jsxFileName",
			printer::AutoGenerateOptions{
				.Flags = printer::GeneratedIdentifierFlagsOptimistic |
					printer::GeneratedIdentifierFlagsFileLevel,
			}),
		nullptr,
		nullptr,
		tx->factory()->newStringLiteral(tx->currentSourceFile->FileName(), TokenFlagsNone));
	tx->filenameDeclaration = d;
	return d->as<VariableDeclaration>()->name;
}

// jsx.go:58 — getJsxFactoryCalleePrimitive
std::string getJsxFactoryCalleePrimitive(JSXTransformer* tx, bool isStaticChildren) {
	if (tx->compilerOptions->Jsx == JsxEmit::ReactJSXDev) {
		return "jsxDEV";
	}
	if (isStaticChildren) {
		return "jsxs";
	}
	return "jsx";
}

// jsx.go:68 — getJsxFactoryCallee
Node* getJsxFactoryCallee(JSXTransformer* tx, bool isStaticChildren) {
	std::string t = getJsxFactoryCalleePrimitive(tx, isStaticChildren);
	return getImplicitImportForName(tx, t);
}

// jsx.go:73 — getImplicitJsxFragmentReference
Node* getImplicitJsxFragmentReference(JSXTransformer* tx) {
	return getImplicitImportForName(tx, "Fragment");
}

// jsx.go:77 — getImplicitImportForName
Node* getImplicitImportForName(JSXTransformer* tx, const std::string& name) {
	std::string importSource = tx->importSpecifier;
	if (name != "createElement") {
		importSource = getJSXRuntimeImport(importSource, tx->compilerOptions);
	}
	auto getResult = tx->utilizedImplicitRuntimeImports.Get(importSource);
	auto* existing = getResult.first;
	if (existing != nullptr) {
		auto it = existing->find(name);
		if (it != existing->end()) {
			return it->second->as<ImportSpecifier>()->name;
		}
	} else {
		tx->utilizedImplicitRuntimeImports.Set(
			importSource, std::unordered_map<std::string, Node*>{});
		existing = tx->utilizedImplicitRuntimeImports.Get(importSource).first;
	}

	Node* generatedName = tx->factory()->newUniqueName(
		"_" + name,
		printer::AutoGenerateOptions{
			.Flags = printer::GeneratedIdentifierFlagsOptimistic |
				printer::GeneratedIdentifierFlagsFileLevel |
				printer::GeneratedIdentifierFlagsAllowNameSubstitution,
		});
	Node* specifier = tx->factory()->newImportSpecifier(
		false, tx->factory()->newIdentifier(name), generatedName);
	tx->emitResolver->SetReferencedImportDeclaration(generatedName, specifier);
	(*existing)[name] = specifier;
	return specifier->as<ImportSpecifier>()->name;
}

// jsx.go:102 — setInChild
void setInChild(JSXTransformer* tx, bool v) {
	tx->inJsxChild = v;
}

// jsx.go:106 — visit
Node* visit(JSXTransformer* tx, Node* node) {
	if (node == nullptr) {
		return nullptr;
	}
	if ((node->subtreeFacts() & SubtreeContainsJsx) == 0) {
		return node;
	}
	switch (node->kind) {
	case Kind::SourceFile:
		setInChild(tx, false);
		return visitSourceFile(tx, node->as<SourceFile>());
	case Kind::JsxElement:
		return visitJsxElement(tx, node->as<JsxElement>());
	case Kind::JsxSelfClosingElement:
		return visitJsxSelfClosingElement(tx, node->as<JsxSelfClosingElement>());
	case Kind::JsxFragment:
		return visitJsxFragment(tx, node->as<JsxFragment>());
	case Kind::JsxOpeningElement:
		TSC_UNREACHABLE("JsxOpeningElement should not be visited, handled in visitJsxElement");
	case Kind::JsxOpeningFragment:
		TSC_UNREACHABLE("JsxOpeningFragment should not be visited, handled in visitJsxFragment");
	case Kind::JsxText:
		setInChild(tx, false);
		return visitJsxText(tx, node->as<JsxText>());
	case Kind::JsxExpression:
		setInChild(tx, false);
		return visitJsxExpression(tx, node->as<JsxExpression>());
	default:
		break;
	}
	setInChild(tx, false);
	return tx->visitor()->visitEachChild(node); // by default, do nothing
}

// jsx.go:141 — hasKeyAfterPropsSpread
/**
 * The react jsx/jsxs transform falls back to `createElement` when an explicit `key` argument comes after a spread
 */
bool hasKeyAfterPropsSpread(Node* node) {
	bool spread = false;
	Node* opener = node;
	if (node->kind == Kind::JsxElement) {
		opener = node->as<JsxElement>()->OpeningElement;
	} // otherwise self-closing
	for (Node* elem : opener->attributes()->properties()) {
		std::vector<Node*> elemExprProps;
		if (isJsxSpreadAttribute(elem) && isObjectLiteralExpression(elem->expression())) {
			elemExprProps = elem->expression()->properties();
		}
		if (isJsxSpreadAttribute(elem) &&
			(!isObjectLiteralExpression(elem->expression()) ||
				std::any_of(elemExprProps.begin(), elemExprProps.end(),
				            [](Node* p) { return isSpreadAssignment(p); }))) {
			spread = true;
		} else if (spread && isJsxAttribute(elem) && isIdentifier(elem->name()) &&
		           elem->name()->text() == "key") {
			return true;
		}
	}
	return false;
}

// jsx.go:157 — shouldUseCreateElement
bool shouldUseCreateElement(JSXTransformer* tx, Node* node) {
	return tx->importSpecifier.size() == 0 || hasKeyAfterPropsSpread(node);
}

// jsx.go:161 — insertStatementAfterPrologue
template<typename T>
std::vector<Node*> insertStatementAfterPrologue(
	std::vector<Node*> to,
	Node* statement,
	bool (*isPrologueDirective)(T, Node*),
	T callee) {
	if (statement == nullptr) {
		return to;
	}
	size_t statementIdx = 0;
	// skip all prologue directives to insert at the correct position
	for (; statementIdx < to.size(); statementIdx++) {
		if (!isPrologueDirective(callee, to[statementIdx])) {
			break;
		}
	}
	to.insert(to.begin() + statementIdx, statement);
	return to;
}

// jsx.go:175 — isAnyPrologueDirective
bool isAnyPrologueDirective(JSXTransformer* tx, Node* node) {
	return isPrologueDirective(node) ||
		(tx->emitContext()->emitFlags(node) & printer::EFCustomPrologue) != 0;
}

// jsx.go:179 — insertStatementAfterCustomPrologue
std::vector<Node*> insertStatementAfterCustomPrologue(JSXTransformer* tx,
                                                    std::vector<Node*> to,
                                                    Node* statement) {
	return insertStatementAfterPrologue(std::move(to), statement,
	                                    isAnyPrologueDirective, tx);
}

// jsx.go:183 — sortImportSpecifiers
int sortImportSpecifiers(Node* a, Node* b) {
	Node* aPropertyName = a->propertyName();
	Node* bPropertyName = b->propertyName();
	stringutil::Comparison res = stringutil::CompareStringsCaseSensitive(
		aPropertyName != nullptr ? aPropertyName->text() : "",
		bPropertyName != nullptr ? bPropertyName->text() : "");
	if (res != stringutil::ComparisonEqual) {
		return res;
	}
	return stringutil::CompareStringsCaseSensitive(
		a->as<ImportSpecifier>()->name->text(),
		b->as<ImportSpecifier>()->name->text());
}

// jsx.go:191 — getSortedSpecifiers
std::vector<Node*> getSortedSpecifiers(
	const std::unordered_map<std::string, Node*>& m) {
	std::vector<Node*> res;
	res.reserve(m.size());
	for (const auto& kv : m) {
		res.push_back(kv.second);
	}
	std::sort(res.begin(), res.end(), sortImportSpecifiers);
	return res;
}

// jsx.go:197 — visitSourceFile
Node* visitSourceFile(JSXTransformer* tx, SourceFile* file) {
	if (file->IsDeclarationFile) {
		return file->asNode();
	}

	tx->currentSourceFile = file;
	tx->importSpecifier = getJSXImplicitImportBase(tx->compilerOptions, file);
	tx->filenameDeclaration = nullptr;
	tx->utilizedImplicitRuntimeImports = {};

	Node* visited = tx->visitor()->visitEachChild(file->asNode());
	for (printer::EmitHelper* helper : tx->emitContext()->readEmitHelpers()) {
		tx->emitContext()->addEmitHelper(visited, helper);
	}
	std::vector<Node*> statements = visited->statements();
	bool statementsUpdated = false;
	if (tx->filenameDeclaration != nullptr) {
		statements = insertStatementAfterCustomPrologue(
			tx, statements,
			tx->factory()->newVariableStatement(
				nullptr, tx->factory()->newVariableDeclarationList(
					tx->factory()->newNodeList(
						std::vector<Node*>{tx->filenameDeclaration}),
					NodeFlagsConst)));
		statementsUpdated = true;
	}

	if (tx->utilizedImplicitRuntimeImports.Size() > 0) {
		if (isExternalModule(file)) {
			statementsUpdated = true;
			std::vector<Node*> newStatements;
			newStatements.reserve(tx->utilizedImplicitRuntimeImports.Size());
			for (const std::string& importSource :
			     tx->utilizedImplicitRuntimeImports.Keys()) {
				const auto* importSpecifiersMap =
					tx->utilizedImplicitRuntimeImports.Get(importSource).first;
				Node* s = tx->factory()->newImportDeclaration(
					nullptr,
					tx->factory()->newImportClause(
						Kind::Unknown, nullptr,
						tx->factory()->newNamedImports(
							tx->factory()->newNodeList(
								getSortedSpecifiers(*importSpecifiersMap)))),
					tx->factory()->newStringLiteral(importSource, TokenFlagsNone),
					nullptr);
				setParentInChildren(s);
				newStatements.push_back(s);
			}
			for (Node* e : newStatements) {
				statements = insertStatementAfterCustomPrologue(tx, statements, e);
			}
		} else if (isExternalOrCommonJSModule(file)) {
			statementsUpdated = true;
			std::vector<Node*> newStatements;
			newStatements.reserve(tx->utilizedImplicitRuntimeImports.Size());
			for (const std::string& importSource :
			     tx->utilizedImplicitRuntimeImports.Keys()) {
				const auto* importSpecifiersMap =
					tx->utilizedImplicitRuntimeImports.Get(importSource).first;
				std::vector<Node*> sorted = getSortedSpecifiers(*importSpecifiersMap);
				std::vector<Node*> asBindingElems;
				asBindingElems.reserve(sorted.size());
				for (Node* elem : sorted) {
					asBindingElems.push_back(tx->factory()->newBindingElement(
						nullptr, elem->propertyName(),
						elem->as<ImportSpecifier>()->name, nullptr));
				}
				Node* s = tx->factory()->newVariableStatement(
					nullptr,
					tx->factory()->newVariableDeclarationList(
						tx->factory()->newNodeList(std::vector<Node*>{
							tx->factory()->newVariableDeclaration(
								tx->factory()->newBindingPattern(
									Kind::ObjectBindingPattern,
									tx->factory()->newNodeList(asBindingElems)),
								nullptr,
								nullptr,
								tx->factory()->newCallExpression(
									tx->factory()->newIdentifier("require"),
									nullptr,
									nullptr,
									tx->factory()->newNodeList(std::vector<Node*>{
										tx->factory()->newStringLiteral(
											importSource, TokenFlagsNone)}),
									NodeFlagsNone))}),
						NodeFlagsConst));
				setParentInChildren(s);
				newStatements.push_back(s);
			}
			for (Node* e : newStatements) {
				statements = insertStatementAfterCustomPrologue(tx, statements, e);
			}
		} else {
			// Do nothing (script file) - consider an error in the checker?
		}
	}

	if (statementsUpdated) {
		visited = tx->factory()->updateSourceFile(
			file, tx->factory()->newNodeList(statements), file->EndOfFileToken);
	}

	tx->currentSourceFile = nullptr;
	tx->importSpecifier = "";
	tx->filenameDeclaration = nullptr;
	tx->utilizedImplicitRuntimeImports = {};

	return visited;
}

// jsx.go:280 — visitJsxElement
Node* visitJsxElement(JSXTransformer* tx, JsxElement* element) {
	Node* (*tagTransform)(JSXTransformer*, Node*, NodeList*, TextRange) =
		visitJsxOpeningLikeElementJSX;
	if (shouldUseCreateElement(tx, element->asNode())) {
		tagTransform = visitJsxOpeningLikeElementCreateElement;
	}
	TextRange location{static_cast<TextPos>(
		                   skipTrivia(tx->currentSourceFile->text, element->pos())),
	                   element->end()};
	return tagTransform(tx, element->OpeningElement, element->Children, location);
}

// jsx.go:289 — visitJsxSelfClosingElement
Node* visitJsxSelfClosingElement(JSXTransformer* tx, JsxSelfClosingElement* element) {
	Node* (*tagTransform)(JSXTransformer*, Node*, NodeList*, TextRange) =
		visitJsxOpeningLikeElementJSX;
	if (shouldUseCreateElement(tx, element->asNode())) {
		tagTransform = visitJsxOpeningLikeElementCreateElement;
	}
	TextRange location{static_cast<TextPos>(
		                   skipTrivia(tx->currentSourceFile->text, element->pos())),
	                   element->end()};
	return tagTransform(tx, element->asNode(), nullptr, location);
}

// jsx.go:298 — visitJsxFragment
Node* visitJsxFragment(JSXTransformer* tx, JsxFragment* fragment) {
	Node* (*tagTransform)(JSXTransformer*, JsxOpeningFragment*, NodeList*,
	                      TextRange) = visitJsxOpeningFragmentJSX;
	if (tx->importSpecifier.size() == 0) {
		tagTransform = visitJsxOpeningFragmentCreateElement;
	}
	TextRange location{static_cast<TextPos>(
		                   skipTrivia(tx->currentSourceFile->text, fragment->pos())),
	                   fragment->end()};
	return tagTransform(tx, fragment->OpeningFragment->as<JsxOpeningFragment>(),
	                    fragment->Children, location);
}

// jsx.go:307 — convertJsxChildrenToChildrenPropObject
Node* convertJsxChildrenToChildrenPropObject(JSXTransformer* tx,
                                             const std::vector<Node*>& children) {
	Node* prop = convertJsxChildrenToChildrenPropAssignment(tx, children);
	if (prop == nullptr) {
		return nullptr;
	}
	return tx->factory()->newObjectLiteralExpression(
		tx->factory()->newNodeList(std::vector<Node*>{prop}), false);
}

// jsx.go:315 — transformJsxChildToExpression
Node* transformJsxChildToExpression(JSXTransformer* tx, Node* node) {
	bool prev = tx->inJsxChild;
	setInChild(tx, true);
	// defer tx.setInChild(prev)
	Node* result = tx->visitor()->visit(node);
	setInChild(tx, prev);
	return result;
}

// jsx.go:322 — convertJsxChildrenToChildrenPropAssignment
Node* convertJsxChildrenToChildrenPropAssignment(JSXTransformer* tx,
                                                 const std::vector<Node*>& children) {
	std::vector<Node*> nonWhitespceChildren = getSemanticJsxChildren(children);
	if (nonWhitespceChildren.size() == 1 &&
	    (nonWhitespceChildren[0]->kind != Kind::JsxExpression ||
	     nonWhitespceChildren[0]->as<JsxExpression>()->DotDotDotToken == nullptr)) {
		Node* result = transformJsxChildToExpression(tx, nonWhitespceChildren[0]);
		if (result == nullptr) {
			return nullptr;
		}
		return tx->factory()->newPropertyAssignment(
			nullptr, tx->factory()->newIdentifier("children"), nullptr, nullptr,
			result);
	}
	// For multiple children in the children property array, don't set StartOnNewLine
	// on child elements — the array literal is single-line.
	std::vector<Node*> results;
	results.reserve(nonWhitespceChildren.size());
	for (Node* child : nonWhitespceChildren) {
		Node* res = transformJsxChildToExpression(tx, child);
		if (res == nullptr) {
			continue;
		}
		tx->emitContext()->setEmitFlags(
			res, tx->emitContext()->emitFlags(res) & ~printer::EFStartOnNewLine);
		results.push_back(res);
	}
	if (results.empty()) {
		return nullptr;
	}
	return tx->factory()->newPropertyAssignment(
		nullptr, tx->factory()->newIdentifier("children"), nullptr, nullptr,
		tx->factory()->newArrayLiteralExpression(
			tx->factory()->newNodeList(results), false));
}

// jsx.go:348 — getTagName
Node* getTagName(JSXTransformer* tx, Node* node) {
	if (node->kind == Kind::JsxElement) {
		return getTagName(tx, node->as<JsxElement>()->OpeningElement);
	} else if (isJsxOpeningLikeElement(node)) {
		Node* tagName = node->tagName();
		if (isIdentifier(tagName) && isIntrinsicJsxName(tagName->text())) {
			return tx->factory()->newStringLiteral(tagName->text(), TokenFlagsNone);
		} else if (isJsxNamespacedName(tagName)) {
			return tx->factory()->newStringLiteral(
				tagName->as<JsxNamespacedName>()->Namespace->text() + ":" +
					tagName->as<JsxNamespacedName>()->name->text(),
				TokenFlagsNone);
		} else {
			return tx->factory()->createExpressionFromEntityName(tagName);
		}
	}
	TSC_UNREACHABLE(
		(std::string("unhandled node kind passed to getTagName: ") +
		 std::string(kindToString(node->kind)))
			.c_str());
}

// jsx.go:367 — visitJsxOpeningLikeElementJSX
Node* visitJsxOpeningLikeElementJSX(JSXTransformer* tx, Node* element,
                                    NodeList* children, TextRange location) {
	Node* tagName = getTagName(tx, element);
	Node* childrenProp = nullptr;
	if (children != nullptr && children->nodes.size() > 0) {
		childrenProp =
			convertJsxChildrenToChildrenPropAssignment(tx, children->nodes);
	}
	Node* keyAttr = nullptr;
	std::vector<Node*> attrs = element->attributes()->properties();
	for (size_t i = 0; i < attrs.size(); i++) {
		Node* p = attrs[i];
		if (p->kind == Kind::JsxAttribute &&
		    p->as<JsxAttribute>()->name != nullptr &&
		    isIdentifier(p->as<JsxAttribute>()->name) &&
		    p->as<JsxAttribute>()->name->text() == "key") {
			keyAttr = p;
			attrs.erase(attrs.begin() + i, attrs.begin() + i + 1);
			break;
		}
	}
	Node* object;
	if (attrs.size() > 0) {
		object = transformJsxAttributesToObjectProps(tx, attrs, childrenProp);
	} else {
		std::vector<Node*> objectChildren;
		if (childrenProp != nullptr) {
			objectChildren.push_back(childrenProp);
		}
		object = tx->factory()->newObjectLiteralExpression(
			tx->factory()->newNodeList(objectChildren),
			false); // When there are no attributes, React wants {}
	}
	return visitJsxOpeningLikeElementOrFragmentJSX(tx, tagName, object, keyAttr,
	                                             children, location);
}

// jsx.go:402 — transformJsxAttributesToObjectProps
Node* transformJsxAttributesToObjectProps(JSXTransformer* tx,
                                          const std::vector<Node*>& attrs,
                                          Node* childrenProp) {
	ScriptTarget target = tx->compilerOptions->GetEmitScriptTarget();
	if (target >= ScriptTarget::ES2018) {
		// target has object spreads, can keep as-is
		return tx->factory()->newObjectLiteralExpression(
			tx->factory()->newNodeList(
				transformJsxAttributesToProps(tx, attrs, childrenProp)),
			false);
	}
	return transformJsxAttributesToExpression(tx, attrs, childrenProp);
}

// jsx.go:456 — combinePropertiesIntoNewExpression
std::pair<std::vector<Node*>, std::vector<Node*>>
combinePropertiesIntoNewExpression(JSXTransformer* tx,
                                   std::vector<Node*> expressions,
                                   std::vector<Node*> props) {
	if (props.empty()) {
		return {std::move(expressions), std::move(props)};
	}
	Node* newObj = tx->factory()->newObjectLiteralExpression(
		tx->factory()->newNodeList(props), false);
	expressions.push_back(newObj);
	return {std::move(expressions), {}};
}

// jsx.go:411 — transformJsxAttributesToExpression
Node* transformJsxAttributesToExpression(JSXTransformer* tx,
                                         const std::vector<Node*>& attrs,
                                         Node* childrenProp) {
	std::vector<Node*> expressions;
	expressions.reserve(2);
	std::vector<Node*> properties;
	properties.reserve(attrs.size());

	for (Node* attr : attrs) {
		if (isJsxSpreadAttribute(attr)) {
			// as an optimization we try to flatten the first level of spread inline object
			// as if its props would be passed as JSX attributes
			if (isObjectLiteralExpression(attr->expression()) &&
			    !hasProto(attr->expression()->as<ObjectLiteralExpression>())) {
				for (Node* prop : attr->expression()->properties()) {
					if (isSpreadAssignment(prop)) {
						auto combined = combinePropertiesIntoNewExpression(
							tx, std::move(expressions), std::move(properties));
						expressions = std::move(combined.first);
						properties = std::move(combined.second);
						expressions.push_back(
							tx->visitor()->visit(prop->expression()));
						continue;
					}
					properties.push_back(tx->visitor()->visit(prop));
				}
				continue;
			}
			auto combined = combinePropertiesIntoNewExpression(
				tx, std::move(expressions), std::move(properties));
			expressions = std::move(combined.first);
			properties = std::move(combined.second);
			expressions.push_back(tx->visitor()->visit(attr->expression()));
			continue;
		}
		properties.push_back(
			transformJsxAttributeToObjectLiteralElement(tx, attr->as<JsxAttribute>()));
	}

	if (childrenProp != nullptr) {
		properties.push_back(childrenProp);
	}

	auto combined = combinePropertiesIntoNewExpression(
		tx, std::move(expressions), std::move(properties));
	expressions = std::move(combined.first);

	if (expressions.size() > 0 && !isObjectLiteralExpression(expressions[0])) {
		// We must always emit at least one object literal before a spread attribute
		// as the JSX always factory expects a fresh object, so we need to make a copy here
		// we also avoid mutating an external reference by doing this (first expression is used as assign's target)
		expressions.insert(
			expressions.begin(),
			tx->factory()->newObjectLiteralExpression(
				tx->factory()->newNodeList(std::vector<Node*>{}), false));
	}

	if (expressions.size() == 1) {
		return expressions[0];
	}
	return tx->factory()->newAssignHelper(expressions,
	                                    tx->compilerOptions->GetEmitScriptTarget());
}

// jsx.go:465 — transformJsxAttributesToProps
std::vector<Node*> transformJsxAttributesToProps(JSXTransformer* tx,
                                               const std::vector<Node*>& attrs,
                                               Node* childrenProp) {
	std::vector<Node*> props;
	props.reserve(attrs.size());
	for (Node* attr : attrs) {
		if (attr->kind == Kind::JsxSpreadAttribute) {
			std::vector<Node*> res = transformJsxSpreadAttributesToProps(
				tx, attr->as<JsxSpreadAttribute>());
			props.insert(props.end(), res.begin(), res.end());
		} else {
			props.push_back(transformJsxAttributeToObjectLiteralElement(
				tx, attr->as<JsxAttribute>()));
		}
	}
	if (childrenProp != nullptr) {
		props.push_back(childrenProp);
	}
	return props;
}

// jsx.go:481 — hasProto
bool hasProto(ObjectLiteralExpression* obj) {
	for (Node* p : obj->properties()) {
		if (isPropertyAssignment(p) &&
		    (isStringLiteral(p->name()) || isIdentifier(p->name())) &&
		    p->name()->text() == "__proto__") {
			return true;
		}
	}
	return false;
}

// jsx.go:490 — transformJsxSpreadAttributesToProps
std::vector<Node*> transformJsxSpreadAttributesToProps(JSXTransformer* tx,
                                                       JsxSpreadAttribute* node) {
	if (isObjectLiteralExpression(node->Expression) &&
	    !hasProto(node->Expression->as<ObjectLiteralExpression>())) {
		auto res = tx->visitor()->visitSlice(node->Expression->properties());
		return res.first;
	}
	return {tx->factory()->newSpreadAssignment(
		tx->visitor()->visit(node->Expression))};
}

// jsx.go:498 — transformJsxAttributeToObjectLiteralElement
Node* transformJsxAttributeToObjectLiteralElement(JSXTransformer* tx,
                                                  JsxAttribute* node) {
	Node* name = getAttributeName(tx, node);
	Node* expression = transformJsxAttributeInitializer(tx, node->Initializer);
	return tx->factory()->newPropertyAssignment(nullptr, name, nullptr, nullptr,
	                                            expression);
}

// jsx.go:509 — getAttributeName
/**
* Emit an attribute name, which is quoted if it needs to be quoted. Because
* these emit into an object literal property name, we don't need to be worried
* about keywords, just non-identifier characters
 */
Node* getAttributeName(JSXTransformer* tx, JsxAttribute* node) {
	Node* name = node->name;
	if (isIdentifier(name)) {
		std::string text = name->text();
		if (isIdentifierText(text, LanguageVariant::Standard)) {
			return name;
		}
		return tx->factory()->newStringLiteral(text, TokenFlagsNone);
	}
	// must be jsx namespace
	return tx->factory()->newStringLiteral(
		name->as<JsxNamespacedName>()->Namespace->text() + ":" +
			name->as<JsxNamespacedName>()->name->text(),
		TokenFlagsNone);
}

// jsx.go:524 — transformJsxAttributeInitializer
Node* transformJsxAttributeInitializer(JSXTransformer* tx, Node* node) {
	if (node == nullptr) {
		return tx->factory()->newTrueExpression();
	}
	if (node->kind == Kind::StringLiteral) {
		// Always recreate the literal to escape any escape sequences or newlines which may be in the original jsx string and which
		// Need to be escaped to be handled correctly in a normal string
		Node* res = tx->factory()->newStringLiteral(
			decodeEntities(node->text()), node->as<StringLiteral>()->TokenFlags);
		res->loc = node->loc;
		// Preserve the original quote style (single vs double quotes)
		res->as<StringLiteral>()->TokenFlags = node->as<StringLiteral>()->TokenFlags;
		return res;
	}
	if (node->kind == Kind::JsxExpression) {
		if (node->expression() == nullptr) {
			return tx->factory()->newTrueExpression();
		}
		return tx->visitor()->visit(node->expression());
	}
	if (isJsxElement(node) || isJsxSelfClosingElement(node) ||
	    isJsxFragment(node)) {
		setInChild(tx, false);
		return tx->visitor()->visit(node);
	}
	TSC_UNREACHABLE(
		(std::string("Unhandled node kind found in jsx initializer: ") +
		 std::string(kindToString(node->kind)))
			.c_str());
}

// jsx.go:550 — visitJsxOpeningLikeElementOrFragmentJSX
Node* visitJsxOpeningLikeElementOrFragmentJSX(JSXTransformer* tx,
                                             Node* tagName,
                                             Node* object,
                                             Node* keyAttr,
                                             NodeList* children,
                                             TextRange location) {
	std::vector<Node*> nonWhitespaceChildren;
	if (children != nullptr) {
		nonWhitespaceChildren = getSemanticJsxChildren(children->nodes);
	}
	bool isStaticChildren =
		nonWhitespaceChildren.size() > 1 ||
		(nonWhitespaceChildren.size() == 1 &&
		 isJsxExpression(nonWhitespaceChildren[0]) &&
		 nonWhitespaceChildren[0]->as<JsxExpression>()->DotDotDotToken != nullptr);
	std::vector<Node*> args;
	args.reserve(3);
	args.push_back(tagName);
	args.push_back(object);
	// function jsx(type, config, maybeKey) {}
	// "maybeKey" is optional. It is acceptable to use "_jsx" without a third argument
	if (keyAttr != nullptr) {
		args.push_back(
			transformJsxAttributeInitializer(tx, keyAttr->initializer()));
	}

	if (tx->compilerOptions->Jsx == JsxEmit::ReactJSXDev) {
		Node* originalFile =
			tx->emitContext()->mostOriginal(tx->currentSourceFile->asNode());
		if (originalFile != nullptr && isSourceFile(originalFile)) {
			// "maybeKey" has to be replaced with "void 0" to not break the jsxDEV signature
			if (keyAttr == nullptr) {
				args.push_back(tx->factory()->newVoidZeroExpression());
			}
			// isStaticChildren development flag
			if (isStaticChildren) {
				args.push_back(tx->factory()->newTrueExpression());
			} else {
				args.push_back(tx->factory()->newFalseExpression());
			}
			// __source development flag
			auto lineCol = getECMALineAndUTF16CharacterOfPosition(
				originalFile->as<SourceFile>(), location.pos());
			Node* sourceObj = tx->factory()->newObjectLiteralExpression(
				tx->factory()->newNodeList(std::vector<Node*>{
					tx->factory()->newPropertyAssignment(
						nullptr, tx->factory()->newIdentifier("fileName"),
						nullptr, nullptr, getCurrentFileNameExpression(tx)),
					tx->factory()->newPropertyAssignment(
						nullptr, tx->factory()->newIdentifier("lineNumber"),
						nullptr, nullptr,
						tx->factory()->newNumericLiteral(
							std::to_string(static_cast<int64_t>(lineCol.first) + 1),
							TokenFlagsNone)),
					tx->factory()->newPropertyAssignment(
						nullptr, tx->factory()->newIdentifier("columnNumber"),
						nullptr, nullptr,
						tx->factory()->newNumericLiteral(
							std::to_string(
								static_cast<int64_t>(lineCol.second) + 1),
							TokenFlagsNone)),
				}),
				false);
			args.push_back(sourceObj);
			// __self development flag
			args.push_back(tx->factory()->newThisExpression());
		}
	}

	Node* element = tx->factory()->newCallExpression(
		getJsxFactoryCallee(tx, isStaticChildren), nullptr, nullptr,
		tx->factory()->newNodeList(args), NodeFlagsNone);
	element->loc = location;

	if (tx->inJsxChild) {
		tx->emitContext()->addEmitFlags(element, printer::EFStartOnNewLine);
	}

	return element;
}

// jsx.go:605 — visitJsxOpeningFragmentJSX
Node* visitJsxOpeningFragmentJSX(JSXTransformer* tx, JsxOpeningFragment* fragment,
                                 NodeList* children, TextRange location) {
	Node* childrenProps = nullptr;
	if (children != nullptr && children->nodes.size() > 0) {
		Node* result =
			convertJsxChildrenToChildrenPropObject(tx, children->nodes);
		if (result != nullptr) {
			childrenProps = result;
		}
	}
	if (childrenProps == nullptr) {
		childrenProps = tx->factory()->newObjectLiteralExpression(
			tx->factory()->newNodeList(std::vector<Node*>{}), false);
	}
	return visitJsxOpeningLikeElementOrFragmentJSX(
		tx,
		getImplicitJsxFragmentReference(tx),
		childrenProps,
		nullptr,
		children,
		location);
}

// jsx.go:625 — createReactNamespace
Node* createReactNamespace(JSXTransformer* tx, std::string reactNamespace,
                           Node* parent) {
	// To ensure the emit resolver can properly resolve the namespace, we need to
	// treat this identifier as if it were a source tree node by clearing the `Synthesized`
	// flag and setting a parent node. TODO: Is this still true? The emit resolver is supposed to be
	// hardened aginast this, so long as the node retains original node pointers back to a parsed node
	if (reactNamespace.size() == 0) {
		reactNamespace = "React";
	}
	Node* react = tx->factory()->newIdentifier(reactNamespace);
	react->flags &= ~NodeFlagsSynthesized;

	// Set the parent that is in parse tree
	// this makes sure that parent chain is intact for checker to traverse complete scope tree
	react->parent = tx->emitContext()->parseNode(parent); //nolint:customlint // Parent is intentionally wired to a parse-tree node for resolver traversal.

	// If the identifier refers to an exported member of a namespace, substitute with
	// a qualified namespace property access (e.g., `React` -> `M.React`).
	// See also: RuntimeSyntaxTransformer.visitExpressionIdentifier in runtimesyntax.go
	if (Node* container =
	        tx->emitResolver->GetReferencedExportContainer(
	            react, false /*prefixLocals*/);
	    container != nullptr && isModuleDeclaration(container)) {
		Node* containerName = tx->factory()->newGeneratedNameForNode(container);
		return tx->factory()->newPropertyAccessExpression(
			containerName, nullptr, react, NodeFlagsNone);
	}

	return react;
}

// jsx.go:651 — createJsxFactoryExpressionFromEntityName
Node* createJsxFactoryExpressionFromEntityName(JSXTransformer* tx, Node* e,
                                               Node* parent) {
	if (isQualifiedName(e)) {
		Node* left = createJsxFactoryExpressionFromEntityName(
			tx, e->as<QualifiedName>()->Left, parent);
		Node* right = tx->factory()->newIdentifier(
			e->as<QualifiedName>()->Right->text());
		return tx->factory()->newPropertyAccessExpression(left, nullptr, right,
		                                                NodeFlagsNone);
	}
	return createReactNamespace(tx, e->text(), parent);
}

// jsx.go:660 — createJsxPseudoFactoryExpression
Node* createJsxPseudoFactoryExpression(JSXTransformer* tx, Node* parent, Node* e,
                                       const std::string& target) {
	if (e != nullptr) {
		return createJsxFactoryExpressionFromEntityName(tx, e, parent);
	}
	return tx->factory()->newPropertyAccessExpression(
		createReactNamespace(tx, tx->compilerOptions->ReactNamespace, parent),
		nullptr,
		tx->factory()->newIdentifier(target),
		NodeFlagsNone);
}

// jsx.go:672 — createJsxFactoryExpression
Node* createJsxFactoryExpression(JSXTransformer* tx, Node* parent) {
	Node* e = tx->emitResolver->GetJsxFactoryEntity(
		tx->currentSourceFile->asNode());
	return createJsxPseudoFactoryExpression(tx, parent, e, "createElement");
}

// jsx.go:677 — createJsxFragmentFactoryExpression
Node* createJsxFragmentFactoryExpression(JSXTransformer* tx, Node* parent) {
	Node* e = tx->emitResolver->GetJsxFragmentFactoryEntity(
		tx->currentSourceFile->asNode());
	return createJsxPseudoFactoryExpression(tx, parent, e, "Fragment");
}

// jsx.go:682 — visitJsxOpeningLikeElementCreateElement
Node* visitJsxOpeningLikeElementCreateElement(JSXTransformer* tx, Node* element,
                                              NodeList* children,
                                              TextRange location) {
	Node* tagName = getTagName(tx, element);
	std::vector<Node*> attrs = element->attributes()->properties();
	Node* objectProperties;
	if (attrs.size() > 0) {
		objectProperties = transformJsxAttributesToObjectProps(tx, attrs, nullptr);
	} else {
		objectProperties = tx->factory()->newKeywordExpression(
			Kind::NullKeyword); // When there are no attributes, React wants "null"
	}

	Node* callee;
	if (tx->importSpecifier.size() == 0) {
		callee = createJsxFactoryExpression(tx, element);
	} else {
		callee = getImplicitImportForName(tx, "createElement");
	}

	std::vector<Node*> newChildren;
	if (children != nullptr && children->nodes.size() > 0) {
		for (Node* c : children->nodes) {
			Node* res = transformJsxChildToExpression(tx, c);
			if (res != nullptr) {
				newChildren.push_back(res);
			}
		}
	}

	// Add StartOnNewLine flag only if there are multiple actual children (after filtering)
	if (newChildren.size() > 1) {
		for (Node* child : newChildren) {
			tx->emitContext()->addEmitFlags(child, printer::EFStartOnNewLine);
		}
	}

	std::vector<Node*> args;
	args.reserve(newChildren.size() + 2);
	args.push_back(tagName);
	args.push_back(objectProperties);
	for (Node* c : newChildren) {
		args.push_back(c);
	}

	Node* result = tx->factory()->newCallExpression(
		callee,
		nullptr,
		nullptr,
		tx->factory()->newNodeList(args),
		NodeFlagsNone);
	result->loc = location;

	if (tx->inJsxChild) {
		tx->emitContext()->addEmitFlags(result, printer::EFStartOnNewLine);
	}
	return result;
}

// jsx.go:736 — visitJsxOpeningFragmentCreateElement
Node* visitJsxOpeningFragmentCreateElement(JSXTransformer* tx,
                                           JsxOpeningFragment* fragment,
                                           NodeList* children,
                                           TextRange location) {
	Node* tagName = createJsxFragmentFactoryExpression(tx, fragment->asNode());
	Node* callee = createJsxFactoryExpression(tx, fragment->asNode());

	std::vector<Node*> newChildren;
	if (children != nullptr && children->nodes.size() > 0) {
		for (Node* c : children->nodes) {
			Node* res = transformJsxChildToExpression(tx, c);
			if (res != nullptr) {
				newChildren.push_back(res);
			}
		}
	}

	// Add StartOnNewLine flag only if there are multiple actual children (after filtering)
	if (newChildren.size() > 1) {
		for (Node* child : newChildren) {
			tx->emitContext()->addEmitFlags(child, printer::EFStartOnNewLine);
		}
	}

	std::vector<Node*> args;
	args.reserve(newChildren.size() + 2);
	args.push_back(tagName);
	args.push_back(tx->factory()->newKeywordExpression(Kind::NullKeyword));
	for (Node* c : newChildren) {
		args.push_back(c);
	}

	Node* result = tx->factory()->newCallExpression(
		callee,
		nullptr,
		nullptr,
		tx->factory()->newNodeList(args),
		NodeFlagsNone);
	result->loc = location;

	if (tx->inJsxChild) {
		tx->emitContext()->addEmitFlags(result, printer::EFStartOnNewLine);
	}
	return result;
}

// jsx.go:777 — visitJsxText
Node* visitJsxText(JSXTransformer* tx, JsxText* text) {
	std::string fixed = fixupWhitespaceAndDecodeEntities(text->Text);
	if (fixed.size() == 0) {
		return nullptr;
	}
	return tx->factory()->newStringLiteral(fixed, TokenFlagsNone);
}

// jsx.go:785 — addLineOfJsxText
void addLineOfJsxText(std::string& b, std::string_view trimmedLine,
                      bool isInitial) {
	// We do not escape the string here as that is handled by the printer
	// when it emits the literal. We do, however, need to decode JSX entities.
	std::string decoded = decodeEntities(trimmedLine);
	if (!isInitial) {
		b += ' ';
	}
	b += decoded;
}

// jsx.go:810 — fixupWhitespaceAndDecodeEntities
/**
* JSX trims whitespace at the end and beginning of lines, except that the
* start/end of a tag is considered a start/end of a line only if that line is
* on the same line as the closing tag. See examples in
* tests/cases/conformance/jsx/tsxReactEmitWhitespace.tsx
* See also https://www.w3.org/TR/html4/struct/text.html#h-9.1 and https://www.w3.org/TR/CSS2/text.html#white-space-model
*
* An equivalent algorithm would be:
* - If there is only one line, return it.
* - If there is only whitespace (but multiple lines), return `undefined`.
* - Split the text into lines.
* - 'trimRight' the first line, 'trimLeft' the last line, 'trim' middle lines.
* - Decode entities on each line (individually).
* - Remove empty lines and join the rest with " ".
 */
std::string fixupWhitespaceAndDecodeEntities(std::string_view text) {
	std::string acc;
	bool initial = true;
	// First non-whitespace character on this line.
	int firstNonWhitespace = 0;
	// End byte position of the last non-whitespace character on this line.
	int lastNonWhitespaceEnd = -1;
	// These initial values are special because the first line is:
	// firstNonWhitespace = 0 to indicate that we want leading whitespace,
	// but lastNonWhitespaceEnd = -1 as a special flag to indicate that we *don't* include the line if it's all whitespace.
	for (size_t i = 0; i < text.size(); i++) {
		int size = 0;
		char32_t c = decodeUtf8Rune(text.substr(i), &size);
		if (isLineBreak(c)) {
			// If we've seen any non-whitespace characters on this line, add the 'trim' of the line.
			// (lastNonWhitespaceEnd === -1 is a special flag to detect whether the first line is all whitespace.)
			if (firstNonWhitespace != -1 && lastNonWhitespaceEnd != -1) {
				addLineOfJsxText(
					acc,
					text.substr(firstNonWhitespace,
					            lastNonWhitespaceEnd - firstNonWhitespace + 1),
					initial);
				initial = false;
			}

			// Reset firstNonWhitespace for the next line.
			// Don't bother to reset lastNonWhitespaceEnd because we ignore it if firstNonWhitespace = -1.
			firstNonWhitespace = -1;
		} else if (!isWhiteSpaceSingleLine(c)) {
			lastNonWhitespaceEnd =
				static_cast<int>(i) + size - 1; // Store the end byte position of the character
			if (firstNonWhitespace == -1) {
				firstNonWhitespace = static_cast<int>(i);
			}
		}

		if (size > 1) {
			i += static_cast<size_t>(size - 1);
		}
	}

	if (firstNonWhitespace != -1) {
		// Last line had a non-whitespace character. Emit the 'trimLeft', meaning keep trailing whitespace.
		addLineOfJsxText(acc, text.substr(firstNonWhitespace), initial);
	}
	return acc;
}

// jsx.go:852 — visitJsxExpression
Node* visitJsxExpression(JSXTransformer* tx, JsxExpression* expression) {
	Node* e = tx->visitor()->visit(expression->Expression);
	if (expression->DotDotDotToken != nullptr) {
		return tx->factory()->newSpreadElement(e);
	}
	return e;
}

// jsx.go:864 — decodeEntities
/**
* Replace entities like "&nbsp;", "&#123;", and "&#xDEADBEEF;" with the characters they encode.
* See https://en.wikipedia.org/wiki/List_of_XML_and_HTML_character_entity_references
 */
std::string decodeEntities(std::string_view text) {
	size_t i = text.find('&');
	if (i == std::string_view::npos) {
		return std::string(text);
	}

	std::string result;
	result.reserve(text.size());
	for (;;) {
		result.append(text.substr(0, i));
		text = text.substr(i);

		size_t semi = text.find(';');
		if (semi == std::string_view::npos) {
			break;
		}

		// Skip past any intervening '&' characters between the current '&'
		// and the ';'. Each such '&' is not part of a valid entity, so emit
		// it (and any text before the next '&') as literals.
		for (;;) {
			size_t nextAmp = text.substr(1, semi - 1).find('&');
			if (nextAmp == std::string_view::npos) {
				break;
			}
			result.append(text.substr(0, nextAmp + 1));
			text = text.substr(nextAmp + 1);
			semi -= nextAmp + 1;
		}

		std::string_view entity = text.substr(1, semi - 1);
		char32_t decoded = 0;
		if (decodeEntity(entity, decoded)) {
			// Use the JS-string encoder so lone surrogates (e.g. "&#xD800;")
			// are preserved rather than being lost to U+FFFD by WriteRune.
			char buf[4];
			int n = encodeJSStringRune(decoded, buf);
			result.append(buf, static_cast<size_t>(n));
		} else {
			result.append(text.substr(0, semi + 1));
		}
		text = text.substr(semi + 1);

		i = text.find('&');
		if (i == std::string_view::npos) {
			break;
		}
	}
	result.append(text);
	return result;
}

// entities — jsx.go:955. map[string]rune of HTML4 entity names.
const std::unordered_map<std::string, char32_t> entities = {
	{"quot", 0x0022},     {"amp", 0x0026},    {"apos", 0x0027},
	{"lt", 0x003C},       {"gt", 0x003E},     {"nbsp", 0x00A0},
	{"iexcl", 0x00A1},    {"cent", 0x00A2},   {"pound", 0x00A3},
	{"curren", 0x00A4},   {"yen", 0x00A5},    {"brvbar", 0x00A6},
	{"sect", 0x00A7},     {"uml", 0x00A8},    {"copy", 0x00A9},
	{"ordf", 0x00AA},     {"laquo", 0x00AB},  {"not", 0x00AC},
	{"shy", 0x00AD},      {"reg", 0x00AE},    {"macr", 0x00AF},
	{"deg", 0x00B0},      {"plusmn", 0x00B1}, {"sup2", 0x00B2},
	{"sup3", 0x00B3},     {"acute", 0x00B4},  {"micro", 0x00B5},
	{"para", 0x00B6},     {"middot", 0x00B7}, {"cedil", 0x00B8},
	{"sup1", 0x00B9},     {"ordm", 0x00BA},   {"raquo", 0x00BB},
	{"frac14", 0x00BC},   {"frac12", 0x00BD}, {"frac34", 0x00BE},
	{"iquest", 0x00BF},   {"Agrave", 0x00C0}, {"Aacute", 0x00C1},
	{"Acirc", 0x00C2},    {"Atilde", 0x00C3}, {"Auml", 0x00C4},
	{"Aring", 0x00C5},    {"AElig", 0x00C6},  {"Ccedil", 0x00C7},
	{"Egrave", 0x00C8},   {"Eacute", 0x00C9}, {"Ecirc", 0x00CA},
	{"Euml", 0x00CB},     {"Igrave", 0x00CC}, {"Iacute", 0x00CD},
	{"Icirc", 0x00CE},    {"Iuml", 0x00CF},   {"ETH", 0x00D0},
	{"Ntilde", 0x00D1},   {"Ograve", 0x00D2}, {"Oacute", 0x00D3},
	{"Ocirc", 0x00D4},    {"Otilde", 0x00D5}, {"Ouml", 0x00D6},
	{"times", 0x00D7},    {"Oslash", 0x00D8}, {"Ugrave", 0x00D9},
	{"Uacute", 0x00DA},   {"Ucirc", 0x00DB},  {"Uuml", 0x00DC},
	{"Yacute", 0x00DD},   {"THORN", 0x00DE},  {"szlig", 0x00DF},
	{"agrave", 0x00E0},   {"aacute", 0x00E1}, {"acirc", 0x00E2},
	{"atilde", 0x00E3},   {"auml", 0x00E4},   {"aring", 0x00E5},
	{"aelig", 0x00E6},    {"ccedil", 0x00E7}, {"egrave", 0x00E8},
	{"eacute", 0x00E9},   {"ecirc", 0x00EA},  {"euml", 0x00EB},
	{"igrave", 0x00EC},   {"iacute", 0x00ED}, {"icirc", 0x00EE},
	{"iuml", 0x00EF},     {"eth", 0x00F0},    {"ntilde", 0x00F1},
	{"ograve", 0x00F2},   {"oacute", 0x00F3}, {"ocirc", 0x00F4},
	{"otilde", 0x00F5},   {"ouml", 0x00F6},   {"divide", 0x00F7},
	{"oslash", 0x00F8},   {"ugrave", 0x00F9}, {"uacute", 0x00FA},
	{"ucirc", 0x00FB},    {"uuml", 0x00FC},   {"yacute", 0x00FD},
	{"thorn", 0x00FE},    {"yuml", 0x00FF},   {"OElig", 0x0152},
	{"oelig", 0x0153},    {"Scaron", 0x0160}, {"scaron", 0x0161},
	{"Yuml", 0x0178},     {"fnof", 0x0192},   {"circ", 0x02C6},
	{"tilde", 0x02DC},    {"Alpha", 0x0391},  {"Beta", 0x0392},
	{"Gamma", 0x0393},    {"Delta", 0x0394},  {"Epsilon", 0x0395},
	{"Zeta", 0x0396},     {"Eta", 0x0397},    {"Theta", 0x0398},
	{"Iota", 0x0399},     {"Kappa", 0x039A},  {"Lambda", 0x039B},
	{"Mu", 0x039C},       {"Nu", 0x039D},     {"Xi", 0x039E},
	{"Omicron", 0x039F},  {"Pi", 0x03A0},     {"Rho", 0x03A1},
	{"Sigma", 0x03A3},    {"Tau", 0x03A4},    {"Upsilon", 0x03A5},
	{"Phi", 0x03A6},      {"Chi", 0x03A7},    {"Psi", 0x03A8},
	{"Omega", 0x03A9},    {"alpha", 0x03B1},  {"beta", 0x03B2},
	{"gamma", 0x03B3},    {"delta", 0x03B4},  {"epsilon", 0x03B5},
	{"zeta", 0x03B6},     {"eta", 0x03B7},    {"theta", 0x03B8},
	{"iota", 0x03B9},     {"kappa", 0x03BA},  {"lambda", 0x03BB},
	{"mu", 0x03BC},       {"nu", 0x03BD},     {"xi", 0x03BE},
	{"omicron", 0x03BF},  {"pi", 0x03C0},     {"rho", 0x03C1},
	{"sigmaf", 0x03C2},   {"sigma", 0x03C3},  {"tau", 0x03C4},
	{"upsilon", 0x03C5},  {"phi", 0x03C6},    {"chi", 0x03C7},
	{"psi", 0x03C8},      {"omega", 0x03C9},  {"thetasym", 0x03D1},
	{"upsih", 0x03D2},    {"piv", 0x03D6},    {"ensp", 0x2002},
	{"emsp", 0x2003},     {"thinsp", 0x2009}, {"zwnj", 0x200C},
	{"zwj", 0x200D},      {"lrm", 0x200E},    {"rlm", 0x200F},
	{"ndash", 0x2013},    {"mdash", 0x2014},  {"lsquo", 0x2018},
	{"rsquo", 0x2019},    {"sbquo", 0x201A},  {"ldquo", 0x201C},
	{"rdquo", 0x201D},    {"bdquo", 0x201E},  {"dagger", 0x2020},
	{"Dagger", 0x2021},   {"bull", 0x2022},   {"hellip", 0x2026},
	{"permil", 0x2030},   {"prime", 0x2032},  {"Prime", 0x2033},
	{"lsaquo", 0x2039},   {"rsaquo", 0x203A}, {"oline", 0x203E},
	{"frasl", 0x2044},    {"euro", 0x20AC},   {"image", 0x2111},
	{"weierp", 0x2118},   {"real", 0x211C},   {"trade", 0x2122},
	{"alefsym", 0x2135},  {"larr", 0x2190},   {"uarr", 0x2191},
	{"rarr", 0x2192},     {"darr", 0x2193},   {"harr", 0x2194},
	{"crarr", 0x21B5},    {"lArr", 0x21D0},   {"uArr", 0x21D1},
	{"rArr", 0x21D2},     {"dArr", 0x21D3},   {"hArr", 0x21D4},
	{"forall", 0x2200},   {"part", 0x2202},   {"exist", 0x2203},
	{"empty", 0x2205},    {"nabla", 0x2207},  {"isin", 0x2208},
	{"notin", 0x2209},    {"ni", 0x220B},     {"prod", 0x220F},
	{"sum", 0x2211},      {"minus", 0x2212},  {"lowast", 0x2217},
	{"radic", 0x221A},    {"prop", 0x221D},   {"infin", 0x221E},
	{"ang", 0x2220},      {"and", 0x2227},    {"or", 0x2228},
	{"cap", 0x2229},      {"cup", 0x222A},    {"int", 0x222B},
	{"there4", 0x2234},   {"sim", 0x223C},    {"cong", 0x2245},
	{"asymp", 0x2248},    {"ne", 0x2260},     {"equiv", 0x2261},
	{"le", 0x2264},       {"ge", 0x2265},     {"sub", 0x2282},
	{"sup", 0x2283},      {"nsub", 0x2284},   {"sube", 0x2286},
	{"supe", 0x2287},     {"oplus", 0x2295},  {"otimes", 0x2297},
	{"perp", 0x22A5},     {"sdot", 0x22C5},   {"lceil", 0x2308},
	{"rceil", 0x2309},    {"lfloor", 0x230A}, {"rfloor", 0x230B},
	{"lang", 0x2329},     {"rang", 0x232A},   {"loz", 0x25CA},
	{"spades", 0x2660},   {"clubs", 0x2663},  {"hearts", 0x2665},
	{"diams", 0x2666},
};

// jsx.go:914 — decodeEntity
bool decodeEntity(std::string_view entity, char32_t& out) {
	if (entity.size() == 0) {
		return false;
	}

	if (entity[0] == '#') {
		entity = entity.substr(1);
		if (entity.size() == 0) {
			return false;
		}

		int base = 10;
		if (entity[0] == 'x') {
			base = 16;
			entity = entity.substr(1);
		}

		if (entity.size() == 0) {
			return false;
		}

		for (char ch : entity) {
			auto c = static_cast<char32_t>(static_cast<unsigned char>(ch));
			if (base == 16 && !isHexDigit(c)) {
				return false;
			}
			if (base == 10 && !isDigit(c)) {
				return false;
			}
		}

		// strconv.ParseInt(entity, base, 32): all chars are valid digits, so the
		// only failure left is range overflow past int32.
		int64_t parsed = 0;
		for (char ch : entity) {
			int64_t digit;
			if (ch >= '0' && ch <= '9') {
				digit = ch - '0';
			} else if (ch >= 'a' && ch <= 'f') {
				digit = ch - 'a' + 10;
			} else {
				digit = ch - 'A' + 10;
			}
			parsed = parsed * base + digit;
			if (parsed > std::numeric_limits<int32_t>::max()) {
				return false;
			}
		}
		out = static_cast<char32_t>(parsed);
		return true;
	}

	auto it = entities.find(std::string(entity));
	if (it == entities.end()) {
		return false;
	}
	out = it->second;
	return true;
}

}  // namespace

// jsx.go:32 — NewJSXTransformer. Factory entry; returned Transformer* is owned
// by the caller per the TransformerFactory contract.
Transformer* newJSXTransformer(TransformOptions* opts) {
	return createJSXTransformer(opts);
}

}  // namespace tsc::transformers::jsxtransforms
