// Port of tsc/internal/transformers/destructuring.go — the flattener that
// decomposes binding/assignment patterns into individual bindings or
// assignments.
#include <algorithm>
#include <string>
#include <utility>

#include "internal/transformers/transformers.h"

namespace tsc::transformers {

// pendingDecl — destructuring.go:46. Tracks a pending variable declaration
// during binding flattening.
struct pendingDecl {
	std::vector<Node*> pendingExpressions;
	Node* name = nullptr;
	Node* value = nullptr;
	TextRange location{};
	Node* original = nullptr;
};

// restIdElemPair — destructuring.go:370
struct restIdElemPair {
	Node* id = nullptr;
	Node* element = nullptr;
};

// flattener — destructuring.go:77. Encapsulates the state and logic for
// flattening destructuring patterns. Equivalent to TypeScript's
// FlattenContext in destructuring.ts.
struct flattener {
	Transformer* tx = nullptr;
	FlattenLevel level = FlattenLevel::All;

	CreateAssignmentCallback createAssignmentCallback;

	// State
	std::vector<Node*> expressions;
	std::vector<pendingDecl> declarations;
	bool hasTransformedPriorElement = false;
	bool hoistTempVariables = false;

	// Mode callbacks (set by flattenDestructuringAssignment or
	// flattenDestructuringBinding)
	std::function<void(flattener*, Node*, Node*, TextRange, Node*)>
		emitBindingOrAssignment;
	std::function<Node*(flattener*, std::vector<Node*>)>
		createArrayBindingOrAssignmentPattern;
	std::function<Node*(flattener*, std::vector<Node*>)>
		createObjectBindingOrAssignmentPattern;
	std::function<Node*(flattener*, Node*)>
		createArrayBindingOrAssignmentElement;

	// --- Assignment mode callbacks ---

	// destructuring.go:105
	Node* createArrayAssignmentPattern(std::vector<Node*> elements) {
		return tx->factory()->newArrayLiteralExpression(
			tx->factory()->newNodeList(std::move(elements)),
			false /*multiLine*/);
	}

	// destructuring.go:109
	Node* createObjectAssignmentPattern(std::vector<Node*> elements) {
		return tx->factory()->newObjectLiteralExpression(
			tx->factory()->newNodeList(std::move(elements)),
			false /*multiLine*/);
	}

	// destructuring.go:113
	Node* createArrayAssignmentElement(Node* expr) { return expr; }

	// destructuring.go:117
	void emitAssignment(Node* target, Node* value, TextRange location,
	                    Node* original) {
		Node* expression = nullptr;
		if (createAssignmentCallback && isIdentifier(target)) {
			expression = createAssignmentCallback(target, value, &location);
		} else {
			expression = tx->factory()->newAssignmentExpression(
				tx->visitor()->visitNode(target), value);
			expression->loc = location;
		}
		tx->emitContext()->setOriginal(expression, original);
		emitExpression(expression);
	}

	// --- Binding mode callbacks ---

	// destructuring.go:131
	Node* createArrayBindingPattern(std::vector<Node*> elements) {
		return tx->factory()->newBindingPattern(
			Kind::ArrayBindingPattern,
			tx->factory()->newNodeList(std::move(elements)));
	}

	// destructuring.go:135
	Node* createObjectBindingPattern(std::vector<Node*> elements) {
		return tx->factory()->newBindingPattern(
			Kind::ObjectBindingPattern,
			tx->factory()->newNodeList(std::move(elements)));
	}

	// destructuring.go:139
	Node* createArrayBindingElement(Node* expr) {
		return tx->factory()->newBindingElement(nullptr, nullptr, expr,
		                                        nullptr);
	}

	// destructuring.go:143
	void emitBinding(Node* target, Node* value, TextRange location,
	                 Node* original) {
		if (!expressions.empty()) {
			std::vector<Node*> exprs = expressions;
			exprs.push_back(value);
			value = tx->factory()->inlineExpressions(std::move(exprs));
			expressions.clear();
		}
		declarations.push_back(pendingDecl{/*.pendingExpressions*/ {},
		                                   /*.name*/ target,
		                                   /*.value*/ value,
		                                   /*.location*/ location,
		                                   /*.original*/ original});
	}

