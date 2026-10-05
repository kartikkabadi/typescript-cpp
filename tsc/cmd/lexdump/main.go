// lexdump — Go oracle token dumper: identical JSON shape to `tscpp lex-json`.
package main

import (
	"fmt"
	"github.com/microsoft/TypeScript/tsc/internal/json"
	"os"
	"strings"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
	"github.com/microsoft/TypeScript/tsc/internal/core"
	"github.com/microsoft/TypeScript/tsc/internal/diagnostics"
	"github.com/microsoft/TypeScript/tsc/internal/scanner"
)

type tok struct {
	Kind  string `json:"kind"`
	Pos   int    `json:"pos"`
	End   int    `json:"end"`
	Flags uint32 `json:"flags"`
}

func main() {
	src, err := os.ReadFile(os.Args[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	s := scanner.NewScanner()
	s.SetText(string(src))
	s.SetSkipTrivia(false)
	s.SetScriptTarget(core.ScriptTargetLatest)
	s.SetOnError(func(d *diagnostics.Message, pos, length int, args ...any) {
		fmt.Fprintf(os.Stderr, "error TS%d: %d:%d\n", d.Code(), pos, pos+length)
	})
	out := []tok{}
	for {
		t := s.Scan()
		out = append(out, tok{strings.TrimPrefix(t.String(), "Kind"), s.TokenStart(), s.TokenEnd(), uint32(s.TokenFlags())})
		if t == ast.KindEndOfFile {
			break
		}
	}
	enc, err := json.Marshal(out)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	fmt.Println(string(enc))
}
