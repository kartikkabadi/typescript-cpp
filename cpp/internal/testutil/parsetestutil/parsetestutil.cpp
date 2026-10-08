// parsetestutil.cpp — port of tsc/internal/testutil/parsetestutil/
// parsetestutil.go.
#include "internal/testutil/parsetestutil/parsetestutil.h"

#include <sstream>
#include <vector>

#include "internal/ast/visitor.h"
#include "internal/core/types.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/parser/parser.h"
#include "internal/tspath/tspath.h"

namespace tsc::testutil::parsetestutil {

// ParseTypeScript — parsetestutil.go:15.
SourceFile* ParseTypeScript(std::string_view text, bool jsx) {
	std::string fileName = jsx ? "/main.tsx" : "/main.ts";
	SourceFile* file = tsc::parseSourceFile(
	    SourceFileParseOptions{
	        .FileName = fileName,
	        .Path = tspath::Path(fileName),
	    },
	    text, getScriptKindFromFileName(fileName));
	return file;
}

namespace {

// formatParseDiagnostics — the shared body of CheckDiagnostics /
// CheckDiagnosticsMessage (parsetestutil.go:28-33, 40-44).
std::string formatParseDiagnostics(SourceFile* file) {
	std::ostringstream b;
	auto diags = diagnosticwriter::fromASTDiagnostics(file->diagnostics);
	std::vector<diagnosticwriter::Diagnostic*> ptrs;
	ptrs.reserve(diags.size());
	for (auto& d : diags) {
		ptrs.push_back(d.get());
	}
	diagnosticwriter::FormattingOptions opts;
	opts.newLine = "\n";
	diagnosticwriter::writeFormatDiagnostics(b, ptrs, &opts);
	return b.str();
}

// newSyntheticRecursiveVisitor — parsetestutil.go:48.
NodeVisitor* newSyntheticRecursiveVisitor() {
	NodeVisitorHooks hooks;
	hooks.visitNode = [](Node* node, NodeVisitor* v) -> Node* {
		if (node != nullptr) {
			node->loc = TextRange::undefined();
		}
		return v->visitNode(node);
	};
	hooks.visitToken = [](Node* node, NodeVisitor* v) -> Node* {
		if (node != nullptr) {
			node->loc = TextRange::undefined();
		}
		return v->visitNode(node);
	};
	hooks.visitNodes = [](NodeList* nodes, NodeVisitor* v) -> NodeList* {
		if (nodes != nullptr) {
			nodes->loc = TextRange::undefined();
		}
		return v->visitNodes(nodes);
	};
	hooks.visitModifiers = [](ModifierList* nodes,
	                          NodeVisitor* v) -> ModifierList* {
		if (nodes != nullptr) {
			nodes->loc = TextRange::undefined();
		}
		return v->visitModifiers(nodes);
	};
	// Go passes &ast.NodeFactory{}; nullptr makes the visitor own a
	// default-constructed one — the same thing. The visit callback closes
	// over `v`, so assign it after the visitor exists.
	NodeVisitor* v = newNodeVisitor(nullptr /*visit*/,
	                                nullptr /*factory*/, hooks);
	v->visit = [v](Node* node) -> Node* {
		return v->visitEachChild(node);
	};
	return v;
}

}  // namespace

// CheckDiagnostics — parsetestutil.go:25.
void CheckDiagnostics(gostd::testing::T* t, SourceFile* file) {
	t->Helper();
	if (!file->diagnostics.empty()) {
		t->Error({formatParseDiagnostics(file)});
	}
}

// CheckDiagnosticsMessage — parsetestutil.go:37.
void CheckDiagnosticsMessage(gostd::testing::T* t, SourceFile* file,
                             std::string_view message) {
	t->Helper();
	if (!file->diagnostics.empty()) {
		t->Error({std::string(message) + formatParseDiagnostics(file)});
	}
}

// MarkSyntheticRecursive — parsetestutil.go:86.
void MarkSyntheticRecursive(Node* node) {
	newSyntheticRecursiveVisitor()->visitNode(node);
}

}  // namespace tsc::testutil::parsetestutil
