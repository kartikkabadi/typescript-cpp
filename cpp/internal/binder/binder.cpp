// Port of tsc/internal/binder/binder.go — full binder: symbol tables,
// declarations, and the control-flow graph.
#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/flow.h"
#include "internal/ast/symbol.h"
#include "internal/ast/symbolflags.h"
#include "internal/binder/binder.h"
#include "internal/core/arena.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/scanner/scanner.h"

namespace tsc {

using ContainerFlags = int32_t;
inline constexpr ContainerFlags ContainerFlagsNone = 0;
inline constexpr ContainerFlags ContainerFlagsIsContainer = 1 << 0;
inline constexpr ContainerFlags ContainerFlagsIsBlockScopedContainer = 1 << 1;
inline constexpr ContainerFlags ContainerFlagsIsControlFlowContainer = 1 << 2;
inline constexpr ContainerFlags ContainerFlagsIsFunctionLike = 1 << 3;
inline constexpr ContainerFlags ContainerFlagsIsFunctionExpression = 1 << 4;
inline constexpr ContainerFlags ContainerFlagsHasLocals = 1 << 5;
inline constexpr ContainerFlags ContainerFlagsIsInterface = 1 << 6;
inline constexpr ContainerFlags ContainerFlagsIsObjectLiteralOrClassExpressionMethodOrAccessor = 1 << 7;
inline constexpr ContainerFlags ContainerFlagsIsThisContainer = 1 << 8;
inline constexpr ContainerFlags ContainerFlagsPropagatesThisKeyword = 1 << 9;

static bool isNarrowingExpression(Node* expr);
static bool isNarrowableReference(Node* node);
static bool containsNarrowableReference(Node* expr);
static bool isNarrowableOperand(Node* expr);
static bool isNarrowingBinaryExpression(BinaryExpression* expr);
static bool hasNarrowableArgument(Node* expr);
static bool isNarrowingTypeOfOperands(Node* expr1, Node* expr2);
static bool isTopLevelLogicalExpression(Node* node);
static bool isStatementCondition(Node* node);
static bool isSignedNumericLiteralBinder(Node* node); // unused; parity note
static Node* getParentOfPropertyAssignment(Node* node);
static Symbol* getInitializerSymbol(Symbol* symbol);
static void setValueDeclaration(Symbol* symbol, Node* node);
ContainerFlags getContainerFlags(Node* node);

struct ExpandoAssignmentInfo {
	Node* node;
	Node* container;
	Node* blockScopeContainer;
};

struct ActiveLabel {
	ActiveLabel* next = nullptr;
	FlowLabel* breakTarget = nullptr;
	FlowLabel* continueTarget = nullptr;
	std::string name;
	bool referenced = false;
	FlowNode* breakTargetFlow() { return breakTarget; }
	FlowNode* continueTargetFlow() { return continueTarget; }
};

// --- small core helpers (core.IfElse/OrElse/Some/AppendIfUnique) ---

template <class T> static T* orElse(T* a, T* b) { return a ? a : b; }

static void appendIfUnique(std::vector<Node*>& v, Node* n) {
	if (std::find(v.begin(), v.end(), n) == v.end()) v.push_back(n);
}

// tsc/internal/core/pattern.go
struct Pattern {
	std::string text;
	int32_t starIndex = -2; // -2 = invalid (multiple stars)
	bool isValid() const {
		return starIndex == -1 ||
		       (starIndex >= 0 && starIndex < (int32_t)text.size());
	}
};

static Pattern tryParsePattern(const std::string& pattern) {
	size_t star = pattern.find('*');
	if (star == std::string::npos)
		return {pattern, -1};
	if (pattern.find('*', star + 1) != std::string::npos)
		return {"", -2};
	return {pattern, (int32_t)star};
}

// tsc/internal/tspath/extension.go — RemoveFileExtension
static std::string removeFileExtension(const std::string& path) {
	static const char* exts[] = {
		".d.ts", ".d.mts", ".d.cts", ".mjs", ".mts", ".cjs", ".cts",
		".ts", ".js", ".tsx", ".jsx", ".json"};
	for (const char* ext : exts) {
		size_t len = std::strlen(ext);
		if (path.size() >= len &&
		    path.compare(path.size() - len, len, ext) == 0) {
			return path.substr(0, path.size() - len);
		}
	}
	return path;
}

struct Binder {
	SourceFile* file = nullptr;
	FlowNode* unreachableFlow = nullptr;

	Node* container = nullptr;
	Node* thisContainer = nullptr;
	Node* blockScopeContainer = nullptr;
	Node* lastContainer = nullptr;
	FlowNode* currentFlow = nullptr;
	FlowLabel* currentBreakTarget = nullptr;
	FlowLabel* currentContinueTarget = nullptr;
	FlowLabel* currentReturnTarget = nullptr;
	FlowLabel* currentTrueTarget = nullptr;
	FlowLabel* currentFalseTarget = nullptr;
	FlowLabel* currentExceptionTarget = nullptr;
	FlowNode* preSwitchCaseFlow = nullptr;
	ActiveLabel* activeLabelList = nullptr;
	NodeFlags emitFlags = 0;
	bool seenThisKeyword = false;
	bool hasExplicitReturn = false;
	bool hasFlowEffects = false;
	bool inAssignmentPattern = false;
	bool seenParseError = false;
	int symbolCount = 0;
	std::unordered_set<Symbol*> notConstEnumOnlyModules;
	Arena* arena = nullptr;
	std::vector<ActiveLabel> labelArena;
	std::vector<ExpandoAssignmentInfo> expandoAssignments;

	bool bind(Node* node);
	Symbol* newSymbol(SymbolFlags flags, const std::string& name) {
		symbolCount++;
		auto* result = arena->alloc<Symbol>();
		result->flags = flags;
		result->name = name;
		return result;
	}
	Symbol* declareSymbol(SymbolTable& symbolTable, Symbol* parent, Node* node,
	                    SymbolFlags includes, SymbolFlags excludes) {
		return declareSymbolEx(symbolTable, parent, node, includes, excludes,
		                       false, false);
	}
	Symbol* declareSymbolEx(SymbolTable& symbolTable, Symbol* parent, Node* node,
	                        SymbolFlags includes, SymbolFlags excludes,
	                        bool isReplaceableByMethod, bool isComputedName);
	std::string getDeclarationName(Node* node);
	std::string getDisplayName(Node* node);
	Symbol* declareModuleMember(Node* node, SymbolFlags symbolFlags,
	                            SymbolFlags symbolExcludes);
	Symbol* declareClassMember(Node* node, SymbolFlags symbolFlags,
	                           SymbolFlags symbolExcludes);
	Symbol* declareSourceFileMember(Node* node, SymbolFlags symbolFlags,
	                                SymbolFlags symbolExcludes);
	Symbol* declareSymbolAndAddToSymbolTable(Node* node, SymbolFlags symbolFlags,
	                                         SymbolFlags symbolExcludes);
	FlowNode* newFlowNode(FlowFlags flags) {
		auto* result = arena->alloc<FlowNode>();
		result->flags = flags;
		return result;
	}
	FlowNode* newFlowNodeEx(FlowFlags flags, Node* node, FlowNode* antecedent) {
		FlowNode* result = newFlowNode(flags);
		result->node = node;
		result->antecedent = antecedent;
		return result;
	}
	FlowLabel* createLoopLabel() { return newFlowNode(FlowFlagsLoopLabel); }
	FlowLabel* createBranchLabel() { return newFlowNode(FlowFlagsBranchLabel); }
	FlowNode* createReduceLabel(FlowLabel* target, FlowList* antecedents,
	                            FlowNode* antecedent) {
		auto* data = arena->alloc<FlowReduceLabelData>();
		data->target = target;
		data->antecedents = antecedents;
		return newFlowNodeEx(FlowFlagsReduceLabel, data, antecedent);
	}
	FlowNode* createFlowCondition(FlowFlags flags, FlowNode* antecedent,
	                              Node* expression);
	FlowNode* createFlowMutation(FlowFlags flags, FlowNode* antecedent,
	                             Node* node);
	FlowNode* createFlowSwitchClause(FlowNode* antecedent,
	                                 Node* switchStatement, int clauseStart,
	                                 int clauseEnd) {
		setFlowNodeReferenced(antecedent);
		auto* data = arena->alloc<FlowSwitchClauseData>();
		data->switchStatement = switchStatement;
		data->clauseStart = clauseStart;
		data->clauseEnd = clauseEnd;
		return newFlowNodeEx(FlowFlagsSwitchClause, data, antecedent);
	}
	FlowNode* createFlowCall(FlowNode* antecedent, Node* node) {
		setFlowNodeReferenced(antecedent);
		hasFlowEffects = true;
		return newFlowNodeEx(FlowFlagsCall, node, antecedent);
	}
	FlowList* newFlowList(FlowNode* head, FlowList* tail) {
		auto* result = arena->alloc<FlowList>();
		result->flow = head;
		result->next = tail;
		return result;
	}
	FlowList* combineFlowLists(FlowList* head, FlowList* tail) {
		if (!head) return tail;
		return newFlowList(head->flow, combineFlowLists(head->next, tail));
	}
	void addAntecedent(FlowLabel* label, FlowNode* antecedent);
	FlowNode* finishFlowLabel(FlowLabel* label);
	static void setFlowNodeReferenced(FlowNode* flow) {
		if (!(flow->flags & FlowFlagsReferenced)) {
			flow->flags |= FlowFlagsReferenced;
		} else {
			flow->flags |= FlowFlagsShared;
		}
	}

