// checker_utilities.cpp — utilities slice: port of tsc/internal/checker/utilities.go.
// Contains the utilities.go functions not already covered by other slices, in Go
// file order. Skipped (already ported elsewhere): tokenIsIdentifierOrKeywordOrGreaterThan,
// hasAsyncModifier, hasReadonlyModifier, isStaticPrivateIdentifierProperty,
// isConstTypeReference(Name), getAliasDeclarationFromName, entityNameToString,
// getContainingQualifiedNameNode, isSideEffectImport, isTypeReferenceIdentifier,
// isTypeAlias, hasDotDotDotToken, isExclamationToken, isOptionalDeclaration,
// isPrivateWithinAmbient, isTypeAssertion, createSymbolTable, sortSymbols,
// compareSymbolsWorker, compareNodes, getDeclarationModifierFlagsFromSymbol(Ex),
// isBinaryOperator, isObjectLiteralType, isValidNumericString portion of
// isValidNumberString-helpers, isThisProperty, isValidESSymbolDeclaration,
// isVariableDeclarationInVariableStatement, IsKnownSymbol, isLateBoundName,
// isObjectOrArrayLiteralType, getContainingClassExcludingClassDecorators,
// isThisTypeParameter, isInfinityOrNaNString, isConstantVariable,
// isParameterOrMutableLocalVariable, isMutableLocalVariableDeclaration,
// isInAmbientOrTypeNode, isCallChain, isSuperCall, getMembersOfDeclaration,
// isInRightSideOfImportOrExportAssignment, isOptionalParameter,
// forEachYieldExpression, getEnclosingContainer, getDeclarationsOfKind,
// minAndMax, getFeatureMap, nodeStartsNewLexicalEnvironment, isCanceled,
// checkNotCanceled, isUncheckedJSSuggestion, GetSetAccessorValueParameter,
// signatureHasRestParameter (static copies live in the slices that needed them
// before this file landed; new code uses the definitions here).

#include "internal/checker/checker.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "internal/checker/mapper.h"
#include "internal/jsnum/jsnum.h"
#include "internal/module/util.h"
#include "internal/scanner/scanner.h"
#include "internal/tspath/tspath.h"

