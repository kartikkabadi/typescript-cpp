// scanbench — Go-side in-process scan benchmark, matching `tscpp bench`.
// Usage: scanbench <file> [iters]
package main

import (
	"fmt"
	"os"
	"time"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
	"github.com/microsoft/TypeScript/tsc/internal/core"
	"github.com/microsoft/TypeScript/tsc/internal/scanner"
)

func main() {
	src, err := os.ReadFile(os.Args[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	iters := 5
	if len(os.Args) > 2 {
		fmt.Sscanf(os.Args[2], "%d", &iters)
	}
	best := 1e30
	var tokens int
	for i := 0; i < iters; i++ {
		t0 := time.Now()
		s := scanner.NewScanner()
		s.SetText(string(src))
		s.SetScriptTarget(core.ScriptTargetLatest)
		s.SetSkipTrivia(false)
		tokens = 0
		for s.Scan() != ast.KindEndOfFile {
			tokens++
		}
		ms := float64(time.Since(t0)) / 1e6
		if ms < best {
			best = ms
		}
	}
	mb := float64(len(src)) / (1024 * 1024)
	fmt.Printf("go lex: %d tokens, %d bytes, best %d/%d: %.2f ms, %.1f MB/s\n",
		tokens, len(src), iters, iters, best, mb*1000/best)
}