	// --- Shared helpers ---

	// destructuring.go:158
	void emitExpression(Node* expr) { expressions.push_back(expr); }

	// destructuring.go:162
	Node* ensureIdentifier(Node* value, bool reuseIdentifierExpressions,
	                       TextRange location) {
		if (reuseIdentifierExpressions && isIdentifier(value)) {
			return value;
		}
		Node* temp = tx->factory()->newTempVariable();
		if (hoistTempVariables) {
			tx->emitContext()->addVariableDeclaration(temp);
			Node* assign = tx->factory()->newAssignmentExpression(temp, value);
			assign->loc = location;
			emitExpression(assign);
		} else {
			emitBindingOrAssignment(this, temp, value, location, nullptr);
		}
		return temp;
	}

	// destructuring.go:178
	Node* createDefaultValueCheck(Node* value, Node* defaultValue,
	                              TextRange location) {
		value = ensureIdentifier(value, true, location);
		return tx->factory()->newConditionalExpression(
			tx->factory()->newTypeCheck(value, "undefined"),
			tx->factory()->newToken(Kind::QuestionToken), defaultValue,
			tx->factory()->newToken(Kind::ColonToken), value);
	}

	// destructuring.go:189
	Node* createDestructuringPropertyAccess(Node* value,
	                                        Node* propertyName) {
		if (isComputedPropertyName(propertyName)) {
			Node* argumentExpression =
				ensureIdentifier(tx->visitor()->visitNode(
					                 propertyName->expression()),
				                 false, propertyName->loc);
			return tx->factory()->newElementAccessExpression(
				value, nullptr, argumentExpression, NodeFlagsNone);
		}
		if (isStringOrNumericLiteralLike(propertyName) ||
		    isBigIntLiteral(propertyName)) {
			Node* argumentExpression =
				propertyName->clone(*tx->factory());
			return tx->factory()->newElementAccessExpression(
				value, nullptr, argumentExpression, NodeFlagsNone);
		}
		Node* name = tx->factory()->newIdentifier(propertyName->text());
		return tx->factory()->newPropertyAccessExpression(
			value, nullptr, name, NodeFlagsNone);
	}

	// --- Entry points ---

	// destructuring.go:204
	Node* flattenDestructuringAssignment(Node* node, bool needsValue) {
		TextRange location = node->loc;
		Node* value = nullptr;
		if (isDestructuringAssignment(node)) {
			value = node->as<BinaryExpression>()->Right;
			while (isEmptyArrayLiteral(node->as<BinaryExpression>()->Left) ||
			       isEmptyObjectLiteral(node->as<BinaryExpression>()->Left)) {
				if (isDestructuringAssignment(value)) {
					node = value;
					location = node->loc;
					value = node->as<BinaryExpression>()->Right;
				} else {
					return tx->visitor()->visitNode(value);
				}
			}
		}

		if (value != nullptr) {
			value = tx->visitor()->visitNode(value);
			if ((isIdentifier(value) &&
			     bindingOrAssignmentElementAssignsToName(node,
			                                             value->text())) ||
			    bindingOrAssignmentElementContainsNonLiteralComputedName(
				    node)) {
				value = ensureIdentifier(value, false, location);
			} else if (needsValue) {
				value = ensureIdentifier(value, true, location);
			} else if (nodeIsSynthesized(node)) {
				location = value->loc;
			}
		}

		flattenBindingOrAssignmentElement(node, value, location,
		                                  isDestructuringAssignment(node));

		if (value != nullptr && needsValue) {
			if (expressions.empty()) {
				return value;
			}
			expressions.push_back(value);
		}

		Node* res = tx->factory()->inlineExpressions(expressions);
		if (res != nullptr) {
			return res;
		}
		return tx->factory()->newOmittedExpression();
	}

