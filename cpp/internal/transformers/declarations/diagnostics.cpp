// Port of tsc/internal/transformers/declarations/diagnostics.go
#include "internal/transformers/declarations/declarations.h"

namespace tsc::transformers::declarations {

namespace {

// diagnostics.go:18 — selector signature used by the wrap*DiagnosticSelector
// helpers.
using DiagnosticSelector =
	std::function<const DiagnosticMessage*(
		Node* node, printer::SymbolAccessibilityResult&)>;

// diagnostics.go:18 wrapSimpleDiagnosticSelector
GetSymbolAccessibilityDiagnostic wrapSimpleDiagnosticSelector(
	Node* node, DiagnosticSelector selector) {
	return [node, selector](printer::SymbolAccessibilityResult&
	                            symbolAccessibilityResult)
	       -> SymbolAccessibilityDiagnostic* {
		const DiagnosticMessage* diagnosticMessage =
		    selector(node, symbolAccessibilityResult);
		if (diagnosticMessage == nullptr) {
			return nullptr;
		}
		auto* result = new SymbolAccessibilityDiagnostic();
		result->errorNode = node;
		result->diagnosticMessage = diagnosticMessage;
		result->typeName = getNameOfDeclaration(node);
		return result;
	};
}

// diagnostics.go:32 wrapNamedDiagnosticSelector
GetSymbolAccessibilityDiagnostic wrapNamedDiagnosticSelector(
	Node* node, DiagnosticSelector selector) {
	return [node, selector](printer::SymbolAccessibilityResult&
	                            symbolAccessibilityResult)
	       -> SymbolAccessibilityDiagnostic* {
		const DiagnosticMessage* diagnosticMessage =
		    selector(node, symbolAccessibilityResult);
		if (diagnosticMessage == nullptr) {
			return nullptr;
		}
		Node* name = getNameOfDeclaration(node);
		auto* result = new SymbolAccessibilityDiagnostic();
		result->errorNode = name;
		result->diagnosticMessage = diagnosticMessage;
		result->typeName = name;
		return result;
	};
}

// diagnostics.go:47 wrapFallbackErrorDiagnosticSelector
GetSymbolAccessibilityDiagnostic wrapFallbackErrorDiagnosticSelector(
	Node* node, DiagnosticSelector selector) {
	return [node, selector](printer::SymbolAccessibilityResult&
	                            symbolAccessibilityResult)
	       -> SymbolAccessibilityDiagnostic* {
		const DiagnosticMessage* diagnosticMessage =
		    selector(node, symbolAccessibilityResult);
		if (diagnosticMessage == nullptr) {
			return nullptr;
		}
		Node* errorNode = getNameOfDeclaration(node);
		if (errorNode == nullptr) {
			errorNode = node;
		}
		auto* result = new SymbolAccessibilityDiagnostic();
		result->errorNode = errorNode;
		result->diagnosticMessage = diagnosticMessage;
		return result;
	};
}

// diagnostics.go:64 selectDiagnosticBasedOnModuleName
const DiagnosticMessage* selectDiagnosticBasedOnModuleName(
	printer::SymbolAccessibilityResult& symbolAccessibilityResult,
	const DiagnosticMessage* moduleNotNameable,
	const DiagnosticMessage* privateModule, const DiagnosticMessage* nonModule) {
	if (!symbolAccessibilityResult.ErrorModuleName.empty()) {
		if (symbolAccessibilityResult.Accessibility ==
		    printer::SymbolAccessibility::CannotBeNamed) {
			return moduleNotNameable;
		}
		return privateModule;
	}
	return nonModule;
}

// diagnostics.go:74 selectDiagnosticBasedOnModuleNameNoNameCheck
const DiagnosticMessage* selectDiagnosticBasedOnModuleNameNoNameCheck(
	printer::SymbolAccessibilityResult& symbolAccessibilityResult,
	const DiagnosticMessage* privateModule, const DiagnosticMessage* nonModule) {
	if (!symbolAccessibilityResult.ErrorModuleName.empty()) {
		return privateModule;
	}
	return nonModule;
}

// diagnostics.go:91 getAccessorNameVisibilityDiagnosticMessage
const DiagnosticMessage* getAccessorNameVisibilityDiagnosticMessage(
	Node* node, printer::SymbolAccessibilityResult& symbolAccessibilityResult) {
	if (isStatic(node)) {
		return selectDiagnosticBasedOnModuleName(
			symbolAccessibilityResult,
			Public_static_property_0_of_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
			Public_static_property_0_of_exported_class_has_or_is_using_name_1_from_private_module_2,
			Public_static_property_0_of_exported_class_has_or_is_using_private_name_1);
	} else if (node->parent->kind == Kind::ClassDeclaration) {
		return selectDiagnosticBasedOnModuleName(
			symbolAccessibilityResult,
			Public_property_0_of_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
			Public_property_0_of_exported_class_has_or_is_using_name_1_from_private_module_2,
			Public_property_0_of_exported_class_has_or_is_using_private_name_1);
	} else {
		return selectDiagnosticBasedOnModuleNameNoNameCheck(
			symbolAccessibilityResult,
			Property_0_of_exported_interface_has_or_is_using_name_1_from_private_module_2,
			Property_0_of_exported_interface_has_or_is_using_private_name_1);
	}
}

// diagnostics.go:115 getMethodNameVisibilityDiagnosticMessage
const DiagnosticMessage* getMethodNameVisibilityDiagnosticMessage(
	Node* node, printer::SymbolAccessibilityResult& symbolAccessibilityResult) {
	if (isStatic(node)) {
		return selectDiagnosticBasedOnModuleName(
			symbolAccessibilityResult,
			Public_static_method_0_of_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
			Public_static_method_0_of_exported_class_has_or_is_using_name_1_from_private_module_2,
			Public_static_method_0_of_exported_class_has_or_is_using_private_name_1);
	} else if (node->parent->kind == Kind::ClassDeclaration) {
		return selectDiagnosticBasedOnModuleName(
			symbolAccessibilityResult,
			Public_method_0_of_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
			Public_method_0_of_exported_class_has_or_is_using_name_1_from_private_module_2,
			Public_method_0_of_exported_class_has_or_is_using_private_name_1);
	} else {
		return selectDiagnosticBasedOnModuleNameNoNameCheck(
			symbolAccessibilityResult,
			Method_0_of_exported_interface_has_or_is_using_name_1_from_private_module_2,
			Method_0_of_exported_interface_has_or_is_using_private_name_1);
	}
}

// diagnostics.go:223 getVariableDeclarationTypeVisibilityDiagnosticMessage
const DiagnosticMessage* getVariableDeclarationTypeVisibilityDiagnosticMessage(
	Node* node, printer::SymbolAccessibilityResult& symbolAccessibilityResult) {
	if (node->kind == Kind::VariableDeclaration ||
	    node->kind == Kind::BindingElement) {
		return selectDiagnosticBasedOnModuleName(
			symbolAccessibilityResult,
			Exported_variable_0_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
			Exported_variable_0_has_or_is_using_name_1_from_private_module_2,
			Exported_variable_0_has_or_is_using_private_name_1);

		// This check is to ensure we don't report error on constructor parameter property as that error would be reported during parameter emit
		// The only exception here is if the constructor was marked as private. we are not emitting the constructor parameters at all.
	} else if (node->kind == Kind::PropertyDeclaration ||
	           node->kind == Kind::PropertyAccessExpression ||
	           node->kind == Kind::ElementAccessExpression ||
	           node->kind == Kind::BinaryExpression ||
	           node->kind == Kind::PropertySignature ||
	           (node->kind == Kind::Parameter &&
	            hasSyntacticModifier(node->parent, ModifierFlagsPrivate))) {
		// TODO(jfreeman): Deal with computed properties in error reporting.
		if (isStatic(node)) {
			return selectDiagnosticBasedOnModuleName(
				symbolAccessibilityResult,
				Public_static_property_0_of_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
				Public_static_property_0_of_exported_class_has_or_is_using_name_1_from_private_module_2,
				Public_static_property_0_of_exported_class_has_or_is_using_private_name_1);
		} else if (node->parent->kind == Kind::ClassDeclaration ||
		           node->kind == Kind::Parameter) {
			return selectDiagnosticBasedOnModuleName(
				symbolAccessibilityResult,
				Public_property_0_of_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
				Public_property_0_of_exported_class_has_or_is_using_name_1_from_private_module_2,
				Public_property_0_of_exported_class_has_or_is_using_private_name_1);
		} else {
			// Interfaces cannot have types that cannot be named
			return selectDiagnosticBasedOnModuleNameNoNameCheck(
				symbolAccessibilityResult,
				Property_0_of_exported_interface_has_or_is_using_name_1_from_private_module_2,
				Property_0_of_exported_interface_has_or_is_using_private_name_1);
		}
	}
	return nullptr;  // TODO: Audit behavior - should this panic? potentially silent error state in strada
}

// diagnostics.go:263 getAccessorDeclarationTypeVisibilityDiagnosticMessage
const DiagnosticMessage* getAccessorDeclarationTypeVisibilityDiagnosticMessage(
	Node* node, printer::SymbolAccessibilityResult& symbolAccessibilityResult) {
	if (node->kind == Kind::SetAccessor) {
		// Getters can infer the return type from the returned expression, but setters cannot, so the
		// "_from_external_module_1_but_cannot_be_named" case cannot occur.
		if (isStatic(node)) {
			return selectDiagnosticBasedOnModuleNameNoNameCheck(
				symbolAccessibilityResult,
				Parameter_type_of_public_static_setter_0_from_exported_class_has_or_is_using_name_1_from_private_module_2,
				Parameter_type_of_public_static_setter_0_from_exported_class_has_or_is_using_private_name_1);
		} else {
			return selectDiagnosticBasedOnModuleNameNoNameCheck(
				symbolAccessibilityResult,
				Parameter_type_of_public_setter_0_from_exported_class_has_or_is_using_name_1_from_private_module_2,
				Parameter_type_of_public_setter_0_from_exported_class_has_or_is_using_private_name_1);
		}
	} else {
		if (isStatic(node)) {
			return selectDiagnosticBasedOnModuleName(
				symbolAccessibilityResult,
				Return_type_of_public_static_getter_0_from_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
				Return_type_of_public_static_getter_0_from_exported_class_has_or_is_using_name_1_from_private_module_2,
				Return_type_of_public_static_getter_0_from_exported_class_has_or_is_using_private_name_1);
		} else {
			return selectDiagnosticBasedOnModuleName(
				symbolAccessibilityResult,
				Return_type_of_public_getter_0_from_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
				Return_type_of_public_getter_0_from_exported_class_has_or_is_using_name_1_from_private_module_2,
				Return_type_of_public_getter_0_from_exported_class_has_or_is_using_private_name_1);
		}
	}
}

// diagnostics.go:299 getReturnTypeVisibilityDiagnosticMessage
const DiagnosticMessage* getReturnTypeVisibilityDiagnosticMessage(
	Node* node, printer::SymbolAccessibilityResult& symbolAccessibilityResult) {
	switch (node->kind) {
	case Kind::ConstructSignature:
		// Interfaces cannot have return types that cannot be named
		return selectDiagnosticBasedOnModuleNameNoNameCheck(
			symbolAccessibilityResult,
			Return_type_of_constructor_signature_from_exported_interface_has_or_is_using_name_0_from_private_module_1,
			Return_type_of_constructor_signature_from_exported_interface_has_or_is_using_private_name_0);
	case Kind::CallSignature:
		// Interfaces cannot have return types that cannot be named
		return selectDiagnosticBasedOnModuleNameNoNameCheck(
			symbolAccessibilityResult,
			Return_type_of_call_signature_from_exported_interface_has_or_is_using_name_0_from_private_module_1,
			Return_type_of_call_signature_from_exported_interface_has_or_is_using_private_name_0);
	case Kind::IndexSignature:
		// Interfaces cannot have return types that cannot be named
		return selectDiagnosticBasedOnModuleNameNoNameCheck(
			symbolAccessibilityResult,
			Return_type_of_index_signature_from_exported_interface_has_or_is_using_name_0_from_private_module_1,
			Return_type_of_index_signature_from_exported_interface_has_or_is_using_private_name_0);

	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
		if (isStatic(node)) {
			return selectDiagnosticBasedOnModuleName(
				symbolAccessibilityResult,
				Return_type_of_public_static_method_from_exported_class_has_or_is_using_name_0_from_external_module_1_but_cannot_be_named,
				Return_type_of_public_static_method_from_exported_class_has_or_is_using_name_0_from_private_module_1,
				Return_type_of_public_static_method_from_exported_class_has_or_is_using_private_name_0);
		} else if (node->parent->kind == Kind::ClassDeclaration) {
			return selectDiagnosticBasedOnModuleName(
				symbolAccessibilityResult,
				Return_type_of_public_method_from_exported_class_has_or_is_using_name_0_from_external_module_1_but_cannot_be_named,
				Return_type_of_public_method_from_exported_class_has_or_is_using_name_0_from_private_module_1,
				Return_type_of_public_method_from_exported_class_has_or_is_using_private_name_0);
		} else {
			// Interfaces cannot have return types that cannot be named
			return selectDiagnosticBasedOnModuleNameNoNameCheck(
				symbolAccessibilityResult,
				Return_type_of_method_from_exported_interface_has_or_is_using_name_0_from_private_module_1,
				Return_type_of_method_from_exported_interface_has_or_is_using_private_name_0);
		}
	case Kind::FunctionDeclaration:
		return selectDiagnosticBasedOnModuleName(
			symbolAccessibilityResult,
			Return_type_of_exported_function_has_or_is_using_name_0_from_external_module_1_but_cannot_be_named,
			Return_type_of_exported_function_has_or_is_using_name_0_from_private_module_1,
			Return_type_of_exported_function_has_or_is_using_private_name_0);
	default:
		std::string msg = "This is unknown kind for signature: " +
		                  std::string(kindToString(node->kind));
		TSC_UNREACHABLE(msg.c_str());
	}
}

// diagnostics.go:358 getParameterDeclarationTypeVisibilityDiagnosticMessage
const DiagnosticMessage* getParameterDeclarationTypeVisibilityDiagnosticMessage(
	Node* node, printer::SymbolAccessibilityResult& symbolAccessibilityResult) {
	switch (node->parent->kind) {
	case Kind::Constructor:
		return selectDiagnosticBasedOnModuleName(
			symbolAccessibilityResult,
			Parameter_0_of_constructor_from_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
			Parameter_0_of_constructor_from_exported_class_has_or_is_using_name_1_from_private_module_2,
			Parameter_0_of_constructor_from_exported_class_has_or_is_using_private_name_1);

	case Kind::ConstructSignature:
	case Kind::ConstructorType:
		// Interfaces cannot have parameter types that cannot be named
		return selectDiagnosticBasedOnModuleNameNoNameCheck(
			symbolAccessibilityResult,
			Parameter_0_of_constructor_signature_from_exported_interface_has_or_is_using_name_1_from_private_module_2,
			Parameter_0_of_constructor_signature_from_exported_interface_has_or_is_using_private_name_1);

	case Kind::CallSignature:
		// Interfaces cannot have parameter types that cannot be named
		return selectDiagnosticBasedOnModuleNameNoNameCheck(
			symbolAccessibilityResult,
			Parameter_0_of_call_signature_from_exported_interface_has_or_is_using_name_1_from_private_module_2,
			Parameter_0_of_call_signature_from_exported_interface_has_or_is_using_private_name_1);

	case Kind::IndexSignature:
		// Interfaces cannot have parameter types that cannot be named
		return selectDiagnosticBasedOnModuleNameNoNameCheck(
			symbolAccessibilityResult,
			Parameter_0_of_index_signature_from_exported_interface_has_or_is_using_name_1_from_private_module_2,
			Parameter_0_of_index_signature_from_exported_interface_has_or_is_using_private_name_1);

	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
		if (isStatic(node->parent)) {
			return selectDiagnosticBasedOnModuleName(
				symbolAccessibilityResult,
				Parameter_0_of_public_static_method_from_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
				Parameter_0_of_public_static_method_from_exported_class_has_or_is_using_name_1_from_private_module_2,
				Parameter_0_of_public_static_method_from_exported_class_has_or_is_using_private_name_1);
		} else if (node->parent->parent->kind == Kind::ClassDeclaration) {
			return selectDiagnosticBasedOnModuleName(
				symbolAccessibilityResult,
				Parameter_0_of_public_method_from_exported_class_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
				Parameter_0_of_public_method_from_exported_class_has_or_is_using_name_1_from_private_module_2,
				Parameter_0_of_public_method_from_exported_class_has_or_is_using_private_name_1);
		} else {
			// Interfaces cannot have parameter types that cannot be named
			return selectDiagnosticBasedOnModuleNameNoNameCheck(
				symbolAccessibilityResult,
				Parameter_0_of_method_from_exported_interface_has_or_is_using_name_1_from_private_module_2,
				Parameter_0_of_method_from_exported_interface_has_or_is_using_private_name_1);
		}

	case Kind::FunctionDeclaration:
	case Kind::FunctionType:
	case Kind::ArrowFunction:
	case Kind::FunctionExpression:
		return selectDiagnosticBasedOnModuleName(
			symbolAccessibilityResult,
			Parameter_0_of_exported_function_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
			Parameter_0_of_exported_function_has_or_is_using_name_1_from_private_module_2,
			Parameter_0_of_exported_function_has_or_is_using_private_name_1);
	case Kind::SetAccessor:
	case Kind::GetAccessor:
		return selectDiagnosticBasedOnModuleName(
			symbolAccessibilityResult,
			Parameter_0_of_accessor_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
			Parameter_0_of_accessor_has_or_is_using_name_1_from_private_module_2,
			Parameter_0_of_accessor_has_or_is_using_private_name_1);

	default:
		std::string msg = "Unknown parent for parameter: " +
		                  std::string(kindToString(node->parent->kind));
		TSC_UNREACHABLE(msg.c_str());
	}
}

// diagnostics.go:436 getTypeParameterConstraintVisibilityDiagnosticMessage
const DiagnosticMessage* getTypeParameterConstraintVisibilityDiagnosticMessage(
	Node* node,
	printer::SymbolAccessibilityResult& /*symbolAccessibilityResult*/) {
	// Type parameter constraints are named by user so we should always be able to name it
	switch (node->parent->kind) {
	case Kind::ClassDeclaration:
		return Type_parameter_0_of_exported_class_has_or_is_using_private_name_1;
	case Kind::InterfaceDeclaration:
		return Type_parameter_0_of_exported_interface_has_or_is_using_private_name_1;
	case Kind::MappedType:
		return Type_parameter_0_of_exported_mapped_object_type_is_using_private_name_1;
	case Kind::ConstructorType:
	case Kind::ConstructSignature:
		return Type_parameter_0_of_constructor_signature_from_exported_interface_has_or_is_using_private_name_1;
	case Kind::CallSignature:
		return Type_parameter_0_of_call_signature_from_exported_interface_has_or_is_using_private_name_1;
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
		if (isStatic(node->parent)) {
			return Type_parameter_0_of_public_static_method_from_exported_class_has_or_is_using_private_name_1;
		} else if (node->parent->parent->kind == Kind::ClassDeclaration) {
			return Type_parameter_0_of_public_method_from_exported_class_has_or_is_using_private_name_1;
		} else {
			return Type_parameter_0_of_method_from_exported_interface_has_or_is_using_private_name_1;
		}
	case Kind::FunctionType:
	case Kind::FunctionDeclaration:
		return Type_parameter_0_of_exported_function_has_or_is_using_private_name_1;

	case Kind::InferType:
		return Extends_clause_for_inferred_type_0_has_or_is_using_private_name_1;

	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
		return Type_parameter_0_of_exported_type_alias_has_or_is_using_private_name_1;

	default:
		std::string msg = "This is unknown parent for type parameter: " +
		                  std::string(kindToString(node->parent->kind));
		TSC_UNREACHABLE(msg.c_str());
	}
}

// diagnostics.go:471 getRelatedSuggestionByDeclarationKind
const DiagnosticMessage* getRelatedSuggestionByDeclarationKind(Kind kind) {
	switch (kind) {
	case Kind::ArrowFunction:
		return Add_a_return_type_to_the_function_expression;
	case Kind::FunctionExpression:
		return Add_a_return_type_to_the_function_expression;
	case Kind::MethodDeclaration:
		return Add_a_return_type_to_the_method;
	case Kind::GetAccessor:
		return Add_a_return_type_to_the_get_accessor_declaration;
	case Kind::SetAccessor:
		return Add_a_type_to_parameter_of_the_set_accessor_declaration;
	case Kind::FunctionDeclaration:
		return Add_a_return_type_to_the_function_declaration;
	case Kind::ConstructSignature:
		return Add_a_return_type_to_the_function_declaration;
	case Kind::Parameter:
		return Add_a_type_annotation_to_the_parameter_0;
	case Kind::VariableDeclaration:
		return Add_a_type_annotation_to_the_variable_0;
	case Kind::PropertyDeclaration:
		return Add_a_type_annotation_to_the_property_0;
	case Kind::PropertySignature:
		return Add_a_type_annotation_to_the_property_0;
	case Kind::ExportAssignment:
		return Move_the_expression_in_default_export_to_a_variable_and_add_a_type_annotation_to_it;
	default:
		return nullptr;
	}
}

// diagnostics.go:502 getErrorByDeclarationKind
const DiagnosticMessage* getErrorByDeclarationKind(Kind kind) {
	switch (kind) {
	case Kind::FunctionExpression:
		return Function_must_have_an_explicit_return_type_annotation_with_isolatedDeclarations;
	case Kind::FunctionDeclaration:
		return Function_must_have_an_explicit_return_type_annotation_with_isolatedDeclarations;
	case Kind::ArrowFunction:
		return Function_must_have_an_explicit_return_type_annotation_with_isolatedDeclarations;
	case Kind::MethodDeclaration:
		return Method_must_have_an_explicit_return_type_annotation_with_isolatedDeclarations;
	case Kind::ConstructSignature:
		return Method_must_have_an_explicit_return_type_annotation_with_isolatedDeclarations;
	case Kind::GetAccessor:
		return At_least_one_accessor_must_have_an_explicit_type_annotation_with_isolatedDeclarations;
	case Kind::SetAccessor:
		return At_least_one_accessor_must_have_an_explicit_type_annotation_with_isolatedDeclarations;
	case Kind::Parameter:
		return Parameter_must_have_an_explicit_type_annotation_with_isolatedDeclarations;
	case Kind::VariableDeclaration:
		return Variable_must_have_an_explicit_type_annotation_with_isolatedDeclarations;
	case Kind::PropertyDeclaration:
		return Property_must_have_an_explicit_type_annotation_with_isolatedDeclarations;
	case Kind::PropertySignature:
		return Property_must_have_an_explicit_type_annotation_with_isolatedDeclarations;
	case Kind::ComputedPropertyName:
		return Computed_property_names_on_class_or_object_literals_cannot_be_inferred_with_isolatedDeclarations;
	case Kind::SpreadAssignment:
		return Objects_that_contain_spread_assignments_can_t_be_inferred_with_isolatedDeclarations;
	case Kind::ShorthandPropertyAssignment:
		return Objects_that_contain_shorthand_properties_can_t_be_inferred_with_isolatedDeclarations;
	case Kind::ArrayLiteralExpression:
		return Only_const_arrays_can_be_inferred_with_isolatedDeclarations;
	case Kind::ExportAssignment:
		return Default_exports_can_t_be_inferred_with_isolatedDeclarations;
	case Kind::SpreadElement:
		return Arrays_with_spread_elements_can_t_inferred_with_isolatedDeclarations;
	default:
		return nullptr;
	}
}

// diagnostics.go:543 isDeclarationEnoughForErrors
bool isDeclarationEnoughForErrors(Node* node) {
	return isExportAssignment(node) || isStatement(node) ||
	       isVariableDeclaration(node) || isPropertyDeclaration(node) ||
	       isParameterDeclaration(node);
}

// diagnostics.go:547 isFunctionLikeAndNotConstructor
bool isFunctionLikeAndNotConstructor(Node* node) {
	return isFunctionLikeDeclaration(node) &&
	       !isConstructorDeclaration(node);
}

// diagnostics.go:551 findNearestDeclaration
Node* findNearestDeclaration(Node* node) {
	Node* result = findAncestor(node, isDeclarationEnoughForErrors);
	if (result == nullptr) {
		return nullptr;
	}
	if (isExportAssignment(result)) {
		return result;
	}
	if (isReturnStatement(result)) {
		return findAncestor(result, isFunctionLikeAndNotConstructor);
	}
	if (isStatement(result)) {
		return nullptr;
	}
	return result;
}

// diagnostics.go:574 addParentDeclarationRelatedInfo
void addParentDeclarationRelatedInfo(Node* node, Diagnostic* diag) {
	Node* parentDeclaration = findNearestDeclaration(node);
	if (parentDeclaration == nullptr) {
		return;
	}
	std::string targetStr;
	if (!isExportAssignment(parentDeclaration) &&
	    parentDeclaration->name() != nullptr) {
		targetStr = getTextOfNode(parentDeclaration->name());
	}
	diag->AddRelatedInfo(createDiagnosticForNode(
		parentDeclaration,
		getRelatedSuggestionByDeclarationKind(parentDeclaration->kind),
		{targetStr}));
}

// diagnostics.go:568 createEntityInTypeNodeError
Diagnostic* createEntityInTypeNodeError(Node* node) {
	Diagnostic* diag = createDiagnosticForNode(
		node,
		Type_containing_private_name_0_can_t_be_used_with_isolatedDeclarations,
		{getTextOfNode(node)});
	addParentDeclarationRelatedInfo(node, diag);
	return diag;
}

// diagnostics.go:586 createAccessorTypeError
Diagnostic* createAccessorTypeError(Node* node) {
	AllAccessorDeclarations allDeclarations =
	    getAllAccessorDeclarationsForDeclaration(node,
	                                             node->symbol()->data->declarations);
	Node* getAccessor = allDeclarations.getAccessor;
	Node* setAccessor = allDeclarations.setAccessor;
	Node* targetNode = node;
	if (isSetAccessorDeclaration(node) && !node->parameters().empty()) {
		targetNode = node->parameters()[0];
	}
	Diagnostic* diag = createDiagnosticForNode(
		targetNode, getErrorByDeclarationKind(node->kind));
	if (setAccessor != nullptr) {
		diag->AddRelatedInfo(createDiagnosticForNode(
			setAccessor->asNode(),
			getRelatedSuggestionByDeclarationKind(setAccessor->kind)));
	}
	if (getAccessor != nullptr) {
		diag->AddRelatedInfo(createDiagnosticForNode(
			getAccessor->asNode(),
			getRelatedSuggestionByDeclarationKind(getAccessor->kind)));
	}
	return diag;
}

// diagnostics.go:604 createObjectLiteralError
Diagnostic* createObjectLiteralError(Node* node) {
	Diagnostic* diag = createDiagnosticForNode(
		node, getErrorByDeclarationKind(node->kind));
	addParentDeclarationRelatedInfo(node, diag);
	return diag;
}

// diagnostics.go:610 createArrayLiteralError
Diagnostic* createArrayLiteralError(Node* node) {
	Diagnostic* diag = createDiagnosticForNode(
		node, getErrorByDeclarationKind(node->kind));
	addParentDeclarationRelatedInfo(node, diag);
	return diag;
}

// diagnostics.go:616 createReturnTypeError
Diagnostic* createReturnTypeError(Node* node) {
	Diagnostic* diag = createDiagnosticForNode(
		node, getErrorByDeclarationKind(node->kind));
	addParentDeclarationRelatedInfo(node, diag);
	diag->AddRelatedInfo(createDiagnosticForNode(
		node, getRelatedSuggestionByDeclarationKind(node->kind)));
	return diag;
}

// diagnostics.go:623 createBindingElementError
Diagnostic* createBindingElementError(Node* node) {
	return createDiagnosticForNode(
		node,
		Binding_elements_with_initializers_can_t_be_exported_directly_with_isolatedDeclarations);
}

// diagnostics.go:627 createVariableOrPropertyError
Diagnostic* createVariableOrPropertyError(Node* node) {
	Diagnostic* diag = createDiagnosticForNode(
		node, getErrorByDeclarationKind(node->kind));
	diag->AddRelatedInfo(createDiagnosticForNode(
		node, getRelatedSuggestionByDeclarationKind(node->kind),
		{getTextOfNode(node->name())}));
	return diag;
}

// diagnostics.go:641 isParentForIDDIagnostic
FindAncestorResult isParentForIDDIagnostic(Node* node) {
	if (isExportAssignment(node)) {
		return FindAncestorResult::True;
	}
	if (isStatement(node)) {
		return FindAncestorResult::Quit;
	}
	return toFindAncestorResult(!isParenthesizedExpression(node) &&
	                            !isAssertionExpression(node));
}

// diagnostics.go:651 createExpressionErrorEx
Diagnostic* createExpressionErrorEx(Node* node,
                                    const DiagnosticMessage* diagnosticMessage) {
	Node* parentDeclaration = findNearestDeclaration(node);
	if (parentDeclaration == nullptr) {
		if (diagnosticMessage == nullptr) {
			diagnosticMessage =
			    Expression_type_can_t_be_inferred_with_isolatedDeclarations;
		}
		return createDiagnosticForNode(node, diagnosticMessage);
	}

	std::string targetStr;
	if (!isExportAssignment(parentDeclaration) &&
	    parentDeclaration->name() != nullptr) {
		targetStr = getTextOfNode(parentDeclaration->name());
	}
	Node* parent =
	    findAncestorOrQuit(node->parent, isParentForIDDIagnostic);

	if (parentDeclaration == parent) {
		if (diagnosticMessage == nullptr) {
			diagnosticMessage =
			    getErrorByDeclarationKind(parentDeclaration->kind);
		}
		Diagnostic* diag =
		    createDiagnosticForNode(node, diagnosticMessage);
		diag->AddRelatedInfo(createDiagnosticForNode(
			parentDeclaration,
			getRelatedSuggestionByDeclarationKind(parentDeclaration->kind),
			{targetStr}));
		return diag;
	}
	if (diagnosticMessage == nullptr) {
		diagnosticMessage =
		    Expression_type_can_t_be_inferred_with_isolatedDeclarations;
	}
	Diagnostic* diag = createDiagnosticForNode(node, diagnosticMessage);
	diag->AddRelatedInfo(createDiagnosticForNode(
		parentDeclaration,
		getRelatedSuggestionByDeclarationKind(parentDeclaration->kind),
		{targetStr}));
	diag->AddRelatedInfo(createDiagnosticForNode(
		node,
		Add_satisfies_and_a_type_assertion_to_this_expression_satisfies_T_as_T_to_make_the_type_explicit));
	return diag;
}

// diagnostics.go:633 createExpressionError
Diagnostic* createExpressionError(Node* node) {
	return createExpressionErrorEx(node, nullptr);
}

// diagnostics.go:637 createClassExpressionError
Diagnostic* createClassExpressionError(Node* node) {
	return createExpressionErrorEx(
		node,
		Inference_from_class_expressions_is_not_supported_with_isolatedDeclarations);
}

}  // namespace

// diagnostics.go:81 createGetSymbolAccessibilityDiagnosticForNodeName
GetSymbolAccessibilityDiagnostic
createGetSymbolAccessibilityDiagnosticForNodeName(Node* node) {
	if (isSetAccessorDeclaration(node) || isGetAccessorDeclaration(node)) {
		return wrapSimpleDiagnosticSelector(
			node, getAccessorNameVisibilityDiagnosticMessage);
	} else if (isMethodDeclaration(node) ||
	           isMethodSignatureDeclaration(node)) {
		return wrapSimpleDiagnosticSelector(
			node, getMethodNameVisibilityDiagnosticMessage);
	} else {
		return createGetSymbolAccessibilityDiagnosticForNode(node);
	}
}

// diagnostics.go:139 createGetSymbolAccessibilityDiagnosticForNode
GetSymbolAccessibilityDiagnostic createGetSymbolAccessibilityDiagnosticForNode(
	Node* node) {
	if (isVariableDeclaration(node) || isPropertyDeclaration(node) ||
	    isPropertySignatureDeclaration(node) ||
	    isPropertyAccessExpression(node) || isElementAccessExpression(node) ||
	    isBinaryExpression(node) || isBindingElement(node) ||
	    isConstructorDeclaration(node)) {
		return wrapSimpleDiagnosticSelector(
			node, getVariableDeclarationTypeVisibilityDiagnosticMessage);
	} else if (isSetAccessorDeclaration(node) ||
	           isGetAccessorDeclaration(node)) {
		return wrapNamedDiagnosticSelector(
			node, getAccessorDeclarationTypeVisibilityDiagnosticMessage);
	} else if (isConstructSignatureDeclaration(node) ||
	           isCallSignatureDeclaration(node) || isMethodDeclaration(node) ||
	           isMethodSignatureDeclaration(node) ||
	           isFunctionDeclaration(node) ||
	           isIndexSignatureDeclaration(node)) {
		return wrapFallbackErrorDiagnosticSelector(
			node, getReturnTypeVisibilityDiagnosticMessage);
	} else if (isParameterDeclaration(node)) {
		if (isParameterPropertyDeclaration(node, node->parent) &&
		    hasSyntacticModifier(node->parent, ModifierFlagsPrivate)) {
			return wrapSimpleDiagnosticSelector(
				node, getVariableDeclarationTypeVisibilityDiagnosticMessage);
		}
		return wrapSimpleDiagnosticSelector(
			node, getParameterDeclarationTypeVisibilityDiagnosticMessage);
	} else if (isTypeParameterDeclaration(node)) {
		return wrapSimpleDiagnosticSelector(
			node, getTypeParameterConstraintVisibilityDiagnosticMessage);
	} else if (isExpressionWithTypeArguments(node)) {
		// unique node selection behavior, inline closure
		return [node](printer::SymbolAccessibilityResult&
		                  symbolAccessibilityResult)
		       -> SymbolAccessibilityDiagnostic* {
			const DiagnosticMessage* diagnosticMessage = nullptr;
			// Heritage clause is written by user so it can always be named
			if (isClassDeclaration(node->parent->parent)) {
				// Class or Interface implemented/extended is inaccessible
				if (isHeritageClause(node->parent) &&
				    node->parent->as<HeritageClause>()->Token ==
				        Kind::ImplementsKeyword) {
					diagnosticMessage =
					    Implements_clause_of_exported_class_0_has_or_is_using_private_name_1;
				} else {
					if (node->parent->parent->name() != nullptr) {
						diagnosticMessage =
						    X_extends_clause_of_exported_class_0_has_or_is_using_private_name_1;
					} else {
						diagnosticMessage =
						    X_extends_clause_of_exported_class_has_or_is_using_private_name_0;
					}
				}
			} else {
				// interface is inaccessible
				diagnosticMessage =
				    X_extends_clause_of_exported_interface_0_has_or_is_using_private_name_1;
			}

			auto* result = new SymbolAccessibilityDiagnostic();
			result->diagnosticMessage = diagnosticMessage;
			result->errorNode = node;
			result->typeName =
			    getNameOfDeclaration(node->parent->parent);
			return result;
		};
	} else if (isImportEqualsDeclaration(node)) {
		return wrapSimpleDiagnosticSelector(
			node, [](Node*, printer::SymbolAccessibilityResult&)
			      -> const DiagnosticMessage* {
				return Import_declaration_0_is_using_private_name_1;
			});
	} else if (isTypeAliasDeclaration(node) ||
	           isJSTypeAliasDeclaration(node)) {
		// unique node selection behavior, inline closure
		return [node](printer::SymbolAccessibilityResult&
		                  symbolAccessibilityResult)
		       -> SymbolAccessibilityDiagnostic* {
			const DiagnosticMessage* diagnosticMessage =
			    selectDiagnosticBasedOnModuleNameNoNameCheck(
					symbolAccessibilityResult,
					Exported_type_alias_0_has_or_is_using_private_name_1_from_module_2,
					Exported_type_alias_0_has_or_is_using_private_name_1);
			Node* errorNode = node->type();
			Node* typeName = node->name();
			auto* result = new SymbolAccessibilityDiagnostic();
			result->errorNode = errorNode;
			result->diagnosticMessage = diagnosticMessage;
			result->typeName = typeName;
			return result;
		};
	} else if (isCallExpression(node)) {
		// JS object.defineProperty call
		// unique node selection behavior, inline closure
		return [node](printer::SymbolAccessibilityResult&
		                  symbolAccessibilityResult)
		       -> SymbolAccessibilityDiagnostic* {
			const DiagnosticMessage* diagnosticMessage =
			    selectDiagnosticBasedOnModuleName(
					symbolAccessibilityResult,
					Exported_variable_0_has_or_is_using_name_1_from_external_module_2_but_cannot_be_named,
					Exported_variable_0_has_or_is_using_name_1_from_private_module_2,
					Exported_variable_0_has_or_is_using_private_name_1);
			Node* errorNode = node->arguments()[1];
			Node* typeName = node->arguments()[1];
			auto* result = new SymbolAccessibilityDiagnostic();
			result->errorNode = errorNode;
			result->diagnosticMessage = diagnosticMessage;
			result->typeName = typeName;
			return result;
		};
	} else {
		std::string msg =
			"Attempted to set a declaration diagnostic context for "
			"unhandled node kind: " +
			std::string(kindToString(node->kind));
		TSC_UNREACHABLE(msg.c_str());
	}
}

// diagnostics.go:683 createGetIsolatedDeclarationErrors
std::function<Diagnostic*(Node*)> createGetIsolatedDeclarationErrors(
	printer::EmitResolver* resolver) {
	auto createParameterError =
	    [resolver](Node* node) -> Diagnostic* {
		if (isSetAccessorDeclaration(node->parent)) {
			return createAccessorTypeError(node->parent);
		}
		bool addUndefined = resolver->RequiresAddingImplicitUndefinedUnsafe(
			node, nullptr, nullptr);  // skip checker lock - node builder will already have one
		if (!addUndefined && node->initializer() != nullptr) {
			return createExpressionError(node->initializer());
		}
		const DiagnosticMessage* message =
		    getErrorByDeclarationKind(node->kind);
		if (addUndefined) {
			message =
			    Declaration_emit_for_this_parameter_requires_implicitly_adding_undefined_to_its_type_This_is_not_supported_with_isolatedDeclarations;
		}
		Diagnostic* diag = createDiagnosticForNode(node, message);
		std::string targetStr = getTextOfNode(node->name());
		diag->AddRelatedInfo(createDiagnosticForNode(
			node, getRelatedSuggestionByDeclarationKind(node->kind),
			{targetStr}));
		return diag;
	};

	return [createParameterError](Node* node) -> Diagnostic* {
		Node* heritageClause = findAncestor(node, isHeritageClause);
		if (heritageClause != nullptr) {
			return createDiagnosticForNode(
				node,
				Extends_clause_can_t_contain_an_expression_with_isolatedDeclarations);
		}
		if (isPartOfTypeNode(node) || isTypeQuery(node)) {
			return createEntityInTypeNodeError(node);
		}
		if (isEntityName(node) || isEntityNameExpression(node)) {
			return createEntityInTypeNodeError(node);
		}
		switch (node->kind) {
		case Kind::GetAccessor:
		case Kind::SetAccessor:
			return createAccessorTypeError(node);
		case Kind::ComputedPropertyName:
		case Kind::ShorthandPropertyAssignment:
		case Kind::SpreadAssignment:
			return createObjectLiteralError(node);
		case Kind::ArrayLiteralExpression:
		case Kind::SpreadElement:
			return createArrayLiteralError(node);
		case Kind::MethodDeclaration:
		case Kind::ConstructSignature:
		case Kind::FunctionExpression:
		case Kind::ArrowFunction:
		case Kind::FunctionDeclaration:
			return createReturnTypeError(node);
		case Kind::BindingElement:
			return createBindingElementError(node);
		case Kind::PropertyDeclaration:
		case Kind::VariableDeclaration:
			return createVariableOrPropertyError(node);
		case Kind::Parameter:
			return createParameterError(node);
		case Kind::PropertyAssignment:
			return createExpressionError(node->initializer());
		case Kind::ClassExpression:
			return createClassExpressionError(node);
		default:
			return createExpressionError(node);
		}
	};
}

}  // namespace tsc::transformers::declarations
