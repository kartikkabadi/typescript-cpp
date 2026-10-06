// Port of tsc/internal/ls/jsdoc.go — symbol documentation + JSDoc tag
// collection. === slice: ls-coreA ===

#include "internal/ls/ls.h"

#include <algorithm>

#include "internal/scanner/scanner.h"

namespace tsc::ls {

namespace {

// core.Some
template <class T, class F>
bool someRange(const std::vector<T>& v, F f) {
	return std::any_of(v.begin(), v.end(), f);
}

// core.Find — first element satisfying pred, or nullptr.
template <class T, class F>
T findRange(const std::vector<T>& v, F f) {
	auto it = std::find_if(v.begin(), v.end(), f);
	return it != v.end() ? *it : T{};
}

// core.FirstOrNil
template <class T>
T firstOrNil(const std::vector<T>& v) {
	return v.empty() ? T{} : v.front();
}

// core.LastOrNil
template <class T>
T lastOrNil(const std::vector<T>& v) {
	return v.empty() ? T{} : v.back();
}

// slices.Contains
template <class T>
bool containsRange(const std::vector<T>& v, const T& value) {
	return std::find(v.begin(), v.end(), value) != v.end();
}

} // namespace

// --- jsdoc.go:28 GetSymbolDocumentationComment ---

std::string GetSymbolDocumentationComment(checker::Checker* c,
                                          Symbol* symbol) {
	if (symbol == nullptr) {
		return "";
	}
	std::vector<std::string> parts;
	collections::Set<Node*> seen;
	for (Node* decl : symbol->declarations) {
		if (decl == nullptr) {
			continue;
		}
		if (!seen.AddIfAbsent(decl)) {
			continue;
		}
		std::string doc = getDocumentationFromDeclaration(
		    noMappedLocation, c, symbol, decl, decl,
		    lsproto::MarkupKindPlainText, true /*commentOnly*/);
		if (!doc.empty() && !containsRange(parts, doc)) {
			parts.push_back(doc);
		}
	}
	std::string out;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i != 0) {
			out += "\n";
		}
		out += parts[i];
	}
	return out;
}

// --- jsdoc.go:51 GetSymbolJSDocTags ---