namespace tsc {
namespace checker {

// --- file-local copies of helpers owned by other files (static in their TUs) ---

// checker.cpp — getBigIntLiteralValue (file-static there too).
static PseudoBigInt getBigIntLiteralValue(Type* t) {
	return std::get<PseudoBigInt>(t->AsLiteralType()->value);
}

// core.FindIndex — index of first element satisfying pred, or -1.
template <class T, class F>
static int findIndexReplica(const std::vector<T>& v, F f) {
	auto it = std::find_if(v.begin(), v.end(), f);
	return it != v.end() ? static_cast<int>(it - v.begin()) : -1;
}

// checker_declchecks.cpp — signatureHasRestParameter.
static bool signatureHasRestParameter(Signature* sig) {
	return (sig->flags & SignatureFlagsHasRestParameter) != 0;
}

// printer/utilities.go:30-181 — EscapeString support (file-statics until the
// printer slice lands; keep names distinct from any future printer exports).
namespace {

const char kEscapedChars[] = "\t\v\f\b\r\n\0\"\'\\`";

const char* getLiteralTextFlagsChar(char ch) {
	switch (ch) {
		case '\t': return "\\t";
		case '\v': return "\\v";
		case '\f': return "\\f";
		case '\b': return "\\b";
		case '\r': return "\\r";
		case '\n': return "\\n";
		case '\0': return "\\0";
		case '\"': return "\\\"";
		case '\'': return "\\'";
		case '\\': return "\\\\";
		case '`': return "\\`";
	}
	return nullptr;
}

std::string encodeUtf16EscapeSequence(uint32_t charCode) {
	char buf[16];
	snprintf(buf, sizeof(buf), "\\u%04x", charCode);
	return buf;
}

bool isDoubleQuote(char ch) { return ch == '\"' || ch == '\''; }

std::string escapeStringWorker(std::string_view s, char quoteChar) {
	std::string escaped;
	escaped.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++) {
		char ch = s[i];
		const char* repl = getLiteralTextFlagsChar(ch);
		if (repl != nullptr) {
			escaped += repl;
			continue;
		}
		if (quoteChar != '\0' && ch == quoteChar) {
			escaped += '\\';
			escaped += ch;
			continue;
		}
		unsigned char uch = static_cast<unsigned char>(ch);
		if (uch < 0x20 || uch == 0x7F) {
			escaped += encodeUtf16EscapeSequence(uch);
			continue;
		}
		escaped += ch;
	}
	return escaped;
}

// printer.EscapeString — escapes non-printable ASCII and the given quote char.
std::string escapeString(std::string_view s, char quoteChar) {
	return escapeStringWorker(s, quoteChar);
}

} // anonymous namespace

// utilities.go: hasOverrideModifier (55)
bool hasOverrideModifier(Node* node) {
	return hasSyntacticModifier(node, ModifierFlagsOverride);
}

// utilities.go: getSelectedModifierFlags (63)
ModifierFlags getSelectedModifierFlags(Node* node, ModifierFlags flags) {
	return static_cast<ModifierFlags>(node->modifierFlags() & flags);
}

// utilities.go: AssignmentKind (79)
// (enum declared in checker.h)

// isEmptyObjectLiteral (utilities.go:4038) — canonical in ast.cpp

// utilities.go: getAssignmentTargetKind (89)
AssignmentKind getAssignmentTargetKind(Node* node) {
	Node* target = getAssignmentTarget(node);
	if (target == nullptr) {
		return AssignmentKindNone;
	}
	switch (target->kind) {
		case Kind::BinaryExpression: {
			Kind binaryOperator = target->as<BinaryExpression>()->OperatorToken->kind;
			if (binaryOperator == Kind::EqualsToken ||
				isLogicalOrCoalescingAssignmentOperator(binaryOperator)) {
				return AssignmentKindDefinite;
			}
			return AssignmentKindCompound;
		}
		case Kind::PrefixUnaryExpression:
		case Kind::PostfixUnaryExpression:
			return AssignmentKindCompound;
		case Kind::ForInStatement:
		case Kind::ForOfStatement:
			return AssignmentKindDefinite;
	}
	TSC_UNREACHABLE("Unhandled case in getAssignmentTargetKind");
}

// utilities.go: isDeleteTarget (109)
bool isDeleteTarget(Node* node) {
	if (!isAccessExpression(node)) {
		return false;
	}
	node = walkUpParenthesizedExpressions(node->parent);
	return node != nullptr && node->kind == Kind::DeleteExpression;
}

// utilities.go: isInCompoundLikeAssignment (117)
bool isInCompoundLikeAssignment(Node* node) {
	Node* target = getAssignmentTarget(node);
	return target != nullptr && isAssignmentExpression(target, true /*excludeCompoundAssignment*/) &&
		isCompoundLikeAssignment(target);
}

// utilities.go: isCompoundLikeAssignment (122)
bool isCompoundLikeAssignment(Node* assignment) {
	Node* right = skipParentheses(assignment->as<BinaryExpression>()->Right);
	return right->kind == Kind::BinaryExpression &&
		isShiftOperatorOrHigher(right->as<BinaryExpression>()->OperatorToken->kind);
}

// utilities.go: GetSingleVariableOfVariableStatement (154)
Node* getSingleVariableOfVariableStatement(Node* node) {
	if (!isVariableStatement(node)) {
		return nullptr;
	}
	auto& declarations = node->as<VariableStatement>()->DeclarationList->as<VariableDeclarationList>()->Declarations->nodes;
	return declarations.empty() ? nullptr : declarations[0];
}

// utilities.go: IsInTypeQuery (168)
bool isInTypeQuery(Node* node) {
	return findAncestorOrQuit(node, [](Node* n) {
		switch (n->kind) {
			case Kind::TypeQuery:
				return FindAncestorResult::True;
			case Kind::Identifier:
			case Kind::QualifiedName:
				return FindAncestorResult::False;
		}
		return FindAncestorResult::Quit;
	}) != nullptr;
}

// utilities.go: canHaveLocals (183)
bool canHaveLocals(Node* node) {
	switch (node->kind) {
		case Kind::ArrowFunction: case Kind::Block: case Kind::CallSignature:
		case Kind::CaseBlock: case Kind::CatchClause: case Kind::ClassStaticBlockDeclaration:
		case Kind::ConditionalType: case Kind::Constructor: case Kind::ConstructorType:
		case Kind::ConstructSignature: case Kind::ForStatement: case Kind::ForInStatement:
		case Kind::ForOfStatement: case Kind::FunctionDeclaration: case Kind::FunctionExpression:
		case Kind::FunctionType: case Kind::GetAccessor: case Kind::IndexSignature:
		case Kind::JSDocSignature: case Kind::MappedType: case Kind::MethodDeclaration:
		case Kind::MethodSignature: case Kind::ModuleDeclaration: case Kind::SetAccessor:
		case Kind::SourceFile: case Kind::TypeAliasDeclaration: case Kind::JSTypeAliasDeclaration:
			return true;
	}
	return false;
}

// utilities.go: isShorthandAmbientModuleSymbol (197)
bool isShorthandAmbientModuleSymbol(Symbol* moduleSymbol) {
	return isShorthandAmbientModule(moduleSymbol->valueDeclaration);
}

// utilities.go: isShorthandAmbientModule (201)
bool isShorthandAmbientModule(Node* node) {
	// The only kind of module that can be missing a body is a shorthand ambient module.
	return node != nullptr && node->kind == Kind::ModuleDeclaration && node->body() == nullptr;
}

// utilities.go: getExternalModuleRequireArgument (233)
Node* getExternalModuleRequireArgument(Node* node) {
	if (isVariableDeclarationInitializedToRequire(node)) {
		return node->initializer()->arguments()[0];
	}
	return nullptr;
}

// utilities.go: isRightSideOfAccessExpression (240)
bool isRightSideOfAccessExpression(Node* node) {
	return node->parent != nullptr &&
		((isPropertyAccessExpression(node->parent) && node->parent->name() == node) ||
		 (isElementAccessExpression(node->parent) &&
		  node->parent->as<ElementAccessExpression>()->ArgumentExpression == node));
}

// utilities.go: isTopLevelInExternalModuleAugmentation (245)
bool isTopLevelInExternalModuleAugmentation(Node* node) {
	return node != nullptr && node->parent != nullptr && isModuleBlock(node->parent) &&
		isExternalModuleAugmentation(node->parent->parent);
}

// utilities.go: isSyntacticDefault (249)
bool isSyntacticDefault(Node* node) {
	return (isExportAssignment(node) && !node->as<ExportAssignment>()->IsExportEquals) ||
		hasSyntacticModifier(node, ModifierFlagsDefault) ||
		isExportSpecifier(node) ||
		isNamespaceExport(node);
}

// utilities.go: hasExportAssignmentSymbol (256)
bool hasExportAssignmentSymbol(Symbol* moduleSymbol) {
	return moduleSymbol->exports.count(InternalSymbolNameExportEquals) != 0;
}

// utilities.go: hasOnlyExpressionInitializer (264)
bool hasOnlyExpressionInitializer(Node* node) {
	switch (node->kind) {
		case Kind::VariableDeclaration:
		case Kind::Parameter:
		case Kind::BindingElement:
		case Kind::PropertyDeclaration:
		case Kind::PropertyAssignment:
		case Kind::EnumMember:
			return true;
	}
	return false;
}

// utilities.go: IsTypeAny (286)
bool isTypeAny(Type* t) {
	return t != nullptr && (t->flags & TypeFlagsAny) != 0;
}

// utilities.go: isJSDocOptionalParameter (290)
bool isJSDocOptionalParameter(ParameterDeclaration* /*node*/) {
	return false; // !!!
}

// isEmptyArrayLiteral (utilities.go:4042) — canonical in ast.cpp

// utilities.go: declarationBelongsToPrivateAmbientMember (333)
static bool isPrivateWithinAmbient(Node* node);
bool declarationBelongsToPrivateAmbientMember(Node* declaration) {
	Node* root = getRootDeclaration(declaration);
	Node* memberDeclaration = root;
	if (root->kind == Kind::Parameter) {
		memberDeclaration = root->parent;
	}
	return isPrivateWithinAmbient(memberDeclaration);
}

static bool isPrivateWithinAmbient(Node* node) {
	return (hasModifier(node, ModifierFlagsPrivate) ||
		isPrivateIdentifierClassElementDeclaration(node)) &&
		(node->flags & NodeFlagsAmbient) != 0;
}

// utilities.go: CompareTypes (414)
int CompareTypes(Type* t1, Type* t2) {
	if (t1 == t2) {
		return 0;
	}
	if (t1 == nullptr) {
		return -1;
	}
	if (t2 == nullptr) {
		return 1;
	}
	if (t1->checker != t2->checker) {
		TSC_UNREACHABLE("Cannot compare types from different checkers");
	}
	// First sort in order of increasing type flags values.
	if (int c = getSortOrderFlags(t1) - getSortOrderFlags(t2); c != 0) {
		return c;
	}
	// Order named types by name and, in the case of aliased types, by alias type arguments.
	if (int c = compareTypeNames(t1, t2); c != 0) {
		return c;
	}
	// We have unnamed types or types with identical names. Now sort by data specific to the type.
	switch (0) {
		case 0: {
		if (t1->flags & (TypeFlagsAny | TypeFlagsUnknown | TypeFlagsString | TypeFlagsNumber |
						 TypeFlagsBoolean | TypeFlagsBigInt | TypeFlagsESSymbol | TypeFlagsVoid |
						 TypeFlagsUndefined | TypeFlagsNull | TypeFlagsNever | TypeFlagsNonPrimitive)) {
			// Only distinguished by type IDs, handled below.
		} else if (t1->flags & TypeFlagsObject) {
			// Order instantiation expression types without relying on lazy symbol IDs.
			// Order other unnamed or identically named object types by symbol.
			if ((t1->objectFlags & ObjectFlagsInstantiationExpressionType) &&
				(t2->objectFlags & ObjectFlagsInstantiationExpressionType)) {
				Node *declaration1 = nullptr, *declaration2 = nullptr;
				if (t1->symbol != nullptr && !t1->symbol->declarations.empty()) {
					declaration1 = t1->symbol->declarations[0];
				}
				if (t2->symbol != nullptr && !t2->symbol->declarations.empty()) {
					declaration2 = t2->symbol->declarations[0];
				}
				// A single instantiation expression can produce multiple types for union constituents,
				// so compare their source declarations before comparing the shared expression node.
				if (int c = t1->checker->compareNodes(declaration1, declaration2); c != 0) {
					return c;
				}
				if (int c = t1->checker->compareNodes(t1->AsInstantiationExpressionType()->node,
													t2->AsInstantiationExpressionType()->node);
					c != 0) {
					return c;
				}
			} else if (int c = t1->checker->compareSymbols(t1->symbol, t2->symbol); c != 0) {
				return c;
			}
			// When object types have the same or no symbol, order by kind. We order type references before other kinds.
			if ((t1->objectFlags & ObjectFlagsReference) && (t2->objectFlags & ObjectFlagsReference)) {
				auto r1 = t1->AsTypeReference();
				auto r2 = t2->AsTypeReference();
				if ((r1->target->objectFlags & ObjectFlagsTuple) &&
					(r2->target->objectFlags & ObjectFlagsTuple)) {
					// Tuple types have no associated symbol, instead we order by tuple element information.
					if (int c = compareTupleTypes(r1->target->AsTupleType(), r2->target->AsTupleType());
						c != 0) {
						return c;
					}
				}
				// Here we know we have references to instantiations of the same type because we have matching targets.
				if (r1->node == nullptr && r2->node == nullptr) {
					// Non-deferred type references with the same target are sorted by their type argument lists.
					if (int c = compareTypeLists(t1->AsTypeReference()->resolvedTypeArguments,
												 t2->AsTypeReference()->resolvedTypeArguments);
						c != 0) {
						return c;
					}
				} else {
					// Deferred type references with the same target are ordered by the source location of the reference.
					if (int c = t1->checker->compareNodes(r1->node, r2->node); c != 0) {
						return c;
					}
					// Instantiations of the same deferred type reference are ordered by their associated type mappers
					// (which reflect the mapping of in-scope type parameters to type arguments).
					if (int c = compareTypeMappers(t1->AsObjectType()->mapper, t2->AsObjectType()->mapper);
						c != 0) {
						return c;
					}
				}
			} else if (t1->objectFlags & ObjectFlagsReference) {
				return -1;
			} else if (t2->objectFlags & ObjectFlagsReference) {
				return 1;
			} else {
				// Order unnamed non-reference object types by kind and instantiation data.
				if (int c = static_cast<int>(t1->objectFlags & ObjectFlagsObjectTypeKindMask) -
							static_cast<int>(t2->objectFlags & ObjectFlagsObjectTypeKindMask);
					c != 0) {
					return c;
				}
				if (t1->objectFlags & ObjectFlagsReverseMapped) {
					auto r1 = t1->AsReverseMappedType();
					auto r2 = t2->AsReverseMappedType();
					if (int c = CompareTypes(r1->source, r2->source); c != 0) {
						return c;
					}
					if (int c = CompareTypes(r1->mappedType, r2->mappedType); c != 0) {
						return c;
					}
					if (int c = CompareTypes(r1->constraintType, r2->constraintType); c != 0) {
						return c;
					}
				}
				TypeMapper* m1 = t1->AsObjectType()->mapper;
				TypeMapper* m2 = t2->AsObjectType()->mapper;
				if (t1->objectFlags & ObjectFlagsMapped) {
					// instantiateAnonymousType prepends a fresh type parameter mapping.
					// Compare the effective instantiation, not the identity of that fresh parameter.
					if (m1 != nullptr) {
						m1 = static_cast<CompositeTypeMapper*>(m1)->m2;
					}
					if (m2 != nullptr) {
						m2 = static_cast<CompositeTypeMapper*>(m2)->m2;
					}
				}
				if (int c = compareTypeMappers(m1, m2); c != 0) {
					return c;
				}
			}
		} else if (t1->flags & TypeFlagsUnion) {
			// Unions are ordered by origin and then constituent type lists.
			Type* o1 = t1->AsUnionType()->origin;
			Type* o2 = t2->AsUnionType()->origin;
			if (o1 == nullptr && o2 == nullptr) {
				if (int c = compareTypeLists(t1->AsUnionType()->types,
											 t2->AsUnionType()->types);
					c != 0) {
					return c;
				}
			} else if (o1 == nullptr) {
				return 1;
			} else if (o2 == nullptr) {
				return -1;
			} else {
				if (int c = CompareTypes(o1, o2); c != 0) {
					return c;
				}
			}
		} else if (t1->flags & TypeFlagsIntersection) {
			// Intersections are ordered by their constituent type lists.
			if (int c = compareTypeLists(t1->AsIntersectionType()->types,
										 t2->AsIntersectionType()->types);
				c != 0) {
				return c;
			}
		} else if (t1->flags & (TypeFlagsEnum | TypeFlagsEnumLiteral | TypeFlagsUniqueESSymbol)) {
			// Enum members are ordered by their symbol (and thus their declaration order).
			if (int c = t1->checker->compareSymbols(t1->symbol, t2->symbol); c != 0) {
				return c;
			}
		} else if (t1->flags & TypeFlagsStringLiteral) {
			// String literal types are ordered by their values.
			if (int c = std::get<std::string>(t1->AsLiteralType()->value)
								.compare(std::get<std::string>(t2->AsLiteralType()->value));
				c != 0) {
				return c;
			}
		} else if (t1->flags & TypeFlagsNumberLiteral) {
			// Numeric literal types are ordered by their values.
			Number n1 = std::get<Number>(t1->AsLiteralType()->value);
			Number n2 = std::get<Number>(t2->AsLiteralType()->value);
			if (n1.v != n2.v) {
				return n1.v < n2.v ? -1 : 1;
			}
		} else if (t1->flags & TypeFlagsBigIntLiteral) {
			if (int c = getBigIntLiteralValue(t1).compare(getBigIntLiteralValue(t2)); c != 0) {
				return c;
			}
		} else if (t1->flags & TypeFlagsBooleanLiteral) {
			bool b1 = std::get<bool>(t1->AsLiteralType()->value);
			bool b2 = std::get<bool>(t2->AsLiteralType()->value);
			if (b1 != b2) {
				if (b1) {
					return 1;
				}
				return -1;
			}
		} else if (t1->flags & TypeFlagsTypeParameter) {
			if (int c = t1->checker->compareSymbols(t1->symbol, t2->symbol); c != 0) {
				return c;
			}
		} else if (t1->flags & TypeFlagsIndex) {
			if (int c = CompareTypes(t1->AsIndexType()->target, t2->AsIndexType()->target); c != 0) {
				return c;
			}
			if (int c = static_cast<int>(t1->AsIndexType()->indexFlags) -
						static_cast<int>(t2->AsIndexType()->indexFlags);
				c != 0) {
				return c;
			}
		} else if (t1->flags & TypeFlagsIndexedAccess) {
			if (int c = CompareTypes(t1->AsIndexedAccessType()->objectType,
									 t2->AsIndexedAccessType()->objectType);
				c != 0) {
				return c;
			}
			if (int c = CompareTypes(t1->AsIndexedAccessType()->indexType,
									 t2->AsIndexedAccessType()->indexType);
				c != 0) {
				return c;
			}
		} else if (t1->flags & TypeFlagsConditional) {
			if (int c = t1->checker->compareNodes(t1->AsConditionalType()->root->node->asNode(),
												t2->AsConditionalType()->root->node->asNode());
				c != 0) {
				return c;
			}
			if (int c = compareTypeMappers(t1->AsConditionalType()->mapper,
										 t2->AsConditionalType()->mapper);
				c != 0) {
				return c;
			}
		} else if (t1->flags & TypeFlagsSubstitution) {
			if (int c = CompareTypes(t1->AsSubstitutionType()->baseType,
									 t2->AsSubstitutionType()->baseType);
				c != 0) {
				return c;
			}
			if (int c = CompareTypes(t1->AsSubstitutionType()->constraint,
									 t2->AsSubstitutionType()->constraint);
				c != 0) {
				return c;
			}
		} else if (t1->flags & TypeFlagsTemplateLiteral) {
			if (int c = [&]() -> int {
					auto& a = t1->AsTemplateLiteralType()->texts;
					auto& b = t2->AsTemplateLiteralType()->texts;
					size_t n = std::min(a.size(), b.size());
					for (size_t i = 0; i < n; i++) {
						if (int r = a[i].compare(b[i]); r != 0) {
							return r;
						}
					}
					return static_cast<int>(a.size()) - static_cast<int>(b.size());
				}();
				c != 0) {
				return c;
			}
			if (int c = compareTypeLists(t1->AsTemplateLiteralType()->types,
									   t2->AsTemplateLiteralType()->types);
				c != 0) {
				return c;
			}
		} else if (t1->flags & TypeFlagsStringMapping) {
			if (int c = CompareTypes(t1->AsStringMappingType()->target,
									 t2->AsStringMappingType()->target);
				c != 0) {
				return c;
			}
		}
		break;
		}
	}
	// Fall back to type IDs. This results in type creation order for built-in types.
	return static_cast<int>(t1->id) - static_cast<int>(t2->id);
}

// utilities.go: getSortOrderFlags (624)
int getSortOrderFlags(Type* t) {
	// Return TypeFlagsEnum for all enum-like unit types (they'll be sorted by their symbols)
	if ((t->flags & (TypeFlagsEnumLiteral | TypeFlagsEnum)) &&
		!(t->flags & TypeFlagsUnion)) {
		return static_cast<int>(TypeFlagsEnum);
	}
	return static_cast<int>(t->flags);
}

// utilities.go: compareTypeNames (632)
int compareTypeNames(Type* t1, Type* t2) {
	Symbol* s1 = getTypeNameSymbol(t1);
	Symbol* s2 = getTypeNameSymbol(t2);
	if (s1 == s2) {
		// Go's TypeAlias.TypeArguments is nil-safe; alias may be nil when the shared
		// name symbol came from t.symbol rather than an alias (or both are nameless).
		static const std::vector<Type*> emptyArgs;
		return compareTypeLists(t1->alias != nullptr ? t1->alias->TypeArguments() : emptyArgs,
								t2->alias != nullptr ? t2->alias->TypeArguments() : emptyArgs);
	}
	if (s1 == nullptr) {
		return 1;
	}
	if (s2 == nullptr) {
		return -1;
	}
	if (int c = s1->name.compare(s2->name); c != 0) {
		return c;
	}
	// Keep distinct same-named declarations together before comparing alias arguments or structure.
	return t1->checker->compareSymbols(s1, s2);
}

// utilities.go: getTypeNameSymbol (651)
Symbol* getTypeNameSymbol(Type* t) {
	if (t->alias != nullptr) {
		return t->alias->symbol;
	}
	if ((t->flags & (TypeFlagsTypeParameter | TypeFlagsStringMapping)) ||
		(t->objectFlags & (ObjectFlagsClassOrInterface | ObjectFlagsReference))) {
		return t->symbol;
	}
	return nullptr;
}

// utilities.go: getObjectTypeName (661)
Symbol* getObjectTypeName(Type* t) {
	if (t->objectFlags & (ObjectFlagsClassOrInterface | ObjectFlagsReference)) {
		return t->symbol;
	}
	return nullptr;
}

// utilities.go: compareTupleTypes (668)
int compareTupleTypes(TupleType* t1, TupleType* t2) {
	if (t1 == t2) {
		return 0;
	}
	if (t1->readonly != t2->readonly) {
		return t1->readonly ? 1 : -1;
	}
	if (t1->elementInfos.size() != t2->elementInfos.size()) {
		return static_cast<int>(t1->elementInfos.size()) - static_cast<int>(t2->elementInfos.size());
	}
	for (size_t i = 0; i < t1->elementInfos.size(); i++) {
		if (int c = static_cast<int>(t1->elementInfos[i].flags) -
					static_cast<int>(t2->elementInfos[i].flags);
			c != 0) {
			return c;
		}
	}
	for (size_t i = 0; i < t1->elementInfos.size(); i++) {
		if (int c = compareElementLabels(t1->elementInfos[i].labeledDeclaration,
									   t2->elementInfos[i].labeledDeclaration);
			c != 0) {
			return c;
		}
	}
	return 0;
}

// utilities.go: compareElementLabels (691)
int compareElementLabels(Node* n1, Node* n2) {
	if (n1 == n2) {
		return 0;
	}
	if (n1 == nullptr) {
		return -1;
	}
	if (n2 == nullptr) {
		return 1;
	}
	return n1->name()->text().compare(n2->name()->text());
}

// utilities.go: compareTypeLists (704)
int compareTypeLists(const std::vector<Type*>& s1, const std::vector<Type*>& s2) {
	if (s1.size() != s2.size()) {
		return static_cast<int>(s1.size()) - static_cast<int>(s2.size());
	}
	for (size_t i = 0; i < s1.size(); i++) {
		if (int c = CompareTypes(s1[i], s2[i]); c != 0) {
			return c;
		}
	}
	return 0;
}

// utilities.go: compareTypeMappers (716)
int compareTypeMappers(TypeMapper* m1, TypeMapper* m2) {
	if (m1 == m2) {
		return 0;
	}
	if (m1 == nullptr) {
		return 1;
	}
	if (m2 == nullptr) {
		return -1;
	}
	auto kind1 = m1->kind();
	auto kind2 = m2->kind();
	if (kind1 != kind2) {
		return static_cast<int>(kind1) - static_cast<int>(kind2);
	}
	switch (kind1) {
		case TypeMapperKind::Simple: {
			auto* s1 = static_cast<SimpleTypeMapper*>(m1);
			auto* s2 = static_cast<SimpleTypeMapper*>(m2);
			if (int c = CompareTypes(s1->source, s2->source); c != 0) {
				return c;
			}
			return CompareTypes(s1->target, s2->target);
		}
		case TypeMapperKind::Array: {
			auto* a1 = static_cast<ArrayTypeMapper*>(m1);
			auto* a2 = static_cast<ArrayTypeMapper*>(m2);
			if (int c = compareTypeLists(a1->sources, a2->sources); c != 0) {
				return c;
			}
			return compareTypeLists(a1->targets, a2->targets);
		}
		case TypeMapperKind::Merged: {
			auto* g1 = static_cast<MergedTypeMapper*>(m1);
			auto* g2 = static_cast<MergedTypeMapper*>(m2);
			if (int c = compareTypeMappers(g1->m1, g2->m1); c != 0) {
				return c;
			}
			return compareTypeMappers(g1->m2, g2->m2);
		}
	}
	return 0;
}

// utilities.go: isExponentiationOperator (800)
bool isExponentiationOperator(Kind kind) {
	return kind == Kind::AsteriskAsteriskToken;
}

// utilities.go: isMultiplicativeOperator (804)
bool isMultiplicativeOperator(Kind kind) {
	return kind == Kind::AsteriskToken || kind == Kind::SlashToken || kind == Kind::PercentToken;
}

// utilities.go: isMultiplicativeOperatorOrHigher (808)
bool isMultiplicativeOperatorOrHigher(Kind kind) {
	return isExponentiationOperator(kind) || isMultiplicativeOperator(kind);
}

// utilities.go: isAdditiveOperator (812)
bool isAdditiveOperator(Kind kind) {
	return kind == Kind::PlusToken || kind == Kind::MinusToken;
}

// utilities.go: isAdditiveOperatorOrHigher (816)
bool isAdditiveOperatorOrHigher(Kind kind) {
	return isAdditiveOperator(kind) || isMultiplicativeOperatorOrHigher(kind);
}

// utilities.go: isShiftOperator (820)
bool isShiftOperator(Kind kind) {
	return kind == Kind::LessThanLessThanToken || kind == Kind::GreaterThanGreaterThanToken ||
		kind == Kind::GreaterThanGreaterThanGreaterThanToken;
}

// utilities.go: isShiftOperatorOrHigher (825)
bool isShiftOperatorOrHigher(Kind kind) {
	return isShiftOperator(kind) || isAdditiveOperatorOrHigher(kind);
}

// utilities.go: isRelationalOperator (829)
bool isRelationalOperator(Kind kind) {
	return kind == Kind::LessThanToken || kind == Kind::LessThanEqualsToken ||
		kind == Kind::GreaterThanToken || kind == Kind::GreaterThanEqualsToken ||
		kind == Kind::InstanceOfKeyword || kind == Kind::InKeyword;
}

// utilities.go: isRelationalOperatorOrHigher (834)
bool isRelationalOperatorOrHigher(Kind kind) {
	return isRelationalOperator(kind) || isShiftOperatorOrHigher(kind);
}

// utilities.go: isEqualityOperator (838)
bool isEqualityOperator(Kind kind) {
	return kind == Kind::EqualsEqualsToken || kind == Kind::EqualsEqualsEqualsToken ||
		kind == Kind::ExclamationEqualsToken || kind == Kind::ExclamationEqualsEqualsToken;
}

// utilities.go: isEqualityOperatorOrHigher (843)
bool isEqualityOperatorOrHigher(Kind kind) {
	return isEqualityOperator(kind) || isRelationalOperatorOrHigher(kind);
}

// utilities.go: isBitwiseOperator (847)
bool isBitwiseOperator(Kind kind) {
	return kind == Kind::AmpersandToken || kind == Kind::BarToken || kind == Kind::CaretToken;
}

// utilities.go: isBitwiseOperatorOrHigher (851)
bool isBitwiseOperatorOrHigher(Kind kind) {
	return isBitwiseOperator(kind) || isEqualityOperatorOrHigher(kind);
}

// utilities.go: isLogicalOperatorOrHigher (855)
bool isLogicalOperatorOrHigher(Kind kind) {
	return isLogicalBinaryOperator(kind) || isBitwiseOperatorOrHigher(kind);
}

// utilities.go: isAssignmentOperatorOrHigher (859)
bool isAssignmentOperatorOrHigher(Kind kind) {
	return kind == Kind::QuestionQuestionToken || isLogicalOperatorOrHigher(kind) ||
		isAssignmentOperator(kind);
}

// utilities.go: isValidNumberString (970)
bool isValidNumberString(const std::string& s, bool roundTripOnly) {
	if (s.empty()) {
		return false;
	}
	Number n = numberFromString(s);
	return !n.isNaN() && !n.isInf() && (!roundTripOnly || n.string() == s);
}

// utilities.go: isValidBigIntString (978)
bool isValidBigIntString(const std::string& s, bool roundTripOnly) {
	if (s.empty()) {
		return false;
	}
	Scanner scanner;
	scanner.setSkipTrivia(false);
	bool success = true;
	scanner.setOnError([&success](const DiagnosticMessage*, int, int, const std::vector<std::string>&) {
		success = false;
	});
	scanner.setText(s + "n");
	Kind result = scanner.scan();
	bool negative = result == Kind::MinusToken;
	if (negative) {
		result = scanner.scan();
	}
	TokenFlags flags = scanner.tokenFlags();
	// validate that
	// * scanning proceeded without error
	// * a bigint can be scanned, and that when it is scanned, it is
	// * the full length of the input string (so the scanner is one character beyond the augmented input length)
	// * it does not contain a numeric separator (the `BigInt` constructor does not accept a numeric separator in its input)
	return success && result == Kind::BigIntLiteral && scanner.tokenEnd() == static_cast<int>(s.size()) + 1 &&
		!(flags & TokenFlagsContainsSeparator) &&
		(!roundTripOnly ||
		 s == PseudoBigInt::create(parsePseudoBigInt(scanner.tokenValue()), negative).string());
}

// utilities.go: IsPrivateIdentifierSymbol (1022)
bool isPrivateIdentifierSymbol(Symbol* symbol) {
	if (symbol == nullptr) {
		return false;
	}
	return symbol->name.rfind(std::string(1, kInternalSymbolNamePrefix) + "#", 0) == 0;
}

// utilities.go: isClassInstanceProperty (1060)
bool isClassInstanceProperty(Node* node) {
	if (isInJSFile(node) && isExpandoPropertyDeclaration(node)) {
		Node* left = node->as<BinaryExpression>()->Left;
		return (!isBindableStaticAccessExpression(left, false /*excludeThisKeyword*/) ||
				!isPrototypeAccess(left->expression())) &&
			!isBindableStaticNameExpression(left, true /*excludeThisKeyword*/);
	}
	return node->parent != nullptr && isClassLike(node->parent) &&
		isPropertyDeclaration(node) && !hasAccessorModifier(node);
}

// utilities.go: isThisInitializedObjectBindingExpression (1069)
bool isThisInitializedObjectBindingExpression(Node* node) {
	return node != nullptr && (isShorthandPropertyAssignment(node) || isPropertyAssignment(node)) &&
		isBinaryExpression(node->parent->parent) &&
		node->parent->parent->as<BinaryExpression>()->OperatorToken->kind == Kind::EqualsToken &&
		node->parent->parent->as<BinaryExpression>()->Right->kind == Kind::ThisKeyword;
}

// utilities.go: isThisInitializedDeclaration (1075)
bool isThisInitializedDeclaration(Node* node) {
	return node != nullptr && isVariableDeclaration(node) && node->initializer() != nullptr &&
		node->initializer()->kind == Kind::ThisKeyword;
}

// utilities.go: isLiteralExpressionOfObject (1107)
bool isLiteralExpressionOfObject(Node* node) {
	switch (node->kind) {
		case Kind::ObjectLiteralExpression:
		case Kind::ArrayLiteralExpression:
		case Kind::RegularExpressionLiteral:
		case Kind::FunctionExpression:
		case Kind::ClassExpression:
			return true;
	}
	return false;
}

// utilities.go: canHaveFlowNode (1116)
bool canHaveFlowNode(Node* node) {
	return node->flowNodeData().flowNode != nullptr;
}

// utilities.go: isNonNullAccess (1120)
bool isNonNullAccess(Node* node) {
	return isAccessExpression(node) && isNonNullExpression(node->expression());
}

// utilities.go: getBindingElementPropertyName (1124)
Node* getBindingElementPropertyName(Node* node) {
	return node->propertyNameOrName();
}

// utilities.go: callLikeExpressionMayHaveTypeArguments (1132)
bool Checker::callLikeExpressionMayHaveTypeArguments(Node* node) {
	return isCallOrNewExpression(node) || isTaggedTemplateExpression(node) ||
		isJsxOpeningLikeElement(node);
}

// utilities.go: isJsxIntrinsicTagName (1159)
bool isJsxIntrinsicTagName(Node* tagName) {
	return (isIdentifier(tagName) && isIntrinsicJsxName(tagName->text())) ||
		isJsxNamespacedName(tagName);
}

// utilities.go: getContainingObjectLiteral (1163)
Node* getContainingObjectLiteral(Node* f) {
	if ((f->kind == Kind::MethodDeclaration || f->kind == Kind::GetAccessor ||
		 f->kind == Kind::SetAccessor) &&
		f->parent->kind == Kind::ObjectLiteralExpression) {
		return f->parent;
	} else if (f->kind == Kind::FunctionExpression && f->parent->kind == Kind::PropertyAssignment) {
		return f->parent->parent;
	}
	return nullptr;
}

// utilities.go: isImportTypeQualifierPart (1174)
Node* isImportTypeQualifierPart(Node* node) {
	Node* parent = node->parent;
	while (isQualifiedName(parent)) {
		node = parent;
		parent = parent->parent;
	}
	if (parent != nullptr && parent->kind == Kind::ImportType &&
		parent->as<ImportTypeNode>()->Qualifier == node) {
		return parent;
	}
	return nullptr;
}

// utilities.go: isInNameOfExpressionWithTypeArgumentsOrHeritageTypeReference (1188)
bool isInNameOfExpressionWithTypeArgumentsOrHeritageTypeReference(Node* node) {
	while (node->parent->kind == Kind::PropertyAccessExpression ||
		   node->parent->kind == Kind::QualifiedName) {
		node = node->parent;
	}
	return node->parent->kind == Kind::ExpressionWithTypeArguments ||
		isNameOfHeritageClauseTypeReference(node);
}

// utilities.go: getIndexSymbolFromSymbolTable (1197)
Symbol* getIndexSymbolFromSymbolTable(SymbolTable& symbolTable) {
	auto it = symbolTable.find(InternalSymbolNameIndex);
	return it != symbolTable.end() ? it->second : nullptr;
}

// utilities.go: expressionResultIsUnused (1203)
bool expressionResultIsUnused(Node* node) {
	for (;;) {
		Node* parent = node->parent;
		// walk up parenthesized expressions, but keep a pointer to the top-most parenthesized expression
		if (isParenthesizedExpression(parent)) {
			node = parent;
			continue;
		}
		// result is unused in an expression statement, `void` expression, or the initializer or incrementer of a `for` loop
		if (isExpressionStatement(parent) || isVoidExpression(parent) ||
			(isForStatement(parent) &&
			 (parent->initializer() == node || parent->as<ForStatement>()->Incrementor == node))) {
			return true;
		}
		if (isBinaryExpression(parent) &&
			parent->as<BinaryExpression>()->OperatorToken->kind == Kind::CommaToken) {
			// left side of comma is always unused
			if (node == parent->as<BinaryExpression>()->Left) {
				return true;
			}
			// right side of comma is unused if parent is unused
			node = parent;
			continue;
		}
		return false;
	}
}

// utilities.go: pseudoBigIntToString (1228)
std::string pseudoBigIntToString(const PseudoBigInt& value) {
	return value.string();
}

// utilities.go: getSuperContainer (1232)
Node* getSuperContainer(Node* node, bool stopOnFunctions) {
	for (;;) {
		node = node->parent;
		if (node == nullptr) {
			return nullptr;
		}
		switch (node->kind) {
			case Kind::ComputedPropertyName:
				node = node->parent;
				break;
			case Kind::FunctionDeclaration:
			case Kind::FunctionExpression:
			case Kind::ArrowFunction:
				if (!stopOnFunctions) {
					continue;
				}
				[[fallthrough]];
			case Kind::PropertyDeclaration:
			case Kind::PropertySignature:
			case Kind::MethodDeclaration:
			case Kind::MethodSignature:
			case Kind::Constructor:
			case Kind::GetAccessor:
			case Kind::SetAccessor:
			case Kind::ClassStaticBlockDeclaration:
				return node;
			case Kind::Decorator:
				// Decorators are always applied outside of the body of a class or method.
				if (isParameterDeclaration(node->parent) && isClassElement(node->parent->parent)) {
					// If the decorator's parent is a Parameter, we resolve the this container from
					// the grandparent class declaration.
					node = node->parent->parent;
				} else if (isClassElement(node->parent)) {
					// If the decorator's parent is a class element, we resolve the 'this' container
					// from the parent class declaration.
					node = node->parent;
				}
				break;
		}
	}
}

// utilities.go: hasType (1308)
bool hasType(Node* node) {
	return node->type() != nullptr;
}

// utilities.go: getNonRestParameterCount (1312)
int getNonRestParameterCount(Signature* sig) {
	return static_cast<int>(sig->parameters.size()) - (signatureHasRestParameter(sig) ? 1 : 0);
}

// utilities.go: rangeOfTypeParameters (1609)
TextRange rangeOfTypeParameters(SourceFile* sourceFile, NodeList* typeParameters) {
	return {typeParameters->pos() - 1,
	        std::min<int>(static_cast<int>(sourceFile->text.size()),
	                      skipTrivia(sourceFile->text, typeParameters->end()) + 1)};
}

// utilities.go: tryGetPropertyAccessOrIdentifierToString (1613) — canonical
// shared definition (typeops previously had a static copy).
std::string tryGetPropertyAccessOrIdentifierToString(Node* expr) {
	if (isPropertyAccessExpression(expr)) {
		std::string baseStr = tryGetPropertyAccessOrIdentifierToString(expr->expression());
		if (!baseStr.empty()) {
			return baseStr + "." + entityNameToString(expr->name());
		}
	} else if (isElementAccessExpression(expr)) {
		std::string baseStr = tryGetPropertyAccessOrIdentifierToString(expr->expression());
		if (!baseStr.empty() &&
			isPropertyName(expr->as<ElementAccessExpression>()->ArgumentExpression)) {
			return baseStr + "." +
				getPropertyNameForPropertyNameNode(
					expr->as<ElementAccessExpression>()->ArgumentExpression);
		}
	} else if (isIdentifier(expr)) {
		return expr->text();
	} else if (isJsxNamespacedName(expr)) {
		return entityNameToString(expr);
	}
	return "";
}

// utilities.go: allDeclarationsInSameSourceFile (1633)
bool allDeclarationsInSameSourceFile(Symbol* symbol) {
	if (symbol->declarations.size() > 1) {
		SourceFile* sourceFile = nullptr;
		for (size_t i = 0; i < symbol->declarations.size(); i++) {
			if (i == 0) {
				sourceFile = getSourceFileOfNode(symbol->declarations[i]);
			} else if (getSourceFileOfNode(symbol->declarations[i]) != sourceFile) {
				return false;
			}
		}
	}
	return true;
}

// utilities.go: containsNonMissingUndefinedType (1647)
bool Checker::containsNonMissingUndefinedType(Type* t) {
	Type* candidate = t;
	if (t->flags & TypeFlagsUnion) {
		candidate = t->AsUnionType()->types[0];
	}
	return (candidate->flags & TypeFlagsUndefined) != 0 &&
	       candidate != missingType;
}

// utilities.go: getAnyImportSyntax (1657)
Node* getAnyImportSyntax(Node* node) {
	Node* importNode = nullptr;
	switch (node->kind) {
	case Kind::ImportEqualsDeclaration:
		importNode = node;
		break;
	case Kind::ImportClause:
		importNode = node->parent;
		break;
	case Kind::NamespaceImport:
		importNode = node->parent->parent;
		break;
	case Kind::ImportSpecifier:
		importNode = node->parent->parent->parent;
		break;
	default:
		return nullptr;
	}
	return importNode;
}

// A reserved member name consists of the byte 0xFE (which is an invalid UTF-8
// encoding) followed by one or more characters where the first character is not
// '@' or '#'. The '@' character indicates that the name is denoted by a well
// known ES Symbol instance and the '#' character indicates that the name is a
// PrivateIdentifier.
// utilities.go: isReservedMemberName (1677) — canonical shared definition.
bool isReservedMemberName(const std::string& name) {
	return name.size() >= 2 && name[0] == '\xFE' && name[1] != '@' &&
	       name[1] != '#';
}

// utilities.go: introducesArgumentsExoticObject (1681)
bool introducesArgumentsExoticObject(Node* node) {
	switch (node->kind) {
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
		return true;
	}
	return false;
}

// utilities.go: symbolsToArray (1690)
std::vector<Symbol*> symbolsToArray(const SymbolTable& symbols) {
	std::vector<Symbol*> result;
	for (auto& [id, symbol] : symbols) {
		if (!isReservedMemberName(id)) {
			result.push_back(symbol);
		}
	}
	return result;
}

// checker.go:32660 GetAliasedSymbol (services tail; needed by SkipAlias)
Symbol* Checker::GetAliasedSymbol(Symbol* symbol) {
	return resolveAlias(symbol);
}

// utilities.go: SkipAlias (1700)
Symbol* SkipAlias(Symbol* symbol, Checker* checker) {
	if (symbol->flags & SymbolFlagsAlias) {
		return checker->GetAliasedSymbol(symbol);
	}
	return symbol;
}

// utilities.go: getPackagesMap (1722)
std::unordered_map<std::string, bool>& Checker::getPackagesMap() {
	if (!packagesMap.has_value()) {
		packagesMap.emplace();
		for (auto& m : program->GetResolvedModules()) {
			if (!m.packageId.name.empty()) {
				packagesMap->operator[](m.packageId.name) =
				    (*packagesMap)[m.packageId.name] ||
				    m.extension == tspath::extensionDts;
			}
		}
	}
	return *packagesMap;
}

// utilities.go: typesPackageExists (1737)
bool Checker::typesPackageExists(const std::string& packageName) {
	auto& pkgs = getPackagesMap();
	return pkgs.find(module::GetTypesPackageName(packageName)) != pkgs.end();
}

// utilities.go: packageBundlesTypes (1743)
bool Checker::packageBundlesTypes(const std::string& packageName) {
	auto& pkgs = getPackagesMap();
	auto it = pkgs.find(packageName);
	return it != pkgs.end() && it->second;
}

// utilities.go: ValueToString (1749)
std::string ValueToString(
	const std::variant<std::monostate, std::string, Number, bool, PseudoBigInt>&
	    value) {
	if (auto* s = std::get_if<std::string>(&value)) {
		return "\"" + escapeString(*s, '"') + "\"";
	}
	if (auto* n = std::get_if<Number>(&value)) {
		return n->string();
	}
	if (auto* b = std::get_if<bool>(&value)) {
		return *b ? "true" : "false";
	}
	if (auto* p = std::get_if<PseudoBigInt>(&value)) {
		return p->string() + "n";
	}
	TSC_UNREACHABLE("unhandled value type in valueToString");
}

// utilities.go: CreateModuleNotFoundChain (1839)
DiagnosticDetails CreateModuleNotFoundChain(Program* program, SourceFile* file,
	const std::string& moduleReference, ResolutionMode mode,
	const std::string& packageName) {
	auto resolvedModule = program->GetResolvedModule(file, moduleReference, mode);

	if (resolvedModule.has_value() && !resolvedModule->alternateResult.empty()) {
		std::string pkg = packageName;
		if (resolvedModule->alternateResult.find("/node_modules/@types/") !=
		    std::string::npos) {
			pkg = "@types/" + module::MangleScopedPackageName(packageName);
		}
		return {
			There_are_types_at_0_but_this_result_could_not_be_resolved_when_respecting_package_json_exports_The_1_library_may_need_to_update_its_package_json_or_typings,
			{resolvedModule->alternateResult, pkg}};
	}

	auto& packagesMap = program->GetPackagesMap();
	auto typesIt = packagesMap.find(module::GetTypesPackageName(packageName));
	if (typesIt != packagesMap.end()) {
		return {
			If_the_0_package_actually_exposes_this_module_consider_sending_a_pull_request_to_amend_https_Colon_Slash_Slashgithub_com_SlashDefinitelyTyped_SlashDefinitelyTyped_Slashtree_Slashmaster_Slashtypes_Slash_1,
			{packageName, module::MangleScopedPackageName(packageName)}};
	}
	auto pkgIt = packagesMap.find(packageName);
	if (pkgIt != packagesMap.end() && pkgIt->second) {
		return {
			If_the_0_package_actually_exposes_this_module_try_adding_a_new_declaration_d_ts_file_containing_declare_module_1,
			{packageName, moduleReference}};
	}
	return {
		Try_npm_i_save_dev_types_Slash_1_if_it_exists_or_add_a_new_declaration_d_ts_file_containing_declare_module_0,
		{moduleReference, module::MangleScopedPackageName(packageName)}};
}

// utilities.go: CreateModeMismatchDetails (1875)
DiagnosticDetails CreateModeMismatchDetails(Program* program,
                                            SourceFile* file) {
	auto ext = tspath::tryGetExtensionFromPath(file->FileName());
	std::string targetExt =
	    ext == tspath::extensionTs
	        ? std::string(tspath::extensionMts)
	        : (ext == tspath::extensionJs ? std::string(tspath::extensionMjs)
	                                    : "");
	auto& meta = program->GetSourceFileMetaData(file->Path());
	auto& packageJsonType = meta.PackageJsonType;
	auto& packageJsonDirectory = meta.PackageJsonDirectory;

	if (!packageJsonDirectory.empty() && packageJsonType.empty()) {
		if (!targetExt.empty()) {
			return {
								    To_convert_this_file_to_an_ECMAScript_module_change_its_file_extension_to_0_or_add_the_field_type_Colon_module_to_1,
				{targetExt,
				 std::string(tspath::combinePaths(packageJsonDirectory,
				                                  {"package.json"}))}};
		}
		return {
			To_convert_this_file_to_an_ECMAScript_module_add_the_field_type_Colon_module_to_0,
			{std::string(
				tspath::combinePaths(packageJsonDirectory, {"package.json"}))}};
	}
	if (!targetExt.empty()) {
		return {
			To_convert_this_file_to_an_ECMAScript_module_change_its_file_extension_to_0_or_create_a_local_package_json_file_with_type_Colon_module,
			{targetExt}};
	}
	return {
		To_convert_this_file_to_an_ECMAScript_module_create_a_local_package_json_file_with_type_Colon_module,
		{}};
}

// utilities.go: walkUpOuterExpressions (1906)
Node* walkUpOuterExpressions(Node* node) {
	Node* parent = node->parent;
	while (parent != nullptr && isOuterExpression(parent, OEKAll)) {
		parent = parent->parent;
	}
	return parent;
}

// utilities.go: quotedAndCommaSeparated (1923) — canonical shared definition.
std::string quotedAndCommaSeparated(const std::vector<std::string>& items) {
	std::string result;
	for (size_t i = 0; i < items.size(); i++) {
		if (i != 0) {
			result += ", ";
		}
		result += "'" + items[i] + "'";
	}
	return result;
}



// checker.go:7577 — checkNonNullExpression
Type* Checker::checkNonNullExpression(Node* node) {
	return checkNonNullType(checkExpression(node), node);
}

// checker.go:7581 — checkNonNullType
Type* Checker::checkNonNullType(Type* t, Node* node) {
	return checkNonNullTypeWithReporter(
	    t, node,
	    [](Checker* c, Node* n, TypeFacts f) {
		    c->reportObjectPossiblyNullOrUndefinedError(n, f);
	    });
}

// checker.go:7585 — checkNonNullTypeWithReporter
Type* Checker::checkNonNullTypeWithReporter(
    Type* t, Node* node, void (*reportError)(Checker*, Node*, TypeFacts)) {
	if (strictNullChecks && (t->flags & TypeFlagsUnknown) != 0) {
		if (isEntityNameExpression(node)) {
			std::string nodeText = entityNameToString(node);
			if (nodeText.size() < 100) {
				error(node, X_0_is_of_type_unknown, nodeText);
				return errorType;
			}
		}
		error(node, Object_is_of_type_unknown);
		return errorType;
	}
	TypeFacts facts = getTypeFacts(t, TypeFactsIsUndefinedOrNull);
	if ((facts & TypeFactsIsUndefinedOrNull) != 0) {
		reportError(this, node, facts);
		Type* nonNullable = GetNonNullableType(t);
		if ((nonNullable->flags & (TypeFlagsNullable | TypeFlagsNever)) != 0) {
			return errorType;
		}
		return nonNullable;
	}
	return t;
}

// checker.go:7609 — checkNonNullNonVoidType
Type* Checker::checkNonNullNonVoidType(Type* t, Node* node) {
	Type* nonNullType = checkNonNullType(t, node);
	if ((nonNullType->flags & TypeFlagsVoid) != 0) {
		if (isEntityNameExpression(node)) {
			std::string nodeText = entityNameToString(node);
			if (isIdentifier(node) && nodeText == "undefined") {
				error(node, The_value_0_cannot_be_used_here, nodeText);
				return nonNullType;
			}
			if (nodeText.size() < 100) {
				error(node, X_0_is_possibly_undefined, nodeText);
				return nonNullType;
			}
		}
		error(node, Object_is_possibly_undefined);
	}
	return nonNullType;
}

// checker.go:7628 — reportObjectPossiblyNullOrUndefinedError
void Checker::reportObjectPossiblyNullOrUndefinedError(Node* node,
                                                       TypeFacts facts) {
	std::string nodeText;
	if (isEntityNameExpression(node)) {
		nodeText = entityNameToString(node);
	}
	if (node->kind == Kind::NullKeyword) {
		error(node, The_value_0_cannot_be_used_here, "null");
		return;
	}
	if (!nodeText.empty() && nodeText.size() < 100) {
		if (isIdentifier(node) && nodeText == "undefined") {
			error(node, The_value_0_cannot_be_used_here, "undefined");
			return;
		}
		error(node,
		      (facts & TypeFactsIsUndefined) != 0
		          ? ((facts & TypeFactsIsNull) != 0
		                 ? X_0_is_possibly_null_or_undefined
		                 : X_0_is_possibly_undefined)
		          : X_0_is_possibly_null,
		      nodeText);
	} else {
		error(node, (facts & TypeFactsIsUndefined) != 0
		                ? ((facts & TypeFactsIsNull) != 0
		                       ? Object_is_possibly_null_or_undefined
		                       : Object_is_possibly_undefined)
		                : Object_is_possibly_null);
	}
}

// utilities.go:71 — isStaticPrivateIdentifierProperty
bool isStaticPrivateIdentifierProperty(Symbol* s) {
	return s->valueDeclaration != nullptr &&
		isPrivateIdentifierClassElementDeclaration(s->valueDeclaration) &&
		isStatic(s->valueDeclaration);
}

// utilities.go:761 — getDeclarationModifierFlagsFromSymbolEx
ModifierFlags getDeclarationModifierFlagsFromSymbolEx(Symbol* s, bool isWrite) {
	if ((s->checkFlags & CheckFlagsSynthetic) != 0) {
		ModifierFlags accessModifier{};
		if ((!isWrite && (s->checkFlags & CheckFlagsContainsPublic) != 0) ||
			(isWrite && (s->checkFlags & CheckFlagsContainsWritePublic) != 0)) {
			accessModifier = ModifierFlagsPublic;
		} else if ((!isWrite && (s->checkFlags & CheckFlagsContainsProtected) != 0) ||
				   (isWrite && (s->checkFlags & CheckFlagsContainsWriteProtected) != 0)) {
			accessModifier = ModifierFlagsProtected;
		} else if ((!isWrite && (s->checkFlags & CheckFlagsContainsPrivate) != 0) ||
				   (isWrite && (s->checkFlags & CheckFlagsContainsWritePrivate) != 0)) {
			accessModifier = ModifierFlagsPrivate;
		}
		if ((s->checkFlags & CheckFlagsContainsStatic) != 0) {
			return accessModifier | ModifierFlagsStatic;
		}
		return accessModifier;
	}
	if (s->valueDeclaration != nullptr) {
		Node* declaration = nullptr;
		if (isWrite) {
			auto it = std::find_if(s->declarations.begin(), s->declarations.end(),
								   isSetAccessorDeclaration);
			if (it != s->declarations.end()) declaration = *it;
		}
		if (declaration == nullptr && (s->flags & SymbolFlagsGetAccessor) != 0) {
			auto it = std::find_if(s->declarations.begin(), s->declarations.end(),
								   isGetAccessorDeclaration);
			if (it != s->declarations.end()) declaration = *it;
		}
		if (declaration == nullptr) {
			declaration = s->valueDeclaration;
		}
		ModifierFlags flags = getCombinedModifierFlags(declaration);
		if (s->parent != nullptr && (s->parent->flags & SymbolFlagsClass) != 0) {
			return flags;
		}
		return flags & ~ModifierFlagsAccessibilityModifier;
	}
	if ((s->flags & SymbolFlagsPrototype) != 0) {
		return ModifierFlagsPublic | ModifierFlagsStatic;
	}
	return ModifierFlagsNone;
}

// utilities.go:757 — getDeclarationModifierFlagsFromSymbol
ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s) {
	return getDeclarationModifierFlagsFromSymbolEx(s, false /*isWrite*/);
}

