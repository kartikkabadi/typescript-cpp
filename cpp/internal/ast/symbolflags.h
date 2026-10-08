// Port of tsc/internal/ast/symbolflags.go — composite/excludes constants.
// Base SymbolFlags constants live in flags.h.
#pragma once
#include <cstdint>

#include "internal/ast/flags.h"

namespace tsc {

inline constexpr SymbolFlags SymbolFlagsFunctionScopedVariableExcludes =
	SymbolFlagsValue & ~SymbolFlagsFunctionScopedVariable;
inline constexpr SymbolFlags SymbolFlagsBlockScopedVariableExcludes =
	SymbolFlagsValue;
inline constexpr SymbolFlags SymbolFlagsParameterExcludes = SymbolFlagsValue;
inline constexpr SymbolFlags SymbolFlagsPropertyExcludes =
	SymbolFlagsValue & ~(SymbolFlagsProperty | SymbolFlagsAccessor);
inline constexpr SymbolFlags SymbolFlagsEnumMemberExcludes =
	SymbolFlagsValue | SymbolFlagsType;
inline constexpr SymbolFlags SymbolFlagsFunctionExcludes =
	SymbolFlagsValue &
	~(SymbolFlagsFunction | SymbolFlagsValueModule | SymbolFlagsClass);
inline constexpr SymbolFlags SymbolFlagsClassExcludes =
	(SymbolFlagsValue | SymbolFlagsType) &
	~(SymbolFlagsValueModule | SymbolFlagsInterface | SymbolFlagsFunction);
inline constexpr SymbolFlags SymbolFlagsInterfaceExcludes =
	SymbolFlagsType & ~(SymbolFlagsInterface | SymbolFlagsClass);
inline constexpr SymbolFlags SymbolFlagsRegularEnumExcludes =
	(SymbolFlagsValue | SymbolFlagsType) &
	~(SymbolFlagsRegularEnum | SymbolFlagsValueModule);
inline constexpr SymbolFlags SymbolFlagsConstEnumExcludes =
	(SymbolFlagsValue | SymbolFlagsType) & ~SymbolFlagsConstEnum;
inline constexpr SymbolFlags SymbolFlagsValueModuleExcludes =
	SymbolFlagsValue &
	~(SymbolFlagsFunction | SymbolFlagsClass | SymbolFlagsRegularEnum |
	  SymbolFlagsValueModule);
inline constexpr SymbolFlags SymbolFlagsNamespaceModuleExcludes =
	SymbolFlagsNone;
inline constexpr SymbolFlags SymbolFlagsMethodExcludes =
	SymbolFlagsValue & ~SymbolFlagsMethod;
inline constexpr SymbolFlags SymbolFlagsGetAccessorExcludes =
	SymbolFlagsValue & ~(SymbolFlagsSetAccessor | SymbolFlagsProperty);
inline constexpr SymbolFlags SymbolFlagsSetAccessorExcludes =
	SymbolFlagsValue & ~(SymbolFlagsGetAccessor | SymbolFlagsProperty);
inline constexpr SymbolFlags SymbolFlagsAccessorExcludes =
	SymbolFlagsValue & ~SymbolFlagsProperty;
inline constexpr SymbolFlags SymbolFlagsTypeParameterExcludes =
	SymbolFlagsType & ~SymbolFlagsTypeParameter;
inline constexpr SymbolFlags SymbolFlagsTypeAliasExcludes = SymbolFlagsType;
inline constexpr SymbolFlags SymbolFlagsAliasExcludes = SymbolFlagsAlias;
inline constexpr SymbolFlags SymbolFlagsModuleMember =
	SymbolFlagsVariable | SymbolFlagsFunction | SymbolFlagsClass |
	SymbolFlagsInterface | SymbolFlagsEnum | SymbolFlagsModule |
	SymbolFlagsTypeAlias | SymbolFlagsAlias;
inline constexpr SymbolFlags SymbolFlagsExportHasLocal =
	SymbolFlagsFunction | SymbolFlagsClass | SymbolFlagsEnum |
	SymbolFlagsValueModule;
inline constexpr SymbolFlags SymbolFlagsBlockScoped =
	SymbolFlagsBlockScopedVariable | SymbolFlagsClass | SymbolFlagsEnum;
inline constexpr SymbolFlags SymbolFlagsPropertyOrAccessor =
	SymbolFlagsProperty | SymbolFlagsAccessor;
inline constexpr SymbolFlags SymbolFlagsClassMember =
	SymbolFlagsMethod | SymbolFlagsAccessor | SymbolFlagsProperty;
inline constexpr SymbolFlags SymbolFlagsExportSupportsDefaultModifier =
	SymbolFlagsClass | SymbolFlagsFunction | SymbolFlagsInterface;
inline constexpr SymbolFlags SymbolFlagsExportDoesNotSupportDefaultModifier =
	~SymbolFlagsExportSupportsDefaultModifier;
inline constexpr SymbolFlags SymbolFlagsLateBindingContainer =
	SymbolFlagsClass | SymbolFlagsInterface | SymbolFlagsTypeLiteral |
	SymbolFlagsObjectLiteral | SymbolFlagsFunction;

// ast/utilities.go: ModuleInstanceState
enum class ModuleInstanceState : int32_t {
	Unknown = 0,
	NonInstantiated = 1,
	Instantiated = 2,
	ConstEnumOnly = 3,
};

// === slice: ls-coreA ===

// utilities.go:2240 — SemanticMeaning (plain int32 + consts, like
// SymbolFlags, so `&`/`|` work on values).
using SemanticMeaning = int32_t;
inline constexpr SemanticMeaning SemanticMeaningNone = 0;
inline constexpr SemanticMeaning SemanticMeaningValue = 1 << 0;
inline constexpr SemanticMeaning SemanticMeaningType = 1 << 1;
inline constexpr SemanticMeaning SemanticMeaningNamespace = 1 << 2;
inline constexpr SemanticMeaning SemanticMeaningAll =
	SemanticMeaningValue | SemanticMeaningType | SemanticMeaningNamespace;

} // namespace tsc