namespace {

// jsdoc.go:85 declarationJSDocTags — returns the JSDoc tags associated with a
// declaration, walking the JSDoc comment location chain like the checker's
// getAllJSDocTags.
std::vector<Node*> declarationJSDocTags(Node* node) {
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

// jsdoc.go:103 getJSDocTagText — renders the text of a single JSDoc tag as a
// plain string, mirroring Strada's getCommentDisplayParts collapsed from
// SymbolDisplayPart[] to a string.
std::string getJSDocTagText(Node* tag) {
	std::string comment = getTextOfJSDocComment(tag->commentList());
	auto addComment = [&](const std::string& s) -> std::string {
		if (comment.empty()) {
			return s;
		}
		return s + " " + comment;
	};
	switch (tag->kind) {
	case Kind::JSDocThrowsTag:
		if (Node* te = tag->as<JSDocThrowsTag>()->TypeExpression;
		    te != nullptr) {
			return addComment(getTextOfNode(te));
		}
		return comment;
	case Kind::JSDocImplementsTag:
		return addComment(getTextOfNode(
		    tag->as<JSDocImplementsTag>()->ClassName));
	case Kind::JSDocAugmentsTag:
		return addComment(getTextOfNode(
		    tag->as<JSDocAugmentsTag>()->ClassName));
	case Kind::JSDocTemplateTag: {
		JSDocTemplateTag* templateTag = tag->as<JSDocTemplateTag>();
		std::string b;
		if (templateTag->Constraint != nullptr) {
			b += getTextOfNode(templateTag->Constraint);
		}
		if (templateTag->TypeParameters != nullptr) {
			int i = 0;
			for (Node* tp : templateTag->TypeParameters->nodes) {
				if (i == 0 && !b.empty()) {
					b += " ";
				}
				if (i != 0) {
					b += ", ";
				}
				b += getTextOfNode(tp);
				i++;
			}
		}
		if (!comment.empty()) {
			if (!b.empty()) {
				b += " ";
			}
			b += comment;
		}
		return b;
	}
	case Kind::JSDocTypeTag:
		return addComment(getTextOfNode(
		    tag->as<JSDocTypeTag>()->TypeExpression));
	case Kind::JSDocSatisfiesTag:
		return addComment(getTextOfNode(
		    tag->as<JSDocSatisfiesTag>()->TypeExpression));
	case Kind::JSDocSeeTag:
		if (Node* ne = tag->as<JSDocSeeTag>()->NameExpression;
		    ne != nullptr) {
			return addComment(getTextOfNode(ne));
		}
		return comment;
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
		if (Node* name = tag->name(); name != nullptr) {
			return addComment(getTextOfNode(name));
		}
		return comment;
	default:
		return comment;
	}
}

} // namespace

std::vector<JSDocTagInfo> GetSymbolJSDocTags(Symbol* symbol) {
	if (symbol == nullptr) {
		return {};
	}
	std::vector<JSDocTagInfo> infos;
	collections::Set<Node*> seen;
	for (Node* decl : symbol->declarations) {
		if (decl == nullptr) {
			continue;
		}
		if (!seen.AddIfAbsent(decl)) {
			continue;
		}
		std::vector<Node*> tags = declarationJSDocTags(decl);
		// Skip comments containing @typedef/@callback since they're not
		// associated with a particular declaration, unless they also carry
		// @param/@return (treated as local docs).
		bool hasTypedef = someRange(tags, [](Node* t) {
			return t->kind == Kind::JSDocTypedefTag ||
			       t->kind == Kind::JSDocCallbackTag;
		});
		bool hasParamOrReturn = someRange(tags, [](Node* t) {
			return t->kind == Kind::JSDocParameterTag ||
			       t->kind == Kind::JSDocReturnTag;
		});
		if (hasTypedef && !hasParamOrReturn) {
			continue;
		}
		for (Node* tag : tags) {
			infos.push_back(JSDocTagInfo{.Name = tag->tagName()->text(),
			                             .Text = getJSDocTagText(tag)});
		}
	}
	return infos;
}

// --- jsdoc.go:164 getJSDoc ---

namespace {

Node* getJSDoc(Node* node) { return lastOrNil(node->jsDoc()); }

// jsdoc.go:297 isMatchingParameterTag.
bool isMatchingParameterTag(Node* tag, const std::string& name);

// jsdoc.go:301 isMatchingTemplateTag.
bool isMatchingTemplateTag(Node* tag, const std::string& name);

// jsdoc.go:305 isNodeWithName.
bool isNodeWithName(Node* node, const std::string& name);

// jsdoc.go:239 getMatchingJSDocTag — forward decl (mutual recursion with
// getJSDocOrTag).
Node* getMatchingJSDocTag(
    checker::Checker* c, Node* node, const std::string& name,
    const std::function<bool(Node*, const std::string&)>& match,
    collections::Set<Symbol*>* seenSymbols);

// jsdoc.go:254 getJSDocParameterTagByPosition — forward decl.
Node* getJSDocParameterTagByPosition(checker::Checker* c, Node* param);

} // namespace

// --- jsdoc.go:168 getJSDocOrTag ---

Node* getJSDocOrTag(checker::Checker* c, Node* node,
                    collections::Set<Symbol*>* seenSymbols) {
	if (node == nullptr) {
		return nullptr;
	}
	if (Node* jsdoc = getJSDoc(node); jsdoc != nullptr) {
		return jsdoc;
	}
	if (isParameterDeclaration(node)) {
		Node* name = node->name();
		if (isBindingPattern(name)) {
			// For binding patterns, match JSDoc @param tags by position
			// rather than by name
			return getJSDocParameterTagByPosition(c, node);
		}
		return getMatchingJSDocTag(c, node->parent, name->text(),
		                           isMatchingParameterTag, seenSymbols);
	}
	if (isTypeParameterDeclaration(node)) {
		return getMatchingJSDocTag(c, node->parent, node->name()->text(),
		                           isMatchingTemplateTag, seenSymbols);
	}
	if (isVariableDeclaration(node) &&
	    isVariableDeclarationList(node->parent) &&
	    firstOrNil(node->parent->as<VariableDeclarationList>()
	                   ->Declarations->nodes) == node) {
		return getJSDocOrTag(c, node->parent->parent, seenSymbols);
	}
	if ((isFunctionExpressionOrArrowFunction(node) ||
	     isClassExpression(node)) &&
	    (isVariableDeclaration(node->parent) ||
	     isPropertyDeclaration(node->parent) ||
	     isPropertyAssignment(node->parent)) &&
	    node->parent->initializer() == node) {
		return getJSDocOrTag(c, node->parent, seenSymbols);
	}
	if (isBindingElement(node) && isObjectBindingPattern(node->parent)) {
		if (Node* name = node->propertyNameOrName(); isIdentifier(name)) {
			if (checker::Type* objectType = c->GetTypeAtLocation(node->parent);
			    objectType != nullptr) {
				if (Symbol* prop = c->GetPropertyOfType(objectType,
				                                        name->text());
				    prop != nullptr) {
					for (Node* d : prop->declarations) {
						if (Node* jsdoc = getJSDoc(d); jsdoc != nullptr) {
							return jsdoc;
						}
					}
				}
			}
		}
	}
	if (Symbol* symbol = node->symbol();
	    symbol != nullptr && node->parent != nullptr) {
		if (isFunctionDeclaration(node) || isMethodDeclaration(node) ||
		    isMethodSignatureDeclaration(node) ||
		    isConstructorDeclaration(node) ||
		    isConstructSignatureDeclaration(node)) {
			Node* firstSignature =
			    findRange(symbol->declarations,
			              [](Node* d) { return isFunctionLike(d); });
			if (firstSignature != nullptr && node != firstSignature) {
				if (Node* jsDoc =
				        getJSDocOrTag(c, firstSignature, seenSymbols);
				    jsDoc != nullptr) {
					return jsDoc;
				}
			}
		}
		if (isClassOrInterfaceLike(node->parent)) {
			bool isStatic = hasStaticModifier(node);
			checker::Type* classType =
			    c->GetDeclaredTypeOfSymbol(node->parent->symbol());
			if (isStatic) {
				// For static members, use the checker's base constructor
				// type resolution. This correctly handles intersection
				// constructor types from mixins (e.g., typeof MixinClass &
				// T) by preserving the full intersection.
				checker::Type* staticBaseType = c->GetApparentType(
				    c->GetBaseConstructorTypeOfClass(classType));
				if (Symbol* prop = c->GetPropertyOfType(staticBaseType,
				                                        symbol->name);
				    prop != nullptr && prop->valueDeclaration != nullptr &&
				    seenSymbols->AddIfAbsent(prop)) {
					if (Node* jsDoc = getJSDocOrTag(
					        c, prop->valueDeclaration, seenSymbols);
					    jsDoc != nullptr) {
						return jsDoc;
					}
				}
			} else {
				for (checker::Type* baseType : c->GetBaseTypes(classType)) {
					if (Symbol* prop = c->GetPropertyOfType(baseType,
					                                        symbol->name);
					    prop != nullptr &&
					    prop->valueDeclaration != nullptr &&
					    seenSymbols->AddIfAbsent(prop)) {
						if (Node* jsDoc = getJSDocOrTag(
						        c, prop->valueDeclaration, seenSymbols);
						    jsDoc != nullptr) {
							return jsDoc;
						}
					}
				}
			}
		}
	}
	return nullptr;
}

// --- jsdoc.go:239 getMatchingJSDocTag ---

namespace {

Node* getMatchingJSDocTag(
    checker::Checker* c, Node* node, const std::string& name,
    const std::function<bool(Node*, const std::string&)>& match,
    collections::Set<Symbol*>* seenSymbols) {
	if (Node* jsdoc = getJSDocOrTag(c, node, seenSymbols);
	    jsdoc != nullptr && jsdoc->kind == Kind::JSDoc) {
		if (NodeList* tags = jsdoc->as<JSDoc>()->Tags; tags != nullptr) {
			for (Node* tag : tags->nodes) {
				if (match(tag, name)) {
					return tag;
				}
			}
		}
	}
	return nullptr;
}

// --- jsdoc.go:254 getJSDocParameterTagByPosition ---

// getJSDocParameterTagByPosition finds a JSDoc @param tag for a binding
// pattern parameter by position. Since binding patterns don't have a simple
// name, we match the @param tag at the same index as the parameter.
Node* getJSDocParameterTagByPosition(checker::Checker* c, Node* param) {
	Node* parent = param->parent;
	if (parent == nullptr) {
		return nullptr;
	}

	// Find the parameter's index in the parent's parameters list
	std::vector<Node*> params = parent->parameters();
	int paramIndex = -1;
	for (size_t i = 0; i < params.size(); i++) {
		if (params[i] == param) {
			paramIndex = static_cast<int>(i);
			break;
		}
	}
	if (paramIndex < 0) {
		return nullptr;
	}

	// Get the JSDoc for the parent function/method
	collections::Set<Symbol*> seenSymbols;
	Node* jsdoc = getJSDocOrTag(c, parent, &seenSymbols);
	if (jsdoc == nullptr || jsdoc->kind != Kind::JSDoc) {
		return nullptr;
	}

	// Collect all @param tags in order
	NodeList* tags = jsdoc->as<JSDoc>()->Tags;
	if (tags == nullptr) {
		return nullptr;
	}

	int paramTagIndex = 0;
	for (Node* tag : tags->nodes) {
		if (tag->kind == Kind::JSDocParameterTag) {
			if (paramTagIndex == paramIndex) {
				return tag;
			}
			paramTagIndex++;
		}
	}
	return nullptr;
}

// --- jsdoc.go:297 isMatchingParameterTag ---

bool isMatchingParameterTag(Node* tag, const std::string& name) {
	return tag->kind == Kind::JSDocParameterTag && isNodeWithName(tag, name);
}

// --- jsdoc.go:301 isMatchingTemplateTag ---

bool isMatchingTemplateTag(Node* tag, const std::string& name) {
	return tag->kind == Kind::JSDocTemplateTag &&
	       someRange(tag->typeParameters(), [&](Node* tp) {
		       return isNodeWithName(tp, name);
	       });
}

// --- jsdoc.go:305 isNodeWithName ---

bool isNodeWithName(Node* node, const std::string& name) {
	Node* nodeName = node->name();
	return isIdentifier(nodeName) && nodeName->text() == name;
}

} // namespace

// --- jsdoc.go:310 noMappedLocation ---

std::pair<lsproto::Location, spanmap::Fidelity>
noMappedLocation(SourceFile* file, TextRange range) {
	return {lsproto::Location{}, spanmap::FidelityNone};
}

// (deduped: getDocumentationFromDeclaration defined in cpp/internal/ls/hover.cpp)

} // namespace tsc::ls