	void bindPropertyWorker(Node* node);
	void bindSourceFileIfExternalModule();
	void bindSourceFileAsExternalModule();
	void bindModuleDeclaration(Node* node);
	ModuleInstanceState declareModuleSymbol(Node* node);
	void bindNamespaceExportDeclaration(Node* node);
	void bindImportClause(Node* node);
	void bindExportDeclaration(Node* node);
	void bindExportAssignment(Node* node);
	void bindJsxAttributes(Node* node);
	void bindJsxAttribute(Node* node, SymbolFlags symbolFlags,
	                     SymbolFlags symbolExcludes);
	void setExportContextFlag(Node* node);
	bool hasExportDeclarations(Node* node);
	void bindFunctionExpression(Node* node);
	void bindCallExpression(Node* node);
	bool setCommonJSModuleIndicator(Node* node);
	void bindClassLikeDeclaration(Node* node);
	void bindPropertyOrMethodOrAccessor(Node* node, SymbolFlags symbolFlags,
	                                    SymbolFlags symbolExcludes);
	void bindFunctionOrConstructorType(Node* node);
	void addLateBoundAssignmentDeclarationToSymbol(Node* node, Symbol* symbol);
	void bindModuleExportsAssignment(Node* node);
	void bindExpandoPropertyAssignment(Node* node);
	void bindDeferredExpandoAssignments();
	void bindCommonJSTypeExports(Symbol* moduleSymbol);
	void bindDeferredExpandoAssignment(Node* node);
	void bindExportsOrObjectDefineProperty(Node* node);
	void bindThisPropertyAssignment(Node* node);
	std::pair<Symbol*, SymbolTable*> getThisClassAndSymbolTable();
	void bindEnumDeclaration(Node* node);
	void bindVariableDeclarationOrBindingElement(Node* node);
	void bindParameter(Node* node);
	void bindFunctionDeclaration(Node* node);
	Node* getInferTypeContainer(Node* node);
	void bindAnonymousDeclaration(Node* node, SymbolFlags symbolFlags,
	                              const std::string& name);
	void bindBlockScopedDeclaration(Node* node, SymbolFlags symbolFlags,
	                                SymbolFlags symbolExcludes);
	void bindTypeParameter(Node* node);
	Symbol* lookupEntity(Node* node, Node* container);
	Symbol* lookupName(const std::string& name, Node* container);
	void checkContextualIdentifier(Node* node);
	void checkPrivateIdentifier(Node* node);
	const DiagnosticMessage* getStrictModeIdentifierMessage(Node* node);
	void checkStrictModeFunctionName(Node* node);
	const DiagnosticMessage* getStrictModeBlockScopeFunctionDeclarationMessage(Node* node);
	void checkStrictModeBinaryExpression(Node* node);
	void checkStrictModeCatchClause(Node* node);
	void checkStrictModeDeleteExpression(Node* node);
	void checkStrictModePostfixUnaryExpression(Node* node);
	void checkStrictModePrefixUnaryExpression(Node* node);
	void checkStrictModeWithStatement(Node* node);
	void checkStrictModeLabeledStatement(Node* node);
	void checkStrictModeEvalOrArguments(Node* contextNode, Node* name);
	const DiagnosticMessage* getStrictModeEvalOrArgumentsMessage(Node* node);
	void bindContainer(Node* node, ContainerFlags containerFlags);
	void declareCommonJSVariable(const std::string& name);
	void bindChildren(Node* node);
	void bindEachChild(Node* node);
	void bindEach(const std::vector<Node*>& nodes);
	void bindNodeList(NodeList* nodeList);
	void bindModifiers(ModifierList* modifiers);
	void bindEachStatementFunctionsFirst(NodeList* statements);
	FlowLabel* setContinueTarget(Node* node, FlowLabel* target);
	template <class F>
	void doWithConditionalBranches(F&& action, Node* value,
	                               FlowLabel* trueTarget, FlowLabel* falseTarget);
	void bindCondition(Node* node, FlowLabel* trueTarget, FlowLabel* falseTarget);
	void bindIterativeStatement(Node* node, FlowLabel* breakTarget,
	                            FlowLabel* continueTarget);
	void bindAssignmentTargetFlow(Node* node);
	void bindDestructuringTargetFlow(Node* node);
	void bindWhileStatement(Node* node);
	void bindDoStatement(Node* node);
	void bindForStatement(Node* node);
	void bindForInOrForOfStatement(Node* node);
	void bindIfStatement(Node* node);
	void bindReturnStatement(Node* node);
	void bindThrowStatement(Node* node);
	void bindBreakStatement(Node* node);
	void bindContinueStatement(Node* node);
	void bindBreakOrContinueStatement(Node* label, FlowNode* currentTarget,
	                                  FlowNode* (ActiveLabel::*getTarget)());
	ActiveLabel* findActiveLabel(const std::string& name);
	void bindBreakOrContinueFlow(FlowLabel* flowLabel);
	void bindTryStatement(Node* node);
	void bindSwitchStatement(Node* node);
	void bindCaseBlock(Node* node);
	void bindCaseOrDefaultClause(Node* node);
	void bindExpressionStatement(Node* node);
	void maybeBindExpressionFlowIfCall(Node* node);
	void bindLabeledStatement(Node* node);
	void bindPrefixUnaryExpressionFlow(Node* node);
	void bindPostfixUnaryExpressionFlow(Node* node);
	void bindDestructuringAssignmentFlow(Node* node);
	void bindBinaryExpressionFlow(Node* node);
	void bindLogicalLikeExpression(Node* node, FlowLabel* trueTarget,
	                               FlowLabel* falseTarget);
	void bindDeleteExpressionFlow(Node* node);
	void bindConditionalExpressionFlow(Node* node);
	void bindVariableDeclarationFlow(Node* node);
	void bindInitializedVariableFlow(Node* node);
	void bindAccessExpressionFlow(Node* node);
	void bindOptionalChainFlow(Node* node);
	void bindOptionalChain(Node* node, FlowLabel* trueTarget,
	                       FlowLabel* falseTarget);
	void bindOptionalExpression(Node* node, FlowLabel* trueTarget,
	                            FlowLabel* falseTarget);
	bool bindOptionalChainRest(Node* node);
	void bindCallExpressionFlow(Node* node);
	void bindNonNullExpressionFlow(Node* node);
	void bindBindingElementFlow(Node* node);
	void bindParameterFlow(Node* node);
	void bindInitializer(Node* node);
	void addToContainerChain(Node* next);
	void addDeclarationToSymbol(Symbol* symbol, Node* node,
	                            SymbolFlags symbolFlags);
	void errorOnNode(Node* node, const DiagnosticMessage* message,
	                 const std::vector<std::string>& args = {});
	void errorOnFirstToken(Node* node, const DiagnosticMessage* message,
	                       const std::vector<std::string>& args = {});
	Diagnostic* createDiagnosticForNode(Node* node,
	                                  const DiagnosticMessage* message,
	                                  const std::vector<std::string>& args = {});
	void addDiagnostic(Diagnostic* diagnostic) {
		file->bindDiagnostics.push_back(diagnostic);
	}
};

static void setFlowNode(Node* node, FlowNode* flowNode) {
	auto data = node->flowNodeData();
	if (data.flowNode) *data.flowNode = flowNode;
}

static void setReturnFlowNode(Node* node, FlowNode* returnFlowNode) {
	switch (node->kind) {
	case Kind::Constructor:
		node->as<ConstructorDeclaration>()->ReturnFlowNode = returnFlowNode;
		break;
	case Kind::FunctionDeclaration:
		node->as<FunctionDeclaration>()->ReturnFlowNode = returnFlowNode;
		break;
	case Kind::FunctionExpression:
		node->as<FunctionExpression>()->ReturnFlowNode = returnFlowNode;
		break;
	case Kind::ClassStaticBlockDeclaration:
		node->as<ClassStaticBlockDeclaration>()->ReturnFlowNode = returnFlowNode;
		break;
	default:
		break;
	}
}

static bool isGeneratorFunctionExpression(Node* node) {
	return isFunctionExpression(node) &&
	       node->as<FunctionExpression>()->AsteriskToken != nullptr;
}

// ---------------------------------------------------------------------------

void bindSourceFile(SourceFile* file) {
	if (!file->isBound.load()) {
		file->bindOnce.run([file] {
			Binder b;
			b.file = file;
			b.arena = &file->nodeArena;
			b.unreachableFlow = b.newFlowNode(FlowFlagsUnreachable);
			b.bind(file);
			b.bindDeferredExpandoAssignments();
			file->SymbolCount = b.symbolCount;
			file->isBound.store(true);
		});
	}
}

Symbol* Binder::declareSymbolEx(SymbolTable& symbolTable, Symbol* parent,
                                Node* node, SymbolFlags includes,
                                SymbolFlags excludes, bool isReplaceableByMethod,
                                bool isComputedName) {
	bool isDefaultExport =
		hasSyntacticModifier(node, ModifierFlagsDefault) ||
		(isExportSpecifier(node) &&
		 moduleExportNameIsDefault(node->as<ExportSpecifier>()->name));
	std::string name;
	if (isComputedName) {
		name = InternalSymbolNameComputed;
	} else if (isDefaultExport && parent != nullptr) {
		name = InternalSymbolNameDefault;
	} else {
		name = getDeclarationName(node);
	}
	Symbol* symbol;
	if (name == InternalSymbolNameMissing) {
		symbol = newSymbol(SymbolFlagsNone, InternalSymbolNameMissing);
	} else {
		auto it = symbolTable.find(name);
		symbol = it == symbolTable.end() ? nullptr : it->second;
		if (symbol == nullptr) {
			symbol = newSymbol(SymbolFlagsNone, name);
			symbolTable[name] = symbol;
			if (isReplaceableByMethod) {
				symbol->flags |= SymbolFlagsReplaceableByMethod;
			}
		} else if (isReplaceableByMethod &&
		           !(symbol->flags & SymbolFlagsReplaceableByMethod)) {
			return symbol;
		} else if (symbol->flags & excludes) {
			if (symbol->flags & SymbolFlagsReplaceableByMethod) {
				symbol = newSymbol(SymbolFlagsNone, name);
				symbolTable[name] = symbol;
			} else if (!((includes & SymbolFlagsVariable &&
			            symbol->flags & SymbolFlagsAssignment) ||
			           (includes & SymbolFlagsAssignment &&
			            symbol->flags & SymbolFlagsVariable))) {
				const DiagnosticMessage* message;
				if (symbol->flags & SymbolFlagsBlockScopedVariable) {
					message = Cannot_redeclare_block_scoped_variable_0;
				} else {
					message = Duplicate_identifier_0;
				}
				bool messageNeedsName = true;
				if (symbol->flags & SymbolFlagsEnum ||
				    includes & SymbolFlagsEnum) {
					message =
						Enum_declarations_can_only_merge_with_namespace_or_other_enum_declarations;
					messageNeedsName = false;
				}
				bool multipleDefaultExports = false;
				if (!symbol->declarations.empty()) {
					if (isDefaultExport) {
						message = A_module_cannot_have_multiple_default_exports;
						messageNeedsName = false;
						multipleDefaultExports = true;
					} else {
						if (!symbol->declarations.empty() &&
						    isExportAssignment(node) &&
						    !node->as<ExportAssignment>()->IsExportEquals) {
							message =
								A_module_cannot_have_multiple_default_exports;
							messageNeedsName = false;
							multipleDefaultExports = true;
						}
					}
				}
				Node* declarationName = getNameOfDeclaration(node);
				if (declarationName == nullptr) declarationName = node;
				Diagnostic* diag;
				if (messageNeedsName) {
					diag = createDiagnosticForNode(declarationName, message,
					                               {getDisplayName(node)});
				} else {
					diag = createDiagnosticForNode(declarationName, message);
				}
				if (isTypeAliasDeclaration(node) &&
				    nodeIsMissing(node->type()) &&
				    hasSyntacticModifier(node, ModifierFlagsExport) &&
				    symbol->flags &
				        (SymbolFlagsAlias | SymbolFlagsType |
				         SymbolFlagsNamespace)) {
					diag->relatedInformation.push_back(createDiagnosticForNode(
						node, Did_you_mean_0,
						{"export type { " +
						 node->as<TypeAliasDeclaration>()->name->text() +
						 " }"}));
				}
				for (size_t index = 0; index < symbol->declarations.size();
				     index++) {
					Node* declaration = symbol->declarations[index];
					Node* decl = getNameOfDeclaration(declaration);
					if (decl == nullptr) decl = declaration;
					Diagnostic* d;
					if (messageNeedsName) {
						d = createDiagnosticForNode(
							decl, message, {getDisplayName(declaration)});
					} else {
						d = createDiagnosticForNode(decl, message);
					}
					if (multipleDefaultExports) {
						d->relatedInformation.push_back(
							createDiagnosticForNode(
								declarationName,
								index == 0
									? Another_export_default_is_here
									: X_and_here));
					}
					addDiagnostic(d);
					if (multipleDefaultExports) {
						diag->relatedInformation.push_back(
							createDiagnosticForNode(
								decl, The_first_export_default_is_here));
					}
				}
				addDiagnostic(diag);
				if (symbol->flags & SymbolFlagsAccessor &&
				    (symbol->flags & SymbolFlagsAccessor) !=
				        (includes & SymbolFlagsAccessor)) {
					symbol->flags |= SymbolFlagsAccessor;
				}
				symbol = newSymbol(SymbolFlagsNone, name);
			}
		}
	}
	addDeclarationToSymbol(symbol, node, includes);
	if (symbol->parent == nullptr) {
		symbol->parent = parent;
	} else if (symbol->parent != parent) {
		TSC_UNREACHABLE("Existing symbol parent should match new one");
	}
	return symbol;
}

std::string Binder::getDeclarationName(Node* node) {
	if (isExportAssignment(node)) {
		return node->as<ExportAssignment>()->IsExportEquals
		           ? InternalSymbolNameExportEquals
		           : InternalSymbolNameDefault;
	}
	Node* name = getNameOfDeclaration(node);
	if (name != nullptr) {
		if (isAmbientModule(node)) {
			std::string moduleName = name->text();
			if (isGlobalScopeAugmentation(node)) {
				return InternalSymbolNameGlobal;
			}
			Pattern pattern = tryParsePattern(moduleName);
			if (pattern.isValid() && pattern.starIndex >= 0) {
				if (Node* attributes =
				        node->as<ModuleDeclaration>()->Attributes) {
					return std::string(1, kInternalSymbolNamePrefix) + "\"" +
					       moduleName + "\"pattern@" +
					       std::to_string(getNodeId(attributes));
				}
			}
			return "\"" + moduleName + "\"";
		}
		if (isPrivateIdentifier(name)) {
			Node* containingClass = getContainingClass(node);
			if (containingClass == nullptr) {
				return InternalSymbolNameMissing;
			}
			return std::string(1, kInternalSymbolNamePrefix) + "#" +
			       std::to_string(getSymbolId(containingClass->symbol())) +
			       "@" + name->text();
		}
		if (isPropertyNameLiteral(name) || isJsxNamespacedName(name)) {
			return name->text();
		}
		if (isComputedPropertyName(name)) {
			Node* nameExpression = name->expression();
			if (isStringOrNumericLiteralLike(nameExpression)) {
				return nameExpression->text();
			}
			if (isSignedNumericLiteral(nameExpression)) {
				auto* unaryExpression =
					nameExpression->as<PrefixUnaryExpression>();
				return std::string(tokenToString(unaryExpression->Operator)) +
				       unaryExpression->Operand->text();
			}
			TSC_UNREACHABLE(
				"Only computed properties with literal names have declaration names");
		}
		return InternalSymbolNameMissing;
	}
	switch (node->kind) {
	case Kind::Constructor:
		return InternalSymbolNameConstructor;
	case Kind::FunctionType:
	case Kind::CallSignature:
		return InternalSymbolNameCall;
	case Kind::ConstructorType:
	case Kind::ConstructSignature:
		return InternalSymbolNameNew;
	case Kind::IndexSignature:
		return InternalSymbolNameIndex;
	case Kind::ExportDeclaration:
		return InternalSymbolNameExportStar;
	case Kind::SourceFile:
	case Kind::BinaryExpression:
		return InternalSymbolNameExportEquals;
	default:
		break;
	}
	return InternalSymbolNameMissing;
}

std::string Binder::getDisplayName(Node* node) {
	Node* nameNode = node->name();
	if (nameNode != nullptr) {
		return declarationNameToString(nameNode);
	}
	std::string name = getDeclarationName(node);
	if (name != InternalSymbolNameMissing) {
		return name;
	}
	return "(Missing)";
}

Symbol* Binder::declareModuleMember(Node* node, SymbolFlags symbolFlags,
                                    SymbolFlags symbolExcludes) {
	Node* container = this->container;
	bool hasExportModifier =
		(getCombinedModifierFlags(node) & ModifierFlagsExport) != 0 ||
		isImplicitlyExportedJSDocDeclaration(node);
	if (symbolFlags & SymbolFlagsAlias) {
		if (node->kind == Kind::ExportSpecifier ||
		    (node->kind == Kind::ImportEqualsDeclaration &&
		     hasExportModifier)) {
			return declareSymbol(getExports(container->symbol()),
			                     container->symbol(), node, symbolFlags,
			                     symbolExcludes);
		}
		return declareSymbol(getLocals(container), nullptr, node, symbolFlags,
		                     symbolExcludes);
	}
	if (!isAmbientModule(node) &&
	    (hasExportModifier ||
	     container->flags & NodeFlagsExportContext)) {
		if (!isLocalsContainer(container) ||
		    (hasSyntacticModifier(node, ModifierFlagsDefault) &&
		     getDeclarationName(node) == InternalSymbolNameMissing)) {
			return declareSymbol(getExports(container->symbol()),
			                     container->symbol(), node, symbolFlags,
			                     symbolExcludes);
		}
		SymbolFlags exportKind = SymbolFlagsNone;
		if (symbolFlags & SymbolFlagsValue) {
			exportKind = SymbolFlagsExportValue;
		}
		Symbol* local = declareSymbol(getLocals(container), nullptr, node,
		                              exportKind, symbolExcludes);
		local->exportSymbol = declareSymbol(getExports(container->symbol()),
		                                    container->symbol(), node,
		                                    symbolFlags, symbolExcludes);
		auto data = node->exportableData();
		if (data.localSymbol) *data.localSymbol = local;
		return local;
	}
	return declareSymbol(getLocals(container), nullptr, node, symbolFlags,
	                     symbolExcludes);
}

Symbol* Binder::declareClassMember(Node* node, SymbolFlags symbolFlags,
                                   SymbolFlags symbolExcludes) {
	if (isStatic(node)) {
		return declareSymbol(getExports(container->symbol()),
		                     container->symbol(), node, symbolFlags,
		                     symbolExcludes);
	}
	return declareSymbol(getMembers(container->symbol()), container->symbol(),
	                     node, symbolFlags, symbolExcludes);
}

Symbol* Binder::declareSourceFileMember(Node* node, SymbolFlags symbolFlags,
                                        SymbolFlags symbolExcludes) {
	if (isExternalModule(file)) {
		return declareModuleMember(node, symbolFlags, symbolExcludes);
	}
	return declareSymbol(getLocals(static_cast<Node*>(file)), nullptr, node,
	                     symbolFlags, symbolExcludes);
}

Symbol* Binder::declareSymbolAndAddToSymbolTable(Node* node,
                                                 SymbolFlags symbolFlags,
                                                 SymbolFlags symbolExcludes) {
	switch (container->kind) {
	case Kind::ModuleDeclaration:
		return declareModuleMember(node, symbolFlags, symbolExcludes);
	case Kind::SourceFile:
		return declareSourceFileMember(node, symbolFlags, symbolExcludes);
	case Kind::ClassExpression:
	case Kind::ClassDeclaration:
		return declareClassMember(node, symbolFlags, symbolExcludes);
	case Kind::EnumDeclaration:
		return declareSymbol(getExports(container->symbol()),
		                     container->symbol(), node, symbolFlags,
		                     symbolExcludes);
	case Kind::TypeLiteral:
	case Kind::ObjectLiteralExpression:
	case Kind::InterfaceDeclaration:
	case Kind::JsxAttributes:
		return declareSymbol(getMembers(container->symbol()),
		                     container->symbol(), node, symbolFlags,
		                     symbolExcludes);
	case Kind::FunctionType:
	case Kind::ConstructorType:
	case Kind::CallSignature:
	case Kind::ConstructSignature:
	case Kind::IndexSignature:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::ClassStaticBlockDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::MappedType:
		return declareSymbol(getLocals(container), nullptr, node, symbolFlags,
		                     symbolExcludes);
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in declareSymbolAndAddToSymbolTable");
}

FlowNode* Binder::createFlowCondition(FlowFlags flags, FlowNode* antecedent,
                                      Node* expression) {
	if (antecedent->flags & FlowFlagsUnreachable) return antecedent;
	if (expression == nullptr) {
		if (flags & FlowFlagsTrueCondition) return antecedent;
		return unreachableFlow;
	}
	if ((expression->kind == Kind::TrueKeyword &&
	         flags & FlowFlagsFalseCondition ||
	     expression->kind == Kind::FalseKeyword &&
	         flags & FlowFlagsTrueCondition) &&
	    !isExpressionOfOptionalChainRoot(expression) &&
	    !isNullishCoalesce(expression->parent)) {
		return unreachableFlow;
	}
	if (!isNarrowingExpression(expression)) {
		return antecedent;
	}
	setFlowNodeReferenced(antecedent);
	return newFlowNodeEx(flags, expression, antecedent);
}

FlowNode* Binder::createFlowMutation(FlowFlags flags, FlowNode* antecedent,
                                     Node* node) {
	setFlowNodeReferenced(antecedent);
	hasFlowEffects = true;
	FlowNode* result = newFlowNodeEx(flags, node, antecedent);
	if (currentExceptionTarget != nullptr) {
		addAntecedent(currentExceptionTarget, result);
	}
	return result;
}

void Binder::addAntecedent(FlowLabel* label, FlowNode* antecedent) {
	if (antecedent->flags & FlowFlagsUnreachable) return;
	FlowList* last = nullptr;
	for (FlowList* list = label->antecedents; list; list = list->next) {
		if (list->flow == antecedent) return;
		last = list;
	}
	if (!last) {
		label->antecedents = newFlowList(antecedent, nullptr);
	} else {
		last->next = newFlowList(antecedent, nullptr);
	}
	setFlowNodeReferenced(antecedent);
}

FlowNode* Binder::finishFlowLabel(FlowLabel* label) {
	if (label->antecedents == nullptr) return unreachableFlow;
	if (label->antecedents->next == nullptr) return label->antecedents->flow;
	return label;
}


// --- bind dispatch ---

static SymbolFlags getOptionalSymbolFlagForNode(Node* node) {
	Node* postfixToken = node->postfixToken();
	return postfixToken != nullptr && postfixToken->kind == Kind::QuestionToken
	           ? SymbolFlagsOptional
	           : SymbolFlagsNone;
}

bool Binder::bind(Node* node) {
	if (node == nullptr) return false;

	switch (node->kind) {
	case Kind::Identifier:
		node->as<Identifier>()->FlowNode = currentFlow;
		checkContextualIdentifier(node);
		break;
	case Kind::ThisKeyword:
	case Kind::SuperKeyword:
		if (node->kind == Kind::ThisKeyword) seenThisKeyword = true;
		node->as<KeywordExpression>()->FlowNode = currentFlow;
		break;
	case Kind::QualifiedName:
		if (currentFlow != nullptr && isPartOfTypeQuery(node))
			node->as<QualifiedName>()->FlowNode = currentFlow;
		break;
	case Kind::MetaProperty:
		node->as<MetaProperty>()->FlowNode = currentFlow;
		break;
	case Kind::PrivateIdentifier:
		checkPrivateIdentifier(node);
		break;
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
		if (currentFlow != nullptr && isNarrowableReference(node))
			setFlowNode(node, currentFlow);
		break;
	case Kind::BinaryExpression:
		switch (getAssignmentDeclarationKind(node)) {
		case JSDeclarationKind::ModuleExports:
			bindModuleExportsAssignment(node);
			break;
		case JSDeclarationKind::ExportsProperty:
			bindExportsOrObjectDefineProperty(node);
			break;
		case JSDeclarationKind::Property:
			bindExpandoPropertyAssignment(node);
			break;
		case JSDeclarationKind::ThisProperty:
			bindThisPropertyAssignment(node);
			break;
		default:
			break;
		}
		checkStrictModeBinaryExpression(node);
		break;
	case Kind::CatchClause:
		checkStrictModeCatchClause(node);
		break;
	case Kind::DeleteExpression:
		checkStrictModeDeleteExpression(node);
		break;
	case Kind::PostfixUnaryExpression:
		checkStrictModePostfixUnaryExpression(node);
		break;
	case Kind::PrefixUnaryExpression:
		checkStrictModePrefixUnaryExpression(node);
		break;
	case Kind::WithStatement:
		checkStrictModeWithStatement(node);
		break;
	case Kind::LabeledStatement:
		checkStrictModeLabeledStatement(node);
		break;
	case Kind::ThisType:
		seenThisKeyword = true;
		break;
	case Kind::TypeParameter:
		bindTypeParameter(node);
		break;
	case Kind::Parameter:
		bindParameter(node);
		break;
	case Kind::VariableDeclaration:
		bindVariableDeclarationOrBindingElement(node);
		break;
	case Kind::BindingElement:
		node->as<BindingElement>()->FlowNode = currentFlow;
		bindVariableDeclarationOrBindingElement(node);
		break;
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
		bindPropertyWorker(node);
		break;
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
		bindPropertyOrMethodOrAccessor(node, SymbolFlagsProperty,
		                               SymbolFlagsPropertyExcludes);
		break;
	case Kind::EnumMember:
		bindPropertyOrMethodOrAccessor(node, SymbolFlagsEnumMember,
		                               SymbolFlagsEnumMemberExcludes);
		break;
	case Kind::CallSignature:
	case Kind::ConstructSignature:
	case Kind::IndexSignature:
		declareSymbolAndAddToSymbolTable(node, SymbolFlagsSignature,
		                                 SymbolFlagsNone);
		break;
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
		bindPropertyOrMethodOrAccessor(
			node, SymbolFlagsMethod | getOptionalSymbolFlagForNode(node),
			isObjectLiteralMethod(node) ? SymbolFlagsValue
			                            : SymbolFlagsMethodExcludes);
		break;
	case Kind::FunctionDeclaration:
		bindFunctionDeclaration(node);
		break;
	case Kind::Constructor:
		declareSymbolAndAddToSymbolTable(node, SymbolFlagsConstructor,
		                                 SymbolFlagsNone);
		break;
	case Kind::GetAccessor:
		bindPropertyOrMethodOrAccessor(node, SymbolFlagsGetAccessor,
		                               SymbolFlagsGetAccessorExcludes);
		break;
	case Kind::SetAccessor:
		bindPropertyOrMethodOrAccessor(node, SymbolFlagsSetAccessor,
		                               SymbolFlagsSetAccessorExcludes);
		break;
	case Kind::FunctionType:
	case Kind::ConstructorType:
		bindFunctionOrConstructorType(node);
		break;
	case Kind::TypeLiteral:
	case Kind::MappedType:
		bindAnonymousDeclaration(node, SymbolFlagsTypeLiteral,
		                         InternalSymbolNameType);
		break;
	case Kind::ObjectLiteralExpression:
		bindAnonymousDeclaration(node, SymbolFlagsObjectLiteral,
		                         InternalSymbolNameObject);
		break;
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
		bindFunctionExpression(node);
		break;
	case Kind::ClassExpression:
	case Kind::ClassDeclaration:
		bindClassLikeDeclaration(node);
		break;
	case Kind::InterfaceDeclaration:
		bindBlockScopedDeclaration(node, SymbolFlagsInterface,
		                           SymbolFlagsInterfaceExcludes);
		break;
	case Kind::CallExpression:
		switch (getAssignmentDeclarationKind(node)) {
		case JSDeclarationKind::ObjectDefinePropertyValue:
			bindExpandoPropertyAssignment(node);
			break;
		case JSDeclarationKind::ObjectDefinePropertyExports:
			bindExportsOrObjectDefineProperty(node);
			break;
		default:
			break;
		}
		if (isInJSFile(node)) bindCallExpression(node);
		break;
	case Kind::TypeAliasDeclaration:
		bindBlockScopedDeclaration(node, SymbolFlagsTypeAlias,
		                           SymbolFlagsTypeAliasExcludes);
		break;
	case Kind::JSTypeAliasDeclaration:
		if (!isSourceFile(blockScopeContainer))
			bindBlockScopedDeclaration(node, SymbolFlagsTypeAlias,
			                           SymbolFlagsTypeAliasExcludes);
		break;
	case Kind::EnumDeclaration:
		bindEnumDeclaration(node);
		break;
	case Kind::ModuleDeclaration:
		bindModuleDeclaration(node);
		break;
	case Kind::ImportEqualsDeclaration:
	case Kind::NamespaceImport:
	case Kind::ImportSpecifier:
	case Kind::ExportSpecifier:
		declareSymbolAndAddToSymbolTable(node, SymbolFlagsAlias,
		                                 SymbolFlagsAliasExcludes);
		break;
	case Kind::NamespaceExportDeclaration:
		bindNamespaceExportDeclaration(node);
		break;
	case Kind::ImportClause:
		bindImportClause(node);
		break;
	case Kind::ExportDeclaration:
		bindExportDeclaration(node);
		break;
	case Kind::ExportAssignment:
		bindExportAssignment(node);
		break;
	case Kind::SourceFile:
		bindSourceFileIfExternalModule();
		break;
	case Kind::JsxAttributes:
		bindJsxAttributes(node);
		break;
	case Kind::JsxAttribute:
		bindJsxAttribute(node, SymbolFlagsProperty,
		                 SymbolFlagsPropertyExcludes);
		break;
	default:
		break;
	}

	bool thisNodeOrAnySubnodesHasError =
		(node->flags & NodeFlagsThisNodeHasError) != 0;
	if (node->kind > KindLastToken) {
		bool saveSeenParseError = seenParseError;
		seenParseError = false;
		ContainerFlags containerFlags = getContainerFlags(node);
		if (containerFlags == ContainerFlagsNone) {
			bindChildren(node);
		} else {
			bindContainer(node, containerFlags);
		}
		if (seenParseError) thisNodeOrAnySubnodesHasError = true;
		seenParseError = saveSeenParseError;
	}
	if (thisNodeOrAnySubnodesHasError) {
		node->flags |= NodeFlagsThisNodeOrAnySubNodesHasError;
		seenParseError = true;
	}
	return false;
}

void Binder::bindPropertyWorker(Node* node) {
	bool isAutoAccessor = isAutoAccessorPropertyDeclaration(node);
	SymbolFlags includes =
		isAutoAccessor ? SymbolFlagsAccessor : SymbolFlagsProperty;
	SymbolFlags excludes = isAutoAccessor ? SymbolFlagsAccessorExcludes
	                                      : SymbolFlagsPropertyExcludes;
	bindPropertyOrMethodOrAccessor(
		node, includes | getOptionalSymbolFlagForNode(node), excludes);
}

void Binder::bindSourceFileIfExternalModule() {
	setExportContextFlag(file);
	if (isExternalOrCommonJSModule(file)) {
		bindSourceFileAsExternalModule();
	} else if (isJsonSourceFile(file)) {
		bindSourceFileAsExternalModule();
		Symbol* originalSymbol = file->Symbol;
		declareSymbol(file->Symbol->exports, file->Symbol, file,
		              SymbolFlagsProperty, SymbolFlagsAll);
		file->Symbol = originalSymbol;
	}
}

void Binder::bindSourceFileAsExternalModule() {
	bindAnonymousDeclaration(file, SymbolFlagsValueModule,
	                         "\"" + removeFileExtension(file->fileName) +
	                             "\"");
}

void Binder::bindModuleDeclaration(Node* node) {
	setExportContextFlag(node);
	if (isAmbientModule(node)) {
		if (hasSyntacticModifier(node, ModifierFlagsExport)) {
			errorOnFirstToken(
				node,
				X_export_modifier_cannot_be_applied_to_ambient_modules_and_module_augmentations_since_they_are_always_visible);
		}
		if (isModuleAugmentationExternal(node)) {
			declareModuleSymbol(node);
		} else {
			Node* name = node->as<ModuleDeclaration>()->name;
			Symbol* symbol = declareSymbolAndAddToSymbolTable(
				node, SymbolFlagsValueModule, SymbolFlagsValueModuleExcludes);
			if (isStringLiteral(name)) {
				Node* attributes = node->as<ModuleDeclaration>()->Attributes;
				Pattern pattern = tryParsePattern(name->text());
				if (!pattern.isValid()) {
					errorOnFirstToken(
						name,
						Pattern_0_can_have_at_most_one_Asterisk_character,
						{name->text()});
				} else if (pattern.starIndex >= 0) {
					auto* pam = arena->alloc<PatternAmbientModule>();
					pam->pattern = pattern.text;
					pam->symbol = symbol;
					file->PatternAmbientModules.push_back(pam);
				} else if (attributes != nullptr) {
					errorOnNode(
						name,
						An_ambient_module_declaration_with_import_attributes_must_use_a_pattern_name_with_an_Asterisk_character);
				}
			}
		}
	} else {
		ModuleInstanceState state = declareModuleSymbol(node);
		if (state != ModuleInstanceState::NonInstantiated) {
			Symbol* symbol = node->symbol();
			bool constEnumOnlyModule =
				(symbol->flags &
			     (SymbolFlagsFunction | SymbolFlagsClass |
			      SymbolFlagsRegularEnum)) == 0 &&
				state == ModuleInstanceState::ConstEnumOnly &&
				notConstEnumOnlyModules.find(symbol) ==
				    notConstEnumOnlyModules.end();
			if (constEnumOnlyModule) {
				symbol->flags |= SymbolFlagsConstEnumOnlyModule;
			} else {
				symbol->flags &= ~SymbolFlagsConstEnumOnlyModule;
				notConstEnumOnlyModules.insert(symbol);
			}
		}
	}
}

ModuleInstanceState Binder::declareModuleSymbol(Node* node) {
	ModuleInstanceState state = getModuleInstanceState(node);
	bool instantiated = state != ModuleInstanceState::NonInstantiated;
	declareSymbolAndAddToSymbolTable(
		node,
		instantiated ? SymbolFlagsValueModule : SymbolFlagsNamespaceModule,
		instantiated ? SymbolFlagsValueModuleExcludes
		             : SymbolFlagsNamespaceModuleExcludes);
	return state;
}

void Binder::bindNamespaceExportDeclaration(Node* node) {
	if (node->modifiers() != nullptr) {
		errorOnNode(node, Modifiers_cannot_appear_here);
	}
	if (!isSourceFile(node->parent)) {
		errorOnNode(node, Global_module_exports_may_only_appear_at_top_level);
	} else if (!isExternalModule(node->parent->as<SourceFile>())) {
		errorOnNode(node,
		            Global_module_exports_may_only_appear_in_module_files);
	} else if (!node->parent->as<SourceFile>()->IsDeclarationFile) {
		errorOnNode(node,
		            Global_module_exports_may_only_appear_in_declaration_files);
	} else {
		declareSymbol(file->GlobalExports, file->Symbol, node,
		              SymbolFlagsAlias, SymbolFlagsAliasExcludes);
	}
}

void Binder::bindImportClause(Node* node) {
	if (node->name() != nullptr) {
		declareSymbolAndAddToSymbolTable(node, SymbolFlagsAlias,
		                                 SymbolFlagsAliasExcludes);
	}
}

void Binder::bindExportDeclaration(Node* node) {
	auto* decl = node->as<ExportDeclaration>();
	if (container->symbol() == nullptr) {
		bindAnonymousDeclaration(node, SymbolFlagsExportStar,
		                         getDeclarationName(node));
	} else if (decl->ExportClause == nullptr) {
		declareSymbol(getExports(container->symbol()), container->symbol(),
		              node, SymbolFlagsExportStar, SymbolFlagsNone);
	} else if (isNamespaceExport(decl->ExportClause)) {
		declareSymbol(getExports(container->symbol()), container->symbol(),
		              decl->ExportClause, SymbolFlagsAlias,
		              SymbolFlagsAliasExcludes);
	}
}

void Binder::bindExportAssignment(Node* node) {
	Node* container = this->container;
	if (container->symbol() == nullptr && isExportAssignment(node)) {
		bindAnonymousDeclaration(node, SymbolFlagsValue,
		                         getDeclarationName(node));
	} else {
		SymbolFlags flags = expressionIsAlias(node->expression())
		                        ? SymbolFlagsAlias
		                        : SymbolFlagsProperty;
		Symbol* symbol =
			declareSymbol(getExports(container->symbol()), container->symbol(),
		                  node, flags, SymbolFlagsAll);
		if (node->as<ExportAssignment>()->IsExportEquals) {
			setValueDeclaration(symbol, node);
		}
	}
}

void Binder::bindJsxAttributes(Node* node) {
	bindAnonymousDeclaration(node, SymbolFlagsObjectLiteral,
	                         InternalSymbolNameJSXAttributes);
}

void Binder::bindJsxAttribute(Node* node, SymbolFlags symbolFlags,
                            SymbolFlags symbolExcludes) {
	declareSymbolAndAddToSymbolTable(node, symbolFlags, symbolExcludes);
}

void Binder::setExportContextFlag(Node* node) {
	if (node->flags & NodeFlagsAmbient && !hasExportDeclarations(node)) {
		node->flags |= NodeFlagsExportContext;
	} else {
		node->flags &= ~NodeFlagsExportContext;
	}
}

bool Binder::hasExportDeclarations(Node* node) {
	std::vector<Node*> statements;
	switch (node->kind) {
	case Kind::SourceFile:
		statements = node->statements();
		break;
	case Kind::ModuleDeclaration: {
		Node* body = node->body();
		if (body != nullptr && isModuleBlock(body)) {
			statements = body->statements();
		}
		break;
	}
	default:
		break;
	}
	for (Node* s : statements) {
		if (isExportDeclaration(s) || isExportAssignment(s)) return true;
	}
	return false;
}

void Binder::bindFunctionExpression(Node* node) {
	if (!file->IsDeclarationFile && !(node->flags & NodeFlagsAmbient) &&
	    isAsyncFunction(node)) {
		emitFlags |= NodeFlagsHasAsyncFunctions;
	}
	setFlowNode(node, currentFlow);
	std::string bindingName = InternalSymbolNameFunction;
	if (isFunctionExpression(node) &&
	    node->as<FunctionExpression>()->name != nullptr) {
		checkStrictModeFunctionName(node);
		bindingName = node->as<FunctionExpression>()->name->text();
	}
	bindAnonymousDeclaration(node, SymbolFlagsFunction, bindingName);
}

void Binder::bindCallExpression(Node* node) {
	if (file->CommonJSModuleIndicator == nullptr &&
	    isRequireCall(node, false)) {
		setCommonJSModuleIndicator(node);
	}
}

bool Binder::setCommonJSModuleIndicator(Node* node) {
	if (file->ExternalModuleIndicator != nullptr &&
	    file->ExternalModuleIndicator != static_cast<Node*>(file)) {
		return false;
	}
	if (file->CommonJSModuleIndicator == nullptr) {
		file->CommonJSModuleIndicator = node;
		if (file->ExternalModuleIndicator == nullptr) {
			bindSourceFileAsExternalModule();
		}
	}
	return true;
}

void Binder::bindClassLikeDeclaration(Node* node) {
	Node* name = node->name();
	switch (node->kind) {
	case Kind::ClassDeclaration:
		bindBlockScopedDeclaration(node, SymbolFlagsClass,
		                           SymbolFlagsClassExcludes);
		break;
	case Kind::ClassExpression: {
		std::string nameText = InternalSymbolNameClass;
		if (name != nullptr) nameText = name->text();
		bindAnonymousDeclaration(node, SymbolFlagsClass, nameText);
		break;
	}
	default:
		break;
	}
	Symbol* symbol = node->symbol();
	Symbol* prototypeSymbol =
		newSymbol(SymbolFlagsProperty | SymbolFlagsPrototype, "prototype");
	Symbol* symbolExport = nullptr;
	{
		auto it = getExports(symbol).find(prototypeSymbol->name);
		if (it != getExports(symbol).end()) symbolExport = it->second;
	}
	if (symbolExport != nullptr) {
		errorOnNode(symbolExport->declarations[0], Duplicate_identifier_0,
		            {symbolName(prototypeSymbol)});
	}
	getExports(symbol)[prototypeSymbol->name] = prototypeSymbol;
	prototypeSymbol->parent = symbol;
}

void Binder::bindPropertyOrMethodOrAccessor(Node* node, SymbolFlags symbolFlags,
                                            SymbolFlags symbolExcludes) {
	if (!file->IsDeclarationFile && !(node->flags & NodeFlagsAmbient) &&
	    isAsyncFunction(node)) {
		emitFlags |= NodeFlagsHasAsyncFunctions;
	}
	if (currentFlow != nullptr &&
	    isObjectLiteralOrClassExpressionMethodOrAccessor(node)) {
		setFlowNode(node, currentFlow);
	}
	if (hasDynamicName(node)) {
		bindAnonymousDeclaration(node, symbolFlags, InternalSymbolNameComputed);
	} else {
		declareSymbolAndAddToSymbolTable(node, symbolFlags, symbolExcludes);
	}
}

void Binder::bindFunctionOrConstructorType(Node* node) {
	Symbol* symbol =
		newSymbol(SymbolFlagsSignature, getDeclarationName(node));
	addDeclarationToSymbol(symbol, node, SymbolFlagsSignature);
	Symbol* typeLiteralSymbol =
		newSymbol(SymbolFlagsTypeLiteral, InternalSymbolNameType);
	addDeclarationToSymbol(typeLiteralSymbol, node, SymbolFlagsTypeLiteral);
	typeLiteralSymbol->members[symbol->name] = symbol;
}

void Binder::addLateBoundAssignmentDeclarationToSymbol(Node* node,
                                                       Symbol* symbol) {
	SymbolTable& exports = getExports(symbol);
	Symbol* assignmentSymbol = nullptr;
	{
		auto it = exports.find(InternalSymbolNameAssignmentDeclaration);
		if (it != exports.end()) assignmentSymbol = it->second;
	}
	if (assignmentSymbol == nullptr) {
		assignmentSymbol = newSymbol(SymbolFlagsNone,
		                             InternalSymbolNameAssignmentDeclaration);
		exports[InternalSymbolNameAssignmentDeclaration] = assignmentSymbol;
	}
	assignmentSymbol->declarations.push_back(node);
}

void Binder::bindModuleExportsAssignment(Node* node) {
	if (setCommonJSModuleIndicator(node)) {
		Node* container = file;
		SymbolFlags flags =
			expressionIsAlias(node->as<BinaryExpression>()->Right)
			    ? SymbolFlagsAlias
			    : SymbolFlagsProperty;
		Symbol* symbol =
			declareSymbol(getExports(container->symbol()), container->symbol(),
		                  node, flags, 0);
		setValueDeclaration(symbol, node);
	}
}

void Binder::bindExpandoPropertyAssignment(Node* node) {
	expandoAssignments.push_back(
		{node, container, blockScopeContainer});
}

void Binder::bindDeferredExpandoAssignments() {
	for (auto& info : expandoAssignments) {
		container = info.container;
		blockScopeContainer = info.blockScopeContainer;
		bindDeferredExpandoAssignment(info.node);
	}
}

void Binder::bindCommonJSTypeExports(Symbol* moduleSymbol) {
	SymbolTable& moduleExports = moduleSymbol->exports;
	auto ee = moduleExports.find(InternalSymbolNameExportEquals);
	if (ee != moduleExports.end() && ee->second != nullptr) {
		Symbol* exportEquals = ee->second;
		for (auto& [name, symbol] : moduleExports) {
			if (name != InternalSymbolNameExportEquals &&
			    symbol->flags & (SymbolFlagsType | SymbolFlagsNamespace)) {
				getExports(exportEquals)[name] = symbol;
				exportEquals->flags |= SymbolFlagsNamespaceModule;
			}
		}
	}
}

void Binder::bindDeferredExpandoAssignment(Node* node) {
	Node* parent = getParentOfPropertyAssignment(node);
	Symbol* symbol = lookupEntity(parent, blockScopeContainer);
	if (symbol == nullptr) symbol = lookupEntity(parent, container);
	symbol = getInitializerSymbol(symbol);
	if (symbol != nullptr) {
		if (hasDynamicName(node)) {
			bindAnonymousDeclaration(
				node, SymbolFlagsProperty | SymbolFlagsAssignment,
				InternalSymbolNameComputed);
			addLateBoundAssignmentDeclarationToSymbol(node, symbol);
		} else {
			SymbolTable& exports = getExports(symbol);
			auto it = exports.find(getDeclarationName(node));
			Symbol* existing = it == exports.end() ? nullptr : it->second;
			if (existing == nullptr ||
			    existing->flags & SymbolFlagsAssignment) {
				declareSymbol(exports, symbol, node,
				              SymbolFlagsProperty | SymbolFlagsAssignment,
				              SymbolFlagsPropertyExcludes);
			}
		}
	}
}

static Node* getParentOfPropertyAssignment(Node* node) {
	switch (node->kind) {
	case Kind::BinaryExpression:
		return node->as<BinaryExpression>()->Left->expression();
	case Kind::CallExpression:
		return node->arguments()[0];
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in getParentOfPropertyAssignment");
}

void Binder::bindExportsOrObjectDefineProperty(Node* node) {
	if (setCommonJSModuleIndicator(node)) {
		Node* container = file;
		SymbolFlags flags =
			isBinaryExpression(node) &&
			        expressionIsAlias(node->as<BinaryExpression>()->Right)
			    ? SymbolFlagsAlias
			    : SymbolFlagsFunctionScopedVariable;
		declareSymbol(getExports(container->symbol()), container->symbol(),
		              node, flags, SymbolFlagsFunctionScopedVariableExcludes);
	}
}

static Symbol* getInitializerSymbol(Symbol* symbol) {
	if (symbol == nullptr || symbol->valueDeclaration == nullptr)
		return nullptr;
	Node* declaration = symbol->valueDeclaration;
	if (isFunctionDeclaration(declaration) ||
	    (isInJSFile(declaration) && isClassDeclaration(declaration))) {
		return symbol;
	}
	if (isVariableDeclaration(declaration) &&
	    ((declaration->parent->flags & NodeFlagsConst) ||
	     isInJSFile(declaration))) {
		Node* initializer = declaration->initializer();
		if (isExpandoInitializer(declaration, initializer)) {
			return initializer->symbol();
		}
	}
	if (isBinaryExpression(declaration) && isInJSFile(declaration)) {
		Node* initializer = declaration->as<BinaryExpression>()->Right;
		if (isExpandoInitializer(declaration, initializer)) {
			return initializer->symbol();
		}
	}
	return nullptr;
}

void Binder::bindThisPropertyAssignment(Node* node) {
	if (!isInJSFile(node)) return;
	auto* bin = node->as<BinaryExpression>();
	if ((isPropertyAccessExpression(bin->Left) &&
	     isPrivateIdentifier(
	         bin->Left->as<PropertyAccessExpression>()->name)) ||
	    thisContainer == nullptr) {
		return;
	}
	auto [classSymbol, symbolTable] = getThisClassAndSymbolTable();
	if (symbolTable != nullptr) {
		if (hasDynamicName(node)) {
			declareSymbolEx(*symbolTable, classSymbol, node,
			                SymbolFlagsProperty, SymbolFlagsNone, true, true);
			addLateBoundAssignmentDeclarationToSymbol(node, classSymbol);
		} else {
			declareSymbolEx(*symbolTable, classSymbol, node,
			                SymbolFlagsProperty | SymbolFlagsAssignment,
			                SymbolFlagsNone, true, false);
		}
	} else if (thisContainer->kind != Kind::FunctionDeclaration &&
	           thisContainer->kind != Kind::FunctionExpression) {
		TSC_UNREACHABLE("Unhandled case in bindThisPropertyAssignment");
	}
}

std::pair<Symbol*, SymbolTable*> Binder::getThisClassAndSymbolTable() {
	if (thisContainer == nullptr) return {nullptr, nullptr};
	switch (thisContainer->kind) {
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
		break;
	case Kind::Constructor:
	case Kind::PropertyDeclaration:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::ClassStaticBlockDeclaration: {
		Symbol* classSymbol = thisContainer->parent->symbol();
		SymbolTable* table =
			isStatic(thisContainer) ? &getExports(classSymbol)
			                        : &getMembers(classSymbol);
		return {classSymbol, table};
	}
	default:
		break;
	}
	return {nullptr, nullptr};
}

void Binder::bindEnumDeclaration(Node* node) {
	if (isEnumConst(node)) {
		bindBlockScopedDeclaration(node, SymbolFlagsConstEnum,
		                           SymbolFlagsConstEnumExcludes);
	} else {
		bindBlockScopedDeclaration(node, SymbolFlagsRegularEnum,
		                           SymbolFlagsRegularEnumExcludes);
	}
}

void Binder::bindVariableDeclarationOrBindingElement(Node* node) {
	checkStrictModeEvalOrArguments(node, node->name());
	Node* name = node->name();
	if (name != nullptr && !isBindingPattern(name)) {
		if (isVariableDeclarationInitializedToRequire(node)) {
			declareSymbolAndAddToSymbolTable(node, SymbolFlagsAlias,
			                                 SymbolFlagsAliasExcludes);
		} else if (isBlockOrCatchScoped(node)) {
			bindBlockScopedDeclaration(node, SymbolFlagsBlockScopedVariable,
			                           SymbolFlagsBlockScopedVariableExcludes);
		} else if (isPartOfParameterDeclaration(node)) {
			declareSymbolAndAddToSymbolTable(
				node, SymbolFlagsFunctionScopedVariable,
				SymbolFlagsParameterExcludes);
		} else {
			declareSymbolAndAddToSymbolTable(
				node, SymbolFlagsFunctionScopedVariable,
				SymbolFlagsFunctionScopedVariableExcludes);
		}
	}
}

void Binder::bindParameter(Node* node) {
	auto* decl = node->as<ParameterDeclaration>();
	if (!(node->flags & NodeFlagsAmbient)) {
		checkStrictModeEvalOrArguments(node, decl->name);
	}
	if (isBindingPattern(decl->name)) {
		int index = -1;
		auto params = node->parent->parameters();
		for (size_t i = 0; i < params.size(); i++) {
			if (params[i] == node) {
				index = (int)i;
				break;
			}
		}
		bindAnonymousDeclaration(node, SymbolFlagsFunctionScopedVariable,
		                         "__" + std::to_string(index));
	} else {
		declareSymbolAndAddToSymbolTable(node, SymbolFlagsFunctionScopedVariable,
		                                 SymbolFlagsParameterExcludes);
	}
	if (isParameterPropertyDeclaration(node, node->parent)) {
		Node* classDeclaration = node->parent->parent;
		SymbolFlags flags =
			SymbolFlagsProperty |
			(decl->QuestionToken != nullptr ? SymbolFlagsOptional
			                               : SymbolFlagsNone);
		declareSymbol(getMembers(classDeclaration->symbol()),
		              classDeclaration->symbol(), node, flags,
		              SymbolFlagsPropertyExcludes);
	}
}

void Binder::bindFunctionDeclaration(Node* node) {
	if (!file->IsDeclarationFile && !(node->flags & NodeFlagsAmbient) &&
	    isAsyncFunction(node)) {
		emitFlags |= NodeFlagsHasAsyncFunctions;
	}
	checkStrictModeFunctionName(node);
	bindBlockScopedDeclaration(node, SymbolFlagsFunction,
	                           SymbolFlagsFunctionExcludes);
}

Node* Binder::getInferTypeContainer(Node* node) {
	Node* extendsType = findAncestor(node, [](Node* n) {
		Node* parent = n->parent;
		return parent != nullptr && isConditionalTypeNode(parent) &&
		       parent->as<ConditionalTypeNode>()->ExtendsType == n;
	});
	if (extendsType != nullptr) return extendsType->parent;
	return nullptr;
}

void Binder::bindAnonymousDeclaration(Node* node, SymbolFlags symbolFlags,
                                      const std::string& name) {
	Symbol* symbol = newSymbol(symbolFlags, name);
	if (symbolFlags & (SymbolFlagsEnumMember | SymbolFlagsClassMember)) {
		symbol->parent = container->symbol();
	}
	addDeclarationToSymbol(symbol, node, symbolFlags);
}

void Binder::bindBlockScopedDeclaration(Node* node, SymbolFlags symbolFlags,
                                        SymbolFlags symbolExcludes) {
	switch (blockScopeContainer->kind) {
	case Kind::ModuleDeclaration:
		declareModuleMember(node, symbolFlags, symbolExcludes);
		break;
	case Kind::SourceFile:
		if (isExternalOrCommonJSModule(container->as<SourceFile>())) {
			declareModuleMember(node, symbolFlags, symbolExcludes);
			break;
		}
		[[fallthrough]];
	default:
		declareSymbol(getLocals(blockScopeContainer), nullptr, node,
		              symbolFlags, symbolExcludes);
	}
}

void Binder::bindTypeParameter(Node* node) {
	if (node->parent->kind == Kind::InferType) {
		Node* container = getInferTypeContainer(node->parent);
		if (container != nullptr) {
			declareSymbol(getLocals(container), nullptr, node,
			              SymbolFlagsTypeParameter,
			              SymbolFlagsTypeParameterExcludes);
		} else {
			bindAnonymousDeclaration(node, SymbolFlagsTypeParameter,
			                         getDeclarationName(node));
		}
	} else {
		declareSymbolAndAddToSymbolTable(node, SymbolFlagsTypeParameter,
		                                 SymbolFlagsTypeParameterExcludes);
	}
}

Symbol* Binder::lookupEntity(Node* node, Node* container) {
	if (isIdentifier(node)) return lookupName(node->text(), container);
	if (node->expression()->kind == Kind::ThisKeyword) {
		auto [cls, symbolTable] = getThisClassAndSymbolTable();
		if (symbolTable != nullptr) {
			if (Node* name = getElementOrPropertyAccessName(node)) {
				auto it = symbolTable->find(name->text());
				return it == symbolTable->end() ? nullptr : it->second;
			}
		}
		return nullptr;
	}
	Symbol* symbol = getInitializerSymbol(
		lookupEntity(node->expression(), container));
	if (symbol != nullptr) {
		if (Node* name = getElementOrPropertyAccessName(node)) {
			auto it = symbol->exports.find(name->text());
			return it == symbol->exports.end() ? nullptr : it->second;
		}
	}
	return nullptr;
}

Symbol* Binder::lookupName(const std::string& name, Node* container) {
	auto localsData = container->localsContainerData();
	if (localsData.locals) {
		auto it = localsData.locals->find(name);
		if (it != localsData.locals->end()) {
			return orElse(it->second->exportSymbol, it->second);
		}
	}
	auto declData = container->declarationData();
	if (declData.symbol && *declData.symbol) {
		Symbol* s = *declData.symbol;
		auto it = s->exports.find(name);
		return it == s->exports.end() ? nullptr : it->second;
	}
	return nullptr;
}


// --- contextual-identifier and strict-mode checks ---

void Binder::checkContextualIdentifier(Node* node) {
	if (file->diagnostics.empty() &&
	    !(node->flags & NodeFlagsAmbient) &&
	    !(node->flags & NodeFlagsJSDoc) &&
	    !isIdentifierName(node)) {
		Kind originalKeywordKind = getIdentifierToken(node->text());
		if (originalKeywordKind == Kind::Identifier) return;
		if (originalKeywordKind >= KindFirstFutureReservedWord &&
		    originalKeywordKind <= KindLastFutureReservedWord) {
			errorOnNode(node, getStrictModeIdentifierMessage(node),
			            {declarationNameToString(node)});
		} else if (originalKeywordKind == Kind::AwaitKeyword) {
			if (isExternalModule(file) && isInTopLevelContext(node)) {
				errorOnNode(
					node,
					Identifier_expected_0_is_a_reserved_word_at_the_top_level_of_a_module,
					{declarationNameToString(node)});
			} else if (node->flags & NodeFlagsAwaitContext) {
				errorOnNode(
					node,
					Identifier_expected_0_is_a_reserved_word_that_cannot_be_used_here,
					{declarationNameToString(node)});
			}
		} else if (originalKeywordKind == Kind::YieldKeyword &&
		           node->flags & NodeFlagsYieldContext) {
			errorOnNode(
				node,
				Identifier_expected_0_is_a_reserved_word_that_cannot_be_used_here,
				{declarationNameToString(node)});
		}
	}
}

void Binder::checkPrivateIdentifier(Node* node) {
	if (node->text() == "#constructor" && file->diagnostics.empty()) {
		errorOnNode(node, X_constructor_is_a_reserved_word,
		            {declarationNameToString(node)});
	}
}

const DiagnosticMessage* Binder::getStrictModeIdentifierMessage(Node* node) {
	if (getContainingClass(node) != nullptr) {
		return Identifier_expected_0_is_a_reserved_word_in_strict_mode_Class_definitions_are_automatically_in_strict_mode;
	}
	if (file->ExternalModuleIndicator != nullptr) {
		return Identifier_expected_0_is_a_reserved_word_in_strict_mode_Modules_are_automatically_in_strict_mode;
	}
	return Identifier_expected_0_is_a_reserved_word_in_strict_mode;
}

static bool isUseStrictPrologueDirective(SourceFile* sourceFile, Node* node) {
	std::string nodeText = getSourceTextOfNodeFromSourceFile(
		sourceFile, node->expression(), false);
	return nodeText == "\"use strict\"" || nodeText == "'use strict'";
}

Node* findUseStrictPrologue(SourceFile* sourceFile,
                            const std::vector<Node*>& statements) {
	for (Node* statement : statements) {
		if (isPrologueDirective(statement)) {
			if (isUseStrictPrologueDirective(sourceFile, statement))
				return statement;
		} else {
			return nullptr;
		}
	}
	return nullptr;
}

void Binder::checkStrictModeFunctionName(Node* node) {
	if (!(node->flags & NodeFlagsAmbient)) {
		checkStrictModeEvalOrArguments(node, node->name());
	}
}

const DiagnosticMessage*
Binder::getStrictModeBlockScopeFunctionDeclarationMessage(Node* node) {
	if (getContainingClass(node) != nullptr) {
		return Function_declarations_are_not_allowed_inside_blocks_in_strict_mode_when_targeting_ES5_Class_definitions_are_automatically_in_strict_mode;
	}
	if (file->ExternalModuleIndicator != nullptr) {
		return Function_declarations_are_not_allowed_inside_blocks_in_strict_mode_when_targeting_ES5_Modules_are_automatically_in_strict_mode;
	}
	return Function_declarations_are_not_allowed_inside_blocks_in_strict_mode_when_targeting_ES5;
}

void Binder::checkStrictModeBinaryExpression(Node* node) {
	auto* expr = node->as<BinaryExpression>();
	if (isLeftHandSideExpression(expr->Left) &&
	    isAssignmentOperator(expr->OperatorToken->kind)) {
		checkStrictModeEvalOrArguments(node, expr->Left);
	}
}

void Binder::checkStrictModeCatchClause(Node* node) {
	auto* clause = node->as<CatchClause>();
	if (clause->VariableDeclaration != nullptr) {
		checkStrictModeEvalOrArguments(
			node, clause->VariableDeclaration->as<VariableDeclaration>()->name);
	}
}

void Binder::checkStrictModeDeleteExpression(Node* node) {
	auto* expr = node->as<DeleteExpression>();
	if (expr->Expression->kind == Kind::Identifier) {
		errorOnNode(expr->Expression,
		            X_delete_cannot_be_called_on_an_identifier_in_strict_mode);
	}
}

void Binder::checkStrictModePostfixUnaryExpression(Node* node) {
	checkStrictModeEvalOrArguments(node,
	                               node->as<PostfixUnaryExpression>()->Operand);
}

void Binder::checkStrictModePrefixUnaryExpression(Node* node) {
	auto* expr = node->as<PrefixUnaryExpression>();
	if (expr->Operator == Kind::PlusPlusToken ||
	    expr->Operator == Kind::MinusMinusToken) {
		checkStrictModeEvalOrArguments(node, expr->Operand);
	}
}

void Binder::checkStrictModeWithStatement(Node* node) {
	errorOnFirstToken(node, X_with_statements_are_not_allowed_in_strict_mode);
}

void Binder::checkStrictModeLabeledStatement(Node* node) {
	auto* data = node->as<LabeledStatement>();
	if (isDeclarationStatement(data->Statement) ||
	    isVariableStatement(data->Statement)) {
		errorOnFirstToken(data->Label, A_label_is_not_allowed_here);
	}
}

static bool isEvalOrArgumentsIdentifier(Node* node) {
	if (isIdentifier(node)) {
		const std::string& text = node->text();
		return text == "eval" || text == "arguments";
	}
	return false;
}

void Binder::checkStrictModeEvalOrArguments(Node* contextNode, Node* name) {
	if (name != nullptr && isEvalOrArgumentsIdentifier(name)) {
		errorOnNode(name, getStrictModeEvalOrArgumentsMessage(contextNode),
		            {name->text()});
	}
}

const DiagnosticMessage*
Binder::getStrictModeEvalOrArgumentsMessage(Node* node) {
	if (getContainingClass(node) != nullptr) {
		return Code_contained_in_a_class_is_evaluated_in_JavaScript_s_strict_mode_which_does_not_allow_this_use_of_0_For_more_information_see_https_Colon_Slash_Slashdeveloper_mozilla_org_Slashen_US_Slashdocs_SlashWeb_SlashJavaScript_SlashReference_SlashStrict_mode;
	}
	if (file->ExternalModuleIndicator != nullptr) {
		return Invalid_use_of_0_Modules_are_automatically_in_strict_mode;
	}
	return Invalid_use_of_0_in_strict_mode;
}

// --- bindContainer ---

void Binder::bindContainer(Node* node, ContainerFlags containerFlags) {
	Node* saveContainer = container;
	Node* saveThisContainer = thisContainer;
	Node* savedBlockScopeContainer = blockScopeContainer;
	if (containerFlags & ContainerFlagsIsContainer) {
		container = node;
		blockScopeContainer = node;
		if (containerFlags & ContainerFlagsHasLocals) {
			addToContainerChain(node);
		}
	} else if (containerFlags & ContainerFlagsIsBlockScopedContainer) {
		blockScopeContainer = node;
		addToContainerChain(node);
	}
	if (containerFlags & ContainerFlagsIsThisContainer) {
		thisContainer = node;
	}
	if (containerFlags & ContainerFlagsIsControlFlowContainer) {
		FlowNode* saveCurrentFlow = currentFlow;
		FlowLabel* saveBreakTarget = currentBreakTarget;
		FlowLabel* saveContinueTarget = currentContinueTarget;
		FlowLabel* saveReturnTarget = currentReturnTarget;
		FlowLabel* saveExceptionTarget = currentExceptionTarget;
		ActiveLabel* saveActiveLabelList = activeLabelList;
		bool saveHasExplicitReturn = hasExplicitReturn;
		bool saveSeenThisKeyword = seenThisKeyword;
		bool isImmediatelyInvoked =
			(containerFlags & ContainerFlagsIsFunctionExpression &&
			 !hasSyntacticModifier(node, ModifierFlagsAsync) &&
			 !isGeneratorFunctionExpression(node) &&
			 getImmediatelyInvokedFunctionExpression(node) != nullptr) ||
			node->kind == Kind::ClassStaticBlockDeclaration;
		if (!isImmediatelyInvoked) {
			FlowNode* flowStart = newFlowNode(FlowFlagsStart);
			currentFlow = flowStart;
			if (containerFlags &
			    (ContainerFlagsIsFunctionExpression |
			     ContainerFlagsIsObjectLiteralOrClassExpressionMethodOrAccessor)) {
				flowStart->node = node;
			}
		}
		if (isImmediatelyInvoked || node->kind == Kind::Constructor) {
			currentReturnTarget = newFlowNode(FlowFlagsBranchLabel);
		} else {
			currentReturnTarget = nullptr;
		}
		currentExceptionTarget = nullptr;
		currentBreakTarget = nullptr;
		currentContinueTarget = nullptr;
		activeLabelList = nullptr;
		hasExplicitReturn = false;
		seenThisKeyword = false;
		bindChildren(node);
		node->flags &= ~NodeFlagsReachabilityAndEmitFlags;
		node->flags &= ~NodeFlagsContainsThis;
		if (!(currentFlow->flags & FlowFlagsUnreachable) &&
		    containerFlags & ContainerFlagsIsFunctionLike) {
			auto bodyData = node->bodyData();
			if (bodyData.body && *bodyData.body &&
			    !nodeIsMissing(*bodyData.body)) {
				node->flags |= NodeFlagsHasImplicitReturn;
				if (hasExplicitReturn) {
					node->flags |= NodeFlagsHasExplicitReturn;
				}
				*bodyData.endFlowNode = currentFlow;
			}
		}
		if (seenThisKeyword) node->flags |= NodeFlagsContainsThis;
		if (node->kind == Kind::SourceFile) node->flags |= emitFlags;
		if (currentReturnTarget != nullptr) {
			addAntecedent(currentReturnTarget, currentFlow);
			currentFlow = finishFlowLabel(currentReturnTarget);
			if (node->kind == Kind::Constructor ||
			    node->kind == Kind::ClassStaticBlockDeclaration) {
				setReturnFlowNode(node, currentFlow);
			}
		}
		if (!isImmediatelyInvoked) {
			currentFlow = saveCurrentFlow;
		}
		currentBreakTarget = saveBreakTarget;
		currentContinueTarget = saveContinueTarget;
		currentReturnTarget = saveReturnTarget;
		currentExceptionTarget = saveExceptionTarget;
		activeLabelList = saveActiveLabelList;
		hasExplicitReturn = saveHasExplicitReturn;
		if (containerFlags & ContainerFlagsPropagatesThisKeyword) {
			seenThisKeyword = saveSeenThisKeyword || seenThisKeyword;
		} else {
			seenThisKeyword = saveSeenThisKeyword;
		}
	} else if (containerFlags & ContainerFlagsIsInterface) {
		bool saveSeenThisKeyword = seenThisKeyword;
		seenThisKeyword = false;
		bindChildren(node);
		if (seenThisKeyword) {
			node->flags |= NodeFlagsContainsThis;
		} else {
			node->flags &= ~NodeFlagsContainsThis;
		}
		seenThisKeyword = saveSeenThisKeyword;
	} else {
		bindChildren(node);
	}
	if (isSourceFile(node) && isInJSFile(node)) {
		for (Node* statement : node->statements()) {
			if (isJSTypeAliasDeclaration(statement)) {
				bindBlockScopedDeclaration(statement, SymbolFlagsTypeAlias,
				                           SymbolFlagsTypeAliasExcludes);
			}
		}
		if (file->CommonJSModuleIndicator != nullptr) {
			declareCommonJSVariable("module");
			declareCommonJSVariable("exports");
		}
	}
	if ((isSourceFile(node) &&
	     isExternalOrCommonJSModule(node->as<SourceFile>())) ||
	    isAmbientModule(node)) {
		bindCommonJSTypeExports(node->symbol());
	}
	container = saveContainer;
	thisContainer = saveThisContainer;
	blockScopeContainer = savedBlockScopeContainer;
}

void Binder::declareCommonJSVariable(const std::string& name) {
	SymbolTable& locals = getLocals(static_cast<Node*>(file));
	if (locals.find(name) == locals.end()) {
		Symbol* symbol =
			newSymbol(SymbolFlagsFunctionScopedVariable |
		                  SymbolFlagsModuleExports,
		              name);
		symbol->declarations = {static_cast<Node*>(file)};
		symbol->valueDeclaration = symbol->declarations[0];
		if (name == "module") {
			Symbol* exportsProperty = newSymbol(
				SymbolFlagsModuleExports | SymbolFlagsProperty, "exports");
			exportsProperty->declarations = symbol->declarations;
			exportsProperty->valueDeclaration = symbol->valueDeclaration;
			exportsProperty->parent = symbol;
			symbol->members["exports"] = exportsProperty;
		}
		locals[name] = symbol;
	}
}

void Binder::bindChildren(Node* node) {
	bool saveInAssignmentPattern = inAssignmentPattern;
	inAssignmentPattern = false;
	if (currentFlow == unreachableFlow) {
		auto flowNodeData = node->flowNodeData();
		if (flowNodeData.flowNode) *flowNodeData.flowNode = nullptr;
		if (isPotentiallyExecutableNode(node)) {
			node->flags |= NodeFlagsUnreachable;
		}
		bindEachChild(node);
		inAssignmentPattern = saveInAssignmentPattern;
		return;
	}
	if (KindFirstStatement <= node->kind && node->kind <= KindLastStatement) {
		auto flowNodeData = node->flowNodeData();
		if (flowNodeData.flowNode) *flowNodeData.flowNode = currentFlow;
	}
	switch (node->kind) {
	case Kind::WhileStatement:
		bindWhileStatement(node);
		break;
	case Kind::DoStatement:
		bindDoStatement(node);
		break;
	case Kind::ForStatement:
		bindForStatement(node);
		break;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		bindForInOrForOfStatement(node);
		break;
	case Kind::IfStatement:
		bindIfStatement(node);
		break;
	case Kind::ReturnStatement:
		bindReturnStatement(node);
		break;
	case Kind::ThrowStatement:
		bindThrowStatement(node);
		break;
	case Kind::BreakStatement:
		bindBreakStatement(node);
		break;
	case Kind::ContinueStatement:
		bindContinueStatement(node);
		break;
	case Kind::TryStatement:
		bindTryStatement(node);
		break;
	case Kind::SwitchStatement:
		bindSwitchStatement(node);
		break;
	case Kind::CaseBlock:
		bindCaseBlock(node);
		break;
	case Kind::CaseClause:
	case Kind::DefaultClause:
		bindCaseOrDefaultClause(node);
		break;
	case Kind::ExpressionStatement:
		bindExpressionStatement(node);
		break;
	case Kind::LabeledStatement:
		bindLabeledStatement(node);
		break;
	case Kind::PrefixUnaryExpression:
		bindPrefixUnaryExpressionFlow(node);
		break;
	case Kind::PostfixUnaryExpression:
		bindPostfixUnaryExpressionFlow(node);
		break;
	case Kind::BinaryExpression:
		if (isDestructuringAssignment(node)) {
			inAssignmentPattern = saveInAssignmentPattern;
			bindDestructuringAssignmentFlow(node);
			inAssignmentPattern = saveInAssignmentPattern;
			return;
		}
		bindBinaryExpressionFlow(node);
		break;
	case Kind::DeleteExpression:
		bindDeleteExpressionFlow(node);
		break;
	case Kind::ConditionalExpression:
		bindConditionalExpressionFlow(node);
		break;
	case Kind::VariableDeclaration:
		bindVariableDeclarationFlow(node);
		break;
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
		bindAccessExpressionFlow(node);
		break;
	case Kind::CallExpression:
		bindCallExpressionFlow(node);
		break;
	case Kind::NonNullExpression:
		bindNonNullExpressionFlow(node);
		break;
	case Kind::SourceFile: {
		auto* sourceFile = node->as<SourceFile>();
		bindEachStatementFunctionsFirst(sourceFile->Statements);
		bind(sourceFile->EndOfFileToken);
		break;
	}
	case Kind::Block:
	case Kind::ModuleBlock:
		bindEachStatementFunctionsFirst(node->statementList());
		break;
	case Kind::BindingElement:
		bindBindingElementFlow(node);
		break;
	case Kind::Parameter:
		bindParameterFlow(node);
		break;
	case Kind::ObjectLiteralExpression:
	case Kind::ArrayLiteralExpression:
	case Kind::PropertyAssignment:
	case Kind::SpreadElement:
		inAssignmentPattern = saveInAssignmentPattern;
		bindEachChild(node);
		break;
	default:
		bindEachChild(node);
		break;
	}
	inAssignmentPattern = saveInAssignmentPattern;
}

void Binder::bindEachChild(Node* node) {
	node->forEachChild([this](Node* n) -> bool { return bind(n); });
}

void Binder::bindEach(const std::vector<Node*>& nodes) {
	for (Node* node : nodes) bind(node);
}

void Binder::bindNodeList(NodeList* nodeList) {
	if (nodeList) bindEach(nodeList->nodes);
}

void Binder::bindModifiers(ModifierList* modifiers) {
	if (modifiers) bindEach(modifiers->nodes);
}

void Binder::bindEachStatementFunctionsFirst(NodeList* statements) {
	for (Node* node : statements->nodes) {
		if (node->kind == Kind::FunctionDeclaration) bind(node);
	}
	for (Node* node : statements->nodes) {
		if (node->kind != Kind::FunctionDeclaration) bind(node);
	}
}

FlowLabel* Binder::setContinueTarget(Node* node, FlowLabel* target) {
	ActiveLabel* label = activeLabelList;
	while (label != nullptr && node->parent &&
	       node->parent->kind == Kind::LabeledStatement) {
		label->continueTarget = target;
		label = label->next;
		node = node->parent;
	}
	return target;
}

template <class F>
void Binder::doWithConditionalBranches(F&& action, Node* value,
                                       FlowLabel* trueTarget,
                                       FlowLabel* falseTarget) {
	FlowLabel* savedTrueTarget = currentTrueTarget;
	FlowLabel* savedFalseTarget = currentFalseTarget;
	currentTrueTarget = trueTarget;
	currentFalseTarget = falseTarget;
	action(value);
	currentTrueTarget = savedTrueTarget;
	currentFalseTarget = savedFalseTarget;
}

static bool isLogicalAssignmentExpression(Node* node) {
	return isLogicalOrCoalescingAssignmentExpression(skipParentheses(node));
}

void Binder::bindCondition(Node* node, FlowLabel* trueTarget,
                           FlowLabel* falseTarget) {
	doWithConditionalBranches(
		[this](Node* n) { bind(n); }, node, trueTarget, falseTarget);
	if (node == nullptr ||
	    (!isLogicalAssignmentExpression(node) && !isLogicalExpression(node) &&
	     !(isOptionalChain(node) && isOutermostOptionalChain(node)))) {
		addAntecedent(trueTarget, createFlowCondition(FlowFlagsTrueCondition,
		                                            currentFlow, node));
		addAntecedent(falseTarget,
		              createFlowCondition(FlowFlagsFalseCondition, currentFlow,
		                                  node));
	}
}

void Binder::bindIterativeStatement(Node* node, FlowLabel* breakTarget,
                                    FlowLabel* continueTarget) {
	FlowLabel* saveBreakTarget = currentBreakTarget;
	FlowLabel* saveContinueTarget = currentContinueTarget;
	currentBreakTarget = breakTarget;
	currentContinueTarget = continueTarget;
	bind(node);
	currentBreakTarget = saveBreakTarget;
	currentContinueTarget = saveContinueTarget;
}

void Binder::bindAssignmentTargetFlow(Node* node) {
	switch (node->kind) {
	case Kind::ArrayLiteralExpression:
		for (Node* e : node->elements()) {
			if (e->kind == Kind::SpreadElement) {
				bindAssignmentTargetFlow(e->expression());
			} else {
				bindDestructuringTargetFlow(e);
			}
		}
		break;
	case Kind::ObjectLiteralExpression:
		for (Node* p : node->properties()) {
			switch (p->kind) {
			case Kind::PropertyAssignment:
				bindDestructuringTargetFlow(p->initializer());
				break;
			case Kind::ShorthandPropertyAssignment:
				bindAssignmentTargetFlow(
					p->as<ShorthandPropertyAssignment>()->name);
				break;
			case Kind::SpreadAssignment:
				bindAssignmentTargetFlow(p->expression());
				break;
			default:
				break;
			}
		}
		break;
	default:
		if (isNarrowableReference(node)) {
			currentFlow = createFlowMutation(FlowFlagsAssignment, currentFlow,
			                                 node);
		}
		break;
	}
}

void Binder::bindDestructuringTargetFlow(Node* node) {
	if (isBinaryExpression(node) &&
	    node->as<BinaryExpression>()->OperatorToken->kind == Kind::EqualsToken) {
		bindAssignmentTargetFlow(node->as<BinaryExpression>()->Left);
	} else {
		bindAssignmentTargetFlow(node);
	}
}

void Binder::bindWhileStatement(Node* node) {
	auto* stmt = node->as<WhileStatement>();
	FlowLabel* preWhileLabel = setContinueTarget(node, createLoopLabel());
	FlowLabel* preBodyLabel = createBranchLabel();
	FlowLabel* postWhileLabel = createBranchLabel();
	addAntecedent(preWhileLabel, currentFlow);
	currentFlow = preWhileLabel;
	bindCondition(stmt->Expression, preBodyLabel, postWhileLabel);
	currentFlow = finishFlowLabel(preBodyLabel);
	bindIterativeStatement(stmt->Statement, postWhileLabel, preWhileLabel);
	addAntecedent(preWhileLabel, currentFlow);
	currentFlow = finishFlowLabel(postWhileLabel);
}

void Binder::bindDoStatement(Node* node) {
	auto* stmt = node->as<DoStatement>();
	FlowLabel* preDoLabel = createLoopLabel();
	FlowLabel* preConditionLabel =
		setContinueTarget(node, createBranchLabel());
	FlowLabel* postDoLabel = createBranchLabel();
	addAntecedent(preDoLabel, currentFlow);
	currentFlow = preDoLabel;
	bindIterativeStatement(stmt->Statement, postDoLabel, preConditionLabel);
	addAntecedent(preConditionLabel, currentFlow);
	currentFlow = finishFlowLabel(preConditionLabel);
	bindCondition(stmt->Expression, preDoLabel, postDoLabel);
	currentFlow = finishFlowLabel(postDoLabel);
}

void Binder::bindForStatement(Node* node) {
	auto* stmt = node->as<ForStatement>();
	bind(stmt->Initializer);
	if (currentFlow == unreachableFlow) {
		bind(stmt->Condition);
		bind(stmt->Statement);
		bind(stmt->Incrementor);
		return;
	}
	FlowLabel* preLoopLabel = setContinueTarget(node, createLoopLabel());
	FlowLabel* preBodyLabel = createBranchLabel();
	FlowLabel* preIncrementorLabel = createBranchLabel();
	FlowLabel* postLoopLabel = createBranchLabel();
	addAntecedent(preLoopLabel, currentFlow);
	currentFlow = preLoopLabel;
	bindCondition(stmt->Condition, preBodyLabel, postLoopLabel);
	currentFlow = finishFlowLabel(preBodyLabel);
	bindIterativeStatement(stmt->Statement, postLoopLabel,
	                       preIncrementorLabel);
	addAntecedent(preIncrementorLabel, currentFlow);
	currentFlow = finishFlowLabel(preIncrementorLabel);
	bind(stmt->Incrementor);
	addAntecedent(preLoopLabel, currentFlow);
	currentFlow = finishFlowLabel(postLoopLabel);
}

void Binder::bindForInOrForOfStatement(Node* node) {
	auto* stmt = node->as<ForInOrOfStatement>();
	bind(stmt->Expression);
	if (currentFlow == unreachableFlow) {
		bind(stmt->Initializer);
		bind(stmt->Statement);
		return;
	}
	FlowLabel* preLoopLabel = setContinueTarget(node, createLoopLabel());
	FlowLabel* postLoopLabel = createBranchLabel();
	addAntecedent(preLoopLabel, currentFlow);
	currentFlow = preLoopLabel;
	if (node->kind == Kind::ForOfStatement) {
		bind(stmt->AwaitModifier);
	}
	addAntecedent(postLoopLabel, currentFlow);
	bind(stmt->Initializer);
	if (stmt->Initializer->kind != Kind::VariableDeclarationList) {
		bindAssignmentTargetFlow(stmt->Initializer);
	}
	bindIterativeStatement(stmt->Statement, postLoopLabel, preLoopLabel);
	addAntecedent(preLoopLabel, currentFlow);
	currentFlow = finishFlowLabel(postLoopLabel);
}

void Binder::bindIfStatement(Node* node) {
	auto* stmt = node->as<IfStatement>();
	FlowLabel* thenLabel = createBranchLabel();
	FlowLabel* elseLabel = createBranchLabel();
	FlowLabel* postIfLabel = createBranchLabel();
	bindCondition(stmt->Expression, thenLabel, elseLabel);
	currentFlow = finishFlowLabel(thenLabel);
	bind(stmt->ThenStatement);
	addAntecedent(postIfLabel, currentFlow);
	currentFlow = finishFlowLabel(elseLabel);
	bind(stmt->ElseStatement);
	addAntecedent(postIfLabel, currentFlow);
	currentFlow = finishFlowLabel(postIfLabel);
}

void Binder::bindReturnStatement(Node* node) {
	bind(node->expression());
	if (currentReturnTarget != nullptr) {
		addAntecedent(currentReturnTarget, currentFlow);
	}
	currentFlow = unreachableFlow;
	hasExplicitReturn = true;
	hasFlowEffects = true;
}

void Binder::bindThrowStatement(Node* node) {
	bind(node->expression());
	currentFlow = unreachableFlow;
	hasFlowEffects = true;
}

void Binder::bindBreakStatement(Node* node) {
	bindBreakOrContinueStatement(node->label(), currentBreakTarget,
	                             &ActiveLabel::breakTargetFlow);
}

void Binder::bindContinueStatement(Node* node) {
	bindBreakOrContinueStatement(node->label(), currentContinueTarget,
	                             &ActiveLabel::continueTargetFlow);
}

void Binder::bindBreakOrContinueStatement(
	Node* label, FlowNode* currentTarget,
	FlowNode* (ActiveLabel::*getTarget)()) {
	bind(label);
	if (label != nullptr) {
		ActiveLabel* activeLabel = findActiveLabel(label->text());
		if (activeLabel != nullptr) {
			activeLabel->referenced = true;
			bindBreakOrContinueFlow((activeLabel->*getTarget)());
		}
	} else {
		bindBreakOrContinueFlow(currentTarget);
	}
}

ActiveLabel* Binder::findActiveLabel(const std::string& name) {
	for (ActiveLabel* label = activeLabelList; label; label = label->next) {
		if (label->name == name) return label;
	}
	return nullptr;
}

void Binder::bindBreakOrContinueFlow(FlowLabel* flowLabel) {
	if (flowLabel != nullptr) {
		addAntecedent(flowLabel, currentFlow);
		currentFlow = unreachableFlow;
		hasFlowEffects = true;
	}
}

void Binder::bindTryStatement(Node* node) {
	auto* stmt = node->as<TryStatement>();
	FlowLabel* saveReturnTarget = currentReturnTarget;
	FlowLabel* saveExceptionTarget = currentExceptionTarget;
	FlowLabel* normalExitLabel = createBranchLabel();
	FlowLabel* returnLabel = createBranchLabel();
	FlowLabel* exceptionLabel = createBranchLabel();
	if (stmt->FinallyBlock != nullptr) {
		currentReturnTarget = returnLabel;
	}
	addAntecedent(exceptionLabel, currentFlow);
	currentExceptionTarget = exceptionLabel;
	bind(stmt->TryBlock);
	addAntecedent(normalExitLabel, currentFlow);
	if (stmt->CatchClause != nullptr) {
		currentFlow = finishFlowLabel(exceptionLabel);
		exceptionLabel = createBranchLabel();
		addAntecedent(exceptionLabel, currentFlow);
		currentExceptionTarget = exceptionLabel;
		bind(stmt->CatchClause);
		addAntecedent(normalExitLabel, currentFlow);
	}
	currentReturnTarget = saveReturnTarget;
	currentExceptionTarget = saveExceptionTarget;
	if (stmt->FinallyBlock != nullptr) {
		FlowLabel* finallyLabel = createBranchLabel();
		finallyLabel->antecedents = combineFlowLists(
			normalExitLabel->antecedents,
			combineFlowLists(exceptionLabel->antecedents,
			                 returnLabel->antecedents));
		currentFlow = finallyLabel;
		bind(stmt->FinallyBlock);
		if (currentFlow->flags & FlowFlagsUnreachable) {
			currentFlow = unreachableFlow;
		} else {
			if (currentReturnTarget != nullptr &&
			    returnLabel->antecedents != nullptr) {
				addAntecedent(currentReturnTarget,
				              createReduceLabel(finallyLabel,
				                                returnLabel->antecedents,
				                                currentFlow));
			}
			if (currentExceptionTarget != nullptr &&
			    exceptionLabel->antecedents != nullptr) {
				addAntecedent(currentExceptionTarget,
				              createReduceLabel(finallyLabel,
				                                exceptionLabel->antecedents,
				                                currentFlow));
			}
			if (normalExitLabel->antecedents != nullptr) {
				currentFlow = createReduceLabel(finallyLabel,
				                                normalExitLabel->antecedents,
				                                currentFlow);
			} else {
				currentFlow = unreachableFlow;
			}
		}
	} else {
		currentFlow = finishFlowLabel(normalExitLabel);
	}
}

void Binder::bindSwitchStatement(Node* node) {
	auto* stmt = node->as<SwitchStatement>();
	FlowLabel* postSwitchLabel = createBranchLabel();
	bind(stmt->Expression);
	FlowLabel* saveBreakTarget = currentBreakTarget;
	FlowNode* savePreSwitchCaseFlow = preSwitchCaseFlow;
	currentBreakTarget = postSwitchLabel;
	preSwitchCaseFlow = currentFlow;
	bind(stmt->CaseBlock);
	addAntecedent(postSwitchLabel, currentFlow);
	bool hasDefault = false;
	for (Node* c : stmt->CaseBlock->as<CaseBlock>()->Clauses->nodes) {
		if (c->kind == Kind::DefaultClause) {
			hasDefault = true;
			break;
		}
	}
	if (!hasDefault) {
		addAntecedent(postSwitchLabel,
		              createFlowSwitchClause(preSwitchCaseFlow, node, 0, 0));
	}
	currentBreakTarget = saveBreakTarget;
	preSwitchCaseFlow = savePreSwitchCaseFlow;
	currentFlow = finishFlowLabel(postSwitchLabel);
}

void Binder::bindCaseBlock(Node* node) {
	Node* switchStatement = node->parent;
	auto& clauses = node->as<CaseBlock>()->Clauses->nodes;
	bool isNarrowingSwitch =
		switchStatement->expression()->kind == Kind::TrueKeyword ||
		isNarrowingExpression(switchStatement->expression());
	FlowNode* fallthroughFlow = unreachableFlow;
	for (size_t i = 0; i < clauses.size(); i++) {
		size_t clauseStart = i;
		while (clauses[i]->statements().empty() && i + 1 < clauses.size()) {
			if (fallthroughFlow == unreachableFlow) {
				currentFlow = preSwitchCaseFlow;
			}
			bind(clauses[i]);
			i++;
		}
		FlowLabel* preCaseLabel = createBranchLabel();
		FlowNode* preCaseFlow = preSwitchCaseFlow;
		if (isNarrowingSwitch) {
			preCaseFlow = createFlowSwitchClause(preSwitchCaseFlow,
			                                     switchStatement,
			                                     (int)clauseStart, (int)i + 1);
		}
		addAntecedent(preCaseLabel, preCaseFlow);
		addAntecedent(preCaseLabel, fallthroughFlow);
		currentFlow = finishFlowLabel(preCaseLabel);
		Node* clause = clauses[i];
		bind(clause);
		fallthroughFlow = currentFlow;
		if (!(currentFlow->flags & FlowFlagsUnreachable) &&
		    i != clauses.size() - 1) {
			clause->as<CaseOrDefaultClause>()->FallthroughFlowNode =
				currentFlow;
		}
	}
}

void Binder::bindCaseOrDefaultClause(Node* node) {
	auto* clause = node->as<CaseOrDefaultClause>();
	if (clause->Expression != nullptr) {
		FlowNode* saveCurrentFlow = currentFlow;
		currentFlow = preSwitchCaseFlow;
		bind(clause->Expression);
		currentFlow = saveCurrentFlow;
	}
	bindEach(clause->Statements->nodes);
}

void Binder::bindExpressionStatement(Node* node) {
	auto* stmt = node->as<ExpressionStatement>();
	bind(stmt->Expression);
	maybeBindExpressionFlowIfCall(stmt->Expression);
}

void Binder::maybeBindExpressionFlowIfCall(Node* node) {
	if (isCallExpression(node)) {
		if (node->expression()->kind != Kind::SuperKeyword &&
		    isDottedName(node->expression())) {
			currentFlow = createFlowCall(currentFlow, node);
		}
	}
}

void Binder::bindLabeledStatement(Node* node) {
	auto* stmt = node->as<LabeledStatement>();
	FlowLabel* postStatementLabel = createBranchLabel();
	ActiveLabel label;
	label.next = activeLabelList;
	label.name = stmt->Label->text();
	label.breakTarget = postStatementLabel;
	label.continueTarget = nullptr;
	label.referenced = false;
	activeLabelList = &label;
	bind(stmt->Label);
	bind(stmt->Statement);
	if (!label.referenced) {
		stmt->Label->flags |= NodeFlagsUnreachable;
	}
	activeLabelList = label.next;
	addAntecedent(postStatementLabel, currentFlow);
	currentFlow = finishFlowLabel(postStatementLabel);
}

void Binder::bindPrefixUnaryExpressionFlow(Node* node) {
	auto* expr = node->as<PrefixUnaryExpression>();
	if (expr->Operator == Kind::ExclamationToken) {
		FlowLabel* saveTrueTarget = currentTrueTarget;
		currentTrueTarget = currentFalseTarget;
		currentFalseTarget = saveTrueTarget;
		bindEachChild(node);
		currentFalseTarget = currentTrueTarget;
		currentTrueTarget = saveTrueTarget;
	} else {
		bindEachChild(node);
		if (expr->Operator == Kind::PlusPlusToken ||
		    expr->Operator == Kind::MinusMinusToken) {
			bindAssignmentTargetFlow(expr->Operand);
		}
	}
}

void Binder::bindPostfixUnaryExpressionFlow(Node* node) {
	auto* expr = node->as<PostfixUnaryExpression>();
	bindEachChild(node);
	if (expr->Operator == Kind::PlusPlusToken ||
	    expr->Operator == Kind::MinusMinusToken) {
		bindAssignmentTargetFlow(expr->Operand);
	}
}

void Binder::bindDestructuringAssignmentFlow(Node* node) {
	auto* expr = node->as<BinaryExpression>();
	if (inAssignmentPattern) {
		inAssignmentPattern = false;
		bind(expr->OperatorToken);
		bind(expr->Right);
		inAssignmentPattern = true;
		bind(expr->Left);
		bind(expr->Type);
	} else {
		inAssignmentPattern = true;
		bind(expr->Left);
		bind(expr->Type);
		inAssignmentPattern = false;
		bind(expr->OperatorToken);
		bind(expr->Right);
	}
	bindAssignmentTargetFlow(expr->Left);
}

void Binder::bindBinaryExpressionFlow(Node* node) {
	auto* expr = node->as<BinaryExpression>();
	Kind operator_ = expr->OperatorToken->kind;
	if (isLogicalOrCoalescingBinaryOperator(operator_) ||
	    isLogicalOrCoalescingAssignmentOperator(operator_)) {
		if (isTopLevelLogicalExpression(node)) {
			FlowLabel* postExpressionLabel = createBranchLabel();
			FlowNode* saveCurrentFlow = currentFlow;
			bool saveHasFlowEffects = hasFlowEffects;
			hasFlowEffects = false;
			bindLogicalLikeExpression(node, postExpressionLabel,
			                          postExpressionLabel);
			if (hasFlowEffects) {
				currentFlow = finishFlowLabel(postExpressionLabel);
			} else {
				currentFlow = saveCurrentFlow;
			}
			hasFlowEffects = hasFlowEffects || saveHasFlowEffects;
		} else {
			bindLogicalLikeExpression(node, currentTrueTarget,
			                          currentFalseTarget);
		}
	} else {
		bind(expr->Left);
		bind(expr->Type);
		if (operator_ == Kind::CommaToken) {
			maybeBindExpressionFlowIfCall(expr->Left);
		}
		bind(expr->OperatorToken);
		bind(expr->Right);
		if (operator_ == Kind::CommaToken) {
			maybeBindExpressionFlowIfCall(expr->Right);
		}
		if (isAssignmentOperator(operator_) && !isAssignmentTarget(node)) {
			bindAssignmentTargetFlow(expr->Left);
			if (operator_ == Kind::EqualsToken &&
			    expr->Left->kind == Kind::ElementAccessExpression) {
				auto* elementAccess =
					expr->Left->as<ElementAccessExpression>();
				if (isNarrowableOperand(elementAccess->Expression)) {
					currentFlow =
						createFlowMutation(FlowFlagsArrayMutation, currentFlow,
				                           node);
				}
			}
		}
	}
}

void Binder::bindLogicalLikeExpression(Node* node, FlowLabel* trueTarget,
                                       FlowLabel* falseTarget) {
	auto* expr = node->as<BinaryExpression>();
	FlowLabel* preRightLabel = createBranchLabel();
	if (expr->OperatorToken->kind == Kind::AmpersandAmpersandToken ||
	    expr->OperatorToken->kind == Kind::AmpersandAmpersandEqualsToken) {
		bindCondition(expr->Left, preRightLabel, falseTarget);
	} else {
		bindCondition(expr->Left, trueTarget, preRightLabel);
	}
	currentFlow = finishFlowLabel(preRightLabel);
	bind(expr->OperatorToken);
	if (isLogicalOrCoalescingAssignmentOperator(expr->OperatorToken->kind)) {
		doWithConditionalBranches([this](Node* n) { bind(n); }, expr->Right,
		                          trueTarget, falseTarget);
		bindAssignmentTargetFlow(expr->Left);
		addAntecedent(trueTarget,
		              createFlowCondition(FlowFlagsTrueCondition, currentFlow,
		                                  node));
		addAntecedent(falseTarget,
		              createFlowCondition(FlowFlagsFalseCondition, currentFlow,
		                                  node));
	} else {
		bindCondition(expr->Right, trueTarget, falseTarget);
	}
}

void Binder::bindDeleteExpressionFlow(Node* node) {
	auto* expr = node->as<DeleteExpression>();
	bindEachChild(node);
	if (expr->Expression->kind == Kind::PropertyAccessExpression) {
		bindAssignmentTargetFlow(expr->Expression);
	}
}

void Binder::bindConditionalExpressionFlow(Node* node) {
	auto* expr = node->as<ConditionalExpression>();
	FlowLabel* trueLabel = createBranchLabel();
	FlowLabel* falseLabel = createBranchLabel();
	FlowLabel* postExpressionLabel = createBranchLabel();
	FlowNode* saveCurrentFlow = currentFlow;
	bool saveHasFlowEffects = hasFlowEffects;
	hasFlowEffects = false;
	bindCondition(expr->Condition, trueLabel, falseLabel);
	currentFlow = finishFlowLabel(trueLabel);
	bind(expr->QuestionToken);
	bind(expr->WhenTrue);
	addAntecedent(postExpressionLabel, currentFlow);
	currentFlow = finishFlowLabel(falseLabel);
	bind(expr->ColonToken);
	bind(expr->WhenFalse);
	addAntecedent(postExpressionLabel, currentFlow);
	if (hasFlowEffects) {
		currentFlow = finishFlowLabel(postExpressionLabel);
	} else {
		currentFlow = saveCurrentFlow;
	}
	hasFlowEffects = hasFlowEffects || saveHasFlowEffects;
}

void Binder::bindVariableDeclarationFlow(Node* node) {
	bindEachChild(node);
	if (node->initializer() != nullptr ||
	    isForInOrOfStatement(node->parent->parent)) {
		bindInitializedVariableFlow(node);
	}
}

void Binder::bindInitializedVariableFlow(Node* node) {
	Node* name = nullptr;
	switch (node->kind) {
	case Kind::VariableDeclaration:
		name = node->as<VariableDeclaration>()->name;
		break;
	case Kind::BindingElement:
		name = node->as<BindingElement>()->name;
		break;
	default:
		break;
	}
	if (name != nullptr && isBindingPattern(name)) {
		for (Node* child : name->elements()) {
			bindInitializedVariableFlow(child);
		}
	} else {
		currentFlow = createFlowMutation(FlowFlagsAssignment, currentFlow, node);
	}
}

void Binder::bindAccessExpressionFlow(Node* node) {
	if (isOptionalChain(node)) {
		bindOptionalChainFlow(node);
	} else {
		bindEachChild(node);
	}
}

void Binder::bindOptionalChainFlow(Node* node) {
	if (isTopLevelLogicalExpression(node)) {
		FlowLabel* postExpressionLabel = createBranchLabel();
		FlowNode* saveCurrentFlow = currentFlow;
		bool saveHasFlowEffects = hasFlowEffects;
		bindOptionalChain(node, postExpressionLabel, postExpressionLabel);
		if (hasFlowEffects) {
			currentFlow = finishFlowLabel(postExpressionLabel);
		} else {
			currentFlow = saveCurrentFlow;
		}
		hasFlowEffects = hasFlowEffects || saveHasFlowEffects;
	} else {
		bindOptionalChain(node, currentTrueTarget, currentFalseTarget);
	}
}

void Binder::bindOptionalChain(Node* node, FlowLabel* trueTarget,
                               FlowLabel* falseTarget) {
	FlowLabel* preChainLabel = nullptr;
	if (isOptionalChainRoot(node)) {
		preChainLabel = createBranchLabel();
	}
	bindOptionalExpression(node->expression(),
	                       preChainLabel ? preChainLabel : trueTarget,
	                       falseTarget);
	if (preChainLabel != nullptr) {
		currentFlow = finishFlowLabel(preChainLabel);
	}
	doWithConditionalBranches(
		[this](Node* n) { bindOptionalChainRest(n); }, node, trueTarget,
		falseTarget);
	if (isOutermostOptionalChain(node)) {
		addAntecedent(trueTarget,
		              createFlowCondition(FlowFlagsTrueCondition, currentFlow,
		                                  node));
		addAntecedent(falseTarget,
		              createFlowCondition(FlowFlagsFalseCondition, currentFlow,
		                                  node));
	}
}

void Binder::bindOptionalExpression(Node* node, FlowLabel* trueTarget,
                                    FlowLabel* falseTarget) {
	doWithConditionalBranches([this](Node* n) { bind(n); }, node, trueTarget,
	                          falseTarget);
	if (!isOptionalChain(node) || isOutermostOptionalChain(node)) {
		addAntecedent(trueTarget,
		              createFlowCondition(FlowFlagsTrueCondition, currentFlow,
		                                  node));
		addAntecedent(falseTarget,
		              createFlowCondition(FlowFlagsFalseCondition, currentFlow,
		                                  node));
	}
}

bool Binder::bindOptionalChainRest(Node* node) {
	switch (node->kind) {
	case Kind::PropertyAccessExpression:
		bind(node->questionDotToken());
		bind(node->name());
		break;
	case Kind::ElementAccessExpression:
		bind(node->questionDotToken());
		bind(node->as<ElementAccessExpression>()->ArgumentExpression);
		break;
	case Kind::CallExpression:
		bind(node->questionDotToken());
		bindNodeList(node->typeArgumentList());
		bindEach(node->arguments());
		break;
	default:
		break;
	}
	return false;
}

void Binder::bindCallExpressionFlow(Node* node) {
	auto* call = node->as<CallExpression>();
	if (isOptionalChain(node)) {
		bindOptionalChainFlow(node);
	} else {
		Node* expr = skipParentheses(call->Expression);
		if (expr->kind == Kind::FunctionExpression ||
		    expr->kind == Kind::ArrowFunction) {
			bindNodeList(call->TypeArguments);
			bindEach(call->Arguments->nodes);
			bind(call->Expression);
		} else {
			bindEachChild(node);
			if (call->Expression->kind == Kind::SuperKeyword) {
				currentFlow = createFlowCall(currentFlow, node);
			}
		}
	}
	if (isPropertyAccessExpression(call->Expression)) {
		auto* access = call->Expression->as<PropertyAccessExpression>();
		if (isIdentifier(access->name) &&
		    isNarrowableOperand(access->Expression) &&
		    isPushOrUnshiftIdentifier(access->name)) {
			currentFlow = createFlowMutation(FlowFlagsArrayMutation,
			                                 currentFlow, node);
		}
	}
}

void Binder::bindNonNullExpressionFlow(Node* node) {
	if (isOptionalChain(node)) {
		bindOptionalChainFlow(node);
	} else {
		bindEachChild(node);
	}
}

void Binder::bindBindingElementFlow(Node* node) {
	auto* elem = node->as<BindingElement>();
	bind(elem->DotDotDotToken);
	bind(elem->PropertyName);
	bindInitializer(elem->Initializer);
	bind(elem->name);
}

void Binder::bindParameterFlow(Node* node) {
	auto* param = node->as<ParameterDeclaration>();
	bindModifiers(param->modifiers);
	bind(param->DotDotDotToken);
	bind(param->QuestionToken);
	bind(param->Type);
	bindInitializer(param->Initializer);
	bind(param->name);
}

void Binder::bindInitializer(Node* node) {
	if (node == nullptr) return;
	FlowNode* entryFlow = currentFlow;
	bind(node);
	if (entryFlow == unreachableFlow || entryFlow == currentFlow) return;
	FlowLabel* exitFlow = createBranchLabel();
	addAntecedent(exitFlow, entryFlow);
	addAntecedent(exitFlow, currentFlow);
	currentFlow = finishFlowLabel(exitFlow);
}

void Binder::addToContainerChain(Node* next) {
	if (lastContainer != nullptr) {
		auto data = lastContainer->localsContainerData();
		if (data.nextContainer) *data.nextContainer = next;
	}
	lastContainer = next;
}

void Binder::addDeclarationToSymbol(Symbol* symbol, Node* node,
                                    SymbolFlags symbolFlags) {
	symbol->flags |= symbolFlags;
	auto data = node->declarationData();
	if (data.symbol) *data.symbol = symbol;
	if (symbol->declarations.empty()) {
		symbol->declarations.push_back(node);
	} else {
		appendIfUnique(symbol->declarations, node);
	}
	if (symbol->flags & SymbolFlagsConstEnumOnlyModule &&
	    symbol->flags &
	        (SymbolFlagsFunction | SymbolFlagsClass |
	         SymbolFlagsRegularEnum)) {
		symbol->flags &= ~SymbolFlagsConstEnumOnlyModule;
		notConstEnumOnlyModules.insert(symbol);
	}
	if (symbolFlags & SymbolFlagsValue) {
		setValueDeclaration(symbol, node);
	}
}

static bool isAssignmentDeclaration(Node* decl) {
	return isBinaryExpression(decl) || isAccessExpression(decl) ||
	       isIdentifier(decl) || isCallExpression(decl);
}

static bool isEffectiveModuleDeclaration(Node* node) {
	return isModuleDeclaration(node) || isIdentifier(node);
}

void setValueDeclaration(Symbol* symbol, Node* node) {
	Node* valueDeclaration = symbol->valueDeclaration;
	if (valueDeclaration == nullptr ||
	    (isAssignmentDeclaration(valueDeclaration) &&
	     !isAssignmentDeclaration(node)) ||
	    (valueDeclaration->kind != node->kind &&
	     isEffectiveModuleDeclaration(valueDeclaration))) {
		symbol->valueDeclaration = node;
	}
}

ContainerFlags getContainerFlags(Node* node) {
	switch (node->kind) {
	case Kind::ClassExpression:
	case Kind::ClassDeclaration:
	case Kind::EnumDeclaration:
	case Kind::ObjectLiteralExpression:
	case Kind::TypeLiteral:
	case Kind::JsxAttributes:
		return ContainerFlagsIsContainer;
	case Kind::InterfaceDeclaration:
		return ContainerFlagsIsContainer | ContainerFlagsIsInterface;
	case Kind::ModuleDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::MappedType:
	case Kind::IndexSignature:
		return ContainerFlagsIsContainer | ContainerFlagsHasLocals;
	case Kind::SourceFile:
		return ContainerFlagsIsContainer |
		       ContainerFlagsIsControlFlowContainer | ContainerFlagsHasLocals;
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::MethodDeclaration:
		if (isObjectLiteralOrClassExpressionMethodOrAccessor(node)) {
			return ContainerFlagsIsContainer |
			       ContainerFlagsIsControlFlowContainer |
			       ContainerFlagsHasLocals | ContainerFlagsIsFunctionLike |
			       ContainerFlagsIsObjectLiteralOrClassExpressionMethodOrAccessor |
			       ContainerFlagsIsThisContainer;
		}
		[[fallthrough]];
	case Kind::Constructor:
	case Kind::FunctionDeclaration:
	case Kind::ClassStaticBlockDeclaration:
		return ContainerFlagsIsContainer |
		       ContainerFlagsIsControlFlowContainer |
		       ContainerFlagsHasLocals | ContainerFlagsIsFunctionLike |
		       ContainerFlagsIsThisContainer;
	case Kind::MethodSignature:
	case Kind::CallSignature:
	case Kind::FunctionType:
	case Kind::ConstructSignature:
	case Kind::ConstructorType:
		return ContainerFlagsIsContainer |
		       ContainerFlagsIsControlFlowContainer |
		       ContainerFlagsHasLocals | ContainerFlagsIsFunctionLike |
		       ContainerFlagsPropagatesThisKeyword;
	case Kind::FunctionExpression:
		return ContainerFlagsIsContainer |
		       ContainerFlagsIsControlFlowContainer |
		       ContainerFlagsHasLocals | ContainerFlagsIsFunctionLike |
		       ContainerFlagsIsFunctionExpression |
		       ContainerFlagsIsThisContainer;
	case Kind::ArrowFunction:
		return ContainerFlagsIsContainer |
		       ContainerFlagsIsControlFlowContainer |
		       ContainerFlagsHasLocals | ContainerFlagsIsFunctionLike |
		       ContainerFlagsIsFunctionExpression |
		       ContainerFlagsPropagatesThisKeyword;
	case Kind::ModuleBlock:
		return ContainerFlagsIsControlFlowContainer;
	case Kind::PropertyDeclaration:
		if (node->initializer() != nullptr) {
			return ContainerFlagsIsControlFlowContainer |
			       ContainerFlagsIsThisContainer;
		}
		return ContainerFlagsNone;
	case Kind::CatchClause:
	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::CaseBlock:
		return ContainerFlagsIsBlockScopedContainer | ContainerFlagsHasLocals;
	case Kind::Block:
		if (isFunctionLike(node->parent) ||
		    isClassStaticBlockDeclaration(node->parent)) {
			return ContainerFlagsNone;
		}
		return ContainerFlagsIsBlockScopedContainer | ContainerFlagsHasLocals;
	default:
		break;
	}
	return ContainerFlagsNone;
}

// --- narrowing predicates ---

static bool isNarrowingExpression(Node* expr) {
	switch (expr->kind) {
	case Kind::Identifier:
	case Kind::ThisKeyword:
		return true;
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
		return containsNarrowableReference(expr);
	case Kind::CallExpression:
		return hasNarrowableArgument(expr);
	case Kind::ParenthesizedExpression:
	case Kind::NonNullExpression:
	case Kind::TypeOfExpression:
		return isNarrowingExpression(expr->expression());
	case Kind::BinaryExpression:
		return isNarrowingBinaryExpression(expr->as<BinaryExpression>());
	case Kind::PrefixUnaryExpression:
		return expr->as<PrefixUnaryExpression>()->Operator ==
		           Kind::ExclamationToken &&
		       isNarrowingExpression(expr->as<PrefixUnaryExpression>()->Operand);
	default:
		break;
	}
	return false;
}

static bool containsNarrowableReference(Node* expr) {
	if (isNarrowableReference(expr)) return true;
	if (expr->flags & NodeFlagsOptionalChain) {
		switch (expr->kind) {
		case Kind::PropertyAccessExpression:
		case Kind::ElementAccessExpression:
		case Kind::CallExpression:
		case Kind::NonNullExpression:
			return containsNarrowableReference(expr->expression());
		default:
			break;
		}
	}
	return false;
}

static bool isNarrowableReference(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
	case Kind::ThisKeyword:
	case Kind::SuperKeyword:
	case Kind::MetaProperty:
		return true;
	case Kind::PropertyAccessExpression:
	case Kind::ParenthesizedExpression:
	case Kind::NonNullExpression:
		return isNarrowableReference(node->expression());
	case Kind::ElementAccessExpression: {
		auto* expr = node->as<ElementAccessExpression>();
		return isStringOrNumericLiteralLike(expr->ArgumentExpression) ||
		       (isEntityNameExpression(expr->ArgumentExpression) &&
		        isNarrowableReference(expr->Expression));
	}
	case Kind::BinaryExpression: {
		auto* expr = node->as<BinaryExpression>();
		return (expr->OperatorToken->kind == Kind::CommaToken &&
		        isNarrowableReference(expr->Right)) ||
		       (isAssignmentOperator(expr->OperatorToken->kind) &&
		        isLeftHandSideExpression(expr->Left));
	}
	default:
		break;
	}
	return false;
}

static bool hasNarrowableArgument(Node* expr) {
	auto* call = expr->as<CallExpression>();
	for (Node* argument : call->Arguments->nodes) {
		if (containsNarrowableReference(argument)) return true;
	}
	if (isPropertyAccessExpression(call->Expression)) {
		if (containsNarrowableReference(call->Expression->expression()))
			return true;
	}
	return false;
}

static bool isNarrowingBinaryExpression(BinaryExpression* expr) {
	switch (expr->OperatorToken->kind) {
	case Kind::EqualsToken:
	case Kind::BarBarEqualsToken:
	case Kind::AmpersandAmpersandEqualsToken:
	case Kind::QuestionQuestionEqualsToken:
		return containsNarrowableReference(expr->Left);
	case Kind::EqualsEqualsToken:
	case Kind::ExclamationEqualsToken:
	case Kind::EqualsEqualsEqualsToken:
	case Kind::ExclamationEqualsEqualsToken: {
		Node* left = skipParentheses(expr->Left);
		Node* right = skipParentheses(expr->Right);
		return isNarrowableOperand(left) || isNarrowableOperand(right) ||
		       isNarrowingTypeOfOperands(right, left) ||
		       isNarrowingTypeOfOperands(left, right) ||
		       (isBooleanLiteral(right) && isNarrowingExpression(left) ||
		        isBooleanLiteral(left) && isNarrowingExpression(right));
	}
	case Kind::InstanceOfKeyword:
		return isNarrowableOperand(expr->Left);
	case Kind::InKeyword:
		return isNarrowingExpression(expr->Right);
	case Kind::CommaToken:
		return isNarrowingExpression(expr->Right);
	default:
		break;
	}
	return false;
}

static bool isNarrowableOperand(Node* expr) {
	switch (expr->kind) {
	case Kind::ParenthesizedExpression:
		return isNarrowableOperand(expr->expression());
	case Kind::BinaryExpression: {
		auto* binary = expr->as<BinaryExpression>();
		switch (binary->OperatorToken->kind) {
		case Kind::EqualsToken:
			return isNarrowableOperand(binary->Left);
		case Kind::CommaToken:
			return isNarrowableOperand(binary->Right);
		default:
			break;
		}
		break;
	}
	default:
		break;
	}
	return containsNarrowableReference(expr);
}

static bool isNarrowingTypeOfOperands(Node* expr1, Node* expr2) {
	return isTypeOfExpression(expr1) &&
	       isNarrowableOperand(expr1->expression()) &&
	       isStringLiteralLike(expr2);
}

// --- error helpers ---

void Binder::errorOnNode(Node* node, const DiagnosticMessage* message,
                         const std::vector<std::string>& args) {
	addDiagnostic(createDiagnosticForNode(node, message, args));
}

void Binder::errorOnFirstToken(Node* node, const DiagnosticMessage* message,
                               const std::vector<std::string>& args) {
	TextRange span = getRangeOfTokenAtPosition(file, node->loc.pos());
	addDiagnostic(newDiagnostic(file, span, message, args));
}

Diagnostic* Binder::createDiagnosticForNode(
	Node* node, const DiagnosticMessage* message,
	const std::vector<std::string>& args) {
	return newDiagnostic(file, getErrorRangeForNode(file, node), message, args);
}

static bool isStatementCondition(Node* node) {
	switch (node->parent->kind) {
	case Kind::IfStatement:
	case Kind::WhileStatement:
	case Kind::DoStatement:
		return node->parent->expression() == node;
	case Kind::ForStatement:
		return node->parent->as<ForStatement>()->Condition == node;
	case Kind::ConditionalExpression:
		return node->parent->as<ConditionalExpression>()->Condition == node;
	default:
		break;
	}
	return false;
}

static bool isTopLevelLogicalExpression(Node* node) {
	while (isParenthesizedExpression(node->parent) ||
	       (isPrefixUnaryExpression(node->parent) &&
	        node->parent->as<PrefixUnaryExpression>()->Operator ==
	            Kind::ExclamationToken)) {
		node = node->parent;
	}
	return !isStatementCondition(node) &&
	       !isLogicalExpression(node->parent) &&
	       !(isOptionalChain(node->parent) &&
	         node->parent->expression() == node);
}

} // namespace tsc
