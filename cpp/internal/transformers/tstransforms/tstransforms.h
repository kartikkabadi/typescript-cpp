// Port of tsc/internal/transformers/tstransforms — package-level helpers.
#pragma once

#include <string_view>
#include <vector>

#include "internal/jsnum/jsnum.h"
#include "internal/transformers/transformers.h"

namespace tsc::checker {
struct EmitResolver;
}

namespace tsc::transformers::tstransforms {

// constantExpression — utilities.go:9. Go takes `any` (string | jsnum.Number);
// two overloads here.
Node* constantExpression(std::string_view value,
                         printer::NodeFactory* factory);
Node* constantExpression(Number value, printer::NodeFactory* factory);

// ---------------------------------------------------------------------------
// Transformer factories (called from compiler/emitter.cpp).
// ---------------------------------------------------------------------------

// NewImportElisionTransformer — importelision.go:17
Transformer* NewImportElisionTransformer(TransformOptions* opt);
// NewLegacyDecoratorsTransformer — legacydecorators.go:25
Transformer* NewLegacyDecoratorsTransformer(TransformOptions* opt);
// NewMetadataTransformer — metadata.go:24
Transformer* NewMetadataTransformer(TransformOptions* opt);
// NewRuntimeSyntaxTransformer — runtimesyntax.go:31
Transformer* NewRuntimeSyntaxTransformer(TransformOptions* opt);
// NewTypeEraserTransformer — typeeraser.go:18
Transformer* NewTypeEraserTransformer(TransformOptions* opt);

// ---------------------------------------------------------------------------
// Shared package helpers.
// ---------------------------------------------------------------------------

// getInnermostModuleDeclarationFromDottedModule — runtimesyntax.go:990.
// Defined in runtimesyntax.cpp; also used by typeeraser.cpp.
ModuleDeclaration* getInnermostModuleDeclarationFromDottedModule(
	ModuleDeclaration* moduleDeclaration);

// getDecoratorsOfParameters — legacydecorators.go:768. Gets an array of
// arrays of decorators for the parameters of a function-like node. The
// offset into the result array should correspond to the offset of the
// parameter. Defined in legacydecorators.cpp; also used by metadata.cpp.
std::vector<std::vector<Node*>> getDecoratorsOfParameters(Node* node);

// GetSetAccessorValueParameter — typeserializer.go:55. Exported in Go.
Node* GetSetAccessorValueParameter(SetAccessorDeclaration* node);

// ---------------------------------------------------------------------------
// metadataSerializer — typeserializer.go:11. Shared with metadata.cpp.
// ---------------------------------------------------------------------------

// metadataSerializerContext — typeserializer.go:20
struct metadataSerializerContext {
	Node* currentLexicalScope = nullptr;
	Node* currentNameScope = nullptr;
	bool serializingConditionalTypeBranch = false;
};

// metadataSerializer — typeserializer.go:11
struct metadataSerializer {
	checker::EmitResolver* resolver = nullptr;
	ScriptTarget languageVersion{};
	bool strictNullChecks = false;
	printer::NodeFactory* f = nullptr;
	printer::EmitContext* ec = nullptr;
	metadataSerializerContext c;

	// SerializeTypeOfNode — typeserializer.go:34
	Node* SerializeTypeOfNode(metadataSerializerContext ctx, Node* node,
	                          Node* container);
	// SerializeParameterTypesOfNode — typeserializer.go:41
	Node* SerializeParameterTypesOfNode(metadataSerializerContext ctx,
	                                  Node* node, Node* container);
	// SerializeReturnTypeOfNode — typeserializer.go:48
	Node* SerializeReturnTypeOfNode(metadataSerializerContext ctx, Node* node);

private:
	void setContext(metadataSerializerContext ctx) { c = ctx; }
	Node* serializeTypeOfNode(Node* node, Node* container);
	Node* serializeParameterTypesOfNode(Node* node, Node* container);
	Node* serializeReturnTypeOfNode(Node* node);
	Node* serializeTypeNode(Node* node);
	Node* serializeUnionOrIntersectionConstituents(
		const std::vector<Node*>& types, bool isIntersection);
	Node* serializeLiteralOfLiteralTypeNode(Node* node);
	Node* serializeTypeReferenceNode(TypeReferenceNode* node);
	Node* serializeBigIntConstructor();
	Node* serializeEntityNameAsExpression(Node* node);
	Node* serializeQualifiedNameAsExpression(QualifiedName* node);
	Node* serializeEntityNameAsExpressionFallback(Node* node);
	Node* createCheckedValue(Node* left, Node* right);
	bool equateSerializedTypeNodes(Node* left, Node* right);
};

// newMetadataSerializer — typeserializer.go:26
metadataSerializer* newMetadataSerializer(
	checker::EmitResolver* resolver, printer::NodeFactory* f,
	printer::EmitContext* ec, ScriptTarget languageVersion,
	bool strictNullChecks);

}  // namespace tsc::transformers::tstransforms
