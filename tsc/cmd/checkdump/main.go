// checkdump — Go oracle check dumper: identical text shape to `tscpp check`.
// Runs `tsc --noEmit <args...>` semantics (same ParseCommandLine + program
// pipeline as the real CLI) and emits a canonical, diffable diagnostic dump:
//
//	G <code>                     — diagnostics with no file location
//	F <fileName>                 — per source file that has diagnostics
//	T <code> <pos> <end>         — diagnostics located in that file
//
// G lines are sorted by code, F sections by file name, T lines by
// (pos, end, code). Command-line parse errors (e.g. unknown flags) are
// emitted as C <code> lines and stop the dump, matching the real CLI which
// reports them and exits before building the program.
package main

import (
	"context"
	"fmt"
	"os"
	"sort"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
	"github.com/microsoft/TypeScript/tsc/internal/bundled"
	"github.com/microsoft/TypeScript/tsc/internal/compiler"
	"github.com/microsoft/TypeScript/tsc/internal/tsoptions"
	"github.com/microsoft/TypeScript/tsc/internal/tspath"
	"github.com/microsoft/TypeScript/tsc/internal/vfs/osvfs"
)

func main() {
	ctx := context.Background()
	cwd, err := os.Getwd()
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	fs := bundled.WrapFS(osvfs.FS())
	host := compiler.NewCompilerHost(fs, bundled.LibPath(), nil, nil, nil)

	// `tsc --noEmit <args>` — the same command line the real CLI sees.
	args := append([]string{"--noEmit"}, os.Args[1:]...)
	parsed := tsoptions.ParseCommandLine(args, fs, tspath.RootedDirectoryPathFromAbsolute(cwd))
	if len(parsed.Errors) > 0 {
		for _, e := range parsed.Errors {
			fmt.Printf("C %d\n", e.Code())
		}
		return
	}

	program := compiler.NewProgram(compiler.ProgramOptions{
		Config: parsed,
		Host:   host,
	})
	program.BindSourceFiles()
	bindCollect := func(ctx context.Context, file *ast.SourceFile) []*ast.Diagnostic {
		return program.GetBindDiagnostics(ctx, file)
	}
	semanticCollect := func(ctx context.Context, file *ast.SourceFile) []*ast.Diagnostic {
		return program.GetSemanticDiagnostics(ctx, file)
	}
	diags := compiler.GetDiagnosticsOfAnyProgram(
		ctx, program, nil /* all files */, false, bindCollect, semanticCollect)

	byFile := map[*ast.SourceFile][]*ast.Diagnostic{}
	var global []*ast.Diagnostic
	for _, d := range diags {
		if d.File() != nil {
			byFile[d.File()] = append(byFile[d.File()], d)
		} else {
			global = append(global, d)
		}
	}
	sort.Slice(global, func(i, j int) bool { return global[i].Code() < global[j].Code() })
	for _, d := range global {
		fmt.Printf("G %d\n", d.Code())
	}
	names := make([]string, 0, len(byFile))
	files := map[string]*ast.SourceFile{}
	for f := range byFile {
		names = append(names, string(f.FileName()))
		files[string(f.FileName())] = f
	}
	sort.Strings(names)
	for _, name := range names {
		fmt.Printf("F %s\n", name)
		ds := byFile[files[name]]
		sort.Slice(ds, func(i, j int) bool {
			if ds[i].Pos() != ds[j].Pos() {
				return ds[i].Pos() < ds[j].Pos()
			}
			if ds[i].End() != ds[j].End() {
				return ds[i].End() < ds[j].End()
			}
			return ds[i].Code() < ds[j].Code()
		})
		for _, d := range ds {
			fmt.Printf("T %d %d %d\n", d.Code(), d.Pos(), d.End())
		}
	}
}
