// Port of tsc/internal/transformers/declarations — transform.go (public
// emit-path surface), tracker.go (SymbolTrackerImpl /
// SymbolTrackerSharedState), diagnostics.go (symbol-accessibility diagnostic
// factories), supplementalreferences.go, util.go.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/visitor.h"
#include "internal/checker/checker.h"
#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/nodebuilder/types.h"
#include "internal/printer/emitcontext.h"
#include "internal/scanner/scanner.h"
#include "internal/transformers/transformers.h"

// printer::EmitResolver is a using-alias for checker::EmitResolver inside
// tsc::printer (the emit slice's printer headers will reconcile the same
// alias; a duplicated identical alias is legal C++).
namespace tsc::printer {
using EmitResolver = ::tsc::checker::EmitResolver;
}

namespace tsc::transformers::declarations {

// ---------------------------------------------------------------------------
// transform.go — public emit-path surface (contract: signatures are fixed)
// ---------------------------------------------------------------------------

// transform.go:28 OutputPaths
struct OutputPaths {
	virtual ~OutputPaths() = default;
	virtual std::string DeclarationFilePath() = 0;
	virtual std::string JsFilePath() = 0;
};

// transform.go:34 DeclarationEmitHost — Go embeds
// modulespecifiers.ModuleSpecifierGenerationHost; the C++ concrete host is
// checker::Program (same mapping modulespecifiers uses).
struct DeclarationEmitHost : checker::Program {
	virtual SourceFile* GetSourceFileFromReference(SourceFile* origin,
	                                               FileReference* ref) = 0;
	virtual OutputPaths* GetOutputPathsFor(SourceFile* file,
	                                       bool forceDtsPaths) = 0;
	virtual bool SourceFileMayBeEmitted(SourceFile* file,
	                                    bool forceDtsEmit) = 0;
	virtual ModifierFlags GetEffectiveDeclarationFlags(
	    Node* node, ModifierFlags flags) = 0;
	virtual printer::EmitResolver* GetEmitResolver() = 0;
};

// transform.go:57/63 DeclarationTransformer — public emit-path surface.
struct DeclarationTransformer : transformers::Transformer {
	virtual ~DeclarationTransformer() = default;
	virtual SourceFile* TransformSourceFile(SourceFile* file) = 0;
	virtual std::vector<Diagnostic*> GetDiagnostics() = 0;
};

// transform.go:103 NewDeclarationTransformer
DeclarationTransformer* NewDeclarationTransformer(
    DeclarationEmitHost* host, printer::EmitContext* emitContext,
    const CompilerOptions* options, std::string_view declarationFilePath,
    std::string_view declarationMapPath);

// supplementalreferences.go:19 NewSupplementalReferencesTransformer
DeclarationTransformer* NewSupplementalReferencesTransformer(
    DeclarationEmitHost* host, SourceFile* sourceFile,
    std::string_view declarationFilePath, bool forceDeclarationPaths);

// ---------------------------------------------------------------------------
// diagnostics.go — symbol accessibility diagnostic plumbing (package-internal)
// ---------------------------------------------------------------------------

// diagnostics.go:12
struct SymbolAccessibilityDiagnostic {
	Node* errorNode = nullptr;
	const DiagnosticMessage* diagnosticMessage = nullptr;
	Node* typeName = nullptr;
};

// diagnostics.go:16
using GetSymbolAccessibilityDiagnostic =
	std::function<SymbolAccessibilityDiagnostic*(
		printer::SymbolAccessibilityResult&)>;

// diagnostics.go:201 — creates a diagnostic for a specific node.
// diagnostics.go:386 — version used when the enclosing declaration is a name.
GetSymbolAccessibilityDiagnostic createGetSymbolAccessibilityDiagnosticForNode(
	Node* node);
GetSymbolAccessibilityDiagnostic
createGetSymbolAccessibilityDiagnosticForNodeName(Node* node);

// diagnostics.go:720
std::function<Diagnostic*(Node*)> createGetIsolatedDeclarationErrors(
	printer::EmitResolver* resolver);

// tracker.go:235 createDiagnosticForNode
Diagnostic* createDiagnosticForNode(
	Node* node, const DiagnosticMessage* message,
	const std::vector<std::string>& args = {});

// ---------------------------------------------------------------------------
// tracker.go
// ---------------------------------------------------------------------------

// tracker.go:239 SymbolTrackerSharedState
struct SymbolTrackerSharedState {
	std::vector<Node*> lateMarkedStatements;
	std::vector<Diagnostic*> diagnostics;
	GetSymbolAccessibilityDiagnostic getSymbolAccessibilityDiagnostic;
	Node* errorNameNode = nullptr;
	bool isolatedDeclarations = false;
	bool stripInternal = false;
	SourceFile* currentSourceFile = nullptr;
	printer::EmitResolver* resolver = nullptr;
	std::function<void(Node*)> reportExpandoFunctionErrors;

