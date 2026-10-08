// hovericon.go — VS image-catalog icon mapping for hover tooltips
// (newVSImageId / getVSHoverImageId / buildVSHoverRawContent).
#include "internal/ls/ls.h"

namespace tsc::ls {

namespace {

// vsImageCatalogGuid is the GUID of the shared VS image catalog (see
// Microsoft.VisualStudio.Imaging.KnownImageIds), mirroring the constant
// duplicated in TypeScript-VS's ImageIdMapping.cs (which avoids taking an
// assembly reference just for this GUID).
constexpr const char* vsImageCatalogGuid =
    "ae27a6b0-e345-4288-96df-5eaf394ee369";

// Known image IDs from Microsoft.VisualStudio.Imaging.KnownImageIds,
// restricted to the subset consumed by TypeScript-VS's ImageIdMapping.cs for
// hover tooltips. Corsa (this LSP server) has no TSServer/Roslyn dependency
// to reuse that mapping from, so the values are duplicated here.
inline constexpr int32_t imageIdWarning = 0x00000637;
inline constexpr int32_t imageIdKeyword = 0x00000635;
inline constexpr int32_t imageIdModulePrivate = 0x0000077D;
inline constexpr int32_t imageIdModuleProtected = 0x0000077E;
inline constexpr int32_t imageIdModulePublic = 0x0000077F;
inline constexpr int32_t imageIdType = 0x00000CA1;
inline constexpr int32_t imageIdNamespace = 0x0000079F;
inline constexpr int32_t imageIdClassPrivate = 0x000001D7;
inline constexpr int32_t imageIdClassProtected = 0x000001D8;
inline constexpr int32_t imageIdClassPublic = 0x000001D9;
inline constexpr int32_t imageIdInterfacePrivate = 0x00000646;
inline constexpr int32_t imageIdInterfaceProtected = 0x00000647;
inline constexpr int32_t imageIdInterfacePublic = 0x00000648;
inline constexpr int32_t imageIdEnumPrivate = 0x00000469;
inline constexpr int32_t imageIdEnumProtected = 0x0000046A;
inline constexpr int32_t imageIdEnumPublic = 0x0000046B;
inline constexpr int32_t imageIdEnumMember = 0x00000465;
inline constexpr int32_t imageIdLocalVariable = 0x000006D3;
inline constexpr int32_t imageIdPropertyPrivate = 0x00000982;
inline constexpr int32_t imageIdPropertyProtected = 0x00000983;
inline constexpr int32_t imageIdPropertyPublic = 0x00000984;
inline constexpr int32_t imageIdMethodPrivate = 0x00000756;
inline constexpr int32_t imageIdMethodProtected = 0x00000757;
inline constexpr int32_t imageIdMethodPublic = 0x00000758;
inline constexpr int32_t imageIdLabel = 0x0000067D;
inline constexpr int32_t imageIdAssembly = 0x000000C4;
inline constexpr int32_t imageIdConstantPrivate = 0x0000026A;
inline constexpr int32_t imageIdConstantProtected = 0x0000026B;
inline constexpr int32_t imageIdConstantPublic = 0x0000026C;

// newVSImageId — hovericon.go:48.
lsp::lsproto::VSImageId* newVSImageId(int32_t id) {
	auto* imageId = new lsp::lsproto::VSImageId;
	imageId->Guid = vsImageCatalogGuid;
	imageId->Id = id;
	return imageId;
}

} // namespace

// getVSHoverImageId — hovericon.go:56. Maps a symbol's
// ScriptElementKind/modifiers to the VS image shown next to the symbol name
// in hover tooltips. This mirrors TypeScript-VS's ImageIdMapping.GetImageId,
// which the legacy (TSServer-backed) hover path uses; Corsa has no TSServer
// to source that mapping from, so the LSP hover response must carry the
// equivalent icon directly.
lsp::lsproto::VSImageId* getVSHoverImageId(
    lsutil::ScriptElementKind kind,
    lsutil::ScriptElementKindModifier modifiers) {
	bool isPrivate =
	    (modifiers & lsutil::ScriptElementKindModifierPrivate) !=
	    lsutil::ScriptElementKindModifierNone;
	bool isProtected =
	    (modifiers & lsutil::ScriptElementKindModifierProtected) !=
	    lsutil::ScriptElementKindModifierNone;

	// No internal/exported arm: the *Internal VS icons carry a chevron
	// overlay that conveys C# assembly-scoped visibility, a concept that
	// doesn't apply to TypeScript.
	auto pick = [isPrivate, isProtected](int32_t priv, int32_t prot,
	                                   int32_t pub)
	    -> lsp::lsproto::VSImageId* {
		if (isPrivate) {
			return newVSImageId(priv);
		}
		if (isProtected) {
			return newVSImageId(prot);
		}
		return newVSImageId(pub);
	};

	switch (kind) {
	case lsutil::ScriptElementKindWarning:
		return newVSImageId(imageIdWarning);
	case lsutil::ScriptElementKindKeyword:
		return newVSImageId(imageIdKeyword);
	case lsutil::ScriptElementKindScriptElement:
		return pick(imageIdModulePrivate, imageIdModuleProtected,
		            imageIdModulePublic);
	case lsutil::ScriptElementKindPrimitiveType:
		return newVSImageId(imageIdType);
	case lsutil::ScriptElementKindModuleElement:
		return newVSImageId(imageIdNamespace);
	case lsutil::ScriptElementKindConstructorImplementationElement:
	case lsutil::ScriptElementKindClassElement:
	case lsutil::ScriptElementKindLocalClassElement:
	case lsutil::ScriptElementKindTypeElement:
		return pick(imageIdClassPrivate, imageIdClassProtected,
		            imageIdClassPublic);
	case lsutil::ScriptElementKindInterfaceElement:
		return pick(imageIdInterfacePrivate, imageIdInterfaceProtected,
		            imageIdInterfacePublic);
	case lsutil::ScriptElementKindEnumElement:
		return pick(imageIdEnumPrivate, imageIdEnumProtected,
		            imageIdEnumPublic);
	case lsutil::ScriptElementKindEnumMemberElement:
		return newVSImageId(imageIdEnumMember);
	case lsutil::ScriptElementKindParameterElement:
	case lsutil::ScriptElementKindVariableElement:
	case lsutil::ScriptElementKindLocalVariableElement:
	case lsutil::ScriptElementKindVariableUsingElement:
	case lsutil::ScriptElementKindVariableAwaitUsingElement:
	case lsutil::ScriptElementKindLetElement:
	case lsutil::ScriptElementKindString:
		return newVSImageId(imageIdLocalVariable);
	case lsutil::ScriptElementKindConstElement:
		return pick(imageIdConstantPrivate, imageIdConstantProtected,
		            imageIdConstantPublic);
	case lsutil::ScriptElementKindMemberGetAccessorElement:
	case lsutil::ScriptElementKindMemberSetAccessorElement:
	case lsutil::ScriptElementKindMemberVariableElement:
	case lsutil::ScriptElementKindMemberAccessorVariableElement:
		return pick(imageIdPropertyPrivate, imageIdPropertyProtected,
		            imageIdPropertyPublic);
	case lsutil::ScriptElementKindFunctionElement:
	case lsutil::ScriptElementKindLocalFunctionElement:
	case lsutil::ScriptElementKindMemberFunctionElement:
	case lsutil::ScriptElementKindCallSignatureElement:
	case lsutil::ScriptElementKindIndexSignatureElement:
	case lsutil::ScriptElementKindConstructSignatureElement:
		return pick(imageIdMethodPrivate, imageIdMethodProtected,
		            imageIdMethodPublic);
	case lsutil::ScriptElementKindTypeParameterElement:
		return newVSImageId(imageIdType);
	case lsutil::ScriptElementKindLabel:
		return newVSImageId(imageIdLabel);
	case lsutil::ScriptElementKindAlias:
		return newVSImageId(imageIdModulePublic);
	default:
		return newVSImageId(imageIdAssembly);
	}
}

// buildVSHoverRawContent — hovericon.go:132. Assembles the VS-specific rich
// hover content (symbol icon + colorized declaration line, plus an optional
// colorized documentation block) matching the shape that TypeScript-VS's
// legacy HoverService.cs builds from TSServer's quickinfo-full response
// (ImageElement + ClassifiedTextElement wrapped in a ContainerElement).
lsp::lsproto::VSContainerElement* buildVSHoverRawContent(
    lsp::lsproto::VSImageId* imageId,
    lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::VSClassifiedTextRun>>
        quickInfoRuns,
    lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::VSClassifiedTextRun>>
        documentationRuns) {
	if (!quickInfoRuns || quickInfoRuns->empty()) {
		return nullptr;
	}

	auto* imageElement = new lsp::lsproto::VSImageElement;
	imageElement->ImageId =
	    std::shared_ptr<lsp::lsproto::VSImageId>(
	        imageId, [](lsp::lsproto::VSImageId*) {});
	auto* qiElement = new lsp::lsproto::VSClassifiedTextElement;
	qiElement->Runs = quickInfoRuns;

	auto* displayLine = new lsp::lsproto::VSContainerElement;
	displayLine->Style = lsp::lsproto::VSContainerElementStyleWrapped;
	displayLine->Elements = std::vector<
	    lsp::lsproto::
	        VSImageElementOrClassifiedTextElementOrContainerElement>{
	    {std::shared_ptr<lsp::lsproto::VSImageElement>(
	        imageElement, [](lsp::lsproto::VSImageElement*) {}),
	     nullptr, nullptr},
	    {nullptr,
	     std::shared_ptr<lsp::lsproto::VSClassifiedTextElement>(
	         qiElement,
	         [](lsp::lsproto::VSClassifiedTextElement*) {}),
	     nullptr},
	};

	if (!documentationRuns || documentationRuns->empty()) {
		return displayLine;
	}

	auto* docElement = new lsp::lsproto::VSClassifiedTextElement;
	docElement->Runs = documentationRuns;

	auto* stacked = new lsp::lsproto::VSContainerElement;
	stacked->Style = lsp::lsproto::VSContainerElementStyleStacked;
	stacked->Elements = std::vector<
	    lsp::lsproto::
	        VSImageElementOrClassifiedTextElementOrContainerElement>{
	    {nullptr, nullptr,
	     std::shared_ptr<lsp::lsproto::VSContainerElement>(
	         displayLine, [](lsp::lsproto::VSContainerElement*) {})},
	    {nullptr,
	     std::shared_ptr<lsp::lsproto::VSClassifiedTextElement>(
	         docElement,
	         [](lsp::lsproto::VSClassifiedTextElement*) {}),
	     nullptr},
	};
	return stacked;
}

} // namespace tsc::ls
