// parsebench — Go-side in-process parse benchmark, matching `tscpp bench-parse`.
// Usage: parsebench <file> [iters]
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
	"github.com/microsoft/TypeScript/tsc/internal/core"
	"github.com/microsoft/TypeScript/tsc/internal/parser"
	"github.com/microsoft/TypeScript/tsc/internal/tspath"
	"github.com/microsoft/TypeScript/tsc/internal/vfs/osvfs"
)

func parseOne(name string, src []byte) {
	fileName := tspath.ToRootedFilePath(name, tspath.RootedDirectoryPathFromAbsolute("/"))
	parser.ParseSourceFile(
		ast.SourceFileParseOptions{FileName: fileName, PathKey: osvfs.FS().CaseSensitivity().PathKey(tspath.RootedPath(fileName))},
		string(src), core.EnsureScriptKindFromFileName(fileName))
}

func main() {
	src, err := os.ReadFile(os.Args[1])
	if err != nil && len(os.Args) > 2 && os.Args[1] == "-dir" {
		// fallthrough to dir mode below
	}
	if len(os.Args) > 2 && os.Args[1] == "-dir" {
		var total int64
		count := 0
		t0 := time.Now()
		err := filepath.Walk(os.Args[2], func(path string, info os.FileInfo, err error) error {
			if err != nil || info.IsDir() {
				return nil
			}
			ext := strings.ToLower(filepath.Ext(path))
			switch ext {
			case ".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs":
				// process below
			default:
				return nil
			}
			b, err := os.ReadFile(path)
			if err != nil {
				return nil
			}
			parseOne(path, b)
			total += int64(len(b))
			count++
			return nil
		})
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(2)
		}
		ms := float64(time.Since(t0)) / 1e6
		mb := float64(total) / (1024 * 1024)
		fmt.Printf("go parse-all: %d files, %.1f MB: %.1f ms, %.1f MB/s\n",
			count, mb, ms, mb*1000/ms)
		return
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	iters := 5
	if len(os.Args) > 2 {
		if _, err := fmt.Sscanf(os.Args[2], "%d", &iters); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(2)
		}
	}
	best := 1e30
	for range iters {
		t0 := time.Now()
		fileName := tspath.ToRootedFilePath(os.Args[1], tspath.RootedDirectoryPathFromAbsolute("/"))
		file := parser.ParseSourceFile(
			ast.SourceFileParseOptions{FileName: fileName, PathKey: osvfs.FS().CaseSensitivity().PathKey(tspath.RootedPath(fileName))},
			string(src), core.EnsureScriptKindFromFileName(fileName))
		_ = file
		ms := float64(time.Since(t0)) / 1e6
		if ms < best {
			best = ms
		}
	}
	mb := float64(len(src)) / (1024 * 1024)
	fmt.Printf("go parse: %d bytes, best %d/%d: %.2f ms, %.1f MB/s\n",
		len(src), iters, iters, best, mb*1000/best)
}