	// destructuring.go:247
	Node* flattenDestructuringBinding(Node* node, Node* rval,
	                                  bool skipInitializer) {
		if (isVariableDeclaration(node)) {
			Node* initializer =
				getInitializerOfBindingOrAssignmentElement(node);
			if (initializer != nullptr &&
			    ((isIdentifier(initializer) &&
			      bindingOrAssignmentElementAssignsToName(
				      node, initializer->text())) ||
			     bindingOrAssignmentElementContainsNonLiteralComputedName(
				     node))) {
				initializer = ensureIdentifier(
					tx->visitor()->visitNode(initializer), false,
					initializer->loc);
				node = tx->factory()->updateVariableDeclaration(
					node->as<VariableDeclaration>(), node->name(),
					nullptr, nullptr, initializer);
			}
		}

		flattenBindingOrAssignmentElement(node, rval, node->loc,
		                                  skipInitializer);

		if (!expressions.empty()) {
			Node* temp = tx->factory()->newTempVariable();
			if (hoistTempVariables) {
				Node* value =
					tx->factory()->inlineExpressions(expressions);
				expressions.clear();
				emitBindingOrAssignment(this, temp, value,
				                        TextRange{}, nullptr);
			} else {
				tx->emitContext()->addVariableDeclaration(temp);
				pendingDecl& last = declarations.back();
				last.pendingExpressions.push_back(
					tx->factory()->newAssignmentExpression(temp,
					                                       last.value));
				last.pendingExpressions.insert(
					last.pendingExpressions.end(), expressions.begin(),
					expressions.end());
				last.value = temp;
			}
		}

		std::vector<Node*> decls;
		decls.reserve(declarations.size());
		for (auto& pending : declarations) {
			Node* expr = pending.value;
			if (!pending.pendingExpressions.empty()) {
				std::vector<Node*> exprs = pending.pendingExpressions;
				exprs.push_back(pending.value);
				expr = tx->factory()->inlineExpressions(std::move(exprs));
			}
			Node* decl = tx->factory()->newVariableDeclaration(
				pending.name, nullptr, nullptr, expr);
			decl->loc = pending.location;
			if (pending.original != nullptr) {
				tx->emitContext()->setOriginal(decl, pending.original);
			}
			decls.push_back(decl);
		}

		if (decls.size() == 1) {
			return decls[0];
		}
		if (decls.empty()) {
			return nullptr;
		}
		return tx->factory()->newSyntaxList(decls);
	}

	// --- Core flattening ---

	// destructuring.go:298
	void flattenBindingOrAssignmentElement(Node* element, Node* value,
	                                       TextRange location,
	                                       bool skipInitializer) {
		Node* bindingTarget = getTargetOfBindingOrAssignmentElement(element);
		if (bindingTarget == nullptr) {
			return;
		}
		if (!skipInitializer) {
			Node* initializer = tx->visitor()->visitNode(
				getInitializerOfBindingOrAssignmentElement(element));
			if (initializer != nullptr) {
				if (value != nullptr) {
					value = createDefaultValueCheck(value, initializer,
					                              location);
					if (!isSimpleCopiableExpression(initializer) &&
					    (isBindingPattern(bindingTarget) ||
					     isAssignmentPattern(bindingTarget))) {
						value = ensureIdentifier(value, true, location);
					}
				} else {
					value = initializer;
				}
			} else if (value == nullptr) {
				value = tx->factory()->newVoidZeroExpression();
			}
		}

		if (isObjectBindingOrAssignmentPattern(bindingTarget)) {
			flattenObjectBindingOrAssignmentPattern(element, bindingTarget,
			                                        value, location);
		} else if (isArrayBindingOrAssignmentPattern(bindingTarget)) {
			flattenArrayBindingOrAssignmentPattern(element, bindingTarget,
			                                       value, location);
		} else {
			emitBindingOrAssignment(this, bindingTarget, value, location,
			                        element);
		}
	}

