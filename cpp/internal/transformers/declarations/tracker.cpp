// Port of tsc/internal/transformers/declarations/tracker.go
#include "internal/transformers/declarations/declarations.h"

namespace tsc::transformers::declarations {

// PopErrorFallbackNode implements checker.SymbolTracker.
void SymbolTrackerImpl::PopErrorFallbackNode() {
	fallbackStack.pop_back();
}

// PushErrorFallbackNode implements checker.SymbolTracker.
void SymbolTrackerImpl::PushErrorFallbackNode(Node* node) {
	fallbackStack.push_back(node);
}

// ReportCyclicStructureError implements checker.SymbolTracker.
void SymbolTrackerImpl::ReportCyclicStructureError() {
	Node* location = errorLocation();
	if (location != nullptr) {
		state->addDiagnostic(createDiagnosticForNode(
			location,
			The_inferred_type_of_0_references_a_type_with_a_cyclic_structure_which_cannot_be_trivially_serialized_A_type_annotation_is_necessary,
			{errorDeclarationNameWithFallback()}));
	}
}

// ReportInaccessibleThisError implements checker.SymbolTracker.
void SymbolTrackerImpl::ReportInaccessibleThisError() {
	Node* location = errorLocation();
	if (location != nullptr) {
		state->addDiagnostic(createDiagnosticForNode(
			location,
			The_inferred_type_of_0_references_an_inaccessible_1_type_A_type_annotation_is_necessary,
			{errorDeclarationNameWithFallback(), "this"}));
	}
}

// ReportInaccessibleUniqueSymbolError implements checker.SymbolTracker.
void SymbolTrackerImpl::ReportInaccessibleUniqueSymbolError() {
	Node* location = errorLocation();
	if (location != nullptr) {
		state->addDiagnostic(createDiagnosticForNode(
			location,
			The_inferred_type_of_0_references_an_inaccessible_1_type_A_type_annotation_is_necessary,
			{errorDeclarationNameWithFallback(), "unique symbol"}));
	}
}

bool SymbolTrackerImpl::isBoundExpando(Node* node) {
	if (!(isExpandoPropertyDeclaration(node) &&
	      isPropertyAccessExpression(
	          node->as<BinaryExpression>()->Left))) {
		return false;
	}
	// Match transformExpandoAssignment: only an assignment rooted at an identifier (`f.x = ...`) can bind an expando
	// property; `this.x = ...`, `super.x = ...`, `f().x = ...` and the like have no referenced declaration.
	Node* ns = getLeftmostAccessExpression(
	    node->as<BinaryExpression>()->Left);
	if (!isIdentifier(ns)) {
		return false;
	}
	Node* ref = resolver->GetReferencedValueDeclarationUnsafe(ns);
	if (ref == nullptr) {
		return false;
	}
	return resolver->IsExpandoFunctionDeclarationUnsafe(ref);
}

bool SymbolTrackerImpl::isChildOfBoundExpando(Node* node) {
	return findAncestorOrQuit(node, [this](Node* n) -> FindAncestorResult {
		if (isSourceFile(n) || isBlock(n)) {
			return FindAncestorResult::Quit;
		}
		return toFindAncestorResult(isBoundExpando(n));
	}) != nullptr;
}

// ReportInferenceFallback implements checker.SymbolTracker.
void SymbolTrackerImpl::ReportInferenceFallback(Node* node) {
	if (!state->isolatedDeclarations) {
		return;
	}
	if (getSourceFileOfNode(node) != state->currentSourceFile) {
		return;  // Nested error on a declaration in another file - ignore, will be reemitted if file is in the output file set
	}
	if (state->resolver->IsExpandoFunctionDeclarationUnsafe(
	        node)) {  // within a node builder call that should already lock the checker, use the unsafe call
		state->reportExpandoFunctionErrors(node);
	}
	if (!isChildOfBoundExpando(
	        node)) {  // expando props get an error when their host is visited by the above, this prevents a follow-on error on a non-inferrable expression
		state->addDiagnostic(getIsolatedDeclarationError(node));
	}
}

// ReportLikelyUnsafeImportRequiredError implements checker.SymbolTracker.
void SymbolTrackerImpl::ReportLikelyUnsafeImportRequiredError(
	const std::string& specifier, const std::string& symbolName) {
	Node* location = errorLocation();
	if (location != nullptr) {
		if (!symbolName.empty()) {
			state->addDiagnostic(createDiagnosticForNode(
				location,
				The_inferred_type_of_0_cannot_be_named_without_a_reference_to_2_from_1_This_is_likely_not_portable_A_type_annotation_is_necessary,
				{errorDeclarationNameWithFallback(), specifier,
				 symbolName}));
		} else {
			state->addDiagnostic(createDiagnosticForNode(
				location,
				The_inferred_type_of_0_cannot_be_named_without_a_reference_to_1_This_is_likely_not_portable_A_type_annotation_is_necessary,
				{errorDeclarationNameWithFallback(), specifier}));
		}
	}
}

// ReportNonSerializableProperty implements checker.SymbolTracker.
void SymbolTrackerImpl::ReportNonSerializableProperty(
	const std::string& propertyName) {
	Node* location = errorLocation();
	if (location != nullptr) {
		state->addDiagnostic(createDiagnosticForNode(
			location,
			The_type_of_this_node_cannot_be_serialized_because_its_property_0_cannot_be_serialized,
			{propertyName}));
	}
}

// ReportNonlocalAugmentation implements checker.SymbolTracker.
void SymbolTrackerImpl::ReportNonlocalAugmentation(
	SourceFile* containingFile, Symbol* parentSymbol,
	Symbol* augmentingSymbol) {
	// core.Find
	Node* primaryDeclaration = nullptr;
	for (Node* d : parentSymbol->data->declarations) {
		if (getSourceFileOfNode(d) == containingFile) {
			primaryDeclaration = d;
			break;
		}
	}
	// core.Filter
	std::vector<Node*> augmentingDeclarations;
	for (Node* d : augmentingSymbol->data->declarations) {
		if (getSourceFileOfNode(d) != containingFile) {
			augmentingDeclarations.push_back(d);
		}
	}
	if (primaryDeclaration != nullptr && !augmentingDeclarations.empty()) {
		for (Node* augmentations : augmentingDeclarations) {
			Diagnostic* diag = createDiagnosticForNode(
				augmentations,
				Declaration_augments_declaration_in_another_file_This_cannot_be_serialized);
			Diagnostic* related = createDiagnosticForNode(
				primaryDeclaration,
				This_is_the_declaration_being_augmented_Consider_moving_the_augmenting_declaration_into_the_same_file);
			diag->AddRelatedInfo(related);
			state->addDiagnostic(diag);
		}
	}
}

// ReportPrivateInBaseOfClassExpression implements checker.SymbolTracker.
void SymbolTrackerImpl::ReportPrivateInBaseOfClassExpression(
	const std::string& propertyName) {
	Node* location = errorLocation();
	if (location != nullptr) {
		Diagnostic* diag = createDiagnosticForNode(
			location,
			Property_0_of_exported_anonymous_class_type_may_not_be_private_or_protected,
			{propertyName});
		if (isVariableDeclaration(location->parent)) {
			Diagnostic* related = createDiagnosticForNode(
				location, Add_a_type_annotation_to_the_variable_0,
				{errorDeclarationNameWithFallback()});
			diag->AddRelatedInfo(related);
		}
		state->addDiagnostic(diag);
	}
}

// ReportTruncationError implements checker.SymbolTracker.
void SymbolTrackerImpl::ReportTruncationError() {
	Node* location = errorLocation();
	if (location != nullptr) {
		state->addDiagnostic(createDiagnosticForNode(
			location,
			The_inferred_type_of_this_node_exceeds_the_maximum_length_the_compiler_will_serialize_An_explicit_type_annotation_is_needed));
	}
}

Node* SymbolTrackerImpl::errorFallbackNode() {
	if (!fallbackStack.empty()) {
		return fallbackStack.back();
	}
	return nullptr;
}

Node* SymbolTrackerImpl::errorLocation() {
	Node* location = state->errorNameNode;
	if (location == nullptr) {
		location = errorFallbackNode();
	}
	return location;
}

std::string SymbolTrackerImpl::errorDeclarationNameWithFallback() {
	if (state->errorNameNode != nullptr) {
		return declarationNameToString(state->errorNameNode);
	}
	if (errorFallbackNode() != nullptr &&
	    getNameOfDeclaration(errorFallbackNode()) != nullptr) {
		return declarationNameToString(
		    getNameOfDeclaration(errorFallbackNode()));
	}
	if (errorFallbackNode() != nullptr &&
	    isExportAssignment(errorFallbackNode())) {
		if (errorFallbackNode()->as<ExportAssignment>()->IsExportEquals) {
			return "export=";
		}
		return "default";
	}
	return "(Missing)";  // same fallback declarationNameToString uses when node is zero-width (ie, nameless)
}

// TrackSymbol implements checker.SymbolTracker.
bool SymbolTrackerImpl::TrackSymbol(Symbol* symbol,
                                    Node* enclosingDeclaration,
                                    SymbolFlags meaning) {
	if ((symbol->flags & SymbolFlagsTypeParameter) != 0) {
		return false;
	}
	// When watching for a class expression symbol, record its usage without
	// reporting accessibility errors — the caller will handle visibility by
	// wrapping the class in a namespace.
	if (watchedClassSymbol != nullptr && symbol == watchedClassSymbol) {
		classSymbolTracked = true;
		return false;
	}
	printer::SymbolAccessibilityResult sar = resolver->IsSymbolAccessible(
		symbol, enclosingDeclaration, meaning,
		/*shouldComputeAliasToMarkVisible*/ true);
	bool issuedDiagnostic = handleSymbolAccessibilityError(sar);
	return issuedDiagnostic;
}

bool SymbolTrackerImpl::handleSymbolAccessibilityError(
	printer::SymbolAccessibilityResult& symbolAccessibilityResult) {
	if (symbolAccessibilityResult.Accessibility ==
	    printer::SymbolAccessibility::Accessible) {
		// Add aliases back onto the possible imports list if they're not there so we can try them again with updated visibility info
		if (!symbolAccessibilityResult.AliasesToMakeVisible.empty()) {
			for (Node* ref : symbolAccessibilityResult.AliasesToMakeVisible) {
				// core.AppendIfUnique
				if (std::find(state->lateMarkedStatements.begin(),
				              state->lateMarkedStatements.end(),
				              ref) == state->lateMarkedStatements.end()) {
					state->lateMarkedStatements.push_back(ref);
				}
			}
		}
		// TODO: Do all these accessibility checks inside/after the first pass in the checker when declarations are enabled, if possible

		// The checker should issue errors on unresolvable names, skip the declaration emit error for using a private/unreachable name for those
	} else if (symbolAccessibilityResult.Accessibility !=
	           printer::SymbolAccessibility::NotResolved) {
		// Report error
		SymbolAccessibilityDiagnostic* errorInfo =
		    state->getSymbolAccessibilityDiagnostic(symbolAccessibilityResult);
		if (errorInfo != nullptr) {
			SymbolAccessibilityDiagnostic info = *errorInfo;
			Node* diagNode = symbolAccessibilityResult.ErrorNode;
			if (diagNode == nullptr) {
				diagNode = info.errorNode;
			}
			if (info.typeName != nullptr) {
				state->addDiagnostic(createDiagnosticForNode(
					diagNode, info.diagnosticMessage,
					{getTextOfNode(info.typeName),
					 symbolAccessibilityResult.ErrorSymbolName,
					 symbolAccessibilityResult.ErrorModuleName}));
			} else {
				state->addDiagnostic(createDiagnosticForNode(
					diagNode, info.diagnosticMessage,
					{symbolAccessibilityResult.ErrorSymbolName,
					 symbolAccessibilityResult.ErrorModuleName}));
			}
			return true;
		}
	}
	return false;
}

// tracker.go:235 createDiagnosticForNode
Diagnostic* createDiagnosticForNode(Node* node,
                                    const DiagnosticMessage* message,
                                    const std::vector<std::string>& args) {
	return checker::NewDiagnosticForNode(node, message, args);
}

// tracker.go:251 SymbolTrackerSharedState::addDiagnostic
void SymbolTrackerSharedState::addDiagnostic(Diagnostic* diag) {
	diagnostics.push_back(diag);
}

// tracker.go:255 NewSymbolTracker
SymbolTrackerImpl* NewSymbolTracker(DeclarationEmitHost* host,
                                    printer::EmitResolver* resolver,
                                    SymbolTrackerSharedState* state) {
	SymbolTrackerImpl* tracker = new SymbolTrackerImpl();
	tracker->host = host;
	tracker->resolver = resolver;
	tracker->state = state;
	tracker->getIsolatedDeclarationError =
	    createGetIsolatedDeclarationErrors(resolver);
	return tracker;
}

}  // namespace tsc::transformers::declarations
