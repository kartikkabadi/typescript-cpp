// Port of tsc/internal/nodebuilder/types.go — Flags, InternalFlags, and the
// SymbolTracker interface. Concrete implementations live on top of the
// checker; these types are shared with the emit resolver in the printer.
#pragma once

#include "internal/ast/ast.h"

#include <cstdint>

namespace tsc::nodebuilder {

// SymbolTracker (types.go).
struct SymbolTracker {
	virtual ~SymbolTracker() = default;
	virtual bool TrackSymbol(Symbol* symbol, Node* enclosingDeclaration,
	                         SymbolFlags meaning) = 0;
	virtual void ReportInaccessibleThisError() = 0;
	virtual void ReportPrivateInBaseOfClassExpression(
		const std::string& propertyName) = 0;
	virtual void ReportInaccessibleUniqueSymbolError() = 0;
	virtual void ReportCyclicStructureError() = 0;
	virtual void ReportLikelyUnsafeImportRequiredError(
		const std::string& specifier, const std::string& symbolName) = 0;
	virtual void ReportTruncationError() = 0;
	virtual void ReportNonlocalAugmentation(SourceFile* containingFile,
	                                        Symbol* parentSymbol,
	                                        Symbol* augmentingSymbol) = 0;
	virtual void ReportNonSerializableProperty(
		const std::string& propertyName) = 0;
	virtual void ReportInferenceFallback(Node* node) = 0;
	virtual void PushErrorFallbackNode(Node* node) = 0;
	virtual void PopErrorFallbackNode() = 0;
};

// Flags (types.go). NOTE: If modifying this enum, must modify TypeFormatFlags
// too!
using Flags = uint32_t;

inline constexpr Flags FlagsNone = 0;
// Options
inline constexpr Flags FlagsNoTruncation = 1 << 0;
inline constexpr Flags FlagsWriteArrayAsGenericType = 1 << 1;
inline constexpr Flags FlagsGenerateNamesForShadowedTypeParams = 1 << 2;
inline constexpr Flags FlagsUseStructuralFallback = 1 << 3;
inline constexpr Flags FlagsForbidIndexedAccessSymbolReferences = 1 << 4;
inline constexpr Flags FlagsWriteTypeArgumentsOfSignature = 1 << 5;
inline constexpr Flags FlagsUseFullyQualifiedType = 1 << 6;
inline constexpr Flags FlagsUseOnlyExternalAliasing = 1 << 7;
inline constexpr Flags FlagsSuppressAnyReturnType = 1 << 8;
inline constexpr Flags FlagsWriteTypeParametersInQualifiedName = 1 << 9;
inline constexpr Flags FlagsMultilineObjectLiterals = 1 << 10;
inline constexpr Flags FlagsWriteClassExpressionAsTypeLiteral = 1 << 11;
inline constexpr Flags FlagsUseTypeOfFunction = 1 << 12;
inline constexpr Flags FlagsOmitParameterModifiers = 1 << 13;
inline constexpr Flags FlagsUseAliasDefinedOutsideCurrentScope = 1 << 14;
inline constexpr Flags FlagsUseSingleQuotesForStringLiteralType = 1u << 28;
inline constexpr Flags FlagsNoTypeReduction = 1u << 29;
inline constexpr Flags FlagsUseInstantiationExpressions = 1u << 30;
inline constexpr Flags FlagsOmitThisParameter = 1 << 25;
inline constexpr Flags FlagsWriteCallStyleSignature = 1 << 27;
// Error handling
inline constexpr Flags FlagsAllowThisInObjectLiteral = 1 << 15;
inline constexpr Flags FlagsAllowQualifiedNameInPlaceOfIdentifier = 1 << 16;
inline constexpr Flags FlagsAllowAnonymousIdentifier = 1 << 17;
inline constexpr Flags FlagsAllowEmptyUnionOrIntersection = 1 << 18;
inline constexpr Flags FlagsAllowEmptyTuple = 1 << 19;
inline constexpr Flags FlagsAllowUniqueESSymbolType = 1 << 20;
inline constexpr Flags FlagsAllowEmptyIndexInfoType = 1 << 21;
// Errors (cont.)
inline constexpr Flags FlagsAllowNodeModulesRelativePaths = 1 << 26;
inline constexpr Flags FlagsIgnoreErrors =
	FlagsAllowThisInObjectLiteral | FlagsAllowQualifiedNameInPlaceOfIdentifier |
	FlagsAllowAnonymousIdentifier | FlagsAllowEmptyUnionOrIntersection |
	FlagsAllowEmptyTuple | FlagsAllowEmptyIndexInfoType |
	FlagsAllowNodeModulesRelativePaths;
// State
inline constexpr Flags FlagsInObjectTypeLiteral = 1 << 22;
inline constexpr Flags FlagsInTypeAlias = 1 << 23;
inline constexpr Flags FlagsInInitialEntityName = 1 << 24;

// InternalFlags (types.go) — @internal.
using InternalFlags = int32_t;

inline constexpr InternalFlags InternalFlagsNone = 0;
inline constexpr InternalFlags InternalFlagsWriteComputedProps = 1 << 0;
inline constexpr InternalFlags InternalFlagsNoSyntacticPrinter = 1 << 1;
inline constexpr InternalFlags InternalFlagsDoNotIncludeSymbolChain = 1 << 2;
inline constexpr InternalFlags InternalFlagsAllowUnresolvedNames = 1 << 3;

}  // namespace tsc::nodebuilder