	// destructuring.go:328
	void flattenObjectBindingOrAssignmentPattern(Node* parent,
	                                             Node* pattern,
	                                             Node* value,
	                                             TextRange location) {
		std::vector<Node*> elements =
			getElementsOfBindingOrAssignmentPattern(pattern);
		size_t numElements = elements.size();
		if (numElements != 1) {
			bool reuseIdentifierExpressions =
				!isDeclarationBindingElement(parent) || numElements != 0;
			value = ensureIdentifier(value, reuseIdentifierExpressions,
			                         location);
		}
		std::vector<Node*> bindingElements;
		std::vector<Node*> computedTempVariables;
		for (size_t i = 0; i < elements.size(); i++) {
			Node* element = elements[i];
			if (getRestIndicatorOfBindingOrAssignmentElement(element) ==
			    nullptr) {
				Node* propertyName =
					tryGetPropertyNameOfBindingOrAssignmentElement(
						element);
				if (level >= FlattenLevel::ObjectRest &&
				    (element->subtreeFacts() &
				     (SubtreeContainsRestOrSpread |
				      SubtreeContainsObjectRestOrSpread)) == 0 &&
				    (getTargetOfBindingOrAssignmentElement(element)
					     ->subtreeFacts() &
				     (SubtreeContainsRestOrSpread |
				      SubtreeContainsObjectRestOrSpread)) == 0 &&
				    !isComputedPropertyName(propertyName)) {
					bindingElements.push_back(
						tx->visitor()->visitNode(element));
				} else {
					if (!bindingElements.empty()) {
						emitBindingOrAssignment(
							this,
							createObjectBindingOrAssignmentPattern(
								this, std::move(bindingElements)),
							value, location, pattern);
						bindingElements.clear();
					}
					Node* rhsValue = createDestructuringPropertyAccess(
						value, propertyName);
					if (isComputedPropertyName(propertyName)) {
						computedTempVariables.push_back(
							rhsValue->as<ElementAccessExpression>()
							    ->ArgumentExpression);
					}
					flattenBindingOrAssignmentElement(
						element, rhsValue, element->loc,
						false /*skipInitializer*/);
				}
			} else if (i == numElements - 1) {
				if (!bindingElements.empty()) {
					emitBindingOrAssignment(
						this,
						createObjectBindingOrAssignmentPattern(
							this, std::move(bindingElements)),
						value, location, pattern);
					bindingElements.clear();
				}
				Node* rhsValue = tx->factory()->newRestHelper(
					value, elements, computedTempVariables,
					pattern->loc);
				flattenBindingOrAssignmentElement(
					element, rhsValue, element->loc,
					false /*skipInitializer*/);
			}
		}
		if (!bindingElements.empty()) {
			emitBindingOrAssignment(
				this,
				createObjectBindingOrAssignmentPattern(
					this, std::move(bindingElements)),
				value, location, pattern);
		}
	}