// ast/utilities.go:2994 — GetClassLikeDeclarationOfSymbol
Node* getClassLikeDeclarationOfSymbol(Symbol* symbol) {
	auto it = std::find_if(symbol->declarations.begin(), symbol->declarations.end(),
						   isClassLike);
	return it == symbol->declarations.end() ? nullptr : *it;
}

// utilities.go:302 — isOptionalParameter (this file's owner; was a dep-stub
// in checker_emitresolver.cpp).
bool Checker::isOptionalParameter(Node* node) {
	// !!! TODO: JSDoc support
	if (isParameterDeclaration(node) && node->questionToken() != nullptr) {
		return true;
	}
	if (!isParameterDeclaration(node)) {
		return false;
	}
	if (node->initializer() != nullptr) {
		Signature* signature = getSignatureFromDeclaration(node->parent);
		int parameterIndex = findIndexReplica(
			node->parent->parameters(),
			[node](Node* p) { return p == node; });
		// debug.Assert(parameterIndex >= 0) — FindIndex on own-parent params
		// cannot miss for a real ParameterDeclaration node.
		// Only consider syntactic or instantiated parameters as optional, not `void` parameters as this function is used
		// in grammar checks and checking for `void` too early results in parameter types widening too early
		// and causes some noImplicitAny errors to be lost.
		return parameterIndex >= getMinArgumentCountEx(
			signature,
			MinArgumentCountFlagsStrongArityForUntypedJS |
				MinArgumentCountFlagsVoidIsNonOptional);
	}
	Node* iife = getImmediatelyInvokedFunctionExpression(node->parent);
	if (iife != nullptr) {
		int parameterIndex = findIndexReplica(
			node->parent->parameters(),
			[node](Node* p) { return p == node; });
		return node->type() == nullptr &&
			node->as<ParameterDeclaration>()->DotDotDotToken == nullptr &&
			parameterIndex >=
				static_cast<int>(getEffectiveCallArguments(iife).size());
	}
	return false;
}

}  // namespace checker
}  // namespace tsc
