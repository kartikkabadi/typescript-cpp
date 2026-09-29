// Port of tsc/internal/ast/{nodeflags,tokenflags,modifierflags,checkflags,
// functionflags,subtreefacts,symbolflags}.go — flag types kept as plain
// integral constants so Go-style `flags & X != 0` code ports 1:1.
#pragma once

#include <cstdint>

namespace tsc {

using NodeFlags = uint32_t;
inline constexpr NodeFlags NodeFlagsNone = 0;
inline constexpr NodeFlags NodeFlagsLet = 1 << 0;
inline constexpr NodeFlags NodeFlagsConst = 1 << 1;
inline constexpr NodeFlags NodeFlagsUsing = 1 << 2;
inline constexpr NodeFlags NodeFlagsReparsed = 1 << 3;
inline constexpr NodeFlags NodeFlagsSynthesized = 1 << 4;
inline constexpr NodeFlags NodeFlagsOptionalChain = 1 << 5;
inline constexpr NodeFlags NodeFlagsExportContext = 1 << 6;
inline constexpr NodeFlags NodeFlagsContainsThis = 1 << 7;
inline constexpr NodeFlags NodeFlagsHasImplicitReturn = 1 << 8;
inline constexpr NodeFlags NodeFlagsHasExplicitReturn = 1 << 9;
inline constexpr NodeFlags NodeFlagsDisallowInContext = 1 << 10;
inline constexpr NodeFlags NodeFlagsYieldContext = 1 << 11;
inline constexpr NodeFlags NodeFlagsDecoratorContext = 1 << 12;
inline constexpr NodeFlags NodeFlagsAwaitContext = 1 << 13;
inline constexpr NodeFlags NodeFlagsDisallowConditionalTypesContext = 1 << 14;
inline constexpr NodeFlags NodeFlagsThisNodeHasError = 1 << 15;
inline constexpr NodeFlags NodeFlagsJavaScriptFile = 1 << 16;
inline constexpr NodeFlags NodeFlagsThisNodeOrAnySubNodesHasError = 1 << 17;
inline constexpr NodeFlags NodeFlagsHasAsyncFunctions = 1 << 18;
inline constexpr NodeFlags NodeFlagsPossiblyContainsDynamicImport = 1 << 19;
inline constexpr NodeFlags NodeFlagsPossiblyContainsImportMeta = 1 << 20;
inline constexpr NodeFlags NodeFlagsHasJSDoc = 1 << 21;
inline constexpr NodeFlags NodeFlagsJSDoc = 1 << 22;
inline constexpr NodeFlags NodeFlagsAmbient = 1 << 23;
inline constexpr NodeFlags NodeFlagsInWithStatement = 1 << 24;
inline constexpr NodeFlags NodeFlagsJsonFile = 1 << 25;
inline constexpr NodeFlags NodeFlagsPossiblyContainsDeprecatedTag = 1 << 26;
inline constexpr NodeFlags NodeFlagsUnreachable = 1 << 27;
inline constexpr NodeFlags NodeFlagsReparserTransformedLiteral = 1 << 28;

inline constexpr NodeFlags NodeFlagsBlockScoped =
	NodeFlagsLet | NodeFlagsConst | NodeFlagsUsing;
inline constexpr NodeFlags NodeFlagsConstant = NodeFlagsConst | NodeFlagsUsing;
inline constexpr NodeFlags NodeFlagsAwaitUsing = NodeFlagsConst | NodeFlagsUsing;
inline constexpr NodeFlags NodeFlagsReachabilityCheckFlags =
	NodeFlagsHasImplicitReturn | NodeFlagsHasExplicitReturn;
inline constexpr NodeFlags NodeFlagsReachabilityAndEmitFlags =
	NodeFlagsReachabilityCheckFlags | NodeFlagsHasAsyncFunctions;
inline constexpr NodeFlags NodeFlagsContextFlags =
	NodeFlagsDisallowInContext | NodeFlagsDisallowConditionalTypesContext |
	NodeFlagsYieldContext | NodeFlagsDecoratorContext | NodeFlagsAwaitContext |
	NodeFlagsJavaScriptFile | NodeFlagsInWithStatement | NodeFlagsAmbient;
inline constexpr NodeFlags NodeFlagsTypeExcludesFlags =
	NodeFlagsYieldContext | NodeFlagsAwaitContext;
inline constexpr NodeFlags NodeFlagsPermanentlySetIncrementalFlags =
	NodeFlagsPossiblyContainsDynamicImport | NodeFlagsPossiblyContainsImportMeta;
inline constexpr NodeFlags NodeFlagsIdentifierHasExtendedUnicodeEscape =
	NodeFlagsContainsThis;
inline constexpr NodeFlags NodeFlagsIdentifierIsInJSDocNamespace =
	NodeFlagsHasAsyncFunctions;
inline constexpr NodeFlags NodeFlagsNestedNamespace = NodeFlagsOptionalChain;

using TokenFlags = int32_t;
inline constexpr TokenFlags TokenFlagsNone = 0;
inline constexpr TokenFlags TokenFlagsPrecedingLineBreak = 1 << 0;
inline constexpr TokenFlags TokenFlagsPrecedingJSDocComment = 1 << 1;
inline constexpr TokenFlags TokenFlagsUnterminated = 1 << 2;
inline constexpr TokenFlags TokenFlagsExtendedUnicodeEscape = 1 << 3;
inline constexpr TokenFlags TokenFlagsScientific = 1 << 4;
inline constexpr TokenFlags TokenFlagsOctal = 1 << 5;
inline constexpr TokenFlags TokenFlagsHexSpecifier = 1 << 6;
inline constexpr TokenFlags TokenFlagsBinarySpecifier = 1 << 7;
inline constexpr TokenFlags TokenFlagsOctalSpecifier = 1 << 8;
inline constexpr TokenFlags TokenFlagsContainsSeparator = 1 << 9;
inline constexpr TokenFlags TokenFlagsUnicodeEscape = 1 << 10;
inline constexpr TokenFlags TokenFlagsContainsInvalidEscape = 1 << 11;
inline constexpr TokenFlags TokenFlagsHexEscape = 1 << 12;
inline constexpr TokenFlags TokenFlagsContainsLeadingZero = 1 << 13;
inline constexpr TokenFlags TokenFlagsContainsInvalidSeparator = 1 << 14;
inline constexpr TokenFlags TokenFlagsPrecedingJSDocLeadingAsterisks = 1 << 15;
inline constexpr TokenFlags TokenFlagsSingleQuote = 1 << 16;
inline constexpr TokenFlags TokenFlagsPrecedingJSDocWithDeprecated = 1 << 17;
inline constexpr TokenFlags TokenFlagsPrecedingJSDocWithSeeOrLink = 1 << 18;
inline constexpr TokenFlags TokenFlagsBinaryOrOctalSpecifier =
	TokenFlagsBinarySpecifier | TokenFlagsOctalSpecifier;
inline constexpr TokenFlags TokenFlagsWithSpecifier =
	TokenFlagsHexSpecifier | TokenFlagsBinaryOrOctalSpecifier;
inline constexpr TokenFlags TokenFlagsStringLiteralFlags =
	TokenFlagsUnterminated | TokenFlagsHexEscape | TokenFlagsUnicodeEscape |
	TokenFlagsExtendedUnicodeEscape | TokenFlagsContainsInvalidEscape |
	TokenFlagsSingleQuote;
inline constexpr TokenFlags TokenFlagsNumericLiteralFlags =
	TokenFlagsScientific | TokenFlagsOctal | TokenFlagsContainsLeadingZero |
	TokenFlagsWithSpecifier | TokenFlagsContainsSeparator |
	TokenFlagsContainsInvalidSeparator;
inline constexpr TokenFlags TokenFlagsTemplateLiteralLikeFlags =
	TokenFlagsUnterminated | TokenFlagsHexEscape | TokenFlagsUnicodeEscape |
	TokenFlagsExtendedUnicodeEscape | TokenFlagsContainsInvalidEscape;
inline constexpr TokenFlags TokenFlagsRegularExpressionLiteralFlags =
	TokenFlagsUnterminated;
inline constexpr TokenFlags TokenFlagsIsInvalid =
	TokenFlagsOctal | TokenFlagsContainsLeadingZero |
	TokenFlagsContainsInvalidSeparator | TokenFlagsContainsInvalidEscape;

using ModifierFlags = uint32_t;
inline constexpr ModifierFlags ModifierFlagsNone = 0;
inline constexpr ModifierFlags ModifierFlagsPublic = 1 << 0;
inline constexpr ModifierFlags ModifierFlagsPrivate = 1 << 1;
inline constexpr ModifierFlags ModifierFlagsProtected = 1 << 2;
inline constexpr ModifierFlags ModifierFlagsReadonly = 1 << 3;
inline constexpr ModifierFlags ModifierFlagsOverride = 1 << 4;
inline constexpr ModifierFlags ModifierFlagsExport = 1 << 5;
inline constexpr ModifierFlags ModifierFlagsAbstract = 1 << 6;
inline constexpr ModifierFlags ModifierFlagsAmbient = 1 << 7;
inline constexpr ModifierFlags ModifierFlagsStatic = 1 << 8;
inline constexpr ModifierFlags ModifierFlagsAccessor = 1 << 9;
inline constexpr ModifierFlags ModifierFlagsAsync = 1 << 10;
inline constexpr ModifierFlags ModifierFlagsDefault = 1 << 11;
inline constexpr ModifierFlags ModifierFlagsConst = 1 << 12;
inline constexpr ModifierFlags ModifierFlagsIn = 1 << 13;
inline constexpr ModifierFlags ModifierFlagsOut = 1 << 14;
inline constexpr ModifierFlags ModifierFlagsDecorator = 1 << 15;
inline constexpr ModifierFlags ModifierFlagsDeprecated = 1 << 16;
inline constexpr ModifierFlags ModifierFlagsJSDocPublic = 1 << 23;
inline constexpr ModifierFlags ModifierFlagsJSDocPrivate = 1 << 24;
inline constexpr ModifierFlags ModifierFlagsJSDocProtected = 1 << 25;
inline constexpr ModifierFlags ModifierFlagsJSDocReadonly = 1 << 26;
inline constexpr ModifierFlags ModifierFlagsJSDocOverride = 1 << 27;
inline constexpr ModifierFlags ModifierFlagsHasComputedJSDocModifiers = 1 << 28;
inline constexpr ModifierFlags ModifierFlagsHasComputedFlags = 1 << 29;
inline constexpr ModifierFlags ModifierFlagsSyntacticOrJSDocModifiers =
	ModifierFlagsPublic | ModifierFlagsPrivate | ModifierFlagsProtected |
	ModifierFlagsReadonly | ModifierFlagsOverride;
inline constexpr ModifierFlags ModifierFlagsSyntacticOnlyModifiers =
	ModifierFlagsExport | ModifierFlagsAmbient | ModifierFlagsAbstract |
	ModifierFlagsStatic | ModifierFlagsAccessor | ModifierFlagsAsync |
	ModifierFlagsDefault | ModifierFlagsConst | ModifierFlagsIn | ModifierFlagsOut |
	ModifierFlagsDecorator;
inline constexpr ModifierFlags ModifierFlagsSyntacticModifiers =
	ModifierFlagsSyntacticOrJSDocModifiers | ModifierFlagsSyntacticOnlyModifiers;
inline constexpr ModifierFlags ModifierFlagsJSDocCacheOnlyModifiers =
	ModifierFlagsJSDocPublic | ModifierFlagsJSDocPrivate |
	ModifierFlagsJSDocProtected | ModifierFlagsJSDocReadonly |
	ModifierFlagsJSDocOverride;
inline constexpr ModifierFlags ModifierFlagsJSDocOnlyModifiers = ModifierFlagsDeprecated;
inline constexpr ModifierFlags ModifierFlagsNonCacheOnlyModifiers =
	ModifierFlagsSyntacticOrJSDocModifiers | ModifierFlagsSyntacticOnlyModifiers |
	ModifierFlagsJSDocOnlyModifiers;
inline constexpr ModifierFlags ModifierFlagsAccessibilityModifier =
	ModifierFlagsPublic | ModifierFlagsPrivate | ModifierFlagsProtected;
inline constexpr ModifierFlags ModifierFlagsParameterPropertyModifier =
	ModifierFlagsAccessibilityModifier | ModifierFlagsReadonly | ModifierFlagsOverride;
inline constexpr ModifierFlags ModifierFlagsNonPublicAccessibilityModifier =
	ModifierFlagsPrivate | ModifierFlagsProtected;
inline constexpr ModifierFlags ModifierFlagsTypeScriptModifier =
	ModifierFlagsAmbient | ModifierFlagsPublic | ModifierFlagsPrivate |
	ModifierFlagsProtected | ModifierFlagsReadonly | ModifierFlagsAbstract |
	ModifierFlagsConst | ModifierFlagsOverride | ModifierFlagsIn | ModifierFlagsOut;
inline constexpr ModifierFlags ModifierFlagsExportDefault =
	ModifierFlagsExport | ModifierFlagsDefault;
inline constexpr ModifierFlags ModifierFlagsAll =
	ModifierFlagsExport | ModifierFlagsAmbient | ModifierFlagsPublic |
	ModifierFlagsPrivate | ModifierFlagsProtected | ModifierFlagsStatic |
	ModifierFlagsReadonly | ModifierFlagsAbstract | ModifierFlagsAccessor |
	ModifierFlagsAsync | ModifierFlagsDefault | ModifierFlagsConst |
	ModifierFlagsDeprecated | ModifierFlagsOverride | ModifierFlagsIn |
	ModifierFlagsOut | ModifierFlagsDecorator;
inline constexpr ModifierFlags ModifierFlagsModifier = ModifierFlagsAll & ~ModifierFlagsDecorator;
inline constexpr ModifierFlags ModifierFlagsJavaScript =
	ModifierFlagsExport | ModifierFlagsStatic | ModifierFlagsAccessor |
	ModifierFlagsAsync | ModifierFlagsDefault;

using CheckFlags = uint32_t;
inline constexpr CheckFlags CheckFlagsNone = 0;
inline constexpr CheckFlags CheckFlagsInstantiated = 1 << 0;
inline constexpr CheckFlags CheckFlagsSyntheticProperty = 1 << 1;
inline constexpr CheckFlags CheckFlagsSyntheticMethod = 1 << 2;
inline constexpr CheckFlags CheckFlagsReadonly = 1 << 3;
inline constexpr CheckFlags CheckFlagsReadPartial = 1 << 4;
inline constexpr CheckFlags CheckFlagsWritePartial = 1 << 5;
inline constexpr CheckFlags CheckFlagsHasNonUniformType = 1 << 6;
inline constexpr CheckFlags CheckFlagsHasLiteralType = 1 << 7;
inline constexpr CheckFlags CheckFlagsContainsPublic = 1 << 8;
inline constexpr CheckFlags CheckFlagsContainsProtected = 1 << 9;
inline constexpr CheckFlags CheckFlagsContainsPrivate = 1 << 10;
inline constexpr CheckFlags CheckFlagsContainsWritePublic = 1 << 11;
inline constexpr CheckFlags CheckFlagsContainsWriteProtected = 1 << 12;
inline constexpr CheckFlags CheckFlagsContainsWritePrivate = 1 << 13;
inline constexpr CheckFlags CheckFlagsContainsStatic = 1 << 14;
inline constexpr CheckFlags CheckFlagsLate = 1 << 15;
inline constexpr CheckFlags CheckFlagsReverseMapped = 1 << 16;
inline constexpr CheckFlags CheckFlagsOptionalParameter = 1 << 17;
inline constexpr CheckFlags CheckFlagsRestParameter = 1 << 18;
inline constexpr CheckFlags CheckFlagsDeferredType = 1 << 19;
inline constexpr CheckFlags CheckFlagsHasNeverType = 1 << 20;
inline constexpr CheckFlags CheckFlagsMapped = 1 << 21;
inline constexpr CheckFlags CheckFlagsStripOptional = 1 << 22;
inline constexpr CheckFlags CheckFlagsUnresolved = 1 << 23;
inline constexpr CheckFlags CheckFlagsIsDiscriminantComputed = 1 << 24;
inline constexpr CheckFlags CheckFlagsIsDiscriminant = 1 << 25;
inline constexpr CheckFlags CheckFlagsIndexSymbol = 1 << 26;
inline constexpr CheckFlags CheckFlagsSynthetic =
	CheckFlagsSyntheticProperty | CheckFlagsSyntheticMethod;
inline constexpr CheckFlags CheckFlagsNonUniformAndLiteral =
	CheckFlagsHasNonUniformType | CheckFlagsHasLiteralType;
inline constexpr CheckFlags CheckFlagsPartial =
	CheckFlagsReadPartial | CheckFlagsWritePartial;

using FunctionFlags = uint32_t;
inline constexpr FunctionFlags FunctionFlagsNormal = 0;
inline constexpr FunctionFlags FunctionFlagsGenerator = 1 << 0;
inline constexpr FunctionFlags FunctionFlagsAsync = 1 << 1;
inline constexpr FunctionFlags FunctionFlagsInvalid = 1 << 2;
inline constexpr FunctionFlags FunctionFlagsAsyncGenerator =
	FunctionFlagsAsync | FunctionFlagsGenerator;

using SubtreeFacts = uint32_t;
inline constexpr SubtreeFacts SubtreeFactsNone = 0;
inline constexpr SubtreeFacts SubtreeContainsTypeScript = 1 << 0;
inline constexpr SubtreeFacts SubtreeContainsJsx = 1 << 1;
inline constexpr SubtreeFacts SubtreeContainsESDecorators = 1 << 2;
inline constexpr SubtreeFacts SubtreeContainsUsing = 1 << 3;
inline constexpr SubtreeFacts SubtreeContainsClassStaticBlocks = 1 << 4;
inline constexpr SubtreeFacts SubtreeContainsESClassFields = 1 << 5;
inline constexpr SubtreeFacts SubtreeContainsLogicalAssignments = 1 << 6;
inline constexpr SubtreeFacts SubtreeContainsNullishCoalescing = 1 << 7;
inline constexpr SubtreeFacts SubtreeContainsOptionalChaining = 1 << 8;
inline constexpr SubtreeFacts SubtreeContainsMissingCatchClauseVariable = 1 << 9;
inline constexpr SubtreeFacts SubtreeContainsESObjectRestOrSpread = 1 << 10;
inline constexpr SubtreeFacts SubtreeContainsForAwaitOrAsyncGenerator = 1 << 11;
inline constexpr SubtreeFacts SubtreeContainsAnyAwait = 1 << 12;
inline constexpr SubtreeFacts SubtreeContainsExponentiationOperator = 1 << 13;
inline constexpr SubtreeFacts SubtreeContainsLexicalThis = 1 << 14;
inline constexpr SubtreeFacts SubtreeContainsLexicalSuper = 1 << 15;
inline constexpr SubtreeFacts SubtreeContainsRestOrSpread = 1 << 16;
inline constexpr SubtreeFacts SubtreeContainsObjectRestOrSpread = 1 << 17;
inline constexpr SubtreeFacts SubtreeContainsAwait = 1 << 18;
inline constexpr SubtreeFacts SubtreeContainsDynamicImport = 1 << 19;
inline constexpr SubtreeFacts SubtreeContainsClassFields = 1 << 20;
inline constexpr SubtreeFacts SubtreeContainsDecorators = 1 << 21;
inline constexpr SubtreeFacts SubtreeContainsIdentifier = 1 << 22;
inline constexpr SubtreeFacts SubtreeContainsPrivateIdentifierInExpression = 1 << 23;
inline constexpr SubtreeFacts SubtreeContainsInvalidTemplateEscape = 1 << 24;
inline constexpr SubtreeFacts SubtreeFactsComputed = 1 << 25;

inline constexpr SubtreeFacts SubtreeExclusionsNode = SubtreeFactsComputed;
inline constexpr SubtreeFacts SubtreeExclusionsEraseable = ~SubtreeContainsTypeScript;
inline constexpr SubtreeFacts SubtreeExclusionsOuterExpression = SubtreeExclusionsNode;
inline constexpr SubtreeFacts SubtreeExclusionsPropertyAccess = SubtreeExclusionsNode;
inline constexpr SubtreeFacts SubtreeExclusionsElementAccess = SubtreeExclusionsNode;
inline constexpr SubtreeFacts SubtreeExclusionsArrowFunction =
	SubtreeExclusionsNode | SubtreeContainsAwait | SubtreeContainsObjectRestOrSpread;
inline constexpr SubtreeFacts SubtreeExclusionsFunction =
	SubtreeExclusionsNode | SubtreeContainsLexicalThis | SubtreeContainsLexicalSuper |
	SubtreeContainsAwait | SubtreeContainsObjectRestOrSpread;
inline constexpr SubtreeFacts SubtreeExclusionsConstructor = SubtreeExclusionsFunction;
inline constexpr SubtreeFacts SubtreeExclusionsMethod = SubtreeExclusionsFunction;
inline constexpr SubtreeFacts SubtreeExclusionsAccessor = SubtreeExclusionsFunction;
inline constexpr SubtreeFacts SubtreeExclusionsProperty =
	SubtreeExclusionsNode | SubtreeContainsLexicalThis | SubtreeContainsLexicalSuper;
inline constexpr SubtreeFacts SubtreeExclusionsClass = SubtreeExclusionsNode;
inline constexpr SubtreeFacts SubtreeExclusionsModule =
	SubtreeExclusionsNode | SubtreeContainsLexicalThis | SubtreeContainsLexicalSuper;
inline constexpr SubtreeFacts SubtreeExclusionsObjectLiteral =
	SubtreeExclusionsNode | SubtreeContainsObjectRestOrSpread;
inline constexpr SubtreeFacts SubtreeExclusionsArrayLiteral = SubtreeExclusionsNode;
inline constexpr SubtreeFacts SubtreeExclusionsCall = SubtreeExclusionsNode;
inline constexpr SubtreeFacts SubtreeExclusionsNew = SubtreeExclusionsNode;
inline constexpr SubtreeFacts SubtreeExclusionsVariableDeclarationList =
	SubtreeExclusionsNode | SubtreeContainsObjectRestOrSpread;
inline constexpr SubtreeFacts SubtreeExclusionsParameter = SubtreeExclusionsNode;
inline constexpr SubtreeFacts SubtreeExclusionsCatchClause =
	SubtreeExclusionsNode | SubtreeContainsObjectRestOrSpread;
inline constexpr SubtreeFacts SubtreeExclusionsBindingPattern =
	SubtreeExclusionsNode | SubtreeContainsRestOrSpread;
inline constexpr SubtreeFacts SubtreeContainsLexicalThisOrSuper =
	SubtreeContainsLexicalThis | SubtreeContainsLexicalSuper;

using SymbolFlags = uint32_t;
inline constexpr SymbolFlags SymbolFlagsNone = 0;
inline constexpr SymbolFlags SymbolFlagsFunctionScopedVariable = 1 << 0;
inline constexpr SymbolFlags SymbolFlagsBlockScopedVariable = 1 << 1;
inline constexpr SymbolFlags SymbolFlagsProperty = 1 << 2;
inline constexpr SymbolFlags SymbolFlagsEnumMember = 1 << 3;
inline constexpr SymbolFlags SymbolFlagsFunction = 1 << 4;
inline constexpr SymbolFlags SymbolFlagsClass = 1 << 5;
inline constexpr SymbolFlags SymbolFlagsInterface = 1 << 6;
inline constexpr SymbolFlags SymbolFlagsConstEnum = 1 << 7;
inline constexpr SymbolFlags SymbolFlagsRegularEnum = 1 << 8;
inline constexpr SymbolFlags SymbolFlagsValueModule = 1 << 9;
inline constexpr SymbolFlags SymbolFlagsNamespaceModule = 1 << 10;
inline constexpr SymbolFlags SymbolFlagsTypeLiteral = 1 << 11;
inline constexpr SymbolFlags SymbolFlagsObjectLiteral = 1 << 12;
inline constexpr SymbolFlags SymbolFlagsMethod = 1 << 13;
inline constexpr SymbolFlags SymbolFlagsConstructor = 1 << 14;
inline constexpr SymbolFlags SymbolFlagsGetAccessor = 1 << 15;
inline constexpr SymbolFlags SymbolFlagsSetAccessor = 1 << 16;
inline constexpr SymbolFlags SymbolFlagsSignature = 1 << 17;
inline constexpr SymbolFlags SymbolFlagsTypeParameter = 1 << 18;
inline constexpr SymbolFlags SymbolFlagsTypeAlias = 1 << 19;
inline constexpr SymbolFlags SymbolFlagsExportValue = 1 << 20;
inline constexpr SymbolFlags SymbolFlagsAlias = 1 << 21;
inline constexpr SymbolFlags SymbolFlagsPrototype = 1 << 22;
inline constexpr SymbolFlags SymbolFlagsExportStar = 1 << 23;
inline constexpr SymbolFlags SymbolFlagsOptional = 1 << 24;
inline constexpr SymbolFlags SymbolFlagsTransient = 1 << 25;
inline constexpr SymbolFlags SymbolFlagsAssignment = 1 << 26;
inline constexpr SymbolFlags SymbolFlagsModuleExports = 1 << 27;
inline constexpr SymbolFlags SymbolFlagsConstEnumOnlyModule = 1 << 28;
inline constexpr SymbolFlags SymbolFlagsReplaceableByMethod = 1 << 29;
inline constexpr SymbolFlags SymbolFlagsGlobalLookup = 1 << 30;
inline constexpr SymbolFlags SymbolFlagsAll = (1 << 30) - 1;
inline constexpr SymbolFlags SymbolFlagsEnum =
	SymbolFlagsRegularEnum | SymbolFlagsConstEnum;
inline constexpr SymbolFlags SymbolFlagsVariable =
	SymbolFlagsFunctionScopedVariable | SymbolFlagsBlockScopedVariable;
inline constexpr SymbolFlags SymbolFlagsValue =
	SymbolFlagsVariable | SymbolFlagsProperty | SymbolFlagsEnumMember |
	SymbolFlagsObjectLiteral | SymbolFlagsFunction | SymbolFlagsClass |
	SymbolFlagsEnum | SymbolFlagsValueModule | SymbolFlagsMethod |
	SymbolFlagsGetAccessor | SymbolFlagsSetAccessor;
inline constexpr SymbolFlags SymbolFlagsType =
	SymbolFlagsClass | SymbolFlagsInterface | SymbolFlagsEnum |
	SymbolFlagsEnumMember | SymbolFlagsTypeLiteral | SymbolFlagsTypeParameter |
	SymbolFlagsTypeAlias;
inline constexpr SymbolFlags SymbolFlagsNamespace =
	SymbolFlagsValueModule | SymbolFlagsNamespaceModule | SymbolFlagsEnum;
inline constexpr SymbolFlags SymbolFlagsModule =
	SymbolFlagsValueModule | SymbolFlagsNamespaceModule;
inline constexpr SymbolFlags SymbolFlagsAccessor =
	SymbolFlagsGetAccessor | SymbolFlagsSetAccessor;

}  // namespace tsc
