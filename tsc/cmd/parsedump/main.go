// parsedump — Go oracle AST dumper: identical text shape to `tscpp parse`.
// Emits pre-order "N <kind> <pos> <end> <flags>" per node, then
// "D <code> <pos> <end>" for parse diagnostics, "J ..." for jsDiagnostics,
// "C ..." for jsdocDiagnostics.
package main

import (
	"fmt"
	"os"
	"strings"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
	"github.com/microsoft/TypeScript/tsc/internal/core"
	"github.com/microsoft/TypeScript/tsc/internal/parser"
	"github.com/microsoft/TypeScript/tsc/internal/tspath"
	"github.com/microsoft/TypeScript/tsc/internal/vfs/osvfs"
)

func dumpNode(n *ast.Node, out *strings.Builder) {
	fmt.Fprintf(out, "N %s %d %d %d\n",
		strings.TrimPrefix(n.Kind.String(), "Kind"), n.Pos(), n.End(),
		uint32(n.Flags))
	var visit func(c *ast.Node) bool
	visit = func(c *ast.Node) bool {
		dumpNode(c, out)
		return false
	}
	n.ForEachChild(visit)
}

func main() {
	src, err := os.ReadFile(os.Args[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	fileName := tspath.ToRootedFilePath(os.Args[1], tspath.RootedDirectoryPathFromAbsolute("/"))
	scriptKind := core.EnsureScriptKindFromFileName(fileName)
	file := parser.ParseSourceFile(
		ast.SourceFileParseOptions{FileName: fileName, PathKey: osvfs.FS().CaseSensitivity().PathKey(tspath.RootedPath(fileName))},
		string(src), scriptKind)
	var out strings.Builder
	dumpNode(file.AsNode(), &out)
	for _, d := range file.Diagnostics() {
		fmt.Fprintf(&out, "D %d %d %d\n", d.Code(), d.Pos(), d.End())
	}
	for _, d := range file.JSDiagnostics() {
		fmt.Fprintf(&out, "J %d %d %d\n", d.Code(), d.Pos(), d.End())
	}
	for _, d := range file.JSDocDiagnostics() {
		fmt.Fprintf(&out, "C %d %d %d\n", d.Code(), d.Pos(), d.End())
	}
	fmt.Print(out.String())
}