	void addDiagnostic(Diagnostic* diag);
};

// tracker.go:12 SymbolTrackerImpl — implements nodebuilder::SymbolTracker.
struct SymbolTrackerImpl : nodebuilder::SymbolTracker {
	printer::EmitResolver* resolver = nullptr;
	SymbolTrackerSharedState* state = nullptr;
	DeclarationEmitHost* host = nullptr;
	std::vector<Node*> fallbackStack;

	// For detecting class expression self-references during member
	// serialization. When set, TrackSymbol records usage without reporting
	// accessibility errors.
	Symbol* watchedClassSymbol = nullptr;
	bool classSymbolTracked = false;

	std::function<Diagnostic*(Node*)> getIsolatedDeclarationError;

	// nodebuilder::SymbolTracker
	void PopErrorFallbackNode() override;
	void PushErrorFallbackNode(Node* node) override;
	void ReportCyclicStructureError() override;
	void ReportInaccessibleThisError() override;
	void ReportInaccessibleUniqueSymbolError() override;
	void ReportInferenceFallback(Node* node) override;
	void ReportLikelyUnsafeImportRequiredError(
	    const std::string& specifier,
	    const std::string& symbolName) override;
	void ReportNonSerializableProperty(
	    const std::string& propertyName) override;
	void ReportNonlocalAugmentation(SourceFile* containingFile,
	                                Symbol* parentSymbol,
	                                Symbol* augmentingSymbol) override;
	void ReportPrivateInBaseOfClassExpression(
	    const std::string& propertyName) override;
	void ReportTruncationError() override;
	bool TrackSymbol(Symbol* symbol, Node* enclosingDeclaration,
	                 SymbolFlags meaning) override;

	bool isBoundExpando(Node* node);
	bool isChildOfBoundExpando(Node* node);
	Node* errorFallbackNode();
	Node* errorLocation();
	std::string errorDeclarationNameWithFallback();
	bool handleSymbolAccessibilityError(
		printer::SymbolAccessibilityResult& symbolAccessibilityResult);
};

// tracker.go:255 NewSymbolTracker
SymbolTrackerImpl* NewSymbolTracker(DeclarationEmitHost* host,
                                    printer::EmitResolver* resolver,
                                    SymbolTrackerSharedState* state);

// ---------------------------------------------------------------------------
// util.go
// ---------------------------------------------------------------------------

bool needsScopeMarker(Node* result);
bool canHaveLiteralInitializer(DeclarationEmitHost* host, Node* node);
bool canProduceDiagnostics(Node* node);
bool canReuseModifierNodes(const std::vector<Node*>& nodes);
bool isDeclarationAndNotVisible(printer::EmitContext* emitContext,
                                printer::EmitResolver* resolver, Node* node);
bool getBindingNameVisible(printer::EmitResolver* resolver, Node* elem);
bool isEnclosingDeclaration(Node* node);
bool isAlwaysType(Node* node);
ModifierFlags maskModifierFlags(Node* node, ModifierFlags modifierMask,
                                ModifierFlags modifierAdditions);
Node* unwrapParenthesizedExpression(Node* o);
bool isPrivateMethodTypeParameter(DeclarationEmitHost* host,
                                  TypeParameterDeclaration* node);
bool shouldEmitFunctionProperties(FunctionDeclaration* input);
Node* getEffectiveBaseTypeNode(Node* node);
bool isScopeMarker(Node* node);
bool hasScopeMarker(NodeList* statements);

}  // namespace tsc::transformers::declarations
