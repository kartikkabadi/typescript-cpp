#!/usr/bin/env python3
"""Generate a deterministic ~100-file interdependent TS project.

Layout: <out>/src/mod000.ts ... mod099.ts, each ~200 lines.
Module i imports only from modules j < i, so any prefix of the file
list is a closed dependency set (used by bench_scaling.py).

Also emits tsconfig.json and tsconfig_1/10/50/100.json ("files" lists).
"""
import json
import os
import random
import sys

N = int(os.environ.get("PROJ_FILES", "100"))
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/perfproj"
SEED = 12345


def gen_file(i, rng):
    lines = []
    # Imports: 2-4 earlier modules (keeps prefix subsets closed).
    deps = sorted(rng.sample(range(i), min(i, rng.randint(2, 4)))) if i else []
    for d in deps:
        lines.append(f'import {{ Thing{d}, make{d}, CONFIG_{d} }} from '
                     f'"./mod{d:03d}";')
    lines.append("")
    if deps:
        uses = ", ".join(f"typeof make{d}" for d in deps[:3])
        lines.append(f"type FactoryChecks = [{uses}];")
        lines.append("")

    lines.append(f"export const CONFIG_{i} = {{ id: {i}, name: \"mod{i:03d}\","
                 f" tags: [\"a\", \"b\", \"{i}\"] }} as const;")
    lines.append("")
    lines.append(f"export interface Shape{i} {{")
    lines.append(f"  id: number;")
    lines.append(f"  name: string;")
    lines.append(f"  payload: Record<string, unknown>;")
    lines.append(f"  next?: Shape{i};")
    lines.append("}")
    lines.append("")
    lines.append(f"export type Result{i}<T, E = Error> =")
    lines.append(f"  | {{ ok: true; value: T }}")
    lines.append(f"  | {{ ok: false; error: E }};")
    lines.append("")
    lines.append(f"export type Predicate{i}<T> = (x: T) => boolean;")
    lines.append("")
    lines.append(f"function logged{i}() {{ return (_t: any, _k: string,"
                 " d: PropertyDescriptor) => d; }")
    lines.append("")
    lines.append(f"export class Thing{i} {{")
    lines.append(f"  private state = new Map<string, number[]>();")
    lines.append(f"  readonly kind = {i};")
    lines.append("")
    lines.append(f"  @logged{i}()")
    lines.append(f"  compute<T extends number | string>(input: T): "
                 f"Result{i}<T> {{")
    lines.append(f"    const acc = this.state.get(\"k\") ?? [1, 2, 3];")
    lines.append(f"    if (typeof input === \"number\" && acc.length > 0) {{")
    lines.append(f"      this.state.set(\"k\", acc.concat(input));")
    lines.append(f"      return {{ ok: true, value: input }};")
    lines.append("    }")
    lines.append(f"    return {{ ok: false, error: new Error(\"bad\") }};")
    lines.append("  }")
    lines.append("")
    lines.append(f"  async fetchAll(urls: string[]): Promise<number[]> {{")
    lines.append(f"    const out: number[] = [];")
    lines.append(f"    for (const u of urls) {{")
    lines.append(f"      out.push(u.length + this.kind);")
    lines.append("    }")
    lines.append("    return out;")
    lines.append("  }")
    lines.append("}")
    lines.append("")
    lines.append(f"export enum Mode{i} {{ Off, On, Auto }}")
    lines.append("")
    lines.append(f"export function make{i}(m: Mode{i} = Mode{i}.Auto): "
                 f"Thing{i} | null {{")
    lines.append(f"  switch (m) {{")
    lines.append(f"    case Mode{i}.On: return new Thing{i}();")
    lines.append(f"    case Mode{i}.Off: return null;")
    lines.append(f"    default: return Math.random() > -1 ? new Thing{i}()"
                 " : null;")
    lines.append("  }")
    lines.append("}")
    lines.append("")
    # Pad to ~200 lines with varied declarations.
    filler = 0
    while len(lines) < 195:
        filler += 1
        lines.append(f"export type Mapped{i}_{filler}<T> = "
                     f"{{ [K in keyof T]: T[K] extends number ? string : "
                     f"T[K] }};")
        lines.append(f"export const fn{i}_{filler} = <T,>(x: T): T[] =>"
                     f" Array(Math.max(1, {filler})).fill(x);")
        lines.append(f"export interface Iface{i}_{filler}<A, B = A> "
                     f"extends Shape{i} {{ extra: A | B; "
                     f"mode: Mode{i} }}")
        lines.append("")
    return "\n".join(lines) + "\n"


def main():
    rng = random.Random(SEED)
    src = os.path.join(OUT, "src")
    os.makedirs(src, exist_ok=True)
    names = []
    for i in range(N):
        name = f"mod{i:03d}.ts"
        names.append(f"src/{name}")
        with open(os.path.join(src, name), "w") as f:
            f.write(gen_file(i, rng))

    base_opts = {
        "strict": True,
        "target": "es2020",
        "module": "commonjs",
        "experimentalDecorators": True,
        "skipLibCheck": True,
        "rootDir": "src",
        "forceConsistentCasingInFileNames": True,
        "noEmitOnError": False,
    }
    with open(os.path.join(OUT, "tsconfig.json"), "w") as f:
        json.dump({"compilerOptions": base_opts, "include": ["src/**/*"]},
                  f, indent=2)
    for n in (1, 10, 50, 100):
        if n > N:
            continue
        with open(os.path.join(OUT, f"tsconfig_{n}.json"), "w") as f:
            json.dump({"compilerOptions": base_opts, "files": names[:n]},
                      f, indent=2)
    total = sum(os.path.getsize(os.path.join(src, n)) for n in
                os.listdir(src))
    print(f"generated {N} files, {total} bytes in {src}")


if __name__ == "__main__":
    main()
