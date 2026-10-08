// Same-package test access for ported Go ls unit tests. Go tests live in
// `package ls`, so they can build `&LanguageService{...}` field literals
// (bypassing the host-derived NewLanguageService wiring) and call unexported
// methods. LSTestAccess is the friend declared on LanguageService; all
// accessors are static.
#pragma once

#include "internal/ls/ls.h"

namespace tsc::ls {

struct LSTestAccess {
	// `&LanguageService{host: h}` — fields set directly, ctor skipped.
	static LanguageService* NewWithHost(Host* host) {
		auto* l = new LanguageService();
		l->host = host;
		return l;
	}
	// `&LanguageService{}` — the Go zero value.
	static LanguageService* NewEmpty() { return new LanguageService(); }
	// `&LanguageService{program: p, converters: c}`.
	static LanguageService* NewWithProgramAndConverters(
	    compiler::SimpleProgram* program, lsconv::Converters* converters) {
		auto* l = new LanguageService();
		l->program = program;
		l->converters = converters;
		return l;
	}
	// `&LanguageService{converters: c}`.
	static LanguageService* NewWithConverters(lsconv::Converters* converters) {
		auto* l = new LanguageService();
		l->converters = converters;
		return l;
	}

	static pathUpdater createPathUpdater(LanguageService* l,
	                                     const std::string& oldPath,
	                                     const std::string& newPath) {
		return l->createPathUpdater(oldPath, newPath);
	}
	static std::vector<TextChange> getFormattingEditsForRange(
	    LanguageService* l, gostd::Context ctx, SourceFile* file,
	    lsutil::FormatCodeSettings options, TextRange r) {
		return l->getFormattingEditsForRange(ctx, file, options, r);
	}
	static std::vector<TextChange> getFormattingEditsAfterKeystroke(
	    LanguageService* l, gostd::Context ctx, SourceFile* file,
	    lsutil::FormatCodeSettings options, int position,
	    std::string key) {
		return l->getFormattingEditsAfterKeystroke(ctx, file, options,
		                                           position, key);
	}
	static std::pair<SymbolAndEntriesData, bool> provideSymbolsAndEntries(
	    LanguageService* l, const gostd::Context& ctx,
	    lsproto::DocumentUri uri, lsproto::Position documentPosition,
	    bool isRename, bool implementations) {
		return l->provideSymbolsAndEntries(ctx, uri, documentPosition,
		                                   isRename, implementations);
	}
};

}  // namespace tsc::ls
