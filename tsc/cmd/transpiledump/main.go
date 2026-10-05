package main

import (
	"context"
	"fmt"
	"os"

	"github.com/microsoft/TypeScript/tsc/internal/core"
	"github.com/microsoft/TypeScript/tsc/internal/transpile"
)

// transpiledump: stdin = TS source; stdout = transpile output.
// usage: transpiledump [-decl] < file.ts
func main() {
	decl := len(os.Args) > 1 && os.Args[1] == "-decl"
	src, _ := os.ReadFile("/dev/stdin")
	opts := transpile.Options{CompilerOptions: &core.CompilerOptions{}}
	var out *transpile.Output
	if decl {
		out = transpile.TranspileDeclaration(context.Background(), string(src), opts)
	} else {
		out = transpile.TranspileModule(context.Background(), string(src), opts)
	}
	fmt.Printf("%s\ndiags:%d", out.OutputText, len(out.Diagnostics))
	for _, d := range out.Diagnostics {
		fmt.Printf("\n%d %s", d.Code(), d.String())
	}
}
