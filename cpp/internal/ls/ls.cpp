// === slice: ls-coreC ===
// ls.cpp — languageservice.go (shared file: the LanguageService struct's
// base methods are ported for real here) plus the dep-stub definitions for
// every sibling-owned symbol ls.h declares.
#include "internal/ls/ls.h"

#include "internal/vfs/vfsmatch/vfsmatch.h"

namespace tsc::ls {

// ============================================================================
// languageservice.go — real port of the trivial accessors/host delegations.
// ============================================================================

// languageservice.go:30 — NewLanguageService (constructor)
LanguageService::LanguageService(autoimport::ProjectID projectID, ::tsc::ls::Host* host,
								 compiler::SimpleProgram* program, SourceFile* activeFile)
	: projectID(std::move(projectID)),
	  host(host),
	  program(program),
	  converters(host->Converters()),
	  activeConfig(host->GetPreferences(activeFile)),
	  documentPositionMappers() {}

// languageservice.go:41 — toPath
tspath::Path LanguageService::toPath(std::string_view fileName) {
	return tspath::toPath(fileName, program->GetCurrentDirectory(), UseCaseSensitiveFileNames());
}

// languageservice.go:57 — tryGetProgramAndFile
std::pair<compiler::SimpleProgram*, SourceFile*> LanguageService::tryGetProgramAndFile(
	std::string_view fileName) {
	compiler::SimpleProgram* program = GetProgram();
	SourceFile* file = program->GetSourceFile(std::string(fileName));
	return {program, file};
}

// languageservice.go:63 — getProgramAndFile (panics when the file is missing)
std::pair<compiler::SimpleProgram*, SourceFile*> LanguageService::getProgramAndFile(
	lsp::lsproto::DocumentUri documentURI) {
	std::string fileName = documentURI.FileName();
	auto [program, file] = tryGetProgramAndFile(fileName);
	if (file == nullptr) {
		TSC_UNREACHABLE(("file not found: " + fileName).c_str());
	}
	return {program, file};
}

// languageservice.go:72 — GetDocumentPositionMapper
sourcemap::DocumentPositionMapper* LanguageService::GetDocumentPositionMapper(
	std::string_view fileName) {
	std::string key(fileName);
	sourcemap::DocumentPositionMapper* d;
	auto it = documentPositionMappers.find(key);
	if (it == documentPositionMappers.end()) {
		d = sourcemap::GetDocumentPositionMapper(this, fileName);
		documentPositionMappers[key] = d;
	} else {
		d = it->second;
	}
	return d;
}

// languageservice.go:81 — ReadFile (sourcemap::Host)
std::pair<std::string, bool> LanguageService::ReadFile(std::string_view fileName) {
	return host->ReadFile(fileName);
}

// languageservice.go:85 — UseCaseSensitiveFileNames (sourcemap::Host)
bool LanguageService::UseCaseSensitiveFileNames() {
	return host->UseCaseSensitiveFileNames();
}

// languageservice.go:89 — GetECMALineInfo (sourcemap::Host)
sourcemap::ECMALineInfo* LanguageService::GetECMALineInfo(std::string_view fileName) {
	return host->GetECMALineInfo(fileName);
}

// languageservice.go:123 — DirectoryExists
bool LanguageService::DirectoryExists(std::string_view path) {
	return host->DirectoryExists(path);
}

// languageservice.go:128 — ReadDirectory (module specifier completions)
std::vector<std::string> LanguageService::ReadDirectory(std::string_view path,
														std::vector<std::string> extensions,
														std::vector<std::string> includes) {
	return host->ReadDirectory(program->GetCurrentDirectory(), path, std::move(extensions),
							   nullptr /*excludes*/, std::move(includes), vfs::vfsmatch::UnlimitedDepth);
}

// languageservice.go:132 — GetDirectories
std::vector<std::string> LanguageService::GetDirectories(std::string_view path) {
	return host->GetDirectories(path);
}

// === dep stubs — removed when owner slice lands ===

// languageservice.go:95 — autoimport slice
autoimport::View* LanguageService::getPreparedAutoImportView(SourceFile* fromFile,
															 checker::Checker* typeChecker) {
	TSC_UNREACHABLE("getPreparedAutoImportView — owned by autoimport slice");
}

// languageservice.go:111 — autoimport slice
autoimport::View* LanguageService::getCurrentAutoImportView(SourceFile* fromFile,
															checker::Checker* typeChecker) {
	TSC_UNREACHABLE("getCurrentAutoImportView — owned by autoimport slice");
}

// --- source_map.go — owned by ls-coreA ---
std::pair<lsp::lsproto::Location, spanmap::Fidelity> LanguageService::sourceFileRangeToLSPLocation(
	SourceFile* file, TextRange fileRange) {
	TSC_UNREACHABLE("sourceFileRangeToLSPLocation — owned by ls-coreA slice");
}
std::pair<lsp::lsproto::Location, spanmap::Fidelity>
LanguageService::sourceFileRangeToLSPLocationForFeature(SourceFile* file, TextRange fileRange,
													  spanmap::Feature feature) {
	TSC_UNREACHABLE("sourceFileRangeToLSPLocationForFeature — owned by ls-coreA slice");
}
std::pair<lsp::lsproto::Location, spanmap::Fidelity> LanguageService::getMappedLocation(
	std::string_view fileName, TextRange fileRange) {
	TSC_UNREACHABLE("getMappedLocation — owned by ls-coreA slice");
}
script* LanguageService::getScript(std::string_view fileName) {
	TSC_UNREACHABLE("getScript — owned by ls-coreA slice");
}
sourcemap::DocumentPosition* LanguageService::tryGetSourcePosition(std::string_view fileName,
																   TextPos position) {
	TSC_UNREACHABLE("tryGetSourcePosition — owned by ls-coreA slice");
}
sourcemap::DocumentPosition* LanguageService::tryGetSourcePositionWorker(std::string_view fileName,
																	   TextPos position) {
	TSC_UNREACHABLE("tryGetSourcePositionWorker — owned by ls-coreA slice");
}
sourcemap::DocumentPosition* LanguageService::tryGetGeneratedPosition(std::string_view fileName,
																	  TextPos position) {
	TSC_UNREACHABLE("tryGetGeneratedPosition — owned by ls-coreA slice");
}
sourcemap::DocumentPosition* LanguageService::tryGetGeneratedPositionWorker(std::string_view fileName,
																		  TextPos position) {
	TSC_UNREACHABLE("tryGetGeneratedPositionWorker — owned by ls-coreA slice");
}
std::pair<lsp::lsproto::Range, spanmap::Fidelity> LanguageService::createLspRangeFromNode(
	::tsc::Node* node, SourceFile* file) {
	TSC_UNREACHABLE("createLspRangeFromNode — owned by ls-coreA slice");
}
std::pair<lsp::lsproto::Range, spanmap::Fidelity> LanguageService::createLspRangeFromNodeForFeature(
	::tsc::Node* node, SourceFile* file, spanmap::Feature feature) {
	TSC_UNREACHABLE("createLspRangeFromNodeForFeature — owned by ls-coreA slice");
}
std::pair<lsp::lsproto::Range, spanmap::Fidelity> LanguageService::createLspRangeFromBounds(
	int start, int end, SourceFile* file) {
	TSC_UNREACHABLE("createLspRangeFromBounds — owned by ls-coreA slice");
}
std::pair<lsp::lsproto::Range, spanmap::Fidelity> LanguageService::createLspRangeFromRange(
	TextRange textRange, script* s) {
	TSC_UNREACHABLE("createLspRangeFromRange — owned by ls-coreA slice");
}
std::pair<lsp::lsproto::Position, spanmap::Fidelity> LanguageService::createLspPosition(
	int position, SourceFile* file) {
	TSC_UNREACHABLE("createLspPosition — owned by ls-coreA slice");
}

// --- findallreferences.go — owned by ls-coreB ---
lsp::lsproto::Range LanguageService::getRangeOfEntry(ReferenceEntry* entry) {
	TSC_UNREACHABLE("getRangeOfEntry — owned by ls-coreB slice");
}
std::pair<lsp::lsproto::Range, bool> LanguageService::getRangeOfEntryForFeature(
	ReferenceEntry* entry, spanmap::Feature feature) {
	TSC_UNREACHABLE("getRangeOfEntryForFeature — owned by ls-coreB slice");
}
lsp::lsproto::DocumentUri LanguageService::getFileNameOfEntry(ReferenceEntry* entry) {
	TSC_UNREACHABLE("getFileNameOfEntry — owned by ls-coreB slice");
}
std::pair<lsp::lsproto::Location, bool> LanguageService::getLocationOfEntryForFeature(
	ReferenceEntry* entry, spanmap::Feature feature) {
	TSC_UNREACHABLE("getLocationOfEntryForFeature — owned by ls-coreB slice");
}
void LanguageService::resolveEntrySource(ReferenceEntry* entry) {
	TSC_UNREACHABLE("resolveEntrySource — owned by ls-coreB slice");
}
ReferenceEntry* LanguageService::resolveEntry(ReferenceEntry* entry) {
	TSC_UNREACHABLE("resolveEntry — owned by ls-coreB slice");
}
nonLocalDefinition* LanguageService::getNonLocalDefinition(gostd::Context ctx,
														   SymbolAndEntries* entry) {
	TSC_UNREACHABLE("getNonLocalDefinition — owned by ls-coreB slice");
}
std::pair<SymbolAndEntriesData, bool> LanguageService::provideSymbolsAndEntries(
	gostd::Context ctx, lsp::lsproto::DocumentUri uri, lsp::lsproto::Position documentPosition,
	bool isRename, bool implementations) {
	TSC_UNREACHABLE("provideSymbolsAndEntries — owned by ls-coreB slice");
}
std::pair<SymbolAndEntriesData, bool> LanguageService::provideSymbolsAndEntriesAtPosition(
	gostd::Context ctx, compiler::SimpleProgram* program, SourceFile* sourceFile, int position,
	bool isRename, bool implementations) {
	TSC_UNREACHABLE("provideSymbolsAndEntriesAtPosition — owned by ls-coreB slice");
}
SymbolAndEntriesData LanguageService::getSymbolAndEntries(gostd::Context ctx,
														  SourceFile* sourceFile, int position,
														  bool isRename, bool implementations) {
	TSC_UNREACHABLE("getSymbolAndEntries — owned by ls-coreB slice");
}
std::vector<SymbolAndEntries*> LanguageService::GetReferencedSymbolsForNode(
	gostd::Context ctx, int pos, ::tsc::Node* node, std::vector<SourceFile*> sourceFiles) {
	TSC_UNREACHABLE("GetReferencedSymbolsForNode — owned by ls-coreB slice");
}
std::vector<ReferenceEntry*> LanguageService::getReferencedSymbolsForSymbol(
	gostd::Context ctx, Symbol* symbol, std::vector<::tsc::Node*> excludeDeclaration,
	SourceFile* sourceFile, std::vector<SourceFile*> sourceFiles) {
	TSC_UNREACHABLE("getReferencedSymbolsForSymbol — owned by ls-coreB slice");
}
lsp::lsproto::ReferencesResponse LanguageService::provideReferencesFromData(
	gostd::Context ctx, lsp::lsproto::ReferenceParams* params,
	CrossProjectOrchestrator* orchestrator, SymbolAndEntriesData data) {
	TSC_UNREACHABLE("provideReferencesFromData — owned by ls-coreB slice");
}
lsp::lsproto::ImplementationResponse LanguageService::provideImplementationsFromData(
	gostd::Context ctx, lsp::lsproto::ImplementationParams* params,
	symbolEntryTransformOptions options, CrossProjectOrchestrator* orchestrator,
	SymbolAndEntriesData data) {
	TSC_UNREACHABLE("provideImplementationsFromData — owned by ls-coreB slice");
}
std::pair<SourceFile*, TextPos> getFileAndStartPosFromDeclaration(::tsc::Node* declaration) {
	TSC_UNREACHABLE("getFileAndStartPosFromDeclaration — owned by ls-coreB slice");
}

// --- utilities.go — owned by ls-coreA ---
CommentRange* isInComment(SourceFile* file, int position, ::tsc::Node* tokenAtPosition) {
	TSC_UNREACHABLE("isInComment — owned by ls-coreA slice");
}
bool isLiteralNameOfPropertyDeclarationOrIndexAccess(::tsc::Node* node) {
	TSC_UNREACHABLE("isLiteralNameOfPropertyDeclarationOrIndexAccess — owned by ls-coreA slice");
}
bool isObjectBindingElementWithoutPropertyName(::tsc::Node* bindingElement) {
	TSC_UNREACHABLE("isObjectBindingElementWithoutPropertyName — owned by ls-coreA slice");
}
::tsc::Node* getAdjustedLocation(::tsc::Node* node, bool forRename, SourceFile* sourceFile) {
	TSC_UNREACHABLE("getAdjustedLocation — owned by ls-coreA slice");
}
SemanticMeaning getMeaningFromLocation(::tsc::Node* node) {
	TSC_UNREACHABLE("getMeaningFromLocation — owned by ls-coreA slice");
}
::tsc::Node* getTargetLabel(::tsc::Node* referenceNode, std::string_view labelName) {
	TSC_UNREACHABLE("getTargetLabel — owned by ls-coreA slice");
}
void getLeadingCommentRangesOfNode(::tsc::Node* node, SourceFile* file,
								   const std::function<bool(const CommentRange&)>& yield) {
	TSC_UNREACHABLE("getLeadingCommentRangesOfNode — owned by ls-coreA slice");
}
std::vector<::tsc::Node*> getChildrenFromNonJSDocNode(::tsc::Node* node, SourceFile* sourceFile) {
	TSC_UNREACHABLE("getChildrenFromNonJSDocNode — owned by ls-coreA slice");
}
::tsc::Node* getContainingObjectLiteralElement(::tsc::Node* node) {
	TSC_UNREACHABLE("getContainingObjectLiteralElement — owned by ls-coreA slice");
}
TextRange* toContextRange(TextRange* textRange, SourceFile* contextFile, ::tsc::Node* context) {
	TSC_UNREACHABLE("toContextRange — owned by ls-coreA slice");
}
checker::Type* getContextualTypeFromParentOrAncestorTypeNode(::tsc::Node* node,
															 checker::Checker* typeChecker) {
	TSC_UNREACHABLE("getContextualTypeFromParentOrAncestorTypeNode — owned by ls-coreA slice");
}
TextRange createRangeFromNode(::tsc::Node* node, SourceFile* file) {
	TSC_UNREACHABLE("createRangeFromNode — owned by ls-coreA slice");
}
TextRange getRangeOfNode(::tsc::Node* node, SourceFile* file, ::tsc::Node* endNode) {
	TSC_UNREACHABLE("getRangeOfNode — owned by ls-coreB slice");
}
::tsc::Node* getContextNode(::tsc::Node* node) {
	TSC_UNREACHABLE("getContextNode — owned by ls-coreB slice");
}
refInfo* getReferenceAtPosition(SourceFile* sourceFile, int position,
								compiler::SimpleProgram* program) {
	TSC_UNREACHABLE("getReferenceAtPosition — owned by ls-coreA slice");
}

// --- jsdoc.go — owned by ls-coreA ---
::tsc::Node* getJSDocOrTag(checker::Checker* c, ::tsc::Node* node,
						  collections::Set<Symbol*>* seenSymbols) {
	TSC_UNREACHABLE("getJSDocOrTag — owned by ls-coreA slice");
}

// --- displaypartswriter.go:19 — ported (unlisted sibling file; needed by
// hover.go). Implements printer::EmitTextWriter. ---
void displayPartsWriter::addRun(lsp::lsproto::ClassificationTypeName classification,
								std::string_view text) {
	if (text.empty()) {
		return;
	}
	if (vsCapability) {
		auto* run = new lsp::lsproto::VSClassifiedTextRun;
		run->ClassificationTypeName = classification;
		run->Text = std::string(text);
		runs.push_back(run);
	}
	lastWritten = std::string(text);
	builder += text;
}

// WriteClassified writes text with an explicit classification type.
void displayPartsWriter::WriteClassified(std::string_view text,
										 lsp::lsproto::ClassificationTypeName classification) {
	addRun(classification, text);
}

// WriteFrom copies the accumulated content from another displayPartsWriter.
void displayPartsWriter::WriteFrom(displayPartsWriter* other) {
	builder += other->String();
	if (vsCapability) {
		auto otherRuns = other->GetRuns();
		runs.insert(runs.end(), otherRuns.begin(), otherRuns.end());
	}
	if (!other->lastWritten.empty()) {
		lastWritten = other->lastWritten;
	}
}

std::vector<lsp::lsproto::VSClassifiedTextRun*> displayPartsWriter::GetRuns() {
	return runs;
}

std::string displayPartsWriter::String() {
	return builder;
}

void displayPartsWriter::Clear() {
	lastWritten.clear();
	builder.clear();
	runs.clear();
}

int displayPartsWriter::GetTextPos() {
	return int(builder.size());
}

bool displayPartsWriter::HasTrailingWhitespace() {
	if (builder.empty()) {
		return false;
	}
	int w = 0;
	char32_t ch = decodeLastUtf8Rune(lastWritten, &w);
	if (w == 0) {
		return false;
	}
	return isWhiteSpaceLike(ch);
}

bool displayPartsWriter::IsAtStartOfLine() { return false; }

bool displayPartsWriter::HasTrailingComment() { return false; }

int displayPartsWriter::GetLine() { return 0; }

TextPos displayPartsWriter::GetColumn() { return TextPos(0); }

int displayPartsWriter::GetIndent() { return 0; }

void displayPartsWriter::RawWrite(const std::string& s) {
	addRun(lsp::lsproto::ClassificationTypeNameText, s);
}

void displayPartsWriter::Write(const std::string& s) {
	addRun(lsp::lsproto::ClassificationTypeNameText, s);
}

void displayPartsWriter::WriteComment(const std::string& text) {
	// Strada's writeComment uses unknownWrite -> SymbolDisplayPartKind.text -> "text"
	addRun(lsp::lsproto::ClassificationTypeNameText, text);
}

void displayPartsWriter::WriteKeyword(const std::string& text) {
	addRun(lsp::lsproto::ClassificationTypeNameKeyword, text);
}

void displayPartsWriter::WriteLine() {
	addRun(lsp::lsproto::ClassificationTypeNameWhiteSpace, " ");
}

void displayPartsWriter::WriteLineForce(bool force) {
	addRun(lsp::lsproto::ClassificationTypeNameWhiteSpace, " ");
}

void displayPartsWriter::WriteLiteral(const std::string& s) {
	// Strada's writeLiteral -> SymbolDisplayPartKind.stringLiteral -> "string"
	addRun(lsp::lsproto::ClassificationTypeNameString, s);
}

void displayPartsWriter::WriteOperator(const std::string& text) {
	addRun(lsp::lsproto::ClassificationTypeNameOperator, text);
}

void displayPartsWriter::WriteParameter(const std::string& text) {
	addRun(lsp::lsproto::ClassificationTypeNameParameterName, text);
}

void displayPartsWriter::WriteProperty(const std::string& text) {
	addRun(lsp::lsproto::ClassificationTypeNamePropertyName, text);
}

void displayPartsWriter::WritePunctuation(const std::string& text) {
	addRun(lsp::lsproto::ClassificationTypeNamePunctuation, text);
}

void displayPartsWriter::WriteSpace(const std::string& text) {
	addRun(lsp::lsproto::ClassificationTypeNameWhiteSpace, text);
}

void displayPartsWriter::WriteStringLiteral(const std::string& text) {
	addRun(lsp::lsproto::ClassificationTypeNameString, text);
}

namespace {

// displaypartswriter.go:208 — isFirstDeclarationOfSymbolParameter
bool isFirstDeclarationOfSymbolParameter(::tsc::Symbol* symbol) {
	if (symbol->declarations.empty()) {
		return false;
	}
	return symbol->declarations[0]->kind == Kind::Parameter;
}

// displaypartswriter.go:167 — classificationForSymbol. Determines the Roslyn
// classification type name based on a symbol's flags. Matches the Strada
// translation chain: displayPartKind() -> GetClassificationName().
lsp::lsproto::ClassificationTypeName classificationForSymbol(::tsc::Symbol* symbol) {
	if (symbol == nullptr) {
		return lsp::lsproto::ClassificationTypeNameText;
	}
	SymbolFlags flags = symbol->flags;
	if ((flags & SymbolFlagsVariable) != 0) {
		if (isFirstDeclarationOfSymbolParameter(symbol)) {
			return lsp::lsproto::ClassificationTypeNameParameterName;
		}
		return lsp::lsproto::ClassificationTypeNameLocalName;
	}
	if ((flags & SymbolFlagsProperty) != 0) {
		return lsp::lsproto::ClassificationTypeNamePropertyName;
	}
	if ((flags & SymbolFlagsGetAccessor) != 0) {
		return lsp::lsproto::ClassificationTypeNamePropertyName;
	}
	if ((flags & SymbolFlagsSetAccessor) != 0) {
		return lsp::lsproto::ClassificationTypeNamePropertyName;
	}
	if ((flags & SymbolFlagsEnumMember) != 0) {
		return lsp::lsproto::ClassificationTypeNameFieldName;
	}
	if ((flags & SymbolFlagsFunction) != 0) {
		return lsp::lsproto::ClassificationTypeNameMethodName;
	}
	if ((flags & SymbolFlagsClass) != 0) {
		return lsp::lsproto::ClassificationTypeNameClassName;
	}
	if ((flags & SymbolFlagsInterface) != 0) {
		return lsp::lsproto::ClassificationTypeNameInterfaceName;
	}
	if ((flags & SymbolFlagsEnum) != 0) {
		return lsp::lsproto::ClassificationTypeNameEnumName;
	}
	if ((flags & SymbolFlagsModule) != 0) {
		return lsp::lsproto::ClassificationTypeNameModuleName;
	}
	if ((flags & SymbolFlagsMethod) != 0) {
		return lsp::lsproto::ClassificationTypeNameMethodName;
	}
	if ((flags & SymbolFlagsTypeParameter) != 0) {
		return lsp::lsproto::ClassificationTypeNameTypeParameterName;
	}
	if ((flags & SymbolFlagsTypeAlias) != 0) {
		return lsp::lsproto::ClassificationTypeNameIdentifier;
	}
	if ((flags & SymbolFlagsAlias) != 0) {
		return lsp::lsproto::ClassificationTypeNameIdentifier;
	}
	return lsp::lsproto::ClassificationTypeNameText;
}

} // namespace

void displayPartsWriter::WriteSymbol(const std::string& text,
									 Symbol* symbol) {
	lsp::lsproto::ClassificationTypeName classification =
		classificationForSymbol(symbol);
	addRun(classification, text);
}

void displayPartsWriter::WriteTrailingSemicolon(const std::string& text) {
	addRun(lsp::lsproto::ClassificationTypeNamePunctuation, text);
}

displayPartsWriter* newDisplayPartsWriter(bool vsCapability) {
	auto* w = new displayPartsWriter;
	w->vsCapability = vsCapability;
	return w;
}

// --- hovericon.go — owned by ls-coreA ---
lsp::lsproto::VSImageId* getVSHoverImageId(lsutil::ScriptElementKind kind,
										 lsutil::ScriptElementKindModifier modifiers) {
	TSC_UNREACHABLE("getVSHoverImageId — owned by ls-coreA slice");
}
lsp::lsproto::VSContainerElement* buildVSHoverRawContent(
	lsp::lsproto::VSImageId* imageId,
	std::vector<lsp::lsproto::VSClassifiedTextRun*> quickInfoRuns,
	std::vector<lsp::lsproto::VSClassifiedTextRun*> documentationRuns) {
	TSC_UNREACHABLE("buildVSHoverRawContent — owned by ls-coreA slice");
}

// --- completions.go — owned by ls-coreA ---
int32_t* supplementalFileIndex(SourceFile* file) {
	TSC_UNREACHABLE("supplementalFileIndex — owned by ls-coreA slice");
}
SourceFile* sourceFileForSupplementalFileIndex(SourceFile* file, int32_t* index) {
	TSC_UNREACHABLE("sourceFileForSupplementalFileIndex — owned by ls-coreA slice");
}
// --- completions.go — owned by ls-coreA ---
int getLineEndOfPosition(SourceFile* file, int pos) {
	TSC_UNREACHABLE("getLineEndOfPosition — owned by ls-coreA slice");
}
// --- utilities.go — owned by ls-coreA ---
::tsc::Node* getContainerNode(::tsc::Node* node) {
	TSC_UNREACHABLE("getContainerNode — owned by ls-coreA slice");
}
// --- crossproject.go — owned by ls-coreB ---
lsp::lsproto::WorkspaceEditOrNull combineRenameResponse(
	std::function<void(std::function<bool(lsp::lsproto::WorkspaceEditOrNull)>)> results) {
	TSC_UNREACHABLE("combineRenameResponse — owned by ls-coreB slice");
}
// === end dep stubs ===

} // namespace tsc::ls

namespace tsc::lsp::lsproto {

// === dep stubs — removed when owner slice lands ===
// lsp.go:17 — DocumentUri.FileName (lsp slice)
std::string DocumentUri::FileName() const {
	TSC_UNREACHABLE("DocumentUri::FileName — owned by lsp slice");
}
// lsp.go:293 — GetClientCapabilities. gostd::Context cannot carry values, so
// this always takes the Go code's empty-caps path (real port, not a stub).
const ResolvedClientCapabilities* GetClientCapabilities(gostd::Context ctx) {
	static const ResolvedClientCapabilities empty;
	return &empty;
}
// === end dep stubs ===

} // namespace tsc::lsp::lsproto

namespace tsc::compiler {

// === slice: ls-coreC ===
// program.go GetTypeChecker(ctx) — single checker + no-op release.
std::pair<checker::Checker*, std::function<void()>> SimpleProgram::GetTypeChecker(
	const gostd::Context& ctx) {
	return GetTypeCheckerForFileExclusive(nullptr);
}
// === end slice: ls-coreC ===

} // namespace tsc::compiler
