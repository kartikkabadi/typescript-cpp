// bindump — Go oracle bind dumper: identical text shape to `tscpp bind`.
// Emits, in pre-order:
//
//	N <kind> <pos> <end> <flags>                       — node + post-bind flags
//	S <name> <flags> <ndelecs> <vpos:vend|-> <pname:pflags|->  — node.symbol
//	X <name> <flags>                                   — exportable localSymbol
//	F <flowFlags> <kind pos end|->                     — node's FlowNode
//	E <flowFlags|->                                    — endFlowNode/returnFlowNode
//	L <name:flags;name:flags;...>                      — sorted locals table
//	Q <pos> <end|->                                    — nextContainer
//
// after the tree:
//
//	M <hasCommonJS> <hasExternal>
//	K <symbolCount>
//	P <pattern> <name:flags>                           — per PatternAmbientModule
//	G <name> <flags>                                   — per sorted GlobalExport
//	B <code> <pos> <end>                               — bind diagnostics
package main

import (
	"fmt"
	"os"
	"sort"
	"strings"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
	"github.com/microsoft/TypeScript/tsc/internal/binder"
	"github.com/microsoft/TypeScript/tsc/internal/core"
	"github.com/microsoft/TypeScript/tsc/internal/parser"
	"github.com/microsoft/TypeScript/tsc/internal/tspath"
	"github.com/microsoft/TypeScript/tsc/internal/vfs/osvfs"
)

func symLine(s *ast.Symbol, out *strings.Builder) {
	vpos, vend := -1, -1
	if vd := s.ValueDeclaration(); vd != nil {
		vpos, vend = vd.Pos(), vd.End()
	}
	pname, pflags := "-", uint32(0)
	if p := s.Parent(); p != nil {
		pname = ast.EscapeAllInternalSymbolNames(ast.SymbolName(p))
		pflags = uint32(p.Flags())
	}
	fmt.Fprintf(out, "S %s %d %d %d:%d %s:%d\n",
		ast.EscapeAllInternalSymbolNames(ast.SymbolName(s)),
		uint32(s.Flags()), len(s.Declarations()), vpos, vend, pname, pflags)
}

func dumpNode(n *ast.Node, out *strings.Builder) {
	fmt.Fprintf(out, "N %s %d %d %d\n",
		strings.TrimPrefix(n.Kind.String(), "Kind"), n.Pos(), n.End(),
		uint32(n.Flags))
	if s := n.Symbol(); s != nil {
		symLine(s, out)
	}
	if e := n.ExportableData(); e != nil && e.LocalSymbol != nil {
		fmt.Fprintf(out, "X %s %d\n",
			ast.EscapeAllInternalSymbolNames(ast.SymbolName(e.LocalSymbol)),
			uint32(e.LocalSymbol.Flags()))
	}
	if f := n.FlowNodeData(); f != nil && f.FlowNode != nil {
		fn := f.FlowNode
		if fn.Node != nil {
			fmt.Fprintf(out, "F %d %s %d %d\n", uint32(fn.Flags),
				strings.TrimPrefix(fn.Node.Kind.String(), "Kind"),
				fn.Node.Pos(), fn.Node.End())
		} else {
			fmt.Fprintf(out, "F %d -\n", uint32(fn.Flags))
		}
	}
	if b := n.BodyData(); b != nil && b.EndFlowNode != nil {
		fmt.Fprintf(out, "E %d\n", uint32(b.EndFlowNode.Flags))
	}
	var ret *ast.FlowNode
	switch n.Kind {
	case ast.KindConstructor:
		ret = n.AsConstructorDeclaration().ReturnFlowNode
	case ast.KindFunctionDeclaration:
		ret = n.AsFunctionDeclaration().ReturnFlowNode
	case ast.KindFunctionExpression:
		ret = n.AsFunctionExpression().ReturnFlowNode
	case ast.KindClassStaticBlockDeclaration:
		ret = n.AsClassStaticBlockDeclaration().ReturnFlowNode
	}
	if ret != nil {
		fmt.Fprintf(out, "R %d\n", uint32(ret.Flags))
	}
	{
		l := n.Locals()
		if len(l) > 0 {
			var keys []string
			for k := range l {
				keys = append(keys, k)
			}
			sort.Strings(keys)
			var parts []string
			for _, k := range keys {
				parts = append(parts, fmt.Sprintf("%s:%d",
					ast.EscapeAllInternalSymbolNames(k), uint32(l[k].Flags())))
			}
			fmt.Fprintf(out, "L %s\n", strings.Join(parts, ";"))
		}
	}
	if c := n.LocalsContainerData(); c != nil && c.NextContainer != nil {
		fmt.Fprintf(out, "Q %d %d\n", c.NextContainer.Pos(), c.NextContainer.End())
	}
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
	binder.BindSourceFile(file)
	var out strings.Builder
	dumpNode(file.AsNode(), &out)
	cjs, ext := 0, 0
	if file.CommonJSModuleIndicator != nil {
		cjs = 1
	}
	if file.ExternalModuleIndicator != nil {
		ext = 1
	}
	fmt.Fprintf(&out, "M %d %d\n", cjs, ext)
	fmt.Fprintf(&out, "K %d\n", file.SymbolCount)
	for _, p := range file.PatternAmbientModules {
		fmt.Fprintf(&out, "P %s %s:%d\n", p.Pattern.Text,
			ast.EscapeAllInternalSymbolNames(ast.SymbolName(p.Symbol)),
			uint32(p.Symbol.Flags()))
	}
	var gkeys []string
	for k := range file.GlobalExports {
		gkeys = append(gkeys, k)
	}
	sort.Strings(gkeys)
	for _, k := range gkeys {
		fmt.Fprintf(&out, "G %s %d\n",
			ast.EscapeAllInternalSymbolNames(k),
			uint32(file.GlobalExports[k].Flags()))
	}
	for _, d := range file.BindDiagnostics() {
		fmt.Fprintf(&out, "B %d %d %d\n", d.Code(), d.Pos(), d.End())
	}
	fmt.Print(out.String())
}