	// destructuring.go:375
	void flattenArrayBindingOrAssignmentPattern(Node* parent,
	                                            Node* pattern,
	                                            Node* value,
	                                            TextRange location) {
		std::vector<Node*> elements =
			getElementsOfBindingOrAssignmentPattern(pattern);
		size_t numElements = elements.size();
		bool allOmitted = true;
		for (Node* element : elements) {
			if (!isOmittedExpression(element)) {
				allOmitted = false;
				break;
			}
		}
		if ((numElements != 1 &&
		     (level < FlattenLevel::ObjectRest || numElements == 0)) ||
		    allOmitted) {
			bool reuseIdentifierExpressions =
				!isDeclarationBindingElement(parent) || numElements != 0;
			value = ensureIdentifier(value, reuseIdentifierExpressions,
			                         location);
		}
		std::vector<Node*> bindingElements;
		std::vector<restIdElemPair> restContainingElements;
		for (size_t i = 0; i < elements.size(); i++) {
			Node* element = elements[i];
			if (level >= FlattenLevel::ObjectRest) {
				if ((element->subtreeFacts() &
				     SubtreeContainsObjectRestOrSpread) != 0 ||
				    (hasTransformedPriorElement &&
				     !isSimpleBindingOrAssignmentElement(element))) {
					hasTransformedPriorElement = true;
					Node* temp = tx->factory()->newTempVariable();
					if (hoistTempVariables) {
						tx->emitContext()->addVariableDeclaration(
							temp);
					}
					restContainingElements.push_back(
						restIdElemPair{temp, element});
					bindingElements.push_back(
						createArrayBindingOrAssignmentElement(this,
						                                      temp));
				} else {
					bindingElements.push_back(element);
				}
			} else if (isOmittedExpression(element)) {
				continue;
			} else if (getRestIndicatorOfBindingOrAssignmentElement(
				           element) == nullptr) {
				Node* rhsValue =
					tx->factory()->newElementAccessExpression(
						value, nullptr,
						tx->factory()->newNumericLiteral(
							std::to_string(i), TokenFlagsNone),
						NodeFlagsNone);
				flattenBindingOrAssignmentElement(element, rhsValue,
				                                  element->loc, false);
			} else if (i == numElements - 1) {
				Node* rhsValue =
					tx->factory()->newArraySliceCall(value,
					                               static_cast<int>(i));
				flattenBindingOrAssignmentElement(element, rhsValue,
				                                  element->loc, false);
			}
		}
		if (!bindingElements.empty()) {
			emitBindingOrAssignment(
				this,
				createArrayBindingOrAssignmentPattern(
					this, std::move(bindingElements)),
				value, location, pattern);
		}
		if (!restContainingElements.empty()) {
			for (auto& pair : restContainingElements) {
				flattenBindingOrAssignmentElement(pair.element, pair.id,
				                                  pair.element->loc,
				                                  false);
			}
		}
	}
};

// FlattenDestructuringAssignment — destructuring.go:27
Node* flattenDestructuringAssignment(
	Transformer* tx, Node* node /*VariableDeclaration |
	                            DestructuringAssignment*/,
	bool needsValue, FlattenLevel level,
	CreateAssignmentCallback createAssignmentCallback) {
	flattener f;
	f.tx = tx;
	f.level = level;
	f.createAssignmentCallback = std::move(createAssignmentCallback);
	f.hoistTempVariables = true;
	// Assignment mode callbacks
	f.emitBindingOrAssignment = [](flattener* fl, Node* target, Node* value,
	                               TextRange location, Node* original) {
		fl->emitAssignment(target, value, location, original);
	};
	f.createArrayBindingOrAssignmentPattern =
		[](flattener* fl, std::vector<Node*> elements) {
			return fl->createArrayAssignmentPattern(std::move(elements));
		};
	f.createObjectBindingOrAssignmentPattern =
		[](flattener* fl, std::vector<Node*> elements) {
			return fl->createObjectAssignmentPattern(std::move(elements));
		};
	f.createArrayBindingOrAssignmentElement = [](flattener* fl, Node* expr) {
		return fl->createArrayAssignmentElement(expr);
	};
	return f.flattenDestructuringAssignment(node, needsValue);
}

// FlattenDestructuringBinding — destructuring.go:57
Node* flattenDestructuringBinding(Transformer* tx, Node* node, Node* rval,
                                  FlattenLevel level,
                                  bool hoistTempVariables,
                                  bool skipInitializer) {
	flattener f;
	f.tx = tx;
	f.level = level;
	f.hoistTempVariables = hoistTempVariables;
	// Binding mode callbacks
	f.emitBindingOrAssignment = [](flattener* fl, Node* target, Node* value,
	                               TextRange location, Node* original) {
		fl->emitBinding(target, value, location, original);
	};
	f.createArrayBindingOrAssignmentPattern =
		[](flattener* fl, std::vector<Node*> elements) {
			return fl->createArrayBindingPattern(std::move(elements));
		};
	f.createObjectBindingOrAssignmentPattern =
		[](flattener* fl, std::vector<Node*> elements) {
			return fl->createObjectBindingPattern(std::move(elements));
		};
	f.createArrayBindingOrAssignmentElement = [](flattener* fl, Node* expr) {
		return fl->createArrayBindingElement(expr);
	};
	return f.flattenDestructuringBinding(node, rval, skipInitializer);
}

// ---------------------------------------------------------------------------
// Exported helper functions
// ---------------------------------------------------------------------------

// BindingOrAssignmentElementAssignsToName — destructuring.go:420
bool bindingOrAssignmentElementAssignsToName(Node* element,
                                             std::string_view name) {
	Node* target = getTargetOfBindingOrAssignmentElement(element);
	if (target == nullptr) {
		return false;
	}
	if (isBindingPattern(target) || isAssignmentPattern(target)) {
		for (Node* e : getElementsOfBindingOrAssignmentPattern(target)) {
			if (bindingOrAssignmentElementAssignsToName(e, name)) {
				return true;
			}
		}
		return false;
	}
	if (isIdentifier(target)) {
		return target->text() == name;
	}
	return false;
}

// BindingOrAssignmentElementContainsNonLiteralComputedName —
// destructuring.go:444
bool bindingOrAssignmentElementContainsNonLiteralComputedName(
	Node* element) {
	Node* propertyName =
		tryGetPropertyNameOfBindingOrAssignmentElement(element);
	if (propertyName != nullptr &&
	    isComputedPropertyName(propertyName) &&
	    !isLiteralExpression(propertyName->expression())) {
		return true;
	}
	Node* target = getTargetOfBindingOrAssignmentElement(element);
	if (target == nullptr ||
	    !(isBindingPattern(target) || isAssignmentPattern(target))) {
		return false;
	}
	for (Node* e : getElementsOfBindingOrAssignmentPattern(target)) {
		if (bindingOrAssignmentElementContainsNonLiteralComputedName(e)) {
			return true;
		}
	}
	return false;
}

// GetInitializerOfBindingOrAssignmentElement — destructuring.go:459
Node* getInitializerOfBindingOrAssignmentElement(Node* bindingElement) {
	if (bindingElement == nullptr) {
		return nullptr;
	}
	if (isDeclarationBindingElement(bindingElement)) {
		return bindingElement->initializer();
	}
	if (isPropertyAssignment(bindingElement)) {
		Node* initializer = bindingElement->initializer();
		if (isAssignmentExpression(initializer,
		                           true /*excludeCompoundAssignment*/)) {
			return initializer->as<BinaryExpression>()->Right;
		}
		return nullptr;
	}
	if (isShorthandPropertyAssignment(bindingElement)) {
		return bindingElement->as<ShorthandPropertyAssignment>()
		    ->ObjectAssignmentInitializer;
	}
	if (isAssignmentExpression(bindingElement,
	                           true /*excludeCompoundAssignment*/)) {
		return bindingElement->as<BinaryExpression>()->Right;
	}
	if (isSpreadElement(bindingElement)) {
		return getInitializerOfBindingOrAssignmentElement(
			bindingElement->expression());
	}
	return nullptr;
}

// isObjectBindingOrAssignmentPattern — destructuring.go:485
bool isObjectBindingOrAssignmentPattern(Node* node) {
	return node != nullptr &&
	       (node->kind == Kind::ObjectBindingPattern ||
	        node->kind == Kind::ObjectLiteralExpression);
}

// isArrayBindingOrAssignmentPattern — destructuring.go:489
bool isArrayBindingOrAssignmentPattern(Node* node) {
	return node != nullptr &&
	       (node->kind == Kind::ArrayBindingPattern ||
	        node->kind == Kind::ArrayLiteralExpression);
}

// isSimpleBindingOrAssignmentElement — destructuring.go:493
bool isSimpleBindingOrAssignmentElement(Node* element) {
	Node* target = getTargetOfBindingOrAssignmentElement(element);
	if (target == nullptr || isOmittedExpression(target)) {
		return true;
	}
	Node* propertyName =
		tryGetPropertyNameOfBindingOrAssignmentElement(element);
	if (propertyName != nullptr && !isPropertyNameLiteral(propertyName)) {
		return false;
	}
	Node* initializer = getInitializerOfBindingOrAssignmentElement(element);
	if (initializer != nullptr &&
	    !isSimpleInlineableExpression(initializer)) {
		return false;
	}
	if (isBindingPattern(target) || isAssignmentPattern(target)) {
		for (Node* e : getElementsOfBindingOrAssignmentPattern(target)) {
			if (!isSimpleBindingOrAssignmentElement(e)) {
				return false;
			}
		}
		return true;
	}
	return isIdentifier(target);
}

}  // namespace tsc::transformers
